#ifndef DISASM_PEMETADATA_H
#define DISASM_PEMETADATA_H

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class HexView;

struct PeSection
{
    QByteArray name;
    uint64_t   virtualAddress = 0;
    uint64_t   virtualSize    = 0;
    uint64_t   fileOffset     = 0;
    uint64_t   fileSize       = 0;
    bool       executable     = false;
};

struct PeExport
{
    QString  name;
    uint64_t rva = 0;
};

struct PeMetadata
{
    bool isValid          = false;
    bool is64Bit          = false;
    uint64_t entryPointRva = 0;
    std::vector<PeSection> sections;
    std::vector<PeExport>  exports;
};

// Reads up to `len` bytes starting at `offset` into `buf`, returning how many
// were actually read (short on EOF, same contract as HexView::getData()).
// Abstracted so the same parsing logic works whether the caller reads via a
// live HexView (cheap, main-thread-only) or its own independently-opened
// QFile (background-thread-safe -- HexView::getData() is not safe to call
// off the GUI thread, since it reads through the live, possibly-mutating
// edit buffer).
using PeByteReader = std::function<size_t(uint64_t offset, uint8_t *buf, size_t len)>;

// Reads section table + (named) exports directly off raw bytes, using the
// fixed PE32/PE32+ byte layout -- no causeway/Parser involvement. isValid is
// false for anything that isn't a well-formed MZ/PE file.
PeMetadata readPeMetadata(const PeByteReader &reader, uint64_t fileSize);

// Convenience overload for synchronous, main-thread-only callers that already
// have a HexView at hand.
PeMetadata readPeMetadata(HexView *hv);

// Translates a relative virtual address into a file offset by locating the
// section whose virtual range contains it; nullopt if rva isn't mapped by
// any section (e.g. it lies in the headers, or points past a section's
// raw data into its zero-padded virtual tail).
std::optional<uint64_t> rvaToFileOffset(const PeMetadata &pe, uint64_t rva);

#endif // DISASM_PEMETADATA_H
