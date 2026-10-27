#ifndef FILEMAP_FILEMAPBUILDER_H
#define FILEMAP_FILEMAPBUILDER_H

#include <QList>
#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

struct StructureRow;
class StructureDefinitionManager;

namespace filemap
{

// What the bytes in a region are, independent of which format described them.
// Inferred from the shape of the Strata row tree (struct/array/scalar, code and
// nested targets, string formats) and refined with byte statistics where the
// definition only says "opaque bytes".
enum class RegionKind
{
    Unknown,    // no row covers it and statistics found nothing distinctive
    Metadata,   // scalar fields / small fixed structs (headers, records)
    Table,      // arrays of structs (section headers, symbols, directory entries)
    Text,       // character arrays and string-formatted fields
    Code,       // [code(...)] ranges, or the opaque span holding an entry point
    Data,       // opaque byte payloads that look like neither of the below
    Padding,    // runs of 00 / FF / CC
    Compressed, // nested(..., transform(...)) payloads, or high-entropy blobs
    Embedded,   // nested(...) payloads opened without a transform
};

// How much to trust a region: Structure is exactly what the definition says,
// Heuristic is a guess (byte statistics, or an entry point implying code)
// about bytes the definition left opaque. Gap means no row covered the bytes;
// Overlay is a gap after the last described byte -- appended installer data,
// signatures, trailing archives. Gaps still get a statistical kind.
enum class RegionSource
{
    Structure,
    Heuristic,
    Gap,
    Overlay,
};

struct Region
{
    uint64_t     start  = 0;
    uint64_t     end    = 0;
    RegionKind   kind   = RegionKind::Unknown;
    RegionSource source = RegionSource::Gap;
    QString      label; // innermost meaningful row name
    QString      path;  // dotted row path from the root, for tooltips
};

// A coarse structural span (top-level fields, table elements) for the
// "structure" column of a file map, as opposed to the "content" column.
struct Section
{
    uint64_t start = 0;
    uint64_t end   = 0;
    QString  label;
    bool     dynamic = false; // placed at a computed offset rather than sequentially
};

// Single-offset markers (entry points) whose [code] extent is a guess rather
// than a described range, so they are not painted as Code regions.
struct Marker
{
    uint64_t offset = 0;
    QString  label;
};

struct FileMap
{
    QString         rootName;
    uint64_t        fileSize = 0;
    QList<Region>   content;   // sorted, disjoint, covers [0, fileSize)
    QList<Section>  structure; // sorted by start; may overlap
    QList<Marker>   markers;
};

using ByteReader = std::function<size_t(uint64_t offset, uint8_t *buf, size_t len)>;

struct BuildOptions
{
    // Lazy children (dynamic arrays, deferred tables) are expanded so their
    // bytes are classified; this bounds how many loaders a single build runs.
    int maxLazyExpansions = 4096;
    // Granularity for statistical refinement of opaque blobs and gaps. Large
    // regions use proportionally larger blocks so the block count stays bounded.
    uint64_t refineBlockSize = 4096;
    int      maxRefineBlocksPerRegion = 512;
    // Below these sizes a char/byte array is just a field of its parent
    // record (a section name, a reserved array), not a region of its own.
    uint64_t minTextBytes = 16;
    uint64_t minBlobBytes = 64;
    std::shared_ptr<std::atomic_bool> cancelFlag;
};

// Classifies an already-built Strata row tree. Rows may be lazily expanded.
FileMap buildFileMap(const std::vector<std::unique_ptr<StructureRow>> &rows,
                     uint64_t fileSize,
                     const ByteReader &reader,
                     const BuildOptions &options = {});

// Convenience: selects the associated Strata root for `fileName`/magic, builds
// its rows, and classifies them. Without a matching root the whole file is a
// single gap, refined statistically.
FileMap buildFileMap(StructureDefinitionManager &definitions,
                     const QString &fileName,
                     uint64_t fileSize,
                     const ByteReader &reader,
                     const BuildOptions &options = {});

QString regionKindName(RegionKind kind);
QString regionSourceName(RegionSource source);

} // namespace filemap

#endif // FILEMAP_FILEMAPBUILDER_H
