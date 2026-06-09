#pragma once

#include "HexView/seqbase.h"

#include <QIODevice>
#include <QString>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

// ── Progress reporting ────────────────────────────────────────────────────────
// Pure interface; widget-free. ProgressDialog implements this in dlgprogress.h.
class ProgressReporter
{
  public:
    virtual ~ProgressReporter()                            = default;
    virtual void reportProgress(qint64 inputBytesConsumed) = 0;
    virtual bool isCancelled() const                       = 0;
};

// ── Format enums and options ──────────────────────────────────────────────────

enum IMPEXP_FORMAT
{
    FORMAT_RAWDATA  = 0,
    FORMAT_HEXDUMP  = 1,
    FORMAT_RAWHEX   = 2,
    FORMAT_HTML     = 3,
    FORMAT_CPP      = 4,
    FORMAT_ASM      = 5,
    FORMAT_INTELHEX = 6,
    FORMAT_SRECORD  = 7,
    FORMAT_BASE64   = 8,
    FORMAT_UUENCODE = 9
};

enum SEARCHTYPE
{
    SEARCHTYPE_HEX,
    SEARCHTYPE_ASCII,
    SEARCHTYPE_UTF8,
    SEARCHTYPE_UTF16,
    SEARCHTYPE_UTF32,
    SEARCHTYPE_BYTE,
    SEARCHTYPE_WORD,
    SEARCHTYPE_DWORD,
    SEARCHTYPE_QWORD,
    SEARCHTYPE_FLOAT,
    SEARCHTYPE_DOUBLE,
};

struct IMPEXP_OPTIONS
{
    IMPEXP_FORMAT format      = FORMAT_HEXDUMP;
    SEARCHTYPE    basetype    = SEARCHTYPE_BYTE;
    bool          fBigEndian  = false;
    bool          fAppend     = false;
    bool          fUseAddress = true;
    size_t        linelen     = 16;
};

// ── DataSource ────────────────────────────────────────────────────────────────
// Abstract read-only view of the bytes being exported. Implemented by
// HexViewSource in dlgexport.cpp; test code uses ByteArraySource.
struct DataSource
{
    virtual void getData(size_w offset, uint8_t *buf, size_t len) const = 0;

    virtual QString filePath() const
    {
        return {};
    }

    virtual ~DataSource() = default;
};

// ── ExportWriter ──────────────────────────────────────────────────────────────
// Abstract output stream wrapping a QIODevice. Back it with QFile for
// Export-to-file, or QBuffer for CopyAs. All Export* routines write through
// this so no logic is duplicated between the file and clipboard paths.
class ExportWriter
{
  public:
    explicit ExportWriter(QIODevice *dev, ProgressReporter *reporter = nullptr);

    void write(const uint8_t *buf, size_t len);
    void printf(const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;

    void advanceProgress(size_w inputConsumed);

    bool hasError() const
    {
        return m_error;
    }

  private:
    static constexpr qint64 kReportInterval = 65536;

    QIODevice        *m_dev;
    ProgressReporter *m_reporter      = nullptr;
    qint64            m_inputConsumed = 0;
    qint64            m_lastReported  = -kReportInterval;
    bool              m_error         = false;
};

// ── Individual format writers ─────────────────────────────────────────────────
bool ExportRaw(ExportWriter &fp, const DataSource &src, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportText(ExportWriter &fp, const DataSource &src, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportRawHex(ExportWriter &fp, const DataSource &src, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportHtml(ExportWriter &fp, const DataSource &src, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportASM(ExportWriter &fp, const DataSource &src, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportCPP(ExportWriter &fp, const DataSource &src, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportIntelHex(ExportWriter &fp, const DataSource &src, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportMotorola(ExportWriter &fp, const DataSource &src, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportBase64(ExportWriter &fp, const DataSource &src, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportUUEncode(ExportWriter &fp, const DataSource &src, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);

// ── Codec primitives (no Qt, no DataSource) ───────────────────────────────────
size_t base64_encode(const uint8_t *inbuf, size_t inlen, char *outbuf);
size_t uu_encode(const uint8_t *inbuf, size_t inlen, char *outbuf);
size_t intel_frame(char *hrec, int type, size_t count, unsigned long addr, const uint8_t *data);
size_t motorola_frame(char *srec, int type, size_t count, unsigned long addr, const uint8_t *data);
char   toAscii(uint8_t b);
