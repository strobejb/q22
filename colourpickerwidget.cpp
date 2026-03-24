#include "colourpickerwidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QApplication>

static const QVector<QColor> kDefaultColours = {
    // Row 1 — light backgrounds
    QColor("#ffffff"), QColor("#ffd5d5"), QColor("#ffd6a5"), QColor("#ffffa5"),
    QColor("#d4f5a5"), QColor("#a5e8f5"), QColor("#d5a5f5"),
    // Row 2 — mid backgrounds
    QColor("#cccccc"), QColor("#ff8080"), QColor("#ffa040"), QColor("#ffff40"),
    QColor("#80e040"), QColor("#40c8e0"), QColor("#a040e0"),
    // Row 3 — dark backgrounds
    QColor("#555555"), QColor("#a01010"), QColor("#a05000"), QColor("#a0a000"),
    QColor("#208010"), QColor("#107090"), QColor("#601090"),
};

ColourPickerWidget::ColourPickerWidget(QWidget *parent)
    : QWidget(parent)
    , m_colours(kDefaultColours)
    , m_foreground(QApplication::palette().text().color())
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ColourPickerWidget::setColumns(int cols)
{
    if (cols < 1) cols = 1;
    m_columns = cols;
    updateGeometry();
    update();
}

void ColourPickerWidget::setForegroundColour(const QColor &fg)
{
    m_foreground = fg;
    update();
}

void ColourPickerWidget::setSelectedColour(const QColor &colour)
{
    m_selectedIndex = m_colours.indexOf(colour);
    update();
}

QColor ColourPickerWidget::selectedColour() const
{
    if (m_selectedIndex >= 0 && m_selectedIndex < m_colours.size())
        return m_colours[m_selectedIndex];
    return {};
}

int ColourPickerWidget::cellSize() const
{
    if (m_columns <= 0) return 1;
    return width() / m_columns;
}

QSize ColourPickerWidget::sizeHint() const
{
    const int cell = (parentWidget() ? parentWidget()->width() / m_columns : 38);
    const int rows = (m_colours.size() + m_columns - 1) / m_columns;
    return QSize(cell * m_columns, cell * rows);
}

int ColourPickerWidget::indexAt(const QPoint &pos) const
{
    const int cell = cellSize();
    if (cell <= 0) return -1;
    const int col = pos.x() / cell;
    const int row = pos.y() / cell;
    if (col >= m_columns) return -1;
    const int idx = row * m_columns + col;
    return (idx < m_colours.size()) ? idx : -1;
}

void ColourPickerWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int cell = cellSize();
    if (cell <= 0) return;

    const int rows = (m_colours.size() + m_columns - 1) / m_columns;

    for (int i = 0; i < m_colours.size(); ++i) {
        const int col = i % m_columns;
        const int row = i / m_columns;
        const QRect r(col * cell, row * cell, cell, cell);

        // Fill background
        p.fillRect(r, m_colours[i]);

        // Draw "Aa" text
        p.setPen(m_foreground);
        p.setFont(QApplication::font());
        p.drawText(r, Qt::AlignCenter, QStringLiteral("Aa"));

        // Draw selection border
        if (i == m_selectedIndex) {
            p.setPen(QPen(QApplication::palette().highlightedText().color(), 2));
            p.drawRect(r.adjusted(1, 1, -2, -2));
            p.setPen(QPen(QApplication::palette().highlight().color(), 2));
            p.drawRect(r.adjusted(2, 2, -3, -3));
        }
    }

    // Draw outer grid lines
    p.setPen(QPen(QApplication::palette().mid().color(), 1));
    for (int c = 0; c <= m_columns && c * cell <= width(); ++c)
        p.drawLine(c * cell, 0, c * cell, rows * cell);
    for (int r = 0; r <= rows; ++r)
        p.drawLine(0, r * cell, m_columns * cell, r * cell);
}

void ColourPickerWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    const int idx = indexAt(event->pos());
    if (idx < 0) return;
    m_selectedIndex = idx;
    update();
    emit colourSelected(m_colours[idx]);
}

void ColourPickerWidget::resizeEvent(QResizeEvent *)
{
    updateGeometry();
    update();
}
