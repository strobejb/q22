#include "filestats/sidepanel.h"

#include "HexView/hexview.h"
#include "filestats/widgets.h"
#include "settings/settingscard.h"

#include <QDate>
#include <QDateTime>
#include <QLabel>
#include <QHBoxLayout>
#include <QTime>
#include <QTimeZone>
#include <QVBoxLayout>

#include <cstring>

using namespace filestats;

namespace
{

class TwoColumnInterpreterRow : public QWidget
{
public:
    TwoColumnInterpreterRow(const QString &leftTitle, const QString &rightTitle,
                            QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 2, 0, 2);
        layout->setSpacing(16);

        auto makeColumn = [this](const QString &title, QLabel **valueOut)
        {
            auto *column = new QWidget(this);
            column->setMinimumWidth(0);
            column->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

            auto *columnLayout = new QVBoxLayout(column);
            columnLayout->setContentsMargins(0, 0, 0, 0);
            columnLayout->setSpacing(3);

            auto *name = new QLabel(title, column);
            QFont labelFont = name->font();
            if (labelFont.pointSizeF() > 0)
                labelFont.setPointSizeF(qMax(1.0, labelFont.pointSizeF() - 1.0));
            else if (labelFont.pixelSize() > 0)
                labelFont.setPixelSize(qMax(1, labelFont.pixelSize() - 1));
            name->setFont(labelFont);
            name->setMinimumWidth(0);
            name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            name->setStyleSheet(QStringLiteral("color: %1;").arg(cssColor(subduedTextColor(palette()))));

            auto *value = new QLabel(column);
            value->setMinimumWidth(0);
            value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            value->setWordWrap(true);

            columnLayout->addWidget(name);
            columnLayout->addWidget(value);
            *valueOut = value;
            return column;
        };

        layout->addWidget(makeColumn(leftTitle, &m_leftValue), 1);
        layout->addWidget(makeColumn(rightTitle, &m_rightValue), 1);
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        return QSize(1, qMax(42, fm.height() * 2 + 16));
    }

    QSize minimumSizeHint() const override { return QSize(1, sizeHint().height()); }

    void setValues(const QString &leftText, const QString &rightText)
    {
        m_leftValue->setText(leftText);
        m_rightValue->setText(rightText);
    }

private:
    QLabel *m_leftValue = nullptr;
    QLabel *m_rightValue = nullptr;
};

uint64_t littleEndianValue(const uint8_t *data, size_t length)
{
    uint64_t value = 0;
    for (size_t i = 0; i < length; ++i)
        value |= uint64_t(data[i]) << (i * 8);
    return value;
}

int64_t signedValue(uint64_t value, size_t length)
{
    if (length == 0 || length >= 8)
        return static_cast<int64_t>(value);

    const uint64_t signBit = uint64_t(1) << (length * 8 - 1);
    const uint64_t mask = (uint64_t(1) << (length * 8)) - 1;
    if ((value & signBit) == 0)
        return static_cast<int64_t>(value);
    return static_cast<int64_t>(value | ~mask);
}

QString hexValue(uint64_t value, size_t length)
{
    return QStringLiteral("0x%1")
        .arg(value, int(length * 2), 16, QLatin1Char('0'))
        .toUpper();
}

QString notEnoughBytes()
{
    return FilePropertiesPanel::tr("Not enough bytes");
}

void setIntegerRow(TwoColumnInterpreterRow *row, const uint8_t *data, size_t length, size_t available)
{
    if (!row)
        return;

    if (available < length)
    {
        row->setValues(notEnoughBytes(), notEnoughBytes());
        return;
    }

    const uint64_t raw = littleEndianValue(data, length);
    row->setValues(QString::number(raw), QString::number(signedValue(raw, length)));
}

QString charText(const uint8_t *data, size_t available)
{
    if (available < 1)
        return notEnoughBytes();

    const uint8_t byte = data[0];
    const QChar ch = QChar::fromLatin1(char(byte));
    const QString display = ch.isPrint()
        ? QStringLiteral("'%1'").arg(ch)
        : QStringLiteral("non-printable");
    return QStringLiteral("%1 (%2)").arg(display, hexValue(byte, 1));
}

QString wcharText(const uint8_t *data, size_t available)
{
    if (available < 2)
        return notEnoughBytes();

    const uint16_t raw = static_cast<uint16_t>(littleEndianValue(data, 2));
    const QChar ch(raw);
    const QString display = ch.isPrint()
        ? QStringLiteral(" '%1'").arg(ch)
        : QString();
    return QStringLiteral("U+%1%2")
        .arg(raw, 4, 16, QLatin1Char('0'))
        .arg(display)
        .toUpper();
}

QString floatText(const uint8_t *data, size_t available)
{
    if (available < 4)
        return notEnoughBytes();

    const uint32_t bits = static_cast<uint32_t>(littleEndianValue(data, 4));
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return QString::number(value, 'g', 8);
}

QString doubleText(const uint8_t *data, size_t available)
{
    if (available < 8)
        return notEnoughBytes();

    const uint64_t bits = littleEndianValue(data, 8);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return QString::number(value, 'g', 12);
}

QString formatUtcDateTime(const QDateTime &dateTime)
{
    if (!dateTime.isValid())
        return FilePropertiesPanel::tr("Invalid");

    return dateTime.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC'"));
}

QString unixTimeText(const uint8_t *data, size_t length, size_t available)
{
    if (available < length)
        return notEnoughBytes();

    const qint64 seconds = signedValue(littleEndianValue(data, length), length);
    return formatUtcDateTime(QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC));
}

QString fileTimeText(const uint8_t *data, size_t available)
{
    if (available < 8)
        return notEnoughBytes();

    const uint64_t ticks = littleEndianValue(data, 8);
    const QDateTime epoch(QDate(1601, 1, 1), QTime(0, 0), QTimeZone::UTC);
    return formatUtcDateTime(epoch.addMSecs(qint64(ticks / 10000)));
}

QString dateTimeText(const uint8_t *data, size_t available)
{
    if (available < 8)
        return notEnoughBytes();

    const uint64_t ticks = littleEndianValue(data, 8);
    const QDateTime epoch(QDate(1, 1, 1), QTime(0, 0), QTimeZone::UTC);
    return formatUtcDateTime(epoch.addMSecs(qint64(ticks / 10000)));
}

QString dosDateText(const uint8_t *data, size_t available)
{
    if (available < 2)
        return notEnoughBytes();

    const uint16_t raw = static_cast<uint16_t>(littleEndianValue(data, 2));
    const int year = 1980 + ((raw >> 9) & 0x7f);
    const int month = (raw >> 5) & 0x0f;
    const int day = raw & 0x1f;
    const QDate date(year, month, day);
    return date.isValid() ? date.toString(QStringLiteral("yyyy-MM-dd"))
                          : FilePropertiesPanel::tr("Invalid");
}

QString dosTimeText(const uint8_t *data, size_t available)
{
    if (available < 2)
        return notEnoughBytes();

    const uint16_t raw = static_cast<uint16_t>(littleEndianValue(data, 2));
    const int hour = (raw >> 11) & 0x1f;
    const int minute = (raw >> 5) & 0x3f;
    const int second = (raw & 0x1f) * 2;
    const QTime time(hour, minute, second);
    return time.isValid() ? time.toString(QStringLiteral("HH:mm:ss"))
                          : FilePropertiesPanel::tr("Invalid");
}

} // namespace

void FilePropertiesPanel::buildDataInterpreterSection(QWidget *parent, QVBoxLayout *contentLayout)
{
    m_dataInterpreterHeader = new SectionHeader(tr("Data Inspector"), parent);
    m_dataInterpreterHeader->setClickedCallback(
        [this]() {
            setSectionCollapsed(SectionId::DataInterpreter,
                                !isSectionCollapsed(SectionId::DataInterpreter));
        });
    contentLayout->addWidget(m_dataInterpreterHeader);
    m_dataInterpreterHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_dataInterpreterHeaderGap = new QSpacerItem(0, kHeaderControlGap,
                                                 QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_dataInterpreterHeaderGap);

    m_dataInterpreterSectionBody = new QWidget(parent);
    m_dataInterpreterSectionBody->setMinimumWidth(0);
    auto *bodyLayout = new QVBoxLayout(m_dataInterpreterSectionBody);
    bodyLayout->setContentsMargins(kSectionHeaderOuterMargin + kCardLeftInset, 0,
                                   kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
    bodyLayout->setSpacing(0);

    QList<QWidget *> rows;
    rows.reserve(9);

    auto addTwoColumnRow = [this, &rows](const char *key, const QString &leftTitle,
                                         const QString &rightTitle) {
        auto *integerRow = new TwoColumnInterpreterRow(leftTitle, rightTitle,
                                                       m_dataInterpreterSectionBody);
        rows.append(integerRow);
        m_dataInterpreterIntegerRows.insert(QString::fromLatin1(key), integerRow);
    };

    addTwoColumnRow("chars", tr("char"), tr("wchar_t"));
    addTwoColumnRow("byte", tr("Byte"), tr("Byte (Signed)"));
    addTwoColumnRow("word", tr("Word"), tr("Word (Signed)"));
    addTwoColumnRow("dword", tr("Dword"), tr("Dword (Signed)"));
    addTwoColumnRow("qword", tr("Qword"), tr("Qword (Signed)"));
    addTwoColumnRow("floats", tr("Float"), tr("Double"));
    addTwoColumnRow("unixTimes", tr("time_t (32-bit)"), tr("time_t (64-bit)"));
    addTwoColumnRow("windowsTimes", tr("FILETIME"), tr("DATETIME"));
    addTwoColumnRow("dosTimes", tr("DOS Date"), tr("DOS Time"));

    auto *card = new SettingsCard(rows, SettingsCard::Style::Spaced, m_dataInterpreterSectionBody);
    card->setMinimumWidth(0);
    bodyLayout->addWidget(card);
    contentLayout->addWidget(m_dataInterpreterSectionBody);

    registerPanelSection({
        SectionId::DataInterpreter,
        tr("Data Inspector"),
        m_dataInterpreterHeader,
        m_dataInterpreterSectionBody,
        m_dataInterpreterHeaderGap,
        nullptr,
        nullptr,
        0,
        [this]() { updateDataInterpreter(); },
        {},
        [this](bool) { updateDataInterpreter(); },
        [this]() { updateDataInterpreter(); },
    });

    updateDataInterpreter();
}

void FilePropertiesPanel::updateDataInterpreter()
{
    if (m_dataInterpreterIntegerRows.isEmpty())
        return;

    uint8_t data[8] = {};
    size_t available = 0;
    if (m_hexView)
    {
        const size_w offset = m_hexView->cursorOffset();
        if (offset < m_hexView->size())
            available = m_hexView->getData(offset, data, sizeof(data));
    }

    if (auto *row = dynamic_cast<TwoColumnInterpreterRow *>(
            m_dataInterpreterIntegerRows.value(QStringLiteral("chars"), nullptr)))
        row->setValues(charText(data, available), wcharText(data, available));

    setIntegerRow(dynamic_cast<TwoColumnInterpreterRow *>(
                      m_dataInterpreterIntegerRows.value(QStringLiteral("byte"), nullptr)),
                  data, 1, available);
    setIntegerRow(dynamic_cast<TwoColumnInterpreterRow *>(
                      m_dataInterpreterIntegerRows.value(QStringLiteral("word"), nullptr)),
                  data, 2, available);
    setIntegerRow(dynamic_cast<TwoColumnInterpreterRow *>(
                      m_dataInterpreterIntegerRows.value(QStringLiteral("dword"), nullptr)),
                  data, 4, available);
    setIntegerRow(dynamic_cast<TwoColumnInterpreterRow *>(
                      m_dataInterpreterIntegerRows.value(QStringLiteral("qword"), nullptr)),
                  data, 8, available);

    if (auto *row = dynamic_cast<TwoColumnInterpreterRow *>(
            m_dataInterpreterIntegerRows.value(QStringLiteral("floats"), nullptr)))
        row->setValues(floatText(data, available), doubleText(data, available));

    if (auto *row = dynamic_cast<TwoColumnInterpreterRow *>(
            m_dataInterpreterIntegerRows.value(QStringLiteral("unixTimes"), nullptr)))
        row->setValues(unixTimeText(data, 4, available), unixTimeText(data, 8, available));

    if (auto *row = dynamic_cast<TwoColumnInterpreterRow *>(
            m_dataInterpreterIntegerRows.value(QStringLiteral("windowsTimes"), nullptr)))
        row->setValues(fileTimeText(data, available), dateTimeText(data, available));

    if (auto *row = dynamic_cast<TwoColumnInterpreterRow *>(
            m_dataInterpreterIntegerRows.value(QStringLiteral("dosTimes"), nullptr)))
        row->setValues(dosDateText(data, available), dosTimeText(data, available));

    requestSectionLayoutRefresh(SectionId::DataInterpreter);
}
