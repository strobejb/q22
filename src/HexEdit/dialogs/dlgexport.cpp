//
//  dlgexport.cpp
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#include "dlgexport.h"
#include "exportformat.h"
#include "dlgprogress.h"
#include "theme.h"
#include "HexView/hexview.h"

#include <QBuffer>
#include <QClipboard>
#include <QApplication>
#include <QFile>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ── Globals ──────────────────────────────────────────────────────────────────

IMPEXP_OPTIONS g_ExportOptions = { FORMAT_HEXDUMP, SEARCHTYPE_BYTE };

// ── Endian helpers ───────────────────────────────────────────────────────────

static inline uint16_t reverse16(uint16_t n) { return (uint16_t)((n >> 8) | (n << 8)); }
static inline uint32_t reverse32(uint32_t n) {
#if defined(_MSC_VER)
    return _byteswap_ulong(n);
#else
    return __builtin_bswap32(n);
#endif
}
static inline uint64_t reverse64(uint64_t n) {
#if defined(_MSC_VER)
    return _byteswap_uint64(n);
#else
    return __builtin_bswap64(n);
#endif
}

#define ENDIAN_TO_NATIVE16(bigend, v) ((bigend) ? reverse16((uint16_t)(v)) : (uint16_t)(v))
#define ENDIAN_TO_NATIVE32(bigend, v) ((bigend) ? reverse32((uint32_t)(v)) : (uint32_t)(v))
#define ENDIAN_TO_NATIVE64(bigend, v) ((bigend) ? reverse64((uint64_t)(v)) : (uint64_t)(v))

#define MAX_SRECORD_LEN 16

// ── ExportWriter ─────────────────────────────────────────────────────────────

ExportWriter::ExportWriter(QIODevice *dev, ProgressReporter *reporter)
    : m_dev(dev), m_reporter(reporter)
{
}

void ExportWriter::write(const uint8_t *buf, size_t len)
{
    if (m_reporter && m_reporter->isCancelled()) { m_error = true; return; }
    if (!m_error && m_dev->write(reinterpret_cast<const char *>(buf), (qint64)len) < 0)
        m_error = true;
}

void ExportWriter::printf(const char *fmt, ...)
{
    if (m_reporter && m_reporter->isCancelled()) { m_error = true; return; }

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);

    if (n <= 0) return;

    QByteArray buf(n + 1, '\0');
    va_start(ap, fmt);
    vsnprintf(buf.data(), n + 1, fmt, ap);
    va_end(ap);

    if (!m_error && m_dev->write(buf.constData(), n) < 0)
        m_error = true;
}

void ExportWriter::advanceProgress(size_w inputConsumed)
{
    if (!m_reporter) return;
    m_inputConsumed += (qint64)inputConsumed;
    if (m_inputConsumed - m_lastReported >= kReportInterval) {
        m_lastReported = m_inputConsumed;
        m_reporter->reportProgress(m_inputConsumed);
    }
}

// ── Format writers ────────────────────────────────────────────────────────────

bool ExportRaw(ExportWriter &fp, HexView *hv, size_w offset, size_w length,
               IMPEXP_OPTIONS * /*eopt*/)
{
    while (length && !fp.hasError())
    {
        uint8_t buf[256];
        size_t  len = (size_t)std::min((size_w)256, length);

        hv->getData(offset, buf, len);
        fp.write(buf, len);
        fp.advanceProgress((size_w)len);

        offset += len;
        length -= len;
    }

    return !fp.hasError();
}

bool ExportText(ExportWriter &fp, HexView *hv, size_w offset, size_w length,
                IMPEXP_OPTIONS *eopt)
{
    const size_t lineLen = eopt->linelen ? eopt->linelen : 16;

    while (length > 0 && !fp.hasError())
    {
        uint8_t buf[256];
        size_t  len = (size_t)std::min((size_w)lineLen, length);

        hv->getData(offset, buf, len);

        // Address
        fp.printf("%08llX  ", (unsigned long long)offset);

        // Hex bytes
        for (size_t i = 0; i < len; i++)
        {
            fp.printf("%02X ", buf[i]);
            if (i == lineLen / 2 - 1)
                fp.printf(" ");
        }

        // Pad incomplete last line
        for (size_t i = len; i < lineLen; i++)
        {
            fp.printf("   ");
            if (i == lineLen / 2 - 1)
                fp.printf(" ");
        }

        // ASCII column
        fp.printf(" |");
        for (size_t i = 0; i < len; i++)
            fp.printf("%c", toAscii(buf[i]));
        fp.printf("|\n");

        fp.advanceProgress((size_w)len);
        offset += len;
        length -= len;
    }

    return !fp.hasError();
}

bool ExportRawHex(ExportWriter &fp, HexView *hv, size_w offset, size_w length,
                  IMPEXP_OPTIONS *eopt)
{
    while (length && !fp.hasError())
    {
        uint8_t buf[256];
        size_w  len = std::min((size_w)eopt->linelen, length);

        hv->getData(offset, buf, (size_t)len);

        for (size_w i = 0; i < len; i++)
            fp.printf("%02X", buf[i]);

        fp.advanceProgress(len);
        offset += len;
        length -= len;
    }

    return !fp.hasError();
}

bool ExportHtml(ExportWriter &fp, HexView *hv, size_w offset, size_w length,
                IMPEXP_OPTIONS *eopt)
{
    const size_t lineLen = eopt->linelen ? eopt->linelen : 16;
    const QString title = hv->filePath();
    const QByteArray titleBytes = title.toUtf8();

    fp.printf("<html>\n<head>\n");
    fp.printf("<title>%s</title>\n", titleBytes.constData());
    fp.printf("<style>\n");
    fp.printf("  body { font-family: Calibri,Verdana,sans-serif; }\n");
    fp.printf("  pre  { font-family: monospace; }\n");
    fp.printf("</style>\n");
    fp.printf("</head>\n<body>\n");
    fp.printf("<h1>%s</h1>\n<hr>\n<pre>\n", titleBytes.constData());

    while (length > 0 && !fp.hasError())
    {
        uint8_t buf[256];
        size_t  len = (size_t)std::min((size_w)lineLen, length);

        hv->getData(offset, buf, len);

        fp.printf("%08llX  ", (unsigned long long)offset);

        for (size_t i = 0; i < len; i++)
        {
            fp.printf("%02X ", buf[i]);
            if (i == lineLen / 2 - 1)
                fp.printf(" ");
        }
        for (size_t i = len; i < lineLen; i++)
        {
            fp.printf("   ");
            if (i == lineLen / 2 - 1)
                fp.printf(" ");
        }

        fp.printf(" |");
        for (size_t i = 0; i < len; i++)
            fp.printf("%c", toAscii(buf[i]));
        fp.printf("|\n");

        fp.advanceProgress((size_w)len);
        offset += len;
        length -= len;
    }

    fp.printf("</pre>\n<hr>\n");
    fp.printf("<font size=\"-3\">Generated by <a href=\"http://www.catch22.net/\">Catch22 HexEdit</a></font>\n");
    fp.printf("</body>\n</html>\n");

    return !fp.hasError();
}

bool ExportASM(ExportWriter &fp, HexView *hv, size_w offset, size_w length,
               IMPEXP_OPTIONS *eopt)
{
    const QString path = hv->filePath();
    fp.printf("; Generated by HexEdit\n");
    fp.printf("; %s\n", path.toUtf8().constData());

    while (length && !fp.hasError())
    {
        uint8_t buf[256];
        size_w  len = std::min((size_w)eopt->linelen, length);

        hv->getData(offset, buf, (size_t)len);

        switch (eopt->basetype)
        {
        case SEARCHTYPE_BYTE:
            fp.printf("db ");
            for (size_w i = 0; i < len; i++)
                fp.printf("0%02Xh ", buf[i]);
            break;

        case SEARCHTYPE_WORD:
            fp.printf("dw ");
            for (size_w i = 0; i < len / 2; i++)
            {
                uint16_t v; memcpy(&v, buf + i * 2, 2);
                fp.printf("0%04Xh ", ENDIAN_TO_NATIVE16(eopt->fBigEndian, v));
            }
            break;

        case SEARCHTYPE_DWORD:
            fp.printf("dd ");
            for (size_w i = 0; i < len / 4; i++)
            {
                uint32_t v; memcpy(&v, buf + i * 4, 4);
                fp.printf("0%08Xh ", ENDIAN_TO_NATIVE32(eopt->fBigEndian, v));
            }
            break;

        case SEARCHTYPE_QWORD:
            fp.printf("dq ");
            for (size_w i = 0; i < len / 8; i++)
            {
                uint64_t v; memcpy(&v, buf + i * 8, 8);
                fp.printf("0%016llXh ", (unsigned long long)ENDIAN_TO_NATIVE64(eopt->fBigEndian, v));
            }
            break;

        default:
            break;
        }

        fp.printf("\n");
        fp.advanceProgress(len);
        length -= len;
        offset += len;
    }

    return !fp.hasError();
}

bool ExportCPP(ExportWriter &fp, HexView *hv, size_w offset, size_w length,
               IMPEXP_OPTIONS *eopt)
{
    static const char *tlook[] =
    {
        nullptr, nullptr, nullptr, nullptr, nullptr,
        "uint8_t", "uint16_t", "uint32_t", "uint64_t", "float", "double"
    };

    const char *typeName = tlook[eopt->basetype];
    if (!typeName) typeName = "uint8_t";

    const QString path = hv->filePath();
    fp.printf("/* Generated by HexEdit */\n");
    fp.printf("/* %s */\n", path.toUtf8().constData());
    fp.printf("%s hexData[0x%llx] =\n{\n", typeName, (unsigned long long)length);

    while (length && !fp.hasError())
    {
        uint8_t buf[256];
        size_w  len = std::min((size_w)eopt->linelen, length);

        hv->getData(offset, buf, (size_t)len);

        fp.printf("  ");

        switch (eopt->basetype)
        {
        case SEARCHTYPE_BYTE:
            for (size_w i = 0; i < len; i++)
                fp.printf("0x%02X, ", buf[i]);
            break;

        case SEARCHTYPE_WORD:
            for (size_w i = 0; i < len / 2; i++)
            {
                uint16_t v; memcpy(&v, buf + i * 2, 2);
                fp.printf("0x%04X, ", ENDIAN_TO_NATIVE16(eopt->fBigEndian, v));
            }
            break;

        case SEARCHTYPE_DWORD:
            for (size_w i = 0; i < len / 4; i++)
            {
                uint32_t v; memcpy(&v, buf + i * 4, 4);
                fp.printf("0x%08X, ", ENDIAN_TO_NATIVE32(eopt->fBigEndian, v));
            }
            break;

        case SEARCHTYPE_QWORD:
            for (size_w i = 0; i < len / 8; i++)
            {
                uint64_t v; memcpy(&v, buf + i * 8, 8);
                fp.printf("0x%016llX, ", (unsigned long long)ENDIAN_TO_NATIVE64(eopt->fBigEndian, v));
            }
            break;

        case SEARCHTYPE_FLOAT:
            for (size_w i = 0; i < len / 4; i++)
            {
                float f; memcpy(&f, buf + i * 4, 4);
                fp.printf("%g, ", (double)f);
            }
            break;

        case SEARCHTYPE_DOUBLE:
            for (size_w i = 0; i < len / 8; i++)
            {
                double d; memcpy(&d, buf + i * 8, 8);
                fp.printf("%g, ", d);
            }
            break;

        default:
            break;
        }

        fp.printf("\n");
        fp.advanceProgress(len);
        offset += len;
        length -= len;
    }

    fp.printf("};\n");
    return !fp.hasError();
}

bool ExportIntelHex(ExportWriter &fp, HexView *hv, size_w offset, size_w length,
                    IMPEXP_OPTIONS *eopt)
{
    if (offset > 0xffffffff) return false;

    char   ach[300];
    size_t alen;
    size_t count = 0;

    if (!eopt->fUseAddress)
        offset = 0;

    // Extended linear address record (type=4): upper 16 bits of address
    uint16_t highaddr = reverse16((uint16_t)((offset >> 16) & 0xffff));
    alen = intel_frame(ach, 4, sizeof(uint16_t), 0,
                       reinterpret_cast<const uint8_t *>(&highaddr));
    fp.printf("%.*s\n", (int)alen, ach);

    while (length > 0 && !fp.hasError())
    {
        uint8_t buf[MAX_SRECORD_LEN];
        size_t  len = (size_t)std::min((size_w)MAX_SRECORD_LEN, length);

        if (count > 0xffff)
        {
            // New extended linear address record every 64 K bytes
            highaddr = reverse16((uint16_t)((offset >> 16) & 0xffff));
            alen = intel_frame(ach, 4, sizeof(uint16_t), 0,
                               reinterpret_cast<const uint8_t *>(&highaddr));
            fp.printf("%.*s\n", (int)alen, ach);
            count = 0;
        }

        hv->getData(offset, buf, len);

        // Intel data record (type=0)
        alen = intel_frame(ach, 0, len, (unsigned long)(offset & 0xffff), buf);
        fp.printf("%.*s\n", (int)alen, ach);

        fp.advanceProgress((size_w)len);
        length -= len;
        offset += len;
        count  += len;
    }

    // End-of-file record (type=1)
    alen = intel_frame(ach, 1, 0, 0, nullptr);
    fp.printf("%.*s\n", (int)alen, ach);

    return !fp.hasError();
}

bool ExportMotorola(ExportWriter &fp, HexView *hv, size_w offset, size_w length,
                    IMPEXP_OPTIONS * /*eopt*/)
{
    if (offset > 0xffffffff) return false;

    char   ach[300];
    size_t alen;

    // Frame header (type=0)
    alen = motorola_frame(ach, 0, 3, 0,
                          reinterpret_cast<const uint8_t *>("HDR"));
    fp.printf("%.*s\n", (int)alen, ach);

    while (length > 0 && !fp.hasError())
    {
        uint8_t buf[MAX_SRECORD_LEN];
        size_t  len = (size_t)std::min((size_w)MAX_SRECORD_LEN, length);

        hv->getData(offset, buf, len);

        // Data record (type=3, 32-bit address)
        alen = motorola_frame(ach, 3, len, (unsigned long)offset, buf);
        fp.printf("%.*s\n", (int)alen, ach);

        fp.advanceProgress((size_w)len);
        length -= len;
        offset += len;
    }

    // Termination record (type=7, matches type-3 data records)
    alen = motorola_frame(ach, 7, 0, 0, nullptr);
    fp.printf("%.*s\n", (int)alen, ach);

    return !fp.hasError();
}

bool ExportBase64(ExportWriter &fp, HexView *hv, size_w offset, size_w length,
                  IMPEXP_OPTIONS * /*eopt*/)
{
    while (length > 0 && !fp.hasError())
    {
        uint8_t buf[256];
        char    ach[256];
        size_t  len  = (size_t)std::min((size_w)54, length);
        size_t  alen;

        hv->getData(offset, buf, len);

        alen = base64_encode(buf, len, ach);
        fp.printf("%.*s\n", (int)alen, ach);

        fp.advanceProgress((size_w)len);
        length -= len;
        offset += len;
    }

    return !fp.hasError();
}

bool ExportUUEncode(ExportWriter &fp, HexView *hv, size_w offset, size_w length,
                    IMPEXP_OPTIONS * /*eopt*/)
{
    const QString path     = hv->filePath();
    const QString fileName = path.section('/', -1);

    fp.printf("begin 666 %s\n", fileName.toUtf8().constData());

    while (length > 0 && !fp.hasError())
    {
        uint8_t buf[256];
        char    ach[256];
        size_t  len  = (size_t)std::min((size_w)45, length);
        size_t  alen;

        hv->getData(offset, buf, len);

        alen = uu_encode(buf, len, ach);
        fp.printf("%.*s\n", (int)alen, ach);

        fp.advanceProgress((size_w)len);
        length -= len;
        offset += len;
    }

    fp.printf("end\n");
    return !fp.hasError();
}

// ── Top-level Export ──────────────────────────────────────────────────────────

bool Export(const QString &szFileName, HexView *hv, IMPEXP_OPTIONS *eopt, QWidget *parent)
{
    size_w offset = hv->selectionStart();
    size_w length = hv->selectionSize();

    if (length == 0)
    {
        offset = 0;
        length = hv->size();
    }

    eopt->linelen = hv->getLineLen();

    QIODevice::OpenMode mode = (eopt->format == FORMAT_RAWDATA)
        ? QIODevice::WriteOnly
        : QIODevice::WriteOnly | QIODevice::Text;

    if (eopt->fAppend)
        mode |= QIODevice::Append;

    QFile file(szFileName);
    if (!file.open(mode))
        return false;

    hv->setCurPos(offset);
    hv->setUpdatesEnabled(false);

    // Show the progress dialog as window-modal so the parent is blocked but
    // the main thread remains free to pump events via processEvents().
    ProgressDialog dlg(static_cast<qint64>(length), QObject::tr("Exporting…"), parent);
    prepareDialogForShow(&dlg);
    dlg.open();
    QApplication::processEvents(); // paint the dialog before the loop starts

    SyncProgressReporter reporter(&dlg);
    ExportWriter writer(&file, &reporter);

    bool success = false;

    switch (eopt->format)
    {
    case FORMAT_RAWDATA:   success = ExportRaw      (writer, hv, offset, length, eopt); break;
    case FORMAT_HEXDUMP:   success = ExportText      (writer, hv, offset, length, eopt); break;
    case FORMAT_RAWHEX:    success = ExportRawHex    (writer, hv, offset, length, eopt); break;
    case FORMAT_HTML:      success = ExportHtml      (writer, hv, offset, length, eopt); break;
    case FORMAT_CPP:       success = ExportCPP       (writer, hv, offset, length, eopt); break;
    case FORMAT_ASM:       success = ExportASM       (writer, hv, offset, length, eopt); break;
    case FORMAT_INTELHEX:  success = ExportIntelHex  (writer, hv, offset, length, eopt); break;
    case FORMAT_SRECORD:   success = ExportMotorola  (writer, hv, offset, length, eopt); break;
    case FORMAT_BASE64:    success = ExportBase64    (writer, hv, offset, length, eopt); break;
    case FORMAT_UUENCODE:  success = ExportUUEncode  (writer, hv, offset, length, eopt); break;
    default: break;
    }

    dlg.close();

    hv->setSelStart(offset);
    hv->setSelEnd(offset + length);
    hv->setUpdatesEnabled(true);

    file.close();
    return success;
}

// ── CopyAs ────────────────────────────────────────────────────────────────────
//
// Runs the export engine into an in-memory QBuffer, then puts the result
// on the clipboard -- exactly the same logic as the Win32 version used a
// named pipe + HGLOBAL, but without the inter-thread plumbing.

bool CopyAs(HexView *hv, IMPEXP_OPTIONS *eopt, QWidget *parent)
{
    size_w offset = hv->selectionStart();
    size_w length = hv->selectionSize();

    if (length == 0)
    {
        offset = 0;
        length = hv->size();
    }

    eopt->linelen = hv->getLineLen();

    hv->setCurPos(offset);
    hv->setUpdatesEnabled(false);

    ProgressDialog dlg(static_cast<qint64>(length), QObject::tr("Copying…"), parent);
    prepareDialogForShow(&dlg);
    dlg.open();
    QApplication::processEvents();

    QBuffer buf;
    buf.open(QIODevice::WriteOnly);

    SyncProgressReporter reporter(&dlg);
    ExportWriter writer(&buf, &reporter);

    bool success = false;

    switch (eopt->format)
    {
    case FORMAT_RAWDATA:   success = ExportRaw      (writer, hv, offset, length, eopt); break;
    case FORMAT_HEXDUMP:   success = ExportText      (writer, hv, offset, length, eopt); break;
    case FORMAT_RAWHEX:    success = ExportRawHex    (writer, hv, offset, length, eopt); break;
    case FORMAT_HTML:      success = ExportHtml      (writer, hv, offset, length, eopt); break;
    case FORMAT_CPP:       success = ExportCPP       (writer, hv, offset, length, eopt); break;
    case FORMAT_ASM:       success = ExportASM       (writer, hv, offset, length, eopt); break;
    case FORMAT_INTELHEX:  success = ExportIntelHex  (writer, hv, offset, length, eopt); break;
    case FORMAT_SRECORD:   success = ExportMotorola  (writer, hv, offset, length, eopt); break;
    case FORMAT_BASE64:    success = ExportBase64    (writer, hv, offset, length, eopt); break;
    case FORMAT_UUENCODE:  success = ExportUUEncode  (writer, hv, offset, length, eopt); break;
    default: break;
    }

    dlg.close();

    hv->setSelStart(offset);
    hv->setSelEnd(offset + length);
    hv->setUpdatesEnabled(true);

    buf.close();

    if (success)
    {
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(QString::fromUtf8(buf.data()));
    }

    return success;
}
