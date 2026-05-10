#include "finddialog.h"
#include "ui_finddialog.h"
#include "datatypecombobox.h"
#include "theme.h"
#include "HexView/hexview.h"
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QStyle>
#include <QToolButton>
#include <QRegularExpression>
#include <algorithm>
#include <cstring>

// ── Search-pattern helpers (ported from HexEdit/HexFind.c) ───────────────────

// Parse a hex string (spaces/tabs ignored) into bytes.
// Each pair of hex digits forms one byte; an odd trailing nibble is treated as
// the low nibble of a byte (e.g. "F" → 0x0F).  Returns empty on bad input.
static QByteArray hex2Binary(const QString &text)
{
    const QString hex = QString(text).remove(u' ').remove(u'\t');
    if (hex.isEmpty()) return {};
    QByteArray result;
    result.reserve((hex.size() + 1) / 2);
    for (int i = 0; i < hex.size(); i += 2) {
        bool ok;
        uint v = hex.mid(i, qMin(2, hex.size() - i)).toUInt(&ok, 16);
        if (!ok) return {};
        result.append((char)(uint8_t)v);
    }
    return result;
}

// Encode a Unicode string as UTF-8, UTF-16LE, or UTF-32LE.
// bigEndian reverses each code unit for UTF-16/32.
static QByteArray text2Binary(const QString &text, SearchDataType type, bool bigEndian)
{
    switch (type) {
    case SearchUTF8:
        return text.toUtf8();
    case SearchUTF16: {
        QByteArray ba(text.size() * 2, '\0');
        memcpy(ba.data(), text.constData(), text.size() * 2);
        if (bigEndian)
            for (int i = 0; i < ba.size(); i += 2)
                std::reverse(ba.data() + i, ba.data() + i + 2);
        return ba;
    }
    case SearchUTF32: {
        const std::u32string u32 = text.toStdU32String();
        QByteArray ba((int)u32.size() * 4, '\0');
        memcpy(ba.data(), u32.data(), u32.size() * 4);
        if (bigEndian)
            for (int i = 0; i < ba.size(); i += 4)
                std::reverse(ba.data() + i, ba.data() + i + 4);
        return ba;
    }
    default:
        return {};
    }
}

// Parse a comma- or space-separated list of integer or floating-point values
// into a packed byte array.  width is 1, 2, 4, or 8; isInt selects integer vs
// float parsing; bigEndian byte-swaps each element.
static QByteArray num2Binary(const QString &text, int width, bool isInt, bool bigEndian)
{
    if (text.trimmed().isEmpty()) return {};
    const QStringList tokens =
        text.split(QRegularExpression(QString("[,\\s]+")), Qt::SkipEmptyParts);
    QByteArray result;
    result.reserve(tokens.size() * width);
    for (const QString &tok : tokens) {
        char buf[8] = {};
        if (isInt) {
            bool ok;
            quint64 v = tok.toULongLong(&ok, 0);
            if (!ok) return {};
            memcpy(buf, &v, width);
        } else {
            bool ok;
            double d = tok.toDouble(&ok);
            if (!ok) return {};
            if (width == 4) { float f = (float)d; memcpy(buf, &f, 4); }
            else             {                      memcpy(buf, &d, 8); }
        }
        if (bigEndian) std::reverse(buf, buf + width);
        result.append(buf, width);
    }
    return result;
}

// Format a byte array as space-separated uppercase hex pairs ("41 42 43").
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

// ─────────────────────────────────────────────────────────────────────────────

FindDialog::FindDialog(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FindDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_StyledBackground, true);
    ui->verticalLayout->insertWidget(0, new Hairline(this));
    hide();

    refreshStylesheet();

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
    ui->editFind->setTextMargins(kPad + 2, kPad, kPad + 2, kPad);
    ui->editFind->setMinimumHeight(ui->editFind->minimumSizeHint().height() + 2 * kPad);

    // Leading search icon inside the text field.
    {
        const QColor iconCol = QApplication::palette().placeholderText().color();
        const QIcon searchIc = recoloredIcon("edit-find-symbolic", iconCol, 16);
        if (!searchIc.isNull())
            ui->editFind->addAction(searchIc, QLineEdit::LeadingPosition);
    }


    // Replace the plain QComboBox placeholder (items defined in .ui) with a
    // DataTypeComboBox, copying the item model across before swapping.
    m_comboDataType = new DataTypeComboBox(this);
    for (int i = 0; i < ui->comboDataType->count(); ++i)
        m_comboDataType->addItem(ui->comboDataType->itemText(i));
    ui->horizontalLayout->replaceWidget(ui->comboDataType, m_comboDataType);
    m_comboDataType->setFixedWidth(ui->comboDataType->minimumWidth() + 20);
    ui->comboDataType->hide();

    m_comboDataType->buildMenu();
    m_comboDataType->setActionData("Hex",    SearchHex);
    m_comboDataType->setActionData("UTF-8",  SearchUTF8);
    m_comboDataType->setActionData("UTF-16", SearchUTF16);
    m_comboDataType->setActionData("UTF-32", SearchUTF32);
    m_comboDataType->setActionData("Byte",   SearchByte);
    m_comboDataType->setActionData("Word",   SearchWord);
    m_comboDataType->setActionData("Dword",  SearchDword);
    m_comboDataType->setDisplayText(m_comboDataType->selectionText());
    {
        const QColor iconCol = QApplication::palette().placeholderText().color();
        const QIcon typeIc = recoloredIcon("type100-001", iconCol, 16);
        if (!typeIc.isNull())
            m_comboDataType->setLeadingIcon(typeIc);
    }

    // Keep the search field's font in sync with the Type combo so both
    // controls render text at the same size.
    ui->editFind->setFont(m_comboDataType->font());
    connect(m_comboDataType, &DataTypeComboBox::selectionChanged, this, [this](int) {
        m_comboDataType->setDisplayText(m_comboDataType->selectionText());
        // Remember the last text-encoding choice so pane-1 activations restore it.
        const auto dt = m_comboDataType->selectionData().value<SearchDataType>();
        if (dt == SearchUTF8 || dt == SearchUTF16 || dt == SearchUTF32)
            m_lastTextType = dt;
        updateSearchHexPreview();
    });
    connect(ui->editFind, &QLineEdit::textChanged, this, [this] { updateSearchHexPreview(); });

    connect(ui->btnClose, &QToolButton::clicked, this, &QWidget::hide);

    // // Navigation popup menu (Find Previous / Find Next) — kept for reference
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
    // connect(actPrev, &QAction::triggered, this, &FindDialog::findPrevious);
    // connect(actNext, &QAction::triggered, this, &FindDialog::findNext);

    connect(ui->btnFindPrev, &QToolButton::clicked, this, &FindDialog::findPrevious);
    connect(ui->btnFindNext, &QToolButton::clicked, this, &FindDialog::findNext);

    connect(ui->editFind, &QLineEdit::returnPressed, this, [this] { triggerSearch(0); });
    m_comboDataType->installEventFilter(this);

#if 0//def 0//Q_OS_WIN
    // QIcon::fromTheme() returns null on Windows; use Segoe MDL2 / QStyle fallbacks.
    ui->btnNavigate->setIcon(segoeIcon(0xEBE8,
        QApplication::palette().buttonText().color(), 14));
    // No great SP_ for a find-options menu button; show a text indicator instead.
    ui->btnOptions->setIcon(QIcon());
    ui->btnOptions->setText("☰");
    ui->btnNavigate->setIconSize(QSize(16, 16));
    ui->btnClose->setIconSize(QSize(16, 16));
#else
    recolorToolButtons(this);
#endif
}

void FindDialog::refreshStylesheet()
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
            margin: 1px;
            border: 1px solid %3;
            border-radius: 6px;
            padding: 0;
        }
        #editFind:hover {
            margin: 1px;
            border: 1px solid %3;
            border-radius: 6px;
            padding: 0;
        }
        #editFind:focus {
            margin: 0;
            border: 2px solid palette(highlight);
            border-radius: 6px;
            padding: 0;
        }
    )").arg(hover, pressed, borderCol));
}

void FindDialog::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::PaletteChange && !m_inRefresh) {
        m_inRefresh = true;
        refreshStylesheet();
        recolorToolButtons(this);
        m_inRefresh = false;
    }
    QWidget::changeEvent(e);
}

FindDialog::~FindDialog()
{
    delete ui;
}

void FindDialog::activate(const QString &initialText, int pane)
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

bool FindDialog::eventFilter(QObject *o, QEvent *e)
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

QByteArray FindDialog::buildPattern() const
{
    const QString text = ui->editFind->text();
    if (text.isEmpty())
        return {};

    switch (m_comboDataType->selectionData().value<SearchDataType>()) {
    case SearchHex:
        return hex2Binary(text);
    case SearchUTF8:
    case SearchUTF16:
    case SearchUTF32:
        return text2Binary(text, m_comboDataType->selectionData().value<SearchDataType>(), /*bigEndian=*/false);
    case SearchByte:
        return num2Binary(text, 1, /*isInt=*/true, /*bigEndian=*/false);
    case SearchWord:
        return num2Binary(text, 2, /*isInt=*/true, /*bigEndian=*/false);
    case SearchDword:
        return num2Binary(text, 4, /*isInt=*/true, /*bigEndian=*/false);
    }
    return {};
}

void FindDialog::triggerSearch(uint flags)
{
    QByteArray pat = buildPattern();
    if (!pat.isEmpty())
        emit searchRequested(pat, flags);
}

void FindDialog::updateSearchHexPreview()
{
    const QString text = ui->editFind->text();
    if (text.isEmpty()) {
        ui->editFind->setStyleSheet({});
        emit searchHexChanged({});
        return;
    }
    const QByteArray pat = buildPattern();
    if (pat.isEmpty()) {
        const QString border = QApplication::palette().mid().color().name();
        ui->editFind->setStyleSheet(QString(
            "color: #c01c28; border: 1px solid #c01c28; border-radius: 6px; margin: 1px;")
            .arg(border));
        emit searchHexChanged(tr("Invalid search pattern"));
    } else {
        ui->editFind->setStyleSheet({});
        emit searchHexChanged(dumpHex(pat));
    }
}

void FindDialog::hideEvent(QHideEvent *e)
{
    ui->editFind->setStyleSheet({});
    emit searchHexChanged({});
    QWidget::hideEvent(e);
}

