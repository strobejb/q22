//
//  dlgimport.h
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#pragma once

#include "dlgexport.h"
#include <QIODevice>
#include <QString>

class HexView;

//
//  ImportReader - abstract input stream wrapping a QIODevice.
//  Back it with QFile for file-based import, QBuffer for clipboard import.
//  Mirrors ExportWriter, providing fread / fgets style access.
//
class ImportReader
{
public:
    explicit ImportReader(QIODevice *dev);

    // Read up to len bytes; returns bytes actually read
    size_t read(uint8_t *buf, size_t len);

    // Read one text line (up to and including '\n') into buf[0..len-1],
    // NUL-terminates, returns true on success, false at EOF
    bool gets(char *buf, size_t len);

    bool atEnd() const;

private:
    QIODevice *m_dev;
};

// ── Individual format parsers ────────────────────────────────────────────────
size_w ImportText     (ImportReader &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportRawHex   (ImportReader &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportHtml     (ImportReader &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportASM      (ImportReader &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportCPP      (ImportReader &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportIntelHex (ImportReader &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportMotorola (ImportReader &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportBase64   (ImportReader &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);
size_w ImportUUEncode (ImportReader &fp, HexView *hv, size_w offset, size_w length, IMPEXP_OPTIONS *ieopt);

// ── Top-level entry points ───────────────────────────────────────────────────

// Parse a formatted file into hv; returns byte count written (0 on failure)
size_w Import    (QIODevice *dev, HexView *hv, IMPEXP_OPTIONS *ieopt);
size_w ImportFile(const QString &path, HexView *hv, IMPEXP_OPTIONS *ieopt);

// Retrieve clipboard data in mimeType, optionally interpret via ieopt,
// and paste into hv at the current cursor.
// fMaskSel: limit write to the current selection size (mask paste).
bool PasteSpecial(HexView *hv, bool fMaskSel,
                  const QString &mimeType, IMPEXP_OPTIONS *ieopt);

extern IMPEXP_OPTIONS g_ImportOptions;
extern IMPEXP_OPTIONS g_PasteOptions;
