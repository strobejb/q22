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
#include <climits>
#include <cstring>

// Custom MIME type for in-process snapshot transfer.
// The payload is a raw HexSnapshot pointer — valid within this process only.
static const char kMimeSnapshot[] = "application/x-hexview-snapshot";
static const char kMimeOctet[]    = "application/octet-stream";

// ── onCopy ────────────────────────────────────────────────────────────────────

bool HexView::onCopy()
{
    if (!m_pDataSeq) return false;
    if (selectionSize() == 0) return false;

    const size_w start = selectionStart();
    const size_w len   = selectionSize();

    // Build snapshot: query descriptor count, allocate, fill.
    // takesnapshot() is called twice — first with nullptr to get the count,
    // then again to populate the allocated array (mirrors Win32 CreateSnapshot).
    size_t desclen = 0;
    m_pDataSeq->takesnapshot(start, len, nullptr, &desclen);

    auto *hss = new HexSnapshot();
    hss->m_desclist = new sequence::span_desc[desclen + 1];
    hss->m_count    = desclen;
    hss->m_length   = len;
    hss->m_source   = this;
    m_pDataSeq->takesnapshot(start, len, hss->m_desclist, &desclen);

    // Keep snapshot alive for the lifetime of our clipboard content.
    // Deleting m_lastSnapshot here invalidates any snapshot pointer left on the
    // clipboard from a previous copy — paste will fall through to the byte fallback.
    delete m_lastSnapshot;
    m_lastSnapshot = hss;

    QMimeData *mime = new QMimeData();

    // Store the raw snapshot pointer for in-process paste (same HexView only).
    {
        QByteArray ptrBytes(static_cast<int>(sizeof(HexSnapshot *)), Qt::Uninitialized);
        std::memcpy(ptrBytes.data(), &hss, sizeof(hss));
        mime->setData(QLatin1String(kMimeSnapshot), ptrBytes);
    }

    // Cross-process fallback: materialise the bytes.
    // Skipped if the selection exceeds QByteArray's int-size limit (~2 GB).
    if (len <= static_cast<size_w>(INT_MAX)) {
        QByteArray bytes(static_cast<int>(len), Qt::Uninitialized);
        m_pDataSeq->render(start, reinterpret_cast<uint8_t *>(bytes.data()), len);
        mime->setData(QLatin1String(kMimeOctet), bytes);
        // Text fallback so the bytes can be pasted into plain-text applications.
        mime->setText(QString::fromLatin1(bytes));
    }

    QApplication::clipboard()->setMimeData(mime);
    return true;
}

// ── onCut ─────────────────────────────────────────────────────────────────────

bool HexView::onCut()
{
    // Cut is only meaningful in insert mode.
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

    // ── Snapshot path (in-process, zero-copy) ────────────────────────────────
    // Only valid when the clipboard content came from this exact HexView widget.
    // Mirrors the Win32 CFSTR_HEX_SNAPSHOTPTR / CFSTR_HEX_HWND check in DropData().
    if (mime->hasFormat(QLatin1String(kMimeSnapshot))) {
        QByteArray ptrBytes = mime->data(QLatin1String(kMimeSnapshot));
        if (ptrBytes.size() == static_cast<int>(sizeof(HexSnapshot *))) {
            HexSnapshot *hss = nullptr;
            std::memcpy(&hss, ptrBytes.constData(), sizeof(hss));

            if (hss && hss->m_source == this) {
                // Inline the enterData logic for snapshot insertion:
                // we cannot call enterData(nullptr,...) because it guards on pSource.
                const bool fReplaceSelection =
                    (m_nEditMode == HVMODE_INSERT && selectionSize() > 0);
                const size_w offset = m_nCursorOffset;

                if (fReplaceSelection) {
                    m_pDataSeq->group();
                    m_pDataSeq->erase(selectionStart(), selectionSize());
                }

                if (selectionSize() > 0 &&
                    (m_nCursorOffset == m_nSelectionStart ||
                     m_nCursorOffset == m_nSelectionEnd))
                    m_nCursorOffset = selectionStart();

                if (m_nEditMode == HVMODE_OVERWRITE)
                    m_pDataSeq->replace_snapshot(m_nCursorOffset, hss->m_length,
                                                 hss->m_desclist, hss->m_count);
                else
                    m_pDataSeq->insert_snapshot(m_nCursorOffset, hss->m_length,
                                                hss->m_desclist, hss->m_count);

                const size_w nLength = hss->m_length;
                m_nCursorOffset  += nLength;
                m_nSelectionStart = m_nCursorOffset;
                m_nSelectionEnd   = m_nCursorOffset;

                if (fReplaceSelection)
                    m_pDataSeq->ungroup();

                if (m_fRedrawChanges)
                    fireChanged(offset, nLength,
                                m_nEditMode == HVMODE_INSERT
                                    ? HVMETHOD_INSERT : HVMETHOD_OVERWRITE);

                m_pDataSeq->breakopt();
                return true;
            }
        }
    }

    // ── Byte fallback (cross-process or different-source paste) ───────────────
    QByteArray bytes;
    if (mime->hasFormat(QLatin1String(kMimeOctet))) {
        bytes = mime->data(QLatin1String(kMimeOctet));
    } else if (mime->hasText()) {
        bytes = mime->text().toLatin1();
    } else {
        return false;
    }

    if (bytes.isEmpty()) return false;

    enterData(reinterpret_cast<uint8_t *>(bytes.data()),
              static_cast<size_w>(bytes.size()),
              /*fAdvanceCaret=*/true, /*fReplaceSelection=*/true, /*fSelectData=*/false);

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
