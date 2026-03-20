//
//  hexviewclipboard.cpp — Clipboard operations (Qt port of HexViewClipboard.cpp)
//
//  www.catch22.net
//  Copyright (C) 2012 James Brown
//  Qt6 port: see LICENCE.TXT
//

#include "hexview.h"
#include <QApplication>
#include <QClipboard>
#include <QMimeData>

// ── onCopy ────────────────────────────────────────────────────────────────────

bool HexView::onCopy()
{
    if (!m_pDataSeq) return false;
    if (selectionSize() == 0) return false;

    size_w start = selectionStart();
    size_w len   = selectionSize();

    QByteArray bytes(static_cast<int>(len), Qt::Uninitialized);
    m_pDataSeq->render(start, reinterpret_cast<uint8_t *>(bytes.data()), len);

    QMimeData *mime = new QMimeData();
    mime->setData(QStringLiteral("application/octet-stream"), bytes);
    // Also expose as text so it can be pasted into text editors
    mime->setText(QString::fromLatin1(bytes));

    QApplication::clipboard()->setMimeData(mime);
    return true;
}

// ── onCut ─────────────────────────────────────────────────────────────────────

bool HexView::onCut()
{
    // Cut is only meaningful in insert mode
    if (m_nEditMode != HVMODE_INSERT) return false;
    if (selectionSize() == 0) return false;

    return onCopy() && onClear();
}

// ── onPaste ───────────────────────────────────────────────────────────────────

bool HexView::onPaste()
{
    if (!m_pDataSeq) return false;
    if (m_nEditMode == HVMODE_READONLY) return false;

    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime) return false;

    QByteArray bytes;

    if (mime->hasFormat(QStringLiteral("application/octet-stream"))) {
        bytes = mime->data(QStringLiteral("application/octet-stream"));
    } else if (mime->hasText()) {
        bytes = mime->text().toLatin1();
    } else {
        return false;
    }

    if (bytes.isEmpty()) return false;

    enterData(reinterpret_cast<uint8_t *>(bytes.data()),
              static_cast<size_w>(bytes.size()),
              true, true, false);

    m_pDataSeq->breakopt();
    return true;
}

// ── onClear ───────────────────────────────────────────────────────────────────

bool HexView::onClear()
{
    if (!m_pDataSeq) return false;

    switch (m_nEditMode) {
    case HVMODE_READONLY:
        return false;

    case HVMODE_INSERT:
        if (selectionSize() > 0)
            return forwardDelete();
        return false;

    case HVMODE_OVERWRITE:
        if (selectionSize() > 0) {
            uint8_t b = 0;
            fillData(&b, 1, selectionSize());
            return true;
        }
        return false;
    }

    return false;
}

// ── Public wrappers ───────────────────────────────────────────────────────────

bool HexView::copy()  { return onCopy();  }
bool HexView::cut()   { return onCut();   }
bool HexView::paste() { return onPaste(); }
bool HexView::clear() { return onClear(); }
