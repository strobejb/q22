#include "structview/pesemanticview.h"

#include "structview/structurebranchicons.h"
#include "structview/structuresemanticview.h"

#include <QString>

#include <memory>
#include <vector>

namespace
{
static constexpr uint64_t kImportDescriptorSize = 20;
static constexpr size_t kMaxImportDescriptors = 256;
static constexpr size_t kMaxThunkEntries = 512;
// Semantic import rows are an educational overlay, not the canonical raw data.
// Keep malformed or misidentified files from producing a huge eager tree; the
// raw dynamic_array descriptor/thunk rows remain available for full inspection.
static constexpr size_t kMaxSemanticImportRows = 8192;
static constexpr uint32_t kOrdinalImport32 = 0x80000000u;
static constexpr uint64_t kOrdinalImport64 = 0x8000000000000000ull;
static constexpr uint32_t kPe32PlusOptionalHeaderMagic = 0x20b;

struct ImportDescriptor
{
    uint32_t originalFirstThunk = 0;
    uint32_t timeDateStamp = 0;
    uint32_t forwarderChain = 0;
    uint32_t name = 0;
    uint32_t firstThunk = 0;

    bool isNull() const
    {
        return originalFirstThunk == 0 && timeDateStamp == 0 && forwarderChain == 0 && name == 0 && firstThunk == 0;
    }
};

bool readImportDescriptor(StructureSemanticContext &context, uint64_t offset, ImportDescriptor *descriptor)
{
    if (!descriptor)
        return false;

    return context.readUInt32(offset + 0, &descriptor->originalFirstThunk)
           && context.readUInt32(offset + 4, &descriptor->timeDateStamp)
           && context.readUInt32(offset + 8, &descriptor->forwarderChain)
           && context.readUInt32(offset + 12, &descriptor->name)
           && context.readUInt32(offset + 16, &descriptor->firstThunk);
}

QString dllDisplayName(const QString &name)
{
    return name.isEmpty() ? QStringLiteral("(unnamed DLL)") : name;
}

bool isPe32PlusImage(StructureSemanticContext &context)
{
    uint32_t ntHeadersOffset = 0;
    if (!context.readUInt32(context.baseOffset() + 0x3c, &ntHeadersOffset))
        return false;

    uint16_t magic = 0;
    return context.readUInt16(context.baseOffset() + ntHeadersOffset + 24, &magic)
        && magic == kPe32PlusOptionalHeaderMagic;
}

bool consumeSemanticRowBudget(size_t *remainingRows)
{
    if (!remainingRows || *remainingRows == 0)
        return false;

    --(*remainingRows);
    return true;
}

size_t descriptorLimitForRow(const StructureRow *importRow)
{
    if (!importRow)
        return kMaxImportDescriptors;

    // dynamic_array creates a parent row whose displayed children are the
    // descriptor elements accepted by the raw renderer. Reuse that count so the
    // semantic overlay cannot run past the rendered table into thunk/name data.
    if (importRow->nameTypePrefix.endsWith(QStringLiteral("[]")) && !importRow->children.empty())
        return std::min(importRow->children.size(), kMaxImportDescriptors);

    return kMaxImportDescriptors;
}

void appendImportLimitRow(StructureSemanticContext &context, StructureRow *importRow)
{
    context.appendSemanticRow(importRow,
                              QStringLiteral("Import list truncated"),
                              QStringLiteral("semantic row limit reached"));
}

std::unique_ptr<StructureRow> makeImportLimitRow(StructureSemanticContext &context)
{
    return context.createSemanticRow(nullptr,
                                     QStringLiteral("Import list truncated"),
                                     QStringLiteral("semantic row limit reached"));
}

std::vector<std::unique_ptr<StructureRow>> importRowsForDescriptor(StructureSemanticContext &context,
                                                                   const ImportDescriptor &descriptor,
                                                                   bool pe32Plus)
{
    std::vector<std::unique_ptr<StructureRow>> rows;
    size_t remainingRows = kMaxSemanticImportRows;
    const uint32_t thunkRva = descriptor.originalFirstThunk ? descriptor.originalFirstThunk : descriptor.firstThunk;
    uint64_t thunkOffset = 0;
    if (thunkRva == 0 || !context.mapLogicalOffset(thunkRva, &thunkOffset))
        return rows;

    const size_t thunkSize = pe32Plus ? sizeof(uint64_t) : sizeof(uint32_t);
    const uint64_t ordinalMask = pe32Plus ? kOrdinalImport64 : kOrdinalImport32;

    for (size_t index = 0; index < kMaxThunkEntries; ++index)
    {
        uint64_t thunk = 0;
        const uint64_t thunkEntryOffset = context.baseOffset() + thunkOffset + index * thunkSize;
        const bool read = pe32Plus
            ? context.readUInt64(thunkEntryOffset, &thunk)
            : [&]() {
                  uint32_t thunk32 = 0;
                  if (!context.readUInt32(thunkEntryOffset, &thunk32))
                      return false;
                  thunk = thunk32;
                  return true;
              }();
        if (!read || thunk == 0)
            return rows;

        if (thunk & ordinalMask)
        {
            if (!consumeSemanticRowBudget(&remainingRows))
            {
                rows.push_back(makeImportLimitRow(context));
                return rows;
            }

            const uint64_t ordinal = thunk & 0xffffu;
            rows.push_back(context.createSemanticRow(nullptr,
                                                     QStringLiteral("Ordinal %1").arg(ordinal),
                                                     QString(),
                                                     thunkEntryOffset,
                                                     thunkSize));
            continue;
        }

        uint64_t importByNameOffset = 0;
        if (!context.mapLogicalOffset(thunk, &importByNameOffset))
            continue;

        uint16_t hint = 0;
        const uint64_t hintOffset = context.baseOffset() + importByNameOffset;
        if (!context.readUInt16(hintOffset, &hint))
            continue;

        const QString functionName = context.readAsciiString(hintOffset + sizeof(uint16_t));
        if (functionName.isEmpty())
            continue;

        if (!consumeSemanticRowBudget(&remainingRows))
        {
            rows.push_back(makeImportLimitRow(context));
            return rows;
        }

        rows.push_back(context.createSemanticRow(nullptr,
                                                 QStringLiteral("Import %1").arg(functionName),
                                                 QStringLiteral("hint %1").arg(hint),
                                                 hintOffset,
                                                 uint64_t(sizeof(uint16_t) + functionName.size() + 1)));
    }

    return rows;
}

void interpretPeImports(StructureSemanticContext &context)
{
    StructureRow *importRow = context.currentRow();
    if (!importRow)
        return;

    const bool pe32Plus = isPe32PlusImage(context);
    size_t remainingRows = kMaxSemanticImportRows;
    const size_t descriptorLimit = descriptorLimitForRow(importRow);
    const uint64_t baseOffset = context.baseOffset();
    const StructureValueBuilder::ByteReader reader = context.byteReader();
    const std::vector<StructureOffsetMap> offsetMaps = context.offsetMaps();

    for (size_t descriptorIndex = 0; descriptorIndex < descriptorLimit; ++descriptorIndex)
    {
        const uint64_t descriptorOffset = importRow->absoluteOffset + descriptorIndex * kImportDescriptorSize;
        ImportDescriptor descriptor;
        if (!readImportDescriptor(context, descriptorOffset, &descriptor) || descriptor.isNull())
            return;

        uint64_t nameOffset = 0;
        QString dllName;
        if (descriptor.name != 0 && context.mapLogicalOffset(descriptor.name, &nameOffset))
            dllName = context.readAsciiString(context.baseOffset() + nameOffset);

        if (!consumeSemanticRowBudget(&remainingRows))
        {
            appendImportLimitRow(context, importRow);
            return;
        }

        StructureRow *dllRow = context.appendSemanticRow(importRow,
                                                         dllDisplayName(dllName),
                                                         QStringLiteral("{...}"),
                                                         descriptorOffset,
                                                         kImportDescriptorSize);

        if (dllRow)
        {
            dllRow->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueSingleClosed),
                                   QString::fromLatin1(StructureBranchIcons::kBlueSingleOpen),
                                   QString::fromLatin1(StructureBranchIcons::kGraySingleClosed));
            dllRow->lazyChildLoader = [baseOffset, reader, offsetMaps, descriptor, pe32Plus]() {
                StructureSemanticContext lazyContext(nullptr,
                                                     nullptr,
                                                     nullptr,
                                                     baseOffset,
                                                     reader,
                                                     offsetMaps);
                return importRowsForDescriptor(lazyContext, descriptor, pe32Plus);
            };
        }
    }
}
}

void registerPeSemanticViews(StructureSemanticViewRegistry &registry)
{
    registry.registerInterpreter(QStringLiteral("pe.imports"), interpretPeImports);
}
