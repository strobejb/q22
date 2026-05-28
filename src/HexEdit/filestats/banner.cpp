#include "filestats/banner.h"

#include "theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
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
                           QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("recalculateStrip"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    const QColor accent = warningBannerAccent();
    const bool dark = parent && parent->palette().window().color().lightness() < 128;
    const QColor bg = warningBannerBackground(parent ? parent->palette() : palette());
    const QColor hover = dark ? accent.lighter(118) : accent.lighter(108);
    const QColor pressed = dark ? accent.lighter(130) : accent.darker(108);
    const QColor text = accent.lightness() < 150 ? QColor(Qt::white) : QColor(Qt::black);

    setStyleSheet(QStringLiteral(R"(
        QFrame#recalculateStrip {
            background: %1;
            border-radius: 6px;
        }
        QFrame#recalculateStrip QPushButton {
            background: %2;
            color: %3;
            border: 1px solid %2;
            border-radius: 6px;
            min-width: 0;
            padding: 4px 14px;
        }
        QFrame#recalculateStrip QPushButton:hover {
            background: %4;
            border-color: %4;
        }
        QFrame#recalculateStrip QPushButton:pressed {
            background: %5;
            border-color: %5;
        }
        QLabel {
            color: %2;
            font-weight: bold;
        }
    )").arg(cssColor(bg), cssColor(accent), cssColor(text),
            cssColor(hover), cssColor(pressed)));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    m_button = new QPushButton(buttonText, this);
    m_button->setCursor(Qt::PointingHandCursor);
    QObject::connect(m_button, &QPushButton::clicked, this, [onClicked]() { onClicked(); });

    const int iconSize = qRound(m_button->sizeHint().height() * 0.75);
    auto *icon = new QLabel(this);
    icon->setFixedSize(iconSize + 8, iconSize);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(recoloredIcon(QStringLiteral("actions/help-about-symbolic"),
                                  warningBannerAccent(),
                                  iconSize).pixmap(iconSize, iconSize));
    layout->addWidget(icon, 0, Qt::AlignVCenter);

    m_message = new QLabel(this);
    m_message->setObjectName(QStringLiteral("recalculateMessage"));
    m_message->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(m_message, 1, Qt::AlignVCenter);
    layout->addWidget(m_button, 0, Qt::AlignVCenter);

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
