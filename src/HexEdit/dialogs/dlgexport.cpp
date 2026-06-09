//
//  dlgexport.cpp
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#include "dlgexport.h"
#include "HexView/hexview.h"
#include "theme.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QFile>

// ── Globals ───────────────────────────────────────────────────────────────────

IMPEXP_OPTIONS g_ExportOptions = {FORMAT_HEXDUMP, SEARCHTYPE_BYTE};

// ── HexViewSource ─────────────────────────────────────────────────────────────
// Adapts HexView to the DataSource interface used by the Export* functions.

namespace
{

struct HexViewSource : DataSource
{
    HexView *hv;

    explicit HexViewSource(HexView *hv) : hv(hv)
    {
    }

    void getData(size_w offset, uint8_t *buf, size_t len) const override
    {
        hv->getData(offset, buf, len);
    }

    QString filePath() const override
    {
        return hv->filePath();
    }
};

} // namespace

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

    QIODevice::OpenMode mode =
        (eopt->format == FORMAT_RAWDATA) ? QIODevice::WriteOnly : QIODevice::WriteOnly | QIODevice::Text;

    if (eopt->fAppend)
        mode |= QIODevice::Append;

    QFile file(szFileName);
    if (!file.open(mode))
        return false;

    hv->setCurPos(offset);
    hv->setUpdatesEnabled(false);

    ProgressDialog dlg(static_cast<qint64>(length), QObject::tr("Exporting…"), parent);
    prepareDialogForShow(&dlg);
    dlg.open();
    QApplication::processEvents();

    SyncProgressReporter reporter(&dlg);
    ExportWriter         writer(&file, &reporter);
    HexViewSource        src(hv);
    bool                 success = false;

    switch (eopt->format)
    {
    case FORMAT_RAWDATA:
        success = ExportRaw(writer, src, offset, length, eopt);
        break;
    case FORMAT_HEXDUMP:
        success = ExportText(writer, src, offset, length, eopt);
        break;
    case FORMAT_RAWHEX:
        success = ExportRawHex(writer, src, offset, length, eopt);
        break;
    case FORMAT_HTML:
        success = ExportHtml(writer, src, offset, length, eopt);
        break;
    case FORMAT_CPP:
        success = ExportCPP(writer, src, offset, length, eopt);
        break;
    case FORMAT_ASM:
        success = ExportASM(writer, src, offset, length, eopt);
        break;
    case FORMAT_INTELHEX:
        success = ExportIntelHex(writer, src, offset, length, eopt);
        break;
    case FORMAT_SRECORD:
        success = ExportMotorola(writer, src, offset, length, eopt);
        break;
    case FORMAT_BASE64:
        success = ExportBase64(writer, src, offset, length, eopt);
        break;
    case FORMAT_UUENCODE:
        success = ExportUUEncode(writer, src, offset, length, eopt);
        break;
    default:
        break;
    }

    dlg.close();

    hv->setSelStart(offset);
    hv->setSelEnd(offset + length);
    hv->setUpdatesEnabled(true);

    file.close();
    return success;
}

// ── CopyAs ────────────────────────────────────────────────────────────────────

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
    ExportWriter         writer(&buf, &reporter);
    HexViewSource        src(hv);
    bool                 success = false;

    switch (eopt->format)
    {
    case FORMAT_RAWDATA:
        success = ExportRaw(writer, src, offset, length, eopt);
        break;
    case FORMAT_HEXDUMP:
        success = ExportText(writer, src, offset, length, eopt);
        break;
    case FORMAT_RAWHEX:
        success = ExportRawHex(writer, src, offset, length, eopt);
        break;
    case FORMAT_HTML:
        success = ExportHtml(writer, src, offset, length, eopt);
        break;
    case FORMAT_CPP:
        success = ExportCPP(writer, src, offset, length, eopt);
        break;
    case FORMAT_ASM:
        success = ExportASM(writer, src, offset, length, eopt);
        break;
    case FORMAT_INTELHEX:
        success = ExportIntelHex(writer, src, offset, length, eopt);
        break;
    case FORMAT_SRECORD:
        success = ExportMotorola(writer, src, offset, length, eopt);
        break;
    case FORMAT_BASE64:
        success = ExportBase64(writer, src, offset, length, eopt);
        break;
    case FORMAT_UUENCODE:
        success = ExportUUEncode(writer, src, offset, length, eopt);
        break;
    default:
        break;
    }

    dlg.close();

    hv->setSelStart(offset);
    hv->setSelEnd(offset + length);
    hv->setUpdatesEnabled(true);

    buf.close();

    if (success)
        QApplication::clipboard()->setText(QString::fromUtf8(buf.data()));

    return success;
}
