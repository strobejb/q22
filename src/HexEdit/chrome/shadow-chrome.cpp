#include "shadow-chrome.h"

#include <QChildEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

#include <cmath>

QRectF shadowContentRect(const QRectF &rect, const ShadowChromeParams &params)
{
    return rect.adjusted(params.shadowSize, params.shadowSize,
                         -params.shadowSize, -params.shadowSize);
}

void paintShadowRings(QPainter &p, const QRectF &content, const ShadowChromeParams &params)
{
    if (content.isEmpty() || params.shadowSize <= 0)
        return;

    p.setPen(Qt::NoPen);
    for (int dist = params.shadowSize; dist >= 1; --dist) {
        const qreal t = qreal(params.shadowSize - dist) / qMax(1, params.shadowSize - 1);
        const int alpha = int(params.maxAlpha * std::pow(t, params.alphaPower));
        const QRectF r = content.adjusted(-dist, -dist, dist, dist);
        QColor c = params.shadowColor;
        c.setAlpha(alpha);
        p.setBrush(c);
        p.drawRoundedRect(r, params.cornerRadius + dist, params.cornerRadius + dist);
    }
}

void paintShadowedRoundedWindow(QPainter &p, QWidget *widget, const QRect &rect,
                                const ShadowChromeParams &params,
                                bool includeBackground)
{
    if (!widget)
        return;

    const QRectF full(rect);
    const QRectF content = shadowContentRect(full, params);
    if (!content.isValid())
        return;

    if (widget->isMaximized() || widget->isFullScreen()) {
        if (includeBackground) {
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.setPen(Qt::NoPen);
            p.setBrush(widget->palette().window());
            p.drawRect(full);
        }
        return;
    }

    p.setCompositionMode(QPainter::CompositionMode_Source);
    paintShadowRings(p, content, params);

    if (includeBackground) {
        p.setBrush(widget->palette().window());
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(content, params.cornerRadius, params.cornerRadius);
    }
}

void paintShadowCornerRepair(QPainter &p, const QRect &rect, const ShadowChromeParams &params)
{
    const QRectF content = shadowContentRect(QRectF(rect), params);
    if (content.isEmpty())
        return;

    QPainterPath contentPath;
    QPainterPath roundedPath;
    contentPath.addRect(content);
    roundedPath.addRoundedRect(content, params.cornerRadius, params.cornerRadius);

    p.setRenderHint(QPainter::Antialiasing);
    p.setClipPath(contentPath - roundedPath);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    paintShadowRings(p, content, params);
}

ShadowCornerClipper::ShadowCornerClipper(QWidget *parent, const ShadowChromeParams &params)
    : QWidget(parent), m_params(params)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setGeometry(parent->rect());
    raise();
    parent->installEventFilter(this);

    for (QObject *child : parent->children()) {
        if (auto *w = qobject_cast<QWidget *>(child); w && w != this)
            w->installEventFilter(this);
    }
}

bool ShadowCornerClipper::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == parentWidget()) {
        if (event->type() == QEvent::Resize) {
            const auto *resize = static_cast<QResizeEvent *>(event);
            setGeometry(QRect(QPoint(0, 0), resize->size()));
            raise();
            update();
        } else if (event->type() == QEvent::ChildAdded) {
            if (auto *w = qobject_cast<QWidget *>(static_cast<QChildEvent *>(event)->child());
                w && w != this) {
                w->installEventFilter(this);
            }
        }
    } else if (obj != this && event->type() == QEvent::Paint) {
        raise();
        update();
    }

    return false;
}

void ShadowCornerClipper::paintEvent(QPaintEvent *)
{
    auto *w = parentWidget();
    if (!w || w->isMaximized() || w->isFullScreen())
        return;

    QPainter p(this);
    paintShadowCornerRepair(p, rect(), m_params);
}
