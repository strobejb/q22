#include "finddialog.h"
#include "ui_finddialog.h"
#include "datatypecombobox.h"
#include "theme.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
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
    setStyleSheet(QString(R"(
        QToolButton {
            border: none;
            border-radius: 6px;
            background: transparent;
        }
        QToolButton:hover   { background: %1; }
        QToolButton:pressed { background: %2; }
        QToolButton::menu-indicator { image: none; width: 0; }
    )").arg(hover, pressed));

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

    ui->btnOptions->setMenu(optMenu);

    static constexpr int kPad = 3;
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
    connect(m_comboDataType, &DataTypeComboBox::selectionChanged, this, [this](int) {
        m_comboDataType->setDisplayText(m_comboDataType->selectionText());
    });

    connect(ui->btnClose, &QToolButton::clicked, this, &QWidget::hide);
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
    if (e->key() == Qt::Key_Escape)
        hide();
    else
        QWidget::keyPressEvent(e);
}

bool    FindDialog::isRegex()     const { return m_actRegex->isChecked(); }
bool    FindDialog::isWholeWord() const { return m_actWholeWord->isChecked(); }
bool    FindDialog::isWrapAround() const { return m_actWrap->isChecked(); }
QString FindDialog::dataType()    const { return m_comboDataType->selectionText(); }
