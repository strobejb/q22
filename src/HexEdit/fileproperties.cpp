#include "fileproperties.h"

#include "HexView/hexview.h"
#include "settings/scrollhintoverlay.h"
#include "settings/settingscard.h"
#include "theme.h"

#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QCursor>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLocale>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPalette>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <array>
#include <algorithm>
#include <functional>
#include <utility>

static constexpr int kContentMargin = 10;
static constexpr int kSectionHeaderOuterMargin = 0;
static constexpr int kHeaderControlGap = 2;
static constexpr int kGroupTopGap = 20;
static constexpr int kSectionHeaderHeight = 32;
static constexpr int kCardScrollbarInset = 6;

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

static QString hexDigest(const QByteArray &digest)
{
    return QString::fromLatin1(digest.toHex());
}

static QString hexNumber(quint32 value, int width)
{
    return QStringLiteral("%1").arg(value, width, 16, QLatin1Char('0')).toUpper();
}

static quint16 updateCrc16Ccitt(quint16 crc, const QByteArray &data)
{
    for (unsigned char byte : data) {
        crc ^= quint16(byte) << 8;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x8000) ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
    }
    return crc;
}

static quint32 updateCrc32Iso(quint32 crc, const QByteArray &data)
{
    for (unsigned char byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return crc;
}

class Md2Hasher
{
public:
    void addData(const QByteArray &data)
    {
        m_partial.append(data);
        while (m_partial.size() >= 16) {
            processMessageBlock(reinterpret_cast<const unsigned char *>(m_partial.constData()));
            m_partial.remove(0, 16);
        }
    }

    QByteArray result()
    {
        const int padLen = 16 - (m_partial.size() % 16);
        m_partial.append(QByteArray(padLen, char(padLen)));
        while (!m_partial.isEmpty()) {
            processMessageBlock(reinterpret_cast<const unsigned char *>(m_partial.constData()));
            m_partial.remove(0, 16);
        }
        transformBlock(m_checksum.data());
        return QByteArray(reinterpret_cast<const char *>(m_state.data()), 16);
    }

private:
    static constexpr std::array<unsigned char, 256> s = {
        41, 46, 67, 201, 162, 216, 124, 1, 61, 54, 84, 161, 236, 240, 6, 19,
        98, 167, 5, 243, 192, 199, 115, 140, 152, 147, 43, 217, 188, 76, 130, 202,
        30, 155, 87, 60, 253, 212, 224, 22, 103, 66, 111, 24, 138, 23, 229, 18,
        190, 78, 196, 214, 218, 158, 222, 73, 160, 251, 245, 142, 187, 47, 238, 122,
        169, 104, 121, 145, 21, 178, 7, 63, 148, 194, 16, 137, 11, 34, 95, 33,
        128, 127, 93, 154, 90, 144, 50, 39, 53, 62, 204, 231, 191, 247, 151, 3,
        255, 25, 48, 179, 72, 165, 181, 209, 215, 94, 146, 42, 172, 86, 170, 198,
        79, 184, 56, 210, 150, 164, 125, 182, 118, 252, 107, 226, 156, 116, 4, 241,
        69, 157, 112, 89, 100, 113, 135, 32, 134, 91, 207, 101, 230, 45, 168, 2,
        27, 96, 37, 173, 174, 176, 185, 246, 28, 70, 97, 105, 52, 64, 126, 15,
        85, 71, 163, 35, 221, 81, 175, 58, 195, 92, 249, 206, 186, 197, 234, 38,
        44, 83, 13, 110, 133, 40, 132, 9, 211, 223, 205, 244, 65, 129, 77, 82,
        106, 220, 55, 200, 108, 193, 171, 250, 36, 225, 123, 8, 12, 189, 177, 74,
        120, 136, 149, 139, 227, 99, 232, 109, 233, 203, 213, 254, 59, 0, 29, 57,
        242, 239, 183, 14, 102, 88, 208, 228, 166, 119, 114, 248, 235, 117, 75, 10,
        49, 68, 80, 180, 143, 237, 31, 26, 219, 153, 141, 51, 159, 17, 131, 20
    };

    void processMessageBlock(const unsigned char *block)
    {
        unsigned char l = m_checksum[15];
        for (int i = 0; i < 16; ++i) {
            m_checksum[i] ^= s[block[i] ^ l];
            l = m_checksum[i];
        }
        transformBlock(block);
    }

    void transformBlock(const unsigned char *block)
    {
        for (int i = 0; i < 16; ++i) {
            m_state[16 + i] = block[i];
            m_state[32 + i] = m_state[16 + i] ^ m_state[i];
        }
        unsigned char t = 0;
        for (int round = 0; round < 18; ++round) {
            for (int i = 0; i < 48; ++i) {
                m_state[i] ^= s[t];
                t = m_state[i];
            }
            t = static_cast<unsigned char>(t + round);
        }
    }

    QByteArray m_partial;
    std::array<unsigned char, 16> m_checksum = {};
    std::array<unsigned char, 48> m_state = {};
};

static QHash<QString, QString> unavailableChecksums(const QString &message)
{
    QHash<QString, QString> results;
    for (const QString &name : {QStringLiteral("SHA1"), QStringLiteral("SHA256"),
                                QStringLiteral("MD2"), QStringLiteral("MD4"),
                                QStringLiteral("MD5"), QStringLiteral("CRC16"),
                                QStringLiteral("CRC32")}) {
        results.insert(name, message);
    }
    return results;
}

static QHash<QString, QString> calculateChecksums(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return unavailableChecksums(QFileInfo(path).exists()
                                    ? FilePropertiesPanel::tr("Unable to read")
                                    : FilePropertiesPanel::tr("No file"));

    Md2Hasher md2;
    QCryptographicHash md4(QCryptographicHash::Md4);
    QCryptographicHash md5(QCryptographicHash::Md5);
    QCryptographicHash sha1(QCryptographicHash::Sha1);
    QCryptographicHash sha256(QCryptographicHash::Sha256);
    quint16 crc16 = 0xFFFF;
    quint32 crc32 = 0xFFFFFFFFu;

    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError)
            return unavailableChecksums(FilePropertiesPanel::tr("Read failed"));

        md2.addData(chunk);
        md4.addData(QByteArrayView(chunk.constData(), chunk.size()));
        md5.addData(QByteArrayView(chunk.constData(), chunk.size()));
        sha1.addData(QByteArrayView(chunk.constData(), chunk.size()));
        sha256.addData(QByteArrayView(chunk.constData(), chunk.size()));
        crc16 = updateCrc16Ccitt(crc16, chunk);
        crc32 = updateCrc32Iso(crc32, chunk);
    }

    QHash<QString, QString> results;
    results.insert(QStringLiteral("MD2"), hexDigest(md2.result()));
    results.insert(QStringLiteral("MD4"), hexDigest(md4.result()));
    results.insert(QStringLiteral("MD5"), hexDigest(md5.result()));
    results.insert(QStringLiteral("SHA1"), hexDigest(sha1.result()));
    results.insert(QStringLiteral("SHA256"), hexDigest(sha256.result()));
    results.insert(QStringLiteral("CRC16"), hexNumber(crc16, 4));
    results.insert(QStringLiteral("CRC32"), hexNumber(crc32 ^ 0xFFFFFFFFu, 8));
    return results;
}

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

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        QWidget::leaveEvent(event);
        setActionIconPressed(false);
        updateHoverIcon();
    }

    void hideEvent(QHideEvent *event) override
    {
        QWidget::hideEvent(event);
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
            m_iconHovered = true;
            updateActionIconStyle();
            updateHoverIcon();
            QTimer::singleShot(0, this, [this]() { updateHoverIcon(); });
            break;
        case QEvent::Leave:
            m_iconHovered = rect().contains(mapFromGlobal(QCursor::pos()));
            updateActionIconStyle();
            updateHoverIcon();
            QTimer::singleShot(0, this, [this]() { updateHoverIcon(); });
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

        const QPoint local = mapFromGlobal(QCursor::pos());
        m_iconHovered = isVisible() && rect().contains(local);
        m_actionIcon->setVisible(m_iconHovered);
        updateActionIconStyle();
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
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_scrollArea->verticalScrollBar()->setFocusPolicy(Qt::NoFocus);
    m_scrollArea->viewport()->setAutoFillBackground(false);
    m_scrollArea->setStyleSheet(QStringLiteral(R"(
        QScrollArea {
            background: transparent;
            border: none;
        }
    )"));
    ScrollHintOverlay::install(m_scrollArea);
    m_scrollArea->verticalScrollBar()->setProperty("filePropertiesScrollBar", true);
    m_scrollArea->verticalScrollBar()->style()->unpolish(m_scrollArea->verticalScrollBar());
    m_scrollArea->verticalScrollBar()->style()->polish(m_scrollArea->verticalScrollBar());

    m_content = new QWidget(m_scrollArea);
    m_content->setMinimumWidth(0);
    m_content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(kSectionHeaderOuterMargin, kContentMargin,
                                      kSectionHeaderOuterMargin, kContentMargin);
    contentLayout->setSpacing(0);

    m_fileHeader = new SectionHeader(tr("File Properties"), m_content);
    m_fileHeader->setClickedCallback([this]() {
        setFileSectionCollapsed(!m_fileSectionCollapsed);
    });
    contentLayout->addWidget(m_fileHeader);
    contentLayout->setAlignment(m_fileHeader, Qt::AlignLeft);
    m_fileHeader->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_fileHeaderGap = new QSpacerItem(0, kHeaderControlGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_fileHeaderGap);

    m_fileSectionBody = new QWidget(m_content);
    m_fileSectionBody->setMinimumWidth(0);
    auto *fileBodyLayout = new QVBoxLayout(m_fileSectionBody);
    fileBodyLayout->setContentsMargins(kSectionHeaderOuterMargin, 0,
                                       kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
    fileBodyLayout->setSpacing(0);

    auto *card = new SettingsCard({
        new PropertyRow(tr("Name"), &m_nameValue, m_fileSectionBody),
        new PropertyRow(tr("Location"), &m_locationValue, m_fileSectionBody,
                        PropertyRow::Action::OpenExternal,
                        [this]() {
                            if (!m_hexView)
                                return;
                            const QString path = m_hexView->filePath();
                            if (!path.isEmpty())
                                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
                        }),
        new PropertyRow(tr("Size"), &m_sizeValue, m_fileSectionBody),
        new PropertyRow(tr("State"), &m_stateValue, m_fileSectionBody),
    }, SettingsCard::Style::Spaced, m_fileSectionBody);
    card->setMinimumWidth(0);
    fileBodyLayout->addWidget(card);
    contentLayout->addWidget(m_fileSectionBody);

    m_betweenSectionsGap = new QSpacerItem(0, kGroupTopGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_betweenSectionsGap);
    m_checksumHeader = new SectionHeader(tr("Checksums"), m_content);
    m_checksumHeader->setClickedCallback([this]() {
        setChecksumSectionCollapsed(!m_checksumSectionCollapsed);
    });
    contentLayout->addWidget(m_checksumHeader);
    contentLayout->setAlignment(m_checksumHeader, Qt::AlignLeft);
    m_checksumHeader->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_checksumHeaderGap = new QSpacerItem(0, kHeaderControlGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_checksumHeaderGap);

    m_checksumSectionBody = new QWidget(m_content);
    m_checksumSectionBody->setMinimumWidth(0);
    auto *checksumBodyLayout = new QVBoxLayout(m_checksumSectionBody);
    checksumBodyLayout->setContentsMargins(kSectionHeaderOuterMargin, 0,
                                           kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
    checksumBodyLayout->setSpacing(0);

    m_checksumProgress = new QProgressBar(m_checksumSectionBody);
    m_checksumProgress->setRange(0, 0);
    m_checksumProgress->setTextVisible(false);
    m_checksumProgress->setFixedHeight(6);
    checksumBodyLayout->addWidget(m_checksumProgress);
    checksumBodyLayout->addSpacing(kHeaderControlGap);

    auto checksumRow = [this](const QString &name) {
        QLabel *value = nullptr;
        auto *row = new PropertyRow(name, &value, m_checksumSectionBody);
        m_checksumValues.insert(name, value);
        return row;
    };

    auto *checksumCard = new SettingsCard({
        checksumRow(QStringLiteral("SHA1")),
        checksumRow(QStringLiteral("SHA256")),
        checksumRow(QStringLiteral("MD2")),
        checksumRow(QStringLiteral("MD4")),
        checksumRow(QStringLiteral("MD5")),
        checksumRow(QStringLiteral("CRC16")),
        checksumRow(QStringLiteral("CRC32")),
    }, SettingsCard::Style::Spaced, m_checksumSectionBody);
    checksumCard->setMinimumWidth(0);
    checksumBodyLayout->addWidget(checksumCard);
    contentLayout->addWidget(m_checksumSectionBody);
    contentLayout->addStretch();

    m_scrollArea->setWidget(m_content);
    root->addWidget(m_scrollArea, 1);

    m_stickyHeader = new SectionHeader(QString(), m_scrollArea->viewport());
    m_stickyHeader->hide();
    m_stickyHeader->raise();
    m_stickyHeader->setClickedCallback([this]() {
        if (!m_scrollArea)
            return;
        const int fileY = m_fileHeader->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
        const int checksumY = m_checksumHeader->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
        if (checksumY <= 0 || (fileY > 0 && checksumY < fileY))
            setChecksumSectionCollapsed(!m_checksumSectionCollapsed);
        else
            setFileSectionCollapsed(!m_fileSectionCollapsed);
    });
    connect(this, &FilePropertiesPanel::sectionReady,
            this, [this](Section section) {
                if (section == Section::Checksums)
                    maybeStartChecksumCalculation();
            });
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &FilePropertiesPanel::updateStickyHeader);
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::rangeChanged,
            this, [this]() { updateStickyHeader(); });
    QTimer::singleShot(0, this, &FilePropertiesPanel::updateStickyHeader);

    setMinimumWidth(260);
    refresh();
    setFileSectionCollapsed(true);
    setChecksumSectionCollapsed(true);
}

void FilePropertiesPanel::showSection(Section section)
{
    if (section == Section::Properties)
        setFileSectionCollapsed(false);
    else
        setChecksumSectionCollapsed(false);
}

void FilePropertiesPanel::setPanelFullyOpened(bool opened)
{
    m_panelFullyOpened = opened;
    if (opened) {
        if (!m_fileSectionCollapsed)
            emitSectionReadyIfPossible(Section::Properties);
        if (!m_checksumSectionCollapsed)
            emitSectionReadyIfPossible(Section::Checksums);
    }
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
    m_checksumStarted = false;
}

void FilePropertiesPanel::maybeStartChecksumCalculation()
{
    if (!m_panelFullyOpened)
        return;
    if (m_checksumSectionCollapsed)
        return;
    if (m_checksumStarted)
        return;
    m_checksumStarted = true;
    startChecksumCalculation();
}

void FilePropertiesPanel::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateStickyHeader();
}

QString FilePropertiesPanel::formatSize(qulonglong bytes)
{
    return tr("%1 bytes").arg(QLocale().toString(bytes));
}

void FilePropertiesPanel::setChecksumRowsPending()
{
    for (QLabel *label : std::as_const(m_checksumValues))
        label->setText(tr("Calculating..."));
    if (m_checksumProgress)
        m_checksumProgress->show();
}

void FilePropertiesPanel::startChecksumCalculation()
{
    if (!m_hexView)
        return;

    const int generation = ++m_checksumGeneration;
    setChecksumRowsPending();

    const QString path = m_hexView->filePath();
    QPointer<FilePropertiesPanel> guard(this);
    auto *thread = QThread::create([guard, generation, path]() {
        const QHash<QString, QString> results = calculateChecksums(path);
        QMetaObject::invokeMethod(qApp, [guard, generation, results]() {
            if (guard)
                guard->applyChecksumResults(generation, results);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void FilePropertiesPanel::applyChecksumResults(int generation, const QHash<QString, QString> &results)
{
    if (generation != m_checksumGeneration)
        return;

    for (auto it = results.cbegin(); it != results.cend(); ++it) {
        if (QLabel *label = m_checksumValues.value(it.key()))
            label->setText(it.value());
    }
    if (m_checksumProgress)
        m_checksumProgress->hide();
}

void FilePropertiesPanel::setFileSectionCollapsed(bool collapsed)
{
    const bool wasCollapsed = m_fileSectionCollapsed;
    m_fileSectionCollapsed = collapsed;
    if (m_fileSectionBody)
        m_fileSectionBody->setVisible(!collapsed);
    if (m_fileHeaderGap)
        m_fileHeaderGap->changeSize(0, collapsed ? 0 : kHeaderControlGap,
                                    QSizePolicy::Minimum, QSizePolicy::Fixed);
    if (m_betweenSectionsGap)
        m_betweenSectionsGap->changeSize(0, collapsed ? kHeaderControlGap : kGroupTopGap,
                                         QSizePolicy::Minimum, QSizePolicy::Fixed);
    if (m_fileHeader)
        m_fileHeader->setCollapsed(collapsed);
    if (m_content && m_content->layout())
        m_content->layout()->invalidate();
    if (wasCollapsed && !collapsed) {
        emit sectionExpanded(Section::Properties);
        emitSectionReadyIfPossible(Section::Properties);
    }
    updateStickyHeader();
    QTimer::singleShot(0, this, &FilePropertiesPanel::updateStickyHeader);
}

void FilePropertiesPanel::setChecksumSectionCollapsed(bool collapsed)
{
    const bool wasCollapsed = m_checksumSectionCollapsed;
    m_checksumSectionCollapsed = collapsed;
    if (m_checksumSectionBody)
        m_checksumSectionBody->setVisible(!collapsed);
    if (m_checksumHeaderGap)
        m_checksumHeaderGap->changeSize(0, collapsed ? 0 : kHeaderControlGap,
                                        QSizePolicy::Minimum, QSizePolicy::Fixed);
    if (m_checksumHeader)
        m_checksumHeader->setCollapsed(collapsed);
    if (m_content && m_content->layout())
        m_content->layout()->invalidate();
    if (wasCollapsed && !collapsed) {
        emit sectionExpanded(Section::Checksums);
        emitSectionReadyIfPossible(Section::Checksums);
    }
    updateStickyHeader();
    QTimer::singleShot(0, this, &FilePropertiesPanel::updateStickyHeader);
}

void FilePropertiesPanel::emitSectionReadyIfPossible(Section section)
{
    if (!m_panelFullyOpened)
        return;

    const bool sectionExpanded = section == Section::Properties
                                     ? !m_fileSectionCollapsed
                                     : !m_checksumSectionCollapsed;
    if (sectionExpanded)
        emit sectionReady(section);
}

void FilePropertiesPanel::syncStickyHeader()
{
    if (!m_scrollArea || !m_stickyHeader || !m_fileHeader || !m_checksumHeader)
        return;

    const int headerWidth = qMax(1, m_scrollArea->viewport()->width()
                                       - 2 * kSectionHeaderOuterMargin
                                       - kCardScrollbarInset);
    if (m_fileHeader->width() != headerWidth)
        m_fileHeader->setFixedWidth(headerWidth);
    if (m_checksumHeader->width() != headerWidth)
        m_checksumHeader->setFixedWidth(headerWidth);

    const int fileY = m_fileHeader->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
    const int checksumY = m_checksumHeader->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
    const bool checksumsActive = checksumY <= 0;

    if (checksumsActive) {
        m_stickyHeader->setTitle(m_checksumHeader->title());
        m_stickyHeader->setCollapsed(m_checksumSectionCollapsed);
        m_stickyHeader->setClickedCallback([this]() {
            setChecksumSectionCollapsed(!m_checksumSectionCollapsed);
        });
    } else {
        m_stickyHeader->setTitle(m_fileHeader->title());
        m_stickyHeader->setCollapsed(m_fileSectionCollapsed);
        m_stickyHeader->setClickedCallback([this]() {
            setFileSectionCollapsed(!m_fileSectionCollapsed);
        });
    }

    const bool shouldStick = checksumsActive || fileY <= 0;
    if (!shouldStick) {
        m_stickyHeader->hide();
        return;
    }

    const int nextHeaderPush = checksumsActive ? kSectionHeaderHeight
                                               : checksumY;
    const int y = qMin(0, nextHeaderPush - kSectionHeaderHeight);
    m_stickyHeader->setGeometry(kSectionHeaderOuterMargin, y, headerWidth, kSectionHeaderHeight);
    m_stickyHeader->show();
    m_stickyHeader->raise();
}

void FilePropertiesPanel::updateStickyHeader()
{
    syncStickyHeader();
}
