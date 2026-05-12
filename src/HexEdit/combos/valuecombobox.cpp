#include "valuecombobox.h"

#include "theme.h"

#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QMenu>
#include <QStyleOptionComboBox>
#include <QStylePainter>

ValueComboBox::ValueComboBox(QWidget *parent)
    : QComboBox(parent)
{
    setFocusPolicy(Qt::NoFocus);
}

void ValueComboBox::setDisplayText(const QString &text)
{
    if (m_displayText == text)
        return;

    m_displayText = text;

    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    opt.currentText = m_displayText;
    const int w = style()->sizeFromContents(
        QStyle::CT_ComboBox, &opt,
        fontMetrics().size(Qt::TextSingleLine, m_displayText), this).width();
    m_minWidth = qMax(m_minWidth, w);
    updateGeometry();
    update();
}

void ValueComboBox::resetMinWidth()
{
    m_minWidth = 0;
    updateGeometry();
}

bool ValueComboBox::eventFilter(QObject *obj, QEvent *e)
{
    // On Wayland the compositor may reposition popup windows; move() here
    // overrides the compositor's placement with the position we computed.
    if (e->type() == QEvent::Show && m_pendingMenuPos.x() >= 0) {
        QWidget *w = static_cast<QWidget *>(obj);
        const QRect ag = QRect(mapToGlobal(QPoint(0, 0)), size());
        w->move(ag.right() - w->width() + 1 + themedMenuRightAlignOffset(),
                m_pendingMenuPos.y());
        m_pendingMenuPos = { -1, -1 };
    }
    return QComboBox::eventFilter(obj, e);
}

void ValueComboBox::enterEvent(QEnterEvent *e)
{
    m_hovered = true;
    update();
    QComboBox::enterEvent(e);
}

void ValueComboBox::leaveEvent(QEvent *e)
{
    // Don't drop the hover highlight while the popup is open: the mouse
    // leaving the widget to reach the menu should not clear the state.
    if (!m_popupOpen && !property("popupOpen").toBool()) {
        m_hovered = false;
        update();
    }
    QComboBox::leaveEvent(e);
}

void ValueComboBox::popupRight(QMenu *menu)
{
    if (menu->isVisible()) {
        menu->hide();
        return;
    }

    const QPoint curPos = QCursor::pos();
    const bool sameClick = (m_closePos == curPos);
    m_closePos = QPoint(-1, -1);
    if (sameClick)
        return;

    m_popupOpen = true;
    m_hovered = true;
    update();

    connect(menu, &QMenu::aboutToHide, this, [this]() {
        // Let QComboBox clear its internal arrow-sunken state; without this the
        // combo stays in State_Sunken because mouseReleaseEvent is never
        // delivered (the QMenu grabs the mouse and consumes the release).
        QComboBox::hidePopup();
        m_popupOpen = false;
        m_closePos = QCursor::pos();
        m_hovered = underMouse();
        update();
    }, Qt::SingleShotConnection);

    const QPoint pos = smartMenuPos(this, menu, /*rightAlign=*/true);
    // On Wayland the compositor may reposition the popup window; move() in the
    // QEvent::Show handler overrides the compositor's placement.
    if (!m_menuFilterInstalled) {
        menu->installEventFilter(this);
        m_menuFilterInstalled = true;
    }
    m_pendingMenuPos = pos;
    menu->popup(pos);
}

bool ValueComboBox::isSameClickReopen()
{
    const QPoint cur = QCursor::pos();
    const bool same = (m_closePos == cur);
    m_closePos = { -1, -1 };
    return same;
}

void ValueComboBox::recordMenuClose()
{
    m_closePos = QCursor::pos();
}

QSize ValueComboBox::sizeHint() const
{
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    opt.currentText = m_displayText;
    const QSize textSz = fontMetrics().size(Qt::TextSingleLine, m_displayText);
    QSize sz = style()->sizeFromContents(QStyle::CT_ComboBox, &opt, textSz, this);
    sz.setWidth(qMax(sz.width(), m_minWidth));
    return sz;
}

void ValueComboBox::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    opt.currentText = m_displayText;
    // Preserve hover appearance while the popup is open: Qt delivers a Leave
    // event when the popup window appears, which clears State_MouseOver in
    // initStyleOption even though the combo should still look active.
    if (m_hovered || m_popupOpen)
        opt.state |= QStyle::State_MouseOver;
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);
    painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
    // The global stylesheet suppresses the native drop-down arrow. Draw it
    // explicitly, but only while the mouse is over the widget.
    if (m_hovered) {
        QStyleOptionComboBox arrowOpt = opt;
        arrowOpt.rect = style()->subControlRect(
            QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxArrow, this);
        painter.drawPrimitive(QStyle::PE_IndicatorArrowDown, arrowOpt);
    }
}
