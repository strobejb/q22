#ifndef STRUCTVIEW_STRUCTUREDISPLAYOPTIONS_H
#define STRUCTVIEW_STRUCTUREDISPLAYOPTIONS_H

#include <QString>

#include <cstdint>

enum class StructureTypeNameMode
{
    Defined,
    Storage
};

struct StructureDisplayOptions
{
    StructureTypeNameMode typeNameMode = StructureTypeNameMode::Defined;
    bool hexadecimalOffsets = true;
    bool relativeOffsets = false;
};

inline QString formatStructureOffset(uint64_t absoluteOffset,
                                     uint64_t relativeOffset,
                                     const StructureDisplayOptions &options)
{
    const uint64_t offset = options.relativeOffsets ? relativeOffset : absoluteOffset;
    const QString prefix = options.relativeOffsets ? QStringLiteral("+") : QString();

    if (!options.hexadecimalOffsets)
        return prefix + QString::number(offset);

    const int width = options.relativeOffsets ? 4 : 8;
    return prefix + QString::number(offset, 16).toUpper().rightJustified(width, QLatin1Char('0'));
}

#endif // STRUCTVIEW_STRUCTUREDISPLAYOPTIONS_H
