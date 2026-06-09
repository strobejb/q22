//
//  dlgimport.cpp
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#include "dlgimport.h"
#include "importformat.h"
#include "dlgprogress.h"
#include "theme.h"
#include "HexView/hexview.h"

#include <QBuffer>
#include <QClipboard>
#include <QApplication>
#include <QFile>
#include <QMimeData>

#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cinttypes>

// ── Globals ───────────────────────────────────────────────────────────────────

IMPEXP_OPTIONS g_ImportOptions = { FORMAT_HEXDUMP,  SEARCHTYPE_BYTE };
IMPEXP_OPTIONS g_PasteOptions  = { FORMAT_RAWDATA,  SEARCHTYPE_BYTE };

// ── Endian helpers ────────────────────────────────────────────────────────────
// (same as in dlgexport.cpp – local statics keep them file-scoped)

static inline uint16_t imp_reverse16(uint16_t n) { return (uint16_t)((n >> 8) | (n << 8)); }
static inline uint64_t imp_reverse64(uint64_t n) {
#if defined(_MSC_VER)
    return _byteswap_uint64(n);
#else
    return __builtin_bswap64(n);
#endif
}

#define IMP_ENDIAN64(bigend, v) ((bigend) ? imp_reverse64((uint64_t)(v)) : (uint64_t)(v))

// ── ImportReader ──────────────────────────────────────────────────────────────

ImportReader::ImportReader(QIODevice *dev, ProgressReporter *reporter)
    : m_dev(dev), m_reporter(reporter)
{
}

void ImportReader::advanceProgress(qint64 consumed)
{
    if (!m_reporter) return;
    m_inputConsumed += consumed;
    if (m_inputConsumed - m_lastReported < kReportInterval) return;
    m_lastReported = m_inputConsumed;
    m_reporter->reportProgress(m_inputConsumed);
}

size_t ImportReader::read(uint8_t *buf, size_t len)
{
    if (m_reporter && m_reporter->isCancelled()) return 0;
    qint64 n = m_dev->read(reinterpret_cast<char *>(buf), (qint64)len);
    if (n > 0) advanceProgress(n);
    return (n < 0) ? 0 : (size_t)n;
}

bool ImportReader::gets(char *buf, size_t len)
{
    if (len < 2 || m_dev->atEnd()) return false;
    if (m_reporter && m_reporter->isCancelled()) return false;

    size_t i = 0;
    while (i < len - 1) {
        char ch;
        if (m_dev->read(&ch, 1) != 1) break;
        if (ch == '\r') {
            char next;
            if (m_dev->peek(&next, 1) == 1 && next == '\n')
                m_dev->read(&next, 1);   // consume the LF of CRLF
            ch = '\n';
        }
        buf[i++] = ch;
        if (ch == '\n') break;
    }

    buf[i] = '\0';
    if (i > 0) advanceProgress((qint64)i);
    return i > 0;
}

bool ImportReader::atEnd() const
{
    return m_dev->atEnd();
}

// ── JumpAndPad ────────────────────────────────────────────────────────────────

static void JumpAndPad(HexView *hv, size_w addr)
{
    hv->padToAddress(addr);
}

// ── Parse helpers ─────────────────────────────────────────────────────────────

static const char *skipspace(const char *p)
{
    while (std::isspace((unsigned char)*p)) p++;
    return p;
}

// Parse a hex run from *pp into *data/*count. Advances *pp past the digits.
// Returns true on success.
static bool hexdata(const char **pp, uint64_t *data, int *count)
{
    const char *p = *pp;
    *data  = 0;
    *count = 0;

    if (!std::isxdigit((unsigned char)*p)) return false;

    while (std::isxdigit((unsigned char)*p)) {
        *data = (*data << 4) | (unsigned)hex2dec(*p);
        (*count)++;
        p++;
    }
    // consume one trailing space or '-'
    if (std::isspace((unsigned char)*p) || *p == '-') p++;

    *pp = p;
    return true;
}

// ── Format parsers ────────────────────────────────────────────────────────────

size_w ImportText(ImportReader &fp, HexView *hv, size_w /*offset*/,
                  size_w /*length*/, IMPEXP_OPTIONS *ieopt)
{
    char   ach[512];
    size_w total = 0;

    while (fp.gets(ach, sizeof(ach)))
    {
        const char *ptr = ach;
        uint64_t addr, data;
        int      count;

        // Try to parse an address/offset at the start
        if (!hexdata(&ptr, &addr, &count))
            break;

        if (count > 2) {
            // It's an address column
            if (ieopt->fUseAddress)
                JumpAndPad(hv, (size_w)addr);
            ptr = skipspace(ptr);
        } else {
            // No address column: restart from beginning of line
            ptr = ach;
        }

        // Parse remaining hex bytes on this line
        while (*ptr) {
            if (!hexdata(&ptr, &data, &count)) break;
            int len = (count + 1) / 2;
            // Bytes were parsed most-significant-first; reverse to little-endian order
            uint64_t rev = 0;
            for (int i = 0; i < len; i++) {
                reinterpret_cast<uint8_t *>(&rev)[i] =
                    reinterpret_cast<uint8_t *>(&data)[len - 1 - i];
            }
            hv->setDataAdv(reinterpret_cast<const uint8_t *>(&rev), (size_t)len);
            total += (size_w)len;
        }
    }

    return total;
}

size_w ImportRawHex(ImportReader &fp, HexView *hv, size_w /*offset*/,
                    size_w /*length*/, IMPEXP_OPTIONS * /*ieopt*/)
{
    uint8_t tmp[256];
    size_t  len;
    size_w  total  = 0;
    uint8_t val    = 0;
    int     nibble = 0;   // 0 = waiting for high nibble, 1 = have high nibble

    while ((len = fp.read(tmp, sizeof(tmp))) > 0)
    {
        for (size_t i = 0; i < len; i++)
        {
            int d = hex2dec(tmp[i]);
            if (d >= 0) {
                if (nibble == 0) {
                    val = (uint8_t)(d << 4);
                    nibble = 1;
                } else {
                    val |= (uint8_t)d;
                    hv->setDataAdv(&val, 1);
                    total++;
                    nibble = 0;
                }
            } else if (std::isspace(tmp[i])) {
                if (nibble) {
                    // flush partial nibble as the whole byte
                    hv->setDataAdv(&val, 1);
                    total++;
                    nibble = 0;
                }
            } else {
                break;   // non-hex, non-space: stop
            }
        }
    }

    if (nibble) {   // flush leftover nibble
        hv->setDataAdv(&val, 1);
        total++;
    }

    return total;
}

size_w ImportHtml(ImportReader & /*fp*/, HexView * /*hv*/, size_w /*offset*/,
                  size_w /*length*/, IMPEXP_OPTIONS * /*ieopt*/)
{
    // HTML parsing is not implemented
    return 0;
}

size_w ImportASM(ImportReader &fp, HexView *hv, size_w /*offset*/,
                 size_w /*length*/, IMPEXP_OPTIONS *ieopt)
{
    char   ach[256];
    size_w total = 0;

    while (fp.gets(ach, sizeof(ach)))
    {
        char *ptr = ach;

        // Skip whitespace and comments
        while (*ptr && std::isspace((unsigned char)*ptr)) ptr++;
        if (*ptr == ';') continue;

        int width = 0;
        if      (std::strncmp(ptr, "db ", 3) == 0) { width = 1; ptr += 3; }
        else if (std::strncmp(ptr, "dw ", 3) == 0) { width = 2; ptr += 3; }
        else if (std::strncmp(ptr, "dd ", 3) == 0) { width = 4; ptr += 3; }
        else break;

        while (*ptr)
        {
            while (*ptr && std::isspace((unsigned char)*ptr)) ptr++;

            // Collect hex digits followed by optional 'h'/'H' suffix
            char numstr[40];
            char *np = numstr;
            bool hexnum = false;

            while (*ptr && (std::isxdigit((unsigned char)*ptr) || *ptr == 'h' || *ptr == 'H')) {
                if (*ptr == 'h' || *ptr == 'H') { hexnum = true; ptr++; break; }
                *np++ = *ptr++;
            }
            *np = '\0';
            if (np == numstr) break;

            uint64_t num = 0;
            if (hexnum)
                std::sscanf(numstr, "%" SCNx64, &num);
            else
                std::sscanf(numstr, "%" SCNu64, &num);

            num = IMP_ENDIAN64(ieopt->fBigEndian, num);

            uint8_t buf[8] = {};
            std::memcpy(buf, &num, (size_t)width);
            hv->setDataAdv(buf, (size_t)width);
            total += (size_w)width;

            // Skip comma
            while (*ptr && (*ptr == ',' || std::isspace((unsigned char)*ptr))) ptr++;
        }
    }

    return total;
}

// Simplified C/C++ array parser: handles `{ 0x12, 0x34, ... }`
size_w ImportCPP(ImportReader &fp, HexView *hv, size_w /*offset*/,
                 size_w /*length*/, IMPEXP_OPTIONS *ieopt)
{
    // Slurp the whole input
    QByteArray buf;
    uint8_t tmp[256];
    size_t len;
    while ((len = fp.read(tmp, sizeof(tmp))) > 0)
        buf.append(reinterpret_cast<const char *>(tmp), (qsizetype)len);

    const char *p    = buf.constData();
    const char *end  = p + buf.size();
    bool inBlock     = false;
    size_w total     = 0;

    // Determine element width from type keyword
    int width = 1;
    if      (std::strstr(p, "uint64_t") || std::strstr(p, "UINT64") || std::strstr(p, "QWORD"))
        width = 8;
    else if (std::strstr(p, "uint32_t") || std::strstr(p, "UINT32") || std::strstr(p, "DWORD"))
        width = 4;
    else if (std::strstr(p, "uint16_t") || std::strstr(p, "UINT16") || std::strstr(p, "WORD"))
        width = 2;

    while (p < end)
    {
        if (*p == '{')         { inBlock = true;  p++; continue; }
        if (*p == '}')         { inBlock = false; p++; continue; }
        if (*p == '/' && *(p+1) == '/') { while (p < end && *p != '\n') p++; continue; }
        if (*p == '/' && *(p+1) == '*') { p += 2; while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) p++; p += 2; continue; }

        if (!inBlock) { p++; continue; }

        // Skip whitespace/commas
        if (std::isspace((unsigned char)*p) || *p == ',') { p++; continue; }

        // Parse a number (hex: 0x... or decimal)
        uint64_t val = 0;
        bool isFloat = false;
        int  n = 0;

        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            std::sscanf(p, "%" SCNx64 "%n", &val, &n);
        } else if (std::isdigit((unsigned char)*p) || *p == '-') {
            double d;
            if (std::sscanf(p, "%lf%n", &d, &n) == 1) {
                if (width == 4) { float f = (float)d; std::memcpy(&val, &f, 4); }
                else if (width == 8) { std::memcpy(&val, &d, 8); }
                else val = (uint64_t)(int64_t)d;
                isFloat = true;
            }
        } else {
            p++; continue;
        }

        if (n <= 0) { p++; continue; }
        p += n;

        if (!isFloat)
            val = IMP_ENDIAN64(ieopt->fBigEndian, val);

        uint8_t out[8] = {};
        std::memcpy(out, &val, (size_t)width);
        hv->setDataAdv(out, (size_t)width);
        total += (size_w)width;
    }

    return total;
}

size_w ImportIntelHex(ImportReader &fp, HexView *hv, size_w /*offset*/,
                      size_w /*length*/, IMPEXP_OPTIONS *ieopt)
{
    char   ach[256];
    size_w total    = 0;
    size_w baseaddr = 0;

    while (fp.gets(ach, sizeof(ach)))
    {
        uint8_t  data[256];
        int      type  = 0;
        int      count = 0;
        unsigned long addr  = 0;

        if (!intel_to_bin(ach, &type, &count, &addr, data))
            return 0;

        switch (type)
        {
        case 0:   // data
            if (ieopt->fUseAddress)
                JumpAndPad(hv, (size_w)(addr | baseaddr));
            hv->setDataAdv(data, (size_t)count);
            total += (size_w)count;
            break;

        case 2:   // extended segment address (high 4 bits)
            baseaddr = ((size_w)addr) << 4;
            break;

        case 4:   // extended linear address (high 16 bits)
            baseaddr = ((size_w)(imp_reverse16((uint16_t)addr))) << 16;
            break;

        case 1:   // end-of-file
            return total;

        default:
            break;
        }
    }

    return total;
}

size_w ImportMotorola(ImportReader &fp, HexView *hv, size_w /*offset*/,
                      size_w /*length*/, IMPEXP_OPTIONS *ieopt)
{
    char   ach[256];
    size_w total = 0;

    while (fp.gets(ach, sizeof(ach)))
    {
        uint8_t    data[256];
        int        type  = 0;
        int        count = 0;
        unsigned long   addr  = 0;

        if (!motorola_to_bin(ach, &type, &count, &addr, data))
            return 0;

        switch (type)
        {
        case 1: case 2: case 3:   // data records (16/24/32-bit address)
            if (ieopt->fUseAddress)
                JumpAndPad(hv, (size_w)addr);
            hv->setDataAdv(data, (size_t)count);
            total += (size_w)count;
            break;

        case 7: case 8: case 9:   // termination records
            return total;

        default:
            break;
        }
    }

    return total;
}

size_w ImportBase64(ImportReader &fp, HexView *hv, size_w /*offset*/,
                    size_w /*length*/, IMPEXP_OPTIONS * /*ieopt*/)
{
    char   line[128];
    uint8_t buf[100];
    size_w  total = 0;

    while (fp.gets(line, sizeof(line)))
    {
        size_t alen = std::strlen(line);

        // Skip PEM-style headers starting with '-' or whitespace-only lines
        if (line[0] == '-' || std::isspace((unsigned char)line[0]))
            continue;

        size_t len = base64_decode(line, alen, buf);
        if (len == 0) break;

        hv->setDataAdv(buf, len);
        total += (size_w)len;
    }

    return total;
}

size_w ImportUUEncode(ImportReader &fp, HexView *hv, size_w /*offset*/,
                      size_w /*length*/, IMPEXP_OPTIONS * /*ieopt*/)
{
    char   line[256];
    uint8_t buf[200];
    size_w  total = 0;

    // Skip lines until we hit 'begin'
    while (fp.gets(line, sizeof(line))) {
        if (std::strncmp(line, "begin ", 6) == 0) break;
    }

    while (fp.gets(line, sizeof(line)))
    {
        if (std::strncmp(line, "end", 3) == 0) break;

        size_t alen = std::strlen(line);
        if (alen == 0 || line[alen - 1] != '\n') break;

        size_t len = uu_decode(line, alen, buf);
        if (len == 0) break;

        hv->setDataAdv(buf, len);
        total += (size_w)len;
    }

    return total;
}

// ── Top-level dispatcher ──────────────────────────────────────────────────────

namespace {
struct ImportGroup {
    HexView *hv;
    explicit ImportGroup(HexView *hv) : hv(hv) { hv->beginGroup(); }
    ~ImportGroup()                              { hv->endGroup(); }
};
}

size_w Import(QIODevice *dev, HexView *hv, IMPEXP_OPTIONS *ieopt,
              ProgressReporter *reporter)
{
    size_w offset = hv->selectionStart();
    ieopt->linelen = hv->getLineLen();

    ImportReader reader(dev, reporter);
    ImportGroup  group(hv);

    hv->setCurPos(offset);
    hv->setUpdatesEnabled(false);

    size_w count = 0;

    switch (ieopt->format)
    {
    case FORMAT_RAWDATA:
        // Raw data: read bytes directly and insert at cursor
        {
            uint8_t buf[4096];
            size_t  len;
            while ((len = reader.read(buf, sizeof(buf))) > 0) {
                hv->setDataAdv(buf, len);
                count += (size_w)len;
            }
        }
        break;

    case FORMAT_HEXDUMP:   count = ImportText      (reader, hv, offset, 0, ieopt); break;
    case FORMAT_RAWHEX:    count = ImportRawHex    (reader, hv, offset, 0, ieopt); break;
    case FORMAT_HTML:      count = ImportHtml      (reader, hv, offset, 0, ieopt); break;
    case FORMAT_CPP:       count = ImportCPP       (reader, hv, offset, 0, ieopt); break;
    case FORMAT_ASM:       count = ImportASM       (reader, hv, offset, 0, ieopt); break;
    case FORMAT_INTELHEX:  count = ImportIntelHex  (reader, hv, offset, 0, ieopt); break;
    case FORMAT_SRECORD:   count = ImportMotorola  (reader, hv, offset, 0, ieopt); break;
    case FORMAT_BASE64:    count = ImportBase64    (reader, hv, offset, 0, ieopt); break;
    case FORMAT_UUENCODE:  count = ImportUUEncode  (reader, hv, offset, 0, ieopt); break;
    default: break;
    }

    hv->setSelStart(offset);
    hv->setSelEnd(offset + count);
    hv->setUpdatesEnabled(true);

    return count;
}

size_w ImportFile(const QString &path, HexView *hv, IMPEXP_OPTIONS *ieopt,
                  QWidget *parent)
{
    QFile file(path);
    QIODevice::OpenMode mode = (ieopt->format == FORMAT_RAWDATA)
        ? QIODevice::ReadOnly
        : QIODevice::ReadOnly | QIODevice::Text;

    if (!file.open(mode))
        return 0;

    ProgressDialog dlg(file.size(), QObject::tr("Importing…"), parent);
    prepareDialogForShow(&dlg);
    dlg.open();
    QApplication::processEvents();

    SyncProgressReporter reporter(&dlg);
    size_w result = Import(&file, hv, ieopt, &reporter);

    dlg.close();
    return result;
}

// ── PasteSpecial ──────────────────────────────────────────────────────────────

bool PasteSpecial(HexView *hv, bool fMaskSel,
                  const QString &mimeType, IMPEXP_OPTIONS *ieopt)
{
    const QMimeData *md = QApplication::clipboard()->mimeData();
    if (!md || !md->hasFormat(mimeType))
        return false;

    QByteArray raw = md->data(mimeType);

    // If masking, cap the input to the current selection size
    if (fMaskSel) {
        size_w selLen = hv->selectionSize();
        if (selLen > 0 && (size_w)raw.size() > selLen)
            raw.truncate((qsizetype)selLen);
    }

    if (ieopt->format == FORMAT_RAWDATA) {
        // Paste raw bytes directly
        hv->setDataAdv(reinterpret_cast<const uint8_t *>(raw.constData()),
                       (size_t)raw.size());
        return true;
    }

    // Otherwise interpret the data through the import engine
    QBuffer buf(&raw);
    buf.open(QIODevice::ReadOnly);
    return Import(&buf, hv, ieopt) > 0;
}
