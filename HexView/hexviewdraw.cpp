//
//  hexviewdraw.cpp — Rendering pipeline (Qt port of HexViewDraw.cpp)
//
//  www.catch22.net
//  Copyright (C) 2012 James Brown
//  Qt6 port: see LICENCE.TXT
//

#include "hexview.h"

#include <QApplication>
#include <QFontMetrics>
#include <QGlyphRun>
#include <QPainter>
#include <QPaintEvent>
#include <algorithm>
#include <cstdio>
#include <cstring>

// ── HEXCOL comparison ─────────────────────────────────────────────────────────

static bool hexcolEq(const HEXCOL &a, const HEXCOL &b)
{
    return a.colFG == b.colFG && a.colBG == b.colBG;
}

// ── Static helper: fill attrList with (fg, bg) for `count` chars ──────────────

static void addAttr(ATTR **pp, QRgb fg, QRgb bg, size_t count)
{
    ATTR *p = *pp;
    for (size_t i = 0; i < count; i++) {
        p[i].colFG = fg;
        p[i].colBG = bg;
    }
    *pp += count;
}

// ── int_to_bin (binary format) ────────────────────────────────────────────────

static size_t intToBin(char *buf, unsigned width, unsigned num)
{
    char *start = buf;
    while (width > 0) {
        for (int i = 0; i < 8; i++) {
            *buf++ = (num & 0x80u) ? '1' : '0';
            num <<= 1;
        }
        --width;
    }
    *buf = '\0';
    return (size_t)(buf - start);
}

// ── realiseColour ─────────────────────────────────────────────────────────────
//
//  If the high bit (HEX_SYS_COLOR) is set the lower bytes hold a Win32
//  COLOR_* index; map it to the corresponding QPalette role.

QRgb HexView::realiseColour(QRgb cr)
{
    if (!(cr & HEX_SYS_COLOR))
        return cr & 0x00FFFFFFu;

    // QColor::rgb() returns 0xFFRRGGBB (alpha=0xFF in the high byte).
    // Strip alpha so all values in attrList have a consistent 0x00RRGGBB form;
    // otherwise span comparisons falsely split when mixing syscolor-resolved
    // colours with direct-stored colours (which are already 0x00RRGGBB).
    const QPalette pal = palette();
    // Use Active group when focused, Inactive when not — this makes the
    // selection highlight visually dim when the HexView loses focus.
    const QPalette::ColorGroup hlGroup =
        hasFocus() ? QPalette::Active : QPalette::Inactive;
    QRgb resolved;
    switch (cr & HEX_GET_COLOR) {
    case COLOR_WINDOW:        resolved = pal.color(QPalette::Base).rgb();                    break;
    case COLOR_WINDOWTEXT:    resolved = pal.color(QPalette::WindowText).rgb();              break;
    case COLOR_HIGHLIGHT:     resolved = pal.color(hlGroup, QPalette::Highlight).rgb();      break;
    case COLOR_HIGHLIGHTTEXT: resolved = pal.color(hlGroup, QPalette::HighlightedText).rgb();break;
    case COLOR_BTNFACE:       resolved = pal.color(QPalette::Button).rgb();                  break;
    case COLOR_BTNSHADOW:     resolved = pal.color(QPalette::Dark).rgb();                    break;
    case COLOR_GRAYTEXT:      resolved = pal.color(QPalette::Disabled,
                                                   QPalette::WindowText).rgb();              break;
    case COLOR_BTNHIGHLIGHT:  resolved = pal.color(QPalette::Light).rgb();                   break;
    default:                  resolved = pal.color(QPalette::Window).rgb();                  break;
    }
    return resolved & 0x00FFFFFFu;
}

// ── formatAddress ─────────────────────────────────────────────────────────────

size_t HexView::formatAddress(size_w addr, char *buf, size_t buflen)
{
    if (checkStyle(HVS_ADDR_INVISIBLE)) {
        buf[0] = '\0';
        return 0;
    }

    addr += m_nAddressOffset;

    if (checkStyle(HVS_ADDR_DEC)) {
        snprintf(buf, buflen, " %0.*llu",
                 m_nAddressDigits, (unsigned long long)addr);
    } else if (checkStyle(HVS_ADDR_MIDCOLON)) {
        const char *fmt = checkStyle(HVS_LOWERCASEHEX)
                          ? " %0.*llx:%04x" : " %0.*llX:%04X";
        snprintf(buf, buflen, fmt,
                 m_nAddressDigits - 4, (unsigned long long)(addr >> 16),
                 (unsigned)(addr & 0xFFFFu));
    } else {
        const char *fmt = checkStyle(HVS_LOWERCASEHEX) ? " %0.*llx" : " %0.*llX";
        snprintf(buf, buflen, fmt, m_nAddressDigits, (unsigned long long)addr);
    }

    if (checkStyle(HVS_ADDR_ENDCOLON))
        strncat(buf, ":", buflen - strlen(buf) - 1);

    size_t len = strlen(buf);

    // Trim to m_nAddressWidth if the formatted string is longer
    if ((int)len > m_nAddressWidth) {
        size_t excess = len - (size_t)m_nAddressWidth;
        memmove(buf + 1, buf + 1 + excess, len - excess);
        len = (size_t)m_nAddressWidth;
        buf[len] = '\0';
    }

    return strlen(buf);
}

// ── formatHexUnit ─────────────────────────────────────────────────────────────

size_t HexView::formatHexUnit(uint8_t *data, char *buf, size_t /*buflen*/)
{
    switch (m_nControlStyles & HVS_FORMAT_MASK) {
    case HVS_FORMAT_HEX:
        return (size_t)snprintf(buf, 16,
                                checkStyle(HVS_LOWERCASEHEX) ? "%02x" : "%02X",
                                data[0]);
    case HVS_FORMAT_DEC:
        return (size_t)snprintf(buf, 16, "%03d", data[0]);
    case HVS_FORMAT_OCT:
        return (size_t)snprintf(buf, 16, "%03o", data[0]);
    case HVS_FORMAT_BIN:
        return intToBin(buf, 1, data[0]);
    default:
        buf[0] = '\0';
        return 0;
    }
}

// ── invalidateRange ───────────────────────────────────────────────────────────

void HexView::invalidateRange(size_w start, size_w finish)
{
    if (m_nFontHeight == 0) return;

    start  += (size_w)m_nDataShift;
    finish += (size_w)m_nDataShift;
    if (start > finish) std::swap(start, finish);

    size_w screenStartOffset = m_nVScrollPos * (size_w)m_nBytesPerLine;
    size_w screenEndOffset   = (m_nVScrollPos + (size_w)m_nWindowLines + 1) * (size_w)m_nBytesPerLine;

    if(start  < screenStartOffset) 	start  = screenStartOffset;
    if(start  > screenEndOffset)   	start  = screenEndOffset;
    if(finish < screenStartOffset)	finish = screenStartOffset;
    if(finish > screenEndOffset)  	finish = screenEndOffset;

    if(screenEndOffset < screenStartOffset)
        screenEndOffset = -1;

    //int firstRow = (int)(start  / (size_w)m_nBytesPerLine - m_nVScrollPos);
    //int lastRow  = (int)(finish / (size_w)m_nBytesPerLine - m_nVScrollPos);

    /*update(QRect(0,
                 firstRow * m_nFontHeight,
                 width(),
                 (lastRow - firstRow + 1) * m_nFontHeight));*/

    size_w length = finish - start;
    int y = (int)(start / m_nBytesPerLine - m_nVScrollPos);

    while(length != 0)
    {
        //RECT   rect;
        size_t x   = (int)(start % m_nBytesPerLine);
        size_t len = std::min((size_t)m_nBytesPerLine - x, (size_t)length);

        // hex column
        update(QRect(
                QPoint(logToPhyXCoord(x, 0),     y * m_nFontHeight),
                QPoint(logToPhyXCoord(x+len, 0), y * m_nFontHeight + m_nFontHeight)
            ));

        //InvalidateRect(m_hWnd, &rect, FALSE);

        // ascii column
        update(QRect(
                QPoint(logToPhyXCoord(x, 1), y * m_nFontHeight),
                QPoint(logToPhyXCoord(x+len, 1), y * m_nFontHeight + m_nFontHeight)
            ));

        //InvalidateRect(m_hWnd, &rect, FALSE);

        y++;
        start   = 0;
        length -= len;
    }

}

// ── getHighlightCol ───────────────────────────────────────────────────────────
//
//  Determines the fg/bg colours for a single byte at `offset` in `pane`
//  (0=hex, 1=ascii).  col1 is the colour of the byte itself; col2 is the
//  colour of the inter-byte space that follows (may differ at a group
//  boundary or selection edge).

bool HexView::getHighlightCol(size_w offset, int pane, BOOKNODE *itemStart,
                               HEXCOL *col1, HEXCOL *col2,
                               bool fModified, bool fMatched, bool fIncSelection)
{
    size_w selstart = std::min(m_nSelectionStart, m_nSelectionEnd);
    size_w selend   = std::max(m_nSelectionStart, m_nSelectionEnd);
    const bool   focused  = hasFocus();

    // Default colour indices
    int nSchemeIdxFG;
    int nSchemeIdxBG;
    if(pane == 0)
    {
        nSchemeIdxFG = (((offset + m_nDataShift)% m_nBytesPerLine) / m_nBytesPerColumn) & 1 ? HVC_HEXEVEN : HVC_HEXODD;
        nSchemeIdxBG = HVC_BACKGROUND;
    }
    else
    {
        nSchemeIdxFG = HVC_ASCII;
        nSchemeIdxBG = HVC_BACKGROUND;
    }

    // modified bytes override normal settings
    if(fModified && checkStyle(HVS_SHOWMODS))
        nSchemeIdxFG = HVC_MODIFY;

    if(fMatched)
    {
        //nSchemeIdxFG = HVC_BACKGROUND;
        nSchemeIdxBG = HVC_MATCHED;
    }

    // Check if any bookmark covers this byte
    BOOKNODE *hi = nullptr;
    if (itemStart) {
        for (BOOKNODE *n = itemStart; n != m_BookTail; n = n->next) {
            if (offset >= n->bookmark.offset &&
                offset <  n->bookmark.offset + n->bookmark.length) {
                hi = n;
                break;
            }
        }
    }

    if (hi &&
        offset >= hi->bookmark.offset &&
        offset < hi->bookmark.offset + hi->bookmark.length)// >= 0)
    {
        //col1->colFG = Highlight[idx].colFG;
        //col1->colBG = Highlight[idx].colBG;


        col1->colFG = hi->bookmark.col;
        col1->colBG = hi->bookmark.backcol;

        //col1->colFG = RGB(255,255,255);
        //col1->colBG = RGB(128,128,128);

        *col2 = *col1;

        if(fModified)
        {
            col1->colFG = getHexColour(HVC_MODIFY);
            col2->colFG = getHexColour(HVC_MODIFY);
        }
    }
    // no highlight, use the default window scheme
    else
    {
        col1->colFG = getHexColour(nSchemeIdxFG);
        col1->colBG = getHexColour(nSchemeIdxBG);
        *col2 = *col1;
    }


    // Selection overrides everything

    // selected data overrides everything else!
    if(fIncSelection && offset >= selstart && offset < selend)
    {
        // selected colour is next sequential index
        if(focused)
            nSchemeIdxFG++;

        //nSchemeIdxFG = nSchemeIdxBG;
        //nSchemeIdxBG++;
        //nSchemeIdxFG++;
        //nSchemeIdxBG++;


        if(nSchemeIdxBG == HVC_MATCHED)
            nSchemeIdxFG = HVC_MATCHEDSEL;

        //nSchemeIdxBG = HVC_SELECTION;

        if(pane != m_nWhichPane)
        {
            //if(nSchemeIdxBG == HVC_BACKGROUND)
            nSchemeIdxBG = HVC_SELECTION;
            //	nSchemeIdxFG++;
            //	nSchemeIdxBG++;
        }
        else
        {
            //if(nSchemeIdxBG == HVC_BACKGROUND)
            nSchemeIdxBG = HVC_SELECTION;

        }

        if(!focused)
        {
            nSchemeIdxBG = HVC_SELECTION3;
            //nSchemeIdxFG = HVC_SELECTION4;
        }


        if(checkStyle(HVS_INVERTSELECTION))
        {
            col1->colBG = 0xffffff & ~col1->colBG;
            col1->colFG = 0xffffff & ~col1->colFG;
        }
        else
        {
            //col1->colFG = MixRgb(GetHexColour(HVC_SELECTION), GetHexColour(nSchemeIdxFG));
            //col1->colBG = MixRgb(GetHexColour(HVC_SELECTION), GetHexColour(nSchemeIdxBG));

            //if(!fModified)
            col1->colFG = !hi || fModified ? getHexColour(nSchemeIdxFG) :col2->colBG;
            col1->colBG = getHexColour(nSchemeIdxBG);


            //col1->colFG = GetHexColour((idx == -1) ? nSchemeIdxFG : HVC_BOOKSEL);
            //col1->colBG = idx == -1 ? col1->colBG : 0xffffff & ~col1->colFG;
        }


#ifdef SELECTION_USES_HIGHLIGHT
        if(m_fHighlighting)
        {
            col1->colFG = 0xffffff & ~GetHexColour(HVC_BOOKMARK_FG);
            col1->colBG = 0xffffff & ~GetHexColour(HVC_BOOKMARK_BG);
        }
#endif

        if(offset < selend - 1 && selend > 0)
            *col2 = *col1;
    }

    // take into account any offset/shift in the datasource
    offset += m_nDataShift;//Start;
    if((offset + 1) % (m_nBytesPerLine) == 0 && offset != 0)
    {
        col2->colFG = col1->colFG;
        col2->colBG = getHexColour(HVC_BACKGROUND);
    }

    return true;
}

// ── formatLine ────────────────────────────────────────────────────────────────
//
//  Fills szBuf with the printable characters for one display line and fills
//  attrList with one ATTR per character describing its fg/bg colours.
//  Returns the number of characters written to szBuf.

size_t HexView::formatLine(uint8_t *data, size_t length, size_w offset, size_t dataShift,
                            char *szBuf, size_t nBufLen,
                            ATTR *attrList, seqchar_info *infobuf,
                            bool fIncSelection)
{
    char  *ptr    = szBuf;
    ATTR  *aptr   = attrList;
    size_t i;

    // ── Address column ────────────────────────────────────────────────────────
    ptr += formatAddress(offset, ptr, nBufLen);
    addAttr(&aptr,
            getHexColour(HVC_ADDRESS),
            getHexColour(HVC_BACKGROUND),
            (size_t)(ptr - szBuf));

    BOOKNODE *highlight = findBookmark(offset, offset + length);


    // ── Hex column ────────────────────────────────────────────────────────────
    if (!checkStyle(HVS_HEX_INVISIBLE)) {

        for (i = 0; i < (size_t)m_nHexPaddingLeft; i++)
            *ptr++ = ' ';
        addAttr(&aptr,
                getHexColour(HVC_ADDRESS),
                getHexColour(HVC_BACKGROUND),
                (size_t)m_nHexPaddingLeft);

        for (i = 0; i < length; i++) {
            HEXCOL col1, col2;

            size_t ulen = formatHexUnit(&data[i], ptr, 16);
            ptr += ulen;

            bool fMod = checkStyle(HVS_SHOWMODS) && (infobuf[i].buffer != m_pDataSeq->origfileid());
            getHighlightCol(offset + (size_w)i, 0, highlight, &col1, &col2,
                            fMod,
                            infobuf[i].userdata != 0,
                            fIncSelection);

            if (i < dataShift)
            {
                col1.colBG = col2.colBG = getHexColour(HVC_BACKGROUND);
                memset(ptr-ulen, ' ', ulen);
            }

            bool newGroup = ((i + 1) % (size_t)m_nBytesPerColumn) == 0;
            bool lastByte = (i >= length - 1);
            bool lastInLine = (i == (size_t)(m_nBytesPerLine - 1));

            if (!hexcolEq(col1, col2)|| i == m_nBytesPerLine - 1 || (i+1) % (m_nBytesPerColumn) != 0){// || lastInLine || !newGroup) {
                addAttr(&aptr, col1.colFG, col1.colBG, ulen);
                //if (newGroup && !lastByte) {
                if((i+1) % (m_nBytesPerColumn) == 0 && (i < length/*m_nBytesPerLine*/ - 1)) {
                    *ptr++ = ' ';
                    addAttr(&aptr, col2.colFG, col2.colBG, 1);
                }
            } else if(i < length - 1){//else if (!lastByte) {
                *ptr++ = ' ';
                addAttr(&aptr, col1.colFG, col1.colBG, ulen + 1);
            } else {
                addAttr(&aptr, col1.colFG, col1.colBG, ulen);
            }
        }

        // Pad dead space on partial last line.
        // addAttr MUST be called for every character written to buf — the
        // rendering loop uses the character index as the attrList index, so
        // a missing addAttr here shifts every subsequent ASCII-column entry
        // by `len` positions, causing those chars to read uninitialised attrs.
        if (i != (size_t)m_nBytesPerLine) {
            int len = m_nHexWidth - (int)(ptr - (szBuf + (m_nAddressWidth + m_nHexPaddingLeft)));
            {//if (len > 0) {
                for (int j = 0; j < len; j++)
                    *ptr++ = ' ';
                addAttr(&aptr,
                        getHexColour(HVC_ADDRESS),
                        getHexColour(HVC_BACKGROUND),
                        (size_t)len);
            }
        }
    }

    // ── ASCII column ──────────────────────────────────────────────────────────
    if (!checkStyle(HVS_ASCII_INVISIBLE)) {

        for (i = 0; i < (size_t)m_nHexPaddingRight; i++)
            *ptr++ = ' ';
        addAttr(&aptr,
                getHexColour(HVC_ASCII),
                getHexColour(HVC_BACKGROUND),
                (size_t)m_nHexPaddingRight);

        for (i = 0; i < length; i++) {
            HEXCOL col1, col2;
            uint8_t v = data[i];

            if (!checkStyle(HVS_ASCII_SHOWCTRLS) && v < 32)
                *ptr++ = '.';
            else if (!checkStyle(HVS_ASCII_SHOWEXTD) && v >= 0x80 && v <= 0xa0)
                *ptr++ = '.';
            else
                *ptr++ = (char)v;

            bool fMod = checkStyle(HVS_SHOWMODS) && (infobuf[i].buffer != m_pDataSeq->origfileid());
            getHighlightCol(offset + (size_w)i, 1, highlight, &col1, &col2,
                            fMod,
                            infobuf[i].userdata != 0,
                            fIncSelection);
            if(i < dataShift)
            {
                col1.colBG = col2.colBG = getHexColour(HVC_BACKGROUND);
                *(ptr-1) = ' ';
            }
            addAttr(&aptr, col1.colFG, col1.colBG, 1);
        }

        // Pad to full line width
        if (i != (size_t)m_nBytesPerLine) {
            size_t pad = (size_t)m_nBytesPerLine - i;
            for (size_t j = 0; j < pad; j++) *ptr++ = ' ';
            addAttr(&aptr,
                    getHexColour(HVC_ASCII),
                    getHexColour(HVC_BACKGROUND),
                    pad);
        }

        *ptr = '\0';
    }

    return (size_t)(ptr - szBuf);
}

// ── identifySearchPatterns ────────────────────────────────────────────────────

void HexView::identifySearchPatterns(uint8_t *data, size_t len, seqchar_info *infobuf)
{
    if (m_nSearchLen == 0) return;

    uint8_t *ptr = data;
    while ((ptr = (uint8_t *)memchr(ptr, m_pSearchPat[0], len - (size_t)(ptr - data))) != nullptr) {
        size_t slen = std::min((size_t)m_nSearchLen, len - (size_t)(ptr - data));
        size_t i;
        for (i = 1; i < slen; i++) {
            if (ptr[i] != m_pSearchPat[i]) break;
        }
        if (i == m_nSearchLen) {
            for (i = 0; i < m_nSearchLen; i++)
                infobuf[i + (size_t)(ptr - data)].userdata = 1;
            ptr += m_nSearchLen;
        } else {
            ptr += 1;
        }
    }
}

// ── drawVLine ─────────────────────────────────────────────────────────────────

void HexView::drawVLine(QPainter &painter, const QRect &paintRect, QRgb col, int pos)
{
    QRect rc = paintRect;
    rc.setLeft(pos);
    rc.setRight(pos);// + 1);
    //painter.drawLine(pos, rc.top(), pos, rc.bottom());
    painter.fillRect(rc, QColor(col));
}

void HexView::drawTextFixed(QPainter &p, QPoint origin,
                   const QString &text, const QRawFont &rawFont, int cellWidth)
{
    const QVector<quint32> glyphs = rawFont.glyphIndexesForString(text);
    const int n = glyphs.size();

    QVector<QPointF> positions(n);
    for (int i = 0; i < n; ++i)
        positions[i] = QPointF(origin.x() + i * cellWidth, origin.y());

    QGlyphRun run;
    run.setRawFont(rawFont);
    run.setGlyphIndexes(glyphs);
    run.setPositions(positions);
    p.drawGlyphRun(QPointF(0, 0), run);
}

// ── paintLine ─────────────────────────────────────────────────────────────────
//
//  Renders one line of data onto `painter`.  `nLineNo` is the absolute
//  line number (0-based from file start); `data`/`datalen`/`infobuf` are
//  already the correct bytes for this line (supplied by paintEvent).
//  Returns the pixel x-coordinate after the last rendered character.

int HexView::paintLine(QPainter &painter, size_w nLineNo,
                        uint8_t *data, size_t datalen, seqchar_info *infobuf,
                       size_t datashift = 0)
{
    const int y      = (int)(nLineNo - m_nVScrollPos) * m_nFontHeight;
    const int xStart = -m_nHScrollPos * m_nFontWidth;
    const QRgb bgCol = getHexColour(HVC_BACKGROUND);

    // Past end-of-file — paint blank background
    if (datalen == 0) {
        painter.fillRect(xStart, y, viewport()->width() - xStart, m_nFontHeight, QColor(bgCol));
        return xStart;
    }

    // File offset of the first byte on this display line
    size_w dispOffset = (size_w)(nLineNo * (size_w)m_nBytesPerLine);
    /*if (m_nDataShift > 0) {
        dispOffset = dispOffset >= (size_w)m_nDataShift
                     ? dispOffset - (size_w)m_nDataShift
                     : 0;
    }*/

    // Build text + attribute buffers
    const int bufSize = m_nTotalWidth + 200;
    char *buf      = new char[bufSize];
    ATTR *attrList = new ATTR[bufSize];

    dispOffset -= m_nDataShift;
    size_t len = formatLine(data, datalen, dispOffset, datashift,
                             buf, (size_t)bufSize, attrList, infobuf, true);

    // Render colour spans.
    //
    // Use a floating-point x accumulator so that span-boundary positions
    // exactly match Qt's internal text layout.  Integer-only tracking with
    // spanLen * m_nFontWidth can differ from the true per-character advance
    // (which may be fractional) and the error grows linearly with the
    // character column — causing highlighted regions to be visibly shifted
    // relative to the text they cover by the time the selection is reached.
    const QFontMetrics fm(m_font);
    //const QFontMetricsF fmf(m_font);
    const int ascent = fm.ascent();

    //qreal xf = (qreal)xStart;
    int x = xStart;

    for (size_t i = 0, lasti = 0; i <= len; i++) {
        if (i == len ||
            attrList[i].colFG != attrList[lasti].colFG ||
            attrList[i].colBG != attrList[lasti].colBG)
        {
            const int spanLen = (int)(i - lasti);
            if (spanLen > 0) {
                const QString spanStr  = QString::fromLatin1(buf + lasti, spanLen);
                /*const qreal spanWidthF = fmf.horizontalAdvance(spanStr);
                const int   xi         = qRound(xf);
                const int   spanWidth  = qRound(xf + spanWidthF) - xi;*/
                int spanWidth = spanLen * m_nFontWidth;

                painter.fillRect(x, y, spanWidth, m_nFontHeight,
                                 QColor(attrList[lasti].colBG));
                painter.setPen(QColor(attrList[lasti].colFG));
                //painter.drawText(x, y + ascent, spanStr);
                drawTextFixed(painter, QPoint(x,y+ascent), spanStr, m_rawFont, m_nFontWidth);
                //xf += spanWidthF;
                x += spanWidth;
            }
            lasti = i;
        }
    }
   // int x = qRound(xf);

    // Fill the gap between the last span and the end of the text layout area.
    // logToPhyXCoord already returns pixels and already accounts for m_nHScrollPos.
    int lineEndX = logToPhyXCoord(m_nBytesPerLine, 1);
    if (x < lineEndX)
        painter.fillRect(x, y, lineEndX - x, m_nFontHeight, QColor(bgCol));

    delete[] buf;
    delete[] attrList;

    return x;
}

// ── paintEvent ────────────────────────────────────────────────────────────────

void HexView::paintEvent(QPaintEvent *event)
{
    if (!m_pDataSeq || m_nFontHeight == 0) return;

    QPainter painter(viewport());//this);
    painter.setFont(m_font);

    const QRect &pr = event->rect();

    size_w first = m_nVScrollPos + (size_w)(pr.top()    / m_nFontHeight);
    size_w last  = m_nVScrollPos + (size_w)(pr.bottom() / m_nFontHeight);
    if (pr.bottom() % m_nFontHeight) ++last;
    if(last < first) last = -1;



    // ── Bulk data render ──────────────────────────────────────────────────────
    // Allocate enough for all visible lines plus shift
    size_w  startFileOff = first * (size_w)m_nBytesPerLine;
    size_t  allocLen     = (size_t)(last - first + 1) * (size_t)m_nBytesPerLine
                           + (size_t)std::max(m_nDataShift, 0);

    uint8_t      *bigbuf  = new uint8_t[allocLen];
    seqchar_info *bufinfo = new seqchar_info[allocLen];
    memset(bufinfo, 0, allocLen * sizeof(seqchar_info));

    int shift  = 0;    // leading pad bytes inserted before the file data
    int shift2 = 0;    // how far back we start reading from the file

    if (m_nDataShift > 0 && (size_w)m_nDataShift > startFileOff) {
        shift = m_nDataShift;
        memset(bigbuf, 0, (size_t)shift);
    } else {
        shift2 = m_nDataShift; // may be 0 (normal case)
    }

    size_t buflen = m_pDataSeq->render(
                        startFileOff - (size_w)shift2,
                        bigbuf  + shift,
                        allocLen - (size_t)shift,
                        bufinfo + shift);
    buflen += (size_t)shift;

    identifySearchPatterns(bigbuf, buflen, bufinfo);

    // ── Fill right margin with background ─────────────────────────────────────
    {
        int marginX = logToPhyXCoord(m_nBytesPerLine, 1);
        if (marginX < viewport()->width())
            painter.fillRect(marginX, pr.top(),
                             viewport()->width() - marginX, pr.height(),
                             QColor(getHexColour(HVC_BACKGROUND)));
    }

    // ── Draw line by line ─────────────────────────────────────────────────────
    for (size_w i = first; i <= last; i++) {
        size_w lineDataOff = (i - first) * (size_w)m_nBytesPerLine;
        size_t len = (lineDataOff < (size_w)buflen)
                     ? (size_t)std::min((size_w)buflen - lineDataOff,
                                        (size_w)m_nBytesPerLine)
                     : 0;

        size_t datashift = (i == 0) ? m_nDataShift : 0;

        int nx = paintLine(painter, i,
                           bigbuf  + lineDataOff,
                           len,
                           bufinfo + lineDataOff,
                           datashift);

        // Bookmark note strips (stub — implemented in Stage 5)
        for (BOOKNODE *bnp = m_BookHead->next; bnp != m_BookTail; bnp = bnp->next) {
            const BOOKMARK &bm = bnp->bookmark;
            if ((bm.pszText || bm.pszTitle) &&
                bm.offset >= i  * (size_w)m_nBytesPerLine &&
                bm.offset <  (i + 1) * (size_w)m_nBytesPerLine &&
                bm.offset <  m_pDataSeq->size())
            {
                int ny = (int)(i - m_nVScrollPos) * m_nFontHeight;
                drawNoteStrip(painter, nx + 30, ny, bnp);
            }
        }
    }

    // ── Resize bar ────────────────────────────────────────────────────────────
    if (checkStyle(HVS_RESIZEBAR)) {
        int pos1 = (m_nAddressWidth - m_nHScrollPos) * m_nFontWidth
                   + (m_nHexPaddingLeft * m_nFontWidth) / 2
            ;//+ m_nFontWidth / 2;
        drawVLine(painter, pr, getHexColour(HVC_RESIZEBAR), pos1);
        drawVLine(painter, pr, getHexColour(HVC_RESIZEBAR), m_nResizeBarPos);
    }

    paintCaret(painter);

    delete[] bigbuf;
    delete[] bufinfo;
}

// ── paintCaret ────────────────────────────────────────────────────────────────
//
//  Inverts the pixels under the caret, mimicking the Win32 system caret.
//  Must be called after all line content is rendered so the XOR operates
//  on the final pixel values.  RasterOp_SourceXorDestination is bitwise
//  XOR — drawing white (0xFFFFFF) inverts every channel, exactly like
//  Win32's PATINVERT caret drawing.

void HexView::paintCaret(QPainter &painter)
{
    if (!m_caretVisible || !hasFocus()) return;

    painter.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
    painter.fillRect(m_nCaretX, m_nCaretY, 2, m_nFontHeight, Qt::white);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
}
