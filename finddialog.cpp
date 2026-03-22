#include "finddialog.h"
#include "ui_finddialog.h"
#include "datatypecombobox.h"
#include "theme.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QStyle>
#include <QToolButton>

FindDialog::FindDialog(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FindDialog)
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

    m_actRegex     = optMenu->addAction(tr("Regular expression"));
    m_actRegex->setCheckable(true);

    m_actWholeWord = optMenu->addAction(tr("Match whole word"));
    m_actWholeWord->setCheckable(true);

    optMenu->addSeparator();

    m_actWrap      = optMenu->addAction(tr("Wrap around"));
    m_actWrap->setCheckable(true);
    m_actWrap->setChecked(true);

    connect(ui->btnOptions, &QToolButton::clicked, this, [this, optMenu]() {
        optMenu->popup(smartMenuPos(ui->btnOptions, optMenu));
    });

    // Scale inner padding with screen DPI: 2 px at 100 %, 3 px at 150 %, 4 px at 200 %.
    const int kPad = qMax(1, qRound(qApp->devicePixelRatio() * 2.0));
    ui->editFind->setTextMargins(kPad + 2, kPad, kPad + 2, kPad);
    ui->editFind->setMinimumHeight(ui->editFind->minimumSizeHint().height() + 2 * kPad);

    // Colour the top border line
    ui->topBorder->setStyleSheet(
        QString("background-color: %1;")
        .arg(QApplication::palette().mid().color().name()));

    // Replace the plain QComboBox placeholder (items defined in .ui) with a
    // DataTypeComboBox, copying the item model across before swapping.
    m_comboDataType = new DataTypeComboBox(this);
    for (int i = 0; i < ui->comboDataType->count(); ++i)
        m_comboDataType->addItem(ui->comboDataType->itemText(i));
    ui->horizontalLayout->replaceWidget(ui->comboDataType, m_comboDataType);
    ui->comboDataType->hide();

    m_comboDataType->buildMenu();
    m_comboDataType->setDisplayText(m_comboDataType->selectionText());

    // Keep the search field's font in sync with the Type combo so both
    // controls render text at the same size.
    ui->editFind->setFont(m_comboDataType->font());
    connect(m_comboDataType, &DataTypeComboBox::selectionChanged, this, [this](int) {
        m_comboDataType->setDisplayText(m_comboDataType->selectionText());
    });

    connect(ui->btnClose, &QToolButton::clicked, this, &QWidget::hide);

    // Navigation popup menu (Find Previous / Find Next)
    auto *navMenu = new QMenu(this);
    themeMenu(navMenu);
    auto *actPrev = navMenu->addAction(tr("Find Previous\tShift+F3"));
    auto *actNext = navMenu->addAction(tr("Find Next\tF3"));
    connect(ui->btnNavigate, &QToolButton::clicked, this, [this, navMenu]() {
        navMenu->popup(smartMenuPos(ui->btnNavigate, navMenu));
    });
    connect(actPrev, &QAction::triggered, this, &FindDialog::findPrevious);
    connect(actNext, &QAction::triggered, this, &FindDialog::findNext);

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

FindDialog::~FindDialog()
{
    delete ui;
}

void FindDialog::activate(const QString &initialText)
{
    if (!initialText.isEmpty())
        ui->editFind->setText(initialText);
    show();
    ui->editFind->setFocus();
    ui->editFind->selectAll();
}

void FindDialog::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        if (ui->editFind->text().isEmpty())
            hide();
        else
            ui->editFind->clear();
    } else {
        QWidget::keyPressEvent(e);
    }
}

bool    FindDialog::isRegex()     const { return m_actRegex->isChecked(); }
bool    FindDialog::isWholeWord() const { return m_actWholeWord->isChecked(); }
bool    FindDialog::isWrapAround() const { return m_actWrap->isChecked(); }
QString FindDialog::dataType()    const { return m_comboDataType->selectionText(); }
