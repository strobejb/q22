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

void HexView::setBookmarks(const QList<Bookmark> &bookmarks)
{
    closeNoteEditor(false);
    m_bookmarks = bookmarks;
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
    m_bookmarks.insert(pos, bm);
    setupScrollbars();
    viewport()->update();
    emit bookmarksChanged();
}

void HexView::removeBookmark(int idx)
{
    if (idx < 0 || idx >= m_bookmarks.size()) return;
    closeNoteEditor(false);
    m_bookmarks.removeAt(idx);
    setupScrollbars();
    viewport()->update();
    emit bookmarksChanged();
}

void HexView::replaceBookmark(int idx, const Bookmark &bm)
{
    if (idx < 0 || idx >= m_bookmarks.size()) return;
    closeNoteEditor(false);  // sets m_noteEditorIdx = -1 before we re-sort
    m_bookmarks[idx] = bm;
    // Re-sort in case the replacement bookmark has a different offset
    // (e.g. editing offset via the bookmark dialog).
    std::sort(m_bookmarks.begin(), m_bookmarks.end(), bmOffsetLess);
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
    const int rectY = ny + kNotePadV;

    const int rectW = std::min(kNoteMaxW, viewW - rectX);
    // Text width leaves room for the gear button on the right.
    const int textW = rectW - 2 * kNotePadH - kNoteBtnSz - kNoteBtnGap;
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
    const QString rawText = isEditing ? m_noteEditor->toPlainText() : bm.name;
    // Use a space as stand-in for empty text so height is never zero.
    const QString text = rawText.isEmpty() ? QStringLiteral(" ") : rawText;
    QTextDocument *textDoc = makeNoteTextDoc(text, textW, nf);
    const int textH = qRound(textDoc->size().height());
    delete textDoc;

    const int rectH = kNotePadV + textH + kNoteRangePad + rangeH + kNotePadV;
    if (rectY + rectH <= 0 || rectY >= viewH) return g;

    const int rangeY = rectY + kNotePadV + textH + kNoteRangePad;

    g.rect      = QRect(rectX, rectY, rectW, rectH);
    g.textRect  = QRect(rectX + kNotePadH, rectY + kNotePadV, textW, textH);
    g.rangeRect = QRect(rectX + kNotePadH, rangeY, textW, rangeH);
    g.rangeText = rangeText;
    g.editRect  = QRect(btnX, rectY + kNotePadV,                    kNoteBtnSz, kNoteBtnSz);
    g.closeRect = QRect(btnX, rangeY + (rangeH - kNoteBtnSz) / 2,   kNoteBtnSz, kNoteBtnSz);
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

    const int rectW = std::min(kNoteMaxW, viewW - rectX);
    const int textW = rectW - 2 * kNotePadH - kNoteBtnSz - kNoteBtnGap;
    if (textW <= 0) return QRect();

    const size_w startLine = bm.offset / (size_w)m_nBytesPerLine;
    const int ny   = (int)((qint64)startLine - (qint64)m_nVScrollPos) * m_nFontHeight;
    const int rectY = ny + kNotePadV;

    const QFontMetrics fm(QApplication::font());
    const int rangeH = fm.height();
    const int rectH  = kNotePadV + rangeH + kNotePadV;

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

    const int rectW = std::min(kNoteMaxW, viewW - rectX);
    const int textW = rectW - 2 * kNotePadH - kNoteBtnSz - kNoteBtnGap;
    if (textW <= 0) return m_nFontHeight;

    const QFontMetrics rangeFm(QApplication::font());
    const int rangeH = rangeFm.height();

    // Use live editor text while this bookmark is being edited so that
    // computeBookmarkLayout() sees the current height, not the last-saved height.
    const int bmIdx_ = (int)(&bm - m_bookmarks.constData());
    const bool isEditing = (bmIdx_ == m_noteEditorIdx && m_noteEditor && m_noteEditor->isVisible());
    const QString rawText = isEditing ? m_noteEditor->toPlainText() : bm.name;
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
// Within each conflict group the "active" member (shown as a full strip) is
// determined by the current caret position (m_nCursorOffset):
//   • If the caret falls inside exactly one group member's byte range, that
//     member is active.
//   • If the caret falls inside multiple members (nested bookmarks), the
//     shortest-length one is chosen (most specific).
//   • If the caret is inside none of them, the first member is shown as the
//     default (so there is always one full strip visible per group when any
//     of its bookmarks are on screen).
// All non-active members of a conflict group are drawn as thin tabs.
QVector<HexView::BmLayout> HexView::computeBookmarkLayout() const
{
    const int n = m_bookmarks.size();
    QVector<BmLayout> layout(n);    // default: inConflict=false, isActive=true
    if (n == 0 || m_nBytesPerLine <= 0 || m_nFontHeight <= 0)
        return layout;

    // Height of a single-line collapsed strip — used for overlap detection below.
    // kNotePadV + one text line + kNotePadV.
    const int collapsedH = kNotePadV + QFontMetrics(QApplication::font()).height() + kNotePadV;

    // Precompute display start-Y and full visual height for each bookmark.
    QVarLengthArray<int, 32> sy(n), fh(n);
    for (int i = 0; i < n; ++i) {
        const int line = (int)(m_bookmarks[i].offset / (size_w)m_nBytesPerLine);
        sy[i] = (line - (int)m_nVScrollPos) * m_nFontHeight;
        fh[i] = noteStripFullHeight(m_bookmarks[i]);
    }

    // Sweep: find maximal conflict groups using full heights for all members.
    int i = 0;
    while (i < n) {
        int end      = i + 1;
        int groupBot = sy[i] + fh[i];
        while (end < n && sy[end] < groupBot) {
            groupBot = std::max(groupBot, sy[end] + fh[end]);
            ++end;
        }

        if (end - i == 1) {
            // Single bookmark — no conflict, always active, never hidden.
            layout[i] = {false, true, false};
        } else {
            // Conflict group [i, end).
            // Determine the active (full-strip) winner:
            //   1. If m_pinnedBookmarkIdx is in this group AND the cursor is still
            //      within the pinned bookmark's byte range, the pin wins outright.
            //      This lets the caller force a specific bookmark visible even when
            //      multiple bookmarks share the same start offset.
            //   2. Otherwise: whichever group member contains the caret wins, with
            //      shortest-length breaking ties (most specific range).
            //   3. If the caret is outside all members, no winner — all collapse.
            int    winnerIdx = -1;
            size_w winnerLen = (size_w)-1;

            // While a mouse button is held (drag-select or drag-drop) we freeze
            // the bookmark display: keep the current pin alive regardless of where
            // the cursor has moved, and skip cursor-based selection entirely.
            // On mouse-release the repaint fires with no buttons held and both
            // paths run normally against the final m_nCursorOffset.
            const bool mouseHeld = QApplication::mouseButtons() != Qt::NoButton;

            // Pin check: overrides cursor logic when the pin is live.
            // During drag the range check is suspended so the pin doesn't expire.
            if (m_pinnedBookmarkIdx >= i && m_pinnedBookmarkIdx < end) {
                const Bookmark &p = m_bookmarks[m_pinnedBookmarkIdx];
                if (mouseHeld ||
                    (m_nCursorOffset >= p.offset && m_nCursorOffset < p.offset + p.length))
                    winnerIdx = m_pinnedBookmarkIdx;
            }

            // Cursor-based fallback — only when no button is held.
            if (winnerIdx == -1 && !mouseHeld) {
                for (int j = i; j < end; ++j) {
                    const Bookmark &bm = m_bookmarks[j];
                    if (m_nCursorOffset >= bm.offset &&
                        m_nCursorOffset <  bm.offset + bm.length) {
                        if (bm.length < winnerLen) {
                            winnerIdx = j;
                            winnerLen = bm.length;
                        }
                    }
                }
            }

            // For non-active members: hide those whose collapsed strip would
            // overlap the active full strip.  When there is no active member
            // (cursor outside the group) nothing is hidden.
            const int activeTop = (winnerIdx >= 0) ? sy[winnerIdx]              : INT_MIN;
            const int activeBot = (winnerIdx >= 0) ? sy[winnerIdx] + fh[winnerIdx] : INT_MIN;

            for (int j = i; j < end; ++j) {
                const bool active = (winnerIdx != -1 && j == winnerIdx);
                bool hidden = false;
                if (!active && winnerIdx >= 0) {
                    // Collapsed strip for bookmark j spans [sy[j], sy[j]+collapsedH].
                    // Hide it if that range intersects the active full strip.
                    hidden = (sy[j] < activeBot) && (sy[j] + collapsedH > activeTop);
                }
                layout[j] = {true, active, hidden};
            }
        }
        i = end;
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

    // Rounded rect + triangle pointer at left-middle.
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    path.addRoundedRect(QRectF(r), kNoteRadius, kNoteRadius);
    const qreal mid  = r.top() + r.height() / 2.0;
    const qreal triH = m_nFontHeight;
    QPolygonF tri;
    tri << QPointF(r.left() - kNoteTriW, mid)
        << QPointF(r.left(),             mid - triH / 2.0)
        << QPointF(r.left(),             mid + triH / 2.0);
    path.addPolygon(tri);
    painter.fillPath(path, bgCol);

    // Text content.
    {
        QTextDocument *doc = makeNoteTextDoc(bm.name, geom.textRect.width(), noteFont());
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

    if (isHovered) {
        const bool eHov  = grabbed ? m_pressedOnEdit : (m_hoverOnEdit || popupHere);
        const bool ePres = m_pressedOnEdit || popupHere;
        drawIconBtn(geom.editRect, eHov, ePres, QStringLiteral("document-edit-symbolic"));
    }

    // Close button (shown on hover).
    if (isHovered && geom.closeRect.isValid()) {
        const bool cHov  = grabbed ? m_pressedOnClose : m_hoverOnClose;
        const bool cPres = m_pressedOnClose;
        paintBtnBg(geom.closeRect, cHov, cPres);
        // Draw an × using two short lines.
        painter.save();
        QColor xCol = fgCol;
        xCol.setAlphaF((cHov || cPres) ? 0.85 : 0.50);
        painter.setPen(QPen(xCol, 1.5));
        const QRectF br = geom.closeRect;
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
    pinBookmark(bmIdx);

    // Guarantee the caret is inside this bookmark's byte range so
    // computeBookmarkLayout() sees it as the cursor-based winner too.
    // Navigate there now so the full strip appears on the first paint.
    if (m_nCursorOffset < bm.offset || m_nCursorOffset >= bm.offset + bm.length) {
        m_nCursorOffset     = bm.offset;
        m_nSelectionEnd     = bm.offset;
        m_nSelectionMode    = SEL_NONE;
        m_fCursorAdjustment = false;
        scrollCenterIfOffScreen(bm.offset);
        int cx, cy;
        caretPosFromOffset(bm.offset, &cx, &cy);
        positionCaret(cx, cy, m_nWhichPane);
        emit cursorChanged(bm.offset);
    }
    const NoteStripGeom geom = noteStripGeom(bm);
    if (!geom.valid) return;

    m_noteEditorIdx   = bmIdx;
    m_noteEditorIsNew = bm.name.isEmpty();  // flag for Escape-cancels-new-bookmark logic

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
    m_noteEditor->setPlainText(bm.name);
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

void HexView::closeNoteEditor(bool save)
{
    if (!m_noteEditor || !m_noteEditor->isVisible()) return;
    // Capture focus state before hide() fires a synchronous FocusOut that
    // would call us recursively (the re-entrant call returns early via isVisible()).
    const bool hadFocus = m_noteEditor->hasFocus()
                       || m_noteEditor->viewport()->hasFocus();
    m_noteEditor->hide();
    if (save && m_noteEditorIdx >= 0 && m_noteEditorIdx < m_bookmarks.size())
        m_bookmarks[m_noteEditorIdx].name = m_noteEditor->toPlainText();
    m_noteEditorIdx = -1;
    // If the editor owned focus, return it to the hex view in the ASCII pane.
    // Skip when FocusOut triggered us — focus is already moving elsewhere.
    if (hadFocus) {
        setActivePane(1);
        viewport()->setFocus();
    }
    viewport()->update();
}
