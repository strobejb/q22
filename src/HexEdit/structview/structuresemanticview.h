#ifndef STRUCTVIEW_STRUCTURESEMANTICVIEW_H
#define STRUCTVIEW_STRUCTURESEMANTICVIEW_H

#include "structview/structurevaluebuilder.h"

#include <QHash>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

struct StructureOffsetMap
{
    uint64_t logicalStart = 0;
    uint64_t logicalSize = 0;
    uint64_t fileOffset = 0;
};

class StructureSemanticContext
{
public:
    StructureSemanticContext(StrataLibrary *library,
                             StructureRow *rootRow,
                             StructureRow *currentRow,
                             uint64_t baseOffset,
                             const StructureValueBuilder::ByteReader &reader,
                             const std::vector<StructureOffsetMap> &offsetMaps);

    StrataLibrary *library() const;
    StructureRow *rootRow() const;
    StructureRow *currentRow() const;
    uint64_t baseOffset() const;
    const StructureValueBuilder::ByteReader &byteReader() const;
    std::vector<StructureOffsetMap> offsetMaps() const;

    bool mapLogicalOffset(uint64_t logicalOffset, uint64_t *fileOffset) const;
    bool readBytes(uint64_t absoluteOffset, uint8_t *buffer, size_t length) const;
    bool readUInt16(uint64_t absoluteOffset, uint16_t *value) const;
    bool readUInt32(uint64_t absoluteOffset, uint32_t *value) const;
    bool readUInt64(uint64_t absoluteOffset, uint64_t *value) const;
    QString readAsciiString(uint64_t absoluteOffset, size_t maxLength = 4096) const;

    StructureRow *appendSemanticRow(StructureRow *parent,
                                    const QString &name,
                                    const QString &value = QString(),
                                    uint64_t absoluteOffset = 0,
                                    uint64_t byteLength = 0) const;
    std::unique_ptr<StructureRow> createSemanticRow(StructureRow *parent,
                                                    const QString &name,
                                                    const QString &value = QString(),
                                                    uint64_t absoluteOffset = 0,
                                                    uint64_t byteLength = 0) const;

private:
    QString formatOffset(uint64_t offset) const;

    StrataLibrary *m_library = nullptr;
    StructureRow *m_rootRow = nullptr;
    StructureRow *m_currentRow = nullptr;
    uint64_t m_baseOffset = 0;
    StructureValueBuilder::ByteReader m_reader;
    const std::vector<StructureOffsetMap> *m_offsetMaps = nullptr;
};

using StructureSemanticInterpreter = std::function<void(StructureSemanticContext &)>;

class StructureSemanticViewRegistry
{
public:
    static StructureSemanticViewRegistry &instance();

    void registerInterpreter(const QString &id, const StructureSemanticInterpreter &interpreter);
    bool run(const QString &id, StructureSemanticContext &context) const;

private:
    QHash<QString, StructureSemanticInterpreter> m_interpreters;
};

void registerBuiltInStructureSemanticViews();
void runStructureSemanticViews(StrataLibrary *library,
                               StructureRow *rootRow,
                               uint64_t baseOffset,
                               const StructureValueBuilder::ByteReader &reader,
                               const std::vector<StructureOffsetMap> &offsetMaps);

#endif // STRUCTVIEW_STRUCTURESEMANTICVIEW_H
