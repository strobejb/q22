#include "menucombobox.h"
#include "theme.h"
#include <QApplication>
#include <QCursor>
#include <QStyleOptionComboBox>
#include <QStylePainter>

static int kPad() { return qMax(1, qRound(qApp->devicePixelRatio() * 2.0)); }

MenuComboBox::MenuComboBox(QWidget *parent)
    : QComboBox(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    m_menu = new QMenu(this);
    themeMenu(m_menu);

    connect(m_menu, &QMenu::aboutToHide, this,
            [this]() { recordMenuClose(); setPopupOpen(false); });
}

void MenuComboBox::setPopupOpen(bool open)
{
    if (!open)
        QComboBox::hidePopup();   // clears Qt's internal State_Sunken arrow flag
    setProperty("popupOpen", open);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

QSize MenuComboBox::sizeHint() const
{
    QSize s = QComboBox::sizeHint();
    return { s.width(), s.height() + 2 * kPad() };
}

QSize MenuComboBox::minimumSizeHint() const
{
    QSize s = QComboBox::minimumSizeHint();
    return { s.width(), s.height() + 2 * kPad() };
}

void MenuComboBox::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    // Inject State_On when the menu is open so that:
    //  1. NoFocusRectStyle's sync code sees State_On matching popupOpen=true
    //     and does not queue a reset.
    //  2. Fusion treats the combo as open/pressed for the darker gradient.
    // Clear State_MouseOver so Fusion picks the sunken path, not the hover path.
    if (property("popupOpen").toBool()) {
        opt.state |= QStyle::State_On;
        opt.state &= ~QStyle::State_MouseOver;
    } else {
        opt.state &= ~(QStyle::State_On | QStyle::State_Sunken);
    }
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);
    painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
    // The ::drop-down stylesheet rule suppresses the native arrow; draw it explicitly.
    QStyleOptionComboBox arrowOpt = opt;
    arrowOpt.rect = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxArrow, this);
    painter.drawPrimitive(QStyle::PE_IndicatorArrowDown, arrowOpt);
}

void MenuComboBox::buildMenu()
{
    m_menu->clear();
    const int cur = currentIndex();
    for (int i = 0; i < count(); ++i) {
        const QString text = itemText(i);
        if (text.isEmpty()) {
            m_menu->addSeparator();
            continue;
        }
        QAction *a = m_menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(i == cur);
        connect(a, &QAction::triggered, this, [this, i]() {
            setCurrentIndex(i);
        });
    }
}

bool MenuComboBox::isSameClickReopen()
{
    const QPoint cur = QCursor::pos();
    const bool same = (m_closePos == cur);
    m_closePos = { -1, -1 };
    return same;
}

void MenuComboBox::showPopup()
{
    if (m_menu->isVisible()) { m_menu->hide(); return; }
    if (isSameClickReopen()) return;

    buildMenu();
    const QPoint pos = smartMenuPos(this, m_menu, /*rightAlign=*/false);
    m_menu->popup(pos);
    setPopupOpen(true);
}
