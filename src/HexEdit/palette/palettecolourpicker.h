#ifndef PALETTECOLOURPICKER_H
#define PALETTECOLOURPICKER_H

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QWidget>

class QMouseEvent;
class QPainter;
class QPaintEvent;

class PaletteColourPicker : public QWidget
{
    Q_OBJECT
public:
    explicit PaletteColourPicker(QWidget *parent = nullptr);

    QSize sizeHint() const override;

    QColor color() const { return m_colour; }
    void setColor(const QColor &c);

signals:
    void colourChanged(const QColor &c);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    enum Zone { ZoneNone, ZoneSV, ZoneHue };

    void drawContent(QPainter &p);
    QRectF svRectF() const;
    QRectF hueRectF() const;
    void pickSV(const QPointF &pos);
    void pickHue(const QPointF &pos);

    QColor m_colour = Qt::red;
    Zone   m_zone   = ZoneNone;
};

#endif // PALETTECOLOURPICKER_H
