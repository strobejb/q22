//
//  dlgexport.h
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#pragma once

#include <QString>
#include <QIODevice>
#include "HexView/seqbase.h"

class HexView;

//
//  Define some import/export formats
//
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
    IMPEXP_FORMAT format    = FORMAT_HEXDUMP;
    SEARCHTYPE    basetype  = SEARCHTYPE_BYTE;
    bool          fBigEndian  = false;
    bool          fAppend     = false;
    bool          fUseAddress = true;
    size_t        linelen     = 16;
};

//
//  ExportWriter - abstract output stream wrapping a QIODevice.
//  Back it with QFile for Export-to-file, or QBuffer for CopyAs.
//  All Export* routines write through this interface so no code is
//  duplicated between the file and clipboard paths.
//
class ExportWriter
{
public:
    explicit ExportWriter(QIODevice *dev);

    void write(const uint8_t *buf, size_t len);
    void printf(const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 2, 3)))
#endif
    ;

    bool hasError() const { return m_error; }

private:
    QIODevice *m_dev;
    bool       m_error = false;
};

// ── Individual format writers ────────────────────────────────────────────────
bool ExportRaw      (ExportWriter &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportText     (ExportWriter &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportRawHex   (ExportWriter &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportHtml     (ExportWriter &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportASM      (ExportWriter &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportCPP      (ExportWriter &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportIntelHex (ExportWriter &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportMotorola (ExportWriter &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportBase64   (ExportWriter &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);
bool ExportUUEncode (ExportWriter &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *eopt);

// ── Top-level entry points ───────────────────────────────────────────────────

// Export data from hv to a file.
bool Export  (const QString &szFileName, HexView *hv, IMPEXP_OPTIONS *eopt);

// Export data from hv into the clipboard using eopt's format.
bool CopyAs  (HexView *hv, IMPEXP_OPTIONS *eopt);

extern IMPEXP_OPTIONS g_ExportOptions;
