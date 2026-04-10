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

void ColourPickerWidget::setColours(const QVector<QColor> &colours)
{
    m_colours = colours;
    m_selectedIndex = -1;
    updateGeometry();
    update();
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

// Pixels between each swatch edge and its cell boundary.
// Adjacent swatches are separated by 2*SWATCH_PAD; the selection ring (3px)
// fits in that gap with 1px to spare on each side.
static constexpr int SWATCH_PAD = 4;

int ColourPickerWidget::cellSize() const
{
    if (m_columns <= 0) return 1;
    return (width() - 2 * SWATCH_PAD) / m_columns;
}

QSize ColourPickerWidget::sizeHint() const
{
    const int w    = width() > 0 ? width() : (m_columns * 38 + 2 * SWATCH_PAD);
    const int cell = qMax(1, (w - 2 * SWATCH_PAD) / m_columns);
    const int rows = (m_colours.size() + m_columns - 1) / m_columns;
    return QSize(w, cell * rows + 2 * SWATCH_PAD);
}

int ColourPickerWidget::indexAt(const QPoint &pos) const
{
    const int cell = cellSize();
    if (cell <= 0) return -1;
    const QPoint p = pos - QPoint(SWATCH_PAD, SWATCH_PAD);
    if (p.x() < 0 || p.y() < 0) return -1;
    const int col = p.x() / cell;
    const int row = p.y() / cell;
    if (col >= m_columns) return -1;
    const int idx = row * m_columns + col;
    return (idx < m_colours.size()) ? idx : -1;
}

void ColourPickerWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int cell = cellSize();
    if (cell <= 0) return;

    const int rows = (m_colours.size() + m_columns - 1) / m_columns;

    // Pass 1: draw all swatches as circles inset within their cells
    for (int i = 0; i < m_colours.size(); ++i) {
        const int col = i % m_columns;
        const int row = i / m_columns;
        const QRect cellR(SWATCH_PAD + col * cell, SWATCH_PAD + row * cell, cell, cell);
        const QRect sw = cellR.adjusted(SWATCH_PAD, SWATCH_PAD, -SWATCH_PAD, -SWATCH_PAD);

        p.setPen(Qt::NoPen);
        p.setBrush(m_colours[i]);
        p.drawEllipse(sw);

        p.setPen(QPen(QColor(0, 0, 0, 60), 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(sw);
    }

    // Pass 2: 2px highlight circle around the selected swatch
    if (m_selectedIndex >= 0 && m_selectedIndex < m_colours.size()) {
        const int col = m_selectedIndex % m_columns;
        const int row = m_selectedIndex / m_columns;
        const QRect cellR(SWATCH_PAD + col * cell, SWATCH_PAD + row * cell, cell, cell);
        const QRect sw = cellR.adjusted(SWATCH_PAD, SWATCH_PAD, -SWATCH_PAD, -SWATCH_PAD);

        p.setPen(QPen(QApplication::palette().highlight().color(), 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(sw.adjusted(-2, -2, 2, 2));
    }
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
