#ifndef FILESTATS_WIDGETS_H
#define FILESTATS_WIDGETS_H

#include "filestats/banner.h"
#include "theme.h"

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QBrush>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace filestats {

static constexpr int kContentMargin = 10;
static constexpr int kSectionHeaderOuterMargin = 0;
static constexpr int kHeaderControlGap = 2;
static constexpr int kGroupTopGap = 20;
static constexpr int kSectionHeaderHeight = 32;
static constexpr int kCardLeftInset = 5;
static constexpr int kCardScrollbarInset = 6;
static constexpr int kSettingsCardShadowInset = 4;
static constexpr int kStringsListMinHeight = 160;

inline QString cssColor(const QColor &color)
{
    return color.name(QColor::HexArgb);
}

inline QColor subduedTextColor(const QPalette &palette)
{
    const QColor text = palette.color(QPalette::WindowText);
    const QColor mid = palette.color(QPalette::Mid);
    constexpr qreal midWeight = 0.55;
    constexpr qreal textWeight = 1.0 - midWeight;
    return QColor(qRound(text.red() * textWeight + mid.red() * midWeight),
                  qRound(text.green() * textWeight + mid.green() * midWeight),
                  qRound(text.blue() * textWeight + mid.blue() * midWeight),
                  qRound(text.alpha() * textWeight + mid.alpha() * midWeight));
}

inline QColor stringsHeaderTextColor(const QPalette &palette)
{
    const QColor mid = palette.color(QPalette::Mid);
    const QColor dark = palette.color(QPalette::Dark);
    constexpr qreal midWeight = 0.45;
    constexpr qreal darkWeight = 1.0 - midWeight;
    return QColor(qRound(mid.red() * midWeight + dark.red() * darkWeight),
                  qRound(mid.green() * midWeight + dark.green() * darkWeight),
                  qRound(mid.blue() * midWeight + dark.blue() * darkWeight),
                  qRound(mid.alpha() * midWeight + dark.alpha() * darkWeight));
}

inline int stringsHeaderPadding(const QFontMetrics &fm)
{
    return qMax(4, qRound(fm.height() * 0.6));
}

inline int stringsHeaderGap(const QFontMetrics &fm)
{
    return qMax(4, fm.height() / 4);
}

static constexpr int kStringFooterRole = Qt::UserRole + 2;

class SectionHeader : public QWidget
{
public:
    explicit SectionHeader(const QString &title, QWidget *parent = nullptr)
        : QWidget(parent), m_title(title)
    {
        setFixedHeight(kSectionHeaderHeight);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        m_icon = new QLabel(this);
        m_icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_icon->setFixedSize(16, 16);

        setCollapsed(false);
    }

    QString title() const { return m_title; }
    bool isCollapsed() const { return m_collapsed; }

    void setTitle(const QString &title)
    {
        m_title = title;
        update();
    }

    void setCollapsed(bool collapsed)
    {
        m_collapsed = collapsed;
        const QString iconName = collapsed
                                     ? QStringLiteral("ui/go-down-symbolic")
                                     : QStringLiteral("ui/go-up-symbolic");
        m_icon->setPixmap(recoloredIcon(iconName, palette().windowText().color(), 16).pixmap(16, 16));
    }

    void setClickedCallback(std::function<void()> callback)
    {
        m_clicked = std::move(callback);
    }

    void setDragCallbacks(std::function<void(QPoint)> started,
                          std::function<void(QPoint)> moved,
                          std::function<void(QPoint)> ended)
    {
        m_dragStarted = std::move(started);
        m_dragMoved = std::move(moved);
        m_dragEnded = std::move(ended);
    }

    void setDragTarget(bool on)  { m_isDragTarget = on; update(); }

    void syncHoverFromCursor()
    {
        const bool over = rect().contains(mapFromGlobal(QCursor::pos()));
        if (m_hover == over)
            return;
        m_hover = over;
        update();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        m_icon->move(width() - kContentMargin - 16, (height() - 16) / 2);
    }

    void enterEvent(QEnterEvent *event) override
    {
        // Suppress hover while another widget holds the mouse grab (e.g. the
        // header being dragged).  grabMouse() only captures button/move events
        // so enterEvent still fires on bystander headers — without this guard
        // they accumulate stale m_hover = true that persists after the drag.
        if (!QWidget::mouseGrabber() || QWidget::mouseGrabber() == this)
            m_hover = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        if (!m_dragging)
            m_hover = false;
        update();
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_dragStarted && !chevronHitRect().contains(event->pos())) {
            m_pressing = true;
            m_dragging = false;
            m_dragMovedAfterStart = false;
            m_pressGlobal = event->globalPosition().toPoint();
            QApplication::setOverrideCursor(Qt::ClosedHandCursor);
            update();
            QTimer::singleShot(750, this, [this]() {
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

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_pressing && !m_dragging) {
            const QPoint delta = event->globalPosition().toPoint() - m_pressGlobal;
            if (delta.manhattanLength() > 6) {
                m_dragging = true;
                grabMouse();
                if (m_dragStarted)
                    m_dragStarted(event->globalPosition().toPoint());
            }
        }
        if (m_dragging && m_dragMoved) {
            m_dragMovedAfterStart = true;
            m_dragMoved(event->globalPosition().toPoint());
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (m_pressing) {
                QApplication::restoreOverrideCursor();
                const bool wasDragging = m_dragging;
                if (m_dragging) {
                    releaseMouse();
                    m_dragging = false;
                    if (m_dragEnded)
                        m_dragEnded(event->globalPosition().toPoint());
                    if (!m_dragMovedAfterStart && rect().contains(event->pos()) && m_clicked)
                        m_clicked();
                    // rebuildSectionLayout() may have moved this header to a new
                    // position. layout->activate() is asynchronous, so the
                    // geometry isn't current yet — defer one tick until it is.
                    QTimer::singleShot(0, this, [this]() {
                        syncHoverFromCursor();
                    });
                } else if (rect().contains(event->pos()) && m_clicked) {
                    m_clicked();
                }
                m_pressing = false;
                if (!wasDragging && !rect().contains(event->pos()))
                    m_hover = false;
                update();
                event->accept();
                return;
            }
            if (rect().contains(event->pos()) && m_clicked) {
                m_clicked();
                event->accept();
                return;
            }
        }
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().window());

        const bool dark = palette().window().color().lightness() < 128;
        if (m_hover || m_pressing) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(dark ? 255 : 0, dark ? 255 : 0, dark ? 255 : 0,
                                    dark ? 28 : 18));
            painter.drawRoundedRect(rect().adjusted(0, 2, 0, -2), 4, 4);
        }

        // Title text — clip to avoid the collapse icon on the right
        QFont boldFont = font();
        boldFont.setBold(true);
        painter.setFont(boldFont);
        painter.setPen(subduedTextColor(palette()));
        const int textRight = width() - kContentMargin - 16 - 4;
        QString displayTitle = m_title;
        if ((m_hover || m_pressing) && m_dragStarted)
            displayTitle.replace(QStringLiteral(" - paused"), QString());
        painter.drawText(QRect(kContentMargin, 0, textRight - kContentMargin, height()),
                         Qt::AlignVCenter | Qt::AlignLeft, displayTitle);

        // Hamburger: gradient fade over title text + icon, drawn on top
        if ((m_hover || m_pressing) && m_dragStarted) {
            const QColor base = palette().window().color();
            const int c = dark ? 255 : 0;
            const int blend = dark ? 28 : 18;
            const QColor bg(
                (base.red()   * (255 - blend) + c * blend) / 255,
                (base.green() * (255 - blend) + c * blend) / 255,
                (base.blue()  * (255 - blend) + c * blend) / 255);

            const int iconLeft = (width() - 16) / 2;
            const int fadeStart = iconLeft - 32;
            QLinearGradient grad(fadeStart, 0, iconLeft, 0);
            grad.setColorAt(0.0, QColor(bg.red(), bg.green(), bg.blue(), 0));
            grad.setColorAt(1.0, bg);
            painter.fillRect(fadeStart, 2, 32, height() - 4, grad);
            painter.fillRect(iconLeft, 2, 16, height() - 4, bg);

            QColor iconColor = subduedTextColor(palette());
            if (!m_pressing) {
                const QColor bg = palette().window().color();
                constexpr qreal bgWeight = 0.35;
                constexpr qreal iconWeight = 1.0 - bgWeight;
                iconColor = QColor(qRound(iconColor.red() * iconWeight + bg.red() * bgWeight),
                                   qRound(iconColor.green() * iconWeight + bg.green() * bgWeight),
                                   qRound(iconColor.blue() * iconWeight + bg.blue() * bgWeight),
                                   iconColor.alpha());
            }
            const QPixmap icon = recoloredIcon(
                QStringLiteral("actions/open-menu-symbolic"),
                iconColor, 16).pixmap(16, 16);
            painter.drawPixmap(iconLeft, (height() - 16) / 2, icon);
        }

        if (m_isDragTarget) {
            painter.setPen(QPen(QColor(dark ? 255 : 0, dark ? 255 : 0, dark ? 255 : 0,
                                      dark ? 70 : 55), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(rect().adjusted(1, 3, -1, -3), 4, 4);
        }
    }

private:
    QRect chevronHitRect() const
    {
        constexpr int kChevronSlop = 8;
        const int iconLeft = width() - kContentMargin - 16;
        return QRect(iconLeft - kChevronSlop, 0,
                     width() - iconLeft + kChevronSlop, height());
    }

    QString m_title;
    QLabel *m_icon = nullptr;
    std::function<void()> m_clicked;
    std::function<void(QPoint)> m_dragStarted;
    std::function<void(QPoint)> m_dragMoved;
    std::function<void(QPoint)> m_dragEnded;
    bool m_collapsed = false;
    bool m_hover = false;
    bool m_pressing = false;
    bool m_dragging = false;
    bool m_dragMovedAfterStart = false;
    bool m_isDragTarget = false;
    QPoint m_pressGlobal;
};

inline QToolButton *createProgressToolButton(QWidget *parent,
                                             const QString &iconName,
                                             const QString &toolTip)
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
    const bool dark = parent->palette().window().color().lightness() < 128;
    const QString hover = dark ? QStringLiteral("rgba(255,255,255,0.15)")
                               : QStringLiteral("rgba(0,0,0,0.10)");
    const QString pressed = dark ? QStringLiteral("rgba(255,255,255,0.25)")
                                 : QStringLiteral("rgba(0,0,0,0.18)");
    button->setStyleSheet(QStringLiteral(R"(
        QToolButton {
            border: none;
            border-radius: 6px;
            background: transparent;
        }
        QToolButton:hover { background: %1; }
        QToolButton:pressed { background: %2; }
        QToolButton::menu-indicator { image: none; width: 0; }
    )").arg(hover, pressed));
    button->hide();
    return button;
}

inline QToolButton *createProgressStopButton(QWidget *parent)
{
    return createProgressToolButton(parent,
                                    QStringLiteral("actions/circle-stop-solid"),
                                    QObject::tr("Stop"));
}

inline QToolButton *createProgressStartButton(QWidget *parent)
{
    return createProgressToolButton(parent,
                                    QStringLiteral("actions/circle-play-solid"),
                                    QObject::tr("Start"));
}

inline QHBoxLayout *createProgressRowLayout(QWidget *row)
{
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    return layout;
}

class SectionOperationStrip
{
public:
    SectionOperationStrip(QWidget *parent,
                          const std::function<void()> &onStart,
                          const std::function<void()> &onStop,
                          const std::function<void()> &onResume,
                          const std::function<void()> &onRetry)
        : m_onStop(onStop), m_onResume(onResume)
    {
        m_strip = new QWidget(parent);
        auto *outer = new QVBoxLayout(m_strip);
        outer->setContentsMargins(kSectionHeaderOuterMargin + kCardLeftInset, 0,
                                  kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
        outer->setSpacing(0);

        auto *inner = new QVBoxLayout;
        inner->setContentsMargins(kSettingsCardShadowInset, 0,
                                  kSettingsCardShadowInset, 0);
        inner->setSpacing(0);
        outer->addLayout(inner);

        m_startButton = createProgressStartButton(m_strip);
        m_startLabel = new QLabel(QObject::tr("Begin scan"), m_strip);
        m_startLabel->setStyleSheet(QStringLiteral("color: %1;")
                                    .arg(cssColor(subduedTextColor(parent->palette()))));
        m_startRow = new QWidget(m_strip);
        auto *startLayout = createProgressRowLayout(m_startRow);
        startLayout->addWidget(m_startButton, 0, Qt::AlignVCenter);
        startLayout->addWidget(m_startLabel, 0, Qt::AlignVCenter);
        startLayout->addStretch(1);
        inner->addWidget(m_startRow);

        m_progress = new QProgressBar(m_strip);
        m_progress->setRange(0, 0);
        m_progress->setTextVisible(false);
        m_progress->setFixedHeight(6);

        m_stopButton = createProgressStopButton(m_strip);
        m_progressRow = new QWidget(m_strip);
        auto *progressLayout = createProgressRowLayout(m_progressRow);
        progressLayout->addWidget(m_stopButton, 0, Qt::AlignVCenter);
        progressLayout->addWidget(m_progress, 1, Qt::AlignVCenter);
        inner->addWidget(m_progressRow);

        m_retryStrip = new ActionBanner(QObject::tr("Restart"), onRetry, m_strip);//, QString("actions/circle-info-solid"));
        auto *retryWrap = new QWidget(m_strip);
        auto *retryLayout = new QVBoxLayout(retryWrap);
        retryLayout->setContentsMargins(0, 0, 0, 0);
        retryLayout->setSpacing(0);
        retryLayout->addWidget(m_retryStrip);
        inner->addWidget(retryWrap);
        inner->addSpacing(kHeaderControlGap);

        QObject::connect(m_startButton, &QToolButton::clicked, m_strip, [onStart]() { onStart(); });
        QObject::connect(m_stopButton, &QToolButton::clicked, m_strip, [this]() {
            if (m_progressAction == ProgressAction::Resume) {
                if (m_onResume)
                    m_onResume();
            } else if (m_onStop) {
                m_onStop();
            }
        });
        clear();
    }

    QWidget *widget() const { return m_strip; }
    QProgressBar *progressBar() const { return m_progress; }
    QToolButton *stopButton() const { return m_stopButton; }
    bool hasOperation() const
    {
        return !m_startRow->isHidden() || !m_progressRow->isHidden() || !m_retryStrip->isHidden();
    }

    void setCollapsed(bool collapsed)
    {
        m_collapsed = collapsed;
        updateVisibility();
    }

    void showProgress()
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

    void setProgressActionStop()
    {
        setProgressAction(ProgressAction::Stop);
    }

    void setProgressActionResume()
    {
        setProgressAction(ProgressAction::Resume);
    }

    void showRetry(const QString &message)
    {
        showAction(message, QObject::tr("Restart"));
    }

    void showRescan(const QString &message)
    {
        showAction(message, QObject::tr("Rescan"));
    }

    void showAction(const QString &message, const QString &buttonText)
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

    void showStart(const QString &message)
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

    void clear()
    {
        m_startButton->hide();
        m_startRow->hide();
        m_progress->hide();
        m_stopButton->hide();
        m_progressRow->hide();
        m_retryStrip->hide();
        updateVisibility();
    }

private:
    enum class ProgressAction { Stop, Resume };

    void setProgressAction(ProgressAction action)
    {
        m_progressAction = action;
        const QString iconName = action == ProgressAction::Resume
                                     ? QStringLiteral("actions/circle-play-solid")
                                     : QStringLiteral("actions/circle-stop-solid");
        m_stopButton->setToolTip(action == ProgressAction::Resume
                                     ? QObject::tr("Resume")
                                     : QObject::tr("Stop"));
        m_stopButton->setProperty("iconThemeName", iconName);
        m_stopButton->setIcon(recoloredIcon(iconName, m_strip->palette().buttonText().color(), 16));
    }

    void updateVisibility()
    {
        m_strip->setVisible(!m_collapsed && hasOperation());
    }

    QWidget *m_strip = nullptr;
    QWidget *m_startRow = nullptr;
    QLabel *m_startLabel = nullptr;
    QToolButton *m_startButton = nullptr;
    QWidget *m_progressRow = nullptr;
    QProgressBar *m_progress = nullptr;
    QToolButton *m_stopButton = nullptr;
    ActionBanner *m_retryStrip = nullptr;
    std::function<void()> m_onStop;
    std::function<void()> m_onResume;
    ProgressAction m_progressAction = ProgressAction::Stop;
    bool m_collapsed = false;
};

class VerticalResizeHandle : public QWidget
{
public:
    static constexpr int kHeight = 14;

    explicit VerticalResizeHandle(std::function<void(int)> onDrag, QWidget *parent = nullptr)
        : QWidget(parent), m_onDrag(std::move(onDrag))
    {
        setFixedHeight(kHeight);
        setCursor(Qt::SizeVerCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
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

    void enterEvent(QEnterEvent *) override { m_hovered = true; update(); }
    void leaveEvent(QEvent *) override { m_hovered = false; update(); }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_dragY = qRound(event->globalPosition().y());
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton))
            return;
        const int y = qRound(event->globalPosition().y());
        const int dy = y - m_dragY;
        if (dy != 0) {
            m_dragY = y;
            m_onDrag(dy);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_dragY = 0;
    }

private:
    std::function<void(int)> m_onDrag;
    bool m_hovered = false;
    int m_dragY = 0;
};

class StringListFrame : public QFrame
{
public:
    explicit StringListFrame(QWidget *parent = nullptr)
        : QFrame(parent)
    {
    }

    void setListWidget(QTreeWidget *list)
    {
        m_list = list;
        if (!m_footerText.isEmpty())
            ensureFooterItem();
        refreshFooterAppearance();
        refreshFooterPlacement();
        positionList();
    }

    void setFooterText(const QString &text)
    {
        m_footerText = text;
        if (!m_list)
            return;

        if (text.isEmpty()) {
            removeFooterItem();
        } else {
            ensureFooterItem();
            if (m_footerLabel)
                m_footerLabel->setText(text);
            refreshFooterPlacement();
        }
        positionList();
    }

    void clearFooter()
    {
        setFooterText({});
    }

    void refreshFooterPlacement()
    {
        if (!m_list || !m_footerItem || m_footerText.isEmpty())
            return;

        const int index = m_list->indexOfTopLevelItem(m_footerItem);
        if (index >= 0) {
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

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QFrame::resizeEvent(event);
        positionList();
    }

    void changeEvent(QEvent *event) override
    {
        QFrame::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::PaletteChange)
            refreshFooterAppearance();
    }

private:
    void ensureFooterItem()
    {
        if (m_footerItem)
            return;
        m_footerItem = new QTreeWidgetItem;
        m_footerWidget = new QWidget(m_list);
        auto *layout = new QHBoxLayout(m_footerWidget);
        layout->setContentsMargins(0, 4, 0, 0);
        layout->setSpacing(0);
        layout->addStretch(1);
        m_footerLabel = new QLabel(m_footerWidget);
        m_footerLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_footerLabel, 0, Qt::AlignCenter);
        layout->addStretch(1);
        refreshFooterAppearance();
    }

    void refreshFooterAppearance()
    {
        if (!m_footerItem)
            return;

        QFont footerFont = font();
        if (footerFont.pointSizeF() > 0)
            footerFont.setPointSizeF(qMax(1.0, footerFont.pointSizeF() - 1.0));
        else if (footerFont.pixelSize() > 0)
            footerFont.setPixelSize(qMax(1, footerFont.pixelSize() - 1));

        if (m_footerLabel) {
            m_footerLabel->setFont(footerFont);
            m_footerLabel->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                                         .arg(cssColor(stringsHeaderTextColor(palette()))));
        }
        m_footerItem->setData(0, kStringFooterRole, true);
        m_footerItem->setFirstColumnSpanned(true);
        m_footerItem->setFlags(Qt::NoItemFlags);
    }

    void removeFooterItem()
    {
        if (!m_footerItem)
            return;
        if (m_list) {
            const int index = m_list->indexOfTopLevelItem(m_footerItem);
            if (index >= 0)
                delete m_list->takeTopLevelItem(index);
        }
        delete m_footerWidget;
        m_footerWidget = nullptr;
        m_footerLabel = nullptr;
        delete m_footerItem;
        m_footerItem = nullptr;
    }

    void positionList()
    {
        if (!m_list)
            return;

        const QRect inner = rect().adjusted(kInset, kInset, -kInset, -kInset);
        m_list->setGeometry(inner.left(), inner.top(), inner.width(), inner.height());
    }

    static constexpr int kInset = 4;
    QTreeWidget *m_list = nullptr;
    QTreeWidgetItem *m_footerItem = nullptr;
    QWidget *m_footerWidget = nullptr;
    QLabel *m_footerLabel = nullptr;
    QString m_footerText;
};

class PropertyRow : public QWidget
{
public:
    enum class Action { CopyValue, OpenExternal };

    explicit PropertyRow(const QString &label, QLabel **valueOut, QWidget *parent = nullptr,
                         Action action = Action::CopyValue,
                         std::function<void()> actionCallback = {},
                         QCheckBox **checkBoxOut = nullptr,
                         bool checked = false)
        : QWidget(parent), m_action(action), m_actionCallback(std::move(actionCallback))
    {
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 2, 0, 2);
        layout->setSpacing(8);

        if (checkBoxOut) {
            static constexpr int kCheckBoxLeftPadding = 0;
            static constexpr int kCheckBoxRightPadding = 6;
            auto *checkWrap = new QWidget(this);
            checkWrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
            auto *checkLayout = new QHBoxLayout(checkWrap);
            checkLayout->setContentsMargins(kCheckBoxLeftPadding, 0,
                                            kCheckBoxRightPadding, 0);
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

        m_nameLabel = new QLabel(label, this);
        QFont labelFont = m_nameLabel->font();
        if (labelFont.pointSizeF() > 0)
            labelFont.setPointSizeF(qMax(1.0, labelFont.pointSizeF() - 1.0));
        else if (labelFont.pixelSize() > 0)
            labelFont.setPixelSize(qMax(1, labelFont.pixelSize() - 1));
        m_nameLabel->setFont(labelFont);
        m_nameLabel->setMinimumWidth(0);
        m_nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        m_nameLabel->setStyleSheet(QStringLiteral("color: %1;")
                                   .arg(cssColor(subduedTextColor(palette()))));

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
        const QString iconName = action == Action::OpenExternal
                                     ? QStringLiteral("actions/external-link-symbolic")
                                     : QStringLiteral("actions/edit-copy-symbolic");
        m_actionIcon->setPixmap(recoloredIcon(iconName, palette().windowText().color(), 16).pixmap(16, 16));
        m_actionIcon->hide();
        updateActionIconStyle();
        layout->addWidget(m_actionIcon, 0, Qt::AlignVCenter);

        m_feedback = new QLabel(QObject::tr("Copied to clipboard"), this);
        m_feedback->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_feedback->setAlignment(Qt::AlignCenter);
        m_feedback->setStyleSheet(QStringLiteral(
            "QLabel { padding: 4px 8px; border-radius: 4px; "
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

    QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        return QSize(1, qMax(42, fm.height() * 2 + 16));
    }

    QSize minimumSizeHint() const override
    {
        return QSize(1, sizeHint().height());
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            if (isActionHit(event->pos()))
                setActionIconPressed(true);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        QWidget::leaveEvent(event);
        setActionIconPressed(false);
        if (!rect().contains(mapFromGlobal(QCursor::pos())))
            clearHoveredRow(this);
    }

    void hideEvent(QHideEvent *event) override
    {
        QWidget::hideEvent(event);
        clearHoveredRow(this);
        if (m_actionIcon)
            m_actionIcon->hide();
        setActionIconPressed(false);
        if (m_feedback)
            m_feedback->hide();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            setActionIconPressed(false);
            if (isActionHit(event->pos()))
                triggerAction(event->pos());
            event->accept();
            return;
        }
        setActionIconPressed(false);
        QWidget::mouseReleaseEvent(event);
    }

    bool eventFilter(QObject *obj, QEvent *event) override
    {
        auto *eventWidget = qobject_cast<QWidget *>(obj);
        if (!eventWidget)
            return QWidget::eventFilter(obj, event);

        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                const QPoint rowPos = mapFromGlobal(eventWidget->mapToGlobal(mouseEvent->pos()));
                if (rect().contains(rowPos)) {
                    if (isActionHit(rowPos)) {
                        updateHoverIcon();
                        setActionIconPressed(true);
                    }
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                const QPoint rowPos = mapFromGlobal(eventWidget->mapToGlobal(mouseEvent->pos()));
                if (rect().contains(rowPos)) {
                    setActionIconPressed(false);
                    if (isActionHit(rowPos))
                        triggerAction(rowPos);
                    return true;
                }
            }
        }

        switch (event->type()) {
        case QEvent::Enter:
        case QEvent::MouseMove:
            setHoveredRow(this);
            break;
        case QEvent::Leave:
            QTimer::singleShot(0, this, [this]() {
                if (!rect().contains(mapFromGlobal(QCursor::pos())))
                    clearHoveredRow(this);
            });
            break;
        default:
            break;
        }
        return QWidget::eventFilter(obj, event);
    }

private:
    void updateHoverIcon()
    {
        if (!m_actionIcon)
            return;

        m_actionIcon->setVisible(m_iconHovered);
        updateActionIconStyle();
    }

    static PropertyRow *&hoveredRow()
    {
        static PropertyRow *row = nullptr;
        return row;
    }

    static void setHoveredRow(PropertyRow *row)
    {
        PropertyRow *&current = hoveredRow();
        if (current == row) {
            row->m_iconHovered = true;
            row->updateHoverIcon();
            return;
        }

        if (current) {
            current->m_iconHovered = false;
            current->setActionIconPressed(false);
            current->updateHoverIcon();
        }

        current = row;
        if (current) {
            current->m_iconHovered = true;
            current->updateHoverIcon();
        }
    }

    static void clearHoveredRow(PropertyRow *row)
    {
        PropertyRow *&current = hoveredRow();
        if (current != row)
            return;

        current->m_iconHovered = false;
        current->setActionIconPressed(false);
        current->updateHoverIcon();
        current = nullptr;
    }

    void setActionIconPressed(bool pressed)
    {
        if (!m_actionIcon)
            return;

        m_iconPressed = pressed;
        updateActionIconStyle();
    }

    void updateActionIconStyle()
    {
        if (!m_actionIcon)
            return;

        const bool dark = qApp->palette().window().color().lightness() < 128;
        const QString pressedBg = dark ? QStringLiteral("rgba(255,255,255,0.25)")
                                       : QStringLiteral("rgba(0,0,0,0.18)");
        m_actionIcon->setStyleSheet(m_iconPressed
                                        ? QStringLiteral("border-radius: 6px; background: %1;")
                                              .arg(pressedBg)
                                        : QStringLiteral("border-radius: 6px;"));
    }

    void triggerAction(const QPoint &clickPos)
    {
        if (m_actionCallback) {
            m_actionCallback();
            return;
        }
        if (m_valueLabel) {
            QApplication::clipboard()->setText(m_valueLabel->text());
            showCopiedFeedback(clickPos);
        }
    }

    bool isActionHit(const QPoint &pos) const
    {
        int left = width();
        if (m_nameLabel)
            left = qMin(left, m_nameLabel->geometry().left());
        if (m_valueLabel)
            left = qMin(left, m_valueLabel->geometry().left());
        return pos.x() >= left;
    }

    void positionFeedback(const QPoint &clickPos)
    {
        if (!m_feedback)
            return;

        m_feedback->adjustSize();
        constexpr int kOffsetX = 12;
        constexpr int kOffsetY = 12;
        const int x = qBound(0, clickPos.x() + kOffsetX,
                             qMax(0, width() - m_feedback->width()));
        const int y = qBound(0, clickPos.y() + kOffsetY,
                             qMax(0, height() - m_feedback->height()));
        m_feedback->move(x, y);
        m_feedback->raise();
    }

    void showCopiedFeedback(const QPoint &clickPos)
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

        QTimer::singleShot(650, this, [this]() {
            if (!m_feedback || !m_feedback->isVisible())
                return;
            m_feedbackFadeOut->setStartValue(m_feedbackOpacity->opacity());
            m_feedbackFadeOut->setEndValue(0.0);
            m_feedbackFadeOut->start();
        });
    }

    void installHoverFilter(QWidget *widget)
    {
        widget->setMouseTracking(true);
        widget->installEventFilter(this);
    }

    QLabel *m_nameLabel = nullptr;
    QLabel *m_valueLabel = nullptr;
    QLabel *m_actionIcon = nullptr;
    QLabel *m_feedback = nullptr;
    QGraphicsOpacityEffect *m_feedbackOpacity = nullptr;
    QPropertyAnimation *m_feedbackFadeIn = nullptr;
    QPropertyAnimation *m_feedbackFadeOut = nullptr;
    Action m_action = Action::CopyValue;
    bool m_iconHovered = false;
    bool m_iconPressed = false;
    std::function<void()> m_actionCallback;
};

} // namespace filestats

#endif // FILESTATS_WIDGETS_H
