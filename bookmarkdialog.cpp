#include "bookmarkdialog.h"
#include "ui_bookmarks.h"
#include "colourpickerwidget.h"

#include <QApplication>
#include <QAbstractButton>
#include <QPainter>
#include <QShowEvent>
#include <QVector>
#include <QDialog>
#include <QPushButton>
#include <QIcon>
#include <QLayout>

// ── PickerCard ────────────────────────────────────────────────────────────────
// Rounded card with drop shadow — same visual style as SettingsGroup.

namespace {

static constexpr int PC_SHADOW = 4;
static constexpr int PC_RADIUS = 10;
static constexpr int PC_PAD    = 0;   // ColourPickerWidget's own SWATCH_PAD supplies the margin
static constexpr int DIALOG_MARGIN = 20;

class PickerCard : public QWidget
{
public:
    explicit PickerCard(QWidget *parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QPalette &pal  = palette();
        const bool      dark = pal.color(QPalette::Window).lightness() < 128;
        const QRectF    card = QRectF(rect()).adjusted(PC_SHADOW, PC_SHADOW,
                                                       -PC_SHADOW, -PC_SHADOW);
        p.setPen(Qt::NoPen);
        for (int i = PC_SHADOW; i >= 1; --i) {
            const int alpha = qRound(7.0 * qreal(PC_SHADOW - i + 1) / PC_SHADOW);
            p.setBrush(QColor(0, 0, 0, dark ? alpha / 2 : alpha));
            const qreal r = PC_RADIUS + i * 0.4;
            p.drawRoundedRect(card.adjusted(-i, -(i - 1), i, i), r, r);
        }
        const QColor borderCol = dark ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 18);
        p.setPen(QPen(borderCol, 1));
        p.setBrush(pal.color(QPalette::Base));
        p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), PC_RADIUS, PC_RADIUS);
    }
};

} // namespace

// ── BookmarkDialog ────────────────────────────────────────────────────────────

BookmarkDialog::BookmarkDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::BookmarkDialog)
    , m_foreground(QApplication::palette().text().color())
{
    ui->setupUi(this);
    removeDialogIcon(this);

    // Set up colour picker and wrap it in a list-group card.
    ui->colourPicker->setColumns(7);
    ui->bookmarkName->setTabChangesFocus(true);

    const QRect buttonRect = ui->buttonBox->geometry();
    auto *card = new PickerCard(this);
    ui->colourPicker->setParent(card);
    ui->colourPicker->setAttribute(Qt::WA_NoSystemBackground);

    const QRect pickerRect = ui->colourPicker->geometry();
    const int cardW = pickerRect.width() + 2 * PC_SHADOW;
    m_buttonTopGap = qMax(0, buttonRect.y() - (pickerRect.y() + pickerRect.height()));

    // Give the picker its final width before querying sizeHint height.
    ui->colourPicker->move(PC_SHADOW, PC_SHADOW);
    ui->colourPicker->resize(pickerRect.width(), 1);
    const int pickerH = ui->colourPicker->sizeHint().height();
    ui->colourPicker->resize(pickerRect.width(), pickerH);

    card->move(pickerRect.x() - PC_SHADOW, pickerRect.y() - PC_SHADOW);
    card->resize(cardW, pickerH + 2 * PC_SHADOW);

    // Strip any platform-supplied icons from OK / Cancel so the buttons stay clean.
    for (QAbstractButton *btn : ui->buttonBox->buttons())
        btn->setIcon(QIcon());

    // Rename OK → "Copy"
    if (QPushButton *ok = ui->buttonBox->button(QDialogButtonBox::Ok))
        ok->setText(tr("Add"));

    ui->buttonBox->layout()->setSpacing(16);
    relayoutDynamicControls();

    //connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &BookmarkDialog::onAccepted);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    //connect(ui->pushButton,   &QPushButton::clicked, this, &QDialog::accept);
    //connect(ui->pushButton_2, &QPushButton::clicked, this, &QDialog::reject);
}

void BookmarkDialog::relayoutDynamicControls()
{
    QWidget *card = ui->colourPicker->parentWidget();
    if (!card)
        return;

    ui->buttonBox->adjustSize();
    const QSize buttonSize = ui->buttonBox->sizeHint().expandedTo(ui->buttonBox->minimumSizeHint());
    const int buttonX = width() - DIALOG_MARGIN - buttonSize.width();
    const int buttonY = card->geometry().bottom() + 1 + m_buttonTopGap;

    ui->buttonBox->setGeometry(buttonX, buttonY, buttonSize.width(), buttonSize.height());
    setFixedSize(width(), buttonY + buttonSize.height() + DIALOG_MARGIN);
}

BookmarkDialog::~BookmarkDialog()
{
    delete ui;
}

void BookmarkDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    ui->bookmarkName->clear();
    ui->bookmarkName->setFocus();
}

void BookmarkDialog::setOffset(quint64 offset)
{
    m_offset = offset;
    updateRangeLabel();
}

void BookmarkDialog::setLength(quint64 length)
{
    m_length = length;
    updateRangeLabel();
}

void BookmarkDialog::updateRangeLabel()
{
    auto hexStr = [](quint64 v) {
        return QString("0x") + QString::number(v, 16).toUpper();
    };

    if (m_length <= 1) {
        ui->bookmarkName->setAnnotation(QString("Position: %1").arg(hexStr(m_offset)));
        return;
    }
    const quint64 end = m_offset + m_length - 1;
    ui->bookmarkName->setAnnotation(
        QString("Range: %1 %2 %3 (%4 bytes)")
            .arg(hexStr(m_offset)).arg(QChar(0x2013)).arg(hexStr(end)).arg(m_length));
}

void BookmarkDialog::setForegroundColour(const QColor &fg)
{
    m_foreground = fg;
    ui->colourPicker->setForegroundColour(fg);
}

void BookmarkDialog::setSwatchColours(const QVector<QColor> &colours)
{
    const int oldH = ui->colourPicker->height();
    ui->colourPicker->setColours(colours);
    const int newH = ui->colourPicker->sizeHint().height();
    if (newH == oldH) return;

    const int delta = newH - oldH;
    ui->colourPicker->resize(ui->colourPicker->width(), newH);

    // Resize the card (picker's parent) to match
    QWidget *card = ui->colourPicker->parentWidget();
    card->resize(card->width(), card->height() + delta);

    relayoutDynamicControls();
}

QString BookmarkDialog::bookmarkName() const
{
    return ui->bookmarkName->toPlainText().trimmed();
}

QColor BookmarkDialog::selectedColour() const
{
    return ui->colourPicker->selectedColour();
}

int BookmarkDialog::selectedColourIndex() const
{
    return ui->colourPicker->selectedIndex();
}
