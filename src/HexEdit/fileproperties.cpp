#include "fileproperties.h"

#include "HexView/hexview.h"
#include "combos/menucombobox.h"
#include "settings/scrollhintoverlay.h"
#include "settings/settingscard.h"
#include "theme.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QBrush>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QCursor>
#include <QDesktopServices>
#include <QEnterEvent>
#include <QEvent>
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
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QHeaderView>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantMap>

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
static constexpr int kStringsListMinHeight = 160;

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

static QHash<QString, QString> calculateChecksums(
    const QString &path,
    const std::shared_ptr<std::atomic_bool> &cancelFlag,
    const std::function<void(int)> &progressCallback)
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
    const qint64 total = file.size();
    qint64 scanned = 0;
    int lastProgress = -1;

    while (!cancelFlag->load() && !file.atEnd()) {
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

        scanned += chunk.size();
        const int progress = total > 0
                                 ? static_cast<int>((scanned * 1000) / total)
                                 : 1000;
        if (progress != lastProgress) {
            lastProgress = progress;
            progressCallback(progress);
        }
    }
    if (cancelFlag->load())
        return {};

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

static bool isAsciiStringByte(unsigned char ch)
{
    return ch >= 0x20 && ch <= 0x7E;
}

static QToolButton *createProgressStopButton(QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(22, 22);
    button->setIconSize(QSize(16, 16));
    button->setToolTip(QObject::tr("Stop"));
    button->setProperty("iconThemeName", QStringLiteral("actions/stopcircle"));
    button->setProperty("iconSize", 16);
    button->setIcon(recoloredIcon(QStringLiteral("actions/stopcircle"),
                                  parent->palette().buttonText().color(), 16));
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

static QWidget *createRecalculateStrip(QWidget *parent,
                                       Qt::Alignment buttonAlignment,
                                       const std::function<void()> &onClicked)
{
    auto *strip = new QFrame(parent);
    strip->setObjectName(QStringLiteral("recalculateStrip"));
    strip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    const QColor accent(QStringLiteral("#d18a4a"));
    const bool dark = parent->palette().window().color().lightness() < 128;
    const QColor bg = dark ? QColor(209, 138, 74, 45) : QColor(209, 138, 74, 30);
    const QColor hover = dark ? accent.lighter(118) : accent.lighter(108);
    const QColor pressed = dark ? accent.lighter(130) : accent.darker(108);
    const QColor text = accent.lightness() < 150 ? QColor(Qt::white) : QColor(Qt::black);

    strip->setStyleSheet(QStringLiteral(R"(
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

    auto *layout = new QHBoxLayout(strip);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    auto *label = new QLabel(strip);
    label->setObjectName(QStringLiteral("recalculateMessage"));
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(label, 1, Qt::AlignVCenter);

    auto *button = new QPushButton(QObject::tr("Recalculate"), strip);
    button->setCursor(Qt::PointingHandCursor);
    QObject::connect(button, &QPushButton::clicked, strip, onClicked);

    if (buttonAlignment & Qt::AlignHCenter)
        layout->addStretch();
    layout->addWidget(button, 0, Qt::AlignVCenter);
    if (buttonAlignment & Qt::AlignHCenter)
        layout->addStretch();

    strip->hide();
    return strip;
}

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

struct StringScanState {
    QVector<QVariantMap> results;
    QByteArray run;
    qulonglong runStart = 0;
    qulonglong runLength = 0;
    qulonglong offset = 0;
    int resultCount = 0;
};

static void flushAsciiRun(StringScanState &state, int minLength)
{
    static constexpr int kMaxStringResults = 10000;
    if (state.runLength >= static_cast<qulonglong>(minLength) && state.resultCount < kMaxStringResults) {
        QVariantMap row;
        row.insert(QStringLiteral("offset"), state.runStart);
        row.insert(QStringLiteral("length"), state.runLength);
        row.insert(QStringLiteral("text"), QString::fromLatin1(state.run.constData(), state.run.size()));
        state.results.append(row);
        ++state.resultCount;
    }
    state.run.clear();
    state.runLength = 0;
}

static void scanAsciiChunk(StringScanState &state, const QByteArray &chunk, int minLength)
{
    static constexpr int kMaxBufferedStringLength = 4096;
    for (char byte : chunk) {
        if (isAsciiStringByte(static_cast<unsigned char>(byte))) {
            if (state.runLength == 0)
                state.runStart = state.offset;
            if (state.run.size() < kMaxBufferedStringLength)
                state.run.append(byte);
            ++state.runLength;
        } else {
            flushAsciiRun(state, minLength);
        }
        ++state.offset;
    }
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
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
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
    m_checksumStopButton = createProgressStopButton(m_checksumSectionBody);
    m_checksumProgressRow = new QWidget(m_checksumSectionBody);
    auto *checksumProgressLayout = new QHBoxLayout(m_checksumProgressRow);
    checksumProgressLayout->setContentsMargins(kContentMargin, 0, kContentMargin, 0);
    checksumProgressLayout->setSpacing(8);
    checksumProgressLayout->addWidget(m_checksumProgress, 1, Qt::AlignVCenter);
    checksumProgressLayout->addWidget(m_checksumStopButton, 0, Qt::AlignVCenter);
    checksumBodyLayout->addWidget(m_checksumProgressRow);
    m_checksumProgress->hide();
    m_checksumProgressRow->hide();
    m_checksumRecalculateStrip = createRecalculateStrip(m_checksumSectionBody, Qt::AlignRight, [this]() {
        m_checksumStarted = false;
        maybeStartChecksumCalculation();
    });
    auto *checksumRecalcWrap = new QWidget(m_checksumSectionBody);
    auto *checksumRecalcLayout = new QVBoxLayout(checksumRecalcWrap);
    checksumRecalcLayout->setContentsMargins(kContentMargin, 0, kContentMargin, 0);
    checksumRecalcLayout->setSpacing(0);
    checksumRecalcLayout->addWidget(m_checksumRecalculateStrip);
    checksumBodyLayout->addWidget(checksumRecalcWrap);
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

    m_betweenChecksumStringsGap = new QSpacerItem(0, kGroupTopGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_betweenChecksumStringsGap);
    m_stringsHeader = new SectionHeader(tr("Strings"), m_content);
    m_stringsHeader->setClickedCallback([this]() {
        setStringsSectionCollapsed(!m_stringsSectionCollapsed);
    });
    contentLayout->addWidget(m_stringsHeader);
    contentLayout->setAlignment(m_stringsHeader, Qt::AlignLeft);
    m_stringsHeader->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_stringsHeaderGap = new QSpacerItem(0, kHeaderControlGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_stringsHeaderGap);

    m_stringsSectionBody = new QWidget(m_content);
    m_stringsSectionBody->setMinimumWidth(0);
    auto *stringsBodyLayout = new QVBoxLayout(m_stringsSectionBody);
    stringsBodyLayout->setContentsMargins(kSectionHeaderOuterMargin, 0,
                                          kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
    stringsBodyLayout->setSpacing(0);

    m_stringsProgress = new QProgressBar(m_stringsSectionBody);
    m_stringsProgress->setRange(0, 0);
    m_stringsProgress->setTextVisible(false);
    m_stringsProgress->setFixedHeight(6);
    m_stringsProgress->hide();
    m_stringsStopButton = createProgressStopButton(m_stringsSectionBody);
    stringsBodyLayout->addSpacing(kHeaderControlGap + 4);
    m_stringsProgressRow = new QWidget(m_stringsSectionBody);
    auto *stringsProgressLayout = new QHBoxLayout(m_stringsProgressRow);
    stringsProgressLayout->setContentsMargins(kContentMargin, 0, kContentMargin, 0);
    stringsProgressLayout->setSpacing(8);
    stringsProgressLayout->addWidget(m_stringsProgress, 1, Qt::AlignVCenter);
    stringsProgressLayout->addWidget(m_stringsStopButton, 0, Qt::AlignVCenter);
    stringsBodyLayout->addWidget(m_stringsProgressRow);
    m_stringsProgressRow->hide();
    m_stringsRecalculateStrip = createRecalculateStrip(m_stringsSectionBody, Qt::AlignRight, [this]() {
        m_stringsStarted = false;
        if (m_stringsList)
            m_stringsList->clear();
        maybeStartStringScan();
    });
    auto *stringsRecalcWrap = new QWidget(m_stringsSectionBody);
    auto *stringsRecalcLayout = new QVBoxLayout(stringsRecalcWrap);
    stringsRecalcLayout->setContentsMargins(kContentMargin, 0, kContentMargin, 0);
    stringsRecalcLayout->setSpacing(0);
    stringsRecalcLayout->addWidget(m_stringsRecalculateStrip);
    stringsBodyLayout->addWidget(stringsRecalcWrap);
    stringsBodyLayout->addSpacing(kContentMargin + 4);

    m_stringEncoding = new MenuComboBox(m_stringsSectionBody);
    m_stringEncoding->addItems({tr("Ascii"), tr("Unicode"), tr("Ascii and Unicode")});
    m_stringEncoding->setFocusPolicy(Qt::StrongFocus);
    m_stringEncoding->setFixedHeight(qMax(24, m_stringEncoding->sizeHint().height() - 4));

    m_minStringLength = new StepSpinBox(tr("Characters"), 3, 128, 1, m_stringsSectionBody);
    m_minStringLength->setValue(3);
    m_minStringLength->setLabelAlignment(Qt::AlignRight);

    auto *stringsControls = new QWidget(m_stringsSectionBody);
    auto *stringsControlsLayout = new QHBoxLayout(stringsControls);
    stringsControlsLayout->setContentsMargins(kContentMargin, 0, kContentMargin, 0);
    stringsControlsLayout->setSpacing(kContentMargin + 6);
    stringsControlsLayout->addWidget(m_stringEncoding, 1, Qt::AlignVCenter);
    stringsControlsLayout->addWidget(m_minStringLength, 0);
    stringsBodyLayout->addWidget(stringsControls);
    stringsBodyLayout->addSpacing(kHeaderControlGap + 4);

    auto *stringsListFrame = new StringListFrame(m_stringsSectionBody);
    stringsListFrame->setObjectName(QStringLiteral("stringsListFrame"));
    stringsListFrame->setFixedHeight(kStringsListMinHeight);
    stringsListFrame->setStyleSheet(QStringLiteral(R"(
        QFrame#stringsListFrame {
            background: palette(base);
            border: 1px solid palette(mid);
            border-radius: 6px;
        }
    )"));

    m_stringsList = new QTreeWidget(stringsListFrame);
    m_stringsList->setMinimumSize(0, 0);
    m_stringsList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_stringsList->setColumnCount(2);
    m_stringsList->setHeaderHidden(true);
    m_stringsList->setRootIsDecorated(false);
    m_stringsList->setAlternatingRowColors(false);
    m_stringsList->setUniformRowHeights(true);
    m_stringsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_stringsList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stringsList->header()->setStretchLastSection(false);
    m_stringsList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_stringsList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_stringsList->setStyleSheet(QStringLiteral(R"(
        QTreeWidget {
            background: palette(base);
            border: none;
            padding: 0px;
        }
        QTreeWidget::item {
            padding: 3px 6px;
        }
        QTreeWidget::item:hover {
            background: palette(button);
        }
        QTreeWidget::item:selected {
            background: palette(highlight);
            color: palette(highlighted-text);
        }
    )"));
    stringsListFrame->setListWidget(m_stringsList);
    auto *stringsListWrap = new QWidget(m_stringsSectionBody);
    auto *stringsListLayout = new QVBoxLayout(stringsListWrap);
    stringsListLayout->setContentsMargins(kContentMargin, 0, kContentMargin, 0);
    stringsListLayout->setSpacing(0);
    stringsListLayout->addWidget(stringsListFrame);
    stringsBodyLayout->addWidget(stringsListWrap);

    auto *stringsResizeWrap = new QWidget(m_stringsSectionBody);
    auto *stringsResizeLayout = new QVBoxLayout(stringsResizeWrap);
    stringsResizeLayout->setContentsMargins(kContentMargin, 0, kContentMargin, 0);
    stringsResizeLayout->setSpacing(0);
    m_stringsResizeHandle = new VerticalResizeHandle([this](int dy) {
        resizeStringsList(dy);
    }, stringsResizeWrap);
    stringsResizeLayout->addWidget(m_stringsResizeHandle);
    stringsResizeLayout->setAlignment(m_stringsResizeHandle, Qt::AlignTop);
    stringsBodyLayout->addWidget(stringsResizeWrap);
    contentLayout->addWidget(m_stringsSectionBody);
    m_stringsResizeSlack = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_stringsResizeSlack);
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
        const int stringsY = m_stringsHeader->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
        if (stringsY <= 0 || (checksumY > 0 && stringsY < checksumY && fileY > 0))
            setStringsSectionCollapsed(!m_stringsSectionCollapsed);
        else if (checksumY <= 0 || (fileY > 0 && checksumY < fileY))
            setChecksumSectionCollapsed(!m_checksumSectionCollapsed);
        else
            setFileSectionCollapsed(!m_fileSectionCollapsed);
    });
    connect(this, &FilePropertiesPanel::sectionReady,
            this, [this](Section section) {
                if (section == Section::Checksums)
                    maybeStartChecksumCalculation();
                else if (section == Section::Strings)
                    maybeStartStringScan();
            });
    connect(m_checksumStopButton, &QToolButton::clicked,
            this, &FilePropertiesPanel::cancelChecksumCalculation);
    connect(m_stringsStopButton, &QToolButton::clicked,
            this, &FilePropertiesPanel::cancelStringScan);
    connect(m_minStringLength, &StepSpinBox::valueChanged, this, [this](int) {
        m_stringsStarted = false;
        if (m_stringsList)
            m_stringsList->clear();
        maybeStartStringScan();
    });
    connect(m_stringEncoding, &QComboBox::currentIndexChanged, this, [this](int) {
        m_stringsStarted = false;
        if (m_stringsList)
            m_stringsList->clear();
        maybeStartStringScan();
    });
    auto navigateToStringItem = [this](QTreeWidgetItem *item, int) {
        if (!item || !m_hexView)
            return;
        const size_w offset = static_cast<size_w>(item->data(0, Qt::UserRole).toULongLong());
        const size_w length = static_cast<size_w>(item->data(0, Qt::UserRole + 1).toULongLong());
        m_hexView->setCurSel(offset, qMin(offset + length, m_hexView->size()));
        m_hexView->scrollCenterIfOffScreen(offset);
        m_hexView->setFocus();
    };
    connect(m_stringsList, &QTreeWidget::itemClicked, this, navigateToStringItem);
    connect(m_stringsList, &QTreeWidget::itemActivated, this, navigateToStringItem);
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &FilePropertiesPanel::updateStickyHeader);
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::rangeChanged,
            this, [this]() { updateStickyHeader(); });
    QTimer::singleShot(0, this, &FilePropertiesPanel::updateStickyHeader);

    setMinimumWidth(260);
    refresh();
    setFileSectionCollapsed(true);
    setChecksumSectionCollapsed(true);
    setStringsSectionCollapsed(true);
}

FilePropertiesPanel::~FilePropertiesPanel()
{
    ++m_checksumGeneration;
    ++m_stringGeneration;
    if (m_checksumCancel)
        m_checksumCancel->store(true);
    if (m_stringCancel)
        m_stringCancel->store(true);
}

void FilePropertiesPanel::showSection(Section section)
{
    if (section == Section::Properties)
        setFileSectionCollapsed(false);
    else if (section == Section::Checksums)
        setChecksumSectionCollapsed(false);
    else
        setStringsSectionCollapsed(false);
}

void FilePropertiesPanel::setPanelFullyOpened(bool opened)
{
    m_panelFullyOpened = opened;
    if (opened) {
        if (!m_fileSectionCollapsed)
            emitSectionReadyIfPossible(Section::Properties);
        if (!m_checksumSectionCollapsed)
            emitSectionReadyIfPossible(Section::Checksums);
        if (!m_stringsSectionCollapsed)
            emitSectionReadyIfPossible(Section::Strings);
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
    ++m_checksumGeneration;
    if (m_checksumCancel)
        m_checksumCancel->store(true);
    m_stringsStarted = false;
    if (m_checksumProgress)
        m_checksumProgress->hide();
    if (m_checksumStopButton)
        m_checksumStopButton->hide();
    if (m_checksumProgressRow)
        m_checksumProgressRow->hide();
    if (m_checksumRecalculateStrip)
        m_checksumRecalculateStrip->hide();
    cancelStringScan();
    if (m_stringsRecalculateStrip)
        m_stringsRecalculateStrip->hide();
    if (m_stringsList)
        m_stringsList->clear();
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

void FilePropertiesPanel::maybeStartStringScan()
{
    if (!m_panelFullyOpened)
        return;
    if (m_stringsSectionCollapsed)
        return;
    if (m_stringsStarted)
        return;
    m_stringsStarted = true;
    startStringScan();
}

void FilePropertiesPanel::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        recolorToolButtons(this);
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
    if (m_checksumProgress) {
        m_checksumProgress->setRange(0, 1000);
        m_checksumProgress->setValue(0);
        m_checksumProgress->show();
    }
    if (m_checksumStopButton)
        m_checksumStopButton->show();
    if (m_checksumProgressRow)
        m_checksumProgressRow->show();
    if (m_checksumRecalculateStrip)
        m_checksumRecalculateStrip->hide();
}

void FilePropertiesPanel::startChecksumCalculation()
{
    if (!m_hexView)
        return;

    const int generation = ++m_checksumGeneration;
    if (m_checksumCancel)
        m_checksumCancel->store(true);
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_checksumCancel = cancelFlag;
    setChecksumRowsPending();

    const QString path = m_hexView->filePath();
    QPointer<FilePropertiesPanel> guard(this);
    auto *thread = QThread::create([guard, generation, path, cancelFlag]() {
        const QHash<QString, QString> results = calculateChecksums(path, cancelFlag, [guard, generation](int progress) {
            QMetaObject::invokeMethod(qApp, [guard, generation, progress]() {
                if (guard)
                    guard->updateChecksumProgress(generation, progress);
            }, Qt::QueuedConnection);
        });
        if (cancelFlag->load())
            return;
        QMetaObject::invokeMethod(qApp, [guard, generation, results]() {
            if (guard)
                guard->applyChecksumResults(generation, results);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void FilePropertiesPanel::updateChecksumProgress(int generation, int value)
{
    if (generation != m_checksumGeneration || !m_checksumProgress)
        return;
    m_checksumProgress->setValue(qBound(0, value, 1000));
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
    if (m_checksumStopButton)
        m_checksumStopButton->hide();
    if (m_checksumProgressRow)
        m_checksumProgressRow->hide();
    if (m_checksumRecalculateStrip)
        showRecalculateStrip(m_checksumRecalculateStrip, tr("Operation complete"));
}

void FilePropertiesPanel::startStringScan()
{
    if (!m_hexView || !m_minStringLength)
        return;

    const int generation = ++m_stringGeneration;
    const int minLength = m_minStringLength->value();
    const QString path = m_hexView->filePath();
    if (m_stringCancel)
        m_stringCancel->store(true);
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_stringCancel = cancelFlag;
    if (m_stringsProgress) {
        m_stringsProgress->setRange(0, 1000);
        m_stringsProgress->setValue(0);
        m_stringsProgress->show();
    }
    if (m_stringsStopButton)
        m_stringsStopButton->show();
    if (m_stringsProgressRow)
        m_stringsProgressRow->show();
    if (m_stringsRecalculateStrip)
        m_stringsRecalculateStrip->hide();
    if (m_stringsList)
        m_stringsList->clear();

    QPointer<FilePropertiesPanel> guard(this);
    auto *thread = QThread::create([guard, generation, minLength, path, cancelFlag]() {
        QVector<QVariantMap> results;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            StringScanState state;
            const qint64 total = file.size();
            qint64 scanned = 0;
            int lastProgress = -1;
            static constexpr qint64 kChunkSize = 1024 * 1024;

            while (!cancelFlag->load() && !file.atEnd()) {
                const QByteArray chunk = file.read(kChunkSize);
                if (chunk.isEmpty())
                    break;
                scanAsciiChunk(state, chunk, minLength);
                if (!state.results.isEmpty()) {
                    const QVector<QVariantMap> batch = std::move(state.results);
                    state.results.clear();
                    QMetaObject::invokeMethod(qApp, [guard, generation, batch]() {
                        if (guard)
                            guard->appendStringResults(generation, batch);
                    }, Qt::QueuedConnection);
                }
                scanned += chunk.size();
                const int progress = total > 0
                                         ? static_cast<int>((scanned * 1000) / total)
                                         : 1000;
                if (progress != lastProgress) {
                    lastProgress = progress;
                    QMetaObject::invokeMethod(qApp, [guard, generation, progress]() {
                        if (guard)
                            guard->updateStringProgress(generation, progress);
                    }, Qt::QueuedConnection);
                }
            }
            if (!cancelFlag->load()) {
                flushAsciiRun(state, minLength);
                if (!state.results.isEmpty())
                    results = std::move(state.results);
            }
        }
        if (cancelFlag->load())
            return;
        QMetaObject::invokeMethod(qApp, [guard, generation, results]() {
            if (guard) {
                if (!results.isEmpty())
                    guard->appendStringResults(generation, results);
                guard->applyStringResults(generation, {});
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void FilePropertiesPanel::updateStringProgress(int generation, int value)
{
    if (generation != m_stringGeneration || !m_stringsProgress)
        return;
    m_stringsProgress->setValue(qBound(0, value, 1000));
}

void FilePropertiesPanel::cancelChecksumCalculation()
{
    ++m_checksumGeneration;
    m_checksumStarted = false;
    if (m_checksumCancel)
        m_checksumCancel->store(true);
    for (QLabel *label : std::as_const(m_checksumValues))
        label->setText(tr("Cancelled"));
    if (m_checksumProgress)
        m_checksumProgress->hide();
    if (m_checksumStopButton)
        m_checksumStopButton->hide();
    if (m_checksumProgressRow)
        m_checksumProgressRow->hide();
    if (m_checksumRecalculateStrip)
        showRecalculateStrip(m_checksumRecalculateStrip, tr("Operation cancelled"));
}

void FilePropertiesPanel::cancelStringScan()
{
    ++m_stringGeneration;
    m_stringsStarted = false;
    if (m_stringCancel)
        m_stringCancel->store(true);
    if (m_stringsProgress)
        m_stringsProgress->hide();
    if (m_stringsStopButton)
        m_stringsStopButton->hide();
    if (m_stringsProgressRow)
        m_stringsProgressRow->hide();
    if (m_stringsRecalculateStrip)
        showRecalculateStrip(m_stringsRecalculateStrip, tr("Operation cancelled"));
}

void FilePropertiesPanel::showRecalculateStrip(QWidget *strip, const QString &message)
{
    if (!strip)
        return;
    if (auto *label = strip->findChild<QLabel *>(QStringLiteral("recalculateMessage")))
        label->setText(message);
    strip->show();
}

void FilePropertiesPanel::resizeStringsList(int dy)
{
    if (!m_stringsList)
        return;
    QWidget *frame = m_stringsList->parentWidget();
    if (!frame)
        return;
    const int maxHeight = qMax(kStringsListMinHeight, height() - kSectionHeaderHeight * 2);
    const int newHeight = qBound(kStringsListMinHeight, frame->height() + dy, maxHeight);
    if (newHeight == frame->height())
        return;
    const int delta = newHeight - frame->height();
    frame->setFixedHeight(newHeight);
    if (m_stringsResizeSlack) {
        if (delta < 0)
            m_stringsResizeSlackHeight += -delta;
        else
            m_stringsResizeSlackHeight = qMax(0, m_stringsResizeSlackHeight - delta);
        m_stringsResizeSlack->changeSize(0, m_stringsResizeSlackHeight,
                                         QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
    if (m_content && m_content->layout())
        m_content->layout()->invalidate();
    updateStickyHeader();
}

void FilePropertiesPanel::appendStringResults(int generation, const QVector<QVariantMap> &results)
{
    if (generation != m_stringGeneration)
        return;

    if (m_stringsList && !results.isEmpty()) {
        m_stringsList->setUpdatesEnabled(false);
        const QColor offsetColor = subduedTextColor(palette());
        for (const QVariantMap &row : results) {
            const qulonglong offset = row.value(QStringLiteral("offset")).toULongLong();
            const qulonglong length = row.value(QStringLiteral("length")).toULongLong();
            auto *item = new QTreeWidgetItem(m_stringsList);
            item->setText(0, row.value(QStringLiteral("text")).toString());
            item->setText(1, QStringLiteral("%1").arg(offset, 8, 16, QLatin1Char('0')).toUpper());
            item->setForeground(1, QBrush(offsetColor));
            item->setData(0, Qt::UserRole, offset);
            item->setData(0, Qt::UserRole + 1, length);
        }
        m_stringsList->setUpdatesEnabled(true);
    }
}

void FilePropertiesPanel::applyStringResults(int generation, const QVector<QVariantMap> &results)
{
    if (generation != m_stringGeneration)
        return;

    appendStringResults(generation, results);
    if (m_stringsProgress)
        m_stringsProgress->hide();
    if (m_stringsStopButton)
        m_stringsStopButton->hide();
    if (m_stringsProgressRow)
        m_stringsProgressRow->hide();
    if (m_stringsRecalculateStrip)
        showRecalculateStrip(m_stringsRecalculateStrip, tr("Operation complete"));
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
    if (m_betweenChecksumStringsGap)
        m_betweenChecksumStringsGap->changeSize(0, collapsed ? kHeaderControlGap : kGroupTopGap,
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

void FilePropertiesPanel::setStringsSectionCollapsed(bool collapsed)
{
    const bool wasCollapsed = m_stringsSectionCollapsed;
    m_stringsSectionCollapsed = collapsed;
    if (m_stringsSectionBody)
        m_stringsSectionBody->setVisible(!collapsed);
    if (m_stringsHeaderGap)
        m_stringsHeaderGap->changeSize(0, collapsed ? 0 : kHeaderControlGap,
                                       QSizePolicy::Minimum, QSizePolicy::Fixed);
    if (m_stringsHeader)
        m_stringsHeader->setCollapsed(collapsed);
    if (m_content && m_content->layout())
        m_content->layout()->invalidate();
    if (wasCollapsed && !collapsed) {
        emit sectionExpanded(Section::Strings);
        emitSectionReadyIfPossible(Section::Strings);
    }
    updateStickyHeader();
    QTimer::singleShot(0, this, &FilePropertiesPanel::updateStickyHeader);
}

void FilePropertiesPanel::emitSectionReadyIfPossible(Section section)
{
    if (!m_panelFullyOpened)
        return;

    bool sectionExpanded = false;
    if (section == Section::Properties)
        sectionExpanded = !m_fileSectionCollapsed;
    else if (section == Section::Checksums)
        sectionExpanded = !m_checksumSectionCollapsed;
    else
        sectionExpanded = !m_stringsSectionCollapsed;
    if (sectionExpanded)
        emit sectionReady(section);
}

void FilePropertiesPanel::syncStickyHeader()
{
    if (!m_scrollArea || !m_stickyHeader || !m_fileHeader || !m_checksumHeader || !m_stringsHeader)
        return;

    const int headerWidth = qMax(1, m_scrollArea->viewport()->width()
                                       - 2 * kSectionHeaderOuterMargin
                                       - kCardScrollbarInset);
    if (m_fileHeader->width() != headerWidth)
        m_fileHeader->setFixedWidth(headerWidth);
    if (m_checksumHeader->width() != headerWidth)
        m_checksumHeader->setFixedWidth(headerWidth);
    if (m_stringsHeader->width() != headerWidth)
        m_stringsHeader->setFixedWidth(headerWidth);

    const int fileY = m_fileHeader->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
    const int checksumY = m_checksumHeader->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
    const int stringsY = m_stringsHeader->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
    Section activeSection = Section::Properties;
    int nextHeaderY = checksumY;
    if (checksumY <= 0) {
        activeSection = Section::Checksums;
        nextHeaderY = stringsY;
    }
    if (stringsY <= 0) {
        activeSection = Section::Strings;
        nextHeaderY = kSectionHeaderHeight;
    }

    if (activeSection == Section::Strings) {
        m_stickyHeader->setTitle(m_stringsHeader->title());
        m_stickyHeader->setCollapsed(m_stringsSectionCollapsed);
        m_stickyHeader->setClickedCallback([this]() {
            setStringsSectionCollapsed(!m_stringsSectionCollapsed);
        });
    } else if (activeSection == Section::Checksums) {
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

    const bool shouldStick = activeSection != Section::Properties || fileY <= 0;
    if (!shouldStick) {
        m_stickyHeader->hide();
        return;
    }

    const int y = qMin(0, nextHeaderY - kSectionHeaderHeight);
    m_stickyHeader->setGeometry(kSectionHeaderOuterMargin, y, headerWidth, kSectionHeaderHeight);
    m_stickyHeader->show();
    m_stickyHeader->raise();
}

void FilePropertiesPanel::updateStickyHeader()
{
    syncStickyHeader();
}
