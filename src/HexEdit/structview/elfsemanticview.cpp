#include "structview/elfsemanticview.h"

#include "structview/structuresemanticview.h"

#include <QString>

#include <algorithm>
#include <vector>

namespace
{
static constexpr uint8_t kElfClass32 = 1;
static constexpr uint8_t kElfClass64 = 2;
static constexpr uint8_t kElfDataMsb = 2;
static constexpr uint32_t kShtNull = 0;
static constexpr uint32_t kShtSymtab = 2;
static constexpr uint32_t kShtDynsym = 11;
static constexpr size_t kMaxSections = 512;
static constexpr size_t kMaxSymbols = 2048;

uint16_t u16(const uint8_t *data, bool bigEndian)
{
    return bigEndian
        ? uint16_t((uint16_t(data[0]) << 8) | uint16_t(data[1]))
        : uint16_t(uint16_t(data[0]) | (uint16_t(data[1]) << 8));
}

uint32_t u32(const uint8_t *data, bool bigEndian)
{
    return bigEndian
        ? (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) | (uint32_t(data[2]) << 8) | uint32_t(data[3])
        : uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
}

uint64_t u64(const uint8_t *data, bool bigEndian)
{
    uint64_t value = 0;
    if (bigEndian)
    {
        for (int i = 0; i < 8; ++i)
            value = (value << 8) | data[i];
    }
    else
    {
        for (int i = 0; i < 8; ++i)
            value |= uint64_t(data[i]) << (i * 8);
    }
    return value;
}

bool readBlock(StructureSemanticContext &context, uint64_t absoluteOffset, uint8_t *data, size_t length)
{
    return context.readBytes(context.baseOffset() + absoluteOffset, data, length);
}

bool read16(StructureSemanticContext &context, uint64_t offset, bool bigEndian, uint16_t *value)
{
    uint8_t data[2] = {};
    if (!value || !readBlock(context, offset, data, sizeof(data)))
        return false;

    *value = u16(data, bigEndian);
    return true;
}

bool read32(StructureSemanticContext &context, uint64_t offset, bool bigEndian, uint32_t *value)
{
    uint8_t data[4] = {};
    if (!value || !readBlock(context, offset, data, sizeof(data)))
        return false;

    *value = u32(data, bigEndian);
    return true;
}

bool read64(StructureSemanticContext &context, uint64_t offset, bool bigEndian, uint64_t *value)
{
    uint8_t data[8] = {};
    if (!value || !readBlock(context, offset, data, sizeof(data)))
        return false;

    *value = u64(data, bigEndian);
    return true;
}

struct ElfHeader
{
    bool is64 = false;
    bool bigEndian = false;
    uint64_t sectionHeaderOffset = 0;
    uint16_t sectionHeaderEntrySize = 0;
    uint16_t sectionHeaderCount = 0;
    uint16_t sectionNameStringIndex = 0;
};

struct ElfSection
{
    uint32_t nameOffset = 0;
    uint32_t type = 0;
    uint64_t fileOffset = 0;
    uint64_t size = 0;
    uint32_t link = 0;
    uint64_t entrySize = 0;
    QString name;
};

bool readElfHeader(StructureSemanticContext &context, ElfHeader *header)
{
    if (!header)
        return false;

    uint8_t ident[16] = {};
    if (!readBlock(context, 0, ident, sizeof(ident)))
        return false;

    if (ident[0] != 0x7f || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F')
        return false;

    header->is64 = ident[4] == kElfClass64;
    if (ident[4] != kElfClass32 && ident[4] != kElfClass64)
        return false;

    header->bigEndian = ident[5] == kElfDataMsb;

    if (header->is64)
    {
        return read64(context, 40, header->bigEndian, &header->sectionHeaderOffset)
               && read16(context, 58, header->bigEndian, &header->sectionHeaderEntrySize)
               && read16(context, 60, header->bigEndian, &header->sectionHeaderCount)
               && read16(context, 62, header->bigEndian, &header->sectionNameStringIndex);
    }

    uint32_t sectionHeaderOffset = 0;
    if (!read32(context, 32, header->bigEndian, &sectionHeaderOffset)
        || !read16(context, 46, header->bigEndian, &header->sectionHeaderEntrySize)
        || !read16(context, 48, header->bigEndian, &header->sectionHeaderCount)
        || !read16(context, 50, header->bigEndian, &header->sectionNameStringIndex))
    {
        return false;
    }

    header->sectionHeaderOffset = sectionHeaderOffset;
    return true;
}

bool readSection(StructureSemanticContext &context,
                 const ElfHeader &header,
                 uint64_t tableOffset,
                 uint16_t entrySize,
                 size_t index,
                 ElfSection *section)
{
    if (!section)
        return false;

    const uint64_t offset = tableOffset + uint64_t(index) * entrySize;
    if (header.is64)
    {
        return read32(context, offset + 0, header.bigEndian, &section->nameOffset)
               && read32(context, offset + 4, header.bigEndian, &section->type)
               && read64(context, offset + 24, header.bigEndian, &section->fileOffset)
               && read64(context, offset + 32, header.bigEndian, &section->size)
               && read32(context, offset + 40, header.bigEndian, &section->link)
               && read64(context, offset + 56, header.bigEndian, &section->entrySize);
    }

    uint32_t fileOffset = 0;
    uint32_t size = 0;
    uint32_t entrySize32 = 0;
    if (!read32(context, offset + 0, header.bigEndian, &section->nameOffset)
        || !read32(context, offset + 4, header.bigEndian, &section->type)
        || !read32(context, offset + 16, header.bigEndian, &fileOffset)
        || !read32(context, offset + 20, header.bigEndian, &size)
        || !read32(context, offset + 24, header.bigEndian, &section->link)
        || !read32(context, offset + 36, header.bigEndian, &entrySize32))
    {
        return false;
    }

    section->fileOffset = fileOffset;
    section->size = size;
    section->entrySize = entrySize32;
    return true;
}

QString sectionName(StructureSemanticContext &context, const ElfSection &stringTable, uint32_t nameOffset)
{
    if (nameOffset >= stringTable.size)
        return {};

    return context.readAsciiString(context.baseOffset() + stringTable.fileOffset + nameOffset,
                                   static_cast<size_t>(stringTable.size - nameOffset));
}

QString symbolName(StructureSemanticContext &context, const ElfSection &stringTable, uint32_t nameOffset)
{
    if (nameOffset >= stringTable.size)
        return {};

    return context.readAsciiString(context.baseOffset() + stringTable.fileOffset + nameOffset,
                                   static_cast<size_t>(stringTable.size - nameOffset));
}

QString symbolValueText(uint64_t value, uint64_t size)
{
    return QStringLiteral("value 0x%1, size %2")
        .arg(QString::number(value, 16).toUpper())
        .arg(size);
}

void appendSymbols(StructureSemanticContext &context,
                   StructureRow *sectionRow,
                   const ElfHeader &header,
                   const ElfSection &symbolSection,
                   const ElfSection &stringTable)
{
    if (!sectionRow || symbolSection.fileOffset == 0 || symbolSection.size == 0)
        return;

    const uint64_t entrySize = symbolSection.entrySize ? symbolSection.entrySize : (header.is64 ? 24 : 16);
    if (entrySize == 0)
        return;

    const uint64_t count = std::min<uint64_t>(symbolSection.size / entrySize, kMaxSymbols);
    for (uint64_t i = 0; i < count; ++i)
    {
        const uint64_t entryOffset = symbolSection.fileOffset + i * entrySize;
        uint32_t nameOffset = 0;
        uint64_t value = 0;
        uint64_t size = 0;

        if (header.is64)
        {
            if (!read32(context, entryOffset + 0, header.bigEndian, &nameOffset)
                || !read64(context, entryOffset + 8, header.bigEndian, &value)
                || !read64(context, entryOffset + 16, header.bigEndian, &size))
            {
                return;
            }
        }
        else
        {
            uint32_t value32 = 0;
            uint32_t size32 = 0;
            if (!read32(context, entryOffset + 0, header.bigEndian, &nameOffset)
                || !read32(context, entryOffset + 4, header.bigEndian, &value32)
                || !read32(context, entryOffset + 8, header.bigEndian, &size32))
            {
                return;
            }
            value = value32;
            size = size32;
        }

        const QString name = symbolName(context, stringTable, nameOffset);
        if (name.isEmpty())
            continue;

        StructureRow *symbolRow = context.appendSemanticRow(sectionRow,
                                                            QStringLiteral("SYMBOL %1").arg(name),
                                                            symbolValueText(value, size),
                                                            context.baseOffset() + entryOffset,
                                                            entrySize);
        if (symbolRow)
            symbolRow->setNameParts(QStringLiteral("SYMBOL"), name);
    }
}

void interpretElfSections(StructureSemanticContext &context)
{
    StructureRow *root = context.currentRow();
    if (!root)
        return;

    ElfHeader header;
    if (!readElfHeader(context, &header) || header.sectionHeaderOffset == 0 || header.sectionHeaderCount == 0)
        return;

    const uint16_t entrySize = header.sectionHeaderEntrySize
        ? header.sectionHeaderEntrySize
        : uint16_t(header.is64 ? 64 : 40);
    const size_t count = std::min<size_t>(header.sectionHeaderCount, kMaxSections);

    std::vector<ElfSection> sections(count);
    for (size_t i = 0; i < count; ++i)
    {
        if (!readSection(context, header, header.sectionHeaderOffset, entrySize, i, &sections[i]))
            return;
    }

    if (header.sectionNameStringIndex < sections.size())
    {
        const ElfSection &stringTable = sections[header.sectionNameStringIndex];
        for (ElfSection &section : sections)
            section.name = sectionName(context, stringTable, section.nameOffset);
    }

    std::vector<StructureRow *> sectionRows(sections.size(), nullptr);
    for (size_t i = 0; i < sections.size(); ++i)
    {
        const ElfSection &section = sections[i];
        if (section.type == kShtNull && section.name.isEmpty())
            continue;

        const QString sectionLabel = section.name.isEmpty()
            ? QStringLiteral("[%1]").arg(i)
            : section.name;
        const QString displayName = QStringLiteral("SECTION %1").arg(sectionLabel);
        sectionRows[i] = context.appendSemanticRow(root,
                                                   displayName,
                                                   QStringLiteral("{...}"),
                                                   context.baseOffset() + section.fileOffset,
                                                   section.size);
        if (sectionRows[i])
        {
            sectionRows[i]->setNameParts(QStringLiteral("SECTION"), sectionLabel, QString(), true);
        }
    }

    for (size_t i = 0; i < sections.size(); ++i)
    {
        const ElfSection &section = sections[i];
        if ((section.type != kShtSymtab && section.type != kShtDynsym)
            || section.link >= sections.size()
            || !sectionRows[i])
        {
            continue;
        }

        appendSymbols(context, sectionRows[i], header, section, sections[section.link]);
    }
}
}

void registerElfSemanticViews(StructureSemanticViewRegistry &registry)
{
    registry.registerInterpreter(QStringLiteral("elf.sections"), interpretElfSections);
    registry.registerInterpreter(QStringLiteral("elf.symbols"), interpretElfSections);
}
