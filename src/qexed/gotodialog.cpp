#include "gotodialog.h"
#include "ui_gotodialog.h"
#include "datatypecombobox.h"
#include "theme.h"
#include "HexView/hexview.h"
#include "HexView/hexviewbookmark.h"
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QStyle>
#include <QToolButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <algorithm>
#include <cstring>




// ─────────────────────────────────────────────────────────────────────────────

GotoDialog::GotoDialog(HexView *hv, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::GotoDialog)
    , m_hv(hv)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_StyledBackground, true);
    ui->verticalLayout->insertWidget(0, new Hairline(this));
    hide();

    refreshStylesheet();

    // Options menu
    auto *optMenu = new QMenu(this);
    themeMenu(optMenu);



    // Scale inner padding with screen DPI: 2 px at 100 %, 3 px at 150 %, 4 px at 200 %.
    const int kPad = qMax(1, qRound(qApp->devicePixelRatio() * 2.0));
    ui->editOffset->setTextMargins(kPad + 2, kPad, kPad + 2, kPad);
    ui->editOffset->setMinimumHeight(ui->editOffset->minimumSizeHint().height() + 2 * kPad);

    ui->editOffset->setValidator(
        new QRegularExpressionValidator(QRegularExpression("[0-9A-Fa-f]*"), ui->editOffset));

    // Replace the plain QComboBox placeholder (items defined in .ui) with a
    // DataTypeComboBox, copying the item model across before swapping.
    m_comboBookmarks = new DataTypeComboBox(this);
    for (int i = 0; i < ui->comboBookmarks->count(); ++i)
        m_comboBookmarks->addItem(ui->comboBookmarks->itemText(i));
    ui->horizontalLayout->replaceWidget(ui->comboBookmarks, m_comboBookmarks);
    m_comboBookmarks->setFixedWidth(ui->comboBookmarks->minimumWidth());
    ui->comboBookmarks->hide();

    // Keep the search field's font in sync with the bookmark combo.
    ui->editOffset->setFont(m_comboBookmarks->font());

    // Navigate to selected bookmark; idx=-1 means "New Bookmark..." was chosen.
    // The display text is intentionally not updated — it always shows "Bookmark...".
    connect(m_comboBookmarks, &DataTypeComboBox::selectionChanged, this, [this](int) {
        QVariant data = m_comboBookmarks->selectionData();
        if (data.isNull()) return;
        const int idx = data.toInt();
        if (idx == -1) {
            emit bookmarkRequested();
            return;
        }
        const QList<Bookmark> &bms = m_hv->bookmarks();
        if (idx >= 0 && idx < bms.size()) {
            const Bookmark &bm = bms[idx];
            // Select the full bookmark range; cursor stays at the start.
            m_hv->setCurSel(bm.offset + bm.length, bm.offset);
            m_hv->scrollTo(bm.offset);
            m_hv->setFocus();
        }
    });

    ui->btnBookmark->hide();
    connect(ui->btnClose, &QToolButton::clicked, this, &QWidget::hide);

    // Navigation popup menu (Find Previous / Find Next)


    connect(ui->editOffset, &QLineEdit::returnPressed, this, [this] { triggerSearch(0); });

    // Leading icons: arrow in the address field, star in the bookmarks combo.
    {
        const QColor placeholderCol = QApplication::palette().placeholderText().color();
        const QColor borderCol      = QApplication::palette().mid().color();
        const QIcon arrowIc = recoloredIcon("thin-arrow-right-icon", placeholderCol, 16);
        const QIcon starIc  = recoloredIcon("starred-symbolic",       borderCol,      16);
        if (!arrowIc.isNull())
            ui->editOffset->addAction(arrowIc, QLineEdit::LeadingPosition);
        if (!starIc.isNull())
            m_comboBookmarks->setLeadingIcon(starIc);
    }

#if 0//def 0//Q_OS_WIN
    // QIcon::fromTheme() returns null on Windows; use Segoe MDL2 / QStyle fallbacks.
    /*ui->btnNavigate->setIcon(segoeIcon(0xEBE8,
        QApplication::palette().buttonText().color(), 14));
    ui->btnClose->setIcon(QApplication::style()->standardIcon(
        QStyle::SP_TitleBarCloseButton));
    // No great SP_ for a find-options menu button; show a text indicator instead.
    ui->btnOptions->setIcon(QIcon());
    ui->btnOptions->setText("☰");
    ui->btnNavigate->setIconSize(QSize(16, 16));*/
    ui->btnClose->setIconSize(QSize(16, 16));
#else
    recolorToolButtons(this);
#endif
}

void GotoDialog::refreshStylesheet()
{
    const bool dark       = QApplication::palette().window().color().lightness() < 128;
    const QString hover   = dark ? "rgba(255,255,255,0.15)" : "rgba(0,0,0,0.10)";
    const QString pressed = dark ? "rgba(255,255,255,0.25)" : "rgba(0,0,0,0.18)";
    const QString borderCol = QApplication::palette().mid().color().name();
    setStyleSheet(QString(R"(
        QToolButton {
            border: none;
            border-radius: 6px;
            background: transparent;
        }
        QToolButton:hover   { background: %1; }
        QToolButton:focus   { border: 2px solid palette(highlight); }
        QToolButton:pressed { background: %2; }
        QToolButton::menu-indicator { image: none; width: 0; }
        #editOffset {
            margin: 1px;
            border: 1px solid %3;
            border-radius: 6px;
            padding: 0;
        }
        #editOffset:hover {
            margin: 1px;
            border: 1px solid %3;
            border-radius: 6px;
            padding: 0;
        }
        #editOffset:focus {
            margin: 0;
            border: 2px solid palette(highlight);
            border-radius: 6px;
            padding: 0;
        }
    )").arg(hover, pressed, borderCol));
}

void GotoDialog::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::PaletteChange && !m_inRefresh) {
        m_inRefresh = true;
        refreshStylesheet();
        recolorToolButtons(this);
        m_inRefresh = false;
    }
    QWidget::changeEvent(e);
}

GotoDialog::~GotoDialog()
{
    delete ui;
}

void GotoDialog::activate(const QString &initialText)
{
    refreshBookmarks();
    if (!initialText.isEmpty())
        ui->editOffset->setText(initialText);
    show();
    ui->editOffset->setFocus();
    ui->editOffset->selectAll();
}

void GotoDialog::refreshBookmarks()
{
    const QList<Bookmark> &bms = m_hv->bookmarks();

    m_comboBookmarks->clear();
    const QString newBmLabel = tr("New Bookmark...\tCtrl+B");
    m_comboBookmarks->addItem(newBmLabel);
    if (!bms.isEmpty())
        m_comboBookmarks->addItem(QString());   // separator

    QStringList labels;
    for (const Bookmark &bm : bms) {
        const QString hex   = QString::number(bm.offset, 16).toUpper();
        const QString label = bm.name.isEmpty()
            ? QStringLiteral("(unnamed) (%1)").arg(hex)
            : QStringLiteral("%1 (%2)").arg(bm.name, hex);
        m_comboBookmarks->addItem(label);
        labels.append(label);
    }
    m_comboBookmarks->buildMenu(/*checkable=*/false);
    m_comboBookmarks->setDisplayText(tr("Bookmarks..."));

    m_comboBookmarks->setActionData(newBmLabel, QVariant::fromValue<int>(-1));
    for (int i = 0; i < bms.size(); ++i)
        m_comboBookmarks->setActionData(labels[i], QVariant::fromValue<int>(i));

    m_comboBookmarks->setEnabled(true);   // always enabled; "New Bookmark..." is always present
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

