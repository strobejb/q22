#include "fileproperties.h"

#include "HexView/hexview.h"
#include "settings/settingscard.h"
#include "theme.h"

#include <QApplication>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QPalette>
#include <QToolButton>
#include <QVBoxLayout>

static QString cssColor(const QColor &color)
{
    return color.name(QColor::HexArgb);
}

static QColor subduedTextColor(const QPalette &palette)
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

class PropertyRow : public QWidget
{
public:
    explicit PropertyRow(const QString &label, QLabel **valueOut, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 2, 0, 2);
        layout->setSpacing(3);

        auto *nameLabel = new QLabel(label, this);
        QFont labelFont = nameLabel->font();
        if (labelFont.pointSizeF() > 0)
            labelFont.setPointSizeF(qMax(1.0, labelFont.pointSizeF() - 1.0));
        else if (labelFont.pixelSize() > 0)
            labelFont.setPixelSize(qMax(1, labelFont.pixelSize() - 1));
        nameLabel->setFont(labelFont);
        nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        nameLabel->setStyleSheet(QStringLiteral("color: %1;")
                                 .arg(cssColor(subduedTextColor(palette()))));

        auto *valueLabel = new QLabel(this);
        valueLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        valueLabel->setWordWrap(true);
        valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        layout->addWidget(nameLabel);
        layout->addWidget(valueLabel);

        if (valueOut)
            *valueOut = valueLabel;
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        return QSize(280, qMax(42, fm.height() * 2 + 16));
    }
};

FilePropertiesPanel::FilePropertiesPanel(HexView *hexView, QWidget *parent)
    : QDialog(parent), m_hexView(hexView)
{
    setWindowTitle(tr("File Properties"));
    setSizeGripEnabled(false);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, pal.color(QPalette::Window));
    setPalette(pal);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 18);
    root->setSpacing(14);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("overlayHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    auto *title = new QLabel(tr("File Properties"), header);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setStyleSheet(QStringLiteral("color: %1;")
                         .arg(cssColor(subduedTextColor(palette()))));

    auto *closeButton = new QToolButton(header);
    closeButton->setAutoRaise(true);
    closeButton->setFixedSize(28, 28);
    closeButton->setIconSize(QSize(16, 16));
    closeButton->setToolTip(tr("Close"));
    closeButton->setProperty("iconThemeName", QStringLiteral("window-close-symbolic"));
    closeButton->setIcon(recoloredIcon(QStringLiteral("actions/window-close-symbolic"),
                                       palette().windowText().color(), 16));
    connect(closeButton, &QToolButton::clicked, this, &FilePropertiesPanel::closeRequested);

    headerLayout->addWidget(title, 0, Qt::AlignVCenter);
    headerLayout->addStretch();
    headerLayout->addWidget(closeButton, 0, Qt::AlignVCenter);
    root->addWidget(header);

    auto *card = new SettingsCard({
        new PropertyRow(tr("Name"), &m_nameValue, this),
        new PropertyRow(tr("Location"), &m_locationValue, this),
        new PropertyRow(tr("Size"), &m_sizeValue, this),
        new PropertyRow(tr("State"), &m_stateValue, this),
    }, SettingsCard::Style::Spaced, this);
    root->addWidget(card);
    root->addStretch();

    setMinimumWidth(260);
    const bool dark = QApplication::palette().window().color().lightness() < 128;
    setStyleSheet(QString(R"(
        QToolButton {
            border: none;
            border-radius: 6px;
            background: transparent;
        }
        QToolButton:hover   { background: %1; }
        QToolButton:focus   { border: 2px solid palette(highlight); }
        QToolButton:pressed { background: %2; }
    )").arg(dark ? "rgba(255,255,255,0.15)" : "rgba(0,0,0,0.10)",
            dark ? "rgba(255,255,255,0.25)" : "rgba(0,0,0,0.18)"));
    refresh();
}

void FilePropertiesPanel::refresh()
{
    if (!m_hexView)
        return;

    const QString path = m_hexView->filePath();
    const QFileInfo info(path);

    m_nameValue->setText(path.isEmpty() ? tr("Untitled") : info.fileName());
    m_locationValue->setText(path.isEmpty() ? tr("Memory") : info.absolutePath());
    m_sizeValue->setText(formatSize(static_cast<qulonglong>(m_hexView->size())));
    m_stateValue->setText(m_hexView->canUndo() ? tr("Modified") : tr("Saved"));
}

QString FilePropertiesPanel::formatSize(qulonglong bytes)
{
    return tr("%1 bytes").arg(QLocale().toString(bytes));
}
