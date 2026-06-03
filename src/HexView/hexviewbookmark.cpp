#include "hexview.h"
#include "theme.h"

#include <algorithm>
#include <QApplication>
#include <QFontMetrics>
#include <QIcon>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextCursor>
#include <QAbstractTextDocumentLayout>
#include <QTextDocument>
#include <QTextOption>
#include <QVarLengthArray>
#include <QVector>

// Geometry constants — shared by noteStripGeom, drawNoteStrip, and the editor.
static constexpr int kNoteTriW    = 12;  // triangle depth (px)
static constexpr int kNotePadH    = 8;   // horizontal text padding (px)
static constexpr int kNotePadV    = 4;   // vertical padding (px)
static constexpr int kNoteRadius  = 6;   // rounded corner radius
static constexpr int kNoteMaxW    = 220; // max strip width (px)
static constexpr int kNoteBtnSz   = 16;  // button icon size (px)
static constexpr int kNoteBtnGap  = 4;   // gap between text and close button (px)
static constexpr int kNoteRangePad = 5;  // gap between note text and range label (px)
static constexpr int kBookmarkAreaBtnSz = 64;

static QTextDocument *makeNoteTextDoc(const QString &text, int width, const QFont &font)
{
    auto *doc = new QTextDocument;
    doc->setDefaultFont(font);
    doc->setDocumentMargin(0);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WordWrap);
    doc->setDefaultTextOption(opt);
    doc->setTextWidth(width);
    doc->setPlainText(text);
    return doc;
}

// Returns the UI font scaled to the hex-view font size so bookmark note strips
// grow and shrink consistently when the user changes the hex display font.
QFont HexView::noteFont() const
{
    QFont f = QApplication::font();
    const int px = m_font.pixelSize();
    if (px > 0) {
        f.setPixelSize(px);
    } else {
        const int pt = m_font.pointSize();
        if (pt > 0) f.setPointSize(pt);
    }
    return f;
}

// Returns the extra logical columns (character widths) that must be added to
// m_nTotalWidth so the horizontal scrollbar gives enough range to scroll the
// note strips fully into view.  Returns 0 when there are no bookmarks.
//
// The seam for a future "bookmark resize bar": kNoteMaxW drives the allocated
// width.  When that bar is implemented, replace kNoteMaxW with a member
// variable (e.g. m_nNoteMaxW) and call setupScrollbars() after a drag.
int HexView::noteStripExtraColumns() const
{
    if (m_bookmarks.isEmpty() || m_nFontWidth <= 0) return 0;
    // Gap (6 em-widths) + triangle pointer + maximum strip width, rounded up.
    const int extraPx = m_nFontWidth * 6 + kNoteTriW + kNoteMaxW;
    return (extraPx + m_nFontWidth - 1) / m_nFontWidth;
}

// ── Bookmark management ───────────────────────────────────────────────────────

// Comparison used to keep m_bookmarks sorted at all times.
// Primary key: offset ascending — bookmarks appear top-to-bottom on screen.
// Secondary key: length descending — when two bookmarks share the same starting
// offset the longer one sorts first and is therefore drawn first (underneath).
// The draw loop applies its own per-line length-descending pass to handle the
// case where bookmarks with *different* offsets land on the same display line.
static bool bmOffsetLess(const Bookmark &a, const Bookmark &b)
{
    if (a.offset != b.offset) return a.offset < b.offset;
    return a.length > b.length;   // longer span first → drawn underneath
}

static void clearBookmarkActive(QList<Bookmark> &bookmarks)
{
    for (Bookmark &bm : bookmarks)
        bm._active = false;
}

void HexView::setBookmarks(const QList<Bookmark> &bookmarks)
{
    closeNoteEditor(false);
    m_expandedBookmarkIdx   = -1;   // full replacement — old pin index is meaningless
    m_surfacedBookmarkIdx = -1;
    m_bookmarks = bookmarks;
    clearBookmarkActive(m_bookmarks);
    std::sort(m_bookmarks.begin(), m_bookmarks.end(), bmOffsetLess);
    setupScrollbars();
    viewport()->update();
    emit bookmarksChanged();
}

void HexView::addBookmark(const Bookmark &bm)
{
    // Sorted insert so the list stays in offset order without a full sort pass.
    const auto pos = std::lower_bound(m_bookmarks.begin(), m_bookmarks.end(),
                                      bm, bmOffsetLess);
    const int insertIdx = (int)(pos - m_bookmarks.begin());
    m_bookmarks.insert(pos, bm);

    // Adjust pin-state indices for the insertion, exactly as removeBookmark
    // adjusts them for deletion.  Without this a pin at or after the insertion
    // point silently migrates to a different bookmark.
    if (m_expandedBookmarkIdx >= insertIdx)
        ++m_expandedBookmarkIdx;
    if (m_surfacedBookmarkIdx >= insertIdx)
        ++m_surfacedBookmarkIdx;
    clearBookmarkActive(m_bookmarks);

    // If the new bookmark's byte range overlaps the currently-pinned bookmark's
    // range, the two have just formed a conflict group.  A pin acquired while
    // the bookmark was lone (via cursor expansion, explicit click, or the note
    // editor) now locks it as the permanent group winner and hides the newly-
    // added bookmark with no way for the user to reach it.  Reset both indices
    // so the group starts in the neutral (all-collapsed) state.
    if (m_expandedBookmarkIdx >= 0 && m_expandedBookmarkIdx < m_bookmarks.size()
            && m_expandedBookmarkIdx != insertIdx) {
        const Bookmark &pinned = m_bookmarks[m_expandedBookmarkIdx];
        const Bookmark &added  = m_bookmarks[insertIdx];
        if (added.offset  < pinned.offset + pinned.length &&
            added.offset  + added.length  > pinned.offset) {
            m_expandedBookmarkIdx   = -1;
            m_surfacedBookmarkIdx = -1;
            clearBookmarkActive(m_bookmarks);
        }
    }

    setupScrollbars();
    viewport()->update();
    emit bookmarksChanged();
}

void HexView::removeBookmark(int idx)
{
    if (idx < 0 || idx >= m_bookmarks.size()) return;
    closeNoteEditor(false);
    if (m_expandedBookmarkIdx == idx)
        m_expandedBookmarkIdx = -1;
    else if (m_expandedBookmarkIdx > idx)
        --m_expandedBookmarkIdx;
    if (m_surfacedBookmarkIdx == idx)
        m_surfacedBookmarkIdx = -1;
    else if (m_surfacedBookmarkIdx > idx)
        --m_surfacedBookmarkIdx;
    m_bookmarks.removeAt(idx);
    clearBookmarkActive(m_bookmarks);
    setupScrollbars();
    viewport()->update();
    emit bookmarksChanged();
}

void HexView::replaceBookmark(int idx, const Bookmark &bm)
{
    if (idx < 0 || idx >= m_bookmarks.size()) return;
    closeNoteEditor(false);  // sets m_noteEditorIdx = -1 before we re-sort
    m_bookmarks[idx] = bm;
    // Re-sort in case the replacement bookmark has a different offset.
    // This invalidates all index-based pin state, so clear both pins.
    m_expandedBookmarkIdx   = -1;
    m_surfacedBookmarkIdx = -1;
    std::sort(m_bookmarks.begin(), m_bookmarks.end(), bmOffsetLess);
    clearBookmarkActive(m_bookmarks);
    setupScrollbars();
    viewport()->update();
    emit bookmarksChanged();
}

int HexView::findBookmark(size_w startoff, size_w endoff) const
{
    for (int i = 0; i < m_bookmarks.size(); ++i) {
        const Bookmark &bm = m_bookmarks[i];
        if (bm.offset + bm.length > startoff && bm.offset < endoff)
            return i;
    }
    return -1;
}

void HexView::setBookmarkButtonLayout(const BookmarkButtonLayout &layout)
{
    m_bookmarkButtonLayout = layout;
    m_hoverOnClose = false;
    m_hoverOnEdit = false;
    m_pressedOnClose = false;
    m_pressedOnEdit = false;
    viewport()->update();
}

QRect HexView::bookmarkButtonRect(const NoteStripGeom &geom, BookmarkButtonAction action) const
{
    if (action == BookmarkButtonAction::None)
        return QRect();
    if (m_bookmarkButtonLayout.topRight == action)
        return geom.topButtonRect;
    if (m_bookmarkButtonLayout.bottomRight == action)
        return geom.bottomButtonRect;
    return QRect();
}

QRect HexView::bookmarkAreaButtonRect() const
{
    if (m_bookmarks.isEmpty() || !m_pDataSeq || m_nBytesPerLine <= 0 ||
            m_nFontWidth <= 0 || m_nFontHeight <= 0)
        return QRect();

    const int viewW = viewport()->width();
    const int viewH = viewport()->height();
    if (viewW < kBookmarkAreaBtnSz || viewH < kBookmarkAreaBtnSz)
        return QRect();

    const int asciiRight = logToPhyXCoord(m_nBytesPerLine, 1);
    const int minX = asciiRight + qMax(6, m_nFontWidth);
    if (minX + kBookmarkAreaBtnSz > viewW)
        return QRect();

    constexpr int kMargin = 12;
    constexpr int kBottomMargin = 20;
    const int noteLeft = asciiRight + m_nFontWidth * 4;
    const int noteRight = noteLeft + kNoteTriW + kNoteMaxW;
    const int preferredX = noteLeft + (noteRight - noteLeft - kBookmarkAreaBtnSz) / 2;
    const int maxX = viewW - kBookmarkAreaBtnSz - kMargin;
    const int x = qBound(minX, preferredX, maxX);
    const int y = qMax(0, viewH - kBookmarkAreaBtnSz - kBottomMargin);
    return QRect(x, y, kBookmarkAreaBtnSz, kBookmarkAreaBtnSz);
}

void HexView::setBookmarkAreaButtonVisible(bool visible)
{
    if (m_bookmarkAreaButtonVisible == visible && m_bookmarkAreaButtonFadeTimer.isActive())
        return;

    m_bookmarkAreaButtonVisible = visible;
    const qreal target = visible ? 1.0 : 0.0;
    if (qFuzzyCompare(m_bookmarkAreaButtonOpacity + 1.0, target + 1.0)) {
        m_bookmarkAreaButtonOpacity = target;
        m_bookmarkAreaButtonFadeTimer.stop();
        viewport()->update();
        return;
    }
    if (!m_bookmarkAreaButtonFadeTimer.isActive())
        m_bookmarkAreaButtonFadeTimer.start();
}

void HexView::advanceBookmarkAreaButtonFade()
{
    const qreal target = m_bookmarkAreaButtonVisible ? 1.0 : 0.0;
    constexpr qreal kStep = 0.075;

    if (m_bookmarkAreaButtonOpacity < target)
        m_bookmarkAreaButtonOpacity = qMin(target, m_bookmarkAreaButtonOpacity + kStep);
    else
        m_bookmarkAreaButtonOpacity = qMax(target, m_bookmarkAreaButtonOpacity - kStep);

    viewport()->update();
    if (qFuzzyCompare(m_bookmarkAreaButtonOpacity + 1.0, target + 1.0))
        m_bookmarkAreaButtonFadeTimer.stop();
}

HitTestRegion HexView::hitTestForBookmarkButtonAction(BookmarkButtonAction action) const
{
    switch (action) {
    case BookmarkButtonAction::Settings:
        return HVHT_BOOKMARK_EDIT;
    case BookmarkButtonAction::Close:
        return HVHT_BOOKMARK_CLOSE;
    case BookmarkButtonAction::None:
        return HVHT_NONE;
    }
    return HVHT_NONE;
}

// ── Note strip geometry ───────────────────────────────────────────────────────
//
// Returns the bounding rect of the rounded rectangle (not including the
// triangle pointer).  Used by drawNoteStrip, hitTest, and the editor.

HexView::NoteStripGeom HexView::noteStripGeom(const Bookmark &bm) const
{
    NoteStripGeom g;
    if (!m_pDataSeq || m_nBytesPerLine <= 0 || m_nFontWidth <= 0)
        return g;

    const int viewW = viewport()->width();
    const int viewH = viewport()->height();

    const int asciiRight = logToPhyXCoord(m_nBytesPerLine, 1);
    const int kGap       = m_nFontWidth * 4;
    const int rectX      = asciiRight + kGap + kNoteTriW;
    if (rectX >= viewW) return g;

    const size_w startLine = bm.offset / (size_w)m_nBytesPerLine;
    const int ny    = (int)((qint64)startLine - (qint64)m_nVScrollPos) * m_nFontHeight;

    const int rectW = kNoteMaxW;
    // Text width leaves room for the optional button column on the right.
    const bool hasButtons = m_bookmarkButtonLayout.topRight != BookmarkButtonAction::None
                         || m_bookmarkButtonLayout.bottomRight != BookmarkButtonAction::None;
    const int textW = rectW - 2 * kNotePadH - (hasButtons ? kNoteBtnSz + kNoteBtnGap : 0);
    if (textW <= 0) return g;

    const QString hexAddr = QStringLiteral("0x") +
                            QString::number(bm.offset, 16).toUpper().rightJustified(8, QLatin1Char('0'));
    const QString rangeText = (bm.length <= 1)
        ? hexAddr
        : (hexAddr + QStringLiteral("  (") + QString::number(bm.length) + QStringLiteral(" bytes)"));
    const QFontMetrics rangeFm(QApplication::font());
    const QRect rangeBounds = rangeFm.boundingRect(QRect(0, 0, textW, 10000),
                                                    Qt::AlignLeft | Qt::AlignTop, rangeText);
    const int rangeH = rangeBounds.height();
    const int btnX   = rectX + rectW - kNotePadV - kNoteBtnSz;

    const QFont nf = noteFont();
    // While this bookmark is being edited use the live editor text so the
    // strip and editor resize together as the user adds/removes lines.
    const int bmIdx_ = (int)(&bm - m_bookmarks.constData());
    const bool isEditing = (bmIdx_ == m_noteEditorIdx && m_noteEditor && m_noteEditor->isVisible());
    const QString rawText = isEditing ? m_noteEditor->toPlainText() : bm.text;
    // Use a space as stand-in for empty text so height is never zero.
    const QString text = rawText.isEmpty() ? QStringLiteral(" ") : rawText;
    QTextDocument *textDoc = makeNoteTextDoc(text, textW, nf);
    const int textH = qRound(textDoc->size().height());
    delete textDoc;

    const int rectH = kNotePadV + textH + kNoteRangePad + rangeH + kNotePadV;

    // ── Arrow tip positioning ─────────────────────────────────────────────────
    // The tip tracks the vertical midpoint of the bookmark's byte range on screen.
    //
    // minMargin: the triangle spans ±(fontHeight/2) around the tip, and the
    // rounded corners occupy kNoteRadius, so this is the minimum distance the tip
    // must sit from either edge of the rect.
    //
    // Two cases:
    //   (normal) Centre the strip on the byte-range midpoint.  This guarantees the
    //            tip is equidistant from both edges and never crowds the top corner,
    //            regardless of whether the range is short (1–2 lines) or medium.
    //   (a) Byte range is large enough that the midpoint falls below the strip's
    //       safe bottom zone — anchor strip at start line, tip at strip centre.
    const int endLine_i    = (int)((bm.offset + (bm.length > 0 ? bm.length - 1 : 0))
                                   / (size_w)m_nBytesPerLine);
    const int numLines     = endLine_i - (int)startLine + 1;
    const int rangeSpanPx  = numLines * m_nFontHeight;
    if (ny + rangeSpanPx <= 0 || ny >= viewH)
        return g;

    const int idealTipY    = ny + rangeSpanPx / 2;   // centre of byte range, screen coords
    const int minMargin    = m_nFontHeight / 2 + kNoteRadius;
    const int defaultRectY = ny + kNotePadV;

    int finalRectY, tipY;
    if (idealTipY > defaultRectY + rectH - minMargin) {
        // (a) Byte range much larger than strip — anchor at start line, tip at centre.
        finalRectY = defaultRectY;
        tipY       = finalRectY + rectH / 2;
    } else {
        // (normal) Centre the strip on the byte-range midpoint so the arrow always
        // has equal breathing room above and below, whether the range is 1 line or
        // several lines shorter than the strip.
        tipY       = idealTipY;
        finalRectY = tipY - rectH / 2;
    }

    // Never let the strip render before the first document line.  When scrolled
    // down, line 0 is above the viewport, so the strip is allowed to scroll away
    // with the bookmarked bytes instead of sticking to viewport y=0.
    const int documentTopY = -(int)m_nVScrollPos * m_nFontHeight;
    finalRectY = std::max(finalRectY, documentTopY);

    if (finalRectY + rectH <= 0 || finalRectY >= viewH) return g;

    const int rangeY = finalRectY + kNotePadV + textH + kNoteRangePad;

    g.rect      = QRect(rectX, finalRectY, rectW, rectH);
    g.textRect  = QRect(rectX + kNotePadH, finalRectY + kNotePadV, textW, textH);
    g.rangeRect = QRect(rectX + kNotePadH, rangeY, textW, rangeH);
    g.rangeText = rangeText;
    g.topButtonRect = hasButtons
        ? QRect(btnX, finalRectY + kNotePadV, kNoteBtnSz, kNoteBtnSz)
        : QRect();
    g.bottomButtonRect = hasButtons
        ? QRect(btnX, rangeY + (rangeH - kNoteBtnSz) / 2, kNoteBtnSz, kNoteBtnSz)
        : QRect();
    g.tipY      = tipY;
    g.valid     = true;
    return g;
}

// Returns the bounding rect of the single-line collapsed strip drawn for a
// non-active conflicting bookmark.  Matches the geometry used by drawNoteStrip
// in the collapsed path so hit-testing is consistent.
// Returns a null rect if the bookmark is scrolled out of view.
QRect HexView::noteCollapsedRect(const Bookmark &bm) const
{
    if (!m_pDataSeq || m_nBytesPerLine <= 0 || m_nFontWidth <= 0)
        return QRect();

    const int viewW = viewport()->width();
    const int viewH = viewport()->height();

    const int asciiRight = logToPhyXCoord(m_nBytesPerLine, 1);
    const int rectX      = asciiRight + m_nFontWidth * 4 + kNoteTriW;
    if (rectX >= viewW) return QRect();

    const int rectW = kNoteMaxW;
    const int textW = rectW - 2 * kNotePadH - kNoteBtnSz - kNoteBtnGap;
    if (textW <= 0) return QRect();

    const size_w startLine = bm.offset / (size_w)m_nBytesPerLine;
    const int ny   = (int)((qint64)startLine - (qint64)m_nVScrollPos) * m_nFontHeight;

    const QFontMetrics fm(QApplication::font());
    const int rangeH = fm.height();
    const int rectH  = kNotePadV + rangeH + kNotePadV;

    // Centre the collapsed strip on the bookmark's start line so the arrow tip
    // (which sits at the strip's vertical midpoint) aligns with the line centre.
    const int rectY = ny + (m_nFontHeight - rectH) / 2;

    if (rectY + rectH <= 0 || rectY >= viewH) return QRect();
    return QRect(rectX, rectY, rectW, rectH);
}

// Returns the full (non-collapsed) strip height for conflict-detection purposes.
// Unlike noteStripGeom(), this does not check screen visibility, so it works
// correctly even for bookmarks scrolled above or below the viewport.
int HexView::noteStripFullHeight(const Bookmark &bm) const
{
    if (m_nBytesPerLine <= 0 || m_nFontWidth <= 0) return m_nFontHeight;

    const int viewW      = viewport()->width();
    const int asciiRight = logToPhyXCoord(m_nBytesPerLine, 1);
    const int rectX      = asciiRight + m_nFontWidth * 4 + kNoteTriW;
    if (rectX >= viewW) return m_nFontHeight;

    const int rectW = kNoteMaxW;
    const int textW = rectW - 2 * kNotePadH - kNoteBtnSz - kNoteBtnGap;
    if (textW <= 0) return m_nFontHeight;

    const QString hexAddr = QStringLiteral("0x") +
                            QString::number(bm.offset, 16).toUpper().rightJustified(8, QLatin1Char('0'));
    const QString rangeText = (bm.length <= 1)
        ? hexAddr
        : (hexAddr + QStringLiteral("  (") + QString::number(bm.length) + QStringLiteral(" bytes)"));
    const QFontMetrics rangeFm(QApplication::font());
    const QRect rangeBounds = rangeFm.boundingRect(QRect(0, 0, textW, 10000),
                                                    Qt::AlignLeft | Qt::AlignTop, rangeText);
    const int rangeH = rangeBounds.height();

    // Use live editor text while this bookmark is being edited so that
    // computeBookmarkLayout() sees the current height, not the last-saved height.
    const int bmIdx_ = (int)(&bm - m_bookmarks.constData());
    const bool isEditing = (bmIdx_ == m_noteEditorIdx && m_noteEditor && m_noteEditor->isVisible());
    const QString rawText = isEditing ? m_noteEditor->toPlainText() : bm.text;
    const QString text = rawText.isEmpty() ? QStringLiteral(" ") : rawText;
    QTextDocument doc;
    doc.setDefaultFont(noteFont());
    doc.setDocumentMargin(0);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WordWrap);
    doc.setDefaultTextOption(opt);
    doc.setTextWidth(textW);
    doc.setPlainText(text);
    const int textH = qRound(doc.size().height());

    return kNotePadV + textH + kNoteRangePad + rangeH + kNotePadV;
}

// Computes the visual layout state for every bookmark in m_bookmarks.
//
// Algorithm: sweep bookmarks in offset order (already sorted) and find maximal
// conflict groups — runs of bookmarks whose full-height strips would overlap.
//
// Conflict group winner selection (first match wins):
//   1. Inline editor: if the editor is open for any group member, that member
//      wins unconditionally — we cannot collapse a strip holding a live editor.
//   2. Explicit pin: a bookmark clicked/opened by the user wins until cursor
//      navigation enters a different bookmark range.
//   3. Cursor-based expansion: when HVS_BOOKMARK_EXPAND_CURSOR is enabled, the
//      member whose byte range contains the cursor wins; shortest range breaks
//      ties.  Independently of expansion, cursor navigation always updates the
//      sticky surfaced tab so hidden collapsed bookmarks can be brought forward.
//
// Lone bookmarks use the HVS_BOOKMARK_EXPAND_LONE / HVS_BOOKMARK_EXPAND_CURSOR
// style flags.  HVS_BOOKMARK_EXPAND_ALWAYS keeps one member of every conflict
// group expanded even when the cursor is elsewhere.
// During mouse drag the display is frozen: the pinned winner is kept regardless
// of cursor movement so selection drags don't disturb the bookmark display.
//
// The function is non-const because it updates m_expandedBookmarkIdx as a
// deliberate UI-state side-effect.  hitTest() passes treatMouseAsReleased=true
// which suppresses pin updates so hit-testing never disturbs layout state.
QVector<HexView::BmLayout> HexView::computeBookmarkLayout(bool treatMouseAsReleased)
{
    const int n = m_bookmarks.size();
    QVector<BmLayout> layout(n);    // default: inConflict=false, isActive=true
    if (n == 0 || m_nBytesPerLine <= 0 || m_nFontHeight <= 0)
        return layout;

    // Height of a single-line collapsed strip — used for overlap detection below.
    // kNotePadV + one text line + kNotePadV.
    const int collapsedH = kNotePadV + QFontMetrics(QApplication::font()).height() + kNotePadV;

    // Precompute display start-Y, full visual height, and natural full-strip
    // top/bottom for each bookmark.  Conflict grouping must use the unclamped
    // rectangle, not just the byte range's start line: tall notes are centered
    // around their range midpoint and can overlap earlier bookmarks.  Do not use
    // the viewport-clamped y=0 drawing position here; offscreen bookmarks would
    // otherwise manufacture conflicts with visible bookmarks farther down.
    QVarLengthArray<int, 32> sy(n), fh(n), fullTop(n), fullBot(n), tabTop(n), tabBot(n);
    for (int i = 0; i < n; ++i) {
        const Bookmark &bm = m_bookmarks[i];
        const int line = (int)(bm.offset / (size_w)m_nBytesPerLine);
        sy[i] = (line - (int)m_nVScrollPos) * m_nFontHeight;
        fh[i] = noteStripFullHeight(bm);

        const int endLine = (int)((bm.offset + (bm.length > 0 ? bm.length - 1 : 0))
                                  / (size_w)m_nBytesPerLine);
        const int numLines = endLine - line + 1;
        const int idealTipY = sy[i] + (numLines * m_nFontHeight) / 2;
        const int minMargin = m_nFontHeight / 2 + kNoteRadius;
        const int defaultRectY = sy[i] + kNotePadV;
        int rectY;
        if (idealTipY > defaultRectY + fh[i] - minMargin)
            rectY = defaultRectY;
        else
            rectY = idealTipY - fh[i] / 2;
        fullTop[i] = rectY;
        fullBot[i] = rectY + fh[i];

        tabTop[i] = sy[i] + (m_nFontHeight - collapsedH) / 2;
        tabBot[i] = tabTop[i] + collapsedH;
    }

    QVector<int> visualOrder(n);
    for (int i = 0; i < n; ++i)
        visualOrder[i] = i;
    std::stable_sort(visualOrder.begin(), visualOrder.end(),
                     [&](int a, int b) {
                         if (fullTop[a] != fullTop[b]) return fullTop[a] < fullTop[b];
                         return a < b;
                     });

    QVector<QVector<int>> groups;
    for (int p = 0; p < n;) {
        QVector<int> group;
        group.append(visualOrder[p]);
        int groupBot = fullBot[visualOrder[p]];
        ++p;
        while (p < n && fullTop[visualOrder[p]] < groupBot) {
            const int idx = visualOrder[p];
            group.append(idx);
            groupBot = std::max(groupBot, fullBot[idx]);
            ++p;
        }
        std::sort(group.begin(), group.end());
        groups.append(group);
    }

    auto isInConflictGroup = [&](int idx) {
        if (idx < 0 || idx >= n)
            return false;
        for (const QVector<int> &group : groups) {
            if (group.size() > 1 && group.contains(idx))
                return true;
        }
        return false;
    };
    auto clearActiveInGroup = [&](const QVector<int> &group) {
        for (int j : group)
            m_bookmarks[j]._active = false;
    };
    auto activeInGroup = [&](const QVector<int> &group) {
        for (int j : group) {
            if (m_bookmarks[j]._active)
                return j;
        }
        return -1;
    };
    auto groupContains = [](const QVector<int> &group, int idx) {
        return std::binary_search(group.begin(), group.end(), idx);
    };
    auto setActiveInGroup = [&](const QVector<int> &group, int idx) {
        clearActiveInGroup(group);
        if (groupContains(group, idx))
            m_bookmarks[idx]._active = true;
    };

    const bool mouseHeld = treatMouseAsReleased
                           ? false
                           : (QApplication::mouseButtons() != Qt::NoButton);

    // Keep sticky bookmark state in sync with the final cursor position before the sweep.
    // Updating it lazily inside the sweep can leave an earlier lone bookmark
    // expanded for the same frame that navigates to a later bookmark.
    if (!treatMouseAsReleased && !mouseHeld) {
        int cursorIdx = -1;
        size_w cursorLen = (size_w)-1;
        for (int j = 0; j < n; ++j) {
            const Bookmark &bm = m_bookmarks[j];
            if (m_nCursorOffset >= bm.offset &&
                m_nCursorOffset <  bm.offset + bm.length &&
                bm.length < cursorLen) {
                cursorIdx = j;
                cursorLen = bm.length;
            }
        }

        bool cursorInPinned = false;
        if (m_expandedBookmarkIdx >= 0 && m_expandedBookmarkIdx < n) {
            const Bookmark &pinned = m_bookmarks[m_expandedBookmarkIdx];
            cursorInPinned = m_nCursorOffset >= pinned.offset &&
                             m_nCursorOffset <  pinned.offset + pinned.length;
        }

        // Blank space deliberately leaves the surfaced tab alone.  When always
        // expand is enabled, a grouped expanded bookmark stays open too;
        // otherwise expansion-on-navigation collapses when the cursor leaves
        // bookmark ranges.
        if (cursorIdx >= 0) {
            m_surfacedBookmarkIdx = cursorIdx;
            if (checkStyle(HVS_BOOKMARK_EXPAND_CURSOR)) {
                m_expandedBookmarkIdx = cursorIdx;
            } else if (m_expandedBookmarkIdx >= 0 && m_expandedBookmarkIdx != cursorIdx) {
                // Navigation expansion is off: entering another bookmark collapses
                // any previous expanded winner, but the surfaced tab still switches.
                m_expandedBookmarkIdx = -1;
            }
        } else if (m_expandedBookmarkIdx >= 0 && !cursorInPinned) {
            const bool keepGroupedExpansion = checkStyle(HVS_BOOKMARK_EXPAND_ALWAYS) &&
                                              isInConflictGroup(m_expandedBookmarkIdx);
            if (!keepGroupedExpansion)
                m_expandedBookmarkIdx = -1;
        }
    }

    // Sweep visual conflict groups using full heights for all members.
    for (const QVector<int> &group : groups) {
        if (group.size() == 1) {
            const int i = group.first();
            // Lone bookmark (no strip overlap with neighbours).
            //
            // HVS_BOOKMARK_EXPAND_LONE: show as a full strip unconditionally
            //   (the "always visible" behaviour) — nothing to do with cursor.
            // HVS_BOOKMARK_EXPAND_CURSOR: the pre-sweep block above pins this
            //   bookmark when the cursor is in range.
            //   Never expands mid-drag (!mouseHeld guard on the pin block).
            // An explicit pin (set by clicking the bookmark strip) always wins.
            const Bookmark &bm = m_bookmarks[i];
            const bool pinned  = (m_expandedBookmarkIdx == i);
            const bool inRange = m_nCursorOffset >= bm.offset &&
                                 m_nCursorOffset <  bm.offset + bm.length;
            const bool loneExpand   = checkStyle(HVS_BOOKMARK_EXPAND_LONE);
            const bool cursorExpand = checkStyle(HVS_BOOKMARK_EXPAND_CURSOR) &&
                                      !mouseHeld && inRange;
            // Redundant with the pre-sweep update, but harmless when a repaint
            // starts with no existing pin.  hitTest calls suppress side-effects.
            if (cursorExpand && !treatMouseAsReleased)
                m_expandedBookmarkIdx = i;
            const bool active = pinned || loneExpand || cursorExpand;
            // active=true  → {false,true,false}  showTab=false → full strip
            // active=false → {true,false,false}  showTab=true  → collapsed tab
            layout[i] = { !active, active, false };
        } else {
            // Conflict group.
            int    winnerIdx = -1;
            size_w winnerLen = (size_w)-1;

            // Step 1: inline editor takes priority — cannot collapse a strip that
            // holds a live text editor regardless of where the cursor is.
            if (groupContains(group, m_noteEditorIdx) &&
                m_noteEditor && m_noteEditor->isVisible()) {
                winnerIdx = m_noteEditorIdx;
                if (!treatMouseAsReleased) {
                    m_expandedBookmarkIdx = m_noteEditorIdx;
                    setActiveInGroup(group, winnerIdx);
                }
            }

            // While dragging, keep the existing display stable.
            if (winnerIdx < 0 && mouseHeld &&
                groupContains(group, m_expandedBookmarkIdx)) {
                winnerIdx = m_expandedBookmarkIdx;
            }

            // Step 2: explicit pin — direct bookmark-tab/body activation should
            // still open the selected bookmark even when cursor navigation
            // expansion is disabled.
            if (winnerIdx < 0 &&
                groupContains(group, m_expandedBookmarkIdx)) {
                winnerIdx = m_expandedBookmarkIdx;
                if (!treatMouseAsReleased)
                    setActiveInGroup(group, winnerIdx);
            }

            // Step 3: cursor-based expansion.  The preference gates full-strip
            // expansion; the pre-sweep state update above still records the
            // matching collapsed tab for surfacing even when expansion is off.
            if (winnerIdx < 0 && checkStyle(HVS_BOOKMARK_EXPAND_CURSOR) && !mouseHeld) {
                for (int j : group) {
                    const Bookmark &bm = m_bookmarks[j];
                    if (m_nCursorOffset >= bm.offset &&
                        m_nCursorOffset <  bm.offset + bm.length &&
                        bm.length < winnerLen) {
                        winnerIdx = j;
                        winnerLen = bm.length;
                    }
                }
                if (winnerIdx >= 0 && !treatMouseAsReleased) {
                    m_expandedBookmarkIdx = winnerIdx;
                    setActiveInGroup(group, winnerIdx);
                }
            }

            // When no bookmark is fully active, surface the group member whose
            // byte range contains the cursor — it becomes the front collapsed tab.
            // Runs regardless of HVS_BOOKMARK_EXPAND_CURSOR so cursor navigation
            // always reveals the relevant bookmark even when expansion is off.
            //
            // Sticky surface: when the cursor leaves all group members, the last
            // surfaced member stays at the front (m_surfacedBookmarkIdx).  This is
            // completely independent of m_pinnedBookmarkIdx so it never triggers
            // full-strip expansion.
            int    surfacedIdx = -1;
            size_w surfacedLen = (size_w)-1;
            if (winnerIdx < 0 && !mouseHeld) {
                for (int j : group) {
                    const Bookmark &bm = m_bookmarks[j];
                    if (m_nCursorOffset >= bm.offset &&
                        m_nCursorOffset <  bm.offset + bm.length &&
                        bm.length < surfacedLen) {
                        surfacedIdx = j;
                        surfacedLen = bm.length;
                    }
                }
                // Pre-sweep state already updates the sticky surface when the
                // cursor enters a bookmark range.
            }
            // Sticky fallback: cursor has left all group members, or the mouse is
            // held and live cursor updates are frozen.  Keep the last-surfaced tab
            // at the front so navigation sticks without changing state mid-drag.
            if (surfacedIdx < 0 &&
                groupContains(group, m_surfacedBookmarkIdx))
                surfacedIdx = m_surfacedBookmarkIdx;

            if (winnerIdx < 0 && checkStyle(HVS_BOOKMARK_EXPAND_ALWAYS)) {
                const int activeIdx = activeInGroup(group);
                winnerIdx = activeIdx >= 0 ? activeIdx : (surfacedIdx >= 0 ? surfacedIdx : group.first());
                if (!treatMouseAsReleased)
                    setActiveInGroup(group, winnerIdx);
            }

            // For non-active members: hide collapsed tabs that overlap the
            // active full strip.  If no member is active (only possible when
            // HVS_BOOKMARK_EXPAND_ALWAYS is disabled), stacked collapsed tabs on
            // the same line are hidden behind the surfaced one.
            const int activeTop = (winnerIdx >= 0) ? fullTop[winnerIdx] : INT_MIN;
            const int activeBot = (winnerIdx >= 0) ? fullBot[winnerIdx] : INT_MIN;

            for (int j : group) {
                const bool active = (winnerIdx != -1 && j == winnerIdx);
                bool hidden = false;
                if (!active && winnerIdx >= 0) {
                    // Hide collapsed tab if it intersects the active full strip.
                    hidden = (tabTop[j] < activeBot) && (tabBot[j] > activeTop);
                }
                if (!active && !hidden && surfacedIdx >= 0 && j != surfacedIdx) {
                    // Hide collapsed tabs stacked on the same line behind the
                    // surfaced (cursor-matching) one.
                    if (sy[j] == sy[surfacedIdx])
                        hidden = true;
                }
                layout[j] = {true, active, hidden};
            }
        }
    }

    // Final visual de-conflict: conflict grouping intentionally uses the
    // natural, unclamped strip rectangles so bookmarks scrolled above the
    // viewport do not pull unrelated visible bookmarks into their group.  Once
    // a strip is actually painted, though, noteStripGeom() may clamp it to the
    // top of the viewport.  That can make two otherwise-separate active full
    // strips overlap.  Keep the current UI winner first (editor, explicit pin,
    // surfaced/cursor bookmark), then demote lower-priority overlaps to
    // collapsed tabs.
    struct ActiveStrip {
        int idx;
        QRect rect;
        int priority;
    };
    auto bookmarkContainsCursor = [&](int idx) {
        if (idx < 0 || idx >= n)
            return false;
        const Bookmark &bm = m_bookmarks[idx];
        return m_nCursorOffset >= bm.offset &&
               m_nCursorOffset <  bm.offset + bm.length;
    };
    auto visualPriority = [&](int idx) {
        if (idx == m_noteEditorIdx && m_noteEditor && m_noteEditor->isVisible())
            return 4;
        if (idx == m_expandedBookmarkIdx)
            return 3;
        if (idx == m_surfacedBookmarkIdx)
            return 2;
        if (bookmarkContainsCursor(idx))
            return 1;
        return 0;
    };
    QVector<ActiveStrip> activeStrips;
    for (int k = 0; k < n; ++k) {
        if (!layout[k].isActive) continue;
        const NoteStripGeom geom = noteStripGeom(m_bookmarks[k]);
        if (!geom.valid) continue;
        activeStrips.append({k, geom.rect, visualPriority(k)});
    }
    std::stable_sort(activeStrips.begin(), activeStrips.end(),
                     [](const ActiveStrip &a, const ActiveStrip &b) {
                         if (a.priority != b.priority) return a.priority > b.priority;
                         if (a.rect.top() != b.rect.top()) return a.rect.top() < b.rect.top();
                         return a.idx < b.idx;
                     });

    QVector<ActiveStrip> keptActive;
    auto overlapsKeptActive = [&](const QRect &rect) {
        for (const ActiveStrip &kept : keptActive) {
            if (rect.top() < kept.rect.bottom() && rect.bottom() > kept.rect.top())
                return true;
        }
        return false;
    };
    auto collapsedTabOverlapsKept = [&](int idx) {
        for (const ActiveStrip &kept : keptActive) {
            if (tabTop[idx] < kept.rect.bottom() && tabBot[idx] > kept.rect.top())
                return true;
        }
        return false;
    };

    for (const ActiveStrip &strip : activeStrips) {
        if (!overlapsKeptActive(strip.rect)) {
            keptActive.append(strip);
            continue;
        }

        layout[strip.idx] = {true, false, collapsedTabOverlapsKept(strip.idx)};
    }

    // Hide collapsed tabs that visually overlap an active full strip.  The
    // per-group hiding above only covers same-group members; cross-group tabs
    // can intrude when a strip is clamped to y=0 (its visual footprint then
    // extends beyond its natural conflict interval).
    for (const ActiveStrip &kept : keptActive) {
        for (int j = 0; j < n; ++j) {
            if (layout[j].isActive || layout[j].hidden) continue;
            if (tabTop[j] < kept.rect.bottom() && tabBot[j] > kept.rect.top())
                layout[j].hidden = true;
        }
    }

    return layout;
}

// ── drawNoteStrip ─────────────────────────────────────────────────────────────

void HexView::drawNoteStrip(QPainter &painter, const Bookmark &bm, const BmLayout &bml)
{
    // Hidden: overlaps the active full strip — draw nothing.
    if (bml.hidden) return;

    const bool showTab = bml.inConflict && !bml.isActive;

    // Non-active conflicting bookmarks are drawn as a single-line collapsed strip:
    // triangle pointer + rounded rect containing only the range label.
    if (showTab) {
        const QRect r = noteCollapsedRect(bm);
        if (r.isNull()) return;

        const QColor bgCol = bm.colourIndex >= 0
            ? QColor(getHexColour(HvColorSlot(HVC_BOOKMARK1 + bm.colourIndex)))
            : (bm.bgColour ? QColor(bm.bgColour) : QColor(getHexColour(HVC_BOOKMARK1)));
        QColor fgCol;
        if (bm.colourIndex >= 0) {
            fgCol = realiseColour(HvColorSlot(HVC_BOOKMARK1_FG + bm.colourIndex));
        } else if (bm.fgColour) {
            fgCol = QColor(bm.fgColour);
        } else {
            fgCol = contrastColourFor(bgCol, realiseColour(HVC_BACKGROUND), realiseColour(HVC_ASCII));
        }

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);

        // Single contiguous path.  The triangle keeps its exact geometry (tip at
        // x0-kNoteTriW, base corners at (x0,y0) and (x0,y1)) but the two
        // base corners receive a quadratic-bezier fillet so the diagonal side
        // flows naturally into the horizontal edge instead of kinking.
        // The right side is unchanged: two standard rounded corners.
        {
            const qreal x0  = r.left(), y0 = r.top(), x1 = r.right(), y1 = r.bottom();
            const qreal mid = (y0 + y1) * 0.5;
            const qreal rad = kNoteRadius;
            const qreal h2  = (y1 - y0) * 0.5;   // half strip height

            // Unit vector along triangle side (from tip toward each base corner).
            const qreal triLen = qSqrt((qreal)kNoteTriW * kNoteTriW + h2 * h2);
            const qreal ux = kNoteTriW / triLen;   // > 0 (rightward component)
            const qreal uy = h2 / triLen;           // > 0 (upward→top, downward→bottom)

            // Fillet distance: how far from each corner the curve starts/ends.
            // Sized to half the half-height so it's proportional but never huge.
            const qreal f = h2 * 0.3;

            QPainterPath path;
            path.moveTo(x0 - kNoteTriW, mid);      // tip

            // Travel up triangle upper side, stop f before top-left corner.
            path.lineTo(x0 - f * ux, y0 + f * uy); // point on upper side, f before (x0,y0)
            // Bezier fillet: control point at the sharp corner, exit f along top edge.
            path.quadTo(x0, y0,  x0 + f, y0);

            path.lineTo(x1 - rad, y0);              // top edge
            path.arcTo(QRectF(x1 - 2*rad, y0,         2*rad, 2*rad), 90,  -90); // top-right
            path.lineTo(x1, y1 - rad);              // right edge
            path.arcTo(QRectF(x1 - 2*rad, y1 - 2*rad, 2*rad, 2*rad),  0,  -90); // bottom-right

            // Bottom edge, stop f before bottom-left corner.
            path.lineTo(x0 + f, y1);
            // Bezier fillet: control point at corner, exit f along lower triangle side.
            path.quadTo(x0, y1,  x0 - f * ux, y1 - f * uy); // point on lower side, f past (x0,y1)

            path.closeSubpath();                    // line down lower triangle side back to tip
            painter.fillPath(path, bgCol);
        }

        // Range label.
        const QString hexAddr = QStringLiteral("0x") +
            QString::number(bm.offset, 16).toUpper().rightJustified(8, QLatin1Char('0'));
        const QString rangeText = (bm.length <= 1)
            ? hexAddr
            : (hexAddr + QStringLiteral("  (") + QString::number(bm.length) + QStringLiteral(" bytes)"));
        const int textW = r.width() - 2 * kNotePadH - kNoteBtnSz - kNoteBtnGap;
        const QRect rangeRect(r.left() + kNotePadH, r.top() + kNotePadV, textW, r.height() - 2 * kNotePadV);
        QColor rangeFg = fgCol;
        rangeFg.setAlphaF(0.70);
        painter.setFont(QApplication::font());
        painter.setPen(rangeFg);
        painter.drawText(rangeRect, Qt::AlignLeft | Qt::AlignVCenter, rangeText);

        // Down-chevron icon in the button slot, shown on hover.
        const int bmIdx    = (int)(&bm - m_bookmarks.constData());
        const bool isHov   = (bmIdx == m_hoverBookmarkIdx);
        if (isHov) {
            const int btnX   = r.right() - kNotePadV - kNoteBtnSz;
            const int btnY   = r.top()   + (r.height() - kNoteBtnSz) / 2;
            const QRect btnRect(btnX, btnY, kNoteBtnSz, kNoteBtnSz);
            QIcon icon = QIcon(QStringLiteral(":/icons/ui/go-down-symbolic.svg"));
            if (icon.isNull()) icon = QIcon::fromTheme(QStringLiteral("go-down-symbolic"));
            if (!icon.isNull()) {
                QPixmap src = icon.pixmap(kNoteBtnSz, kNoteBtnSz);
                QPixmap dst(src.size());
                dst.setDevicePixelRatio(src.devicePixelRatio());
                dst.fill(Qt::transparent);
                QPainter p2(&dst);
                p2.drawPixmap(0, 0, src);
                p2.setCompositionMode(QPainter::CompositionMode_SourceIn);
                p2.fillRect(dst.rect(), fgCol);
                painter.drawPixmap(btnRect.topLeft(), dst);
            }
        }

        painter.restore();
        return;
    }

    const NoteStripGeom geom = noteStripGeom(bm);
    if (!geom.valid) return;

    const QRect &r = geom.rect;

    const QColor bgCol = bm.colourIndex >= 0
        ? QColor(getHexColour(HvColorSlot(HVC_BOOKMARK1 + bm.colourIndex)))
        : (bm.bgColour ? QColor(bm.bgColour) : QColor(getHexColour(HVC_BOOKMARK1)));
    QColor fgCol;
    if (bm.colourIndex >= 0) {
        fgCol = realiseColour(HvColorSlot(HVC_BOOKMARK1_FG + bm.colourIndex));
    } else if (bm.fgColour) {
        fgCol = QColor(bm.fgColour);
    } else {
        const QColor bg  = realiseColour(HVC_BACKGROUND);
        const QColor asc = realiseColour(HVC_ASCII);
        fgCol = contrastColourFor(bgCol, bg, asc);
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    // Rounded rect + triangle pointer.  The tip Y is provided by noteStripGeom()
    // and reflects the vertical midpoint of the bookmark's byte range, so the
    // arrow points into the highlighted region rather than always sitting at the
    // strip centre.  The triangle base spans one font-height; minMargin inside
    // noteStripGeom() guarantees the base clears the rounded corners.
    QPainterPath path;
    const qreal mid  = geom.tipY;
    const qreal triH = m_nFontHeight;

    // Square off a left corner only when the strip is clamped to that viewport
    // edge AND the triangle base sits inside the arc zone.  All other positions
    // keep fully rounded corners (TL rounds just like TR, BR, BL by default).
    const int  viewH_   = viewport()->height();
    const bool squareTL = (r.top() == 0)             && (mid - triH / 2.0 <= (qreal)kNoteRadius);
    const bool squareBL = (r.bottom() >= viewH_ - 1) && (mid + triH / 2.0 >= r.bottom() - (qreal)kNoteRadius);
    {
        const qreal x0 = r.left(), y0 = r.top(), x1 = r.right(), y1 = r.bottom();
        const qreal rad = kNoteRadius;
        path.setFillRule(Qt::WindingFill);

        // Start on the top edge (after TL arc if rounded, at corner if square).
        path.moveTo(squareTL ? x0 : x0 + rad, y0);
        path.lineTo(x1 - rad, y0);                                        // top edge →
        path.arcTo(QRectF(x1-2*rad, y0,       2*rad, 2*rad),  90, -90);  // top-right arc ↘
        path.lineTo(x1, y1 - rad);                                        // right edge ↓
        path.arcTo(QRectF(x1-2*rad, y1-2*rad, 2*rad, 2*rad),   0, -90);  // bottom-right arc ↙
        if (squareBL) {
            path.lineTo(x0, y1);                                          // bottom edge ← (square BL)
        } else {
            path.lineTo(x0 + rad, y1);                                    // bottom edge ←
            path.arcTo(QRectF(x0, y1-2*rad, 2*rad, 2*rad), 270, -90);    // bottom-left arc ↖
        }
        // Left edge ↑; add TL arc when rounded.
        if (!squareTL) {
            path.lineTo(x0, y0 + rad);
            path.arcTo(QRectF(x0, y0, 2*rad, 2*rad), 180, -90);          // top-left arc ↗
        }
        path.closeSubpath();
    }

    QPolygonF tri;
    tri << QPointF(r.left() - kNoteTriW, mid)
        << QPointF(r.left(),             mid - triH / 2.0)
        << QPointF(r.left(),             mid + triH / 2.0);
    path.addPolygon(tri);
    painter.fillPath(path, bgCol);

    // Text content.
    {
        QTextDocument *doc = makeNoteTextDoc(bm.text, geom.textRect.width(), noteFont());
        painter.save();
        painter.setClipRect(geom.textRect);
        painter.translate(geom.textRect.topLeft());
        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.palette.setColor(QPalette::Base, bgCol);
        ctx.palette.setColor(QPalette::Text, fgCol);
        doc->documentLayout()->draw(&painter, ctx);
        painter.restore();
        delete doc;
    }

    // Range label (dimmed).
    painter.setFont(QApplication::font());
    QColor rangeFg = fgCol;
    rangeFg.setAlphaF(0.55);
    painter.setPen(rangeFg);
    painter.drawText(geom.rangeRect, Qt::AlignLeft | Qt::AlignTop, geom.rangeText);

    // ── Gear button (shown on hover) ──────────────────────────────────────────
    const int bmIdx      = (int)(&bm - m_bookmarks.constData());
    const bool isHovered = (bmIdx == m_hoverBookmarkIdx);
    const bool grabbed   = (QWidget::mouseGrabber() == viewport());
    const bool popupHere = (bmIdx == m_bookmarkPopupIdx);

    const bool darkBg = bgCol.lightness() < 128;
    const QColor hoverFill  (darkBg ? QColor(255,255,255, 50) : QColor(0,0,0, 35));
    const QColor pressedFill(darkBg ? QColor(255,255,255, 90) : QColor(0,0,0, 65));

    auto paintBtnBg = [&](const QRect &br, bool hov, bool pres) {
        if ((hov || pres) && br.isValid()) {
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            painter.setBrush(pres ? pressedFill : hoverFill);
            painter.drawRoundedRect(QRectF(br).adjusted(-2,-2,2,2), 4, 4);
            painter.restore();
        }
    };

    auto drawIconBtn = [&](const QRect &br, bool hov, bool pres, const QString &iconName) {
        paintBtnBg(br, hov, pres);
        QIcon icon = QIcon(QStringLiteral(":/icons/actions/") + iconName + QStringLiteral(".svg"));
        if (icon.isNull()) icon = QIcon::fromTheme(iconName);
        if (!icon.isNull()) {
            QPixmap src = icon.pixmap(kNoteBtnSz, kNoteBtnSz);
            QPixmap dst(src.size());
            dst.setDevicePixelRatio(src.devicePixelRatio());
            dst.fill(Qt::transparent);
            QPainter p2(&dst);
            p2.drawPixmap(0, 0, src);
            p2.setCompositionMode(QPainter::CompositionMode_SourceIn);
            p2.fillRect(dst.rect(), fgCol);
            painter.drawPixmap(br.topLeft(), dst);
        }
    };

    const QRect settingsButtonRect = bookmarkButtonRect(geom, BookmarkButtonAction::Settings);
    if (isHovered && settingsButtonRect.isValid()) {
        const bool eHov  = grabbed ? m_pressedOnEdit : (m_hoverOnEdit || popupHere);
        const bool ePres = m_pressedOnEdit || popupHere;
        drawIconBtn(settingsButtonRect, eHov, ePres, QStringLiteral("document-edit-symbolic"));
    }

    // Close button (shown on hover).
    const QRect closeButtonRect = bookmarkButtonRect(geom, BookmarkButtonAction::Close);
    if (isHovered && closeButtonRect.isValid()) {
        const bool cHov  = grabbed ? m_pressedOnClose : m_hoverOnClose;
        const bool cPres = m_pressedOnClose;
        paintBtnBg(closeButtonRect, cHov, cPres);
        // Draw an × using two short lines.
        painter.save();
        QColor xCol = fgCol;
        xCol.setAlphaF((cHov || cPres) ? 0.85 : 0.50);
        painter.setPen(QPen(xCol, 1.5));
        const QRectF br = closeButtonRect;
        const qreal m = 3.5;
        painter.drawLine(QPointF(br.left()+m, br.top()+m),    QPointF(br.right()-m, br.bottom()-m));
        painter.drawLine(QPointF(br.right()-m, br.top()+m),   QPointF(br.left()+m,  br.bottom()-m));
        painter.restore();
    }

    painter.restore();
}

// ── Inline note editor ────────────────────────────────────────────────────────

void HexView::openNoteEditor(int bmIdx, QPoint clickPos)
{
    if (bmIdx < 0 || bmIdx >= m_bookmarks.size()) return;
    closeNoteEditor(true);

    const Bookmark &bm = m_bookmarks[bmIdx];

    // Always pin this bookmark so it wins in its conflict group regardless of
    // what m_pinnedBookmarkIdx was before (e.g. a stale pin from a previously
    // active overlapping bookmark would otherwise suppress the new one).
    expandBookmark(bmIdx);

    // Guarantee the caret is inside this bookmark's byte range so
    // computeBookmarkLayout() sees it as the cursor-based winner too.
    // Navigate there now so the full strip appears on the first paint.
    if (m_nCursorOffset < bm.offset || m_nCursorOffset >= bm.offset + bm.length) {
        m_nCursorOffset     = bm.offset;
        m_nSelectionEnd     = bm.offset;
        m_nSelectionMode    = SEL_NONE;
        m_fCursorAdjustment = false;
        scrollCenterIfOffScreen(bm.offset, bm.length);
        int cx, cy;
        caretPosFromOffset(bm.offset, &cx, &cy);
        positionCaret(cx, cy, m_nWhichPane);
        emit cursorChanged(bm.offset);
    }
    const NoteStripGeom geom = noteStripGeom(bm);
    if (!geom.valid) return;

    m_noteEditorIdx   = bmIdx;
    m_noteEditorIsNew = bm.text.isEmpty();  // flag for Escape-cancels-new-bookmark logic

    if (!m_noteEditor) {
        m_noteEditor = new QPlainTextEdit(viewport());
        m_noteEditor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_noteEditor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_noteEditor->setWordWrapMode(QTextOption::WordWrap);
        m_noteEditor->setFrameShape(QFrame::NoFrame);
        m_noteEditor->document()->setDocumentMargin(0);
        m_noteEditor->setContentsMargins(0, 0, 0, 0);
        m_noteEditor->installEventFilter(this);
        m_noteEditor->viewport()->installEventFilter(this);

        // Resize the strip and editor together whenever lines are added/removed.
        connect(m_noteEditor->document(), &QTextDocument::contentsChanged,
                this, [this]() {
            if (m_noteEditorIdx < 0 || m_noteEditorIdx >= m_bookmarks.size()) return;
            const NoteStripGeom geom = noteStripGeom(m_bookmarks[m_noteEditorIdx]);
            if (geom.valid) {
                // Add 2px to the bottom so QPlainTextEdit never needs to scroll
                // when the cursor sits on the last line (QTextDocument::size()
                // can be a pixel or two short of what ensureCursorVisible needs).
                const QRect editorRect = geom.textRect.adjusted(0, 0, 0, 2);
                if (m_noteEditor->geometry() != editorRect) {
                    m_noteEditor->setGeometry(editorRect);
                    viewport()->update();
                }
            }
        });

        // QPlainTextEdit calls ensureCursorVisible() on every cursor move
        // (including during selection), which scrolls the internal document
        // viewport even though the scrollbars are hidden.  Since the editor is
        // always sized to fit the full text there is never anything to scroll —
        // snap both axes back to zero on each cursor change.
        connect(m_noteEditor, &QPlainTextEdit::cursorPositionChanged,
                m_noteEditor, [this]() {
            m_noteEditor->verticalScrollBar()->setValue(0);
            m_noteEditor->horizontalScrollBar()->setValue(0);
        });
    }

    const QColor bg = bm.colourIndex >= 0
        ? QColor(getHexColour(HvColorSlot(HVC_BOOKMARK1 + bm.colourIndex)))
        : (bm.bgColour ? QColor(bm.bgColour) : QColor(getHexColour(HVC_BOOKMARK1)));
    QColor fg;
    if (bm.colourIndex >= 0) {
        fg = realiseColour(HvColorSlot(HVC_BOOKMARK1_FG + bm.colourIndex));
    } else if (bm.fgColour) {
        fg = QColor(bm.fgColour);
    } else {
        const QColor hvBG  = realiseColour(HVC_BACKGROUND);
        const QColor hvAsc = realiseColour(HVC_ASCII);
        fg = contrastColourFor(bg, hvBG, hvAsc);
    }

    m_noteEditor->setStyleSheet(QString(
        "QPlainTextEdit {"
        "  border: none; border-radius: 0; padding: 0; margin: 0;"
        "  background: %1; color: %2;"
        "}"
        "QPlainTextEdit:focus { border: none; padding: 0; }")
        .arg(bg.name()).arg(fg.name()));

    m_noteEditor->setFont(noteFont());
    m_noteEditor->setPlainText(bm.text);
    m_noteEditor->setGeometry(geom.textRect.adjusted(0, 0, 0, 2));
    m_noteEditor->show();
    m_noteEditor->raise();
    m_noteEditor->setFocus();
    // Place the caret under the click when we know where the mouse landed;
    // otherwise fall back to end-of-document.
    if (clickPos.x() >= 0) {
        // clickPos is in viewport coordinates; map to editor-viewport coordinates.
        const QPoint localPos = clickPos - geom.textRect.topLeft();
        QTextCursor cur = m_noteEditor->cursorForPosition(localPos);
        m_noteEditor->setTextCursor(cur);
    } else {
        m_noteEditor->moveCursor(QTextCursor::End);
    }

    // Repaint so the note strip behind the editor is suppressed.
    viewport()->update();
}

// Adds a blank bookmark at the cursor / selection and opens its inline editor,
// or navigates to and opens an existing bookmark when the new range would
// duplicate one.  Called by Ctrl+B, the context-menu action, and the goto panel.
void HexView::addBookmarkInline()
{
    const size_w selSize = selectionSize();
    const size_w offset  = selSize > 0 ? selectionStart() : cursorOffset();
    const size_w length  = selSize > 0 ? selSize : 1;

    const QList<Bookmark> &existing = bookmarks();

    if (!checkStyle(HVS_BOOKMARK_NESTED)) {
        // Nesting disabled: if the new range overlaps any existing bookmark,
        // open the best-matching one (smallest range, same shortest-wins logic
        // as cursor-based selection) instead of creating a duplicate/nested one.
        int    bestIdx = -1;
        size_w bestLen = (size_w)-1;
        for (int i = 0; i < existing.size(); ++i) {
            const Bookmark &bm = existing[i];
            const bool overlaps = offset < bm.offset + bm.length &&
                                  offset + length > bm.offset;
            if (overlaps && bm.length < bestLen) {
                bestIdx = i;
                bestLen = bm.length;
            }
        }
        if (bestIdx >= 0) {
            scrollCenterIfOffScreen(existing[bestIdx].offset, existing[bestIdx].length);
            scrollHEnd();
            openNoteEditor(bestIdx);
            return;
        }
    } else {
        // Nesting allowed: still redirect on an exact offset match to avoid
        // creating a bookmark directly on top of an existing one.
        for (int i = 0; i < existing.size(); ++i) {
            if (existing[i].offset == offset) {
                scrollCenterIfOffScreen(offset, existing[i].length);
                scrollHEnd();
                openNoteEditor(i);
                return;
            }
        }
    }

    Bookmark bm;
    bm.offset      = offset;
    bm.length      = length;
    bm.text        = QString();
    bm.fgColour    = 0;
    bm.colourIndex = 0;
    addBookmark(bm);

    // addBookmark uses a sorted insert so the new entry is not necessarily at
    // the end.  Find it by offset (safe: we verified above that no bookmark
    // existed at this offset before the insert).
    const QList<Bookmark> &updated = bookmarks();
    int newIdx = updated.size() - 1;  // fallback — should never be needed
    for (int i = 0; i < updated.size(); ++i) {
        if (updated[i].offset == offset) { newIdx = i; break; }
    }

    scrollCenterIfOffScreen(offset, length);
    scrollHEnd();
    openNoteEditor(newIdx);
}

void HexView::closeNoteEditor(bool save)
{
    if (!m_noteEditor || !m_noteEditor->isVisible()) return;
    // Capture focus state before hide() fires a synchronous FocusOut that
    // would call us recursively (the re-entrant call returns early via isVisible()).
    const bool hadFocus = m_noteEditor->hasFocus()
                       || m_noteEditor->viewport()->hasFocus();
    m_noteEditor->hide();
    bool changed = false;
    if (save && m_noteEditorIdx >= 0 && m_noteEditorIdx < m_bookmarks.size()) {
        const int idx = m_noteEditorIdx;

        // Record the strip height before saving so we can detect whether
        // the new text makes the strip taller.  The editor is already hidden
        // so noteStripFullHeight() now reads bm.text (the old text) rather
        // than the live editor content — exactly the pre-save height.
        const int oldH = (m_expandedBookmarkIdx == idx && m_nBytesPerLine > 0)
                         ? noteStripFullHeight(m_bookmarks[idx]) : 0;

        const QString newText = m_noteEditor->toPlainText();
        if (m_bookmarks[idx].text != newText) {
            m_bookmarks[idx].text = newText;
            changed = true;
        }

        // If saving grew the strip AND the pinned bookmark is the one just
        // edited, check whether the taller strip now pulls in a right
        // neighbour that wasn't previously in the conflict group.  If so,
        // reset the pin so that newly-formed group starts in the neutral
        // (all-collapsed) state rather than permanently hiding the neighbour.
        if (m_expandedBookmarkIdx == idx && oldH > 0 && m_nFontHeight > 0) {
            const int newH = noteStripFullHeight(m_bookmarks[idx]);
            if (newH > oldH && idx + 1 < m_bookmarks.size()) {
                const int line     = (int)(m_bookmarks[idx].offset / (size_w)m_nBytesPerLine);
                const int sy_self  = (line - (int)m_nVScrollPos) * m_nFontHeight;
                const int nextLine = (int)(m_bookmarks[idx + 1].offset / (size_w)m_nBytesPerLine);
                const int sy_next  = (nextLine - (int)m_nVScrollPos) * m_nFontHeight;
                // Newly captured = was outside the old footprint, now inside.
                if (sy_next >= sy_self + oldH && sy_next < sy_self + newH) {
                    m_expandedBookmarkIdx   = -1;
                    m_surfacedBookmarkIdx = -1;
                    clearBookmarkActive(m_bookmarks);
                }
            }
        }
    }
    m_noteEditorIdx = -1;
    if (changed)
        emit bookmarksChanged();
    // If the editor owned focus, return it to the hex view in the ASCII pane.
    // Skip when FocusOut triggered us — focus is already moving elsewhere.
    if (hadFocus) {
        setActivePane(1);
        viewport()->setFocus();
    }
    viewport()->update();
}
