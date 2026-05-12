#include "palettecolourpicker.h"

#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QSizePolicy>

static constexpr int kHueHeight = 20;
static constexpr int kGap       =  8;
static constexpr int kRadius    =  4;

PaletteColourPicker::PaletteColourPicker(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QSize PaletteColourPicker::sizeHint() const
{
    return QSize(360, 200 + kGap + kHueHeight);
}

void PaletteColourPicker::setColor(const QColor &c)
{
    m_colour = c.isValid() ? c : Qt::black;
    update();
}

void PaletteColourPicker::paintEvent(QPaintEvent *)
{
    if (!isEnabled()) {
        const qreal dpr = devicePixelRatioF();
        QImage img(size() * dpr, QImage::Format_ARGB32);
        img.setDevicePixelRatio(dpr);
        img.fill(Qt::transparent);
        { QPainter pp(&img); drawContent(pp); }
        for (int y = 0; y < img.height(); ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const int g = 128 + (qGray(line[x]) >> 1);
                line[x] = qRgba(g, g, g, qAlpha(line[x]));
            }
        }
        QPainter p(this);
        p.drawImage(0, 0, img);
        return;
    }
    QPainter p(this);
    drawContent(p);
}

void PaletteColourPicker::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (svRectF().contains(e->position())) {
        m_zone = ZoneSV;
        pickSV(e->position());
    } else if (hueRectF().contains(e->position())) {
        m_zone = ZoneHue;
        pickHue(e->position());
    }
}

void PaletteColourPicker::mouseMoveEvent(QMouseEvent *e)
{
    if (m_zone == ZoneSV)
        pickSV(e->position());
    if (m_zone == ZoneHue)
        pickHue(e->position());
}

void PaletteColourPicker::mouseReleaseEvent(QMouseEvent *)
{
    m_zone = ZoneNone;
}

void PaletteColourPicker::drawContent(QPainter &p)
{
    const QRectF sv  = svRectF();
    const QRectF hr  = hueRectF();
    const qreal hueF = qBound(0.0, m_colour.hsvHueF(), 1.0);

    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath svPath;
    svPath.addRoundedRect(sv, kRadius, kRadius);

    QLinearGradient gS(sv.left(), 0, sv.right(), 0);
    gS.setColorAt(0.0, Qt::white);
    gS.setColorAt(1.0, QColor::fromHsvF(hueF, 1.0, 1.0));
    p.save();
    p.setClipPath(svPath);
    p.fillRect(sv, gS);

    QLinearGradient gV(0, sv.top(), 0, sv.bottom());
    gV.setColorAt(0.0, Qt::transparent);
    gV.setColorAt(1.0, Qt::black);
    p.fillRect(sv, gV);
    p.restore();

    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(sv.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);

    if (isEnabled()) {
        const qreal cx = sv.left() + qBound(0.0, m_colour.hsvSaturationF(), 1.0) * sv.width();
        const qreal cy = sv.top() + (1.0 - qBound(0.0, m_colour.valueF(), 1.0)) * sv.height();
        p.setPen(QPen(Qt::black, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(cx, cy), 7.0, 7.0);
        p.setPen(QPen(Qt::white, 1.5));
        p.drawEllipse(QPointF(cx, cy), 5.5, 5.5);
    }

    QLinearGradient gHue(hr.left(), 0, hr.right(), 0);
    for (int i = 0; i <= 12; ++i)
        gHue.setColorAt(i / 12.0, QColor::fromHsvF(i == 12 ? 0.9999 : i / 12.0, 1.0, 1.0));

    QPainterPath huePath;
    huePath.addRoundedRect(hr, kRadius, kRadius);
    p.fillPath(huePath, gHue);

    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(hr.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);

    if (isEnabled()) {
        const qreal hx = hr.left() + hueF * hr.width();
        p.setPen(QPen(Qt::black, 2.0));
        p.drawLine(QPointF(hx, hr.top() + 2), QPointF(hx, hr.bottom() - 2));
        p.setPen(QPen(Qt::white, 1.0));
        p.drawLine(QPointF(hx, hr.top() + 3), QPointF(hx, hr.bottom() - 3));
    }
}

QRectF PaletteColourPicker::svRectF() const
{
    return QRectF(0, 0, width(), height() - kGap - kHueHeight);
}

QRectF PaletteColourPicker::hueRectF() const
{
    return QRectF(0, height() - kHueHeight, width(), kHueHeight);
}

void PaletteColourPicker::pickSV(const QPointF &pos)
{
    const QRectF sv = svRectF();
    const qreal h = qBound(0.0, m_colour.hsvHueF(), 1.0);
    const qreal s = qBound(0.0, (pos.x() - sv.left()) / sv.width(), 1.0);
    const qreal v = qBound(0.0, 1.0 - (pos.y() - sv.top()) / sv.height(), 1.0);
    m_colour = QColor::fromHsvF(h, s, v);
    update();
    emit colourChanged(m_colour);
}

void PaletteColourPicker::pickHue(const QPointF &pos)
{
    const QRectF hr = hueRectF();
    const qreal h = qBound(0.0, (pos.x() - hr.left()) / hr.width(), 1.0);
    m_colour = QColor::fromHsvF(h, qBound(0.0, m_colour.hsvSaturationF(), 1.0),
                                qBound(0.0, m_colour.valueF(), 1.0));
    update();
    emit colourChanged(m_colour);
}
