#include "themepicker.h"

#include <QApplication>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

// ─── ThemePickerWidget ────────────────────────────────────────────────────────

ThemePickerWidget::ThemePickerWidget(ColorScheme current,
                                     std::function<void(ColorScheme)> cb,
                                     QWidget *parent)
    : QWidget(parent), m_current(current), m_cb(std::move(cb))
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

QSize ThemePickerWidget::sizeHint() const
{
    // Height drives d() — set it so circles come out TARGET_D tall.
    // Width is just a sensible minimum; the menu stretches us to its width.
    return QSize(TARGET_D * 3 + GAP * 2 + TARGET_D, VPAD + TARGET_D + VPAD);
}

void ThemePickerWidget::setCurrent(ColorScheme s)
{
    m_current = s;
    update();
}

void ThemePickerWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPalette &pal  = palette();
    const QColor    bord = pal.color(QPalette::Mid);
    const QColor    sel  = pal.color(QPalette::Highlight);

    static const QColor sLight("#f8f8f8");
    static const QColor sDark ("#242424");

    for (int i = 0; i < 3; ++i) {
        const QRectF cr   = QRectF(circleRect(i)).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool   hov  = (m_hovered == i);
        const bool   curr = (static_cast<int>(m_current) == i);

        if (i == 1) {
            // System — left half dark, right half light
            p.setPen(Qt::NoPen);
            QPainterPath clip;
            clip.addEllipse(cr);
            p.setClipPath(clip);
            p.setBrush(sDark);
            p.drawRect(QRectF(cr.left(), cr.top(), cr.width() / 2, cr.height()));
            p.setBrush(sLight);
            p.drawRect(QRectF(cr.left() + cr.width() / 2, cr.top(),
                              cr.width() / 2, cr.height()));
            p.setClipping(false);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(i == 0 ? sLight : sDark);
            p.drawEllipse(cr);
        }

        // Border: accent if selected, hover-lightened if hovered, normal otherwise
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(curr ? sel : (hov ? bord.lighter(140) : bord),
                      curr ? 2.5 : 1.0));
        p.drawEllipse(cr);
    }
}

bool ThemePickerWidget::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        const int h = hitCircle(he->pos());
        static const char *tips[] = { "Light", "System", "Dark" };
        if (h >= 0)
            QToolTip::showText(he->globalPos(), tips[h], this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

void ThemePickerWidget::mouseMoveEvent(QMouseEvent *e)
{
    const int h = hitCircle(e->pos());
    if (h != m_hovered) { m_hovered = h; update(); }
}

void ThemePickerWidget::leaveEvent(QEvent *)
{
    if (m_hovered != -1) { m_hovered = -1; update(); }
}

void ThemePickerWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    const int h = hitCircle(e->pos());
    if (h >= 0) {
        m_current = static_cast<ColorScheme>(h);
        update();
        m_cb(m_current);
    }
}

QRect ThemePickerWidget::circleRect(int i) const
{
    // Circles grouped with GAP between them, centered in the widget width.
    const int diam   = d();
    const int groupW = diam * 3 + GAP * 2;
    const int startX = (width() - groupW) / 2;
    const int cx     = startX + diam / 2 + i * (diam + GAP);
    const int cy     = height() / 2;
    return QRect(cx - diam / 2, cy - diam / 2, diam, diam);
}

int ThemePickerWidget::hitCircle(QPoint pos) const
{
    for (int i = 0; i < 3; ++i)
        if (circleRect(i).adjusted(-6, -6, 6, 6).contains(pos))
            return i;
    return -1;
}

// ─── ThemePickerAction ────────────────────────────────────────────────────────

ThemePickerAction::ThemePickerAction(ColorScheme current,
                                     std::function<void(ColorScheme)> cb,
                                     QObject *parent)
    : QWidgetAction(parent), m_current(current), m_cb(std::move(cb))
{}

void ThemePickerAction::setCurrent(ColorScheme current)
{
    m_current = current;
    for (auto it = m_widgets.begin(); it != m_widgets.end();) {
        if (*it) {
            (*it)->setCurrent(current);
            ++it;
        } else {
            it = m_widgets.erase(it);
        }
    }
}

QWidget *ThemePickerAction::createWidget(QWidget *parent)
{
    auto *widget = new ThemePickerWidget(m_current, [this](ColorScheme s) {
        setCurrent(s);
        m_cb(s);
    }, parent);
    m_widgets.append(widget);
    return widget;
}
