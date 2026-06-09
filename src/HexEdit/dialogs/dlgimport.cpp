//
//  dlgimport.cpp
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#include "dlgimport.h"
#include "HexView/hexview.h"
#include "dlgprogress.h"
#include "theme.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QFile>
#include <QMimeData>

// ── Globals ───────────────────────────────────────────────────────────────────

IMPEXP_OPTIONS g_ImportOptions = {FORMAT_HEXDUMP, SEARCHTYPE_BYTE};
IMPEXP_OPTIONS g_PasteOptions  = {FORMAT_RAWDATA, SEARCHTYPE_BYTE};

// ── HexViewSink ───────────────────────────────────────────────────────────────
// Adapts HexView to the DataSink interface used by the Import* functions.

namespace
{

struct HexViewSink : DataSink
{
    HexView *hv;

    explicit HexViewSink(HexView *hv) : hv(hv)
    {
    }

    void write(const uint8_t *buf, size_t len) override
    {
        hv->writeAtCursor(buf, len);
    }

    void padToAddress(size_w addr) override
    {
        hv->padToAddress(addr);
    }
};

struct ImportGroup
{
    HexView *hv;

    explicit ImportGroup(HexView *hv) : hv(hv)
    {
        hv->beginGroup();
    }

    ~ImportGroup()
    {
        hv->endGroup();
    }
};

} // namespace

// ── Top-level dispatcher ──────────────────────────────────────────────────────

size_w Import(QIODevice *dev, HexView *hv, IMPEXP_OPTIONS *ieopt, ProgressReporter *reporter)
{
    size_w offset  = hv->selectionStart();
    ieopt->linelen = hv->getLineLen();

    ImportReader reader(dev, reporter);
    HexViewSink  sink(hv);
    ImportGroup  group(hv);

    hv->setCurPos(offset);
    hv->setUpdatesEnabled(false);

    size_w count = 0;

    switch (ieopt->format)
    {
    case FORMAT_RAWDATA:
    {
        uint8_t buf[4096];
        size_t  len;
        while ((len = reader.read(buf, sizeof(buf))) > 0)
        {
            sink.write(buf, len);
            count += (size_w)len;
        }
    }
    break;

    case FORMAT_HEXDUMP:
        count = ImportText(reader, sink, offset, 0, ieopt);
        break;
    case FORMAT_RAWHEX:
        count = ImportRawHex(reader, sink, offset, 0, ieopt);
        break;
    case FORMAT_HTML:
        count = ImportHtml(reader, sink, offset, 0, ieopt);
        break;
    case FORMAT_CPP:
        count = ImportCPP(reader, sink, offset, 0, ieopt);
        break;
    case FORMAT_ASM:
        count = ImportASM(reader, sink, offset, 0, ieopt);
        break;
    case FORMAT_INTELHEX:
        count = ImportIntelHex(reader, sink, offset, 0, ieopt);
        break;
    case FORMAT_SRECORD:
        count = ImportMotorola(reader, sink, offset, 0, ieopt);
        break;
    case FORMAT_BASE64:
        count = ImportBase64(reader, sink, offset, 0, ieopt);
        break;
    case FORMAT_UUENCODE:
        count = ImportUUEncode(reader, sink, offset, 0, ieopt);
        break;
    default:
        break;
    }

    hv->setSelStart(offset);
    hv->setSelEnd(offset + count);
    hv->setUpdatesEnabled(true);

    return count;
}

size_w ImportFile(const QString &path, HexView *hv, IMPEXP_OPTIONS *ieopt, QWidget *parent)
{
    QFile               file(path);
    QIODevice::OpenMode mode =
        (ieopt->format == FORMAT_RAWDATA) ? QIODevice::ReadOnly : QIODevice::ReadOnly | QIODevice::Text;

    if (!file.open(mode))
        return 0;

    ProgressDialog dlg(file.size(), QObject::tr("Importing…"), parent);
    prepareDialogForShow(&dlg);
    dlg.open();
    QApplication::processEvents();

    SyncProgressReporter reporter(&dlg);
    size_w               result = Import(&file, hv, ieopt, &reporter);

    dlg.close();
    return result;
}

// ── PasteSpecial ──────────────────────────────────────────────────────────────

bool PasteSpecial(HexView *hv, bool fMaskSel, const QString &mimeType, IMPEXP_OPTIONS *ieopt)
{
    const QMimeData *md = QApplication::clipboard()->mimeData();
    if (!md || !md->hasFormat(mimeType))
        return false;

    QByteArray raw = md->data(mimeType);

    if (fMaskSel)
    {
        size_w selLen = hv->selectionSize();
        if (selLen > 0 && (size_w)raw.size() > selLen)
            raw.truncate((qsizetype)selLen);
    }

    if (ieopt->format == FORMAT_RAWDATA)
    {
        hv->writeAtCursor(reinterpret_cast<const uint8_t *>(raw.constData()), (size_t)raw.size());
        return true;
    }

    QBuffer buf(&raw);
    buf.open(QIODevice::ReadOnly);
    return Import(&buf, hv, ieopt) > 0;
}
