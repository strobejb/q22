#include "popupmenucallout.h"

#include "theme.h"

#include <QEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QScreen>
#include <QWidget>

namespace {

constexpr int kShadowMargin = 8;
constexpr int kCornerRadius = 8;
constexpr int kArrowHalfWidth = 10;
constexpr qreal kArrowTipInset = 1.0;
constexpr qreal kShadowYOffset = 3.0;
constexpr qreal kShadowBottomInset = 1.0;
constexpr int kShadowPeakAlpha = 30;

int popupTransparentMargin()
{
#ifdef Q_OS_WIN
    return 0;
#else
    return kShadowMargin;
#endif
}

QRect availableGeometryForPoint(const QPoint &globalPos)
{
    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect();
}

QPainterPath popupMenuCalloutPath(const QRectF &body, int arrowX)
{
    const qreal r = qMin<qreal>(kCornerRadius, qMin(body.width(), body.height()) / 2.0);
    const qreal minTipX = body.left() + r + kArrowHalfWidth + 2.0;
    const qreal maxTipX = body.right() - r - kArrowHalfWidth - 2.0;
    const qreal tipX = (maxTipX >= minTipX)
        ? qBound(minTipX, qreal(arrowX), maxTipX)
        : body.center().x();
    const qreal arrowLeft = tipX - kArrowHalfWidth;
    const qreal arrowRight = tipX + kArrowHalfWidth;
    const qreal tipY = kArrowTipInset + 0.5;

    QPainterPath path;
    path.moveTo(body.left() + r, body.top());
    path.lineTo(arrowLeft, body.top());
    path.lineTo(tipX, tipY);
    path.lineTo(arrowRight, body.top());
    path.lineTo(body.right() - r, body.top());
    path.quadTo(body.right(), body.top(), body.right(), body.top() + r);
    path.lineTo(body.right(), body.bottom() - r);
    path.quadTo(body.right(), body.bottom(), body.right() - r, body.bottom());
    path.lineTo(body.left() + r, body.bottom());
    path.quadTo(body.left(), body.bottom(), body.left(), body.bottom() - r);
    path.lineTo(body.left(), body.top() + r);
    path.quadTo(body.left(), body.top(), body.left() + r, body.top());
    path.closeSubpath();
    return path;
}

class PopupMenuCalloutOverlay : public QWidget {
    int m_arrowX = -1;

public:
    explicit PopupMenuCalloutOverlay(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setGeometry(parentWidget()->rect());
        parent->installEventFilter(this);
    }

    void setArrowX(int x)
    {
        m_arrowX = x;
        update();
    }

    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (obj == parent() && e->type() == QEvent::Resize)
            setGeometry(parentWidget()->rect());
        return false;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const int margin = popupTransparentMargin();
        if (margin <= 0 || m_arrowX < 0)
            return;

        const QRectF body = QRectF(rect()).adjusted(margin, margin, -margin, -margin);
        if (!body.isValid())
            return;

        const int minX = int(body.left()) + kCornerRadius + kArrowHalfWidth + 2;
        const int maxX = int(body.right()) - kCornerRadius - kArrowHalfWidth - 2;
        if (maxX <= minX)
            return;

        const int x = qBound(minX, m_arrowX, maxX);
        const QPainterPath path = popupMenuCalloutPath(body, x);
        const QPainterPath shadowPath = popupMenuCalloutPath(
            body.adjusted(0.0, 0.0, 0.0, -kShadowBottomInset), x).translated(0.0, kShadowYOffset);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QPainterPath outside;
        outside.addRect(QRectF(rect()));
        outside -= path;
        p.setClipPath(outside);
        p.setPen(Qt::NoPen);
        p.setCompositionMode(QPainter::CompositionMode_Source);

        for (int i = margin; i >= 1; --i) {
            const qreal t = (margin > 1) ? qreal(margin - i) / qreal(margin - 1) : 1.0;
            QPainterPathStroker stroker;
            stroker.setWidth(i * 2.4);
            stroker.setJoinStyle(Qt::RoundJoin);
            stroker.setCapStyle(Qt::RoundCap);
            p.setBrush(QColor(0, 0, 0, qRound(kShadowPeakAlpha * t * t)));
            p.drawPath(stroker.createStroke(shadowPath));
        }

        p.setClipping(false);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);

        QPainterPath arrowFill;
        arrowFill.moveTo(x, kArrowTipInset + 0.5);
        arrowFill.lineTo(x - kArrowHalfWidth, body.top());
        arrowFill.lineTo(x + kArrowHalfWidth, body.top());
        arrowFill.closeSubpath();
        p.fillPath(arrowFill, palette().base());

        p.setPen(QPen(palette().base().color(), 3));
        p.drawLine(QPointF(x - kArrowHalfWidth + 1, body.top()),
                   QPointF(x + kArrowHalfWidth - 1, body.top()));

        p.setPen(QPen(palette().mid().color(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
};

class PopupMenuRightAlignPositioner : public QObject {
    QRect m_anchor;

public:
    PopupMenuRightAlignPositioner(QRect anchorGlobal, QObject *parent)
        : QObject(parent), m_anchor(anchorGlobal) {}

    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (e->type() == QEvent::Show) {
            auto *w = static_cast<QWidget *>(obj);
            const int margin = popupTransparentMargin();
            const int bodyW = qMax(1, w->width() - margin * 2);
            const QPoint tip = m_anchor.center();
            int bodyLeft = m_anchor.left();
            int y = tip.y() + 4;

            const QRect avail = availableGeometryForPoint(tip);
            if (avail.isValid()) {
                const int maxX = avail.right() + 1 - bodyW;
                bodyLeft = (maxX >= avail.left()) ? qBound(avail.left(), bodyLeft, maxX) : avail.left();
                const int bodyH = qMax(1, w->height() - margin * 2);
                const int maxY = avail.bottom() + 1 - bodyH;
                y = (maxY >= avail.top()) ? qBound(avail.top(), y, maxY) : avail.top();
            }

            w->move(bodyLeft, y);
        }
        return false;
    }
};

class PopupMenuCalloutPositioner : public QObject {
    QRect m_anchor;
    PopupMenuCalloutOverlay *m_overlay = nullptr;

public:
    PopupMenuCalloutPositioner(QRect anchorGlobal, PopupMenuCalloutOverlay *overlay, QObject *parent)
        : QObject(parent), m_anchor(anchorGlobal), m_overlay(overlay) {}

    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (e->type() == QEvent::Show) {
            auto *w = static_cast<QWidget *>(obj);
            for (QWidget *child : w->findChildren<QWidget *>(QStringLiteral("qexedMenuShadowOverlay")))
                child->hide();

            const int margin = popupTransparentMargin();
            const int bodyW = qMax(1, w->width() - margin * 2);
            const QPoint tip = m_anchor.center();

            int bodyLeft = m_anchor.left();
            const QRect avail = availableGeometryForPoint(tip);
            if (avail.isValid()) {
                const int maxLeft = avail.right() + 1 - bodyW;
                bodyLeft = (maxLeft >= avail.left())
                    ? qBound(avail.left(), bodyLeft, maxLeft)
                    : avail.left();
            }

            const int bodyTop = tip.y() + margin - int(kArrowTipInset);
            w->move(bodyLeft, bodyTop);

            if (m_overlay) {
                const int finalWindowLeft = bodyLeft - margin;
                m_overlay->setArrowX(tip.x() - finalWindowLeft);
                m_overlay->raise();
            }
        }
        return false;
    }
};

} // namespace

void installPopupMenuCallout(QMenu *menu, QRect anchorGlobal)
{
    if (!menu)
        return;

    auto *overlay = new PopupMenuCalloutOverlay(menu);
    overlay->show();
    overlay->raise();
    menu->installEventFilter(new PopupMenuCalloutPositioner(anchorGlobal, overlay, menu));
}

void installPopupMenuRightAligned(QMenu *menu, QRect anchorGlobal)
{
    if (!menu)
        return;

    menu->installEventFilter(new PopupMenuRightAlignPositioner(anchorGlobal, menu));
}
