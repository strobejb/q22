#include "hexview.h"

#include <QPainter>
#include <QPainterPath>

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

void HexView::drawNoteStrip(QPainter &painter, int asciiRight, int ny, const Bookmark &bm)
{
    const int viewWidth = viewport()->width();

    // Geometry constants
    const int kGap    = m_nFontWidth * 4;           // gap between ASCII right edge and triangle tip
    const int kTriW   = 12;                          // triangle depth (how far left tip extends)
    const int kTriH   = m_nFontHeight ;          // triangle base height along rect left edge
    const int kPadH   = m_nFontWidth / 2;           // horizontal text padding inside rect
    const int kPadV   = 4;                          // vertical padding inside rect
    const int kRadius = 6;                          // rounded rect corner radius

    // Left edge of the rounded rectangle (triangle tip is kTriW further left)
    const int rectX = asciiRight + kGap + kTriW;
    if (rectX >= viewWidth)
        return;

    const QFontMetrics fm(m_font);

    // Height spans all lines covered by the bookmark
    const size_w startLine = bm.offset / (size_w)m_nBytesPerLine;
    const size_w endLine   = bm.length > 0
        ? (bm.offset + bm.length - 1) / (size_w)m_nBytesPerLine
        : startLine;
    const int numLines = (int)(endLine - startLine) + 1;

    const int rectY = ny + kPadV;
    const int rectH = std::min(numLines * m_nFontHeight - 2 * kPadV,
                               viewport()->height() - rectY);
    if (rectH <= 0)
        return;

    const int rectW = std::min(std::max(kPadH + fm.horizontalAdvance(bm.name) + kPadH, 100),
                               viewWidth - rectX);
    if (rectW <= 0)
        return;

    const QColor bgCol = bm.colourIndex >= 0
        ? QColor(getHexColour(HvColorSlot(HVC_BOOKMARK1 + bm.colourIndex)))
        : (bm.bgColour ? QColor(bm.bgColour) : QColor(getHexColour(HVC_BOOKMARK1)));
    const QColor fgCol = bm.fgColour ? QColor(bm.fgColour) : QColor(getHexColour(HVC_BOOKSEL));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    // Combined path: rounded rect + pointer triangle at top-left.
    // Triangle: tip at (rectX - kTriW, rectY), base from (rectX, rectY) to (rectX, rectY + kTriH).
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    path.addRoundedRect(QRectF(rectX, rectY, rectW, rectH), kRadius, kRadius);
    QPolygonF tri;
    tri << QPointF(rectX - kTriW, rectY + rectH/2)
        << QPointF(rectX, rectY+ rectH/2 - kTriH/2)
        << QPointF(rectX, rectY + rectH/2 + kTriH/2);
    path.addPolygon(tri);
    painter.fillPath(path, bgCol);

    // Draw bookmark name clipped to the rect interior, vertically centred in the first line
    if (rectW > kPadH) {
        const int firstLineH = std::min(m_nFontHeight - 2 * kPadV, rectH);
        painter.setClipRect(QRectF(rectX, rectY, rectW, rectH));
        painter.setPen(fgCol);
        painter.setFont(m_font);
        painter.drawText(QPointF(rectX + kPadH,
                                 rectY + (firstLineH - fm.height()) / 2.0 + fm.ascent()),
                         bm.name);
    }

    painter.restore();
}
