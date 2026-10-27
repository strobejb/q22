#include "filemap/filemapbuilder.h"

#include "causeway/parser.h"
#include "structview/strataroot.h"
#include "structview/structuredefinitionmanager.h"
#include "structview/structuretreemodel.h"
#include "structview/structurevaluebuilder.h"

#include <QByteArray>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <map>

namespace filemap
{
namespace {

struct Claim
{
    RegionKind   kind   = RegionKind::Unknown;
    RegionSource source = RegionSource::Gap;
    QString      label;
    QString      path;
};

// Disjoint [start, end) intervals keyed by start. Painting a range replaces
// whatever was there, so painting parents before children (pre-order) leaves
// the most specific claim on every byte.
class IntervalPainter
{
public:
    void paint(uint64_t start, uint64_t end, const Claim &claim)
    {
        if (start >= end)
            return;
        split(start);
        split(end);
        m_segments.erase(m_segments.lower_bound(start), m_segments.lower_bound(end));
        m_segments.emplace(start, Segment{end, claim});
    }

    uint64_t coveredEnd() const
    {
        return m_segments.empty() ? 0 : std::prev(m_segments.end())->second.end;
    }

    template <typename Fn>
    void forEach(Fn fn) const
    {
        for (const auto &[start, segment] : m_segments)
            fn(start, segment.end, segment.claim);
    }

private:
    struct Segment
    {
        uint64_t end;
        Claim    claim;
    };

    // Ensures `at` is a segment boundary.
    void split(uint64_t at)
    {
        auto it = m_segments.upper_bound(at);
        if (it == m_segments.begin())
            return;
        --it;
        if (it->first < at && at < it->second.end)
        {
            Segment tail{it->second.end, it->second.claim};
            it->second.end = at;
            m_segments.emplace(at, std::move(tail));
        }
    }

    std::map<uint64_t, Segment> m_segments;
};

enum class RowShape
{
    Scalar,
    Struct,
    ScalarArray,
    StructArray,
};

Type *skipModifiers(Type *type)
{
    while (type && (type->ty == typeIDENTIFIER || type->ty == typeCONST
                    || type->ty == typeSIGNED || type->ty == typeUNSIGNED))
        type = type->link;
    return type;
}

Tag *findRowTag(const StructureRow *row, TOKEN tok, ExprNode **expr = nullptr)
{
    if (Tag *tag = FindTag(row->tagListOverride, tok, expr))
        return tag;
    return row->typeDecl ? FindTag(row->typeDecl->tagList, tok, expr) : nullptr;
}

// Raw array element rows carry "[n]" in their type prefix (elements past the
// display cap have no rendered name); semantic element rows only in the name.
bool isElementRow(const StructureRow *row)
{
    return row->nameTypePrefix.startsWith(QLatin1Char('[')) || row->name.startsWith(QLatin1Char('['));
}

// Children that belong to the row's own tree: a raw row's semantic children
// are attached summary views, while a semantic row's children are all semantic.
bool sameTree(const StructureRow *row, const StructureRow *child)
{
    return (row->kind == StructureRowKind::Semantic) == (child->kind == StructureRowKind::Semantic);
}

const StructureRow *firstElement(const StructureRow *row)
{
    for (const auto &child : row->children)
        if (sameTree(row, child.get()))
            return isElementRow(child.get()) ? child.get() : nullptr;
    return nullptr;
}

// Raw arrays are declared as such; dynamic arrays (dynamic_array, mapped
// tables) carry the element type instead, so fall back to the "[n]" children.
bool isArrayRow(const StructureRow *row)
{
    const Type *type = skipModifiers(row->type);
    return (type && type->ty == typeARRAY) || firstElement(row);
}

TYPE arrayElementBaseType(const StructureRow *row)
{
    Type *type = skipModifiers(row->type);
    if (type && type->ty == typeARRAY)
        return BaseType(type->link);
    const StructureRow *element = firstElement(row);
    return element ? BaseType(element->type) : typeNULL;
}

RowShape rowShape(const StructureRow *row)
{
    if (isArrayRow(row))
    {
        // Element rows of a struct array carry their own field children; a
        // scalar array either renders no element rows or childless ones.
        for (const auto &child : row->children)
            if (sameTree(row, child.get()) && !child->children.empty())
                return RowShape::StructArray;
        return row->lazyChildLoader ? RowShape::StructArray : RowShape::ScalarArray;
    }
    // bitflag/bitfield expand a scalar into child rows describing its bits.
    if (findRowTag(row, TOK_BITFLAG) || findRowTag(row, TOK_BITFIELD))
        return RowShape::Scalar;
    return row->children.empty() && !row->lazyChildLoader ? RowShape::Scalar : RowShape::Struct;
}


bool isTextFormatted(const StructureRow *row)
{
    if (findRowTag(row, TOK_STRING))
        return true;
    ExprNode *expr = nullptr;
    if (!findRowTag(row, TOK_FORMAT, &expr) || !expr || expr->type != EXPR_STRINGBUF || !expr->str)
        return false;
    const QString format = QString::fromLocal8Bit(expr->str).toLower();
    return format == QLatin1String("string") || format == QLatin1String("ascii")
        || format == QLatin1String("utf8") || format.startsWith(QLatin1String("utf16"));
}

// Raw rows render as "<type> <identifier>"; the identifier alone reads better
// on a map. Semantic rows have no type prefix and are already display names.
QString rowLabel(const StructureRow *row)
{
    if (row->kind == StructureRowKind::Semantic)
        return row->name.trimmed();
    const QString identifier = row->nameIdentifier.trimmed();
    if (isElementRow(row))
        return identifier.isEmpty() ? row->nameTypePrefix : row->nameTypePrefix + QLatin1Char(' ') + identifier;
    return identifier.isEmpty() ? row->name.trimmed() : identifier;
}

QString joinPath(const QString &parentPath, const QString &label)
{
    if (label.isEmpty())
        return parentPath;
    if (parentPath.isEmpty())
        return label;
    // Array elements render as "[3]" or "[3] .text", and section names often
    // start with a dot already; attach those without another one.
    const bool attach = label.startsWith(QLatin1Char('[')) || label.startsWith(QLatin1Char('.'));
    return attach ? parentPath + label : parentPath + QLatin1Char('.') + label;
}

// The engine propagates [code] and nested(...) targets to every descendant
// row so context menus work anywhere inside them; only the outermost row in
// such a subtree is the declaration.
bool inheritsCodeTarget(const StructureRow *row)
{
    const StructureRow *parent = row->parent;
    return parent && parent->hasCodeTarget && parent->codeTargetOffset == row->codeTargetOffset
        && parent->codeByteLength == row->codeByteLength;
}

bool inheritsNestedTarget(const StructureRow *row)
{
    const StructureRow *parent = row->parent;
    return parent && parent->hasOpenAsTarget && parent->openAsOffset == row->openAsOffset
        && parent->openAsByteLength == row->openAsByteLength;
}

bool within(const StructureRow *row, uint64_t offset, uint64_t length)
{
    return offset >= row->absoluteOffset && length <= row->byteLength
        && offset - row->absoluteOffset <= row->byteLength - length;
}

// Whether a record holds a payload of its own (a blob, a nested file, a code
// body) rather than just describing things elsewhere. Arrays of payload
// records are record sequences (PNG chunks, ZIP local entries); arrays of
// payload-free records are tables (section headers, symbols, directories --
// including directories whose entries point at nested files elsewhere).
bool carriesPayload(const StructureRow *row, uint64_t minBlobBytes, int depth = 0)
{
    if (row->kind == StructureRowKind::Semantic)
        return false;
    if (row->hasOpenAsTarget && within(row, row->openAsOffset, row->openAsByteLength))
        return true;
    if (row->hasCodeTarget && within(row, row->codeTargetOffset, row->codeByteLength))
        return true;
    // Byte and character arrays are payload; word/dword arrays are values
    // (relocation entries), which keep a record a table entry.
    if (rowShape(row) == RowShape::ScalarArray)
    {
        const TYPE element = arrayElementBaseType(row);
        const bool payloadElement = element == typeBYTE || element == typeCHAR
            || element == typeWCHAR || element == typeNULL;
        return payloadElement && row->byteLength >= minBlobBytes;
    }
    if (depth >= 8)
        return false;
    for (const auto &child : row->children)
        if (carriesPayload(child.get(), minBlobBytes, depth + 1))
            return true;
    return false;
}

// Widest plain scalar (a GUID-sized field) before a childless row is treated
// as an opaque blob instead.
constexpr uint64_t kMaxScalarBytes = 16;

// Most semantic siblings that still describe a layout (sections, segments,
// chunks) rather than an index of items (strings, fields, methods).
constexpr int kMaxLayoutGroupSize = 256;

class Walker
{
public:
    Walker(uint64_t fileSize, const BuildOptions &options, FileMap *map)
        : m_fileSize(fileSize), m_options(options), m_map(map)
    {
    }

    void walk(StructureRow *row, const Claim &inherited, int depth)
    {
        if (!row || cancelled())
            return;
        if (row->kind == StructureRowKind::Semantic)
        {
            walkSemantic(row, inherited.path, false);
            return;
        }

        const QString label = rowLabel(row);
        const QString path = joinPath(inherited.path, label);
        const uint64_t start = row->absoluteOffset;
        const uint64_t end = clippedEnd(start, row->byteLength);
        const RowShape shape = rowShape(row);
        const bool isTable = shape == RowShape::StructArray && !elementsCarryPayload(row);
        // A top-level record sequence is shown as its records ("[1] IDAT")
        // rather than one span; a top-level table stays one span.
        const bool promoteElements = depth == 1 && shape == RowShape::StructArray && !isTable;

        const bool outermostDynamic = row->kind == StructureRowKind::Dynamic
            && !(row->parent && row->parent->kind == StructureRowKind::Dynamic);
        if (outermostDynamic && !isElementRow(row) && start < end && !label.isEmpty())
            m_structure.push_back(Section{start, end, label, true});
        else if (depth == 1 && start < end && !promoteElements)
            m_structure.push_back(Section{start, end, label, false});

        // Scalars (and table elements) keep the claim they inherit, path
        // included, so adjacent fields merge into one region per record.
        Claim claim = inherited;
        bool descend = true;

        switch (shape)
        {
        case RowShape::Scalar:
            // No scalar is this wide; a childless row this size is an opaque
            // typedef'd blob (PE's dynamic SECTION rows, for instance).
            if (row->byteLength > kMaxScalarBytes)
                claim = Claim{RegionKind::Data, RegionSource::Structure, label, path};
            paint(Layer::Layout, start, end, claim);
            descend = false;
            break;

        case RowShape::Struct:
            // Element records of a table stay part of the table; any other
            // record is metadata unless something inside says otherwise.
            // Records don't paint their own span: every declared field has a
            // row, so bytes no child covers are offset(...) jumps or
            // alignment -- real gaps, not "part of the header".
            if (inherited.kind != RegionKind::Table || !isElementRow(row))
                claim = Claim{RegionKind::Metadata, RegionSource::Structure, label, path};
            break;

        case RowShape::StructArray:
            // A record sequence paints nothing itself; each element record
            // claims its own header bytes and its payload children the rest.
            if (isTable && row->byteLength > 0)
            {
                claim = Claim{RegionKind::Table, RegionSource::Structure, label, path};
                paint(Layer::Layout, start, end, claim);
            }
            else
            {
                claim.path = path;
            }
            break;

        case RowShape::ScalarArray:
        {
            const TYPE element = arrayElementBaseType(row);
            const bool text = isTextFormatted(row) || element == typeCHAR || element == typeWCHAR;
            // Only byte arrays are opaque; word/dword arrays are tables of
            // values (relocation entries, offset tables).
            const bool opaque = element == typeBYTE || element == typeNULL;
            if (text && row->byteLength >= m_options.minTextBytes)
                claim = Claim{RegionKind::Text, RegionSource::Structure, label, path};
            else if (opaque && row->byteLength >= m_options.minBlobBytes)
                claim = Claim{RegionKind::Data, RegionSource::Structure, label, path}; // refined later
            else if (!text && !opaque && row->byteLength >= m_options.minBlobBytes
                     && inherited.kind != RegionKind::Table)
                claim = Claim{RegionKind::Table, RegionSource::Structure, label, path};
            paint(Layer::Layout, start, end, claim);
            descend = false;
            break;
        }
        }

        applyTargets(row, label, path);

        if (!descend)
            return;

        // The root has no inherited claim; give its children a path to hang
        // their own claims off.
        if (claim.path.isEmpty())
            claim.path = path;
        forEachChild(row, [&](StructureRow *child) {
            walk(child, claim, promoteElements ? 1 : depth + 1);
        });
    }

    // Applies the collected claims and fills FileMap::structure/markers.
    void finish(IntervalPainter *painter)
    {
        inferCodeFromEntryPoints();

        // Semantic spans have no nesting order that means anything (ELF lists
        // sections and then the segments that contain them), so the more
        // specific -- smaller -- span wins wherever two overlap.
        auto &semantic = m_layers[size_t(Layer::Semantic)];
        std::stable_sort(semantic.begin(), semantic.end(), [](const PaintOp &a, const PaintOp &b) {
            return a.end - a.start > b.end - b.start;
        });

        // Semantic spans are the backdrop; exact layout overrides them; code
        // and nested targets describe what bytes *are*, so they go last.
        for (const auto &layer : m_layers)
            for (const PaintOp &op : layer)
                if (op.claim.kind != RegionKind::Unknown)
                    painter->paint(op.start, op.end, op.claim);

        // Of the semantic groups (Sections, Segments, Exports, ...), the one
        // that partitions the most of the file is the format's own idea of
        // its layout. Overlap is penalised: segments nest and overlap,
        // sections tile.
        const std::vector<Section> *best = nullptr;
        int64_t bestScore = 0;
        for (const auto &[key, sections] : m_semanticGroups)
        {
            const int64_t score = partitionScore(sections);
            if (score > bestScore)
            {
                best = &sections;
                bestScore = score;
            }
        }

        // Prefer the semantic label where raw layout produced the same span
        // ("[4] WASM_SECTION_CODE" vs the summary's name for it).
        QList<Section> structure = m_structure;
        if (best)
            for (const Section &section : *best)
            {
                auto same = std::find_if(structure.begin(), structure.end(), [&](const Section &s) {
                    return s.start == section.start && s.end == section.end;
                });
                if (same != structure.end())
                    same->label = section.label;
                else
                    structure.push_back(section);
            }

        std::stable_sort(structure.begin(), structure.end(), [](const Section &a, const Section &b) {
            return a.start < b.start;
        });
        m_map->structure = structure;
        m_map->markers = m_markers;
        std::sort(m_map->markers.begin(), m_map->markers.end(), [](const Marker &a, const Marker &b) {
            return a.offset < b.offset;
        });
    }

private:
    enum class Layer { Semantic, Layout, Target, Count };

    // Union coverage minus double-covered bytes.
    static int64_t partitionScore(std::vector<Section> sections)
    {
        std::sort(sections.begin(), sections.end(), [](const Section &a, const Section &b) {
            return a.start < b.start;
        });
        uint64_t sum = 0;
        uint64_t unionCoverage = 0;
        uint64_t reach = 0;
        for (const Section &section : sections)
        {
            sum += section.end - section.start;
            const uint64_t from = std::max(section.start, reach);
            if (section.end > from)
                unionCoverage += section.end - from;
            reach = std::max(reach, section.end);
        }
        return int64_t(unionCoverage) - int64_t(sum - unionCoverage);
    }

    struct PaintOp
    {
        uint64_t start;
        uint64_t end;
        Claim    claim;
    };

    // Semantic rows that cover bytes describe *where* something is (".text",
    // "Symbols") without a byte-level layout. They paint a low-priority Data
    // backdrop that statistics and entry points refine later.
    void walkSemantic(StructureRow *row, const QString &parentPath, bool sizedAncestor, bool itemized = false)
    {
        if (cancelled())
            return;

        const QString label = rowLabel(row);
        const QString path = joinPath(parentPath, label);
        const uint64_t start = row->absoluteOffset;
        const uint64_t end = clippedEnd(start, row->byteLength);
        const bool sized = start < end;

        if (sized && !itemized)
        {
            paint(Layer::Semantic, start, end, Claim{RegionKind::Data, RegionSource::Structure, label, path});
            if (!sizedAncestor)
                m_semanticGroups[{row->parent, row->type}].push_back(Section{start, end, label, false});
        }

        applyTargets(row, label, path);

        // A semantic byte array ("Bytes[]") is one span; its one-byte
        // element rows are not regions of their own.
        if (rowShape(row) == RowShape::ScalarArray)
            return;

        // Hundreds of siblings (every string, every method) are an index of
        // items, not a layout: they neither paint nor become structure.
        // Their bytes fall through to statistical refinement.
        const bool childrenItemized = itemized || row->children.size() > size_t(kMaxLayoutGroupSize);
        forEachChild(row, [&](StructureRow *child) {
            walkSemantic(child, path, sizedAncestor || sized, childrenItemized);
        });
    }

    template <typename Fn>
    void forEachChild(StructureRow *row, Fn fn)
    {
        for (const auto &child : row->children)
            fn(child.get());

        if (row->lazyChildLoader && !row->lazyChildrenLoaded && m_lazyBudget > 0)
        {
            --m_lazyBudget;
            auto lazyRows = row->lazyChildLoader();
            for (const auto &child : lazyRows)
                fn(child.get());
            m_lazyHold.insert(m_lazyHold.end(),
                              std::make_move_iterator(lazyRows.begin()),
                              std::make_move_iterator(lazyRows.end()));
        }
    }

    bool elementsCarryPayload(const StructureRow *array) const
    {
        // Elements are homogeneous by declaration; a handful is representative.
        int checked = 0;
        for (const auto &element : array->children)
        {
            if (carriesPayload(element.get(), m_options.minBlobBytes))
                return true;
            if (++checked == 8)
                break;
        }
        return false;
    }

    void applyTargets(StructureRow *row, const QString &label, const QString &path)
    {
        if (row->hasCodeTarget && row->codeByteLength > 0 && !inheritsCodeTarget(row))
        {
            const uint64_t codeEnd = clippedEnd(row->codeTargetOffset, row->codeByteLength);
            // [entrypoint, code(..., extent(65536))] gives a disassembly
            // window, not the size of anything, so it is only a marker.
            if (findRowTag(row, TOK_ENTRYPOINT))
                m_markers.push_back(Marker{row->codeTargetOffset, label.isEmpty() ? QStringLiteral("entry") : label});
            else
                paint(Layer::Target, row->codeTargetOffset, codeEnd,
                      Claim{RegionKind::Code, RegionSource::Structure, label, path});
        }

        if (row->hasOpenAsTarget && row->openAsByteLength > 0 && !inheritsNestedTarget(row))
        {
            const RegionKind kind = row->openAsTransform.isEmpty() ? RegionKind::Embedded : RegionKind::Compressed;
            QString nestedLabel = row->openAsName.isEmpty() ? label : row->openAsName;
            if (!row->openAsRootTypeName.isEmpty())
                nestedLabel += QStringLiteral(" (%1)").arg(row->openAsRootTypeName);
            paint(Layer::Target, row->openAsOffset, clippedEnd(row->openAsOffset, row->openAsByteLength),
                  Claim{kind, RegionSource::Structure, nestedLabel, path});
        }
    }

    // The smallest opaque span containing an entry point is very likely
    // code (".text", "__TEXT", "CODE"), whatever the format calls it. The
    // definition doesn't say so, so the claim is heuristic. Checked per
    // layer: a section can be both a semantic summary row and a dynamic row.
    void inferCodeFromEntryPoints()
    {
        for (Layer layer : {Layer::Semantic, Layer::Layout})
            for (const Marker &marker : std::as_const(m_markers))
            {
                PaintOp *smallest = nullptr;
                for (PaintOp &op : m_layers[size_t(layer)])
                    if (op.claim.kind == RegionKind::Data && op.start <= marker.offset && marker.offset < op.end
                        && (!smallest || op.end - op.start < smallest->end - smallest->start))
                        smallest = &op;
                if (smallest)
                {
                    smallest->claim.kind = RegionKind::Code;
                    smallest->claim.source = RegionSource::Heuristic;
                }
            }
    }

    void paint(Layer layer, uint64_t start, uint64_t end, const Claim &claim)
    {
        if (start < end && claim.kind != RegionKind::Unknown)
            m_layers[size_t(layer)].push_back(PaintOp{start, end, claim});
    }

    uint64_t clippedEnd(uint64_t start, uint64_t length) const
    {
        if (start >= m_fileSize)
            return start;
        return length > m_fileSize - start ? m_fileSize : start + length;
    }

    bool cancelled() const
    {
        return m_options.cancelFlag && m_options.cancelFlag->load();
    }

    uint64_t m_fileSize;
    const BuildOptions &m_options;
    FileMap *m_map;
    std::array<std::vector<PaintOp>, size_t(Layer::Count)> m_layers;
    QList<Section> m_structure;
    QList<Marker> m_markers;
    // Keyed by (parent, element type): flattened siblings such as ELF's
    // Sections[] and Segments[] share a parent but not a type.
    std::map<std::pair<const StructureRow *, const Type *>, std::vector<Section>> m_semanticGroups;
    std::vector<std::unique_ptr<StructureRow>> m_lazyHold;
    int m_lazyBudget = m_options.maxLazyExpansions;
};

// ---- statistical refinement ------------------------------------------------

// Fewer bytes than this say nothing statistically (a lone 00 is not padding).
constexpr size_t kMinClassifyBytes = 16;

RegionKind classifyBytes(const uint8_t *data, size_t length, RegionKind fallback)
{
    if (length < kMinClassifyBytes)
        return fallback;

    std::array<uint32_t, 256> histogram{};
    size_t printable = 0;
    for (size_t i = 0; i < length; ++i)
    {
        const uint8_t byte = data[i];
        ++histogram[byte];
        if ((byte >= 0x20 && byte < 0x7F) || byte == '\t' || byte == '\r' || byte == '\n')
            ++printable;
    }

    for (const uint8_t fill : {uint8_t(0x00), uint8_t(0xFF), uint8_t(0xCC)})
        if (histogram[fill] == length)
            return RegionKind::Padding;

    if (printable * 100 >= length * 90)
        return RegionKind::Text;

    // Entropy can't reach 8 bits/byte in fewer than 256 bytes, so compare
    // against the maximum achievable for this sample size.
    if (length >= 256)
    {
        double entropy = 0.0;
        for (const uint32_t count : histogram)
        {
            if (!count)
                continue;
            const double p = double(count) / double(length);
            entropy -= p * std::log2(p);
        }
        const double ceiling = std::min(8.0, std::log2(double(length)));
        if (entropy >= ceiling * 0.9)
            return RegionKind::Compressed;
    }

    return fallback;
}

// Splits a Data/Unknown region into blocks and classifies each one.
// Structure-described Data keeps its label; padding inside a gap is common
// (section alignment) and is worth showing separately from real unknowns.
void refineRegion(const Region &region,
                  const ByteReader &reader,
                  const BuildOptions &options,
                  QList<Region> *out)
{
    const uint64_t length = region.end - region.start;
    uint64_t blockSize = options.refineBlockSize;
    if (options.maxRefineBlocksPerRegion > 0 && length / blockSize > uint64_t(options.maxRefineBlocksPerRegion))
        blockSize = (length + options.maxRefineBlocksPerRegion - 1) / options.maxRefineBlocksPerRegion;

    // Sample at most refineBlockSize bytes from the head of each block.
    const size_t sampleSize = size_t(std::min(blockSize, options.refineBlockSize));
    QByteArray buffer(int(sampleSize), Qt::Uninitialized);

    for (uint64_t offset = region.start; offset < region.end; offset += blockSize)
    {
        const uint64_t blockEnd = std::min(region.end, offset + blockSize);
        const size_t want = size_t(std::min<uint64_t>(sampleSize, blockEnd - offset));
        const size_t got = reader(offset, reinterpret_cast<uint8_t *>(buffer.data()), want);

        Region block = region;
        block.start = offset;
        block.end = blockEnd;
        const RegionKind kind = classifyBytes(reinterpret_cast<const uint8_t *>(buffer.constData()), got, region.kind);
        if (kind != region.kind)
        {
            block.kind = kind;
            // A gap that turns out to be padding is still a gap; only data
            // the definition described gets a stronger provenance downgrade.
            if (region.source == RegionSource::Structure)
                block.source = RegionSource::Heuristic;
        }
        out->push_back(block);
    }
}

bool mergeable(const Region &a, const Region &b)
{
    return a.end == b.start && a.kind == b.kind && a.source == b.source && a.label == b.label;
}

} // namespace

FileMap buildFileMap(const std::vector<std::unique_ptr<StructureRow>> &rows,
                     uint64_t fileSize,
                     const ByteReader &reader,
                     const BuildOptions &options)
{
    FileMap map;
    map.fileSize = fileSize;
    if (!rows.empty())
        map.rootName = rowLabel(rows.front().get());

    IntervalPainter painter;
    Walker walker(fileSize, options, &map);
    for (const auto &row : rows)
        walker.walk(row.get(), Claim{}, 0);
    walker.finish(&painter);

    // Fill gaps: inside the described extent they are unknown; past the last
    // described byte they are an overlay (appended installer data, signatures,
    // trailing garbage).
    const uint64_t coveredEnd = painter.coveredEnd();
    QList<Region> painted;
    uint64_t cursor = 0;
    auto pushGap = [&](uint64_t start, uint64_t end) {
        if (start >= end)
            return;
        const bool overlay = coveredEnd > 0 && start >= coveredEnd;
        painted.push_back(Region{start, end, RegionKind::Unknown,
                                 overlay ? RegionSource::Overlay : RegionSource::Gap,
                                 QString(), QString()});
    };
    painter.forEach([&](uint64_t start, uint64_t end, const Claim &claim) {
        pushGap(cursor, start);
        painted.push_back(Region{start, end, claim.kind, claim.source, claim.label, claim.path});
        cursor = end;
    });
    pushGap(cursor, fileSize);

    QList<Region> refined;
    for (const Region &region : std::as_const(painted))
    {
        if (options.cancelFlag && options.cancelFlag->load())
            return {};
        if (region.kind == RegionKind::Data || region.kind == RegionKind::Unknown)
            refineRegion(region, reader, options, &refined);
        else
            refined.push_back(region);
    }

    for (const Region &region : std::as_const(refined))
    {
        if (!map.content.isEmpty() && mergeable(map.content.last(), region))
            map.content.last().end = region.end;
        else
            map.content.push_back(region);
    }

    // An entry point inside described header fields is a "none" value (ELF
    // shared objects use e_entry = 0), not a location worth marking.
    map.markers.removeIf([&map](const Marker &marker) {
        auto region = std::upper_bound(map.content.cbegin(), map.content.cend(), marker.offset,
                                       [](uint64_t offset, const Region &r) { return offset < r.end; });
        return region != map.content.cend()
            && (region->kind == RegionKind::Metadata || region->kind == RegionKind::Table);
    });

    return map;
}

FileMap buildFileMap(StructureDefinitionManager &definitions,
                     const QString &fileName,
                     uint64_t fileSize,
                     const ByteReader &reader,
                     const BuildOptions &options)
{
    definitions.ensureLoaded();
    TypeDecl *rootType = definitions.library()
        ? associatedStrataRoot(definitions.exportedTypes(), fileName, reader)
        : nullptr;

    std::vector<std::unique_ptr<StructureRow>> rows;
    if (rootType)
    {
        const ByteReader cancellableReader = [reader, cancelFlag = options.cancelFlag](uint64_t offset,
                                                                                       uint8_t *buffer,
                                                                                       size_t length) -> size_t {
            return cancelFlag && cancelFlag->load() ? 0 : reader(offset, buffer, length);
        };
        rows = StructureValueBuilder().build(definitions.library(),
                                             rootType,
                                             strataRootBaseOffset(rootType),
                                             cancellableReader);
    }
    return buildFileMap(rows, fileSize, reader, options);
}

QString regionKindName(RegionKind kind)
{
    switch (kind)
    {
    case RegionKind::Unknown:    return QStringLiteral("unknown");
    case RegionKind::Metadata:   return QStringLiteral("metadata");
    case RegionKind::Table:      return QStringLiteral("table");
    case RegionKind::Text:       return QStringLiteral("text");
    case RegionKind::Code:       return QStringLiteral("code");
    case RegionKind::Data:       return QStringLiteral("data");
    case RegionKind::Padding:    return QStringLiteral("padding");
    case RegionKind::Compressed: return QStringLiteral("compressed");
    case RegionKind::Embedded:   return QStringLiteral("embedded");
    }
    return QString();
}

QString regionSourceName(RegionSource source)
{
    switch (source)
    {
    case RegionSource::Structure: return QStringLiteral("structure");
    case RegionSource::Heuristic: return QStringLiteral("heuristic");
    case RegionSource::Gap:       return QStringLiteral("gap");
    case RegionSource::Overlay:   return QStringLiteral("overlay");
    }
    return QString();
}

} // namespace filemap
