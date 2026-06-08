#include "findpanel.h"
#include "ui_findpanel.h"
#include "combos/datatypecombobox.h"
#include "panels/dockpanelrow.h"
#include "theme.h"
#include "HexView/hexview.h"
#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QStyle>
#include <QToolButton>
#include <QRegularExpression>

static QString dumpHex(const QByteArray &ba)
{
    if (ba.isEmpty()) return {};
    QString s;
    s.reserve(ba.size() * 3 - 1);
    for (int i = 0; i < ba.size(); ++i) {
        if (i) s += u' ';
        s += QString::number((uint8_t)ba[i], 16).rightJustified(2, u'0').toUpper();
    }
    return s;
}

FindPanel::FindPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FindPanel)
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

    m_actRegex     = optMenu->addAction(tr("Regular expression"));
    m_actRegex->setCheckable(true);
    m_actRegex->setEnabled(false);

    m_actWholeWord = optMenu->addAction(tr("Match whole word"));
    m_actWholeWord->setCheckable(true);
    m_actWholeWord->setEnabled(false);

    m_actHighlightAll = optMenu->addAction(tr("Highlight all occurrences"));
    m_actHighlightAll->setCheckable(true);
    m_actHighlightAll->setChecked(true);
    connect(m_actHighlightAll, &QAction::toggled,
            this, &FindPanel::highlightAllOccurrencesChanged);

    optMenu->addSeparator();

    m_actWrap      = optMenu->addAction(tr("Wrap around"));
    m_actWrap->setCheckable(true);
    m_actWrap->setChecked(true);

    connect(ui->btnOptions, &QToolButton::clicked, this, [this, optMenu]() {
        if (optMenu->isVisible()) { optMenu->hide(); return; }
        const QPoint cur = QCursor::pos();
        const bool same  = (m_optMenuClosePos == cur);
        m_optMenuClosePos = {-1, -1};
        if (same) return;
        connect(optMenu, &QMenu::aboutToHide, this,
                [this]() { m_optMenuClosePos = QCursor::pos(); },
                Qt::SingleShotConnection);
        optMenu->popup(smartMenuPos(ui->btnOptions, optMenu));
    });

    // Scale inner padding with screen DPI: 2 px at 100 %, 3 px at 150 %, 4 px at 200 %.
    const int kPad = qMax(1, qRound(qApp->devicePixelRatio() * 2.0));
    const int controlHeight = DockPanelRow::inputHeight(ui->editFind);
    ui->editFind->setTextMargins(kPad + 2, kPad, kPad + 2, kPad);
    ui->editFind->setFixedHeight(controlHeight);
    m_row->setControlAlignment(ui->editFind);

    // Leading search icon inside the text field.
    {
        const QString iconName = QStringLiteral("edit-find-symbolic");
        QIcon searchIc(QStringLiteral(":/icons/actions/") + iconName + QStringLiteral(".svg"));
        if (searchIc.isNull())
            searchIc = QIcon::fromTheme(iconName);
        if (!searchIc.isNull()) {
            QAction *action = ui->editFind->addAction(searchIc, QLineEdit::LeadingPosition);
            action->setProperty("iconThemeName", iconName);
            action->setProperty("iconColorRole", QStringLiteral("placeholderText"));
            action->setProperty("iconSize", 16);
        }
    }

    // Trailing clear button â€” visible only when the field has content.
    {
        const auto existingBtns = ui->editFind->findChildren<QToolButton *>();
        QAction *clearAct = ui->editFind->addAction(QIcon(), QLineEdit::TrailingPosition);
        clearAct->setProperty("iconThemeName",  QStringLiteral("edit-clear-symbolic"));
        clearAct->setProperty("iconColorRole",  QStringLiteral("placeholderText"));
        clearAct->setProperty("iconSize", 16);
        clearAct->setVisible(false);
        clearAct->setToolTip(tr("Clear"));
        for (auto *btn : ui->editFind->findChildren<QToolButton *>())
            if (!existingBtns.contains(btn))
                btn->setCursor(Qt::PointingHandCursor);
        connect(ui->editFind, &QLineEdit::textChanged, clearAct,
                [clearAct](const QString &text) { clearAct->setVisible(!text.isEmpty()); });
        connect(clearAct, &QAction::triggered, ui->editFind, &QLineEdit::clear);
    }


    // Replace the plain QComboBox placeholder (items defined in .ui) with a
    // DataTypeComboBox, copying the item model across before swapping.
    m_comboDataType = new DataTypeComboBox(this);
    for (int i = 0; i < ui->comboDataType->count(); ++i)
        m_comboDataType->addItem(ui->comboDataType->itemText(i));
    m_row->replaceWidget(ui->comboDataType, m_comboDataType);
    m_comboDataType->setFixedWidth(qMax(ui->comboDataType->minimumWidth() + 20,
                                        m_comboDataType->fontMetrics().horizontalAdvance(tr("Dword (Signed, Big endian)")) + 66));
    m_comboDataType->setFixedHeight(controlHeight);
    ui->comboDataType->hide();

    m_comboDataType->buildMenu();
    m_comboDataType->setActionData("Hex",    SearchHex);
    m_comboDataType->setActionData("UTF-8",  SearchUTF8);
    m_comboDataType->setActionData("UTF-16", SearchUTF16);
    m_comboDataType->setActionData("UTF-32", SearchUTF32);
    m_comboDataType->setActionData("Byte",   SearchByte);
    m_comboDataType->setActionData("Word",   SearchWord);
    m_comboDataType->setActionData("Dword",  SearchDword);
    const QList<DataTypeComboBox::InlineModifier> endianMods = {
        { QStringLiteral("textBigEndian"), tr("Big endian") }
    };
    const QList<DataTypeComboBox::InlineModifier> endianPlaceholderMods = {
        { QStringLiteral("textBigEndian"), tr("Big endian"), false }
    };
    const QList<DataTypeComboBox::InlineModifier> signedMods = {
        { QStringLiteral("integerBigEndian"), tr("Big endian"), false },
        { QStringLiteral("signed"), tr("Signed") }
    };
    const QList<DataTypeComboBox::InlineModifier> integerMods = {
        { QStringLiteral("integerBigEndian"), tr("Big endian") },
        { QStringLiteral("signed"), tr("Signed") }
    };
    m_comboDataType->setActionInlineModifiers("UTF-8", endianPlaceholderMods);
    m_comboDataType->setActionInlineModifiers("UTF-16", endianMods);
    m_comboDataType->setActionInlineModifiers("UTF-32", endianMods);
    m_comboDataType->setActionInlineModifiers("Byte", signedMods);
    m_comboDataType->setActionInlineModifiers("Word", integerMods);
    m_comboDataType->setActionInlineModifiers("Dword", integerMods);
    m_comboDataType->setInlineModifierChecked(QStringLiteral("textBigEndian"), true);
    refreshDataTypeDisplayText();
    m_comboDataType->addIconAction(QStringLiteral("type100-001"));

    // Keep the search field's font in sync with the Type combo so both
    // controls render text at the same size.
    ui->editFind->setFont(m_comboDataType->font());
    connect(m_comboDataType, &DataTypeComboBox::selectionChanged, this, [this](int) {
        refreshDataTypeDisplayText();
        // Remember the last text-encoding choice so pane-1 activations restore it.
        const auto dt = m_comboDataType->selectionData().value<SearchDataType>();
        if (dt == SearchUTF8 || dt == SearchUTF16 || dt == SearchUTF32)
            m_lastTextType = dt;
        updateSearchHexPreview();
    });
    connect(m_comboDataType, &DataTypeComboBox::inlineModifierToggled, this, [this](const QString &, bool) {
        refreshDataTypeDisplayText();
        updateSearchHexPreview();
    });
    connect(ui->editFind, &QLineEdit::textChanged, this, [this] { updateSearchHexPreview(); });

    connect(ui->btnClose, &QToolButton::clicked, this, &QWidget::hide);

    // // Navigation popup menu (Find Previous / Find Next) â€” kept for reference
    // auto *navMenu = new QMenu(this);
    // themeMenu(navMenu);
    // auto *actPrev = navMenu->addAction(tr("Find Previous\tShift+F3"));
    // auto *actNext = navMenu->addAction(tr("Find Next\tF3"));
    // connect(ui->btnNavigate, &QToolButton::clicked, this, [this, navMenu]() {
    //     if (navMenu->isVisible()) { navMenu->hide(); return; }
    //     const QPoint cur = QCursor::pos();
    //     const bool same  = (m_navMenuClosePos == cur);
    //     m_navMenuClosePos = {-1, -1};
    //     if (same) return;
    //     connect(navMenu, &QMenu::aboutToHide, this,
    //             [this]() { m_navMenuClosePos = QCursor::pos(); },
    //             Qt::SingleShotConnection);
    //     navMenu->popup(smartMenuPos(ui->btnNavigate, navMenu));
    // });
    // connect(actPrev, &QAction::triggered, this, &FindPanel::findPrevious);
    // connect(actNext, &QAction::triggered, this, &FindPanel::findNext);

    connect(ui->btnFindPrev, &QToolButton::clicked, this, &FindPanel::findPrevious);
    connect(ui->btnFindNext, &QToolButton::clicked, this, &FindPanel::findNext);

    connect(ui->editFind, &QLineEdit::returnPressed, this, [this] { triggerSearch(0); });
    m_comboDataType->installEventFilter(this);

#if 0//def 0//Q_OS_WIN
    // QIcon::fromTheme() returns null on Windows; use Segoe MDL2 / QStyle fallbacks.
    ui->btnNavigate->setIcon(segoeIcon(0xEBE8,
        QApplication::palette().buttonText().color(), 14));
    // No great SP_ for a find-options menu button; show a text indicator instead.
    ui->btnOptions->setIcon(QIcon());
    ui->btnOptions->setText("â˜°");
    ui->btnNavigate->setIconSize(QSize(16, 16));
    ui->btnClose->setIconSize(QSize(16, 16));
#else
    recolorToolButtons(this);
#endif
}

void FindPanel::refreshStylesheet()
{
    const bool dark      = QApplication::palette().window().color().lightness() < 128;
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
        #editFind {
            margin: 0;
            border: 1px solid %3;
            border-radius: 6px;
            padding: 1px;
        }
        #editFind:hover {
            margin: 0;
            border: 1px solid %3;
            border-radius: 6px;
            padding: 1px;
        }
        #editFind:focus {
            margin: 0;
            border: 2px solid palette(highlight);
            border-radius: 6px;
            padding: 0;
        }
    )").arg(hover, pressed, borderCol));
}

void FindPanel::refreshSearchIcon()
{
    recolorToolButtons(ui->editFind);
}

void FindPanel::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::PaletteChange && !m_inRefresh) {
        m_inRefresh = true;
        refreshStylesheet();
        recolorToolButtons(this);
        m_inRefresh = false;
    }
    QWidget::changeEvent(e);
}

FindPanel::~FindPanel()
{
    delete ui;
}

void FindPanel::activate(const QString &initialText, int pane)
{
    if (pane == 0)
        m_comboDataType->selectByData(QVariant::fromValue(SearchHex));
    else if (pane == 1)
        m_comboDataType->selectByData(QVariant::fromValue(m_lastTextType));

    if (!initialText.isEmpty())
        ui->editFind->setText(initialText);
    show();
    ui->editFind->setFocus();
    ui->editFind->selectAll();
}

bool FindPanel::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_comboDataType && e->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(e);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
                && !m_comboDataType->property("popupOpen").toBool()) {
            triggerSearch(0);
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

void FindPanel::keyPressEvent(QKeyEvent *e)
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

bool    FindPanel::isRegex()     const { return m_actRegex->isChecked(); }
bool    FindPanel::isWholeWord() const { return m_actWholeWord->isChecked(); }
bool    FindPanel::isWrapAround() const { return m_actWrap->isChecked(); }
bool    FindPanel::highlightAllOccurrences() const { return m_actHighlightAll->isChecked(); }
QString FindPanel::dataType()    const { return m_comboDataType->selectionText(); }
bool    FindPanel::searchTextBigEndian() const { return m_comboDataType->inlineModifierChecked(QStringLiteral("textBigEndian")); }
bool    FindPanel::searchIntegerBigEndian() const { return m_comboDataType->inlineModifierChecked(QStringLiteral("integerBigEndian")); }
bool    FindPanel::searchSigned() const { return m_comboDataType->inlineModifierChecked(QStringLiteral("signed")); }

void FindPanel::setSearchTextBigEndian(bool checked)
{
    m_comboDataType->setInlineModifierChecked(QStringLiteral("textBigEndian"), checked);
}

void FindPanel::setSearchIntegerBigEndian(bool checked)
{
    m_comboDataType->setInlineModifierChecked(QStringLiteral("integerBigEndian"), checked);
}

void FindPanel::setSearchSigned(bool checked)
{
    m_comboDataType->setInlineModifierChecked(QStringLiteral("signed"), checked);
}

void FindPanel::refreshDataTypeDisplayText()
{
    QString text = m_comboDataType->selectionText();
    const SearchDataType type = m_comboDataType->selectionData().value<SearchDataType>();
    QStringList modifiers;
    if (searchSigned() && (type == SearchByte || type == SearchWord || type == SearchDword))
        modifiers.append(tr("Signed"));
    if (((type == SearchUTF16 || type == SearchUTF32) && searchTextBigEndian())
            || ((type == SearchWord || type == SearchDword) && searchIntegerBigEndian()))
        modifiers.append(tr("Big endian"));
    if (!modifiers.isEmpty())
        text += tr(" (%1)").arg(modifiers.join(tr(", ")));
    m_comboDataType->setDisplayText(text);
}

QByteArray FindPanel::buildPattern() const
{
    return buildFindPattern(ui->editFind->text(),
                            m_comboDataType->selectionData().value<SearchDataType>(),
                            searchTextBigEndian(),
                            searchIntegerBigEndian(),
                            searchSigned());
}

void FindPanel::triggerSearch(uint flags)
{
    QByteArray pat = buildPattern();
    if (!pat.isEmpty())
        emit searchRequested(pat, flags);
}

void FindPanel::updateSearchHexPreview()
{
    const QString text = ui->editFind->text();
    if (text.isEmpty()) {
        ui->editFind->setStyleSheet({});
        refreshSearchIcon();
        emit searchHexChanged({});
        return;
    }
    const QByteArray pat = buildPattern();
    if (pat.isEmpty()) {
        const QString border = errorColour().name();
        ui->editFind->setStyleSheet(QString(
            "color: %1; border: 1px solid %1; border-radius: 6px; margin: 0; padding: 1px;")
            .arg(border));
        refreshSearchIcon();
        emit searchHexChanged(tr("Invalid search pattern"));
    } else {
        ui->editFind->setStyleSheet({});
        refreshSearchIcon();
        emit searchHexChanged(dumpHex(pat));
    }
}

void FindPanel::hideEvent(QHideEvent *e)
{
    ui->editFind->setStyleSheet({});
    refreshSearchIcon();
    emit searchHexChanged({});
    QWidget::hideEvent(e);
}
