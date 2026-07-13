#include "structview/dexsemanticview.h"

#include "structview/structuresemanticview.h"

#include <QByteArray>
#include <QChar>
#include <QLatin1Char>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <vector>

namespace
{
static constexpr uint32_t kDexEndianConstant = 0x12345678;
static constexpr size_t kMaxIds = 250000;
static constexpr size_t kMaxDecodedStringRows = 4096;
static constexpr size_t kMaxDecodedTypeRows = 4096;
static constexpr size_t kMaxDecodedFieldRows = 8192;
static constexpr size_t kMaxDecodedMethodRows = 8192;
static constexpr size_t kMaxDecodedClassRows = 8192;
static constexpr size_t kMaxStringBytes = 4096;

struct DexHeader
{
    uint32_t fileSize = 0;
    uint32_t endianTag = 0;
    uint32_t stringIdsSize = 0;
    uint32_t stringIdsOff = 0;
    uint32_t typeIdsSize = 0;
    uint32_t typeIdsOff = 0;
    uint32_t protoIdsSize = 0;
    uint32_t protoIdsOff = 0;
    uint32_t fieldIdsSize = 0;
    uint32_t fieldIdsOff = 0;
    uint32_t methodIdsSize = 0;
    uint32_t methodIdsOff = 0;
    uint32_t classDefsSize = 0;
    uint32_t classDefsOff = 0;
};

struct DexString
{
    QString text;
    uint64_t offset = 0;
    uint64_t byteLength = 0;
};

bool read32(StructureSemanticContext &context, uint64_t relativeOffset, uint32_t *value)
{
    return context.readUInt32(context.baseOffset() + relativeOffset, value);
}

bool readDexHeader(StructureSemanticContext &context, DexHeader *header)
{
    if (!header)
        return false;

    uint8_t magic[4] = {};
    if (!context.readBytes(context.baseOffset(), magic, sizeof(magic))
        || magic[0] != 'd' || magic[1] != 'e' || magic[2] != 'x' || magic[3] != '\n')
    {
        return false;
    }

    if (!read32(context, 0x20, &header->fileSize)
        || !read32(context, 0x28, &header->endianTag)
        || !read32(context, 0x38, &header->stringIdsSize)
        || !read32(context, 0x3c, &header->stringIdsOff)
        || !read32(context, 0x40, &header->typeIdsSize)
        || !read32(context, 0x44, &header->typeIdsOff)
        || !read32(context, 0x48, &header->protoIdsSize)
        || !read32(context, 0x4c, &header->protoIdsOff)
        || !read32(context, 0x50, &header->fieldIdsSize)
        || !read32(context, 0x54, &header->fieldIdsOff)
        || !read32(context, 0x58, &header->methodIdsSize)
        || !read32(context, 0x5c, &header->methodIdsOff)
        || !read32(context, 0x60, &header->classDefsSize)
        || !read32(context, 0x64, &header->classDefsOff))
    {
        return false;
    }

    return header->endianTag == kDexEndianConstant
        && header->stringIdsSize <= kMaxIds
        && header->typeIdsSize <= kMaxIds
        && header->protoIdsSize <= kMaxIds
        && header->fieldIdsSize <= kMaxIds
        && header->methodIdsSize <= kMaxIds
        && header->classDefsSize <= kMaxIds;
}

bool checkedTableRead(const DexHeader &header, uint64_t offset, uint64_t bytes)
{
    return offset <= header.fileSize && bytes <= header.fileSize - offset;
}

bool readUleb128(StructureSemanticContext &context, uint64_t absoluteOffset, uint64_t *value, uint64_t *encodedLength)
{
    if (!value || !encodedLength)
        return false;

    *value = 0;
    *encodedLength = 0;

    for (uint64_t i = 0; i < 10; ++i)
    {
        uint8_t byte = 0;
        if (!context.readBytes(absoluteOffset + i, &byte, 1))
            return false;

        *value |= uint64_t(byte & 0x7f) << (i * 7);
        *encodedLength = i + 1;
        if ((byte & 0x80) == 0)
            return true;
    }

    return false;
}

QString decodeModifiedUtf8(const QByteArray &bytes)
{
    QString text;
    text.reserve(bytes.size());

    for (qsizetype i = 0; i < bytes.size();)
    {
        const uint8_t b0 = static_cast<uint8_t>(bytes[i]);
        if (b0 < 0x80)
        {
            text.append(QChar(ushort(b0)));
            ++i;
            continue;
        }

        if ((b0 & 0xe0) == 0xc0 && i + 1 < bytes.size())
        {
            const uint8_t b1 = static_cast<uint8_t>(bytes[i + 1]);
            if ((b1 & 0xc0) == 0x80)
            {
                const ushort code = ushort(((b0 & 0x1f) << 6) | (b1 & 0x3f));
                text.append(QChar(code));
                i += 2;
                continue;
            }
        }

        if ((b0 & 0xf0) == 0xe0 && i + 2 < bytes.size())
        {
            const uint8_t b1 = static_cast<uint8_t>(bytes[i + 1]);
            const uint8_t b2 = static_cast<uint8_t>(bytes[i + 2]);
            if ((b1 & 0xc0) == 0x80 && (b2 & 0xc0) == 0x80)
            {
                const ushort code = ushort(((b0 & 0x0f) << 12) | ((b1 & 0x3f) << 6) | (b2 & 0x3f));
                text.append(QChar(code));
                i += 3;
                continue;
            }
        }

        text.append(QChar::ReplacementCharacter);
        ++i;
    }

    return text;
}

bool readDexString(StructureSemanticContext &context, const DexHeader &header, uint32_t index, DexString *string)
{
    if (!string || index >= header.stringIdsSize)
        return false;

    const uint64_t stringIdOffset = uint64_t(header.stringIdsOff) + uint64_t(index) * 4;
    if (!checkedTableRead(header, stringIdOffset, 4))
        return false;

    uint32_t stringDataOff = 0;
    if (!read32(context, stringIdOffset, &stringDataOff) || stringDataOff >= header.fileSize)
        return false;

    const uint64_t absoluteStringOffset = context.baseOffset() + stringDataOff;
    uint64_t utf16Length = 0;
    uint64_t lebLength = 0;
    if (!readUleb128(context, absoluteStringOffset, &utf16Length, &lebLength))
        return false;

    QByteArray raw;
    raw.reserve(int(std::min<uint64_t>(kMaxStringBytes, header.fileSize - stringDataOff)));
    for (uint64_t i = 0; i < kMaxStringBytes && stringDataOff + lebLength + i < header.fileSize; ++i)
    {
        uint8_t byte = 0;
        if (!context.readBytes(absoluteStringOffset + lebLength + i, &byte, 1))
            return false;
        if (byte == 0)
        {
            string->text = decodeModifiedUtf8(raw);
            string->offset = absoluteStringOffset;
            string->byteLength = lebLength + i + 1;
            return true;
        }
        raw.append(char(byte));
    }

    return false;
}

bool isReadableSummaryString(const QString &text)
{
    const QString display = text.trimmed();
    if (display.isEmpty())
        return false;

    int readable = 0;
    int useful = 0;
    for (QChar ch : display)
    {
        if (!ch.isNull() && ch.unicode() >= 0x20 && ch.unicode() != 0x7f)
            ++readable;
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('$')
            || ch == QLatin1Char('/') || ch == QLatin1Char('.') || ch == QLatin1Char(';'))
        {
            ++useful;
        }
    }

    return readable * 4 >= display.size() * 3 && useful >= std::min(2, int(display.size()));
}

QString displayString(const QString &text)
{
    return text.trimmed();
}

QString indexedName(const QString &prefix, uint32_t index, const QString &text)
{
    return QStringLiteral("%1[%2] %3").arg(prefix).arg(index).arg(displayString(text));
}

void appendDecodedStrings(StructureSemanticContext &context,
                          StructureRow *summaryRow,
                          const DexHeader &header,
                          std::vector<DexString> *strings)
{
    if (!summaryRow || !strings)
        return;

    strings->resize(header.stringIdsSize);
    StructureRow *stringsRow = context.appendSemanticRow(summaryRow, QStringLiteral("Decoded Strings"), QStringLiteral("{...}"));
    if (!stringsRow)
        return;

    size_t appended = 0;
    for (uint32_t i = 0; i < header.stringIdsSize; ++i)
    {
        DexString string;
        if (!readDexString(context, header, i, &string))
            continue;

        (*strings)[i] = string;
        if (!isReadableSummaryString(string.text))
            continue;

        context.appendSemanticRow(stringsRow,
                                  indexedName(QStringLiteral("String"), i, string.text),
                                  QString(),
                                  string.offset,
                                  string.byteLength);
        if (++appended >= kMaxDecodedStringRows)
            break;
    }
}

void appendDecodedTypes(StructureSemanticContext &context,
                        StructureRow *summaryRow,
                        const DexHeader &header,
                        const std::vector<DexString> &strings)
{
    if (!summaryRow || !checkedTableRead(header, header.typeIdsOff, uint64_t(header.typeIdsSize) * 4))
        return;

    StructureRow *typesRow = context.appendSemanticRow(summaryRow, QStringLiteral("Decoded Types"), QStringLiteral("{...}"));
    if (!typesRow)
        return;

    const uint32_t limit = std::min<uint32_t>(header.typeIdsSize, kMaxDecodedTypeRows);
    for (uint32_t i = 0; i < limit; ++i)
    {
        uint32_t descriptorIndex = 0;
        if (!read32(context, uint64_t(header.typeIdsOff) + uint64_t(i) * 4, &descriptorIndex)
            || descriptorIndex >= strings.size()
            || strings[descriptorIndex].text.isEmpty())
        {
            continue;
        }

        context.appendSemanticRow(typesRow,
                                  indexedName(QStringLiteral("Type"), i, strings[descriptorIndex].text),
                                  QStringLiteral("string %1").arg(descriptorIndex),
                                  context.baseOffset() + header.typeIdsOff + uint64_t(i) * 4,
                                  4);
    }
}

QString typeNameForIndex(StructureSemanticContext &context,
                         const DexHeader &header,
                         const std::vector<DexString> &strings,
                         uint32_t typeIndex)
{
    if (typeIndex >= header.typeIdsSize)
        return {};

    uint32_t descriptorIndex = 0;
    if (!read32(context, uint64_t(header.typeIdsOff) + uint64_t(typeIndex) * 4, &descriptorIndex)
        || descriptorIndex >= strings.size())
    {
        return {};
    }

    return strings[descriptorIndex].text;
}

void appendDecodedMethods(StructureSemanticContext &context,
                          StructureRow *summaryRow,
                          const DexHeader &header,
                          const std::vector<DexString> &strings)
{
    if (!summaryRow || !checkedTableRead(header, header.methodIdsOff, uint64_t(header.methodIdsSize) * 8))
        return;

    StructureRow *methodsRow = context.appendSemanticRow(summaryRow, QStringLiteral("Decoded Methods"), QStringLiteral("{...}"));
    if (!methodsRow)
        return;

    const uint32_t limit = std::min<uint32_t>(header.methodIdsSize, kMaxDecodedMethodRows);
    for (uint32_t i = 0; i < limit; ++i)
    {
        const uint64_t methodOffset = uint64_t(header.methodIdsOff) + uint64_t(i) * 8;
        uint16_t classIdx = 0;
        uint16_t protoIdx = 0;
        uint32_t nameIdx = 0;
        if (!context.readUInt16(context.baseOffset() + methodOffset, &classIdx)
            || !context.readUInt16(context.baseOffset() + methodOffset + 2, &protoIdx)
            || !read32(context, methodOffset + 4, &nameIdx)
            || nameIdx >= strings.size()
            || strings[nameIdx].text.isEmpty())
        {
            continue;
        }

        const QString className = typeNameForIndex(context, header, strings, classIdx);
        const QString value = className.isEmpty()
            ? QStringLiteral("proto %1").arg(protoIdx)
            : QStringLiteral("%1  proto %2").arg(className).arg(protoIdx);
        context.appendSemanticRow(methodsRow,
                                  indexedName(QStringLiteral("Method"), i, strings[nameIdx].text),
                                  value,
                                  context.baseOffset() + methodOffset,
                                  8);
    }
}

void appendDecodedFields(StructureSemanticContext &context,
                         StructureRow *summaryRow,
                         const DexHeader &header,
                         const std::vector<DexString> &strings)
{
    if (!summaryRow || !checkedTableRead(header, header.fieldIdsOff, uint64_t(header.fieldIdsSize) * 8))
        return;

    StructureRow *fieldsRow = context.appendSemanticRow(summaryRow, QStringLiteral("Decoded Fields"), QStringLiteral("{...}"));
    if (!fieldsRow)
        return;

    const uint32_t limit = std::min<uint32_t>(header.fieldIdsSize, kMaxDecodedFieldRows);
    for (uint32_t i = 0; i < limit; ++i)
    {
        const uint64_t fieldOffset = uint64_t(header.fieldIdsOff) + uint64_t(i) * 8;
        uint16_t classIdx = 0;
        uint16_t typeIdx = 0;
        uint32_t nameIdx = 0;
        if (!context.readUInt16(context.baseOffset() + fieldOffset, &classIdx)
            || !context.readUInt16(context.baseOffset() + fieldOffset + 2, &typeIdx)
            || !read32(context, fieldOffset + 4, &nameIdx)
            || nameIdx >= strings.size()
            || displayString(strings[nameIdx].text).isEmpty())
        {
            continue;
        }

        const QString className = typeNameForIndex(context, header, strings, classIdx);
        const QString typeName = typeNameForIndex(context, header, strings, typeIdx);
        const QString value = QStringLiteral("%1 : %2")
            .arg(displayString(className).isEmpty() ? QStringLiteral("class %1").arg(classIdx) : displayString(className))
            .arg(displayString(typeName).isEmpty() ? QStringLiteral("type %1").arg(typeIdx) : displayString(typeName));

        context.appendSemanticRow(fieldsRow,
                                  indexedName(QStringLiteral("Field"), i, strings[nameIdx].text),
                                  value,
                                  context.baseOffset() + fieldOffset,
                                  8);
    }
}

void appendDecodedClasses(StructureSemanticContext &context,
                          StructureRow *summaryRow,
                          const DexHeader &header,
                          const std::vector<DexString> &strings)
{
    if (!summaryRow || !checkedTableRead(header, header.classDefsOff, uint64_t(header.classDefsSize) * 32))
        return;

    StructureRow *classesRow = context.appendSemanticRow(summaryRow, QStringLiteral("Decoded Classes"), QStringLiteral("{...}"));
    if (!classesRow)
        return;

    const uint32_t limit = std::min<uint32_t>(header.classDefsSize, kMaxDecodedClassRows);
    for (uint32_t i = 0; i < limit; ++i)
    {
        const uint64_t classOffset = uint64_t(header.classDefsOff) + uint64_t(i) * 32;
        uint32_t classIdx = 0;
        uint32_t accessFlags = 0;
        uint32_t superclassIdx = 0;
        uint32_t sourceFileIdx = 0;
        if (!read32(context, classOffset + 0, &classIdx)
            || !read32(context, classOffset + 4, &accessFlags)
            || !read32(context, classOffset + 8, &superclassIdx)
            || !read32(context, classOffset + 16, &sourceFileIdx)
            || classIdx >= header.typeIdsSize)
        {
            continue;
        }

        const QString className = displayString(typeNameForIndex(context, header, strings, classIdx));
        if (className.isEmpty())
            continue;

        QStringList details;
        details.append(QStringLiteral("flags 0x%1").arg(accessFlags, 8, 16, QLatin1Char('0')).toUpper());
        if (superclassIdx != 0xffffffffu && superclassIdx < header.typeIdsSize)
        {
            const QString superclassName = displayString(typeNameForIndex(context, header, strings, superclassIdx));
            if (!superclassName.isEmpty())
                details.append(QStringLiteral("extends %1").arg(superclassName));
        }
        if (sourceFileIdx != 0xffffffffu && sourceFileIdx < strings.size())
        {
            const QString sourceFile = displayString(strings[sourceFileIdx].text);
            if (!sourceFile.isEmpty())
                details.append(QStringLiteral("source %1").arg(sourceFile));
        }

        context.appendSemanticRow(classesRow,
                                  indexedName(QStringLiteral("Class"), i, className),
                                  details.join(QStringLiteral("  ")),
                                  context.baseOffset() + classOffset,
                                  32);
    }
}

void interpretDexSummary(StructureSemanticContext &context)
{
    StructureRow *root = context.currentRow();
    if (!root)
        return;

    DexHeader header;
    if (!readDexHeader(context, &header))
        return;

    StructureRow *summaryRow = context.appendSemanticRow(root, QStringLiteral("DEX Summary"), QStringLiteral("{...}"));
    if (!summaryRow)
        return;

    std::vector<DexString> strings;
    appendDecodedStrings(context, summaryRow, header, &strings);
    appendDecodedTypes(context, summaryRow, header, strings);
    appendDecodedFields(context, summaryRow, header, strings);
    appendDecodedMethods(context, summaryRow, header, strings);
    appendDecodedClasses(context, summaryRow, header, strings);
}
}

void registerDexSemanticViews(StructureSemanticViewRegistry &registry)
{
    registry.registerInterpreter(QStringLiteral("dex.summary"), interpretDexSummary);
}
