#include "filestats/banner.h"

#include "theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QToolButton>
#include <QtMath>

namespace {

QString cssColor(const QColor &color)
{
    return color.name(QColor::HexArgb);
}

} // namespace

namespace filestats {

ActionBanner::ActionBanner(const QString &buttonText,
                           const std::function<void()> &onClicked,
                           QWidget *parent,
                           const std::function<void()> &onClose,
                           const QString &actionIconResourceName)
    : QFrame(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    const QColor accent = warningBannerAccent();
    const bool dark = parent && parent->palette().window().color().lightness() < 128;
    const QColor bg = warningBannerBackground(parent ? parent->palette() : palette());
    const QColor btnHover = dark ? accent.lighter(118) : accent.lighter(108);
    const QColor btnPressed = dark ? accent.lighter(130) : accent.darker(108);
    const QColor btnText = accent.lightness() < 150 ? QColor(Qt::white) : QColor(Qt::black);
    const QColor fg = bg.lightness() >= 128 ? QColor(Qt::black) : QColor(Qt::white);
    const QColor subtleFg(fg.red(), fg.green(), fg.blue(), 178);
    const QColor toolHover(fg.red(), fg.green(), fg.blue(), 40);
    const QColor toolPressed(fg.red(), fg.green(), fg.blue(), 70);

    setStyleSheet(QStringLiteral(R"(
        QFrame {
            background: %1;
            border-radius: 6px;
        }
        QFrame QPushButton {
            background: %4;
            color: %3;
            border: 1px solid %4;
            border-radius: 6px;
            min-width: 0;
            padding: 4px 14px;
            font-weight: bold;
        }
        QFrame QPushButton:hover { background: %2; border-color: %2; }
        QFrame QPushButton:pressed { background: %5; border-color: %5; }
        QFrame QToolButton {
            border: none;
            border-radius: 6px;
            background: transparent;
        }
        QFrame QToolButton:hover { background: %6; }
        QFrame QToolButton:pressed { background: %7; }
    )").arg(cssColor(bg), cssColor(accent), cssColor(btnText),
            cssColor(btnHover), cssColor(btnPressed),
            cssColor(toolHover), cssColor(toolPressed)));

    const bool hasClose = static_cast<bool>(onClose);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, hasClose ? 4 : 8, 6);
    layout->setSpacing(8);

    m_button = new QPushButton(buttonText, this);
    m_button->setCursor(Qt::PointingHandCursor);
    QObject::connect(m_button, &QPushButton::clicked, this, [onClicked]() { onClicked(); });

    const int iconSize = qRound(m_button->sizeHint().height() * 0.75);
    auto *icon = new QLabel(this);
    icon->setFixedSize(iconSize + 8, iconSize);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(recoloredIcon(actionIconResourceName,
                                  subtleFg, iconSize).pixmap(iconSize, iconSize));
    layout->addWidget(icon, 0, Qt::AlignVCenter);

    m_message = new QLabel(this);
    m_message->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_message->setStyleSheet(QStringLiteral("color: %1;").arg(cssColor(subtleFg)));
    layout->addWidget(m_message, 1, Qt::AlignVCenter);
    layout->addWidget(m_button, 0, Qt::AlignVCenter);

    if (onClose) {
        auto *closeBtn = new QToolButton(this);
        closeBtn->setAutoRaise(true);
        closeBtn->setCursor(Qt::PointingHandCursor);
        closeBtn->setFixedSize(30, 30);
        closeBtn->setIconSize(QSize(16, 16));
        closeBtn->setIcon(recoloredIcon(QStringLiteral("actions/window-close-symbolic"), fg, 16));
        QObject::connect(closeBtn, &QToolButton::clicked, this, [this, onClose]() {
            hide();
            onClose();
        });
        layout->addWidget(closeBtn, 0, Qt::AlignVCenter);
    }

    hide();
}

void ActionBanner::setMessage(const QString &message)
{
    if (m_message)
        m_message->setText(message);
}

void ActionBanner::setButtonText(const QString &text)
{
    if (m_button)
        m_button->setText(text);
}

} // namespace filestats
