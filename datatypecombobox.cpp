#include "datatypecombobox.h"
#include "theme.h"
#include <QActionGroup>
#include <QApplication>
#include <QEvent>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QStyleOptionMenuItem>

DataTypeComboBox::DataTypeComboBox(QWidget *parent)
    : ValueComboBox(parent)
{
    setFocusPolicy(Qt::ClickFocus);
    m_menu = new QMenu(this);
    themeMenu(m_menu);
    m_menu->installEventFilter(this);
}

void DataTypeComboBox::buildMenu(bool checkable)
{
    m_menu->clear();
    m_actions.clear();

    QActionGroup *group = checkable ? new QActionGroup(m_menu) : nullptr;
    if (group) group->setExclusive(true);

    for (int i = 0; i < count(); ++i) {
        const QString text = itemText(i);
        if (text.isEmpty()) {
            m_menu->addSeparator();
            continue;
        }
        const int actionIndex = m_actions.size();
        QAction *a = m_menu->addAction(text);
        if (checkable) {
            a->setCheckable(true);
            a->setChecked(actionIndex == m_selection);
            group->addAction(a);
            connect(a, &QAction::toggled, this, [this, actionIndex](bool checked) {
                if (checked) { m_selection = actionIndex; emit selectionChanged(actionIndex); }
            });
        } else {
            connect(a, &QAction::triggered, this, [this, actionIndex]() {
                m_selection = actionIndex; emit selectionChanged(actionIndex);
            });
        }
        m_actions.append(a);
    }
}

QString DataTypeComboBox::selectionText() const
{
    if (m_selection >= 0 && m_selection < m_actions.size())
        return m_actions[m_selection]->text();
    return {};
}

void DataTypeComboBox::setActionData(const QString &text, const QVariant &data)
{
    for (QAction *a : m_actions)
        if (a->text() == text) { a->setData(data); return; }
}

QVariant DataTypeComboBox::selectionData() const
{
    if (m_selection >= 0 && m_selection < m_actions.size())
        return m_actions[m_selection]->data();
    return {};
}

void DataTypeComboBox::selectByData(const QVariant &data)
{
    for (QAction *a : m_actions) {
        if (a->data() == data) {
            a->setChecked(true);  // emits toggled → updates m_selection + selectionChanged
            return;
        }
    }
}

// Render a menu item twice — once with text, once without — and return the x
// of the first pixel column where they differ. This is the exact text start
// position regardless of style, theme, or DPI.
static int measureMenuItemTextX(QMenu *menu, QAction *action)
{
    const QRect item = menu->actionGeometry(action);
    if (!item.isValid()) return 0;

    const int W = item.width(), H = item.height();

    auto render = [&](const QString &text) {
        QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        QStyleOptionMenuItem o;
        o.initFrom(menu);
        o.rect               = img.rect();
        o.text               = text;
        o.menuItemType       = QStyleOptionMenuItem::Normal;
        o.checkType          = QStyleOptionMenuItem::Exclusive;
        o.menuHasCheckableItems = true;
        o.checked            = false;
        o.maxIconWidth       = 0;
        o.state              = QStyle::State_Enabled;
        menu->style()->drawControl(QStyle::CE_MenuItem, &o, &p, menu);
        return img;
    };

    const QImage withText = render("X");
    const QImage noText   = render("");

    const int midY = H / 2;
    for (int x = 0; x < W; ++x) {
        const QRgb a = withText.pixel(x, midY);
        const QRgb b = noText.pixel(x, midY);
        if (qAbs(int(qRed(a))   - int(qRed(b)))   > 8 ||
            qAbs(int(qGreen(a)) - int(qGreen(b))) > 8 ||
            qAbs(int(qBlue(a))  - int(qBlue(b)))  > 8 ||
            qAbs(int(qAlpha(a)) - int(qAlpha(b))) > 8) {
            return item.left() + x;
        }
    }
    return 0;
}

// Scale padding with screen DPI so the combo height stays visually consistent.
static int kPad() { return qMax(1, qRound(qApp->devicePixelRatio() * 2.0)); }

QSize DataTypeComboBox::sizeHint() const
{
    QSize s = ValueComboBox::sizeHint();
    return { s.width(), s.height() + 2 * kPad() };
}

QSize DataTypeComboBox::minimumSizeHint() const
{
    QSize s = ValueComboBox::minimumSizeHint();
    return { s.width(), s.height() + 2 * kPad() };
}

void DataTypeComboBox::paintEvent(QPaintEvent *)
{
    const QString text = displayText().isEmpty() ? selectionText() : displayText();
    QStylePainter painter(this);
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    opt.currentText = text;
    // When the QMenu popup is open, set State_On so that:
    //  1. NoFocusRectStyle's sync code sees State_On matching popupOpen=true
    //     and does not queue a reset.
    //  2. Fusion treats the combo as "open/pressed" and draws the darker sunken
    //     gradient rather than the lighter hover gradient.
    // Clear State_MouseOver so Fusion picks the sunken path, not the hover path.
    if (property("popupOpen").toBool()) {
        opt.state |= QStyle::State_On;
        opt.state &= ~QStyle::State_MouseOver;
    }
    // Draw the frame (including the drop-down arrow via QComboBox::down-arrow
    // in the global stylesheet) at full widget size.
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);
    // SC_ComboBoxEditField already has the stylesheet's padding: 3px 8px applied,
    // so use the rect directly — no additional inset needed.
    QRect textRect = style()->subControlRect(
                         QStyle::CC_ComboBox, &opt,
                         QStyle::SC_ComboBoxEditField, this);
    if (!m_leadingIcon.isNull()) {
        const int iconSz = fontMetrics().height();
        const QRect iconRect(textRect.left(),
                             textRect.top() + (textRect.height() - iconSz) / 2,
                             iconSz, iconSz);
        m_leadingIcon.paint(&painter, iconRect);
        textRect.setLeft(iconRect.right() + 8);
    }
    style()->drawItemText(&painter, textRect,
                          Qt::AlignLeft | Qt::AlignVCenter,
                          opt.palette, isEnabled(),
                          text, QPalette::ButtonText);
    // The ::drop-down stylesheet rule suppresses the native arrow; draw it explicitly.
    QStyleOptionComboBox arrowOpt = opt;
    arrowOpt.rect = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxArrow, this);
    painter.drawPrimitive(QStyle::PE_IndicatorArrowDown, arrowOpt);
}

void DataTypeComboBox::setPopupOpen(bool open)
{
    setProperty("popupOpen", open);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void DataTypeComboBox::showPopup()
{
    if (m_menu->isVisible()) { m_menu->hide(); return; }
    if (isSameClickReopen()) return;

    QStyleOptionComboBox cbOpt;
    initStyleOption(&cbOpt);
    int comboTextX = style()->subControlRect(QStyle::CC_ComboBox, &cbOpt,
                                             QStyle::SC_ComboBoxEditField, this).left();
    m_targetTextScreenX = mapToGlobal(QPoint(comboTextX, 0)).x();

    connect(m_menu, &QMenu::aboutToHide, this,
            [this]() { recordMenuClose(); setPopupOpen(false); },
            Qt::SingleShotConnection);

    const QPoint pos = smartMenuPos(this, m_menu, /*rightAlign=*/false);
    m_targetMenuY = pos.y();
    m_menu->popup(pos);
    setPopupOpen(true);
}

bool DataTypeComboBox::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == m_menu && e->type() == QEvent::Show && !m_actions.isEmpty()) {
        const int menuTextX = measureMenuItemTextX(m_menu, m_actions.first());
        if (menuTextX > 0)
            m_menu->move(m_targetTextScreenX - menuTextX, m_targetMenuY);
    }
    return ValueComboBox::eventFilter(obj, e);
}
