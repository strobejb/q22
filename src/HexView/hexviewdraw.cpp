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
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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

// WCAG relative luminance. QColor::lightness() is HSL-based, which treats
// bright saturated colours like yellow as mid-light and can pick white text.
static double srgbChannelToLinear(int channel)
{
    const double c = channel / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

static double relativeLuminance(const QColor &colour)
{
    return 0.2126 * srgbChannelToLinear(colour.red())
         + 0.7152 * srgbChannelToLinear(colour.green())
         + 0.0722 * srgbChannelToLinear(colour.blue());
}

static double contrastRatio(const QColor &a, const QColor &b)
{
    const double l1 = relativeLuminance(a);
    const double l2 = relativeLuminance(b);
    const double lighter = std::max(l1, l2);
    const double darker  = std::min(l1, l2);
    return (lighter + 0.05) / (darker + 0.05);
}

static QColor contrastColourFor(const QColor &background,
                                const QColor &candidateA,
                                const QColor &candidateB)
{
    return contrastRatio(background, candidateA) >= contrastRatio(background, candidateB)
        ? candidateA
        : candidateB;
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
//  Maps a colour slot to a concrete QColor.
//
//  Phase 1 — focus redirection: when the widget is unfocused, selection-related
//  slots silently redirect to their inactive counterparts so the draw code never
//  needs to know about focus state.
//
//  Phase 2 — custom colour: if the slot has a user-set QColor, return it.
//
//  Phase 3 — HexView default: map the slot to the appropriate QPalette role
//  or fixed semantic default.
//  Chained slots (HVC_HEXODDSEL etc.) call realiseColour(HVC_SELTEXT) recursively;
//  the redirect in phase 1 ensures this cannot loop.

QColor HexView::realiseColour(HvColorSlot slot) const
{
    // Phase 1: redirect selection slots to inactive variants when unfocused
    if (!hasAppFocus()) {//hasFocus()) {
        switch (slot) {
        case HVC_SELECTION:
        case HVC_MATCHEDSEL:
            slot = HVC_SELECTION_INACTIVE; break;
        case HVC_SELTEXT:
        case HVC_HEXODDSEL:
        case HVC_HEXEVENSEL:
        case HVC_ASCIISEL:
            slot = HVC_SELTEXT_INACTIVE;   break;
        default: break;
        }
    }

    // Phase 2: user-set custom colour
    const QColor &c = m_ColourList[slot];
    if (c.isValid()) return c;

    // Phase 3: HexView defaults
    const QPalette &pal = palette();
    switch (slot) {
    case HVC_HEXODDSEL:
    case HVC_HEXEVENSEL:
    case HVC_ASCIISEL:           return realiseColour(HVC_SELTEXT);  // chain; only reached when focused
    default:
        return defaultColourForSlot(slot, pal);
    }
}

QColor HexView::defaultColourForSlot(HvColorSlot slot, const QPalette &pal)
{
    switch (slot) {
    case HVC_BACKGROUND:         return pal.color(QPalette::Base);
    case HVC_SELECTION:          return pal.color(QPalette::Active,   QPalette::Highlight);
    case HVC_SELECTION_INACTIVE: return pal.color(QPalette::Inactive,   QPalette::Highlight);/*{
        const QColor bg = defaultColourForSlot(HVC_BACKGROUND, pal);
        return bg.lightness() >= 128 ? bg.darker(300) : bg.lighter(300);
    }*/
    case HVC_SELTEXT:            return pal.color(QPalette::Active,   QPalette::HighlightedText);
    case HVC_SELTEXT_INACTIVE:  return pal.color(QPalette::Inactive,   QPalette::HighlightedText);/*{
        const QColor bg = defaultColourForSlot(HVC_SELECTION_INACTIVE, pal);
        return bg.lightness() >= 128 ? Qt::black : Qt::white;
    }*/
    case HVC_HEXODDSEL:
    case HVC_HEXEVENSEL:
    case HVC_ASCIISEL:           return defaultColourForSlot(HVC_SELTEXT, pal);
    case HVC_ADDRESS:
    case HVC_HEXODD:
    case HVC_HEXEVEN:
    case HVC_ASCII:              return pal.color(QPalette::WindowText);
    case HVC_MODIFY:             return QColor(200, 50, 50);
    case HVC_MODIFYSEL:          return QColor(255, 128, 128);
    case HVC_RESIZEBAR:          return pal.color(QPalette::Mid);
    case HVC_MATCHED:            return pal.color(QPalette::Highlight);//QColor(255, 165, 0);
    case HVC_MATCHEDSEL:         return pal.color(QPalette::Highlight);
    case HVC_HIGHLIGHT:          return pal.color(QPalette::Highlight);
    case HVC_BOOKMARK1:          return QColor(255, 255,   0);
    case HVC_BOOKMARK2:          return QColor(255, 165,   0);
    case HVC_BOOKMARK3:          return QColor(255,  80,  80);
    case HVC_BOOKMARK4:          return QColor(180, 100, 220);
    case HVC_BOOKMARK5:          return QColor( 80, 200, 120);
    case HVC_BOOKMARK6:          return QColor( 80, 160, 255);
    case HVC_BOOKMARK7:          return QColor(255, 150, 200);
    case HVC_BOOKSEL:            return Qt::black;
    case HVC_BOOKMARK1_FG:
    case HVC_BOOKMARK2_FG:
    case HVC_BOOKMARK3_FG:
    case HVC_BOOKMARK4_FG:
    case HVC_BOOKMARK5_FG:
    case HVC_BOOKMARK6_FG:
    case HVC_BOOKMARK7_FG:       return pal.color(QPalette::Text);
    case HVC_MAX_COLOURS:        break;
    }
    return pal.color(QPalette::WindowText);
}

QColor HexView::contrastColourFor(const QColor &background,
                                  const QColor &candidateA,
                                  const QColor &candidateB) const
{
    return ::contrastColourFor(background, candidateA, candidateB);
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


QColor blendColor(const QColor &i_color1, const QColor &i_color2, double i_alpha)
{
    return QColor(
        qRound(qreal(i_color1.red())*(1.0-i_alpha) + qreal(i_color2.red())*i_alpha),
        qRound(qreal(i_color1.green())*(1.0-i_alpha) + qreal(i_color2.green())*i_alpha),
        qRound(qreal(i_color1.blue())*(1.0-i_alpha) + qreal(i_color2.blue())*i_alpha),
        qRound(qreal(i_color1.alpha())*(1.0-i_alpha) + qreal(i_color2.alpha())*i_alpha)
        );
}

bool HexView::hasAppFocus() const
{
#ifdef Q_OS_WIN
    QWidget *topLevel = window();
    if (!topLevel)
        return false;

    const HWND appWindow = reinterpret_cast<HWND>(topLevel->winId());
    if (!appWindow || !IsWindowEnabled(appWindow))
        return false;

    const HWND foregroundWindow = GetForegroundWindow();
    if (!foregroundWindow)
        return false;

    return GetAncestor(foregroundWindow, GA_ROOTOWNER) ==
           GetAncestor(appWindow, GA_ROOTOWNER);
#else
    QWidget *topLevel = window();
    return topLevel &&
           topLevel->isEnabled() &&
           QGuiApplication::applicationState() == Qt::ApplicationActive;
#endif
#if 0
    QWidget *topLevel = window();
    QWindow *win = topLevel ? topLevel->windowHandle() : nullptr;

    return topLevel &&
           topLevel->isEnabled() &&
           QGuiApplication::applicationState() == Qt::ApplicationActive &&
           QApplication::activeWindow() == topLevel &&
           win &&
           win->isActive();
#endif
}

// ── getHighlightCol ───────────────────────────────────────────────────────────
//
//  Determines the fg/bg colours for a single byte at `offset` in `pane`
//  (0=hex, 1=ascii).  col1 is the colour of the byte itself; col2 is the
//  colour of the inter-byte space that follows (may differ at a group
//  boundary or selection edge).

bool HexView::getHighlightCol(size_w offset, int pane,
                               const QList<Bookmark> &highlights,
                               HEXCOL *col1, HEXCOL *col2)
{
    const QRgb selFG   = getHexColour(HVC_SELTEXT);
    const QRgb selBG   = getHexColour(HVC_SELECTION);
    const QRgb matchFG = getHexColour(HVC_BACKGROUND);
    const QRgb matchBG = getHexColour(HVC_MATCHEDSEL);
    const QRgb defFG   = getHexColour(pane == 0
        ? ((((offset + m_nDataShift) % m_nBytesPerLine) / m_nBytesPerColumn) & 1
           ? HVC_HEXEVEN : HVC_HEXODD)
        : HVC_ASCII);
    const QRgb defBG   = getHexColour(HVC_BACKGROUND);
    const bool selWins = checkStyle(HVS_SELECTION_OVERRIDES);

    // Scan `highlights` for position `pos`.
    //   gap==true  : only entries already active at pos-1 (prevents start-of-range bleed).
    //   gap==false : normal per-byte scan.
    // Returns the resolved HEXCOL:
    //   - shortest non-selection entry that covers pos supplies the highlight colour.
    //     "Shortest wins" mirrors the note-strip logic and gives nested bookmarks the
    //     correct visual layering: a small inner bookmark renders on top of a large
    //     outer one rather than being shadowed by it.
    //     bgColour==0 means FG-only (modified): contributes fgColour but not bgColour.
    //   - if selection also covers pos, the two-mode logic is applied.
    auto resolve = [&](size_w pos, bool gap) -> HEXCOL {
        const Bookmark *hl = nullptr;
        bool inSel = false;
        bool inMod = false;
        for (const Bookmark &bm : highlights) {
            if (gap ? (bm.offset >= pos) : (pos < bm.offset)) continue;
            if (pos >= bm.offset + bm.length)                  continue;
            if      (bm.colourIndex == -2)                    inSel = true;
            else if (bm.colourIndex < 0 && bm.bgColour == 0)  inMod = true;   // FG-only (modified)
            else if (!hl || bm.length < hl->length)           hl = &bm;       // shortest wins
        }

        HEXCOL c;
        if ((hl || inMod) && inSel) {
            c = selWins ? HEXCOL{selFG, selBG} : HEXCOL{matchFG, matchBG};
        } else if (hl) {
            const QRgb hlBG = hl->colourIndex >= 0
                ? getHexColour(HvColorSlot(HVC_BOOKMARK1 + hl->colourIndex))
                : hl->bgColour;
            QRgb hlFG;
            if (hl->colourIndex >= 0) {
                hlFG = realiseColour(HvColorSlot(HVC_BOOKMARK1_FG + hl->colourIndex)).rgb();
            } else if (hl->fgColour) {
                hlFG = hl->fgColour;
            } else {
                // Custom bgColour — not palette-indexed; apply the same pole logic inline.
                const QColor bg  = realiseColour(HVC_BACKGROUND);
                const QColor asc = realiseColour(HVC_ASCII);
                hlFG = contrastColourFor(QColor(hlBG), bg, asc).rgb();
            }
            c = { hlFG, hlBG };
        } else if (inSel) {
            c = {selFG, selBG};
        } else {
            c = {defFG, defBG};
        }

        // Modified FG wins over any highlight FG when not selected.
        if (inMod && !inSel)
            c.colFG = getHexColour(HVC_MODIFY);

        return c;
    };

    *col1 = resolve(offset,     false);
    *col2 = resolve(offset + 1, true);
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
                            const QList<Bookmark> &matchHighlights,
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

    // Build per-line highlight list in priority order:
    //   1. matched search ranges (highest)
    //   2. bookmarks
    //   3. modified byte ranges
    //   4. selection (lowest — any highlight above wins)
    QList<Bookmark> lineHighlights;
    const size_w lineEnd = offset + (size_w)length;
    // When dataShift > 0 on the first display line, 'offset' has wrapped to a
    // large unsigned value (0 - dataShift).  offset + dataShift wraps back to
    // the correct file address of the first real byte, so use that for overlap
    // comparisons instead of bare 'offset' to avoid spuriously failing checks.
    const size_w realLineStart = offset + (size_w)dataShift;
    for (const Bookmark &bm : matchHighlights)
        if (bm.offset + bm.length > realLineStart && bm.offset < lineEnd)
            lineHighlights.append(bm);
    for (const Bookmark &bm : m_bookmarks)
        if (bm.offset + bm.length > realLineStart && bm.offset < lineEnd)
            lineHighlights.append(bm);
    if (checkStyle(HVS_SHOWMODS)) {
        size_t rangeStart = length;  // length == no open range
        for (size_t j = 0; j <= length; j++) {
            bool mod = j < length && infobuf[j].buffer != m_pDataSeq->origfileid();
            if (mod && rangeStart == length) {
                rangeStart = j;
            } else if (!mod && rangeStart != length) {
                Bookmark bm;
                bm.offset   = offset + (size_w)rangeStart;
                bm.length   = (size_w)(j - rangeStart);
                bm.fgColour = getHexColour(HVC_MODIFY);
                bm.bgColour = 0;  // sentinel: FG-only, BG shows through from lower priority
                lineHighlights.append(bm);
                rangeStart = length;
            }
        }
    }
    if (fIncSelection) {
        size_w selstart = std::min(m_nSelectionStart, m_nSelectionEnd);
        size_w selend   = std::max(m_nSelectionStart, m_nSelectionEnd);
        if (selend > selstart && selend > realLineStart && selstart < lineEnd) {
            Bookmark sel;
            sel.offset   = selstart;
            sel.length   = selend - selstart;
            sel.colourIndex = -2;
            lineHighlights.append(sel);
        }
    }

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

            getHighlightCol(offset + (size_w)i, 0, lineHighlights, &col1, &col2);

            if (i < dataShift)
            {
                col1.colBG = col2.colBG = getHexColour(HVC_BACKGROUND);
                memset(ptr-ulen, ' ', ulen);
            }

            bool newGroup = ((i + 1) % (size_t)m_nBytesPerColumn) == 0;
            bool lastByte = (i >= length - 1);
            bool lastInLine = (i == (size_t)(m_nBytesPerLine - 1));

            if (!hexcolEq(col1, col2)|| i == m_nBytesPerLine - 1 || (i+1) % (m_nBytesPerColumn) != 0){
                addAttr(&aptr, col1.colFG, col1.colBG, ulen);
                if((i+1) % (m_nBytesPerColumn) == 0 && (i < length - 1)) {
                    *ptr++ = ' ';
                    addAttr(&aptr, col2.colFG, col2.colBG, 1);
                }
            } else if(i < length - 1){
                *ptr++ = ' ';
                addAttr(&aptr, col1.colFG, col1.colBG, ulen + 1);
            } else {
                addAttr(&aptr, col1.colFG, col1.colBG, ulen);
            }
        }

        // Pad dead space on partial last line.
        // addAttr MUST be called for every character written to buf -- the
        // rendering loop uses the character index as the attrList index, so
        // a missing addAttr here shifts every subsequent ASCII-column entry
        // by `len` positions, causing those chars to read uninitialised attrs.
        if (i != (size_t)m_nBytesPerLine) {
            int len = m_nHexWidth - (int)(ptr - (szBuf + (m_nAddressWidth + m_nHexPaddingLeft)));
            {
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

            getHighlightCol(offset + (size_w)i, 1, lineHighlights, &col1, &col2);
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

QList<Bookmark> HexView::identifySearchPatterns(const uint8_t *data, size_t len, size_w bufBaseOffset)
{
    QList<Bookmark> results;
    if (m_nSearchLen == 0) return results;

    const QRgb matchFG = getHexColour(HVC_BACKGROUND);
    const QRgb matchBG = getHexColour(HVC_MATCHED);

    const uint8_t *ptr = data;
    while ((ptr = (const uint8_t *)memchr(ptr, m_pSearchPat[0], len - (size_t)(ptr - data))) != nullptr) {
        size_t slen = std::min((size_t)m_nSearchLen, len - (size_t)(ptr - data));
        size_t i;
        for (i = 1; i < slen; i++) {
            if (ptr[i] != m_pSearchPat[i]) break;
        }
        if (i == m_nSearchLen) {
            Bookmark bm;
            bm.offset   = bufBaseOffset + (size_w)(ptr - data);
            bm.length   = (size_w)m_nSearchLen;
            bm.fgColour = matchFG;
            bm.bgColour = matchBG;
            results.append(bm);
            ptr += m_nSearchLen;
        } else {
            ptr += 1;
        }
    }
    return results;
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
                        size_t datashift, const QList<Bookmark> &matchHighlights)
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

    // Build text + attribute buffers
    const int bufSize = m_nTotalWidth + 200;
    char *buf      = new char[bufSize];
    ATTR *attrList = new ATTR[bufSize];

    dispOffset -= m_nDataShift;
    size_t len = formatLine(data, datalen, dispOffset, datashift,
                             buf, (size_t)bufSize, attrList, infobuf, matchHighlights, true);

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

    size_w bufBaseOffset = startFileOff - (size_w)shift2;
    QList<Bookmark> matchHighlights = identifySearchPatterns(bigbuf + shift, buflen - shift, bufBaseOffset);

    // ── Fill right margin with background ─────────────────────────────────────
    {
        int marginX = logToPhyXCoord(m_nBytesPerLine, 1);
        if (marginX < viewport()->width())
            painter.fillRect(marginX, pr.top(),
                             viewport()->width() - marginX, pr.height(),
                             QColor(getHexColour(HVC_BACKGROUND)));
    }

    // ── Draw line by line ─────────────────────────────────────────────────────
    const int asciiRight = logToPhyXCoord(m_nBytesPerLine, 1);
    // Compute conflict layout once for all bookmarks before the draw loop.
    const QVector<BmLayout> bmLayout = computeBookmarkLayout();
    for (size_w i = first; i <= last; i++) {
        size_w lineDataOff = (i - first) * (size_w)m_nBytesPerLine;
        size_t len = (lineDataOff < (size_w)buflen)
                     ? (size_t)std::min((size_w)buflen - lineDataOff,
                                        (size_w)m_nBytesPerLine)
                     : 0;

        size_t datashift = (i == 0) ? m_nDataShift : 0;

        paintLine(painter, i,
                  bigbuf  + lineDataOff,
                  len,
                  bufinfo + lineDataOff,
                  datashift,
                  matchHighlights);

        // Bookmark note strips — drawn to the right of the ASCII column.
        // Bookmarks are stored sorted by (offset ASC, length DESC), which already
        // handles the same-offset case.  For bookmarks with *different* offsets
        // that still land on the same display line we need an extra length-
        // descending pass so the strip with the larger span is always painted
        // first (i.e. underneath).  Collect matching bookmarks into a small
        // stack buffer, sort by length descending, then draw in that order.
        {
            const size_w lineStart = i * (size_w)m_nBytesPerLine;
            const size_w lineEnd   = lineStart + (size_w)m_nBytesPerLine;
            const size_w fileSize  = m_pDataSeq->size();

            // Stack-allocated for the typical case of 0-3 bookmarks per line.
            QVarLengthArray<const Bookmark *, 8> lineBms;
            for (const Bookmark &bm : m_bookmarks) {
                if (bm.offset >= lineStart && bm.offset < lineEnd && bm.offset < fileSize)
                    lineBms.append(&bm);
            }

            if (lineBms.size() > 1) {
                // Stable so bookmarks with identical length preserve their
                // storage (offset-ascending) order.
                std::stable_sort(lineBms.begin(), lineBms.end(),
                                 [](const Bookmark *a, const Bookmark *b) {
                                     return a->length > b->length;
                                 });
            }

            for (const Bookmark *bm : lineBms) {
                const int bmIdx = (int)(bm - m_bookmarks.constData());
                drawNoteStrip(painter, *bm, bmLayout.value(bmIdx));
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
    if (!m_caretVisible || !hasFocus() || QApplication::activeModalWidget()) return;

    painter.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
    painter.fillRect(m_nCaretX, m_nCaretY, 2, m_nFontHeight, Qt::white);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
}
