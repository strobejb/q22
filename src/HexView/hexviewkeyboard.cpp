//
//  hexviewkeyboard.cpp — Keyboard input (Qt port of HexViewKeyboard.cpp)
//
//  www.catch22.net
//  Copyright (C) 2012 James Brown
//  Qt6 port: see LICENCE.TXT
//

#include "hexview.h"
#include <QApplication>
#include <QKeyEvent>
#include <algorithm>

// ── scrollToCaret ─────────────────────────────────────────────────────────────

void HexView::scrollToCaret()
{
    int x, y;
    int dx = 0, dy = 0;

    scrollTo(m_nCursorOffset);

    if ((m_nCursorOffset + (size_w)m_nDataShift) % (size_w)m_nBytesPerLine != 0)
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
}

// ── forwardDelete / backDelete ────────────────────────────────────────────────

bool HexView::forwardDelete()
{
    if (!m_pDataSeq) return false;

    size_w length;
    bool success = false;

    if (!allowChange(m_nCursorOffset, std::max(selectionSize(), (size_w)1), HVMETHOD_DELETE))
        return false;

    if (selectionSize() > 0) {
        length  = selectionSize();
        success = m_pDataSeq->erase(selectionStart(), length);
        m_nCursorOffset = selectionStart();
        m_pDataSeq->breakopt();
    } else {
        length  = 1;
        success = m_pDataSeq->erase(m_nCursorOffset, length);
    }

    m_nSelectionStart = m_nCursorOffset;
    m_nSelectionEnd   = m_nCursorOffset;

    if (success)
        fireChanged(m_nCursorOffset, length, HVMETHOD_DELETE);

    return true;
}

bool HexView::backDelete()
{
    if (!m_pDataSeq) return false;

    size_w offset = 0, length = 0;
    bool success = false;

    if (selectionSize() > 0) {
        offset = selectionStart();
        length = selectionSize();

        if (!allowChange(offset, length, HVMETHOD_DELETE))
            return false;

        success = m_pDataSeq->erase(offset, length);
        m_nCursorOffset = offset;
        m_pDataSeq->breakopt();
    } else if (m_nCursorOffset > 0) {
        offset = --m_nCursorOffset;
        length = 1;

        if (!allowChange(offset, length, HVMETHOD_DELETE))
            return false;

        success = m_pDataSeq->erase(offset, length);
    }

    m_nSelectionStart = m_nCursorOffset;
    m_nSelectionEnd   = m_nCursorOffset;

    if (success)
        fireChanged(offset, length, HVMETHOD_DELETE);

    return true;
}

// ── undo / redo ───────────────────────────────────────────────────────────────

static uint undoMethod(size_w newlen, size_w oldlen)
{
    if (newlen == oldlen)  return HVMETHOD_OVERWRITE;
    if (newlen <  oldlen)  return HVMETHOD_DELETE;
    return HVMETHOD_INSERT;
}

bool HexView::undo()
{
    if (!m_pDataSeq) return false;
    if (checkStyle(HVS_DISABLE_UNDO)) return false;

    size_w oldlen = m_pDataSeq->size();

    if (m_pDataSeq->undo()) {
        m_nSelectionStart = m_pDataSeq->event_index();
        m_nSelectionEnd   = m_pDataSeq->event_length() + m_nSelectionStart;
        m_nCursorOffset   = m_nSelectionEnd;

        fireChanged(m_pDataSeq->event_index(),
                    m_pDataSeq->event_datalength(),
                    undoMethod(m_pDataSeq->size(), oldlen));
        return true;
    }
    return false;
}

bool HexView::redo()
{
    if (!m_pDataSeq) return false;
    if (checkStyle(HVS_DISABLE_UNDO)) return false;

    size_w oldlen = m_pDataSeq->size();

    if (m_pDataSeq->redo()) {
        m_nSelectionStart = m_pDataSeq->event_index();
        m_nSelectionEnd   = m_pDataSeq->event_length() + m_nSelectionStart;
        m_nCursorOffset   = m_nSelectionEnd;

        fireChanged(m_pDataSeq->event_index(),
                    m_pDataSeq->event_datalength(),
                    undoMethod(m_pDataSeq->size(), oldlen));
        return true;
    }
    return false;
}

bool HexView::canUndo() const
{
    return m_pDataSeq && m_pDataSeq->canundo();
}

bool HexView::canRedo() const
{
    return m_pDataSeq && m_pDataSeq->canredo();
}


// ── keyPressEvent ─────────────────────────────────────────────────────────────

void HexView::keyPressEvent(QKeyEvent *event)
{
    if (!m_pDataSeq) return;

    bool fCtrlDown  = (event->modifiers() & Qt::ControlModifier) != 0;
    bool fShiftDown = (event->modifiers() & Qt::ShiftModifier)   != 0;
    bool fForceUpdate = !fShiftDown;
    size_w oldoffset = m_nCursorOffset;

    int key = event->key();

    // Modifier-only keys — nothing to do
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt)
        return;

    if (m_inlineRangeBookmarkIdx >= 0) {
        if (key == Qt::Key_Escape) {
            clearBookmarkRangeStepper();
            viewport()->update();
            return;
        }
        if (kBookmarkRangeEditExperiment == BookmarkRangeEditExperiment::Stepper &&
                key == Qt::Key_Up) {
            stepActiveBookmarkRange(1);
            viewport()->update();
            return;
        }
        if (kBookmarkRangeEditExperiment == BookmarkRangeEditExperiment::Stepper &&
                key == Qt::Key_Down) {
            stepActiveBookmarkRange(-1);
            viewport()->update();
            return;
        }

        clearBookmarkRangeStepper();
        viewport()->update();
    }

    switch (key)
    {
    case Qt::Key_Escape:
        fForceUpdate = true;
        break;

    case Qt::Key_Insert:
        if (fCtrlDown) {
            onCopy();
        } else if (fShiftDown) {
            onPaste();
        } else if (!checkStyle(HVS_FIXED_EDITMODE)) {
            if (m_nEditMode == HVMODE_INSERT)
                m_nEditMode = HVMODE_OVERWRITE;
            else if (m_nEditMode == HVMODE_OVERWRITE)
                m_nEditMode = HVMODE_INSERT;
            // notify mode change
            emit editModeChanged(m_nEditMode);
        }
        return;

    case Qt::Key_Z:
        m_nSubItem = 0;
        if (fCtrlDown) undo();
        return;

    case Qt::Key_Y:
        m_nSubItem = 0;
        if (fCtrlDown) redo();
        return;

    case Qt::Key_Delete:
        if (m_nEditMode == HVMODE_READONLY) {
            return;
        } else if (m_nEditMode == HVMODE_INSERT || checkStyle(HVS_ALWAYSDELETE)) {
            forwardDelete();
        } else if (selectionSize() > 0) {
            uint8_t b = 0;
            fillData(&b, 1, selectionSize());
        }
        return;

    case Qt::Key_Backspace:
        if (m_nEditMode == HVMODE_READONLY) {
            return;
        } else if (m_nEditMode == HVMODE_INSERT || checkStyle(HVS_ALWAYSDELETE)) {
            backDelete();
        } else if (selectionSize() > 0) {
            uint8_t b = 0;
            fillData(&b, 1, selectionSize());
        }
        return;

    case Qt::Key_Left:
        if (fCtrlDown) {
            scroll(-1, 0);
            return;
        }
        if (m_nCursorOffset > 0)
            m_nCursorOffset--;
        m_fCursorAdjustment = false;
        break;

    case Qt::Key_Right:
        if (fCtrlDown) {
            scroll(1, 0);
            return;
        }
        if (m_nCursorOffset < size()) {
            m_nCursorOffset++;
            if (m_nCursorOffset == size() &&
                size() % (size_w)m_nBytesPerLine == 0)
                m_fCursorAdjustment = true;
            else
                m_fCursorAdjustment = false;
        }
        break;

    case Qt::Key_Up:
        if (fCtrlDown) {
            scroll(0, -1);
            return;
        }
        if (m_nCursorOffset > (size_w)m_nBytesPerLine)
            m_nCursorOffset -= (size_w)m_nBytesPerLine;
        else if (m_nCursorOffset == (size_w)m_nBytesPerLine && !m_fCursorAdjustment)
            m_nCursorOffset = 0;
        break;

    case Qt::Key_Down:
        if (fCtrlDown) {
            scroll(0, 1);
            return;
        }
        {
            size_w step = std::min((size_w)m_nBytesPerLine,
                                   size() - m_nCursorOffset);
            m_nCursorOffset += step;

            if (m_nCursorOffset >= size() && !m_fCursorAdjustment) {
                if (oldoffset % (size_w)m_nBytesPerLine <
                        size() % (size_w)m_nBytesPerLine ||
                    size() % (size_w)m_nBytesPerLine == 0)
                {
                    m_nCursorOffset = oldoffset;
                    fForceUpdate = true;
                }
            }
        }
        break;

    case Qt::Key_Home:
        if (fCtrlDown) {
            m_nCursorOffset = 0;
            scroll(0, -(int)m_nVScrollPos);   // scroll to top
        } else {
            m_nCursorOffset += (size_w)m_nDataShift;
            if (m_fCursorAdjustment && m_nCursorOffset > 0)
                m_nCursorOffset--;
            m_nCursorOffset -= m_nCursorOffset % (size_w)m_nBytesPerLine;
            m_nCursorOffset -= std::min((size_w)m_nDataShift, m_nCursorOffset);
        }
        m_fCursorAdjustment = false;
        break;

    case Qt::Key_End:
        if (fCtrlDown) {
            m_nCursorOffset = size();
            if (m_nCursorOffset % (size_w)m_nBytesPerLine == 0)
                m_fCursorAdjustment = true;
            // scroll to bottom
            {
                size_w total = numFileLines(size());
                if (total > (size_w)m_nWindowLines)
                    scroll(0, (int)(total - (size_w)m_nWindowLines - m_nVScrollPos));
            }
        } else {
            m_nCursorOffset += (size_w)m_nDataShift;
            size_w adjdocsize = size() + (size_w)m_nDataShift;

            if (!m_fCursorAdjustment) {
                if (adjdocsize >= (size_w)m_nBytesPerLine &&
                    adjdocsize - (size_w)m_nBytesPerLine >= m_nCursorOffset)
                {
                    m_nCursorOffset += (size_w)m_nBytesPerLine -
                                       (m_nCursorOffset % (size_w)m_nBytesPerLine);
                    m_fCursorAdjustment = true;
                } else {
                    m_nCursorOffset += adjdocsize - m_nCursorOffset;
                }
            }

            if (m_nCursorOffset >= adjdocsize &&
                adjdocsize % (size_w)m_nBytesPerLine == 0)
                m_fCursorAdjustment = true;

            m_nCursorOffset -= std::min((size_w)m_nDataShift, m_nCursorOffset);
        }
        break;

    case Qt::Key_PageUp:
        m_nCursorOffset -= std::min(m_nCursorOffset,
                                    (size_w)m_nBytesPerLine * (size_w)m_nWindowLines);
        break;

    case Qt::Key_PageDown:
        m_nCursorOffset += std::min(size() - m_nCursorOffset,
                                    (size_w)m_nBytesPerLine * (size_w)m_nWindowLines);
        if (m_nCursorOffset >= size() &&
            size() % (size_w)m_nBytesPerLine == 0)
            m_fCursorAdjustment = true;
        break;

    case Qt::Key_Tab:
        if (event->modifiers() & Qt::ControlModifier) {
            emit paneFocusRequested();
            return;
        }
        m_nWhichPane ^= 1;
        fForceUpdate = true;
        break;

    default:
        // Pass printable characters to onChar; ignore everything else
        if (!event->text().isEmpty()) {
            uint ch = event->text().at(0).unicode();
            if (ch >= 32)
                onChar(ch);
        }
        return;
    }

    m_nSubItem = 0;

    if (m_nCursorOffset != oldoffset || fForceUpdate) {
        const bool hadSelection = selectionStart() != selectionEnd();
        if (fShiftDown) {
            m_nSelectionEnd = m_nCursorOffset;
            invalidateRange(oldoffset, m_nSelectionEnd);
        } else if (key != Qt::Key_Tab) {
            if (m_nSelectionEnd != m_nSelectionStart)
                invalidateRange(m_nSelectionEnd, m_nSelectionStart);
            m_nSelectionEnd   = m_nCursorOffset;
            m_nSelectionStart = m_nCursorOffset;
        }

        scrollToCaret();
        emit cursorChanged(m_nCursorOffset);
        if (hadSelection || selectionStart() != selectionEnd())
            emit selectionChanged(selectionStart(), selectionEnd());

        if (key == Qt::Key_PageUp || key == Qt::Key_PageDown)
            refreshWindow();
    }
}

// ── onChar ────────────────────────────────────────────────────────────────────

void HexView::onChar(uint nChar)
{
    if (nChar < 32) return;
    if (!m_pDataSeq) return;

    if (m_nEditMode == HVMODE_READONLY) {
        QApplication::beep();
        return;
    }

    if (m_nWhichPane == 0) {    // hex column
        // Number of digits per byte in each format: hex=2, dec=3, oct=3, bin=8
        static const int cl[4] = { 2, 3, 3, 8 };
        // Base for each format
        static const int cb[4] = { 16, 10, 8, 2 };

        int cf = (int)(m_nControlStyles & HVS_FORMAT_MASK);

        // Validate character for the current format
        bool valid = false;
        switch (cf) {
        case HVS_FORMAT_HEX: valid = std::isxdigit(nChar) != 0; break;
        case HVS_FORMAT_DEC: valid = (nChar >= '0' && nChar <= '9'); break;
        case HVS_FORMAT_OCT: valid = (nChar >= '0' && nChar <= '7'); break;
        case HVS_FORMAT_BIN: valid = (nChar == '0' || nChar == '1'); break;
        }
        if (!valid) {
            QApplication::beep();
            return;
        }

        // Digit value
        int val2;
        if      (nChar >= 'a') val2 = (int)(nChar - 'a') + 0x0a;
        else if (nChar >= 'A') val2 = (int)(nChar - 'A') + 0x0A;
        else                   val2 = (int)(nChar - '0');

        // Read existing byte (or zero in insert mode)
        uint8_t b = 0;
        if (m_nEditMode != HVMODE_INSERT)
            getData(m_nCursorOffset, &b, 1);

        // Compute new byte value by replacing the current digit position
        int base  = cb[cf];
        int power = 1;
        for (int i = cl[cf] - 1; i > m_nSubItem; i--)
            power *= base;

        int val = b;
        val = (val / power) % base;
        val *= power;
        val = b - val;
        val += val2 * power;

        if (val > 0xff)
            val -= b % power;

        b = (uint8_t)val;

        m_nSubItem++;

        if (m_fCursorMoved) {
            enterData(&b, 1, (m_nWhichPane == 0 ? false : true), true, false);
            m_nLastEditOffset = m_nCursorOffset;
            m_fCursorMoved    = false;
            if (m_nSubItem == cl[cf]) {
                m_nSubItem = 0;
                m_nCursorOffset++;
            }
            repositionCaret();
        } else {
            // In-place edit of the last-modified byte — no new span
            m_pDataSeq->getlastmodref() = b;
            fireChanged(m_nCursorOffset, 1, HVMETHOD_OVERWRITE);

            if (m_nSubItem == cl[cf]) {
                m_nSubItem = 0;
                m_nCursorOffset++;
            }

            repositionCaret();
        }
    } else {
        // ASCII column — enter byte as-is
        uint8_t b = (uint8_t)nChar;
        m_nSubItem = 0;
        enterData(&b, 1, true, true, false);
    }
}
