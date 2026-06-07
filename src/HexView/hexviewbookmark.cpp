#include "hexview.h"
#include "hexviewbookmarklogic.h"
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
static constexpr int kRangeStepperGap = 2;
static constexpr int kRangeStepperButtonW = 26;
static constexpr int kRangeStepperH = 22;
static constexpr int kRangeStepperW = kRangeStepperButtonW * 2 + 1;

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

static QString bookmarkOffsetText(size_w offset)
{
    return QStringLiteral("0x") +
           QString::number(offset, 16).toUpper().rightJustified(8, QLatin1Char('0'));
}

static QString bookmarkLengthText(size_w length)
{
    const size_w shownLength = qMax<size_w>(1, length);
    return QStringLiteral("(") + QString::number(shownLength) +
           QStringLiteral(" ") + (shownLength == 1 ? QStringLiteral("byte") : QStringLiteral("bytes")) +
           QStringLiteral(")");
}

static int bookmarkAreaButtonIndex(BookmarkAreaButton button)
{
    return static_cast<int>(button);
}

static QString bookmarkAreaButtonIconName(BookmarkAreaButton button)
{
    switch (button) {
    case BOOKMARK_ADD:
        return QStringLiteral("bookmark-star-add");
    case BOOKMARK_LIST:
    case BOOKMARK_AREA_BUTTON_COUNT:
        break;
    }
    return QStringLiteral("bookmark-star-on-tray");
}

static QString bookmarkAreaButtonFallbackText(BookmarkAreaButton button)
{
    return button == BOOKMARK_ADD ? QStringLiteral("+") : QStringLiteral("*");
}

static void drawTintedIconOrFallbackText(QPainter &painter,
                                         const QRect &rect,
                                         const QString &iconName,
                                         const QColor &colour,
                                         const QString &fallbackText)
{
    QIcon icon(QStringLiteral(":/icons/actions/") + iconName + QStringLiteral(".svg"));
    if (icon.isNull())
        icon = QIcon::fromTheme(iconName);

    const QPixmap src = icon.pixmap(rect.size());
    if (!src.isNull()) {
        QPixmap tinted(src.size());
        tinted.setDevicePixelRatio(src.devicePixelRatio());
        tinted.fill(Qt::transparent);

        QPainter iconPainter(&tinted);
        iconPainter.drawPixmap(0, 0, src);
        iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        iconPainter.fillRect(tinted.rect(), colour);
        iconPainter.end();

        painter.drawPixmap(rect, tinted);
        return;
    }

    painter.save();
    painter.setPen(colour);
    painter.drawText(rect, Qt::AlignCenter, fallbackText);
    painter.restore();
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
    clearBookmarkRangeStepper();
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
    if (m_inlineRangeBookmarkIdx >= insertIdx)
        ++m_inlineRangeBookmarkIdx;
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
    if (m_inlineRangeBookmarkIdx == idx)
        clearBookmarkRangeStepper();
    else if (m_inlineRangeBookmarkIdx > idx)
        --m_inlineRangeBookmarkIdx;
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
    const bool keepInline = (m_inlineRangeBookmarkIdx == idx);
    m_bookmarks[idx] = bm;
    if (keepInline) {
        // Offset/length dragging replaces the bookmark on every mouse move.
        // Because changing offset can re-sort m_bookmarks, tag the Bookmark
        // object itself before sorting and rediscover its new index below.
        // This cannot use _active: layout clears that bit as a scratch value.
        for (Bookmark &bookmark : m_bookmarks)
            bookmark._rangeEditing = false;
        m_bookmarks[idx]._rangeEditing = true;
    }
    // Re-sort in case the replacement bookmark has a different offset.
    // This invalidates all index-based pin state, so clear both pins.
    m_expandedBookmarkIdx   = -1;
    m_surfacedBookmarkIdx = -1;
    std::sort(m_bookmarks.begin(), m_bookmarks.end(), bmOffsetLess);
    if (keepInline) {
        m_inlineRangeBookmarkIdx = -1;
        for (int i = 0; i < m_bookmarks.size(); ++i) {
            if (m_bookmarks[i]._rangeEditing) {
                m_inlineRangeBookmarkIdx = i;
                break;
            }
        }
    }
    clearBookmarkActive(m_bookmarks);
    setupScrollbars();
    viewport()->update();
    emit bookmarksChanged();
}

void HexView::activateBookmarkRangeStepper(int idx, BookmarkRangeField field)
{
    if (kBookmarkRangeEditExperiment == BookmarkRangeEditExperiment::None ||
            idx < 0 || idx >= m_bookmarks.size()) {
        clearBookmarkRangeStepper();
        return;
    }

    closeNoteEditor(true);
    // The drag owner has to survive replaceBookmark() re-sorting while the
    // mouse is held, so keep a dedicated transient marker on the Bookmark.
    for (Bookmark &bm : m_bookmarks)
        bm._rangeEditing = false;
    m_bookmarks[idx]._rangeEditing = true;
    m_inlineRangeBookmarkIdx = idx;
    m_inlineRangeField = field;
    m_hoverInlineRangeStep = HVHT_NONE;
    m_pressedInlineRangeStep = HVHT_NONE;
    m_inlineRangeDragActive = false;
    m_inlineRangeDragValueRect = QRect();
    m_inlineRangeDragOriginalBookmark = m_bookmarks[idx];
    m_inlineRangeDragLastDelta = 0;
    if (kBookmarkRangeEditExperiment == BookmarkRangeEditExperiment::DragAdjust) {
        const NoteStripGeom geom = noteStripGeom(m_bookmarks[idx]);
        m_inlineRangeDragValueRect = (field == BookmarkRangeField::Offset)
            ? geom.offsetRect
            : geom.lengthRect;
        m_inlineRangeDragActive = m_inlineRangeDragValueRect.isValid();
    }
    m_caretTimer.stop();
    m_caretVisible = false;
    viewport()->setFocus(Qt::MouseFocusReason);
    expandBookmark(idx);
}

void HexView::clearBookmarkRangeStepper()
{
    const bool wasActive = (m_inlineRangeBookmarkIdx >= 0);
    for (Bookmark &bm : m_bookmarks)
        bm._rangeEditing = false;
    m_inlineRangeBookmarkIdx = -1;
    m_hoverInlineRangeStep = HVHT_NONE;
    m_pressedInlineRangeStep = HVHT_NONE;
    m_inlineRangeDragActive = false;
    m_inlineRangeDragValueRect = QRect();
    m_inlineRangeDragOriginalBookmark = Bookmark();
    m_inlineRangeDragLastDelta = 0;
    if (wasActive && (hasFocus() || viewport()->hasFocus())) {
        m_caretVisible = true;
        m_caretTimer.start(QApplication::cursorFlashTime() / 2);
    }
}

void HexView::clampBookmarkRangeToNonNestedGap(int idx, Bookmark &updated) const
{
    if (checkStyle(HVS_BOOKMARK_NESTED))
        return;

    // With nested bookmarks disabled, offset/length adjustment must preserve
    // the same invariant as addBookmarkInline(): user bookmarks are not allowed
    // to overlap.  Clamp the proposed edit into the current gap around this
    // bookmark, excluding the bookmark being edited.
    //
    // Offset edit: the right edge is anchored, so only the start can move.  It
    // may move left until it touches the previous/overlapping neighbour's end,
    // and may move right until at least one byte remains before the anchored end.
    //
    // Length edit: the start is anchored, so only the right edge can move.  It
    // may extend right until it touches the next/overlapping neighbour's start.
    // Shrinking is always safe.
    const auto field = m_inlineRangeField == BookmarkRangeField::Offset
        ? BookmarkLogic::RangeField::Offset
        : BookmarkLogic::RangeField::Length;
    BookmarkLogic::clampToNonNestedGap(m_bookmarks, idx, field, updated, size(),
                                       checkStyle(HVS_BOOKMARK_NESTED));
}

void HexView::stepActiveBookmarkRange(int delta)
{
    const int idx = m_inlineRangeBookmarkIdx;
    if (idx < 0 || idx >= m_bookmarks.size() || delta == 0) {
        clearBookmarkRangeStepper();
        viewport()->update();
        return;
    }

    Bookmark updated = m_bookmarks[idx];
    const size_w fileSize = size();

    if (m_inlineRangeField == BookmarkRangeField::Offset) {
        const size_w oldOffset = updated.offset;
        const size_w oldLength = qMax<size_w>(1, updated.length);
        const size_w oldEnd = oldOffset == (size_w)-1 || oldLength > (size_w)-1 - oldOffset
            ? (size_w)-1
            : oldOffset + oldLength;

        if (delta < 0) {
            updated.offset = oldOffset > 0 ? oldOffset - 1 : 0;
        } else {
            const size_w maxOffset = fileSize > 0 ? qMin(fileSize - 1, oldEnd - 1) : fileSize;
            updated.offset = qMin(oldOffset + 1, maxOffset);
        }

        if (fileSize > 0) {
            if (updated.offset >= fileSize)
                updated.length = 1;
            else
                updated.length = qMax<size_w>(1, qMin(oldEnd - updated.offset,
                                                      fileSize - updated.offset));
        } else {
            updated.length = qMax<size_w>(1, oldEnd - updated.offset);
        }
    } else {
        const size_w cur = qMax<size_w>(1, updated.length);
        if (delta < 0)
            updated.length = cur > 1 ? cur - 1 : 1;
        else
            updated.length = cur == (size_w)-1 ? cur : cur + 1;

        if (fileSize > 0) {
            if (updated.offset >= fileSize)
                updated.length = 1;
            else
                updated.length = qMax<size_w>(1, qMin(updated.length, fileSize - updated.offset));
        }
    }

    clampBookmarkRangeToNonNestedGap(idx, updated);

    replaceBookmark(idx, updated);
    if (m_inlineRangeBookmarkIdx < 0)
        clearBookmarkRangeStepper();
    else
        expandBookmark(m_inlineRangeBookmarkIdx);
}

int HexView::bookmarkRangeDragDelta(const QPoint &pos) const
{
    return BookmarkLogic::rangeDragDelta(m_inlineRangeDragValueRect,
                                         m_dragStartPos,
                                         pos,
                                         m_nBytesPerLine);
}

void HexView::updateBookmarkRangeDrag(const QPoint &pos)
{
    int idx = m_inlineRangeBookmarkIdx;
    if (idx < 0 || idx >= m_bookmarks.size() || !m_bookmarks[idx]._rangeEditing) {
        idx = -1;
        for (int i = 0; i < m_bookmarks.size(); ++i) {
            if (m_bookmarks[i]._rangeEditing) {
                idx = i;
                m_inlineRangeBookmarkIdx = i;
                break;
            }
        }
    }
    if (idx < 0 || idx >= m_bookmarks.size() || !m_inlineRangeDragActive) {
        clearBookmarkRangeStepper();
        viewport()->update();
        return;
    }

    const int delta = bookmarkRangeDragDelta(pos);
    if (delta == m_inlineRangeDragLastDelta)
        return;
    m_inlineRangeDragLastDelta = delta;

    Bookmark updated = m_inlineRangeDragOriginalBookmark;
    const size_w fileSize = size();

    if (m_inlineRangeField == BookmarkRangeField::Offset) {
        const size_w originalOffset = updated.offset;
        const size_w originalLength = qMax<size_w>(1, updated.length);
        const size_w maxOffset = fileSize > 0 ? fileSize - 1 : (size_w)-1;
        const size_w boundedOriginalOffset = qMin(originalOffset, maxOffset);
        size_w newOffset = boundedOriginalOffset;

        if (delta < 0) {
            const size_w amount = (size_w)(-delta);
            newOffset = amount > boundedOriginalOffset ? 0 : boundedOriginalOffset - amount;
        } else {
            const size_w amount = (size_w)delta;
            const size_w maxOffsetForLength = originalLength - 1 > maxOffset - boundedOriginalOffset
                ? maxOffset
                : boundedOriginalOffset + originalLength - 1;
            newOffset = amount > maxOffsetForLength - boundedOriginalOffset
                ? maxOffsetForLength
                : boundedOriginalOffset + amount;
        }

        updated.offset = newOffset;
        if (newOffset < boundedOriginalOffset) {
            const size_w moved = boundedOriginalOffset - newOffset;
            updated.length = moved > (size_w)-1 - originalLength
                ? (size_w)-1
                : originalLength + moved;
        } else {
            const size_w moved = newOffset - boundedOriginalOffset;
            updated.length = moved >= originalLength ? 1 : originalLength - moved;
        }

        if (fileSize > 0) {
            if (updated.offset >= fileSize)
                updated.length = 1;
            else
                updated.length = qMax<size_w>(1, qMin(updated.length, fileSize - updated.offset));
        }
    } else {
        const size_w originalLength = qMax<size_w>(1, updated.length);
        if (delta < 0) {
            const size_w amount = (size_w)(-delta);
            updated.length = amount >= originalLength ? 1 : originalLength - amount;
        } else {
            const size_w amount = (size_w)delta;
            updated.length = amount > (size_w)-1 - originalLength
                ? (size_w)-1
                : originalLength + amount;
        }

        if (fileSize > 0) {
            if (updated.offset >= fileSize)
                updated.length = 1;
            else
                updated.length = qMax<size_w>(1, qMin(updated.length, fileSize - updated.offset));
        }
    }

    clampBookmarkRangeToNonNestedGap(idx, updated);

    replaceBookmark(idx, updated);
    if (m_inlineRangeBookmarkIdx < 0)
        clearBookmarkRangeStepper();
    else
        expandBookmark(m_inlineRangeBookmarkIdx);
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
    m_hoverOnOffset = false;
    m_hoverOnLength = false;
    m_pressedOnClose = false;
    m_pressedOnEdit = false;
    m_pressedOnOffset = false;
    m_pressedOnLength = false;
    m_hoverInlineRangeStep = HVHT_NONE;
    m_pressedInlineRangeStep = HVHT_NONE;
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

QRect HexView::bookmarkRangeStepperRect(const NoteStripGeom &geom, BookmarkRangeField field) const
{
    if (!geom.valid)
        return QRect();

    const QRect valueRect = field == BookmarkRangeField::Offset
        ? geom.offsetRect
        : geom.lengthRect;
    if (!valueRect.isValid())
        return QRect();

    const int minX = geom.rect.left() + kNotePadH;
    const int maxX = geom.rect.right() - kNotePadH - kRangeStepperW + 1;
    if (maxX < minX)
        return QRect();

    const int preferredX = valueRect.right() + 1 - kRangeStepperW;
    const int x = qBound(minX, preferredX, maxX);
    const int y = valueRect.bottom() + 1 + kRangeStepperGap;

    return QRect(x, y, kRangeStepperW, kRangeStepperH);
}

HitTestRegion HexView::hitTestBookmarkRangeStepper(const NoteStripGeom &geom, int x, int y) const
{
    if (m_inlineRangeBookmarkIdx < 0)
        return HVHT_NONE;

    const QRect stepper = bookmarkRangeStepperRect(geom, m_inlineRangeField);
    if (!stepper.contains(x, y))
        return HVHT_NONE;

    const QRect decRect(stepper.left(), stepper.top(), kRangeStepperButtonW, stepper.height());
    const QRect incRect(stepper.left() + kRangeStepperButtonW + 1, stepper.top(),
                        kRangeStepperButtonW, stepper.height());
    if (decRect.contains(x, y))
        return HVHT_BOOKMARK_RANGE_DEC;
    if (incRect.contains(x, y))
        return HVHT_BOOKMARK_RANGE_INC;
    return HVHT_NONE;
}

int HexView::bookmarkAreaCenterX() const
{
    if (!m_pDataSeq || m_nBytesPerLine <= 0 ||
            m_nFontWidth <= 0 || m_nFontHeight <= 0)
        return -1;

    const int viewW = viewport()->width();
    if (viewW < kBookmarkAreaBtnSz)
        return -1;

    const int asciiRight = logToPhyXCoord(m_nBytesPerLine, 1);
    const int minX = asciiRight + qMax(6, m_nFontWidth);
    if (minX + kBookmarkAreaBtnSz > viewW)
        return -1;

    constexpr int kMargin = 12;
    const int noteLeft = asciiRight + m_nFontWidth * 4;
    const int noteRight = noteLeft + kNoteTriW + kNoteMaxW;
    const int preferredX = noteLeft + (noteRight - noteLeft - kBookmarkAreaBtnSz) / 2;
    const int maxX = qMax(minX, viewW - kBookmarkAreaBtnSz - kMargin);
    return qBound(minX, preferredX, maxX) + kBookmarkAreaBtnSz / 2;
}

int HexView::bookmarkRangeMidpointY(size_w offset, size_w length) const
{
    if (m_nBytesPerLine <= 0 || m_nFontHeight <= 0)
        return 0;

    const size_w startLine = offset / (size_w)m_nBytesPerLine;
    const int startY = (int)((qint64)startLine - (qint64)m_nVScrollPos) * m_nFontHeight;
    const size_w endOffset = offset + (length > 0 ? length - 1 : 0);
    const int endLine = (int)(endOffset / (size_w)m_nBytesPerLine);
    const int numLines = endLine - (int)startLine + 1;
    return startY + (numLines * m_nFontHeight) / 2;
}

bool HexView::bookmarkRangeIntersectsViewport(size_w offset, size_w length) const
{
    if (length == 0 || m_nBytesPerLine <= 0 || m_nFontHeight <= 0)
        return false;

    const size_w startLine = offset / (size_w)m_nBytesPerLine;
    const size_w endLine = (offset + length - 1) / (size_w)m_nBytesPerLine;
    const int top = (int)((qint64)startLine - (qint64)m_nVScrollPos) * m_nFontHeight;
    const int bottom = (int)((qint64)endLine - (qint64)m_nVScrollPos + 1) * m_nFontHeight;
    return bottom > 0 && top < viewport()->height();
}

bool HexView::shouldShowBookmarkAddButton() const
{
    const size_w selSize = selectionSize();
    if (selSize == 0 || m_nSelectionMode != SEL_NONE ||
            !bookmarkRangeIntersectsViewport(selectionStart(), selSize))
        return false;

    if (checkStyle(HVS_BOOKMARK_NESTED))
        return true;

    const size_w offset = selectionStart();
    const size_w end = offset + selSize;
    for (const Bookmark &bm : m_bookmarks) {
        if (offset < bm.offset + bm.length && end > bm.offset)
            return false;
    }

    return true;
}

QRect HexView::bookmarkAreaPopupAnchorRect(int y, int height) const
{
    height = qMax(1, height);

    if (!m_pDataSeq || m_nBytesPerLine <= 0 || m_nFontWidth <= 0)
        return QRect(viewport()->mapToGlobal(QPoint(0, y)), QSize(1, height));

    const int asciiRight = logToPhyXCoord(m_nBytesPerLine, 1);
    const int bookmarkLeft = asciiRight + m_nFontWidth * 4;
    const int x = asciiRight + (bookmarkLeft - asciiRight) / 2;
    return QRect(viewport()->mapToGlobal(QPoint(x, y)), QSize(1, height));
}

QRect HexView::bookmarkAreaButtonRect(BookmarkAreaButton button) const
{
    if (button == BOOKMARK_LIST && m_bookmarks.isEmpty())
        return QRect();
    if (button == BOOKMARK_ADD && !shouldShowBookmarkAddButton())
        return QRect();

    const int viewH = viewport()->height();
    if (viewH < kBookmarkAreaBtnSz)
        return QRect();

    const int centerX = bookmarkAreaCenterX();
    if (centerX < 0)
        return QRect();

    const int x = centerX - kBookmarkAreaBtnSz / 2;
    int y = 0;
    if (button == BOOKMARK_ADD) {
        constexpr int kMargin = 8;
        const int preferredY = bookmarkRangeMidpointY(selectionStart(), selectionSize())
                             - kBookmarkAreaBtnSz / 2;
        const int maxY = qMax(kMargin, viewH - kBookmarkAreaBtnSz - kMargin);
        y = qBound(kMargin, preferredY, maxY);
    } else {
        constexpr int kBottomMargin = 20;
        y = qMax(0, viewH - kBookmarkAreaBtnSz - kBottomMargin);
    }
    return QRect(x, y, kBookmarkAreaBtnSz, kBookmarkAreaBtnSz);
}

void HexView::setBookmarkAreaButtonVisible(BookmarkAreaButton button, bool visible)
{
    const int idx = bookmarkAreaButtonIndex(button);
    if (idx < 0 || idx >= BOOKMARK_AREA_BUTTON_COUNT)
        return;

    if (m_bookmarkAreaButtonVisible[idx] == visible && m_bookmarkAreaButtonFadeTimer.isActive())
        return;

    m_bookmarkAreaButtonVisible[idx] = visible;
    const qreal target = visible ? 1.0 : 0.0;
    if (qFuzzyCompare(m_bookmarkAreaButtonOpacity[idx] + 1.0, target + 1.0)) {
        m_bookmarkAreaButtonOpacity[idx] = target;
        if (m_bookmarkAreaButtonOpacity[BOOKMARK_LIST] == (m_bookmarkAreaButtonVisible[BOOKMARK_LIST] ? 1.0 : 0.0) &&
                m_bookmarkAreaButtonOpacity[BOOKMARK_ADD] == (m_bookmarkAreaButtonVisible[BOOKMARK_ADD] ? 1.0 : 0.0))
            m_bookmarkAreaButtonFadeTimer.stop();
        viewport()->update();
        return;
    }
    if (!m_bookmarkAreaButtonFadeTimer.isActive())
        m_bookmarkAreaButtonFadeTimer.start();
}

void HexView::setBookmarkAreaButtonsVisible(bool visible)
{
    const bool showAdd = visible && shouldShowBookmarkAddButton();
    bool showList = visible && !m_bookmarks.isEmpty();
    if (showAdd && showList) {
        const bool allowBothBookmarkAreaButtons = false;
        const QRect addRect = bookmarkAreaButtonRect(BOOKMARK_ADD);
        const QRect listRect = bookmarkAreaButtonRect(BOOKMARK_LIST);
        if (!allowBothBookmarkAreaButtons ||
                (!addRect.isNull() && !listRect.isNull() && addRect.intersects(listRect)))
            showList = false;
    }

    setBookmarkAreaButtonVisible(BOOKMARK_ADD, showAdd);
    setBookmarkAreaButtonVisible(BOOKMARK_LIST, showList);
}

void HexView::advanceBookmarkAreaButtonFade()
{
    constexpr qreal kStep = 0.075;
    bool allAtTarget = true;

    for (int idx = 0; idx < BOOKMARK_AREA_BUTTON_COUNT; ++idx) {
        const qreal target = m_bookmarkAreaButtonVisible[idx] ? 1.0 : 0.0;
        if (m_bookmarkAreaButtonOpacity[idx] < target)
            m_bookmarkAreaButtonOpacity[idx] = qMin(target, m_bookmarkAreaButtonOpacity[idx] + kStep);
        else
            m_bookmarkAreaButtonOpacity[idx] = qMax(target, m_bookmarkAreaButtonOpacity[idx] - kStep);

        if (!qFuzzyCompare(m_bookmarkAreaButtonOpacity[idx] + 1.0, target + 1.0))
            allAtTarget = false;
    }

    viewport()->update();
    if (allAtTarget)
        m_bookmarkAreaButtonFadeTimer.stop();
}

void HexView::clearBookmarkAreaButtonState()
{
    m_hoverBookmarkAreaButton.fill(false);
    m_pressedBookmarkAreaButton.fill(false);
    setBookmarkAreaButtonsVisible(false);
}

void HexView::drawBookmarkAreaButtons(QPainter &painter)
{
    auto drawButton = [this, &painter](BookmarkAreaButton button) {
        const int idx = bookmarkAreaButtonIndex(button);
        if (idx < 0 || idx >= BOOKMARK_AREA_BUTTON_COUNT ||
                m_bookmarkAreaButtonOpacity[idx] <= 0.0)
            return;

        const QRect r = bookmarkAreaButtonRect(button);
        if (r.isEmpty())
            return;

        const bool direct = m_hoverBookmarkAreaButton[idx] || m_pressedBookmarkAreaButton[idx];
        QColor circleColor = palette().color(direct ? QPalette::Mid : QPalette::Window);
        if (direct)
            circleColor = circleColor.darker(125);
        const QColor iconColor = direct ? QColor(Qt::white) : QColor(188, 188, 188);
        const QRectF circle = QRectF(r).adjusted(2.5, 2.5, -2.5, -2.5);
        QPainterPath clipPath;
        clipPath.addEllipse(circle);

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setOpacity(m_bookmarkAreaButtonOpacity[idx]);
        painter.setClipPath(clipPath);
        painter.setPen(Qt::NoPen);
        painter.setBrush(circleColor);
        painter.drawEllipse(circle);

        const int iconSz = 30;
        const QRect iconRect(r.left() + (r.width() - iconSz) / 2,
                             r.top() + (r.height() - iconSz) / 2,
                             iconSz, iconSz);
        drawTintedIconOrFallbackText(painter, iconRect,
                                     bookmarkAreaButtonIconName(button),
                                     iconColor,
                                     bookmarkAreaButtonFallbackText(button));
        painter.restore();
    };

    drawButton(BOOKMARK_LIST);
    drawButton(BOOKMARK_ADD);
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

    const QString hexAddr = bookmarkOffsetText(bm.offset);
    const QString lengthText = bookmarkLengthText(bm.length);
    const QString rangeText = hexAddr + QStringLiteral("  ") + lengthText;
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

    const int idealTipY    = bookmarkRangeMidpointY(bm.offset, bm.length);
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
    g.offsetText = hexAddr;
    g.lengthText = lengthText;
    g.offsetRect = QRect(g.rangeRect.left(), g.rangeRect.top(),
                         rangeFm.horizontalAdvance(hexAddr), g.rangeRect.height());
    const int lengthX = g.offsetRect.right() + 1 + rangeFm.horizontalAdvance(QStringLiteral("  "));
    g.lengthRect = QRect(lengthX, g.rangeRect.top(),
                         rangeFm.horizontalAdvance(lengthText), g.rangeRect.height());
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

    const QString rangeText = bookmarkOffsetText(bm.offset) + QStringLiteral("  ") +
                              bookmarkLengthText(bm.length);
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

    // HexView still owns widget-specific geometry: note height depends on fonts,
    // live editor text, scroll position, and viewport clamping.  BookmarkLogic
    // owns the state-machine answer once these measurements are supplied.
    const int collapsedH = kNotePadV + QFontMetrics(QApplication::font()).height() + kNotePadV;
    QVector<BookmarkLogic::BookmarkLayoutGeometry> geometry(n);
    for (int i = 0; i < n; ++i) {
        const Bookmark &bm = m_bookmarks[i];
        BookmarkLogic::BookmarkLayoutGeometry &g = geometry[i];

        const int line = (int)(bm.offset / (size_w)m_nBytesPerLine);
        g.sy = (line - (int)m_nVScrollPos) * m_nFontHeight;
        const int fullHeight = noteStripFullHeight(bm);

        const int endLine = (int)((bm.offset + (bm.length > 0 ? bm.length - 1 : 0))
                                  / (size_w)m_nBytesPerLine);
        const int numLines = endLine - line + 1;
        const int idealTipY = g.sy + (numLines * m_nFontHeight) / 2;
        const int minMargin = m_nFontHeight / 2 + kNoteRadius;
        const int defaultRectY = g.sy + kNotePadV;
        int rectY;
        if (idealTipY > defaultRectY + fullHeight - minMargin)
            rectY = defaultRectY;
        else
            rectY = idealTipY - fullHeight / 2;
        g.fullTop = rectY;
        g.fullBot = rectY + fullHeight;

        g.tabTop = g.sy + (m_nFontHeight - collapsedH) / 2;
        g.tabBot = g.tabTop + collapsedH;

        const NoteStripGeom painted = noteStripGeom(bm);
        g.paintedFullRect = painted.rect;
        g.paintedFullRectValid = painted.valid;
    }

    BookmarkLogic::BookmarkLayoutRequest request;
    request.cursorOffset = m_nCursorOffset;
    request.expandedBookmarkIdx = m_expandedBookmarkIdx;
    request.surfacedBookmarkIdx = m_surfacedBookmarkIdx;
    request.inlineRangeBookmarkIdx = m_inlineRangeBookmarkIdx;
    request.noteEditorIdx = m_noteEditorIdx;
    request.noteEditorVisible = m_noteEditor && m_noteEditor->isVisible();
    request.mouseHeld = treatMouseAsReleased ? false : (QApplication::mouseButtons() != Qt::NoButton);
    request.allowStateUpdates = !treatMouseAsReleased;
    request.expandLone = checkStyle(HVS_BOOKMARK_EXPAND_LONE);
    request.expandCursor = checkStyle(HVS_BOOKMARK_EXPAND_CURSOR);
    request.expandAlways = checkStyle(HVS_BOOKMARK_EXPAND_ALWAYS);

    const BookmarkLogic::BookmarkLayoutResult result =
        BookmarkLogic::computeLayout(m_bookmarks, geometry, request);

    for (int i = 0; i < n && i < result.layout.size(); ++i) {
        const BookmarkLogic::BookmarkLayoutEntry &entry = result.layout[i];
        layout[i] = {entry.inConflict, entry.isActive, entry.hidden};
    }

    if (!treatMouseAsReleased) {
        m_expandedBookmarkIdx = result.expandedBookmarkIdx;
        m_surfacedBookmarkIdx = result.surfacedBookmarkIdx;
        m_inlineRangeBookmarkIdx = result.inlineRangeBookmarkIdx;
        for (int i = 0; i < n && i < result.activeFlags.size(); ++i)
            m_bookmarks[i]._active = result.activeFlags[i];
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
        const QString rangeText = bookmarkOffsetText(bm.offset) + QStringLiteral("  ") +
                                  bookmarkLengthText(bm.length);
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
        // A collapsed draw during live range-drag is exactly the failure mode
        // we guard against: clearing here drops _rangeEditing, then an
        // overlapping bookmark's byte highlight can paint over the edited range.
        // Only cancel stale inline state when no button is held.
        if (BookmarkLogic::shouldClearRangeEditFromCollapsedDraw(
                    bmIdx,
                    m_inlineRangeBookmarkIdx,
                    QApplication::mouseButtons() != Qt::NoButton))
            clearBookmarkRangeStepper();
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

    // Range label (dimmed). Offset and length are separate hit targets.
    painter.setFont(QApplication::font());
    QColor rangeFg = fgCol;
    rangeFg.setAlphaF(0.55);
    painter.setPen(rangeFg);

    // ── Gear button (shown on hover) ──────────────────────────────────────────
    const int bmIdx      = (int)(&bm - m_bookmarks.constData());
    const bool isHovered = (bmIdx == m_hoverBookmarkIdx);
    const bool grabbed   = (QWidget::mouseGrabber() == viewport());
    const bool isPressedBookmark = grabbed && bmIdx == m_pressedBookmarkIdx;
    const bool popupHere = (bmIdx == m_bookmarkPopupIdx);
    const bool rangeActive = (bmIdx == m_inlineRangeBookmarkIdx);
    const bool stepperRangeActive = rangeActive &&
        kBookmarkRangeEditExperiment == BookmarkRangeEditExperiment::Stepper;
    const bool dragRangeActive = rangeActive &&
        kBookmarkRangeEditExperiment == BookmarkRangeEditExperiment::DragAdjust;

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

    auto paintRangeBg = [&](const QRect &br, bool hov, bool pres) {
        if (!br.isValid() || (!hov && !pres)) return;
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(pres ? pressedFill : hoverFill);
        painter.drawRoundedRect(QRectF(br).adjusted(-3, 0, 3, 0), 4, 4);
        painter.restore();
    };

    const bool offsetPressed = isPressedBookmark && m_pressedOnOffset;
    const bool lengthPressed = isPressedBookmark && m_pressedOnLength;
    const bool offsetHov = isHovered && (grabbed ? offsetPressed : m_hoverOnOffset);
    const bool lengthHov = isHovered && (grabbed ? lengthPressed : m_hoverOnLength);
    paintRangeBg(geom.offsetRect, offsetHov, offsetPressed);
    paintRangeBg(geom.lengthRect, lengthHov, lengthPressed);

    QRect activeValueRect;
    QRect activeStepperRect;
    QPainterPath activeFramePath;
    QColor activeFill;
    if (rangeActive) {
        activeValueRect = (m_inlineRangeField == BookmarkRangeField::Offset)
            ? geom.offsetRect
            : geom.lengthRect;
        activeStepperRect = stepperRangeActive ? bookmarkRangeStepperRect(geom, m_inlineRangeField) : QRect();
        const QRect valueFrame = stepperRangeActive
            ? activeValueRect.adjusted(-4, -2, 2, kRangeStepperGap + 2)
            : activeValueRect.adjusted(-4, -2, 2, 2);
        const QRect stepperFrame = stepperRangeActive && activeStepperRect.isValid()
            ? activeStepperRect.adjusted(-2, -2, 2, 2)
            : QRect();

        QPainterPath valuePath;
        valuePath.addRoundedRect(QRectF(valueFrame).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
        if (stepperFrame.isValid()) {
            QPainterPath stepperPath;
            stepperPath.addRoundedRect(QRectF(stepperFrame).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
            activeFramePath = valuePath.united(stepperPath);
        } else {
            activeFramePath = valuePath;
        }

        activeFill = QColor((bgCol.red() + 255) / 2,
                            (bgCol.green() + 255) / 2,
                            (bgCol.blue() + 255) / 2);

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(activeFill);
        painter.drawPath(activeFramePath);
        painter.restore();
    }

    const QColor activeRangeFg(Qt::black);
    painter.setPen(((stepperRangeActive || dragRangeActive) && m_inlineRangeField == BookmarkRangeField::Offset)
        ? activeRangeFg
        : rangeFg);
    painter.drawText(geom.offsetRect, Qt::AlignLeft | Qt::AlignTop, geom.offsetText);
    painter.setPen(((stepperRangeActive || dragRangeActive) && m_inlineRangeField == BookmarkRangeField::Length)
        ? activeRangeFg
        : rangeFg);
    painter.drawText(geom.lengthRect, Qt::AlignLeft | Qt::AlignTop, geom.lengthText);

    if (stepperRangeActive || dragRangeActive) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        QColor focusCol = palette().color(QPalette::Highlight);
        focusCol.setAlphaF(0.95);
        painter.setPen(QPen(focusCol, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(activeFramePath);
        painter.restore();

        const QRect stepper = activeStepperRect;
        if (stepperRangeActive && stepper.isValid()) {
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);
            const QPalette &pal = palette();
            const QColor btnBorder = pal.color(QPalette::Mid);

            const QColor hoverCol(0, 0, 0, 22);
            const QColor pressCol(0, 0, 0, 45);
            const QRect decRect(stepper.left(), stepper.top(), kRangeStepperButtonW, stepper.height());
            const QRect incRect(stepper.left() + kRangeStepperButtonW + 1, stepper.top(),
                                kRangeStepperButtonW, stepper.height());

            auto drawStepOverlay = [&](HitTestRegion ht, const QRect &br) {
                if (m_pressedInlineRangeStep != ht && m_hoverInlineRangeStep != ht)
                    return;
                painter.save();
                QPainterPath clip;
                clip.addRoundedRect(QRectF(stepper), 5, 5);
                painter.setClipPath(clip);
                painter.setPen(Qt::NoPen);
                painter.setBrush(m_pressedInlineRangeStep == ht ? pressCol : hoverCol);
                painter.drawRect(br);
                painter.restore();
            };
            drawStepOverlay(HVHT_BOOKMARK_RANGE_DEC, decRect);
            drawStepOverlay(HVHT_BOOKMARK_RANGE_INC, incRect);

            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setPen(btnBorder);
            const int divX = stepper.left() + kRangeStepperButtonW;
            painter.drawLine(divX, stepper.top() + 3, divX, stepper.bottom() - 3);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(activeRangeFg);
            painter.drawText(decRect, Qt::AlignCenter, QStringLiteral("-"));
            painter.drawText(incRect, Qt::AlignCenter, QStringLiteral("+"));
            painter.restore();
        }
    }

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
        const bool editPressed = isPressedBookmark && m_pressedOnEdit;
        const bool eHov  = grabbed ? editPressed : (m_hoverOnEdit || popupHere);
        const bool ePres = editPressed || popupHere;
        drawIconBtn(settingsButtonRect, eHov, ePres, QStringLiteral("document-edit-symbolic"));
    }

    // Close button (shown on hover).
    const QRect closeButtonRect = bookmarkButtonRect(geom, BookmarkButtonAction::Close);
    if (isHovered && closeButtonRect.isValid()) {
        const bool closePressed = isPressedBookmark && m_pressedOnClose;
        const bool cHov  = grabbed ? closePressed : m_hoverOnClose;
        const bool cPres = closePressed;
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
