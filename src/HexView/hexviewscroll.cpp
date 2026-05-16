//
//  hexviewscroll.cpp — Scroll logic (Qt port of HexViewScroll.cpp)
//
//  www.catch22.net
//  Copyright (C) 2012 James Brown
//  Qt6 port: see LICENCE.TXT
//

#include "hexview.h"
#include <QScrollBar>
#include <QSignalBlocker>
#include <algorithm>

// ── numFileLines ──────────────────────────────────────────────────────────────

size_w HexView::numFileLines(size_w length) const
{
    if (length == 0 || m_nBytesPerLine == 0) return 0;
    size_w olen = length + (size_w)m_nDataShift;
    size_w lines = olen / (size_w)m_nBytesPerLine;
    if (olen % (size_w)m_nBytesPerLine) lines++;
    return lines;
}

// ── setupScrollbars ───────────────────────────────────────────────────────────
//
//  Clamp scroll positions to valid range and update the pinned offset.
//  Does NOT call updateMetrics() — caller must ensure metrics are current.

void HexView::setupScrollbars()
{
    //if (m_nVScrollPos > m_nVScrollMax) m_nVScrollPos = m_nVScrollMax;
    ///if (m_nHScrollPos > m_nHScrollMax) m_nHScrollPos = 0;
    //u//pdatePinnedOffset();

    size_w totalLines = numFileLines(m_pDataSeq->size());
    m_nVScrollMax = totalLines > (size_w)m_nWindowLines
                    ? totalLines - (size_w)m_nWindowLines : 0;

    // setRange() must fire rangeChanged so QAbstractScrollArea's internal
    // _q_adjustScrollbars() slot runs and updates scrollbar visibility.
    // Only setValue() needs blocking — it would otherwise trigger our
    // valueChanged lambda and cause a redundant repaint.
    verticalScrollBar()->setRange(0, (int)m_nVScrollMax);
    verticalScrollBar()->setPageStep(m_nWindowLines);
    verticalScrollBar()->setSingleStep(1);
    {
        QSignalBlocker b(verticalScrollBar());
        verticalScrollBar()->setValue((int)m_nVScrollPos);
    }

    m_nTotalWidth = calcTotalWidth();

    horizontalScrollBar()->setRange(0, m_nTotalWidth - m_nWindowColumns - 1);
    horizontalScrollBar()->setPageStep(m_nWindowColumns);
    horizontalScrollBar()->setSingleStep(1);
    {
        QSignalBlocker b(horizontalScrollBar());
        horizontalScrollBar()->setValue(m_nHScrollPos);
    }
    m_nHScrollMax = m_nTotalWidth - m_nWindowColumns;

    updateResizeBarPos();
    updatePinnedOffset();

}

void HexView::updatePinnedOffset()
{
    if (m_nBytesPerLine == 0) return;
    m_nVScrollPinned  = m_nVScrollPos * (size_w)m_nBytesPerLine;
    m_nVScrollPinned -= std::min((size_w)m_nDataShift, m_nVScrollPinned);
}

// ── pinToOffset / pinToBottomCorner ───────────────────────────────────────────

void HexView::pinToOffset(size_w offset)
{
    if (m_nBytesPerLine == 0) return;
    m_nDataShift  = m_nBytesPerLine - (int)(offset % (size_w)m_nBytesPerLine);
    m_nDataShift %= m_nBytesPerLine;
    m_nVScrollPos  = (offset + (size_w)m_nDataShift) / (size_w)m_nBytesPerLine;
}

bool HexView::pinToBottomCorner()
{
    bool repos = false;

    if (m_nTotalWidth > 0 && m_nHScrollPos + m_nWindowColumns > m_nTotalWidth) {
        m_nHScrollPos = std::max(0, m_nTotalWidth - m_nWindowColumns);
        repos = true;
    }

    size_w totalLines = numFileLines(m_pDataSeq ? m_pDataSeq->size() : 0);
    if (totalLines > 0 && m_nVScrollPos + (size_w)m_nWindowLines > totalLines) {
        m_nVScrollPos = totalLines > (size_w)m_nWindowLines
                        ? totalLines - (size_w)m_nWindowLines : 0;
        repos = true;
    }

    return repos;
}

// ── recalcPositions ───────────────────────────────────────────────────────────

void HexView::recalcPositions()
{
    /*if (m_nBytesPerLine > 0)
        m_nDataShift %= m_nBytesPerLine;

    updateMetrics();
    setupScrollbars();

    if (m_nVScrollPos > 0)
        pinToOffset(m_nVScrollPinned);*/

    //RECT rect;
    //GetClientRect(m_hWnd, &rect);

    emit lengthChanged(m_pDataSeq->size());
    //OnLengthChange(m_pDataSeq->size());

    m_nDataShift %= m_nBytesPerLine;
    setGrouping(m_nBytesPerColumn);

    const QRect &rect = viewport()->rect();

    m_nWindowColumns = std::min(rect.width() / m_nFontWidth, m_nTotalWidth);

    updateResizeBarPos();

    if(m_nVScrollPos > 0)
        pinToOffset(m_nVScrollPinned);
}

// ── scroll ────────────────────────────────────────────────────────────────────

void HexView::scroll(int dx, int dy)
{
    if (dy < 0)
        dy = -(int)std::min((size_w)(-dy), m_nVScrollPos);
    else if (dy > 0)
        dy = (int)std::min((size_w)dy, m_nVScrollMax - m_nVScrollPos);

    if (dx < 0)
        dx = -std::min(-dx, m_nHScrollPos);
    else if (dx > 0)
        dx = std::min(dx, m_nHScrollMax - m_nHScrollPos);

    m_nHScrollPos += dx;
    m_nVScrollPos += dy;

    if (dx != 0 || dy != 0) {
        setupScrollbars();
        viewport()->update();
    }
}

// ── scrollTo / scrollTop ──────────────────────────────────────────────────────

bool HexView::scrollTo(size_w offset)
{
    if (!m_pDataSeq || offset > m_pDataSeq->size()) return false;
    if (m_nBytesPerLine == 0) return false;

    bool fRedraw = false;

    if (offset / (size_w)m_nBytesPerLine < m_nVScrollPos) {
        m_nVScrollPos = offset / (size_w)m_nBytesPerLine;
        fRedraw = true;
    } else if (offset / (size_w)m_nBytesPerLine > m_nVScrollPos + (size_w)m_nWindowLines) {
        m_nVScrollPos = offset / (size_w)m_nBytesPerLine - (size_w)m_nWindowLines;
        fRedraw = true;
    }

    if (fRedraw) {
        setupScrollbars();
        viewport()->update();
    }

    return true;
}

bool HexView::scrollTop(size_w offset)
{
    if (!m_pDataSeq || offset > m_pDataSeq->size()) return false;

    pinToOffset(offset);
    m_nVScrollPinned = offset;
    setupScrollbars();
    viewport()->update();
    return true;
}

bool HexView::scrollCenter(size_w offset)
{
    if (!m_pDataSeq || offset > m_pDataSeq->size()) return false;
    if (m_nBytesPerLine == 0) return false;

    const size_w line = offset / (size_w)m_nBytesPerLine;
    const size_w half = (size_w)(m_nWindowLines / 2);
    m_nVScrollPos = (line > half) ? line - half : 0;
    setupScrollbars();
    repositionCaret();
    viewport()->update();
    return true;
}
