#include "gotodialog.h"
#include "ui_gotodialog.h"
#include "datatypecombobox.h"
#include "theme.h"
#include "HexView/hexview.h"
#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QMenu>
#include <QStyle>
#include <QToolButton>
#include <QRegularExpression>
#include <algorithm>
#include <cstring>




// ─────────────────────────────────────────────────────────────────────────────

GotoDialog::GotoDialog(HexView *hv, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::GotoDialog)
    , m_hv(hv)
{
    ui->setupUi(this);
    setAutoFillBackground(true);
    hide();

    // Button style matching the titlebar aesthetic
    bool dark     = QApplication::palette().window().color().lightness() < 128;
    QString hover   = dark ? "rgba(255,255,255,0.15)" : "rgba(0,0,0,0.10)";
    QString pressed = dark ? "rgba(255,255,255,0.25)" : "rgba(0,0,0,0.18)";
    // Use the palette mid colour for the QLineEdit border so it matches the
    // combobox border defined in theme.cpp's buildStylesheet().  Once any
    // border property is set via stylesheet Qt stops drawing the platform
    // default border, so we must declare it explicitly.
    QString borderCol = QApplication::palette().mid().color().name();
    setStyleSheet(QString(R"(
        QToolButton {
            border: none;
            border-radius: 6px;
            background: transparent;
        }
        QToolButton:hover   { background: %1; }
        QToolButton:pressed { background: %2; }
        QToolButton::menu-indicator { image: none; width: 0; }
        QLineEdit {
            margin: 2px 0;
            border: 1px solid %3;
            border-radius: 6px;
        }
    )").arg(hover, pressed, borderCol));

    // Options menu
    auto *optMenu = new QMenu(this);
    themeMenu(optMenu);



    // Scale inner padding with screen DPI: 2 px at 100 %, 3 px at 150 %, 4 px at 200 %.
    const int kPad = qMax(1, qRound(qApp->devicePixelRatio() * 2.0));
    ui->editOffset->setTextMargins(kPad + 2, kPad, kPad + 2, kPad);
    ui->editOffset->setMinimumHeight(ui->editOffset->minimumSizeHint().height() + 2 * kPad);

    // Colour the top border line
    ui->topBorder->setStyleSheet(
        QString("background-color: %1;")
        .arg(QApplication::palette().mid().color().name()));

    // Replace the plain QComboBox placeholder (items defined in .ui) with a
    // DataTypeComboBox, copying the item model across before swapping.
    m_comboBookmarks = new DataTypeComboBox(this);
    for (int i = 0; i < ui->comboBookmarks->count(); ++i)
        m_comboBookmarks->addItem(ui->comboBookmarks->itemText(i));
    ui->horizontalLayout->replaceWidget(ui->comboBookmarks, m_comboBookmarks);
    m_comboBookmarks->setFixedWidth(ui->comboBookmarks->minimumWidth());
    ui->comboBookmarks->hide();

    m_comboBookmarks->buildMenu();
    /*m_comboDataType->setActionData("Hex",    SearchHex);
    m_comboDataType->setActionData("UTF-8",  SearchUTF8);
    m_comboDataType->setActionData("UTF-16", SearchUTF16);
    m_comboDataType->setActionData("UTF-32", SearchUTF32);
    m_comboDataType->setActionData("Byte",   SearchByte);
    m_comboDataType->setActionData("Word",   SearchWord);
    m_comboDataType->setActionData("Dword",  SearchDword);
    m_comboDataType->setDisplayText(m_comboDataType->selectionText());*/

    // Keep the search field's font in sync with the Type combo so both
    // controls render text at the same size.
    ui->editOffset->setFont(m_comboBookmarks->font());
    connect(m_comboBookmarks, &DataTypeComboBox::selectionChanged, this, [this](int) {
        m_comboBookmarks->setDisplayText(m_comboBookmarks->selectionText());
        //updateSearchHexPreview();
        connect(ui->editOffset, &QLineEdit::textChanged, this, [this] { updateSearchHexPreview(); });
    });

    connect(ui->btnClose, &QToolButton::clicked, this, &QWidget::hide);

    // Navigation popup menu (Find Previous / Find Next)


    connect(ui->editOffset,     &QLineEdit::returnPressed, this, [this] { triggerSearch(0); });
    connect(ui->btnGotoAddress, &QToolButton::clicked,     this, [this] { triggerSearch(0); });

#ifdef Q_OS_WIN
    // QIcon::fromTheme() returns null on Windows; use Segoe MDL2 / QStyle fallbacks.
    ui->btnNavigate->setIcon(segoeIcon(0xEBE8,
        QApplication::palette().buttonText().color(), 14));
    ui->btnClose->setIcon(QApplication::style()->standardIcon(
        QStyle::SP_TitleBarCloseButton));
    // No great SP_ for a find-options menu button; show a text indicator instead.
    ui->btnOptions->setIcon(QIcon());
    ui->btnOptions->setText("☰");
    ui->btnNavigate->setIconSize(QSize(16, 16));
    ui->btnClose->setIconSize(QSize(16, 16));
#endif
}

GotoDialog::~GotoDialog()
{
    delete ui;
}

void GotoDialog::activate(const QString &initialText)
{
    if (!initialText.isEmpty())
        ui->editOffset->setText(initialText);
    show();
    ui->editOffset->setFocus();
    ui->editOffset->selectAll();
}

void GotoDialog::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        if (ui->editOffset->text().isEmpty())
            hide();
        else
            ui->editOffset->clear();
    } else {
        QWidget::keyPressEvent(e);
    }
}


void GotoDialog::hideEvent(QHideEvent *e)
{
    //emit searchHexChanged({});
    QWidget::hideEvent(e);
}

void GotoDialog::updateSearchHexPreview()
{
}

void GotoDialog::triggerSearch(uint /*flags*/)
{
    const QString text = ui->editOffset->text().trimmed();
    if (text.isEmpty())
        return;

    bool ok = false;
    size_w offset = text.toULongLong(&ok, 16);
    if (!ok)
        return;

    if (m_hv->setCurPos(offset)) {
        m_hv->scrollTo(offset);
        m_hv->setFocus();
    }
}
