#include "scrollhintoverlay.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QVariantAnimation>

// ── HintWidget ────────────────────────────────────────────────────────────────
// A small circular chevron button parented to the scroll area's viewport.
// Uses WA_TranslucentBackground so only the circle is visible.
// One-shot: clicking triggers a smooth scroll then the widget fades out.

class HintWidget : public QWidget
{
public:
    HintWidget(QScrollBar *bar, QWidget *parent)
        : QWidget(parent), m_bar(bar)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(kSz, kSz);
        setCursor(Qt::PointingHandCursor);
        hide();
    }

    void setHintOpacity(qreal opacity)
    {
        opacity = qBound(0.0, opacity, 1.0);
        if (qFuzzyCompare(m_opacity + 1.0, opacity + 1.0)) return;
        m_opacity = opacity;
        setVisible(m_opacity > 0.0);
        if (isVisible()) update();
    }

    void fadeOut()
    {
        if (!isVisible()) return;
        auto *anim = new QVariantAnimation(this);
        anim->setStartValue(m_opacity);
        anim->setEndValue(0.0);
        anim->setDuration(350);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            setHintOpacity(v.toReal());
        });
        connect(anim, &QVariantAnimation::finished, this, &QWidget::hide);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

protected:
    void enterEvent(QEnterEvent *) override
    {
        m_hovered = true;
        update();
    }

    void leaveEvent(QEvent *) override
    {
        m_hovered = false;
        update();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setOpacity(m_opacity);

        QColor circleColor = palette().color(QPalette::Text);
        // Darken on hover to give feedback
        const int baseAlpha = m_hovered ? 210 : 150;
        circleColor.setAlpha(baseAlpha);
        const QColor chevronColor = circleColor.lightness() > 128
                                        ? QColor(30, 30, 30)
                                        : Qt::white;

        const QRectF circle(2.5, 2.5, kSz - 5.0, kSz - 5.0);
        p.setPen(Qt::NoPen);
        p.setBrush(circleColor);
        p.drawEllipse(circle);

        p.setPen(QPen(chevronColor, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);

        const QPointF c = circle.center();
        constexpr qreal aw = 5.5, ah = 3.5;
        // Down chevron
        p.drawLine(QPointF(c.x() - aw, c.y() - ah * 0.5), QPointF(c.x(), c.y() + ah * 0.5));
        p.drawLine(QPointF(c.x(),       c.y() + ah * 0.5), QPointF(c.x() + aw, c.y() - ah * 0.5));
    }

    void mousePressEvent(QMouseEvent *) override
    {
        auto *anim = new QPropertyAnimation(m_bar, "value", this);
        anim->setDuration(350);
        anim->setEasingCurve(QEasingCurve::InOutCubic);
        anim->setStartValue(m_bar->value());
        anim->setEndValue(m_bar->value() + m_bar->pageStep());
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

private:
    static constexpr int kSz = 32;
    QScrollBar *m_bar;
    qreal       m_opacity = 0.0;
    bool        m_hovered = false;
};

// ── ScrollHintOverlay ─────────────────────────────────────────────────────────

ScrollHintOverlay::ScrollHintOverlay(QScrollArea *scrollArea)
    : QObject(scrollArea), m_scroll(scrollArea)
{
    auto *vp  = scrollArea->viewport();
    auto *bar = scrollArea->verticalScrollBar();

    m_hint = new HintWidget(bar, vp);

    vp->installEventFilter(this);
    m_scroll->installEventFilter(this);
    connect(bar, &QScrollBar::rangeChanged, this, &ScrollHintOverlay::updateVisibility);
    connect(bar, &QScrollBar::valueChanged, this, &ScrollHintOverlay::dismiss);

    reposition();
    updateVisibility();
}

void ScrollHintOverlay::install(QScrollArea *scrollArea)
{
    new ScrollHintOverlay(scrollArea);
}

bool ScrollHintOverlay::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_scroll->viewport() && event->type() == QEvent::Resize) {
        reposition();
    } else if (obj == m_scroll) {
        const auto t = event->type();
        if (t == QEvent::Show || t == QEvent::ShowToParent) {
            reset();
        }
    }
    return QObject::eventFilter(obj, event);
}

void ScrollHintOverlay::reposition()
{
    const QSize vp  = m_scroll->viewport()->size();
    const int   sz  = m_hint->width();
    const int   cx  = (vp.width() - sz) / 2;
    constexpr int margin = 10;

    m_hint->move(cx, vp.height() - sz - margin);
    m_hint->raise();
}

void ScrollHintOverlay::updateVisibility()
{
    if (m_dismissed) return;
    QScrollBar *bar = m_scroll->verticalScrollBar();
    if (bar->maximum() > bar->minimum()) {
        m_hint->setHintOpacity(1.0);
        reposition();
    } else {
        m_hint->setHintOpacity(0.0);
    }
}

void ScrollHintOverlay::reset()
{
    m_dismissed = false;
    updateVisibility();
}

void ScrollHintOverlay::dismiss()
{
    if (m_dismissed) return;
    m_dismissed = true;
    m_hint->fadeOut();
}
