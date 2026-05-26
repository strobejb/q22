//
//  hexviewmouse.cpp — Mouse, caret and hit-test logic (Qt port of HexViewMouse.cpp)
//
//  www.catch22.net
//  Copyright (C) 2012 James Brown
//  Qt6 port: see LICENCE.TXT
//

#include "hexview.h"

#include <QApplication>
#include <QPlainTextEdit>
#include <QContextMenuEvent>
#include <QCursor>
#include <QMenu>
#include "theme.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cctype>

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool inrange(size_w offset, size_w start, size_w finish)
{
    return (offset >= start && offset < finish) ||
           (offset >= finish && offset < start);
}

static int scrollDir(int counter, int distance)
{
    if (distance > 48)  return  5;
    if (distance > 16)  return  2;
    if (distance >  3)  return  1;
    if (distance >  0)  return  counter % 5 == 0 ? 1 : 0;
    if (distance < -48) return -5;
    if (distance < -16) return -2;
    if (distance <  -3) return -1;
    if (distance <   0) return  counter % 5 == 0 ? -1 : 0;
    return 0;
}

// ── Coordinate mapping ────────────────────────────────────────────────────────

int HexView::getLogicalX(int x, int *pane, int *subitem) const
{
    if (pane)    *pane    = checkStyle(HVS_HEX_INVISIBLE) ? 1 : 0;
    if (subitem) *subitem = 0;

    // Adjust for horizontal scroll
    x += m_nHScrollPos * m_nFontWidth;

    // Round down to mid-character
    x = (x + (m_nFontWidth * 7) / 16) / m_nFontWidth;

    // Clicked in address column?
    if (!checkStyle(HVS_ADDR_INVISIBLE)) {
        if (x < m_nAddressWidth + m_nHexPaddingLeft)
            return 0;
        x -= m_nAddressWidth;
    }

    if (!checkStyle(HVS_HEX_INVISIBLE)) {
        x -= m_nHexPaddingLeft;

        // Clicked in hex column?
        if (x < m_nHexWidth) {
            int uw     = unitWidth();
            int col    = x / (m_nBytesPerColumn * uw + 1);
            int coloff = std::min(m_nBytesPerColumn - 1,
                                  (x - col * (m_nBytesPerColumn * uw + 1)) / uw);
            col *= m_nBytesPerColumn;
            col += coloff;
            if (subitem)
                *subitem = (x % (m_nBytesPerColumn * uw + 1)) % uw;
            return col;
        }

        x -= m_nHexWidth;

        // Clicked in first half of right-padding?
        if (x < m_nHexPaddingRight / 2)
            return m_nBytesPerLine;
    }

    if (!checkStyle(HVS_ASCII_INVISIBLE)) {
        x -= m_nHexPaddingRight;
        if (pane) *pane = 1;
        if (x > m_nBytesPerLine) x = m_nBytesPerLine;
    } else {
        x = m_nBytesPerLine;
    }

    if (x < 0) x = 0;
    return x;
}

int HexView::getLogicalY(int y) const
{
    if (y < 0) y = 0;
    return m_nFontHeight > 0 ? y / m_nFontHeight : 0;
}

// ── Caret helpers ─────────────────────────────────────────────────────────────

void HexView::caretPosFromOffset(size_w offset, int *x, int *y) const
{
    if (m_nBytesPerLine == 0) { *x = *y = 0; return; }

    offset += (size_w)m_nDataShift;

    if (m_fCursorAdjustment && offset > 0) {
        *y = (int)(offset / (size_w)m_nBytesPerLine - m_nVScrollPos - 1);
        *x = m_nBytesPerLine;
        if (*y < 0 && m_nVScrollPos == 0) *y = 0;
    } else {
        *y = (int)(offset / (size_w)m_nBytesPerLine - m_nVScrollPos);
        *x = (int)(offset % (size_w)m_nBytesPerLine);
    }

    // Hide caret if well outside viewport to avoid wrap-around artefacts
    if (offset / (size_w)m_nBytesPerLine < m_nVScrollPos) {
        *y = -1;
    } else if (offset / (size_w)m_nBytesPerLine - m_nVScrollPos > (size_w)(m_nWindowLines + 1)) {
        *y = m_nWindowLines + 1;
    }
}

size_w HexView::offsetFromPhysCoord(int mx, int my, int *pane,
                                     int *lx, int *ly, int *subitem)
{
    int x = getLogicalX(mx, pane, subitem);
    int y = getLogicalY(my);
    //qDebug() << "logx" << x;

    if (y >= m_nWindowLines)   y = m_nWindowLines - 1;
    if (x >= m_nWindowColumns) x = m_nWindowColumns - 1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    size_w seqsize    = m_pDataSeq ? m_pDataSeq->size() : 0;
    size_w adjdocsize = seqsize + (size_w)m_nDataShift;

    size_w offset = (size_w)(y + (int)m_nVScrollPos) * (size_w)m_nBytesPerLine;
    offset += std::min((size_w)x, adjdocsize - offset);

    if (offset < m_nDataShift) {
        // did we click in the deadspace at start of the file?
        x = m_nDataShift;
    }

    if (lx) *lx = x;
    if (ly) *ly = y;

    if (offset >= adjdocsize) {
        offset = adjdocsize;
        //m_fCursorAdjustment = (m_nBytesPerLine > 0 && offset % (size_w)m_nBytesPerLine == 0);
        m_fCursorAdjustment = (offset % m_nBytesPerLine == 0);
        if (lx && ly) {
            if (offset == 0)
                *lx = *ly = 0;
            else
                caretPosFromOffset(offset - (size_w)m_nDataShift, lx, ly);
        }
    }

    offset -= std::min((size_w)m_nDataShift, offset);
    return offset;
}

void HexView::positionCaret(int x, int y, int pane)
{
    //(void)x; (void)y;   // In Qt the caret is drawn from m_nCursorOffset directly

    //qDebug() << "position:" << x << y;

    if (m_nLastEditOffset != m_nCursorOffset)
        m_fCursorMoved = true;

    m_fCursorAdjustment = (x == m_nBytesPerLine);
    int physx = logToPhyXCoord(x, pane);
    int physy = y * m_nFontHeight;

    physx += m_nSubItem * m_nFontWidth;

    setCaretPos(physx, physy);
    viewport()->update();
}

void HexView::repositionCaret()
{
    int x, y;
    caretPosFromOffset(m_nCursorOffset, &x, &y);
    positionCaret(x, y, m_nWhichPane);
}

void HexView::setCaretPos(int px, int py)
{
    m_nCaretX = px;
    m_nCaretY = py;
    //qDebug() << "curser" << px << py;
    m_caretVisible      = true;
}
/*void HexView::scrollToCaret()
{
    int x, y, dx = 0, dy = 0;

    scrollTo(m_nCursorOffset);

    if (m_nBytesPerLine > 0 &&
        (m_nCursorOffset + (size_w)m_nDataShift) % (size_w)m_nBytesPerLine != 0)
        m_fCursorAdjustment = false;

    caretPosFromOffset(m_nCursorOffset, &x, &y);

    if (y < 0)
        dy = y;
    else if (y > m_nWindowLines - 1)
        dy = y - m_nWindowLines + 1;

    if (x < 0)
        dx = x;

    scroll(dx, dy);

    caretPosFromOffset(m_nCursorOffset, &x, &y);
    positionCaret(x, y, m_nWhichPane);
}*/

// ── Hit testing ───────────────────────────────────────────────────────────────

bool HexView::isOverResizeBar(int x) const
{
    const int BARWIDTH = 8;
    return (x / BARWIDTH) == (m_nResizeBarPos / BARWIDTH);
}

HitTestRegion HexView::hitTest(int x, int y, int *bookmarkIdx)
{
    const int BARWIDTH = 8;

    if (bookmarkIdx) *bookmarkIdx = -1;

    // Resize bar?
    if (!checkStyle(HVS_FITTOWINDOW) && checkStyle(HVS_RESIZEBAR)) {
        if ((x / BARWIDTH) == (m_nResizeBarPos / BARWIDTH))
            return HVHT_RESIZE;

        int pos1 = (m_nAddressWidth - m_nHScrollPos) * m_nFontWidth +
                   (m_nHexPaddingLeft * m_nFontWidth) / 2;
        if ((x / BARWIDTH) == (pos1 / BARWIDTH))
            return HVHT_RESIZE0;
    }

    // Bookmark note strips (to the right of the ASCII column)?
    // Compute layout so we know which bookmarks are shown as full strips vs tabs.
    // Among overlapping hit targets return the smallest-span bookmark (drawn on
    // top); ties broken by higher storage index.
    {
        // Use treatMouseAsReleased=true so the layout matches the last rendered
        // frame (no button was held when that frame was drawn).  Without this,
        // mousePressEvent fires with mouseHeld=true which freezes cursor-based
        // selection and misclassifies an expanded strip as a collapsed tab,
        // causing the click to miss and fall through to HVHT_MAIN.
        const QVector<BmLayout> layout = computeBookmarkLayout(/*treatMouseAsReleased=*/true);

        int    bestIdx = -1;
        size_w bestLen = (size_w)-1;
        HitTestRegion bestHt = HVHT_NONE;

        for (int i = 0; i < m_bookmarks.size(); ++i) {
            const BmLayout &bml = layout.value(i);
            if (bml.hidden) continue;   // not drawn, not hittable

            const bool isTab = bml.inConflict && !bml.isActive;

            HitTestRegion ht = HVHT_NONE;
            if (isTab) {
                // Collapsed single-line strip — entire area navigates to bm.offset.
                const QRect tab = noteCollapsedRect(m_bookmarks[i]);
                if (tab.contains(x, y)) ht = HVHT_BOOKMARK_COLLAPSED;
            } else {
                // Full strip.
                const NoteStripGeom geom = noteStripGeom(m_bookmarks[i]);
                if (!geom.valid) continue;
                if      (geom.closeRect.contains(x, y)) ht = HVHT_BOOKMARK_CLOSE;
                else if (geom.editRect .contains(x, y)) ht = HVHT_BOOKMARK_EDIT;
                else if (geom.rect     .contains(x, y)) ht = HVHT_BOOKMARK;
            }
            if (ht == HVHT_NONE) continue;

            const size_w len = m_bookmarks[i].length;
            const bool better = (bestIdx == -1)
                                || (len < bestLen)
                                || (len == bestLen && i > bestIdx);
            if (better) { bestIdx = i; bestLen = len; bestHt = ht; }
        }

        if (bestIdx != -1) {
            if (bookmarkIdx) *bookmarkIdx = bestIdx;
            return bestHt;
        }
    }

    // Main hex/ascii area?
    if (x < m_nWindowColumns * m_nFontWidth) {
        size_w curoff = offsetFromPhysCoord(x, y);
        if (inrange(curoff, m_nSelectionStart, m_nSelectionEnd))
            return HVHT_SELECTION;
        return HVHT_MAIN;
    }

    return HVHT_NONE;
}

// ── Mouse event handlers ──────────────────────────────────────────────────────

void HexView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    viewport()->setFocus();

    int x = event->pos().x();
    int y = event->pos().y();

    //qDebug() << "press:" << x << y;

    HitTestRegion ht = hitTest(x, y, &m_pressedBookmarkIdx);
    m_pressedHitTest = ht;

    if (ht & HVHT_RESIZE) {
        if (ht == HVHT_RESIZE)
            m_fResizeBar  = true;
        else if (ht == HVHT_RESIZE0)
            m_fResizeAddr = true;
        return;
    }

    if (ht == HVHT_BOOKMARK) {
        // Full-strip body click: navigate caret and open the inline text editor.
        const int bmIdx = m_pressedBookmarkIdx;
        if (bmIdx >= 0 && bmIdx < m_bookmarks.size()) {
            expandBookmark(bmIdx);
            const size_w target = m_bookmarks[bmIdx].offset;
            m_nCursorOffset   = target;
            m_nSelectionMode  = SEL_NONE;
            if (checkStyle(HVS_BOOKMARK_SELECTION_HIGHLIGHTS)) {
                m_nSelectionStart = target;
                m_nSelectionEnd   = target + m_bookmarks[bmIdx].length;
            } else {
                m_nSelectionStart = target;
                m_nSelectionEnd   = target;
            }
            m_fCursorAdjustment = false;
            scrollCenterIfOffScreen(target, m_bookmarks[bmIdx].length);
            int cx, cy;
            caretPosFromOffset(target, &cx, &cy);
            positionCaret(cx, cy, m_nWhichPane);
            emit cursorChanged(target);
            openNoteEditor(bmIdx, {x, y});
        }
        return;
    }

    if (ht == HVHT_BOOKMARK_COLLAPSED) {
        // Collapsed-strip click: navigate caret; the strip will expand on repaint.
        closeNoteEditor(true);   // save & close any open editor before switching pin
        const int bmIdx = m_pressedBookmarkIdx;
        if (bmIdx >= 0 && bmIdx < m_bookmarks.size()) {
            expandBookmark(bmIdx);
            const size_w target = m_bookmarks[bmIdx].offset;
            m_nCursorOffset  = target;
            m_nSelectionMode = SEL_NONE;
            if (checkStyle(HVS_BOOKMARK_SELECTION_HIGHLIGHTS)) {
                m_nSelectionStart = target;
                m_nSelectionEnd   = target + m_bookmarks[bmIdx].length;
            } else {
                m_nSelectionStart = target;
                m_nSelectionEnd   = target;
            }
            m_fCursorAdjustment = false;
            scrollCenterIfOffScreen(target, m_bookmarks[bmIdx].length);
            int cx, cy;
            caretPosFromOffset(target, &cx, &cy);
            positionCaret(cx, cy, m_nWhichPane);
            emit cursorChanged(target);
            viewport()->update();
        }
        return;
    }

    if (ht == HVHT_BOOKMARK_CLOSE || ht == HVHT_BOOKMARK_EDIT) {
        // Action fires on release, only if the mouse is still over the same button.
        m_pressedOnClose = (ht == HVHT_BOOKMARK_CLOSE);
        m_pressedOnEdit  = (ht == HVHT_BOOKMARK_EDIT);
        viewport()->grabMouse(Qt::PointingHandCursor);
        viewport()->update();
        return;
    }

    // Normal click: position cursor
    m_nSubItem      = 0;
    m_nCursorOffset = offsetFromPhysCoord(x, y, &m_nWhichPane, &x, &y, &m_nSubItem);

    // Mouse-down is a navigation event even if it later becomes a selection drag.
    // Seed the surfaced bookmark now; computeBookmarkLayout() freezes live cursor
    // updates while the button is held, so waiting until release shows the old
    // winner for one frame.
    {
        int    cursorIdx = -1;
        size_w cursorLen = (size_w)-1;
        for (int i = 0; i < m_bookmarks.size(); ++i) {
            const Bookmark &bm = m_bookmarks[i];
            if (m_nCursorOffset >= bm.offset &&
                m_nCursorOffset <  bm.offset + bm.length &&
                bm.length < cursorLen) {
                cursorIdx = i;
                cursorLen = bm.length;
            }
        }
        if (cursorIdx >= 0) {
            m_surfacedBookmarkIdx = cursorIdx;
            if (checkStyle(HVS_BOOKMARK_EXPAND_CURSOR))
                m_expandedBookmarkIdx = cursorIdx;
            else if (m_expandedBookmarkIdx >= 0 && m_expandedBookmarkIdx != cursorIdx)
                m_expandedBookmarkIdx = -1;
        }
    }

    if (inrange(m_nCursorOffset, m_nSelectionStart, m_nSelectionEnd)) {
        // Click inside selection — potential drag-drop start
        m_fStartDrag   = true;
        m_dragStartPos = event->pos();
    } else {
        m_nSelectionMode = SEL_NORMAL;

        if (event->modifiers() & Qt::ShiftModifier) {
            invalidateRange(m_nCursorOffset, m_nSelectionEnd);
            m_nSelectionEnd = m_nCursorOffset;
        } else {
            invalidateRange(m_nSelectionStart, m_nSelectionEnd);
            m_nSelectionStart = m_nCursorOffset;
            m_nSelectionEnd   = m_nCursorOffset;
        }
    }

    positionCaret(x, y, m_nWhichPane);
    emit cursorChanged(m_nCursorOffset);
    viewport()->update();
}

void HexView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }

    // Bookmark button: fire only if the mouse is released over the same button.
    if (m_pressedHitTest == HVHT_BOOKMARK_CLOSE || m_pressedHitTest == HVHT_BOOKMARK_EDIT) {
        const HitTestRegion pressedHt = m_pressedHitTest;
        const int  pressedIdx = m_pressedBookmarkIdx;
        m_pressedHitTest      = HVHT_NONE;
        m_pressedBookmarkIdx = -1;
        m_pressedOnClose      = false;
        m_pressedOnEdit       = false;
        viewport()->releaseMouse();
        int  releaseIdx = -1;
        HitTestRegion releaseHt = hitTest(event->pos().x(), event->pos().y(), &releaseIdx);
        if (releaseHt == pressedHt && releaseIdx == pressedIdx) {
            if (pressedHt == HVHT_BOOKMARK_CLOSE) {
                removeBookmark(pressedIdx);   // handles closeNoteEditor + index adjustment
            } else {
                // HVHT_BOOKMARK_EDIT — settings popup.
                if (pressedIdx >= 0 && pressedIdx < m_bookmarks.size()) {
                    const NoteStripGeom geom = noteStripGeom(m_bookmarks[pressedIdx]);
                    const QRect btnGlobal = geom.valid
                        ? QRect(viewport()->mapToGlobal(geom.editRect.topLeft()), geom.editRect.size())
                        : QRect(event->globalPosition().toPoint(), QSize(0, 0));
                    emit bookmarkSettingsRequested(pressedIdx, btnGlobal);
                }
            }
        }
        return;
    }

    // Drag never started — click collapsed selection
    if (m_fStartDrag) {
        m_nSelectionStart = m_nSelectionEnd;
        m_fStartDrag      = false;
        viewport()->update();
    }

    if (m_fResizeBar || m_fResizeAddr) {
        m_fResizeBar  = false;
        m_fResizeAddr = false;
    } else if (m_nSelectionMode) {
        if (m_scrollTimer.isActive())
            m_scrollTimer.stop();
        if (m_nSelectionMode == SEL_NORMAL)
            m_nSelectionMode = SEL_NONE;
    } else {
        m_pressedBookmarkIdx = -1;
    }

    // Repaint so bookmark display reflects the final cursor position.
    viewport()->update();

    // No explicit releaseMouse() needed — Qt's implicit grab ends automatically
    // when the last mouse button is released.
}

void HexView::mouseMoveEvent(QMouseEvent *event)
{
    int mx = event->pos().x();
    int my = event->pos().y();
    int x  = mx, y = my;
    int pane;

    size_w offset = offsetFromPhysCoord(mx, my, &pane, &x, &y);

    if (m_fStartDrag) {
        if ((event->pos() - m_dragStartPos).manhattanLength() <
            QApplication::startDragDistance())
            return;

        m_fStartDrag = false;
        if (checkStyle(HVS_ENABLEDRAGDROP))
            startDrag();
        return;
    }

    if (m_nSelectionMode) {
        // Pane switch during drag
        if (pane != m_nWhichPane)
            m_nWhichPane = pane;

        if (m_nSelectionMode != SEL_DRAGDROP && m_nCursorOffset != offset) {
            m_nCursorOffset = offset;
            m_nSubItem      = 0;
            invalidateRange(m_nCursorOffset, m_nSelectionEnd);
            m_nSelectionEnd = m_nCursorOffset;
            positionCaret(x, y, m_nWhichPane);
            emit selectionChanged(selectionStart(), selectionEnd());
            emit cursorChanged(m_nCursorOffset);
        }

        // Auto-scroll timer
        QRect rect(0, 0, viewport()->width(), viewport()->height());
        if (m_nFontHeight > 0)
            rect.setBottom(rect.bottom() - rect.bottom() % m_nFontHeight);

        if (rect.contains(event->pos())) {
            if (m_scrollTimer.isActive())
                m_scrollTimer.stop();
        } else {
            if (!m_scrollTimer.isActive()) {
                m_nScrollCounter = 0;
                m_scrollTimer.start(30);
            }
        }

    } else if (m_fResizeBar) {
        // Drag the hex/ascii split point
        int w  = mx / m_nFontWidth + m_nHScrollPos;
        int uw = unitWidth();
        int prevbpl = m_nBytesPerLine;

        if (!checkStyle(HVS_HEX_INVISIBLE)) {
            w -= m_nAddressWidth + m_nHexPaddingLeft;
            if (m_nBytesPerColumn > 0)
                m_nBytesPerLine = (w * m_nBytesPerColumn) /
                                  (m_nBytesPerColumn * uw + 1);
        } else {
            w -= m_nAddressWidth + m_nHexPaddingRight;
            m_nBytesPerLine = w;
        }

        m_nBytesPerLine = std::max(m_nBytesPerLine, 1);

        if (m_nBytesPerLine != prevbpl) {
            m_fCursorAdjustment = false;
            if (m_nVScrollPos > 0)
                pinToOffset(m_nVScrollPinned);
            recalcPositions();
            setupScrollbars();
            repositionCaret();
            viewport()->update();
            emit lineLengthChanged(m_nBytesPerLine);
        }

    } else if (m_fResizeAddr) {
        // Drag the address/hex split (adjusts data shift)
        int pos1 = (m_nAddressWidth - m_nHScrollPos) * m_nFontWidth +
                   (m_nHexPaddingLeft * m_nFontWidth) / 2;
        int oldds = m_nDataShift;
        int pos   = mx / m_nFontWidth + m_nHScrollPos;

        m_nDataShift = (pos - pos1 / m_nFontWidth) / 2;
        m_nDataShift = std::max(0, m_nDataShift);
        m_nDataShift = std::min(m_nDataShift, m_nBytesPerLine - 1);

        if (m_nDataShift != oldds) {
            recalcPositions();
            setupScrollbars();
            repositionCaret();
            viewport()->update();
        }

    } else {
        // Idle — update cursor and hover/press state from hit-test.
        int  bmIdx = -1;
        HitTestRegion ht = hitTest(mx, my, &bmIdx);

        if (m_pressedHitTest == HVHT_BOOKMARK_CLOSE || m_pressedHitTest == HVHT_BOOKMARK_EDIT) {
            // Only track position for the originally-pressed button — never activate any other.
            const bool stillOver = (ht == m_pressedHitTest && bmIdx == m_pressedBookmarkIdx);
            bool &pressedFlag = (m_pressedHitTest == HVHT_BOOKMARK_CLOSE) ? m_pressedOnClose : m_pressedOnEdit;
            if (stillOver != pressedFlag) {
                pressedFlag = stillOver;
                viewport()->update();
            }
        } else {
            const int  newHoverBm    = (ht == HVHT_BOOKMARK           || ht == HVHT_BOOKMARK_CLOSE ||
                                        ht == HVHT_BOOKMARK_EDIT       ||
                                        ht == HVHT_BOOKMARK_COLLAPSED) ? bmIdx : -1;
            const bool newHoverClose = (ht == HVHT_BOOKMARK_CLOSE);
            const bool newHoverEdit  = (ht == HVHT_BOOKMARK_EDIT);
            if (newHoverBm != m_hoverBookmarkIdx || newHoverClose != m_hoverOnClose || newHoverEdit != m_hoverOnEdit) {
                m_hoverBookmarkIdx = newHoverBm;
                m_hoverOnClose     = newHoverClose;
                m_hoverOnEdit      = newHoverEdit;
                viewport()->update();
            }
        }

        switch (ht) {
        case HVHT_RESIZE:
        case HVHT_RESIZE0:        viewport()->setCursor(Qt::SizeHorCursor); break;
        case HVHT_MAIN:
        case HVHT_BOOKMARK:           viewport()->setCursor(Qt::IBeamCursor);       break;
        case HVHT_BOOKMARK_COLLAPSED:
        case HVHT_BOOKMARK_CLOSE:
        case HVHT_BOOKMARK_EDIT:      viewport()->setCursor(Qt::PointingHandCursor); break;
        default:                  viewport()->setCursor(Qt::ArrowCursor);   break;
        }
    }
}

void HexView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mouseDoubleClickEvent(event);
        return;
    }

    int x = event->pos().x();
    int y = event->pos().y();

    HitTestRegion ht = hitTest(x, y, &m_pressedBookmarkIdx);
    if (ht & HVHT_BOOKMARK) return;

    // Select the alphanumeric word under the cursor
    size_t back = (size_t)std::min((size_w)128, m_nCursorOffset);
    uint8_t buf[256];
    size_t len = m_pDataSeq->render(m_nCursorOffset - back, buf, 256);

    size_t i;
    for (i = back; i < len; i++)
        if (!std::isalnum(buf[i])) break;
    m_nSelectionEnd = m_nCursorOffset - back + i;

    for (i = 0; i < back; i++)
        if (!std::isalnum(buf[back - i - 1])) break;
    m_nSelectionStart = m_nCursorOffset - i;
    m_nCursorOffset   = m_nSelectionEnd;

    emit selectionChanged(selectionStart(), selectionEnd());
    emit cursorChanged(m_nCursorOffset);
    viewport()->update();
    scrollToCaret();
}

void HexView::wheelEvent(QWheelEvent *event)
{
    int nScrollLines  = 3;
    int nDelta        = event->angleDelta().y();
    int nScrollAmount = nDelta + m_nScrollMouseRemainder;
    m_nScrollMouseRemainder = nScrollAmount % (120 / nScrollLines);
    scroll(0, -nScrollAmount * nScrollLines / 120);
    repositionCaret();
}

void HexView::contextMenuEvent(QContextMenuEvent *event)
{
    // Bookmark context menu — intercepts right-clicks on any note strip.
    {
        const QPoint vp = viewport()->mapFromGlobal(event->globalPos());
        int bmIdx = -1;
        const HitTestRegion ht = hitTest(vp.x(), vp.y(), &bmIdx);
        if (ht == HVHT_BOOKMARK || ht == HVHT_BOOKMARK_CLOSE || ht == HVHT_BOOKMARK_EDIT) {
            if (m_bookmarkContextMenuExternallyHandled) {
                emit bookmarkContextRequested(bmIdx, QRect(event->globalPos(), QSize(1, 1)));
                return;
            }

            QMenu bmMenu(this);
            themeMenu(&bmMenu);
            QAction *editAct   = bmMenu.addAction(tr("&Edit"));
            QAction *deleteAct = bmMenu.addAction(tr("&Delete"));

            // Show the gear button in its pressed state for the duration of
            // the context menu — same visual treatment as the settings popup.
            m_bookmarkPopupIdx = bmIdx;
            viewport()->update();
            QAction *act = bmMenu.exec(event->globalPos());
            m_bookmarkPopupIdx = -1;
            viewport()->update();

            if (act == editAct)
                emit bookmarkEditRequested(bmIdx);
            else if (act == deleteAct)
                removeBookmark(bmIdx);
            return;
        }
    }

    if (m_contextMenu) {
        m_contextMenu->exec(event->globalPos());
        return;
    }

    QMenu menu(this);
    themeMenu(&menu);
    bool hasSel = (m_nSelectionStart != m_nSelectionEnd);
    bool ro     = (m_nEditMode == HVMODE_READONLY);

    QAction *undoAct   = menu.addAction("&Undo");  undoAct->setEnabled(canUndo());
    QAction *redoAct   = menu.addAction("&Redo");  redoAct->setEnabled(canRedo());
    menu.addSeparator();
    QAction *cutAct    = menu.addAction("Cu&t");   cutAct->setEnabled(hasSel && !ro);
    QAction *copyAct   = menu.addAction("&Copy");  copyAct->setEnabled(hasSel);
    QAction *pasteAct  = menu.addAction("&Paste"); pasteAct->setEnabled(!ro);
    QAction *delAct    = menu.addAction("&Delete");delAct->setEnabled(hasSel && !ro);
    menu.addSeparator();
    QAction *selAllAct = menu.addAction("Select &All");

    QAction *act = menu.exec(event->globalPos());
    if (!act) return;

    if      (act == undoAct)   undo();
    else if (act == redoAct)   redo();
    else if (act == cutAct)    onCut();
    else if (act == copyAct)   onCopy();
    else if (act == pasteAct)  onPaste();
    else if (act == delAct)    onClear();
    else if (act == selAllAct) selectAll();
}

// ── Scroll timer slot (auto-scroll during mouse drag) ─────────────────────────

void HexView::onScrollTimer()
{
    QRect rect(0, 0, viewport()->width(), viewport()->height());
    if (m_nFontHeight > 0)
        rect.setBottom(rect.bottom() - rect.bottom() % m_nFontHeight);

    QPoint pt = viewport()->mapFromGlobal(QCursor::pos());

    int dx = 0, dy = 0;
    if (m_nSelectionMode == SEL_DRAGDROP) {
        constexpr int kEdgeBand = 24;
        if      (pt.y() <= rect.top() + kEdgeBand)    dy = scrollDir(m_nScrollCounter, pt.y() - (rect.top() + kEdgeBand));
        else if (pt.y() >= rect.bottom() - kEdgeBand) dy = scrollDir(m_nScrollCounter, pt.y() - (rect.bottom() - kEdgeBand));
        if      (pt.x() <= rect.left() + kEdgeBand)   dx = scrollDir(m_nScrollCounter, pt.x() - (rect.left() + kEdgeBand));
        else if (pt.x() >= rect.right() - kEdgeBand)  dx = scrollDir(m_nScrollCounter, pt.x() - (rect.right() - kEdgeBand));
    } else {
        if      (pt.y() < rect.top())    dy = scrollDir(m_nScrollCounter, pt.y() - rect.top());
        else if (pt.y() >= rect.bottom()) dy = scrollDir(m_nScrollCounter, pt.y() - rect.bottom());
        if      (pt.x() < rect.left())   dx = scrollDir(m_nScrollCounter, pt.x() - rect.left());
        else if (pt.x() > rect.right())  dx = scrollDir(m_nScrollCounter, pt.x() - rect.right());
    }

    if (dx != 0 || dy != 0) {
        scroll(dx, dy);

        if (m_nSelectionMode == SEL_DRAGDROP) {
            updateDropCaret(pt);
        } else {
            // Fake a mouse-move to update selection
            int x = pt.x(), y = pt.y();
            int pane;
            size_w offset = offsetFromPhysCoord(x, y, &pane, &x, &y);

            if (pane != m_nWhichPane)
                m_nWhichPane = pane;

            if (m_nCursorOffset != offset) {
                m_nCursorOffset = offset;
                m_nSubItem      = 0;
                invalidateRange(m_nCursorOffset, m_nSelectionEnd);
                m_nSelectionEnd = m_nCursorOffset;
                positionCaret(x, y, m_nWhichPane);
                emit selectionChanged(selectionStart(), selectionEnd());
                emit cursorChanged(m_nCursorOffset);
            }
        }

        viewport()->update();
    }

    m_nScrollCounter++;
}
