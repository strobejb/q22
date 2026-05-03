#include "hexview.h"
#include "../theme.h"

#include <QApplication>
#include <QFontMetrics>
#include <QIcon>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextOption>

// Geometry constants — shared by noteStripGeom, drawNoteStrip, and the editor.
static constexpr int kNoteTriW   = 12;   // triangle depth (px)
static constexpr int kNotePadH   = 8;    // horizontal text padding (px)
static constexpr int kNotePadV   = 4;    // vertical padding (px)
static constexpr int kNoteRadius = 6;    // rounded corner radius
static constexpr int kNoteMaxW   = 220;  // max strip width (px)
static constexpr int kNoteBtnSz  = 14;   // close button icon size (px)
static constexpr int kNoteBtnGap = 4;    // gap between text and close button (px)

// ── Bookmark management ───────────────────────────────────────────────────────

void HexView::addBookmark(const Bookmark &bm)
{
    m_bookmarks.append(bm);
    viewport()->update();
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
    // Text width leaves room for the close button on the right.
    const int textW = rectW - 2 * kNotePadH - kNoteBtnSz - kNoteBtnGap;
    if (textW <= 0) return g;

    const QFontMetrics fm(QApplication::font());
    // While this bookmark is being edited use the live editor text so the
    // strip and editor resize together as the user adds/removes lines.
    const int bmIdx_ = (int)(&bm - m_bookmarks.constData());
    const bool isEditing = (bmIdx_ == m_noteEditorIdx && m_noteEditor && m_noteEditor->isVisible());
    const QString rawText = isEditing ? m_noteEditor->toPlainText() : bm.name;
    // Use a space as stand-in for empty text so height is never zero.
    const QString text = rawText.isEmpty() ? QStringLiteral(" ") : rawText;
    const QRect bounds = fm.boundingRect(QRect(0, 0, textW, 10000),
                                          Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                                          text);
    const int rectH = bounds.height() + 2 * kNotePadV;

    if (rectY + rectH <= 0 || rectY >= viewH) return g;

    // Close button: top-right corner with uniform padding.
    const int closeX = rectX + rectW - kNotePadV - kNoteBtnSz;
    const int closeY = rectY + kNotePadV;

    g.rect      = QRect(rectX, rectY, rectW, rectH);
    g.textRect  = QRect(rectX + kNotePadH, rectY + kNotePadV, textW, rectH - 2 * kNotePadV);
    g.closeRect = QRect(closeX, closeY, kNoteBtnSz, kNoteBtnSz);
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
    if (bm.fgColour) {
        fgCol = QColor(bm.fgColour);
    } else if (bm.colourIndex >= 0) {
        fgCol = realiseColour(HvColorSlot(HVC_BOOKMARK1_FG + bm.colourIndex));
    } else {
        const QColor bg  = realiseColour(HVC_BACKGROUND);
        const QColor asc = realiseColour(HVC_ASCII);
        const QColor &dark  = bg.lightness() <= asc.lightness() ? bg  : asc;
        const QColor &light = bg.lightness() <= asc.lightness() ? asc : bg;
        const int bmL = bgCol.lightness();
        fgCol = qAbs(bmL - dark.lightness()) >= qAbs(bmL - light.lightness()) ? dark : light;
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

    // Text: UI font, word-wrapped (clipped to text area, not over the button).
    painter.setFont(QApplication::font());
    painter.setPen(fgCol);
    painter.setClipRect(geom.textRect);
    painter.drawText(geom.textRect, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                     bm.name);

    // Close button — only shown while this strip is hovered.
    painter.setClipping(false);
    const int bmIdx = (int)(&bm - m_bookmarks.constData());
    if (bmIdx == m_hoverBookmarkIdx) {
        // Hover background behind the button icon.
        if (m_hoverOnClose) {
            const bool darkBg = bgCol.lightness() < 128;
            const QColor hoverFill(darkBg ? QColor(255, 255, 255, 50) : QColor(0, 0, 0, 35));
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            painter.setBrush(hoverFill);
            painter.drawRoundedRect(QRectF(geom.closeRect).adjusted(-2, -2, 2, 2), 4, 4);
        }

        QIcon icon = QIcon::fromTheme("window-close-symbolic");
        if (icon.isNull())
            icon = QIcon(":/icons/hicolor/scalable/actions/window-close-symbolic.svg");
        if (!icon.isNull()) {
            QPixmap src = icon.pixmap(kNoteBtnSz, kNoteBtnSz);
            QPixmap dst(src.size());
            dst.setDevicePixelRatio(src.devicePixelRatio());
            dst.fill(Qt::transparent);
            QPainter p2(&dst);
            p2.drawPixmap(0, 0, src);
            p2.setCompositionMode(QPainter::CompositionMode_SourceIn);
            p2.fillRect(dst.rect(), fgCol);
            painter.drawPixmap(geom.closeRect.topLeft(), dst);
        }
    }

    painter.restore();
}

// ── Inline note editor ────────────────────────────────────────────────────────

void HexView::openNoteEditor(int bmIdx)
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
            if (geom.valid && m_noteEditor->geometry() != geom.textRect) {
                m_noteEditor->setGeometry(geom.textRect);
                viewport()->update();
            }
        });
    }

    const QColor bg = bm.colourIndex >= 0
        ? QColor(getHexColour(HvColorSlot(HVC_BOOKMARK1 + bm.colourIndex)))
        : (bm.bgColour ? QColor(bm.bgColour) : QColor(getHexColour(HVC_BOOKMARK1)));
    QColor fg;
    if (bm.fgColour) {
        fg = QColor(bm.fgColour);
    } else if (bm.colourIndex >= 0) {
        fg = realiseColour(HvColorSlot(HVC_BOOKMARK1_FG + bm.colourIndex));
    } else {
        const QColor hvBG  = realiseColour(HVC_BACKGROUND);
        const QColor hvAsc = realiseColour(HVC_ASCII);
        const QColor &dark  = hvBG.lightness() <= hvAsc.lightness() ? hvBG  : hvAsc;
        const QColor &light = hvBG.lightness() <= hvAsc.lightness() ? hvAsc : hvBG;
        const int bmL = bg.lightness();
        fg = qAbs(bmL - dark.lightness()) >= qAbs(bmL - light.lightness()) ? dark : light;
    }

    m_noteEditor->setStyleSheet(QString(
        "QPlainTextEdit {"
        "  border: none; border-radius: 0; padding: 0; margin: 0;"
        "  background: %1; color: %2;"
        "}"
        "QPlainTextEdit:focus { border: none; padding: 0; }")
        .arg(bg.name()).arg(fg.name()));

    m_noteEditor->setFont(QApplication::font());
    m_noteEditor->setPlainText(bm.name);
    m_noteEditor->setGeometry(geom.textRect);
    m_noteEditor->show();
    m_noteEditor->raise();
    m_noteEditor->setFocus();
    m_noteEditor->moveCursor(QTextCursor::End);

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
