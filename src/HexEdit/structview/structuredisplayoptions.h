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
    bool hexadecimalValues = false;
    bool hexadecimalOffsets = true;
    bool relativeOffsets = false;
};

inline int64_t signedStructureValue(uint64_t value, uint64_t length)
{
    if (length == 0 || length >= 8)
        return static_cast<int64_t>(value);

    const uint64_t signBit = uint64_t(1) << (length * 8 - 1);
    const uint64_t mask = (uint64_t(1) << (length * 8)) - 1;
    if ((value & signBit) == 0)
        return static_cast<int64_t>(value);

    return -static_cast<int64_t>((~value + 1) & mask);
}

inline QString formatStructureIntegerValue(uint64_t rawValue,
                                           uint64_t byteLength,
                                           bool signedValue,
                                           const QString &characterSuffix,
                                           const StructureDisplayOptions &options)
{
    QString text;
    if (options.hexadecimalValues)
    {
        const int width = qMax(1, int(byteLength * 2));
        text = QString::number(rawValue, 16).toUpper().rightJustified(width, QLatin1Char('0'));
    }
    else if (signedValue)
    {
        text = QString::number(signedStructureValue(rawValue, byteLength));
    }
    else
    {
        text = QString::number(rawValue);
    }

    return characterSuffix.isEmpty() ? text : text + characterSuffix;
}

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
