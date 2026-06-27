#ifndef DISASM_ELFMETADATA_H
#define DISASM_ELFMETADATA_H

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class HexView;

struct ElfSection
{
    QByteArray name;
    uint64_t   virtualAddress = 0;
    uint64_t   virtualSize    = 0;
    uint64_t   fileOffset     = 0;
    uint64_t   fileSize       = 0; // 0 for SHT_NOBITS (e.g. .bss) -- no file-backed bytes
    bool       executable     = false;
};

struct ElfExport
{
    QString  name;
    uint64_t vaddr = 0;
};

struct ElfMetadata
{
    bool     isValid    = false;
    bool     is64Bit    = false;
    uint16_t machine    = 0; // e_machine (ELF_MACHINE enum in elf.struct, e.g. EM_X86_64 = 62)
    uint64_t entryVaddr = 0;
    std::vector<ElfSection> sections;
    std::vector<ElfExport>  exports; // global/weak STT_FUNC symbols from .dynsym (or .symtab as fallback)
};

// Same short-read contract as PeByteReader -- see pemetadata.h for why this is
// abstracted instead of reading through a live HexView directly.
using ElfByteReader = std::function<size_t(uint64_t offset, uint8_t *buf, size_t len)>;

// Reads section headers + exported function symbols directly off raw bytes,
// using the fixed ELF32/ELF64 byte layout (see causeway/strata/elf.struct) --
// no causeway/Parser involvement. isValid is false for anything that isn't a
// well-formed ELF file.
ElfMetadata readElfMetadata(const ElfByteReader &reader, uint64_t fileSize);

// Convenience overload for synchronous, main-thread-only callers that already
// have a HexView at hand.
ElfMetadata readElfMetadata(HexView *hv);

// Translates a virtual address into a file offset by locating the section
// whose virtual range contains it; nullopt if vaddr isn't backed by any
// section's file content (unmapped, in a header/gap, or inside a SHT_NOBITS
// section's zero-filled tail).
std::optional<uint64_t> vaddrToFileOffset(const ElfMetadata &elf, uint64_t vaddr);

#endif // DISASM_ELFMETADATA_H
