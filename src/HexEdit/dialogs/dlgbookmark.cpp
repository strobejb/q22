#include "dlgbookmark.h"
#include "ui_bookmarks.h"
#include "bookmarkcolourwidget.h"

#include <QApplication>
#include <QAbstractButton>
#include <QEvent>
#include <QIcon>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QTextCursor>
#include <QVector>
#include <QLayout>

// ── PickerCard ────────────────────────────────────────────────────────────────

namespace {

static constexpr int PC_SHADOW = 4;
static constexpr int PC_RADIUS = 10;
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

    ui->colourPicker->setColumns(7);
    ui->bookmarkName->setTabChangesFocus(true);

    const QRect buttonRect = ui->buttonBox->geometry();
    auto *card = new PickerCard(this);
    ui->colourPicker->setParent(card);
    ui->colourPicker->setAttribute(Qt::WA_NoSystemBackground);

    const QRect pickerRect = ui->colourPicker->geometry();
    const int cardW = pickerRect.width() + 2 * PC_SHADOW;
    m_buttonTopGap = qMax(0, buttonRect.y() - (pickerRect.y() + pickerRect.height()));

    ui->colourPicker->move(PC_SHADOW, PC_SHADOW);
    ui->colourPicker->resize(pickerRect.width(), 1);
    const int pickerH = ui->colourPicker->sizeHint().height();
    ui->colourPicker->resize(pickerRect.width(), pickerH);

    card->move(pickerRect.x() - PC_SHADOW, pickerRect.y() - PC_SHADOW);
    card->resize(cardW, pickerH + 2 * PC_SHADOW);

    // Strip platform-supplied icons from OK / Cancel.
    for (QAbstractButton *btn : ui->buttonBox->buttons())
        btn->setIcon(QIcon());

    // Pin both buttons to the same width — text + fixed padding, ~30 % narrower
    // than the platform default.  min-width + max-width in a stylesheet overrides
    // QDialogButtonBox's internal layout so the values stick.
    {
        const QFontMetrics fm(QApplication::font());
        const int btnW = fm.horizontalAdvance(tr("Cancel")) + 28;
        ui->buttonBox->setStyleSheet(
            QString("QPushButton { min-width: %1px; max-width: %1px; }").arg(btnW));
    }

    if (QPushButton *ok = ui->buttonBox->button(QDialogButtonBox::Ok))
        ok->setText(tr("Add"));

    // Delete button lives in the .ui file — just wire it up.
    ui->bookmarkDelete->setVisible(false);
    ui->bookmarkDelete->setIconSize(QSize(16, 16));
    updateDeleteIcon();
    connect(ui->bookmarkDelete, &QToolButton::clicked, this, [this]() {
        emit deleteRequested(m_editIdx);
        reject();
    });

    ui->buttonBox->layout()->setSpacing(16);
    relayoutDynamicControls();

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

BookmarkDialog::~BookmarkDialog()
{
    delete ui;
}

// ── Layout ────────────────────────────────────────────────────────────────────

void BookmarkDialog::relayoutDynamicControls()
{
    QWidget *card = ui->colourPicker->parentWidget();
    if (!card) return;

    ui->buttonBox->adjustSize();
    const QSize buttonSize = ui->buttonBox->sizeHint().expandedTo(ui->buttonBox->minimumSizeHint());
    const int buttonX = width() - DIALOG_MARGIN - buttonSize.width();
    const int buttonY = card->geometry().bottom() + 1 + m_buttonTopGap;

    ui->buttonBox->setGeometry(buttonX, buttonY, buttonSize.width(), buttonSize.height());

    // Square button, same height as the OK/Cancel row — flat, no border.
    const int btnSz = buttonSize.height();
    ui->bookmarkDelete->setGeometry(DIALOG_MARGIN, buttonY, btnSz, btnSz);

    setFixedSize(width(), buttonY + buttonSize.height() + DIALOG_MARGIN);
}

// ── Icon recolouring ──────────────────────────────────────────────────────────

void BookmarkDialog::updateDeleteIcon()
{
    // Flat style — destructive red icon with red-tinted hover/pressed backgrounds.
    const QColor err = errorColour();
    const QString errName = err.name();
    // Hover/pressed: the error colour at reduced alpha so the tint is subtle.
    const QString hoverBg   = QString("rgba(%1,%2,%3,0.12)").arg(err.red()).arg(err.green()).arg(err.blue());
    const QString pressedBg = QString("rgba(%1,%2,%3,0.22)").arg(err.red()).arg(err.green()).arg(err.blue());
    ui->bookmarkDelete->setStyleSheet(QString(
        "QToolButton { border: none; border-radius: 4px; background: transparent; }"
        "QToolButton:hover   { background: %1; }"
        "QToolButton:pressed { background: %2; }"
    ).arg(hoverBg, pressedBg));

    // Build a two-mode icon: Normal = muted ButtonText, Active = error red.
    // Qt's Fusion style selects Active mode on hover, so the colour shift is free.
    QIcon baseIcon = QIcon::fromTheme(QStringLiteral("user-trash-symbolic"));
    if (baseIcon.isNull())
        baseIcon = QIcon(QStringLiteral(":/icons/hicolor/scalable/actions/user-trash-symbolic.svg"));
    if (baseIcon.isNull()) return;

    const int sz  = ui->bookmarkDelete->iconSize().width();
    const auto tinted = [&](const QColor &col) {
        QPixmap src = baseIcon.pixmap(sz, sz);
        QPixmap dst(src.size());
        dst.setDevicePixelRatio(src.devicePixelRatio());
        dst.fill(Qt::transparent);
        QPainter p(&dst);
        p.drawPixmap(0, 0, src);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(dst.rect(), col);
        return dst;
    };

    QIcon icon;
    icon.addPixmap(tinted(palette().color(QPalette::ButtonText)), QIcon::Normal);
    icon.addPixmap(tinted(err),                                   QIcon::Active);
    ui->bookmarkDelete->setIcon(icon);
}

void BookmarkDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        updateDeleteIcon();
}

// ── Public API ────────────────────────────────────────────────────────────────

void BookmarkDialog::setEditMode(int bookmarkIdx, const QString &name, int colourIndex)
{
    m_editIdx = bookmarkIdx;
    const bool editing = (bookmarkIdx >= 0);

    if (QPushButton *ok = ui->buttonBox->button(QDialogButtonBox::Ok))
        ok->setText(editing ? tr("Save") : tr("Add"));

    ui->bookmarkDelete->setVisible(editing);

    if (editing) {
        ui->bookmarkName->setPlainText(name);
        const auto &colours = ui->colourPicker->colours();
        if (colourIndex >= 0 && colourIndex < colours.size())
            ui->colourPicker->setSelectedColour(colours[colourIndex]);
        else
            ui->colourPicker->selectFirst();
    }
}

void BookmarkDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (m_editIdx < 0) {
        ui->bookmarkName->clear();
        ui->colourPicker->selectFirst();
    }
    ui->bookmarkName->setFocus();
    ui->bookmarkName->moveCursor(QTextCursor::End);
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
