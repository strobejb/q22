#include "hexview.h"
#include "theme.h"

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

void HexView::setBookmarks(const QList<Bookmark> &bookmarks)
{
    closeNoteEditor(false);
    m_bookmarks = bookmarks;
    setupScrollbars();
    viewport()->update();
    emit bookmarksChanged();
}

void HexView::addBookmark(const Bookmark &bm)
{
    m_bookmarks.append(bm);
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
    closeNoteEditor(false);
    m_bookmarks[idx] = bm;
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
    // Text width leaves room for the two stacked buttons (close + edit) on the right.
    const int textW = rectW - 2 * kNotePadH - kNoteBtnSz - kNoteBtnGap;
    if (textW <= 0) return g;

    const QFont nf = noteFont();
    const QFontMetrics fm(nf);
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

    // Range label: "0xADDR  (N bytes)" or just "0xADDR" for single-byte bookmarks.
    // Width matches textW so the close button (bottom-right) doesn't overlap it.
    const QString hexAddr = QStringLiteral("0x") + QString::number(bm.offset, 16).toUpper();
    QString rangeText;
    if (bm.length <= 1) {
        rangeText = hexAddr;
    } else {
        rangeText = hexAddr + QStringLiteral("  (") +
                    QString::number(bm.length) + QStringLiteral(" bytes)");
    }
    const QRect rangeBounds = fm.boundingRect(QRect(0, 0, textW, 10000),
                                               Qt::AlignLeft | Qt::AlignTop, rangeText);
    const int rangeH = rangeBounds.height();

    const int rectH = kNotePadV + textH + kNoteRangePad + rangeH + kNotePadV;

    if (rectY + rectH <= 0 || rectY >= viewH) return g;

    // Edit button: top-right.  Close (delete) button: bottom-right, beside range label.
    const int btnX   = rectX + rectW - kNotePadV - kNoteBtnSz;
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

// ── drawNoteStrip ─────────────────────────────────────────────────────────────

void HexView::drawNoteStrip(QPainter &painter, int /*asciiRight*/, int /*ny*/,
                             const Bookmark &bm)
{
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

    // Text: render via the same QTextDocument engine used for measurement so
    // the line positions in the painted strip exactly match the editor.
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

    // Range label: dimmed foreground, single line, below the note text.
    painter.setFont(noteFont());
    QColor rangeFg = fgCol;
    rangeFg.setAlphaF(0.55);
    painter.setPen(rangeFg);
    painter.drawText(geom.rangeRect, Qt::AlignLeft | Qt::AlignTop, geom.rangeText);

    // Overlay buttons — only shown while this strip is hovered.
    const int bmIdx = (int)(&bm - m_bookmarks.constData());
    if (bmIdx == m_hoverBookmarkIdx) {
        const bool darkBg = bgCol.lightness() < 128;
        const QColor hoverFill  (darkBg ? QColor(255, 255, 255,  50) : QColor(0, 0, 0,  35));
        const QColor pressedFill(darkBg ? QColor(255, 255, 255,  90) : QColor(0, 0, 0,  65));

        auto drawBtn = [&](const QRect &btnRect, bool hovered, bool pressed, const QString &iconName) {
            if (hovered || pressed) {
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setPen(Qt::NoPen);
                painter.setBrush(pressed ? pressedFill : hoverFill);
                painter.drawRoundedRect(QRectF(btnRect).adjusted(-2, -2, 2, 2), 4, 4);
            }
            QIcon icon = QIcon::fromTheme(iconName);
            if (icon.isNull())
                icon = QIcon(QStringLiteral(":/icons/hicolor/scalable/actions/") + iconName + QStringLiteral(".svg"));
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
        };

        // With grabMouse active during a press, m_pressedOn* tracks the mouse reliably
        // even outside the viewport, so it can double as the hover source.
        // When no grab is active the normal m_hoverOn* state is used.
        const bool grabbed        = (QWidget::mouseGrabber() == viewport());
        const bool showHoverClose = grabbed ? m_pressedOnClose : m_hoverOnClose;
        // Keep the gear button in its pressed state while its settings popup is open.
        const bool popupOpenHere  = (bmIdx == m_bookmarkPopupIdx);
        const bool showHoverEdit  = grabbed ? m_pressedOnEdit : (m_hoverOnEdit || popupOpenHere);
        //drawBtn(geom.closeRect, showHoverClose, m_pressedOnClose, QStringLiteral("window-close-symbolic"));
        //drawBtn(geom.editRect,  showHoverEdit,  m_pressedOnEdit,  QStringLiteral("document-edit-symbolic"));
        drawBtn(geom.editRect, showHoverEdit, m_pressedOnEdit || popupOpenHere,
                QStringLiteral("document-edit-symbolic"));
    }

    painter.restore();
}

// ── Inline note editor ────────────────────────────────────────────────────────

void HexView::openNoteEditor(int bmIdx, QPoint clickPos)
{
    if (bmIdx < 0 || bmIdx >= m_bookmarks.size()) return;
    closeNoteEditor(true);

    const Bookmark &bm       = m_bookmarks[bmIdx];
    const NoteStripGeom geom = noteStripGeom(bm);
    if (!geom.valid) return;

    m_noteEditorIdx = bmIdx;

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
    // Hide before saving so FocusOut doesn't re-enter.
    m_noteEditor->hide();
    if (save && m_noteEditorIdx >= 0 && m_noteEditorIdx < m_bookmarks.size())
        m_bookmarks[m_noteEditorIdx].name = m_noteEditor->toPlainText();
    m_noteEditorIdx = -1;
    viewport()->update();
}
