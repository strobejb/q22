#include "bookmarkdialog.h"
#include "ui_bookmarks.h"
#include "colourpickerwidget.h"

#include <QApplication>
#include <QPainter>
#include <QShowEvent>
#include <QVector>

// ── PickerCard ────────────────────────────────────────────────────────────────
// Rounded card with drop shadow — same visual style as SettingsGroup.

namespace {

static constexpr int PC_SHADOW = 4;
static constexpr int PC_RADIUS = 10;
static constexpr int PC_PAD    = 0;   // ColourPickerWidget's own SWATCH_PAD supplies the margin

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

    auto *card = new PickerCard(this);
    ui->colourPicker->setParent(card);
    ui->colourPicker->setAttribute(Qt::WA_NoSystemBackground);

    // Original picker geometry from .ui: x=30, y=160, width=271, height=38.
    static constexpr int ORIG_X = 30, ORIG_Y = 160, ORIG_W = 271;
    const int cardW   = ORIG_W + 2 * PC_SHADOW;

    // Give the picker its final width before querying sizeHint height.
    ui->colourPicker->move(PC_SHADOW, PC_SHADOW);
    ui->colourPicker->resize(ORIG_W, 1);
    const int pickerH = ui->colourPicker->sizeHint().height();
    ui->colourPicker->resize(ORIG_W, pickerH);

    card->move(ORIG_X - PC_SHADOW, ORIG_Y - PC_SHADOW);
    card->resize(cardW, pickerH + 2 * PC_SHADOW);

    // Position buttons: span the full control width (same as other widgets),
    // centred, stretching from x=ORIG_X to x=ORIG_X+ORIG_W.
    static constexpr int ORIG_PICKER_BOTTOM = ORIG_Y + 38;
    static constexpr int ORIG_BTN_Y         = 228;
    static constexpr int BTN_GAP            = 8;
    static constexpr int BTN_H              = 27;
    const int gap    = ORIG_BTN_Y - ORIG_PICKER_BOTTOM;
    const int newBtnY = card->geometry().bottom() + 1 + gap;
    const int bw1    = (ORIG_W - BTN_GAP) / 2;
    const int bw2    = ORIG_W - BTN_GAP - bw1;
    ui->pushButton_2->setGeometry(ORIG_X,            newBtnY, bw1, BTN_H); // Cancel
    ui->pushButton->setGeometry  (ORIG_X + bw1 + BTN_GAP, newBtnY, bw2, BTN_H); // OK

    setFixedSize(width(), newBtnY + BTN_H + ORIG_X);

    connect(ui->pushButton,   &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->pushButton_2, &QPushButton::clicked, this, &QDialog::reject);
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

    const QColor mid = palette().color(QPalette::Mid);
    ui->rangeLabel->setStyleSheet(QString("color: %1;").arg(mid.name()));

    if (m_length <= 1) {
        ui->rangeLabel->setText(hexStr(m_offset));
        return;
    }
    const quint64 end = m_offset + m_length - 1;
    ui->rangeLabel->setText(QString("%1 %2 %3 (%4 bytes)")
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

    // Shift buttons and expand dialog
    ui->pushButton->move  (ui->pushButton->x(),   ui->pushButton->y()   + delta);
    ui->pushButton_2->move(ui->pushButton_2->x(), ui->pushButton_2->y() + delta);
    setFixedSize(width(), height() + delta);
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
