#include "togglebuttongroup.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QVariantAnimation>

ToggleButtonGroup::ToggleButtonGroup(const QList<QIcon> &icons, QWidget *parent)
    : QWidget(parent), m_indicatorX(kPad), m_icons(icons)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(150);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_indicatorX = v.toReal();
        update();
    });
}

void ToggleButtonGroup::addButton(const QIcon &icon)
{
    m_icons.append(icon);
    updateGeometry();
    update();
}

void ToggleButtonGroup::setMode(int mode)
{
    if (mode < 0 || mode >= m_icons.size()) return;
    if (mode == m_mode) return;
    m_mode = mode;
    emit modeChanged(mode);
    m_anim->stop();
    m_anim->setStartValue(m_indicatorX);
    m_anim->setEndValue(qreal(kPad + mode * slotWidth()));
    m_anim->start();
    update();
}

QSize ToggleButtonGroup::sizeHint() const
{
    return QSize(m_icons.size() * slotWidth() + 2 * kPad, kSlotH + 2 * kPad);
}

void ToggleButtonGroup::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPalette &pal = qApp->palette();
    const bool dark = pal.window().color().lightness() < 128;

    p.setBrush(pal.button().color());
    p.setPen(QPen(pal.mid().color(), 1));
    p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1, height() - 1), kRadius, kRadius);

    if (!m_icons.isEmpty()) {
        const QColor indFill   = pal.highlight().color();
        const QColor indBorder = dark ? QColor(0, 0, 0, 30) : QColor(255, 255, 255, 20);
        p.setBrush(indFill);
        p.setPen(QPen(indBorder, 1));
        p.drawRoundedRect(QRectF(m_indicatorX, kPad, slotWidth(), kSlotH), kInnerR, kInnerR);
    }

    if (m_hovered >= 0 && m_hovered != m_mode) {
        p.setBrush(pal.mid().color());
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(kPad + m_hovered * slotWidth(), kPad, slotWidth(), kSlotH),
                          kInnerR, kInnerR);
    }

    const QColor normalCol   = pal.buttonText().color();
    const QColor selectedCol = pal.highlightedText().color();
    for (int i = 0; i < m_icons.size(); ++i) {
        const QColor col = (i == m_mode) ? selectedCol : normalCol;
        const QIcon icon = recolored(m_icons.at(i), col);
        const int cx = kPad + i * slotWidth() + slotWidth() / 2;
        const int cy = kPad + kSlotH / 2;
        icon.paint(&p, QRect(cx - kIconSz / 2, cy - kIconSz / 2, kIconSz, kIconSz));
    }
}

void ToggleButtonGroup::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        m_pressed = slotAt(e->pos());
}

void ToggleButtonGroup::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_pressed >= 0) {
        if (slotAt(e->pos()) == m_pressed)
            setMode(m_pressed);
        m_pressed = -1;
    }
}

void ToggleButtonGroup::mouseMoveEvent(QMouseEvent *e)
{
    const int hov = slotAt(e->pos());
    if (hov != m_hovered) { m_hovered = hov; update(); }
}

void ToggleButtonGroup::leaveEvent(QEvent *)
{
    m_hovered = -1;
    update();
}

int ToggleButtonGroup::slotAt(const QPoint &pos) const
{
    if (m_icons.isEmpty()) return -1;
    if (pos.x() < kPad || pos.x() >= width() - kPad) return -1;
    if (pos.y() < kPad || pos.y() >= height() - kPad) return -1;
    return qBound(0, (pos.x() - kPad) / slotWidth(), m_icons.size() - 1);
}

QIcon ToggleButtonGroup::recolored(const QIcon &icon, const QColor &color) const
{
    if (icon.isNull()) return icon;

    const QPixmap src = icon.pixmap(kIconSz, kIconSz);
    QPixmap dst(src.size());
    dst.setDevicePixelRatio(src.devicePixelRatio());
    dst.fill(Qt::transparent);

    QPainter p(&dst);
    p.drawPixmap(0, 0, src);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(dst.rect(), color);
    return QIcon(dst);
}
