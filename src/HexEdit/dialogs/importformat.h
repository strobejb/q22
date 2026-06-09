#pragma once

#include "exportformat.h"

#include <QIODevice>
#include <cstddef>
#include <cstdint>

// ── DataSink ──────────────────────────────────────────────────────────────────
// Abstract write-only view of the buffer being imported into. Implemented by
// HexViewSink in dlgimport.cpp; test code uses ByteArraySink.
struct DataSink
{
    virtual void write(const uint8_t *buf, size_t len) = 0;

    virtual void padToAddress(size_w addr)
    {
    }

    virtual ~DataSink() = default;
};

// ── ImportReader ──────────────────────────────────────────────────────────────
// Abstract input stream wrapping a QIODevice. Back it with QFile for file-based
// import, or QBuffer for clipboard import. Provides fread/fgets-style access.
class ImportReader
{
  public:
    explicit ImportReader(QIODevice *dev, ProgressReporter *reporter = nullptr);

    size_t read(uint8_t *buf, size_t len);
    bool   gets(char *buf, size_t len);
    bool   atEnd() const;

  private:
    static constexpr qint64 kReportInterval = 65536;

    void advanceProgress(qint64 consumed);

    QIODevice        *m_dev;
    ProgressReporter *m_reporter      = nullptr;
    qint64            m_inputConsumed = 0;
    qint64            m_lastReported  = -kReportInterval;
};

// ── Codec primitives ──────────────────────────────────────────────────────────
int    hex2dec(int ch);
bool   intel_to_bin(const char *hrec, int *type, int *count, unsigned long *addr, uint8_t *data);
bool   motorola_to_bin(const char *srec, int *type, int *count, unsigned long *addr, uint8_t *data);
size_t base64_decode(const char *inbuf, size_t inlen, uint8_t *outbuf);
size_t uu_decode(const char *inbuf, size_t inlen, uint8_t *outbuf);

// ── Individual format parsers ─────────────────────────────────────────────────
size_w ImportText(ImportReader &fp, DataSink &sink, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportRawHex(ImportReader &fp, DataSink &sink, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportHtml(ImportReader &fp, DataSink &sink, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportASM(ImportReader &fp, DataSink &sink, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportCPP(ImportReader &fp, DataSink &sink, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportIntelHex(ImportReader &fp, DataSink &sink, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportMotorola(ImportReader &fp, DataSink &sink, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportBase64(ImportReader &fp, DataSink &sink, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportUUEncode(ImportReader &fp, DataSink &sink, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
