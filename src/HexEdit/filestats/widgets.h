#ifndef FILESTATS_WIDGETS_H
#define FILESTATS_WIDGETS_H

#include "filestats/banner.h"
#include "theme.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>
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

class SectionHeader : public QWidget
{
public:
    explicit SectionHeader(const QString &title, QWidget *parent = nullptr)
        : QWidget(parent), m_title(title)
    {
        setFixedHeight(kSectionHeaderHeight);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(kContentMargin, 0, kContentMargin, 0);
        layout->setSpacing(6);

        m_label = new QLabel(title, this);
        QFont font = m_label->font();
        font.setBold(true);
        m_label->setFont(font);
        m_label->setStyleSheet(QStringLiteral("color: %1;")
                               .arg(cssColor(subduedTextColor(palette()))));

        layout->addWidget(m_label, 1, Qt::AlignVCenter);
        layout->addStretch();

        m_icon = new QLabel(this);
        m_icon->setFixedSize(16, 16);
        layout->addWidget(m_icon, 0, Qt::AlignVCenter);

        setCollapsed(false);
        updatePalette();
    }

    QString title() const { return m_title; }
    bool isCollapsed() const { return m_collapsed; }

    void setTitle(const QString &title)
    {
        m_title = title;
        m_label->setText(title);
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

protected:
    void enterEvent(QEnterEvent *event) override
    {
        m_hover = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hover = false;
        update();
        QWidget::leaveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && m_clicked) {
            m_clicked();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().window());
        if (!m_hover)
            return;

        const bool dark = qApp->palette().window().color().lightness() < 128;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(dark ? 255 : 0, dark ? 255 : 0, dark ? 255 : 0,
                                dark ? 28 : 18));
        painter.drawRoundedRect(rect().adjusted(0, 2, 0, -2), 4, 4);
    }

private:
    void updatePalette()
    {
        m_label->setStyleSheet(QStringLiteral("color: %1;")
                               .arg(cssColor(subduedTextColor(palette()))));
    }

    QString m_title;
    QLabel *m_icon = nullptr;
    QLabel *m_label = nullptr;
    std::function<void()> m_clicked;
    bool m_collapsed = false;
    bool m_hover = false;
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
                                    QStringLiteral("actions/stopcircle"),
                                    QObject::tr("Stop"));
}

inline QToolButton *createProgressStartButton(QWidget *parent)
{
    return createProgressToolButton(parent,
                                    QStringLiteral("actions/media-playback-start-symbolic"),
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
                          const std::function<void()> &onRetry)
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

        m_retryStrip = new ActionBanner(QObject::tr("Restart"), onRetry, m_strip);
        auto *retryWrap = new QWidget(m_strip);
        auto *retryLayout = new QVBoxLayout(retryWrap);
        retryLayout->setContentsMargins(0, 0, 0, 0);
        retryLayout->setSpacing(0);
        retryLayout->addWidget(m_retryStrip);
        inner->addWidget(retryWrap);
        inner->addSpacing(kHeaderControlGap);

        QObject::connect(m_startButton, &QToolButton::clicked, m_strip, [onStart]() { onStart(); });
        QObject::connect(m_stopButton, &QToolButton::clicked, m_strip, [onStop]() { onStop(); });
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
        m_stopButton->show();
        m_progressRow->show();
        m_retryStrip->hide();
        updateVisibility();
    }

    void showRetry(const QString &message)
    {
        m_startButton->hide();
        m_startRow->hide();
        m_progress->hide();
        m_stopButton->hide();
        m_progressRow->hide();
        m_retryStrip->setMessage(message);
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

    void setListWidget(QWidget *list)
    {
        m_list = list;
        positionList();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QFrame::resizeEvent(event);
        positionList();
    }

private:
    void positionList()
    {
        if (!m_list)
            return;
        m_list->setGeometry(rect().adjusted(kInset, kInset, -kInset, -kInset));
    }

    static constexpr int kInset = 4;
    QWidget *m_list = nullptr;
};

class PropertyRow : public QWidget
{
public:
    enum class Action { CopyValue, OpenExternal };

    explicit PropertyRow(const QString &label, QLabel **valueOut, QWidget *parent = nullptr,
                         Action action = Action::CopyValue,
                         std::function<void()> actionCallback = {})
        : QWidget(parent), m_action(action), m_actionCallback(std::move(actionCallback))
    {
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 2, 0, 2);
        layout->setSpacing(8);

        auto *textLayout = new QVBoxLayout;
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(3);

        auto *nameLabel = new QLabel(label, this);
        QFont labelFont = nameLabel->font();
        if (labelFont.pointSizeF() > 0)
            labelFont.setPointSizeF(qMax(1.0, labelFont.pointSizeF() - 1.0));
        else if (labelFont.pixelSize() > 0)
            labelFont.setPixelSize(qMax(1, labelFont.pixelSize() - 1));
        nameLabel->setFont(labelFont);
        nameLabel->setMinimumWidth(0);
        nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        nameLabel->setStyleSheet(QStringLiteral("color: %1;")
                                 .arg(cssColor(subduedTextColor(palette()))));

        m_valueLabel = new QLabel(this);
        m_valueLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_valueLabel->setWordWrap(true);
        m_valueLabel->setMinimumWidth(0);
        m_valueLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

        textLayout->addWidget(nameLabel);
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
        installHoverFilter(nameLabel);
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
                    updateHoverIcon();
                    setActionIconPressed(true);
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                const QPoint rowPos = mapFromGlobal(eventWidget->mapToGlobal(mouseEvent->pos()));
                if (rect().contains(rowPos)) {
                    setActionIconPressed(false);
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
