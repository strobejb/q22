#include "structview/pesemanticview.h"

#include "structview/structurebranchicons.h"
#include "structview/structuresemanticview.h"

#include <QString>

namespace
{
static constexpr uint64_t kImportDescriptorSize = 20;
static constexpr size_t kMaxImportDescriptors = 256;
static constexpr size_t kMaxThunkEntries = 512;
static constexpr uint32_t kOrdinalImport32 = 0x80000000u;

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

void appendImportsForDescriptor(StructureSemanticContext &context,
                                StructureRow *dllRow,
                                const ImportDescriptor &descriptor)
{
    const uint32_t thunkRva = descriptor.originalFirstThunk ? descriptor.originalFirstThunk : descriptor.firstThunk;
    uint64_t thunkOffset = 0;
    if (thunkRva == 0 || !context.mapLogicalOffset(thunkRva, &thunkOffset))
        return;

    for (size_t index = 0; index < kMaxThunkEntries; ++index)
    {
        uint32_t thunk = 0;
        const uint64_t thunkEntryOffset = context.baseOffset() + thunkOffset + index * sizeof(uint32_t);
        if (!context.readUInt32(thunkEntryOffset, &thunk) || thunk == 0)
            return;

        if (thunk & kOrdinalImport32)
        {
            const uint32_t ordinal = thunk & 0xffffu;
            context.appendSemanticRow(dllRow,
                                      QStringLiteral("Ordinal %1").arg(ordinal),
                                      QString(),
                                      thunkEntryOffset,
                                      sizeof(uint32_t));
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

        context.appendSemanticRow(dllRow,
                                  QStringLiteral("Import %1").arg(functionName),
                                  QStringLiteral("hint %1").arg(hint),
                                  hintOffset,
                                  uint64_t(sizeof(uint16_t) + functionName.size() + 1));
    }
}

void interpretPeImports(StructureSemanticContext &context)
{
    StructureRow *importRow = context.currentRow();
    if (!importRow)
        return;

    for (size_t descriptorIndex = 0; descriptorIndex < kMaxImportDescriptors; ++descriptorIndex)
    {
        const uint64_t descriptorOffset = importRow->absoluteOffset + descriptorIndex * kImportDescriptorSize;
        ImportDescriptor descriptor;
        if (!readImportDescriptor(context, descriptorOffset, &descriptor) || descriptor.isNull())
            return;

        uint64_t nameOffset = 0;
        QString dllName;
        if (descriptor.name != 0 && context.mapLogicalOffset(descriptor.name, &nameOffset))
            dllName = context.readAsciiString(context.baseOffset() + nameOffset);

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
        }
        appendImportsForDescriptor(context, dllRow, descriptor);
    }
}
}

void registerPeSemanticViews(StructureSemanticViewRegistry &registry)
{
    registry.registerInterpreter(QStringLiteral("pe.imports"), interpretPeImports);
}
