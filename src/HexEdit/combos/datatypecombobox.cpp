#include "datatypecombobox.h"
#include "theme.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QStyleOptionComboBox>
#include <QStylePainter>

// Transparent overlay that paints swatch circles on top of the QMenu's own
// rendering.  QStyleSheetStyle owns the entire CE_MenuItem paint pass and
// suppresses QAction icon drawing, so we bypass the style system entirely
// and paint directly in a raised child widget — exactly like MenuShadowOverlay.
namespace {
struct MenuActionOverlay : public QWidget
{
    explicit MenuActionOverlay(DataTypeComboBox *combo, QMenu *menu)
        : QWidget(menu), m_combo(combo)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setGeometry(menu->rect());
        menu->installEventFilter(this);
    }

    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (obj == parent()) {
            if (e->type() == QEvent::Resize)
                setGeometry(parentWidget()->rect());
            if (e->type() == QEvent::Show)
                update();
            if (e->type() == QEvent::Leave || e->type() == QEvent::Hide) {
                clearCloseHover();
            } else if (m_combo && m_combo->actionCloseButtonsEnabled()) {
                if (e->type() == QEvent::MouseMove) {
                    auto *me = static_cast<QMouseEvent *>(e);
                    updateCloseHover(me->pos());
                    update();
                } else if (e->type() == QEvent::MouseButtonPress) {
                    auto *me = static_cast<QMouseEvent *>(e);
                    if (me->button() == Qt::LeftButton && actionCloseRect(actionAt(me->pos())).contains(me->pos())) {
                        m_pressedCloseAction = actionAt(me->pos());
                        e->accept();
                        return true;
                    }
                } else if (e->type() == QEvent::MouseButtonRelease) {
                    auto *me = static_cast<QMouseEvent *>(e);
                    QAction *action = actionAt(me->pos());
                    if (m_pressedCloseAction && action == m_pressedCloseAction
                            && actionCloseRect(action).contains(me->pos())) {
                        QAction *pressed = m_pressedCloseAction;
                        m_pressedCloseAction = nullptr;
                        const int idx = actionIndex(pressed);
                        if (idx >= 0)
                            emit m_combo->actionCloseRequested(idx, pressed->data());
                        e->accept();
                        return true;
                    }
                    m_pressedCloseAction = nullptr;
                }
            }
        }
        return false;
    }

    void paintEvent(QPaintEvent *) override
    {
        auto *menu = qobject_cast<QMenu *>(parentWidget());
        if (!menu) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        for (QAction *a : menu->actions()) {
            const QVariant sc = a->property("swatchColor");
            if (!sc.isValid()) continue;
            const QColor col = sc.value<QColor>();
            const QRect r = menu->actionGeometry(a);
            if (r.isNull()) continue;
            constexpr int kSz = 15;
            const int x = r.left() + 10;
            const int y = r.top() + (r.height() - kSz) / 2;
            p.setBrush(col);
            p.setPen(Qt::NoPen);
            p.drawEllipse(x, y, kSz, kSz);
        }

        if (!m_combo || !m_combo->actionCloseButtonsEnabled())
            return;

        QAction *active = m_hoverCloseAction ? m_hoverCloseAction : menu->activeAction();
        if (!active || active->isSeparator())
            return;

        const QRect iconRect = actionCloseIconRect(active);
        if (iconRect.isNull())
            return;

        const QColor iconColor = menuSelectedTextColor(menu->palette());
        recoloredIcon(QStringLiteral("actions/window-close-symbolic"), iconColor, iconRect.width())
            .paint(&p, iconRect);
    }

private:
    QAction *actionAt(const QPoint &pos) const
    {
        auto *menu = qobject_cast<QMenu *>(parentWidget());
        if (!menu)
            return nullptr;
        for (QAction *action : menu->actions()) {
            if (!action || action->isSeparator())
                continue;
            if (menu->actionGeometry(action).contains(pos))
                return action;
        }
        return nullptr;
    }

    int actionIndex(QAction *needle) const
    {
        auto *menu = qobject_cast<QMenu *>(parentWidget());
        if (!menu || !needle)
            return -1;
        int idx = 0;
        for (QAction *action : menu->actions()) {
            if (!action || action->isSeparator())
                continue;
            if (action == needle)
                return idx;
            ++idx;
        }
        return -1;
    }

    QRect actionCloseIconRect(QAction *action) const
    {
        auto *menu = qobject_cast<QMenu *>(parentWidget());
        if (!menu || !action || action->isSeparator())
            return {};
        const QRect r = menu->actionGeometry(action);
        if (r.isNull())
            return {};
        constexpr int kIconSize = 14;
        constexpr int kRightInset = 10;
        return QRect(r.right() - kRightInset - kIconSize + 1,
                     r.top() + (r.height() - kIconSize) / 2,
                     kIconSize, kIconSize);
    }

    QRect actionCloseRect(QAction *action) const
    {
        const QRect iconRect = actionCloseIconRect(action);
        if (iconRect.isNull())
            return {};
        return iconRect.adjusted(-4, -4, 4, 4);
    }

    void updateCloseHover(const QPoint &pos)
    {
        auto *menu = qobject_cast<QMenu *>(parentWidget());
        if (!menu)
            return;

        QAction *action = actionAt(pos);
        if (action && !actionCloseRect(action).contains(pos))
            action = nullptr;

        if (action) {
            menu->setActiveAction(action);
            menu->setCursor(Qt::PointingHandCursor);
        } else {
            menu->unsetCursor();
        }

        if (action != m_hoverCloseAction) {
            m_hoverCloseAction = action;
            update();
        }
    }

    void clearCloseHover()
    {
        auto *menu = qobject_cast<QMenu *>(parentWidget());
        if (menu)
            menu->unsetCursor();
        if (m_hoverCloseAction || m_pressedCloseAction) {
            m_hoverCloseAction = nullptr;
            m_pressedCloseAction = nullptr;
            update();
        }
    }

    DataTypeComboBox *m_combo = nullptr;
    QAction *m_hoverCloseAction = nullptr;
    QAction *m_pressedCloseAction = nullptr;
};
} // namespace

DataTypeComboBox::DataTypeComboBox(QWidget *parent)
    : ValueComboBox(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    m_menu = new QMenu(this);
    themeMenu(m_menu);
    m_swatchOverlay = new MenuActionOverlay(this, m_menu);
}

QAction *DataTypeComboBox::addIconAction(const QIcon &icon, IconActionPosition position)
{
    if (position != LeadingPosition)
        return nullptr;

    if (!m_leadingAction) {
        m_leadingAction = new QAction(this);
        connect(m_leadingAction, &QAction::changed, this, [this]() { update(); });
    }
    m_leadingAction->setIcon(icon);
    update();
    return m_leadingAction;
}

QAction *DataTypeComboBox::addIconAction(const QString &iconName, IconActionPosition position)
{
    QIcon icon(QStringLiteral(":/icons/actions/") + iconName + QStringLiteral(".svg"));
    if (icon.isNull())
        icon = QIcon::fromTheme(iconName);

    QAction *action = addIconAction(icon, position);
    if (action) {
        action->setProperty("iconThemeName", iconName);
        action->setProperty("iconColorRole", QStringLiteral("placeholderText"));
        action->setProperty("iconSize", 16);
    }
    return action;
}

void DataTypeComboBox::setLeadingIcon(const QIcon &icon)
{
    addIconAction(icon, LeadingPosition);
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
        // If the item has a QColor stored as DecorationRole, attach it as a
        // property so SwatchOverlay can paint a swatch circle for that item.
        const QVariant decoVar = itemData(i, Qt::DecorationRole);
        if (decoVar.canConvert<QColor>()) {
            const QColor c = decoVar.value<QColor>();
            if (c.isValid())
                a->setProperty("swatchColor", c);
        } else {
            const QIcon itemIcon = qvariant_cast<QIcon>(decoVar);
            if (!itemIcon.isNull())
                a->setIcon(itemIcon);
        }
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

void DataTypeComboBox::setActionCloseButtonsEnabled(bool enabled)
{
    if (m_actionCloseButtonsEnabled == enabled)
        return;
    m_actionCloseButtonsEnabled = enabled;
    if (m_swatchOverlay)
        m_swatchOverlay->update();
}

void DataTypeComboBox::appendAction(QAction *action)
{
    const int idx = m_actions.size();
    m_menu->addAction(action);
    connect(action, &QAction::triggered, this, [this, idx]() {
        m_selection = idx;
        emit selectionChanged(idx);
    });
    m_actions.append(action);
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

QSize DataTypeComboBox::sizeHint() const
{
    return ValueComboBox::sizeHint();
}

QSize DataTypeComboBox::minimumSizeHint() const
{
    return ValueComboBox::minimumSizeHint();
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
    } else {
        opt.state &= ~(QStyle::State_On | QStyle::State_Sunken);
    }
    // Draw the frame (including the drop-down arrow via QComboBox::down-arrow
    // in the global stylesheet) at full widget size.
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);
    // SC_ComboBoxEditField already has the stylesheet's padding: 3px 8px applied,
    // so use the rect directly — no additional inset needed.
    QRect textRect = style()->subControlRect(
                         QStyle::CC_ComboBox, &opt,
                         QStyle::SC_ComboBoxEditField, this);
    if (m_leadingAction && !m_leadingAction->icon().isNull()) {
        const int iconSz = fontMetrics().height();
        const QRect iconRect(textRect.left(),
                             textRect.top() + (textRect.height() - iconSz) / 2,
                             iconSz, iconSz);
        m_leadingAction->icon().paint(&painter, iconRect);
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

void DataTypeComboBox::keyPressEvent(QKeyEvent *e)
{
    if (m_menu->isVisible() || m_actions.isEmpty()) {
        ValueComboBox::keyPressEvent(e);
        return;
    }
    if (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) {
        const int n = m_actions.size();
        const int next = qBound(0, m_selection + (e->key() == Qt::Key_Down ? 1 : -1), n - 1);
        if (next != m_selection) {
            if (m_actions[next]->isCheckable()) {
                m_actions[next]->setChecked(true);   // QActionGroup: unchecks old, fires toggled → m_selection + selectionChanged
                setDisplayText(selectionText());
            } else {
                m_selection = next;
                emit selectionChanged(m_selection);
            }
        }
        e->accept();
        return;
    }
    ValueComboBox::keyPressEvent(e);
}

void DataTypeComboBox::setPopupOpen(bool open)
{
    if (!open)
        QComboBox::hidePopup();   // clears Qt's internal State_Sunken arrow flag
    setProperty("popupOpen", open);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void DataTypeComboBox::showPopup()
{
    if (m_menu->isVisible()) { m_menu->hide(); return; }
    if (isSameClickReopen()) return;

    connect(m_menu, &QMenu::aboutToHide, this,
            [this]() { recordMenuClose(); setPopupOpen(false); },
            Qt::SingleShotConnection);

    const QPoint pos = smartMenuPos(this, m_menu, /*rightAlign=*/false);
    m_menu->popup(pos);
    setPopupOpen(true);
}

void DataTypeComboBox::popupAbove(const QRect &anchorGlobal)
{
    if (m_menu->isVisible()) {
        m_menu->hide();
        return;
    }

    connect(m_menu, &QMenu::aboutToHide, this,
            [this]() { recordMenuClose(); setPopupOpen(false); emit popupClosed(); },
            Qt::SingleShotConnection);

    const QSize popupSize = m_menu->sizeHint();
    QScreen *screen = QGuiApplication::screenAt(anchorGlobal.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect();

    constexpr int kGap = 6;
#ifdef Q_OS_WIN
    constexpr int kBorderOverlap = 2;
#else
    constexpr int kMenuShadowMargin = 8;
    constexpr int kBorderOverlap = 2 * kMenuShadowMargin + 2;
#endif
    int x = anchorGlobal.left();
    int y = anchorGlobal.top() - popupSize.height() + kBorderOverlap;
    if (avail.isValid()) {
        x = qBound(avail.left(), x, avail.right() - popupSize.width() + 1);
        if (y < avail.top())
            y = qMin(avail.bottom() - popupSize.height() + 1, anchorGlobal.bottom() + kGap);
    }

    m_menu->popup({x, y});
    setPopupOpen(true);
}
