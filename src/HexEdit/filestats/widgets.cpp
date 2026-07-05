#include "filestats/widgets.h"

#include "filestats/banner.h"
#include "theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStyleOptionHeader>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace filestats
{

static constexpr int kStyledListHeaderBottomGap      = 3;
static constexpr int kStyledListHeaderUnderlineWidth = 4;

// â”€â”€ Internal helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static QToolButton *createProgressToolButton(QWidget *parent, const QString &iconName, const QString &toolTip)
{
    auto *button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(22, 22);
    button->setIconSize(QSize(16, 16));
    button->setToolTip(toolTip);
    button->setProperty("iconThemeName", iconName);
    button->setProperty("iconSize", 16);
    button->setIcon(recoloredIcon(iconName, parent->palette().buttonText().color(), 16));
    const bool    dark    = parent->palette().window().color().lightness() < 128;
    const QString hover   = dark ? QStringLiteral("rgba(255,255,255,0.15)") : QStringLiteral("rgba(0,0,0,0.10)");
    const QString pressed = dark ? QStringLiteral("rgba(255,255,255,0.25)") : QStringLiteral("rgba(0,0,0,0.18)");
    button->setStyleSheet(QStringLiteral(R"(
        QToolButton {
            border: none;
            border-radius: 6px;
            background: transparent;
        }
        QToolButton:hover { background: %1; }
        QToolButton:pressed { background: %2; }
        QToolButton::menu-indicator { image: none; width: 0; }
    )")
                              .arg(hover, pressed));
    button->hide();
    return button;
}

QFont styledListHeaderFont(const QFont &baseFont)
{
    QFont headerFont = baseFont;
    if (headerFont.pointSizeF() > 0)
        headerFont.setPointSizeF(qMax(1.0, headerFont.pointSizeF() - 1.0));
    else if (headerFont.pixelSize() > 0)
        headerFont.setPixelSize(qMax(1, headerFont.pixelSize() - 1));
    headerFont.setWeight(QFont::DemiBold);
    return headerFont;
}

int styledListHeaderHeight(const QFont &baseFont)
{
    const QFontMetrics metrics(styledListHeaderFont(baseFont));
    return metrics.height() + 2 * stringsHeaderPadding(metrics) + kStyledListHeaderBottomGap;
}

int styledListHeaderItemLeftPadding(const QFont &baseFont, int itemCellInset)
{
    return qMax(0, stringsHeaderPadding(QFontMetrics(styledListHeaderFont(baseFont))) - itemCellInset);
}

StyledListHeader::StyledListHeader(Qt::Orientation orientation, QWidget *parent)
    : QHeaderView(orientation, parent)
{
    setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    setHighlightSections(false);
    setMouseTracking(true);
}

void StyledListHeader::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    paintSectionBase(painter, rect, logicalIndex, styledListHeaderFont(font()),
                     model() ? model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString()
                             : QString());
}

void StyledListHeader::mouseMoveEvent(QMouseEvent *event)
{
    QHeaderView::mouseMoveEvent(event);
    const QPoint pos = event->position().toPoint();
    const int hoveredSection = logicalIndexAt(pos);
    if (m_hoveredSection != hoveredSection)
    {
        m_hoveredSection = hoveredSection;
        viewport()->update();
    }

    if (nearSectionResizeHandle(pos))
        viewport()->setCursor(Qt::SplitHCursor);
    else if (hoveredSection >= 0)
        viewport()->setCursor(Qt::PointingHandCursor);
    else
        viewport()->setCursor(Qt::ArrowCursor);
}

void StyledListHeader::leaveEvent(QEvent *event)
{
    QHeaderView::leaveEvent(event);
    m_hoveredSection = -1;
    viewport()->unsetCursor();
    viewport()->update();
}

bool StyledListHeader::isSectionHovered(int logicalIndex) const
{
    return logicalIndex >= 0 && logicalIndex == m_hoveredSection;
}

bool StyledListHeader::nearSectionResizeHandle(const QPoint &pos) const
{
    constexpr int kResizeSlop = 4;
    for (int visual = 0; visual < count() - 1; ++visual)
    {
        const int logical = logicalIndex(visual);
        if (isSectionHidden(logical))
            continue;

        const int edgeX = sectionViewportPosition(logical) + sectionSize(logical);
        if (qAbs(pos.x() - edgeX) <= kResizeSlop)
            return true;
    }
    return false;
}

QRect StyledListHeader::sectionTextRect(const QRect &sectionRect, const QFontMetrics &metrics) const
{
    const int pad = stringsHeaderPadding(metrics);
    return sectionRect.adjusted(pad, pad, -pad, -pad);
}

void StyledListHeader::paintSectionBase(QPainter *painter, const QRect &rect, int logicalIndex,
                                        const QFont &headerFont, const QString &text,
                                        bool drawNativeHeader) const
{
    if (!painter || !rect.isValid())
        return;

    painter->save();
    const bool   hovered    = isSectionHovered(logicalIndex);
    const QColor background = palette().color(QPalette::Base);
    painter->fillRect(rect, background);

    QStyleOptionHeader opt;
    initStyleOption(&opt);
    initStyleOptionForIndex(&opt, logicalIndex);
    opt.rect          = rect.adjusted(0, 0, 0, -kStyledListHeaderBottomGap);
    opt.fontMetrics   = QFontMetrics(headerFont);
    opt.text.clear();
    opt.sortIndicator = QStyleOptionHeader::None;
    opt.palette.setColor(QPalette::Button, background);
    opt.palette.setColor(QPalette::Window, background);
    if (hovered)
        opt.state |= QStyle::State_MouseOver;
    if (drawNativeHeader)
        style()->drawControl(QStyle::CE_Header, &opt, painter, this);

    const QColor textColor = hovered ? palette().color(QPalette::WindowText) : stringsHeaderTextColor(palette());
    painter->setFont(headerFont);
    painter->setPen(textColor);
    painter->drawText(sectionTextRect(opt.rect, opt.fontMetrics), Qt::AlignLeft | Qt::AlignVCenter, text);

    if (hovered)
    {
        painter->fillRect(QRect(rect.left(), rect.bottom() - kStyledListHeaderUnderlineWidth + 1,
                                rect.width(), kStyledListHeaderUnderlineWidth),
                          palette().color(QPalette::Button));
    }

    painter->restore();
}

StyledSortHeader::StyledSortHeader(Qt::Orientation orientation, QWidget *parent)
    : StyledListHeader(orientation, parent)
{
}

void StyledSortHeader::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    if (!painter || !rect.isValid())
        return;

    const QFont        headerFont = styledListHeaderFont(font());
    const QFontMetrics headerMetrics(headerFont);
    const QString      text = model() ? model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString()
                                      : QString();
    paintSectionBase(painter, rect, logicalIndex, headerFont, text);

    if (logicalIndex != sortIndicatorSection())
        return;

    constexpr int kIconGap  = 4;
    constexpr int kIconSize = 10;
    const QRect   textRect  = sectionTextRect(rect, headerMetrics);
    const QColor  iconColor = isSectionHovered(logicalIndex) ? palette().color(QPalette::WindowText)
                                                             : stringsHeaderTextColor(palette());

    painter->save();
    painter->setFont(headerFont);
    const int     textWidth = painter->fontMetrics().horizontalAdvance(text);
    const int     x         = qMin(textRect.left() + textWidth + kIconGap, textRect.right() - kIconSize + 1);
    const QRect   iconRect(x, textRect.top() + (textRect.height() - kIconSize) / 2, kIconSize, kIconSize);
    const QString iconName = sortIndicatorOrder() == Qt::AscendingOrder
                                 ? QStringLiteral("ui/go-up-symbolic")
                                 : QStringLiteral("ui/go-down-symbolic");
    recoloredIcon(iconName, iconColor, kIconSize).paint(painter, iconRect);
    painter->restore();
}

static QToolButton *createProgressStopButton(QWidget *parent)
{
    return createProgressToolButton(parent, QStringLiteral("actions/circle-stop-solid"), QObject::tr("Stop"));
}

static QToolButton *createProgressStartButton(QWidget *parent)
{
    return createProgressToolButton(parent, QStringLiteral("actions/circle-play-solid"), QObject::tr("Start"));
}

static QHBoxLayout *createProgressRowLayout(QWidget *row)
{
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    return layout;
}

// â”€â”€ SectionHeader â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
SectionHeader::SectionHeader(const QString &title, QWidget *parent) : QWidget(parent), m_title(title)
{
    setFixedHeight(kSectionHeaderHeight);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);

    m_icon = new QLabel(this);
    m_icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_icon->setFixedSize(16, 16);

    setCollapsed(false);
}

void SectionHeader::setCollapsed(bool collapsed)
{
    m_collapsed = collapsed;
    updateChevronIcon();
}

void SectionHeader::setCloseMode(bool on)
{
    m_closeMode = on;
    setExpandable(on);
    updateChevronIcon();
}

void SectionHeader::updateChevronIcon()
{
    if (m_closeMode)
    {
        const QColor color = m_chevronPressed ? palette().windowText().color()
                                              : subduedTextColor(palette());
        m_icon->setPixmap(
            recoloredIcon(QStringLiteral("ui/go-next-symbolic"), color, 16).pixmap(16, 16));
        return;
    }

    QString      iconName;
    QColor       iconColor;
    if (m_expandable && m_hover)
    {
        // 3) looks too
        // iconName  = m_sectionExpanded ? QStringLiteral("ui/collapse-panel") : QStringLiteral("ui/expand-panel");

        // 2) visually cohesive
        //iconName  = m_collapsed ? QStringLiteral("ui/double-down") : QStringLiteral("ui/double-up");

        // 3)
        if(m_sectionExpanded)
            iconName  = m_collapsed ? QStringLiteral("ui/collapse-down") : QStringLiteral("ui/collapse-up");
        else
            iconName  = m_collapsed ? QStringLiteral("ui/expand-down") : QStringLiteral("ui/expand-up");


        iconColor = m_chevronPressed ? palette().windowText().color() : subduedTextColor(palette());
    }
    else
    {
        iconName  = m_collapsed ? QStringLiteral("ui/go-down-symbolic") : QStringLiteral("ui/go-up-symbolic");
        iconColor = palette().windowText().color();
    }
    m_icon->setPixmap(recoloredIcon(iconName, iconColor, 16).pixmap(16, 16));
}

void SectionHeader::syncHoverFromCursor()
{
    const bool over = rect().contains(mapFromGlobal(QCursor::pos()));
    if (m_hover == over)
        return;
    m_hover = over;
    updateChevronIcon();
    update();
}

void SectionHeader::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_icon->move(width() - kContentMargin - 16, (height() - 16) / 2);
}

void SectionHeader::enterEvent(QEnterEvent *event)
{
    // Suppress hover while another widget holds the mouse grab (e.g. the
    // header being dragged).  grabMouse() only captures button/move events
    // so enterEvent still fires on bystander headers â€” without this guard
    // they accumulate stale m_hover = true that persists after the drag.
    if (!QWidget::mouseGrabber() || QWidget::mouseGrabber() == this)
    {
        m_hover = true;
        updateChevronIcon();
    }
    update();
    QWidget::enterEvent(event);
}

void SectionHeader::leaveEvent(QEvent *event)
{
    if (!m_dragging)
        m_hover = false;
    m_chevronPressed = false;
    m_chevronHovered = false;
    updateChevronIcon();
    update();
    QWidget::leaveEvent(event);
}

void SectionHeader::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && chevronHitRect().contains(event->pos()))
    {
        m_chevronPressed = true;
        updateChevronIcon();
        update();
    }
    if (event->button() == Qt::LeftButton && m_dragStarted && !chevronHitRect().contains(event->pos()))
    {
        m_pressing            = true;
        m_dragging            = false;
        m_dragMovedAfterStart = false;
        m_pressGlobal         = event->globalPosition().toPoint();
        QApplication::setOverrideCursor(Qt::ClosedHandCursor);
        update();
        QTimer::singleShot(750, this,
                           [this]()
                           {
                               if (!m_pressing || m_dragging)
                                   return;
                               m_dragging = true;
                               grabMouse();
                               if (m_dragStarted)
                                   m_dragStarted(QCursor::pos());
                           });
    }
    QWidget::mousePressEvent(event);
}

void SectionHeader::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressing && !m_dragging)
    {
        const QPoint delta = event->globalPosition().toPoint() - m_pressGlobal;
        if (delta.manhattanLength() > 6)
        {
            m_dragging = true;
            grabMouse();
            if (m_dragStarted)
                m_dragStarted(event->globalPosition().toPoint());
        }
    }
    if (m_dragging && m_dragMoved)
    {
        m_dragMovedAfterStart = true;
        m_dragMoved(event->globalPosition().toPoint());
    }
    {
        const bool overChevron = chevronHitRect().contains(event->pos());
        if (overChevron != m_chevronHovered)
        {
            m_chevronHovered = overChevron;
            updateChevronIcon();
            update();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void SectionHeader::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (m_pressing)
        {
            QApplication::restoreOverrideCursor();
            const bool wasDragging = m_dragging;
            if (m_dragging)
            {
                releaseMouse();
                m_dragging = false;
                if (m_dragEnded)
                    m_dragEnded(event->globalPosition().toPoint());
                if (!m_dragMovedAfterStart && rect().contains(event->pos()) && m_clicked)
                    m_clicked();
                // rebuildSectionLayout() may have moved this header to a new
                // position. layout->activate() is asynchronous, so the
                // geometry isn't current yet â€” defer one tick until it is.
                QTimer::singleShot(0, this,
                                   [this]()
                                   {
                                       syncHoverFromCursor();
                                   });
            }
            else if (rect().contains(event->pos()) && m_clicked)
            {
                m_clicked();
            }
            m_pressing = false;
            if (!wasDragging && !rect().contains(event->pos()))
                m_hover = false;
            update();
            event->accept();
            return;
        }
        if (m_chevronPressed)
        {
            m_chevronPressed = false;
            updateChevronIcon();
            update();
        }
        if (rect().contains(event->pos()))
        {
            if (m_expandable && chevronHitRect().contains(event->pos()) && m_expandCallback)
                m_expandCallback();
            else if (m_clicked)
                m_clicked();
            event->accept();
            return;
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void SectionHeader::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_dragging && m_doubleClicked)
        m_doubleClicked();
    QWidget::mouseDoubleClickEvent(event);
}

void SectionHeader::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().window());

    const bool dark = palette().window().color().lightness() < 128;
    if (m_hover || m_pressing)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(dark ? 255 : 0, dark ? 255 : 0, dark ? 255 : 0, dark ? 28 : 18));
        painter.drawRoundedRect(rect().adjusted(0, 2, 0, -2), 4, 4);
    }

    // Title text â€” clip to avoid the collapse icon (and optional expand icon) on the right
    QFont boldFont = font();
    boldFont.setBold(true);
    painter.setFont(boldFont);
    painter.setPen(subduedTextColor(palette()));
    const int rightIconsWidth = 16;
    const int textRight       = width() - kContentMargin - rightIconsWidth - 4;
    QString   displayTitle    = m_title;
    if ((m_hover || m_pressing) && m_dragStarted)
        displayTitle.replace(QStringLiteral(" - paused"), QString());
    painter.drawText(QRect(kContentMargin, 0, textRight - kContentMargin, height()),
                     Qt::AlignVCenter | Qt::AlignLeft, displayTitle);

    // Hamburger: gradient fade over title text + icon, drawn on top
    if ((m_hover || m_pressing) && m_dragStarted)
    {
        const QColor base  = palette().window().color();
        const int    c     = dark ? 255 : 0;
        const int    blend = dark ? 28 : 18;
        const QColor bg((base.red() * (255 - blend) + c * blend) / 255,
                        (base.green() * (255 - blend) + c * blend) / 255,
                        (base.blue() * (255 - blend) + c * blend) / 255);

        const int       iconLeft  = (width() - 16) / 2;
        const int       fadeStart = iconLeft - 32;
        QLinearGradient grad(fadeStart, 0, iconLeft, 0);
        grad.setColorAt(0.0, QColor(bg.red(), bg.green(), bg.blue(), 0));
        grad.setColorAt(1.0, bg);
        painter.fillRect(fadeStart, 2, 32, height() - 4, grad);
        painter.fillRect(iconLeft, 2, 16, height() - 4, bg);

        QColor iconColor = subduedTextColor(palette());
        if (!m_pressing)
        {
            const QColor    bgColor    = palette().window().color();
            constexpr qreal bgWeight   = 0.35;
            constexpr qreal iconWeight = 1.0 - bgWeight;
            iconColor = QColor(qRound(iconColor.red() * iconWeight + bgColor.red() * bgWeight),
                               qRound(iconColor.green() * iconWeight + bgColor.green() * bgWeight),
                               qRound(iconColor.blue() * iconWeight + bgColor.blue() * bgWeight),
                               iconColor.alpha());
        }
        const QPixmap icon =
            recoloredIcon(QStringLiteral("actions/open-menu-symbolic"), iconColor, 16).pixmap(16, 16);
        painter.drawPixmap(iconLeft, (height() - 16) / 2, icon);
    }

    if (m_isDragTarget)
    {
        painter.setPen(QPen(QColor(dark ? 255 : 0, dark ? 255 : 0, dark ? 255 : 0, dark ? 70 : 55), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect().adjusted(1, 3, -1, -3), 4, 4);
    }
}

QRect SectionHeader::chevronHitRect() const
{
    constexpr int kChevronSlop = 8;
    const int     iconLeft     = width() - kContentMargin - 16;
    return QRect(iconLeft - kChevronSlop, 0, width() - iconLeft + kChevronSlop, height());
}


// â”€â”€ SectionOperationStrip â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

SectionOperationStrip::SectionOperationStrip(QWidget *parent, const std::function<void()> &onStart,
                                             const std::function<void()> &onStop,
                                             const std::function<void()> &onResume,
                                             const std::function<void()> &onRetry)
    : m_onStop(onStop), m_onResume(onResume)
{
    m_strip     = new QWidget(parent);
    auto *outer = new QVBoxLayout(m_strip);
    outer->setContentsMargins(kSectionHeaderOuterMargin + kCardLeftInset, 0,
                              kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
    outer->setSpacing(0);

    auto *inner = new QVBoxLayout;
    inner->setContentsMargins(kSettingsCardShadowInset, 0, kSettingsCardShadowInset, 0);
    inner->setSpacing(0);
    outer->addLayout(inner);

    m_startButton = createProgressStartButton(m_strip);
    m_startLabel  = new QLabel(QObject::tr("Begin scan"), m_strip);
    m_startLabel->setStyleSheet(QStringLiteral("color: %1;").arg(cssColor(subduedTextColor(parent->palette()))));
    m_startRow        = new QWidget(m_strip);
    auto *startLayout = createProgressRowLayout(m_startRow);
    startLayout->addWidget(m_startButton, 0, Qt::AlignVCenter);
    startLayout->addWidget(m_startLabel, 0, Qt::AlignVCenter);
    startLayout->addStretch(1);
    inner->addWidget(m_startRow);

    m_progress = new QProgressBar(m_strip);
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);

    m_stopButton         = createProgressStopButton(m_strip);
    m_progressRow        = new QWidget(m_strip);
    auto *progressLayout = createProgressRowLayout(m_progressRow);
    progressLayout->addWidget(m_stopButton, 0, Qt::AlignVCenter);
    progressLayout->addWidget(m_progress, 1, Qt::AlignVCenter);
    inner->addWidget(m_progressRow);

    m_retryStrip  = new ActionBanner(QObject::tr("Restart"), onRetry, m_strip);
    auto *retryWrap   = new QWidget(m_strip);
    auto *retryLayout = new QVBoxLayout(retryWrap);
    retryLayout->setContentsMargins(0, 0, 0, 0);
    retryLayout->setSpacing(0);
    retryLayout->addWidget(m_retryStrip);
    inner->addWidget(retryWrap);
    inner->addSpacing(kHeaderControlGap);

    QObject::connect(m_startButton, &QToolButton::clicked, m_strip,
                     [onStart]()
                     {
                         onStart();
                     });
    QObject::connect(m_stopButton, &QToolButton::clicked, m_strip,
                     [this]()
                     {
                         if (m_progressAction == ProgressAction::Resume)
                         {
                             if (m_onResume)
                                 m_onResume();
                         }
                         else if (m_onStop)
                         {
                             m_onStop();
                         }
                     });
    clear();
}

bool SectionOperationStrip::hasOperation() const
{
    return !m_startRow->isHidden() || !m_progressRow->isHidden() || !m_retryStrip->isHidden();
}

void SectionOperationStrip::showProgress()
{
    m_startButton->hide();
    m_startRow->hide();
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progress->show();
    setProgressActionStop();
    m_stopButton->show();
    m_progressRow->show();
    m_retryStrip->hide();
    updateVisibility();
}

void SectionOperationStrip::showAction(const QString &message, const QString &buttonText)
{
    m_startButton->hide();
    m_startRow->hide();
    m_progress->hide();
    m_stopButton->hide();
    m_progressRow->hide();
    m_retryStrip->setMessage(message);
    m_retryStrip->setButtonText(buttonText);
    m_retryStrip->show();
    updateVisibility();
}

void SectionOperationStrip::showStart(const QString &message)
{
    m_startLabel->setText(message);
    m_startButton->show();
    m_startRow->show();
    m_progress->hide();
    m_stopButton->hide();
    m_progressRow->hide();
    m_retryStrip->hide();
    updateVisibility();
}

void SectionOperationStrip::clear()
{
    m_startButton->hide();
    m_startRow->hide();
    m_progress->hide();
    m_stopButton->hide();
    m_progressRow->hide();
    m_retryStrip->hide();
    updateVisibility();
}

void SectionOperationStrip::setProgressAction(ProgressAction action)
{
    m_progressAction       = action;
    const QString iconName = action == ProgressAction::Resume ? QStringLiteral("actions/circle-play-solid")
                                                              : QStringLiteral("actions/circle-stop-solid");
    m_stopButton->setToolTip(action == ProgressAction::Resume ? QObject::tr("Resume") : QObject::tr("Stop"));
    m_stopButton->setProperty("iconThemeName", iconName);
    m_stopButton->setIcon(recoloredIcon(iconName, m_strip->palette().buttonText().color(), 16));
}

void SectionOperationStrip::updateVisibility()
{
    m_strip->setVisible(!m_collapsed && hasOperation());
}

// â”€â”€ VerticalResizeHandle â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

VerticalResizeHandle::VerticalResizeHandle(std::function<void(int)> onDrag, QWidget *parent)
    : QWidget(parent), m_onDrag(std::move(onDrag))
{
    setFixedHeight(kHeight);
    setCursor(Qt::SizeVerCursor);
}

void VerticalResizeHandle::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor col = palette().color(QPalette::Mid);
    col.setAlphaF(m_hovered ? 0.9 : 0.35);
    p.setPen(QPen(col, 2, Qt::SolidLine, Qt::RoundCap));
    const int cy = height() / 2;
    const int cx = width() / 2;
    for (int i = -1; i <= 1; ++i)
        p.drawLine(cx + i * 12 - 5, cy, cx + i * 12 + 5, cy);
}

void VerticalResizeHandle::enterEvent(QEnterEvent *)
{
    m_hovered = true;
    update();
}

void VerticalResizeHandle::leaveEvent(QEvent *)
{
    m_hovered = false;
    update();
}

void VerticalResizeHandle::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_dragY = qRound(event->globalPosition().y());
}

void VerticalResizeHandle::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;
    const int y  = qRound(event->globalPosition().y());
    const int dy = y - m_dragY;
    if (dy != 0)
    {
        m_dragY = y;
        m_onDrag(dy);
    }
}

void VerticalResizeHandle::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_dragY = 0;
}

// â”€â”€ StringListFrame â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

StringListFrame::StringListFrame(QWidget *parent) : QFrame(parent)
{
}

void StringListFrame::setListWidget(QTreeWidget *list)
{
    m_list = list;
    if (!m_footerText.isEmpty())
        ensureFooterItem();
    refreshFooterAppearance();
    refreshFooterPlacement();
    positionList();
}

void StringListFrame::setFooterText(const QString &text)
{
    m_footerText = text;
    if (!m_list)
        return;

    if (text.isEmpty())
    {
        removeFooterItem();
    }
    else
    {
        ensureFooterItem();
        if (m_footerLabel)
            m_footerLabel->setText(text);
        refreshFooterPlacement();
    }
    positionList();
}

void StringListFrame::clearList()
{
    // FooterItem is owned by the tree and will be deleted by clear();
    // null our pointers before that happens so we don't hold dangling refs.
    // m_footerWidget is parented to the viewport and survives clear(), but
    // it's orphaned â€” let it be cleaned up with the tree widget.
    m_footerItem   = nullptr;
    m_footerWidget = nullptr;
    m_footerLabel  = nullptr;
    m_footerText.clear();
    if (m_list)
        m_list->clear();
}

void StringListFrame::refreshFooterPlacement()
{
    if (!m_list || !m_footerItem || m_footerText.isEmpty())
        return;

    const int index = m_list->indexOfTopLevelItem(m_footerItem);
    if (index >= 0)
    {
        if (index == m_list->topLevelItemCount() - 1)
            return;
        m_list->takeTopLevelItem(index);
    }
    m_list->addTopLevelItem(m_footerItem);
    m_list->setFirstColumnSpanned(m_list->indexOfTopLevelItem(m_footerItem), QModelIndex(), true);
    if (m_footerWidget)
        m_list->setItemWidget(m_footerItem, 0, m_footerWidget);
    m_footerItem->setHidden(false);
}

void StringListFrame::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    positionList();
}

void StringListFrame::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::PaletteChange)
        refreshFooterAppearance();
}

bool StringListFrame::FooterItem::operator<(const QTreeWidgetItem &other) const
{
    if (other.data(0, kStringFooterRole).toBool())
        return false;
    if (!treeWidget())
        return false;
    return treeWidget()->header()->sortIndicatorOrder() == Qt::DescendingOrder;
}

void StringListFrame::ensureFooterItem()
{
    if (m_footerItem)
        return;
    m_footerItem   = new FooterItem;
    m_footerWidget = new QWidget(m_list);
    auto *layout   = new QHBoxLayout(m_footerWidget);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(0);
    layout->addStretch(1);
    m_footerLabel = new QLabel(m_footerWidget);
    m_footerLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_footerLabel, 0, Qt::AlignCenter);
    layout->addStretch(1);
    refreshFooterAppearance();
}

void StringListFrame::refreshFooterAppearance()
{
    if (!m_footerItem)
        return;

    QFont footerFont = font();
    if (footerFont.pointSizeF() > 0)
        footerFont.setPointSizeF(qMax(1.0, footerFont.pointSizeF() - 1.0));
    else if (footerFont.pixelSize() > 0)
        footerFont.setPixelSize(qMax(1, footerFont.pixelSize() - 1));

    if (m_footerLabel)
    {
        m_footerLabel->setFont(footerFont);
        m_footerLabel->setStyleSheet(
            QStringLiteral("background: transparent; color: %1;").arg(cssColor(stringsHeaderTextColor(palette()))));
    }
    m_footerItem->setData(0, kStringFooterRole, true);
    m_footerItem->setFirstColumnSpanned(true);
    m_footerItem->setFlags(Qt::NoItemFlags);
}

void StringListFrame::removeFooterItem()
{
    if (!m_footerItem)
        return;
    if (m_list)
    {
        const int index = m_list->indexOfTopLevelItem(m_footerItem);
        if (index >= 0)
            delete m_list->takeTopLevelItem(index);
    }
    delete m_footerWidget;
    m_footerWidget = nullptr;
    m_footerLabel  = nullptr;
    delete m_footerItem;
    m_footerItem = nullptr;
}

void StringListFrame::positionList()
{
    if (!m_list)
        return;

    const QRect inner = rect().adjusted(kInset, kInset, -kInset, -kInset);
    m_list->setGeometry(inner.left(), inner.top(), inner.width(), inner.height());
}

TabbedContentFrame::TabbedContentFrame(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);

    m_contentLayout = new QVBoxLayout(this);
    m_contentLayout->setContentsMargins(kBorderWidth, kBorderWidth, kBorderWidth,
                                        kTabHeight + kBorderWidth);
    m_contentLayout->setSpacing(0);
}

void TabbedContentFrame::setTabs(const QStringList &labels)
{
    m_tabLabels = labels;
    m_tabRects.clear();
    m_tabRects.resize(labels.size());
    updateTabRects();
    updateFooterChildren();
    update();
}

void TabbedContentFrame::setContentWidget(QWidget *widget)
{
    if (!widget)
        return;
    m_contentLayout->addWidget(widget);
}

void TabbedContentFrame::setStatusLabel(QLabel *label)
{
    m_statusLabel = label;
    if (m_statusLabel)
    {
        m_statusLabel->setParent(this);
        m_statusLabel->setWordWrap(false);
    }
    updateFooterChildren();
}

void TabbedContentFrame::setTabChangedCallback(std::function<void(int)> callback)
{
    m_tabChangedCallback = std::move(callback);
}

void TabbedContentFrame::setCurrentIndex(int index)
{
    if (m_currentIndex == index)
        return;
    m_currentIndex = index;
    update();
}

QSize TabbedContentFrame::sizeHint() const
{
    return QSize(420, 320);
}

void TabbedContentFrame::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    updateTabRects();
    const QColor border = palette().color(QPalette::Mid);
    const QColor base = palette().color(QPalette::Base);
    const QColor footer = palette().color(QPalette::Window);

    painter.fillRect(rect(), footer);
    paintContentBody(&painter, base, border);

    for (int i = 0; i < m_tabRects.size(); ++i)
        paintTab(&painter, m_tabRects[i], m_tabLabels.value(i), i);
}

void TabbedContentFrame::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateTabRects();
    updateFooterChildren();
}

void TabbedContentFrame::mouseMoveEvent(QMouseEvent *event)
{
    const int hoverTab = tabAt(event->pos());
    if (m_hoverTab != hoverTab)
    {
        m_hoverTab = hoverTab;
        update();
    }
    setCursor(hoverTab >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

void TabbedContentFrame::leaveEvent(QEvent *event)
{
    m_hoverTab = -1;
    unsetCursor();
    update();
    QWidget::leaveEvent(event);
}

void TabbedContentFrame::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        const int tab = tabAt(event->pos());
        if (tab >= 0)
        {
            if (tab != m_currentIndex)
            {
                m_currentIndex = tab;
                update();
            }
            if (m_tabChangedCallback)
                m_tabChangedCallback(tab);
            event->accept();
            return;
        }
    }

    QWidget::mousePressEvent(event);
}

int TabbedContentFrame::tabTop() const
{
    return height() - kTabHeight;
}

QRectF TabbedContentFrame::contentBodyRect() const
{
    return QRectF(0.5, 0.5, qMax(0, width() - 1), qMax(0, tabTop()));
}

void TabbedContentFrame::updateTabRects()
{
    if (m_tabRects.size() != m_tabLabels.size())
        m_tabRects.resize(m_tabLabels.size());

    const QFontMetrics metrics(font());
    const int y = tabTop();

    // Right-aligned, growing leftward -- same layout structview uses.
    int x = width() - kFooterPad;
    for (int i = m_tabLabels.size() - 1; i >= 0; --i)
    {
        const int w = metrics.horizontalAdvance(m_tabLabels.at(i)) + 2 * kTabHorzPad;
        x -= w;
        m_tabRects[i] = QRect(x + 1, y, w, kTabHeight);
        x += 1;
    }
}

QRect TabbedContentFrame::tabGroupRect() const
{
    QRect group;
    for (const QRect &r : m_tabRects)
        group = group.united(r);
    return group;
}

void TabbedContentFrame::updateFooterChildren()
{
    updateTabRects();

    const QRect tabs = tabGroupRect();
    const int footerTop = tabTop();
    const QRect labelRect(kFooterPad, footerTop, qMax(0, tabs.left() - kFooterPad), kTabHeight);

    if (m_statusLabel)
        m_statusLabel->setGeometry(labelRect);
}

int TabbedContentFrame::tabAt(const QPoint &pos) const
{
    for (int i = 0; i < m_tabRects.size(); ++i)
        if (m_tabRects[i].contains(pos))
            return i;
    return -1;
}

void TabbedContentFrame::paintContentBody(QPainter *painter, const QColor &base, const QColor &border)
{
    if (!painter)
        return;

    const QRectF body = contentBodyRect();
    if (body.width() <= 0.0 || body.height() <= 0.0)
        return;

    QPainterPath fillPath;
    fillPath.moveTo(body.left() + kRadius, body.top());
    fillPath.lineTo(body.right() - kRadius, body.top());
    fillPath.quadTo(body.right(), body.top(), body.right(), body.top() + kRadius);
    fillPath.lineTo(body.right(), body.bottom());
    fillPath.lineTo(body.left() + kRadius, body.bottom());
    fillPath.quadTo(body.left(), body.bottom(), body.left(), body.bottom() - kRadius);
    fillPath.lineTo(body.left(), body.top() + kRadius);
    fillPath.quadTo(body.left(), body.top(), body.left() + kRadius, body.top());
    fillPath.closeSubpath();
    painter->fillPath(fillPath, base);

    QPainterPath borderPath;
    borderPath.moveTo(body.left() + kRadius, body.top());
    borderPath.lineTo(body.right() - kRadius, body.top());
    borderPath.quadTo(body.right(), body.top(), body.right(), body.top() + kRadius);
    borderPath.lineTo(body.right(), body.bottom());

    const QRect activeTab = activeTabRect();
    if (!activeTab.isValid())
    {
        borderPath.lineTo(body.left() + kRadius, body.bottom());
    }
    else
    {
        borderPath.lineTo(activeTab.right() + 0.5, body.bottom());
        borderPath.moveTo(activeTab.left() + 0.5, body.bottom());
        borderPath.lineTo(body.left() + kRadius, body.bottom());
    }

    borderPath.quadTo(body.left(), body.bottom(), body.left(), body.bottom() - kRadius);
    borderPath.lineTo(body.left(), body.top() + kRadius);
    borderPath.quadTo(body.left(), body.top(), body.left() + kRadius, body.top());

    painter->setPen(QPen(border, 1.0));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(borderPath);
}

QRect TabbedContentFrame::activeTabRect() const
{
    return m_currentIndex >= 0 && m_currentIndex < m_tabRects.size() ? m_tabRects[m_currentIndex] : QRect();
}

void TabbedContentFrame::paintTabChrome(QPainter *painter, const QRect &rect, const QColor &fill, bool active)
{
    if (!painter || !rect.isValid())
        return;

    const QColor border = palette().color(QPalette::Mid);
    QRectF tabRect(rect);
    tabRect.adjust(0.5, 0.5, -0.5, -0.5);

    QPainterPath fillPath;
    fillPath.moveTo(tabRect.left(), tabRect.top());
    fillPath.lineTo(tabRect.right(), tabRect.top());
    fillPath.lineTo(tabRect.right(), tabRect.bottom() - kRadius);
    fillPath.quadTo(tabRect.right(), tabRect.bottom(),
                    tabRect.right() - kRadius, tabRect.bottom());
    fillPath.lineTo(tabRect.left() + kRadius, tabRect.bottom());
    fillPath.quadTo(tabRect.left(), tabRect.bottom(),
                    tabRect.left(), tabRect.bottom() - kRadius);
    fillPath.lineTo(tabRect.left(), tabRect.top());
    fillPath.closeSubpath();
    painter->fillPath(fillPath, fill);

    QPainterPath borderPath;
    if (active)
    {
        borderPath.moveTo(tabRect.left(), tabRect.top());
        borderPath.lineTo(tabRect.left(), tabRect.bottom() - kRadius);
        borderPath.quadTo(tabRect.left(), tabRect.bottom(),
                          tabRect.left() + kRadius, tabRect.bottom());
        borderPath.lineTo(tabRect.right() - kRadius, tabRect.bottom());
        borderPath.quadTo(tabRect.right(), tabRect.bottom(),
                          tabRect.right(), tabRect.bottom() - kRadius);
        borderPath.lineTo(tabRect.right(), tabRect.top());
    }
    else
    {
        borderPath.moveTo(tabRect.left(), tabRect.top());
        borderPath.lineTo(tabRect.right(), tabRect.top());
        borderPath.lineTo(tabRect.right(), tabRect.bottom() - kRadius);
        borderPath.quadTo(tabRect.right(), tabRect.bottom(),
                          tabRect.right() - kRadius, tabRect.bottom());
        borderPath.lineTo(tabRect.left() + kRadius, tabRect.bottom());
        borderPath.quadTo(tabRect.left(), tabRect.bottom(),
                          tabRect.left(), tabRect.bottom() - kRadius);
        borderPath.lineTo(tabRect.left(), tabRect.top());
    }

    painter->setPen(QPen(border, 1.0));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(borderPath);
}

void TabbedContentFrame::paintTab(QPainter *painter, const QRect &rect, const QString &text, int index)
{
    if (!painter || !rect.isValid())
        return;

    const bool active = m_currentIndex == index;
    const bool hovered = m_hoverTab == index;
    const QColor border = palette().color(QPalette::Mid);
    const QColor fill = active ? palette().color(QPalette::Base)
        : hovered ? palette().color(QPalette::Button).lighter(104)
                  : palette().color(QPalette::Button);
    const QColor textColor = active || hovered
        ? palette().color(QPalette::WindowText)
        : filestats::subduedTextColor(palette());

    paintTabChrome(painter, rect, fill, active);

    painter->setPen(textColor);
    painter->drawText(rect.adjusted(kTabHorzPad, 0, -kTabHorzPad, -1),
                      Qt::AlignCenter, text);
}

// â”€â”€ PropertyRow â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

PropertyRow::PropertyRow(const QString &label, QLabel **valueOut, QWidget *parent, Action action,
                         std::function<void()> actionCallback, QCheckBox **checkBoxOut, bool checked)
    : QWidget(parent), m_action(action), m_actionCallback(std::move(actionCallback))
{
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(8);

    if (checkBoxOut)
    {
        static constexpr int kCheckBoxLeftPadding  = 0;
        static constexpr int kCheckBoxRightPadding = 6;
        auto                *checkWrap             = new QWidget(this);
        checkWrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        auto *checkLayout = new QHBoxLayout(checkWrap);
        checkLayout->setContentsMargins(kCheckBoxLeftPadding, 0, kCheckBoxRightPadding, 0);
        checkLayout->setSpacing(0);
        auto *checkBox = new QCheckBox(checkWrap);
        checkBox->setChecked(checked);
        checkBox->setCursor(Qt::PointingHandCursor);
        checkBox->setFocusPolicy(Qt::StrongFocus);
        checkLayout->addWidget(checkBox, 0, Qt::AlignVCenter);
        layout->addWidget(checkWrap, 0, Qt::AlignVCenter);
        *checkBoxOut = checkBox;
    }

    auto *textLayout = new QVBoxLayout;
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(3);

    m_nameLabel     = new QLabel(label, this);
    QFont labelFont = m_nameLabel->font();
    if (labelFont.pointSizeF() > 0)
        labelFont.setPointSizeF(qMax(1.0, labelFont.pointSizeF() - 1.0));
    else if (labelFont.pixelSize() > 0)
        labelFont.setPixelSize(qMax(1, labelFont.pixelSize() - 1));
    m_nameLabel->setFont(labelFont);
    m_nameLabel->setMinimumWidth(0);
    m_nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_nameLabel->setStyleSheet(QStringLiteral("color: %1;").arg(cssColor(subduedTextColor(palette()))));

    m_valueLabel = new QLabel(this);
    m_valueLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_valueLabel->setWordWrap(true);
    m_valueLabel->setMinimumWidth(0);
    m_valueLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    textLayout->addWidget(m_nameLabel);
    textLayout->addWidget(m_valueLabel);
    layout->addLayout(textLayout, 1);

    m_actionIcon = new QLabel(this);
    m_actionIcon->setFixedSize(28, 28);
    m_actionIcon->setAlignment(Qt::AlignCenter);
    m_actionIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
    const QString iconName = action == Action::OpenExternal ? QStringLiteral("actions/external-link-symbolic")
                                                            : QStringLiteral("actions/edit-copy-symbolic");
    m_actionIcon->setPixmap(recoloredIcon(iconName, palette().windowText().color(), 16).pixmap(16, 16));
    m_actionIcon->hide();
    updateActionIconStyle();
    layout->addWidget(m_actionIcon, 0, Qt::AlignVCenter);

    m_feedback = new QLabel(QObject::tr("Copied to clipboard"), this);
    m_feedback->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_feedback->setAlignment(Qt::AlignCenter);
    m_feedback->setStyleSheet(QStringLiteral("QLabel { padding: 4px 8px; border-radius: 4px; "
                                             "background: black; color: white; }"));
    m_feedback->hide();
    m_feedbackOpacity = new QGraphicsOpacityEffect(m_feedback);
    m_feedback->setGraphicsEffect(m_feedbackOpacity);
    m_feedbackFadeIn = new QPropertyAnimation(m_feedbackOpacity, "opacity", this);
    m_feedbackFadeIn->setDuration(140);
    m_feedbackFadeOut = new QPropertyAnimation(m_feedbackOpacity, "opacity", this);
    m_feedbackFadeOut->setDuration(1800);
    connect(m_feedbackFadeOut, &QPropertyAnimation::finished, m_feedback, &QWidget::hide);

    installHoverFilter(this);
    installHoverFilter(m_nameLabel);
    installHoverFilter(m_valueLabel);

    if (valueOut)
        *valueOut = m_valueLabel;
}

void PropertyRow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
    {
        if (isActionHit(event->pos()))
            setActionIconPressed(true);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PropertyRow::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    setActionIconPressed(false);
    if (!rect().contains(mapFromGlobal(QCursor::pos())))
        clearHoveredRow(this);
}

void PropertyRow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    clearHoveredRow(this);
    if (m_actionIcon)
        m_actionIcon->hide();
    setActionIconPressed(false);
    if (m_feedback)
        m_feedback->hide();
}

void PropertyRow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
    {
        setActionIconPressed(false);
        if (isActionHit(event->pos()))
            triggerAction(event->pos());
        event->accept();
        return;
    }
    setActionIconPressed(false);
    QWidget::mouseReleaseEvent(event);
}

bool PropertyRow::eventFilter(QObject *obj, QEvent *event)
{
    auto *eventWidget = qobject_cast<QWidget *>(obj);
    if (!eventWidget)
        return QWidget::eventFilter(obj, event);

    if (event->type() == QEvent::MouseButtonPress)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            const QPoint rowPos = mapFromGlobal(eventWidget->mapToGlobal(mouseEvent->pos()));
            if (rect().contains(rowPos))
            {
                if (isActionHit(rowPos))
                {
                    updateHoverIcon();
                    setActionIconPressed(true);
                }
            }
        }
    }
    else if (event->type() == QEvent::MouseButtonRelease)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            const QPoint rowPos = mapFromGlobal(eventWidget->mapToGlobal(mouseEvent->pos()));
            if (rect().contains(rowPos))
            {
                setActionIconPressed(false);
                if (isActionHit(rowPos))
                    triggerAction(rowPos);
                return true;
            }
        }
    }

    switch (event->type())
    {
    case QEvent::Enter:
    case QEvent::MouseMove:
        setHoveredRow(this);
        break;
    case QEvent::Leave:
        QTimer::singleShot(0, this,
                           [this]()
                           {
                               if (!rect().contains(mapFromGlobal(QCursor::pos())))
                                   clearHoveredRow(this);
                           });
        break;
    default:
        break;
    }
    return QWidget::eventFilter(obj, event);
}

void PropertyRow::updateHoverIcon()
{
    if (!m_actionIcon)
        return;
    m_actionIcon->setVisible(m_iconHovered);
    updateActionIconStyle();
}

PropertyRow *&PropertyRow::hoveredRow()
{
    static PropertyRow *row = nullptr;
    return row;
}

void PropertyRow::setHoveredRow(PropertyRow *row)
{
    PropertyRow *&current = hoveredRow();
    if (current == row)
    {
        row->m_iconHovered = true;
        row->updateHoverIcon();
        return;
    }

    if (current)
    {
        current->m_iconHovered = false;
        current->setActionIconPressed(false);
        current->updateHoverIcon();
    }

    current = row;
    if (current)
    {
        current->m_iconHovered = true;
        current->updateHoverIcon();
    }
}

void PropertyRow::clearHoveredRow(PropertyRow *row)
{
    PropertyRow *&current = hoveredRow();
    if (current != row)
        return;
    current->m_iconHovered = false;
    current->setActionIconPressed(false);
    current->updateHoverIcon();
    current = nullptr;
}

void PropertyRow::setActionIconPressed(bool pressed)
{
    if (!m_actionIcon)
        return;
    m_iconPressed = pressed;
    updateActionIconStyle();
}

void PropertyRow::updateActionIconStyle()
{
    if (!m_actionIcon)
        return;
    const bool    dark      = qApp->palette().window().color().lightness() < 128;
    const QString pressedBg = dark ? QStringLiteral("rgba(255,255,255,0.25)") : QStringLiteral("rgba(0,0,0,0.18)");
    m_actionIcon->setStyleSheet(m_iconPressed ? QStringLiteral("border-radius: 6px; background: %1;").arg(pressedBg)
                                              : QStringLiteral("border-radius: 6px;"));
}

void PropertyRow::triggerAction(const QPoint &clickPos)
{
    if (m_actionCallback)
    {
        m_actionCallback();
        return;
    }
    if (m_valueLabel)
    {
        QApplication::clipboard()->setText(m_valueLabel->text());
        showCopiedFeedback(clickPos);
    }
}

bool PropertyRow::isActionHit(const QPoint &pos) const
{
    int left = width();
    if (m_nameLabel)
        left = qMin(left, m_nameLabel->geometry().left());
    if (m_valueLabel)
        left = qMin(left, m_valueLabel->geometry().left());
    return pos.x() >= left;
}

void PropertyRow::positionFeedback(const QPoint &clickPos)
{
    if (!m_feedback)
        return;
    m_feedback->adjustSize();
    constexpr int kOffsetX = 12;
    constexpr int kOffsetY = 12;
    const int     x        = qBound(0, clickPos.x() + kOffsetX, qMax(0, width() - m_feedback->width()));
    const int     y        = qBound(0, clickPos.y() + kOffsetY, qMax(0, height() - m_feedback->height()));
    m_feedback->move(x, y);
    m_feedback->raise();
}

void PropertyRow::showCopiedFeedback(const QPoint &clickPos)
{
    if (!m_feedback || !m_feedbackOpacity || !m_feedbackFadeIn || !m_feedbackFadeOut)
        return;
    m_feedbackFadeIn->stop();
    m_feedbackFadeOut->stop();
    positionFeedback(clickPos);
    m_feedbackOpacity->setOpacity(0.0);
    m_feedback->show();
    m_feedback->raise();
    m_feedbackFadeIn->setStartValue(0.0);
    m_feedbackFadeIn->setEndValue(1.0);
    m_feedbackFadeIn->start();

    QTimer::singleShot(650, this,
                       [this]()
                       {
                           if (!m_feedback || !m_feedback->isVisible())
                               return;
                           m_feedbackFadeOut->setStartValue(m_feedbackOpacity->opacity());
                           m_feedbackFadeOut->setEndValue(0.0);
                           m_feedbackFadeOut->start();
                       });
}

void PropertyRow::installHoverFilter(QWidget *widget)
{
    widget->setMouseTracking(true);
    widget->installEventFilter(this);
}

} // namespace filestats
