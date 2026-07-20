#include "sourcebreadcrumbbar.h"

#include "theme.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QToolTip>

SourceBreadcrumbBar::SourceBreadcrumbBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sourceBreadcrumbBar"));
    setCursor(Qt::ArrowCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
    setFixedHeight(kBarHeight);

    m_closeButton = new QToolButton(this);
    m_closeButton->setObjectName(QStringLiteral("sourceBreadcrumbCloseButton"));
    m_closeButton->setAutoRaise(true);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    m_closeButton->setToolTip(tr("Exit nested source view"));
    m_closeButton->setIcon(recoloredIcon(QStringLiteral("actions/window-close-symbolic"),
                                         palette().color(QPalette::WindowText), 14));
    m_closeButton->setIconSize(QSize(14, 14));
    m_closeButton->setFixedSize(kCloseSide, kCloseSide);
    connect(m_closeButton, &QToolButton::clicked,
            this, [this]() {
                if (m_exitCallback)
                    m_exitCallback();
            });

    updateStyle();
    updateVisibleState();
}

void SourceBreadcrumbBar::setNavigateCallback(std::function<void(int)> callback)
{
    m_navigateCallback = std::move(callback);
}

void SourceBreadcrumbBar::setExitCallback(std::function<void()> callback)
{
    m_exitCallback = std::move(callback);
}

void SourceBreadcrumbBar::setBreadcrumbEnabled(bool enabled)
{
    if (m_breadcrumbEnabled == enabled)
        return;

    m_breadcrumbEnabled = enabled;
    updateVisibleState();
}

void SourceBreadcrumbBar::setStack(const QStringList &labels, const QStringList &details, int activeIndex)
{
    if (labels.size() <= 1 || activeIndex < 0)
    {
        m_labels.clear();
        m_details.clear();
        m_tabRects.clear();
        m_activeIndex = -1;
        m_hoverIndex = -1;
        updateVisibleState();
        return;
    }

    m_labels.clear();
    m_details = details;
    for (int i = 0; i < labels.size(); ++i)
    {
        const QString label = labels[i].trimmed().isEmpty()
            ? tr("Source %1").arg(i + 1)
            : labels[i].trimmed();
        m_labels.push_back(label);
    }
    m_activeIndex = activeIndex;
    updateTabRects();
    updateVisibleState();
    update();
}

void SourceBreadcrumbBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    updateTabRects();

    const QColor window = palette().color(QPalette::Window);
    const QColor border = palette().color(QPalette::Mid);
    const QColor base = palette().color(QPalette::Base);
    const QColor button = palette().color(QPalette::Button);
    const QColor strip = QColor::fromRgbF(window.redF() * 0.72 + border.redF() * 0.28,
                                          window.greenF() * 0.72 + border.greenF() * 0.28,
                                          window.blueF() * 0.72 + border.blueF() * 0.28,
                                          1.0);

    painter.fillRect(rect(), strip);
    painter.setPen(QPen(border, 1.0));
    painter.drawLine(0, height() - 1, width(), height() - 1);

    for (int i = 0; i < m_tabRects.size(); ++i)
    {
        if (i == m_activeIndex)
            continue;
        const bool hovered = i == m_hoverIndex;
        paintTab(&painter, m_tabRects[i], m_labels.value(i), false,
                 hovered ? button.lighter(104) : window);
    }

    if (m_activeIndex >= 0 && m_activeIndex < m_tabRects.size())
        paintTab(&painter, m_tabRects[m_activeIndex], m_labels.value(m_activeIndex), true, base);
}

void SourceBreadcrumbBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateTabRects();
}

void SourceBreadcrumbBar::mouseMoveEvent(QMouseEvent *event)
{
    const int hover = tabAt(event->pos());
    if (m_hoverIndex != hover)
    {
        m_hoverIndex = hover;
        update();
    }
    setCursor(hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

void SourceBreadcrumbBar::leaveEvent(QEvent *event)
{
    m_hoverIndex = -1;
    unsetCursor();
    update();
    QWidget::leaveEvent(event);
}

void SourceBreadcrumbBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        const int index = tabAt(event->pos());
        if (index >= 0)
        {
            if (m_navigateCallback)
                m_navigateCallback(index);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

bool SourceBreadcrumbBar::event(QEvent *event)
{
    if (event && event->type() == QEvent::ToolTip)
    {
        auto *help = static_cast<QHelpEvent *>(event);
        const int index = tabAt(help->pos());
        if (index >= 0 && index < m_details.size())
        {
            QToolTip::showText(help->globalPos(), m_details[index], this);
            return true;
        }
    }
    return QWidget::event(event);
}

void SourceBreadcrumbBar::changeEvent(QEvent *event)
{
    if (event && event->type() == QEvent::PaletteChange)
        updateStyle();
    QWidget::changeEvent(event);
}

void SourceBreadcrumbBar::updateStyle()
{
    const QColor button = palette().color(QPalette::Button);
    const QColor midlight = palette().color(QPalette::Midlight);
    const QColor text = palette().color(QPalette::WindowText);
    m_closeButton->setIcon(recoloredIcon(QStringLiteral("actions/window-close-symbolic"), text, 14));

    if (m_updatingStyle)
        return;

    m_updatingStyle = true;
    setStyleSheet(QStringLiteral(R"(
        QToolButton#sourceBreadcrumbCloseButton {
            background: transparent;
            border: none;
            border-radius: 6px;
            padding: 3px;
            margin-left: 4px;
        }
        QToolButton#sourceBreadcrumbCloseButton:hover {
            background: %1;
        }
        QToolButton#sourceBreadcrumbCloseButton:pressed {
            background: %2;
        }
    )").arg(button.name(QColor::HexRgb),
            midlight.name(QColor::HexRgb)));
    m_updatingStyle = false;
    update();
}

void SourceBreadcrumbBar::updateTabRects()
{
    m_tabRects.clear();

    const QFontMetrics fm(font());
    QFont activeFont = font();
    activeFont.setWeight(QFont::DemiBold);
    const QFontMetrics activeFm(activeFont);
    int x = kLeftPad;
    const int y = height() - kTabHeight;
    const int maxRight = qMax(kLeftPad, width() - kLeftPad - kCloseSide - kCloseGap);
    for (int i = 0; i < m_labels.size(); ++i)
    {
        const bool active = i == m_activeIndex;
        const int available = maxRight - x + 1;
        const int tabCap = active ? qMin(640, qMax(320, available)) : 220;
        const int desiredW = (active ? activeFm.horizontalAdvance(m_labels[i])
                                     : fm.horizontalAdvance(m_labels[i]))
            + 2 * kTabHorzPad
            + (active ? 48 : 0);
        const int tabW = qBound(active ? 160 : 64, desiredW, tabCap);
        if (x >= maxRight)
            break;

        const int actualW = qMin(tabW, maxRight - x + 1);
        m_tabRects.push_back(QRect(x, y, qMax(0, actualW), kTabHeight));
        x += qMax(0, actualW) - 1;
    }

    m_closeButton->setGeometry(width() - kLeftPad - kCloseSide,
                               (height() - kCloseSide) / 2,
                               kCloseSide,
                               kCloseSide);
}

int SourceBreadcrumbBar::tabAt(const QPoint &pos) const
{
    for (int i = m_tabRects.size() - 1; i >= 0; --i)
    {
        if (m_tabRects[i].contains(pos))
            return i;
    }
    return -1;
}

void SourceBreadcrumbBar::paintTab(QPainter *painter, const QRect &rect, const QString &label,
                                   bool active, const QColor &fill)
{
    if (!painter || !rect.isValid())
        return;

    const QColor border = palette().color(QPalette::Mid);
    const QColor textColor = active || rect.contains(mapFromGlobal(QCursor::pos()))
        ? palette().color(QPalette::WindowText)
        : palette().color(QPalette::Disabled, QPalette::WindowText);
    QRectF tabRect(rect);
    tabRect.adjust(0.5, 0.5, -0.5, -0.5);

    QPainterPath fillPath;
    fillPath.moveTo(tabRect.left() + kRadius, tabRect.top());
    fillPath.lineTo(tabRect.right() - kRadius, tabRect.top());
    fillPath.quadTo(tabRect.right(), tabRect.top(), tabRect.right(), tabRect.top() + kRadius);
    fillPath.lineTo(tabRect.right(), tabRect.bottom());
    fillPath.lineTo(tabRect.left(), tabRect.bottom());
    fillPath.lineTo(tabRect.left(), tabRect.top() + kRadius);
    fillPath.quadTo(tabRect.left(), tabRect.top(), tabRect.left() + kRadius, tabRect.top());
    fillPath.closeSubpath();
    painter->fillPath(fillPath, fill);

    QPainterPath borderPath;
    borderPath.moveTo(tabRect.left(), tabRect.bottom());
    borderPath.lineTo(tabRect.left(), tabRect.top() + kRadius);
    borderPath.quadTo(tabRect.left(), tabRect.top(), tabRect.left() + kRadius, tabRect.top());
    borderPath.lineTo(tabRect.right() - kRadius, tabRect.top());
    borderPath.quadTo(tabRect.right(), tabRect.top(), tabRect.right(), tabRect.top() + kRadius);
    borderPath.lineTo(tabRect.right(), tabRect.bottom());
    if (!active)
        borderPath.lineTo(tabRect.left(), tabRect.bottom());

    painter->setPen(QPen(border, 1.0));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(borderPath);

    if (active)
    {
        painter->setPen(QPen(fill, 1.0));
        painter->drawLine(QPointF(tabRect.left() + 1.0, tabRect.bottom()),
                          QPointF(tabRect.right() - 1.0, tabRect.bottom()));
    }

    const QFont oldFont = painter->font();
    QFont tabFont = oldFont;
    if (active)
        tabFont.setWeight(QFont::DemiBold);
    painter->setFont(tabFont);
    painter->setPen(textColor);
    const QString text = QFontMetrics(tabFont).elidedText(label, Qt::ElideMiddle,
                                                          qMax(0, rect.width() - 2 * kTabHorzPad));
    painter->drawText(rect.adjusted(kTabHorzPad, 0, -kTabHorzPad, -1),
                      Qt::AlignCenter, text);
    painter->setFont(oldFont);
}

void SourceBreadcrumbBar::updateVisibleState()
{
    const bool visible = m_breadcrumbEnabled && m_labels.size() > 1 && m_activeIndex >= 0;
    m_closeButton->setVisible(visible);
    setVisible(visible);
}
