#include "disasm/elfmetadata.h"

#include "HexView/hexview.h"

namespace {

constexpr uint32_t kElfMagic        = 0x464C457F; // "\x7fELF" read little-endian as a u32
constexpr int      kElfClass32      = 1;
constexpr int      kElfClass64      = 2;
constexpr uint32_t kShtNobits        = 8;
constexpr uint32_t kShtSymtab        = 2;
constexpr uint32_t kShtDynsym        = 11;
constexpr uint32_t kShfExecinstr     = 0x4;
constexpr uint8_t  kSttFunc          = 2; // st_info & 0xf
constexpr uint8_t  kStbLocal         = 0; // (st_info >> 4) & 0xf
constexpr uint16_t kShnUndef         = 0;
constexpr int      kMaxSections      = 4096;  // sanity cap against a corrupt/hostile count
constexpr int      kMaxSymbols       = 262144;

QByteArray readBytes(const ElfByteReader &reader, uint64_t offset, int len)
{
    QByteArray buf(len, '\0');
    const size_t got = reader(offset, reinterpret_cast<uint8_t *>(buf.data()), static_cast<size_t>(len));
    buf.resize(static_cast<int>(got));
    return buf;
}

uint16_t readU16(const QByteArray &buf, int offset)
{
    if (offset < 0 || offset + 2 > buf.size())
        return 0;
    const auto *p = reinterpret_cast<const uint8_t *>(buf.constData()) + offset;
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readU32(const QByteArray &buf, int offset)
{
    if (offset < 0 || offset + 4 > buf.size())
        return 0;
    const auto *p = reinterpret_cast<const uint8_t *>(buf.constData()) + offset;
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t readU64(const QByteArray &buf, int offset)
{
    if (offset < 0 || offset + 8 > buf.size())
        return 0;
    const auto *p = reinterpret_cast<const uint8_t *>(buf.constData()) + offset;
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | p[i];
    return v;
}

QString readCStringFrom(const QByteArray &table, int offset)
{
    if (offset < 0 || offset >= table.size())
        return {};
    const int nul = table.indexOf('\0', offset);
    return QString::fromLatin1(table.mid(offset, nul >= 0 ? nul - offset : -1));
}

} // namespace

std::optional<uint64_t> vaddrToFileOffset(const ElfMetadata &elf, uint64_t vaddr)
{
    for (const ElfSection &section : elf.sections)
    {
        if (section.virtualAddress == 0)
            continue; // not mapped (e.g. .shstrtab, debug sections)
        if (vaddr >= section.virtualAddress && vaddr < section.virtualAddress + section.virtualSize)
        {
            const uint64_t delta = vaddr - section.virtualAddress;
            if (delta < section.fileSize)
                return section.fileOffset + delta;
            return std::nullopt; // SHT_NOBITS, or falls into a zero-filled tail
        }
    }
    return std::nullopt;
}

ElfMetadata readElfMetadata(const ElfByteReader &reader, uint64_t fileSize)
{
    ElfMetadata elf;
    if (!reader || fileSize < 64)
        return elf;

    const QByteArray ident = readBytes(reader, 0, 16);
    if (readU32(ident, 0) != kElfMagic)
        return elf;

    const int elfClass = ident.size() > 4 ? static_cast<uint8_t>(ident.at(4)) : 0;
    if (elfClass != kElfClass32 && elfClass != kElfClass64)
        return elf;
    elf.is64Bit = (elfClass == kElfClass64);

    const int ehdrSize = elf.is64Bit ? 64 : 52;
    const QByteArray ehdr = readBytes(reader, 0, ehdrSize);
    if (ehdr.size() < ehdrSize)
        return elf;

    elf.machine = readU16(ehdr, 18);

    uint64_t shoff;
    uint16_t shentsize, shnum, shstrndx;
    if (elf.is64Bit)
    {
        elf.entryVaddr = readU64(ehdr, 24);
        shoff     = readU64(ehdr, 40);
        shentsize = readU16(ehdr, 58);
        shnum     = readU16(ehdr, 60);
        shstrndx  = readU16(ehdr, 62);
    }
    else
    {
        elf.entryVaddr = readU32(ehdr, 24);
        shoff     = readU32(ehdr, 32);
        shentsize = readU16(ehdr, 46);
        shnum     = readU16(ehdr, 48);
        shstrndx  = readU16(ehdr, 50);
    }

    const int shdrSize = elf.is64Bit ? 64 : 40;
    const int sectionCount = qBound(0, static_cast<int>(shnum), kMaxSections);
    if (sectionCount == 0 || shentsize < shdrSize)
    {
        elf.isValid = true; // a stripped/degenerate ELF is still a valid ELF, just nothing to scan
        return elf;
    }
    const QByteArray sectionTable = readBytes(reader, shoff, sectionCount * shdrSize);

    // Raw (sh_name offset, parsed fields) per section, names resolved once
    // .shstrtab's own bytes are known below.
    struct RawSection { uint32_t nameOffset; ElfSection section; uint32_t type; };
    std::vector<RawSection> rawSections;
    rawSections.reserve(sectionCount);
    for (int i = 0; i < sectionCount; ++i)
    {
        const int base = i * shdrSize;
        RawSection raw;
        raw.nameOffset = readU32(sectionTable, base + 0);
        raw.type       = readU32(sectionTable, base + 4);
        ElfSection &s  = raw.section;
        if (elf.is64Bit)
        {
            const uint64_t flags = readU64(sectionTable, base + 8);
            s.virtualAddress = readU64(sectionTable, base + 16);
            s.fileOffset     = readU64(sectionTable, base + 24);
            s.virtualSize    = readU64(sectionTable, base + 32);
            s.fileSize       = (raw.type == kShtNobits) ? 0 : s.virtualSize;
            s.executable     = (flags & kShfExecinstr) != 0;
        }
        else
        {
            const uint32_t flags = readU32(sectionTable, base + 8);
            s.virtualAddress = readU32(sectionTable, base + 12);
            s.fileOffset     = readU32(sectionTable, base + 16);
            s.virtualSize    = readU32(sectionTable, base + 20);
            s.fileSize       = (raw.type == kShtNobits) ? 0 : s.virtualSize;
            s.executable     = (flags & kShfExecinstr) != 0;
        }
        rawSections.push_back(raw);
    }

    QByteArray shstrtab;
    if (shstrndx < rawSections.size())
    {
        const ElfSection &strSec = rawSections[shstrndx].section;
        shstrtab = readBytes(reader, strSec.fileOffset, static_cast<int>(qMin<uint64_t>(strSec.fileSize, 1u * 1024 * 1024)));
    }

    elf.sections.reserve(rawSections.size());
    for (RawSection &raw : rawSections)
    {
        raw.section.name = readCStringFrom(shstrtab, static_cast<int>(raw.nameOffset)).toLatin1();
        elf.sections.push_back(raw.section);
    }

    elf.isValid = true;

    // Symbol table: prefer .dynsym (the dynamically-exported symbols, the
    // closest ELF analog of a PE export table) and fall back to .symtab for
    // statically-linked binaries that have no dynamic symbol table at all.
    int symtabIdx = -1;
    int dynsymIdx = -1;
    for (size_t i = 0; i < rawSections.size(); ++i)
    {
        if (rawSections[i].type == kShtDynsym && dynsymIdx < 0)
            dynsymIdx = static_cast<int>(i);
        else if (rawSections[i].type == kShtSymtab && symtabIdx < 0)
            symtabIdx = static_cast<int>(i);
    }
    const int symSecIdx = dynsymIdx >= 0 ? dynsymIdx : symtabIdx;
    if (symSecIdx >= 0)
    {
        const RawSection &symRaw = rawSections[symSecIdx];
        const uint32_t linkIdx = readU32(sectionTable, symSecIdx * shdrSize + 24);
        if (linkIdx < rawSections.size() && symRaw.section.fileSize > 0)
        {
            const ElfSection &strSec = rawSections[linkIdx].section;
            const QByteArray strtab = readBytes(reader, strSec.fileOffset,
                                                 static_cast<int>(qMin<uint64_t>(strSec.fileSize, 16u * 1024 * 1024)));

            const int symEntrySize = elf.is64Bit ? 24 : 16;
            const int symCount = qBound(0, static_cast<int>(symRaw.section.fileSize / symEntrySize), kMaxSymbols);
            const QByteArray symTable = readBytes(reader, symRaw.section.fileOffset, symCount * symEntrySize);

            elf.exports.reserve(symCount);
            for (int i = 0; i < symCount; ++i)
            {
                const int base = i * symEntrySize;
                uint32_t nameOffset;
                uint8_t  info;
                uint16_t shndx;
                uint64_t value;
                if (elf.is64Bit)
                {
                    nameOffset = readU32(symTable, base + 0);
                    info       = static_cast<uint8_t>(symTable.size() > base + 4 ? symTable.at(base + 4) : 0);
                    shndx      = readU16(symTable, base + 6);
                    value      = readU64(symTable, base + 8);
                }
                else
                {
                    nameOffset = readU32(symTable, base + 0);
                    value      = readU32(symTable, base + 4);
                    info       = static_cast<uint8_t>(symTable.size() > base + 12 ? symTable.at(base + 12) : 0);
                    shndx      = readU16(symTable, base + 14);
                }

                const uint8_t type = info & 0xF;
                const uint8_t bind = (info >> 4) & 0xF;
                if (type != kSttFunc || bind == kStbLocal || shndx == kShnUndef || value == 0)
                    continue;

                const QString name = readCStringFrom(strtab, static_cast<int>(nameOffset));
                if (!name.isEmpty())
                    elf.exports.push_back({name, value});
            }
        }
    }

    return elf;
}

ElfMetadata readElfMetadata(HexView *hv)
{
    if (!hv)
        return {};
    ElfByteReader reader = [hv](uint64_t offset, uint8_t *buf, size_t len) {
        return hv->getData(static_cast<size_w>(offset), buf, len);
    };
    return readElfMetadata(reader, static_cast<uint64_t>(hv->size()));
}
