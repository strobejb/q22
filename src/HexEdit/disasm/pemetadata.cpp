#include "disasm/pemetadata.h"

#include "HexView/hexview.h"

namespace {

constexpr uint16_t kImageDosSignature   = 0x5A4D; // "MZ"
constexpr uint32_t kImageNtSignature    = 0x00004550; // "PE\0\0"
constexpr uint16_t kImageNtOptionalHdr32Magic = 0x10b;
constexpr uint16_t kImageNtOptionalHdr64Magic = 0x20b;
constexpr uint32_t kImageScnMemExecute  = 0x20000000;
constexpr int      kSectionHeaderSize   = 40;
constexpr int      kExportDirectorySize = 40;
constexpr int      kMaxExportEntries    = 65536; // sanity cap against a corrupt/hostile count

QByteArray readBytes(const PeByteReader &reader, uint64_t offset, int len)
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

// Reads a NUL-terminated ASCII string at a file offset, bounded to avoid
// runaway reads against a corrupt export name pointer.
QString readCString(const PeByteReader &reader, uint64_t offset, int maxLen = 256)
{
    const QByteArray buf = readBytes(reader, offset, maxLen);
    const int nul = buf.indexOf('\0');
    return QString::fromLatin1(nul >= 0 ? buf.left(nul) : buf);
}

} // namespace

std::optional<uint64_t> rvaToFileOffset(const PeMetadata &pe, uint64_t rva)
{
    for (const PeSection &section : pe.sections)
    {
        if (rva >= section.virtualAddress && rva < section.virtualAddress + section.virtualSize)
        {
            const uint64_t delta = rva - section.virtualAddress;
            if (delta < section.fileSize)
                return section.fileOffset + delta;
            return std::nullopt; // falls into the section's zero-padded virtual tail
        }
    }
    return std::nullopt;
}

PeMetadata readPeMetadata(const PeByteReader &reader, uint64_t fileSize)
{
    PeMetadata pe;
    if (!reader || fileSize < 64)
        return pe;

    const QByteArray dosHeader = readBytes(reader, 0, 64);
    if (readU16(dosHeader, 0) != kImageDosSignature)
        return pe;

    const uint64_t lfaNew = readU32(dosHeader, 0x3C);
    const QByteArray ntHeader = readBytes(reader, lfaNew, 4 + 20 + 256);
    if (readU32(ntHeader, 0) != kImageNtSignature)
        return pe;

    // IMAGE_FILE_HEADER starts right after the 4-byte "PE\0\0" signature.
    constexpr int kFileHeaderOffset = 4;
    const uint16_t numberOfSections     = readU16(ntHeader, kFileHeaderOffset + 2);
    const uint16_t sizeOfOptionalHeader = readU16(ntHeader, kFileHeaderOffset + 16);

    // IMAGE_OPTIONAL_HEADER{32,64} starts right after the 20-byte file header.
    constexpr int kOptionalHeaderOffset = kFileHeaderOffset + 20;
    const uint16_t magic = readU16(ntHeader, kOptionalHeaderOffset);
    if (magic != kImageNtOptionalHdr32Magic && magic != kImageNtOptionalHdr64Magic)
        return pe;
    pe.is64Bit = (magic == kImageNtOptionalHdr64Magic);

    // AddressOfEntryPoint sits at the same +0x10 offset in both PE32 and
    // PE32+ -- every field before it (Magic/linker versions/SizeOfCode/
    // SizeOfInitializedData/SizeOfUninitializedData) is identical in size.
    pe.entryPointRva = readU32(ntHeader, kOptionalHeaderOffset + 0x10);

    // DataDirectory[] begins at +96 (PE32) or +112 (PE32+) -- fixed per the
    // PE/COFF spec, matching pe.strata's IMAGE_OPTIONAL_HEADER field list.
    const int dataDirectoryOffset = kOptionalHeaderOffset + (pe.is64Bit ? 112 : 96);
    const uint32_t exportDirRva  = readU32(ntHeader, dataDirectoryOffset + 0);
    const uint32_t exportDirSize = readU32(ntHeader, dataDirectoryOffset + 4);

    // Section table starts right after the optional header.
    const uint64_t sectionTableOffset = lfaNew + kOptionalHeaderOffset + sizeOfOptionalHeader;
    const int sectionCount = qBound(0, static_cast<int>(numberOfSections), 96);
    const QByteArray sectionTable = readBytes(reader, sectionTableOffset, sectionCount * kSectionHeaderSize);

    pe.sections.reserve(sectionCount);
    for (int i = 0; i < sectionCount; ++i)
    {
        const int base = i * kSectionHeaderSize;
        PeSection section;
        section.name = sectionTable.mid(base, 8);
        const int nameNul = section.name.indexOf('\0');
        if (nameNul >= 0)
            section.name.truncate(nameNul);
        section.virtualSize    = readU32(sectionTable, base + 8);
        section.virtualAddress = readU32(sectionTable, base + 12);
        section.fileSize       = readU32(sectionTable, base + 16);
        section.fileOffset     = readU32(sectionTable, base + 20);
        section.executable     = (readU32(sectionTable, base + 36) & kImageScnMemExecute) != 0;
        pe.sections.push_back(section);
    }

    pe.isValid = true;

    if (exportDirRva != 0 && exportDirSize != 0)
    {
        if (auto exportDirOffset = rvaToFileOffset(pe, exportDirRva))
        {
            const QByteArray exportDir = readBytes(reader, *exportDirOffset, kExportDirectorySize);
            const uint32_t numberOfFunctions      = readU32(exportDir, 20);
            const uint32_t numberOfNames          = readU32(exportDir, 24);
            const uint32_t addressOfFunctionsRva  = readU32(exportDir, 28);
            const uint32_t addressOfNamesRva      = readU32(exportDir, 32);
            const uint32_t addressOfOrdinalsRva   = readU32(exportDir, 36);
            const int nameCount = qBound(0, static_cast<int>(numberOfNames), kMaxExportEntries);

            const auto namesOffset     = rvaToFileOffset(pe, addressOfNamesRva);
            const auto ordinalsOffset  = rvaToFileOffset(pe, addressOfOrdinalsRva);
            const auto functionsOffset = rvaToFileOffset(pe, addressOfFunctionsRva);
            if (nameCount > 0 && namesOffset && ordinalsOffset && functionsOffset)
            {
                const QByteArray names    = readBytes(reader, *namesOffset, nameCount * 4);
                const QByteArray ordinals = readBytes(reader, *ordinalsOffset, nameCount * 2);
                const QByteArray functions = readBytes(reader, *functionsOffset,
                                                        static_cast<int>(qMin<uint32_t>(numberOfFunctions, static_cast<uint32_t>(kMaxExportEntries))) * 4);

                pe.exports.reserve(nameCount);
                for (int i = 0; i < nameCount; ++i)
                {
                    const uint32_t nameRva = readU32(names, i * 4);
                    const uint16_t ordinalIndex = readU16(ordinals, i * 2);
                    if (ordinalIndex >= numberOfFunctions)
                        continue;
                    const uint32_t funcRva = readU32(functions, ordinalIndex * 4);
                    // Forwarded exports point back inside the export directory
                    // itself (a string like "OTHERDLL.Func") rather than to
                    // code in this file -- not something we can disassemble.
                    if (funcRva == 0 || (funcRva >= exportDirRva && funcRva < exportDirRva + exportDirSize))
                        continue;
                    if (const auto nameOffset = rvaToFileOffset(pe, nameRva))
                    {
                        PeExport exp;
                        exp.name = readCString(reader, *nameOffset);
                        exp.rva  = funcRva;
                        if (!exp.name.isEmpty())
                            pe.exports.push_back(exp);
                    }
                }
            }
        }
    }

    return pe;
}

PeMetadata readPeMetadata(HexView *hv)
{
    if (!hv)
        return {};
    PeByteReader reader = [hv](uint64_t offset, uint8_t *buf, size_t len) {
        return hv->getData(static_cast<size_w>(offset), buf, len);
    };
    return readPeMetadata(reader, static_cast<uint64_t>(hv->size()));
}
