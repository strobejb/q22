#include "gotopanel.h"
#include "ui_gotopanel.h"
#include "combos/datatypecombobox.h"
#include "panels/dockpanelrow.h"
#include "theme.h"
#include "HexView/hexview.h"
#include "HexView/hexviewbookmark.h"
#include <QAction>
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

GotoPanel::GotoPanel(HexView *hv, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::GotoPanel)
    , m_hv(hv)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_StyledBackground, true);
    ui->verticalLayout->insertWidget(0, new Hairline(this));
    m_row = new DockPanelRow(this);
    m_row->adoptFrom(ui->horizontalLayout);
    ui->verticalLayout->removeItem(ui->horizontalLayout);
    delete ui->horizontalLayout;
    ui->verticalLayout->addWidget(m_row);
    hide();

    refreshStylesheet();

    // Options menu
    auto *optMenu = new QMenu(this);
    themeMenu(optMenu);



    // Scale inner padding with screen DPI: 2 px at 100 %, 3 px at 150 %, 4 px at 200 %.
    const int kPad = qMax(1, qRound(qApp->devicePixelRatio() * 2.0));
    const int controlHeight = DockPanelRow::inputHeight(ui->editOffset);
    ui->editOffset->setTextMargins(kPad + 2, kPad, kPad + 2, kPad);
    ui->editOffset->setFixedHeight(controlHeight);
    m_row->setControlAlignment(ui->editOffset);

    ui->editOffset->setValidator(
        new QRegularExpressionValidator(QRegularExpression("[0-9A-Fa-f]*"), ui->editOffset));

    // Replace the plain QComboBox placeholder (items defined in .ui) with a
    // DataTypeComboBox, copying the item model across before swapping.
    m_comboBookmarks = new DataTypeComboBox(this);
    for (int i = 0; i < ui->comboBookmarks->count(); ++i)
        m_comboBookmarks->addItem(ui->comboBookmarks->itemText(i));
    m_row->replaceWidget(ui->comboBookmarks, m_comboBookmarks);
    m_comboBookmarks->setFixedWidth(ui->comboBookmarks->minimumWidth());
    m_comboBookmarks->setFixedHeight(controlHeight);
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
        const QString iconName = QStringLiteral("forward");
        QIcon arrowIc(QStringLiteral(":/icons/hicolor/scalable/actions/") + iconName + QStringLiteral(".svg"));
        if (arrowIc.isNull())
            arrowIc = QIcon::fromTheme(iconName);
        if (!arrowIc.isNull()) {
            QAction *action = ui->editOffset->addAction(arrowIc, QLineEdit::LeadingPosition);
            action->setProperty("iconThemeName", iconName);
            action->setProperty("iconColorRole", QStringLiteral("placeholderText"));
            action->setProperty("iconSize", 16);
        }
        m_comboBookmarks->addIconAction(QStringLiteral("bookmark-star-on-tray"));
    }

    // Trailing clear button — visible only when the field has content.
    {
        const auto existingBtns = ui->editOffset->findChildren<QToolButton *>();
        QAction *clearAct = ui->editOffset->addAction(QIcon(), QLineEdit::TrailingPosition);
        clearAct->setProperty("iconThemeName",  QStringLiteral("edit-clear-symbolic"));
        clearAct->setProperty("iconColorRole",  QStringLiteral("placeholderText"));
        clearAct->setProperty("iconSize", 16);
        clearAct->setVisible(false);
        clearAct->setToolTip(tr("Clear"));
        for (auto *btn : ui->editOffset->findChildren<QToolButton *>())
            if (!existingBtns.contains(btn))
                btn->setCursor(Qt::PointingHandCursor);
        connect(ui->editOffset, &QLineEdit::textChanged, clearAct,
                [clearAct](const QString &text) { clearAct->setVisible(!text.isEmpty()); });
        connect(clearAct, &QAction::triggered, ui->editOffset, &QLineEdit::clear);
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

void GotoPanel::refreshStylesheet()
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
            margin: 0;
            border: 1px solid %3;
            border-radius: 6px;
            padding: 1px;
        }
        #editOffset:hover {
            margin: 0;
            border: 1px solid %3;
            border-radius: 6px;
            padding: 1px;
        }
        #editOffset:focus {
            margin: 0;
            border: 2px solid palette(highlight);
            border-radius: 6px;
            padding: 0;
        }
    )").arg(hover, pressed, borderCol));
}

void GotoPanel::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::PaletteChange && !m_inRefresh) {
        m_inRefresh = true;
        refreshStylesheet();
        recolorToolButtons(this);
        m_inRefresh = false;
    }
    QWidget::changeEvent(e);
}

GotoPanel::~GotoPanel()
{
    delete ui;
}

void GotoPanel::activate(const QString &initialText)
{
    refreshBookmarks();
    if (!initialText.isEmpty())
        ui->editOffset->setText(initialText);
    show();
    ui->editOffset->setFocus();
    ui->editOffset->selectAll();
}

void GotoPanel::refreshBookmarks()
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

void GotoPanel::keyPressEvent(QKeyEvent *e)
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


void GotoPanel::hideEvent(QHideEvent *e)
{
    //emit searchHexChanged({});
    QWidget::hideEvent(e);
}

void GotoPanel::updateSearchHexPreview()
{
}

void GotoPanel::triggerSearch(uint /*flags*/)
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


