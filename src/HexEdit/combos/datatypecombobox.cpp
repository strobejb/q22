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
#include <QSet>
#include <QStyleOptionComboBox>
#include <QStylePainter>

// Transparent overlay that paints swatch circles on top of the QMenu's own
// rendering.  QStyleSheetStyle owns the entire CE_MenuItem paint pass and
// suppresses QAction icon drawing, so we bypass the style system entirely
// and paint directly in a raised child widget, exactly like MenuShadowOverlay.
namespace {
constexpr bool kToggleModifierWhenSelectingDifferentRow = false;

QColor blendTowards(const QColor &from, const QColor &to, qreal keepFrom)
{
    const qreal keepTo = 1.0 - keepFrom;
    return QColor(qRound(from.red() * keepFrom + to.red() * keepTo),
                  qRound(from.green() * keepFrom + to.green() * keepTo),
                  qRound(from.blue() * keepFrom + to.blue() * keepTo),
                  qRound(from.alpha() * keepFrom + to.alpha() * keepTo));
}

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
                if (auto *menu = qobject_cast<QMenu *>(parentWidget()))
                    menu->setActiveAction(nullptr);
                clearCloseHover();
            } else if (m_combo) {
                if (e->type() == QEvent::MouseMove) {
                    auto *me = static_cast<QMouseEvent *>(e);
                    updateModifierHover(me->pos());
                    if (m_combo->actionCloseButtonsEnabled())
                        updateCloseHover(me->pos());
                    update();
                } else if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonDblClick) {
                    auto *me = static_cast<QMouseEvent *>(e);
                    if (me->button() == Qt::LeftButton) {
                        const auto hit = modifierAt(me->pos());
                        if (!hit.first.isEmpty()) {
                            if (!m_combo->inlineModifierSelectable(hit.second, hit.first)) {
                                if (auto *menu = qobject_cast<QMenu *>(parentWidget()))
                                    menu->setActiveAction(hit.second);
                                if (hit.second && hit.second->isCheckable() && !hit.second->isChecked())
                                    hit.second->setChecked(true);
                                updateModifierHover(me->pos());
                                e->accept();
                                return true;
                            }
                            m_pressedModifierId = hit.first;
                            m_pressedModifierAction = hit.second;
                            const bool wasChecked = hit.second && hit.second->isChecked();
                            if (hit.second && hit.second->isCheckable() && !hit.second->isChecked())
                                hit.second->setChecked(true);
                            if (wasChecked) {
                                m_combo->setInlineModifierChecked(hit.first, !m_combo->inlineModifierChecked(hit.first));
                            } else if (kToggleModifierWhenSelectingDifferentRow && !m_combo->inlineModifierChecked(hit.first)) {
                                m_combo->setInlineModifierChecked(hit.first, true);
                            }
                            updateModifierHover(me->pos());
                            e->accept();
                            return true;
                        }
                        if (m_combo->actionCloseButtonsEnabled()
                                && actionCloseRect(actionAt(me->pos())).contains(me->pos())) {
                            m_pressedCloseAction = actionAt(me->pos());
                            e->accept();
                            return true;
                        }
                    }
                } else if (e->type() == QEvent::MouseButtonRelease) {
                    auto *me = static_cast<QMouseEvent *>(e);
                    const auto hit = modifierAt(me->pos());
                    if (!hit.first.isEmpty() && !m_combo->inlineModifierSelectable(hit.second, hit.first)) {
                        e->accept();
                        return true;
                    }
                    if (!m_pressedModifierId.isEmpty()) {
                        const QString id = m_pressedModifierId;
                        QAction *action = m_pressedModifierAction;
                        m_pressedModifierId.clear();
                        m_pressedModifierAction = nullptr;
                        if (hit.first == id && hit.second == action) {
                            e->accept();
                            return true;
                        }
                    }

                    QAction *action = actionAt(me->pos());
                    if (m_combo->actionCloseButtonsEnabled()
                            && m_pressedCloseAction && action == m_pressedCloseAction
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

        if (m_combo)
            paintInlineModifiers(&p, menu);

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
    QPair<QString, QAction *> modifierAt(const QPoint &pos) const
    {
        if (!m_combo)
            return {};
        for (QAction *action : m_combo->visibleInlineModifierActions()) {
            for (const auto &entry : m_combo->inlineModifierRects(action)) {
                if (m_combo->inlineModifierVisible(action, entry.first)
                        && entry.second.contains(pos))
                    return { entry.first, action };
            }
        }
        return {};
    }

    void updateModifierHover(const QPoint &pos)
    {
        auto *menu = qobject_cast<QMenu *>(parentWidget());
        if (menu) {
            QAction *action = actionAt(pos);
            if (action && !action->isSeparator())
                menu->setActiveAction(action);
        }

        const auto hit = modifierAt(pos);
        if (menu && !m_combo->actionCloseButtonsEnabled()) {
            if (!hit.first.isEmpty()
                    && hit.second == menu->activeAction()
                    && hit.second->isChecked()
                    && m_combo->inlineModifierSelectable(hit.second, hit.first)) {
                menu->setCursor(Qt::PointingHandCursor);
            } else {
                menu->unsetCursor();
            }
        }
        if (hit.first == m_hoverModifierId && hit.second == m_hoverModifierAction)
            return;
        m_hoverModifierId = hit.first;
        m_hoverModifierAction = hit.second;
        update();
    }

    void paintInlineModifiers(QPainter *p, QMenu *menu)
    {
        const QPalette pal = menu->palette();
        const bool dark = pal.window().color().lightness() < 128;
        const QColor offBorder = dark ? QColor(255, 255, 255, 52) : QColor(0, 0, 0, 45);
        const QColor offText = pal.placeholderText().color().isValid()
            ? pal.placeholderText().color()
            : pal.mid().color();
        const QColor onBg = dark ? QColor(86, 86, 86) : QColor(70, 70, 70);
        const QColor onBorder = onBg.darker(dark ? 85 : 125);
        const QColor onText = QColor(255, 255, 255);
        const QColor highlightedOffText = dark ? QColor(205, 205, 205) : QColor(118, 118, 118);
        const QColor menuBg = pal.window().color();
        QAction *highlightedAction = menu->activeAction();
        if (highlightedAction && highlightedAction->isSeparator())
            highlightedAction = nullptr;

        for (QAction *action : m_combo->visibleInlineModifierActions()) {
            if (!action || action->isSeparator())
                continue;
            for (const auto &entry : m_combo->inlineModifierRects(action)) {
                const QString &id = entry.first;
                if (!m_combo->inlineModifierVisible(action, id))
                    continue;
                QRect r = entry.second;
                const bool selectable = m_combo->inlineModifierSelectable(action, id);
                const bool highlighted = action == highlightedAction;
                const bool checked = action->isChecked();
                const bool on = m_combo->inlineModifierDrawnChecked(action, id, highlighted);
                const bool fullStateRow = highlighted || checked;
                bool drawBorder = true;
                QColor bg = Qt::transparent;
                QColor border = offBorder;
                QColor fg = offText;
                if (!selectable) {
                    border = offBorder;
                    fg = pal.mid().color();
                    if (highlighted)
                        drawBorder = false;
                } else if (fullStateRow) {
                    if (on) {
                        // Highlighted rows need a strong affordance; checked rows
                        // use a quieter mid-dark fill so they stay readable
                        // without competing with the hovered menu item.
                        const QColor checkedOnBg = blendTowards(onBg, pal.mid().color(), 0.55);
                        bg = highlighted ? onBg : checkedOnBg;
                        border = highlighted ? onBorder : checkedOnBg.darker(dark ? 85 : 120);
                        fg = onText;
                    } else {
                        bg = menuBg;
                        fg = highlighted ? highlightedOffText : offText;
                    }
                }

                p->setPen(drawBorder ? QPen(border) : Qt::NoPen);
                p->setBrush(bg);
                p->drawRoundedRect(r.adjusted(0, 0, -1, -1), 5, 5);
                p->setPen(fg);

                p->drawText(r, Qt::AlignCenter, m_combo->inlineModifierText(id, action));
            }
        }
    }

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
        if (m_hoverCloseAction || m_pressedCloseAction || !m_hoverModifierId.isEmpty() || m_pressedModifierAction) {
            m_hoverCloseAction = nullptr;
            m_pressedCloseAction = nullptr;
            m_hoverModifierId.clear();
            m_hoverModifierAction = nullptr;
            m_pressedModifierId.clear();
            m_pressedModifierAction = nullptr;
            update();
        }
    }

    DataTypeComboBox *m_combo = nullptr;
    QAction *m_hoverCloseAction = nullptr;
    QAction *m_pressedCloseAction = nullptr;
    QString m_hoverModifierId;
    QString m_pressedModifierId;
    QAction *m_hoverModifierAction = nullptr;
    QAction *m_pressedModifierAction = nullptr;
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
    m_actionModifiers.clear();
    m_modifierState.clear();
    m_modifierLabels.clear();
    m_modifierOrder.clear();

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

void DataTypeComboBox::setActionInlineModifiers(const QString &text, const QList<InlineModifier> &modifiers)
{
    for (QAction *a : std::as_const(m_actions)) {
        if (a->text() != text)
            continue;
        if (modifiers.isEmpty()) {
            m_actionModifiers.remove(a);
        } else {
            m_actionModifiers.insert(a, modifiers);
            for (const InlineModifier &modifier : modifiers) {
                if (!m_modifierState.contains(modifier.id))
                    m_modifierState.insert(modifier.id, false);
                m_modifierLabels.insert(modifier.id, modifier.text);
                if (!m_modifierOrder.contains(modifier.id))
                    m_modifierOrder.append(modifier.id);
            }
        }
        if (m_swatchOverlay)
            m_swatchOverlay->update();
        return;
    }
}

bool DataTypeComboBox::inlineModifierChecked(const QString &id) const
{
    return m_modifierState.value(id, false);
}

void DataTypeComboBox::setInlineModifierChecked(const QString &id, bool checked)
{
    if (m_modifierState.value(id, false) == checked)
        return;
    m_modifierState.insert(id, checked);
    if (m_swatchOverlay)
        m_swatchOverlay->update();
    update();
    emit inlineModifierToggled(id, checked);
}

QString DataTypeComboBox::inlineModifierText(const QString &id, QAction *action) const
{
    for (const InlineModifier &modifier : m_actionModifiers.value(action))
        if (modifier.id == id)
            return modifier.text;
    return {};
}

bool DataTypeComboBox::hasInlineModifiers(QAction *action) const
{
    return action && m_actionModifiers.contains(action) && !m_actionModifiers.value(action).isEmpty();
}

QAction *DataTypeComboBox::visibleInlineModifierAction() const
{
    const QList<QAction*> actions = visibleInlineModifierActions();
    if (!actions.isEmpty())
        return actions.first();
    return nullptr;
}

QList<QAction*> DataTypeComboBox::visibleInlineModifierActions() const
{
    QList<QAction*> actions;
    if (m_selection >= 0 && m_selection < m_actions.size() && hasInlineModifiers(m_actions[m_selection]))
        actions.append(m_actions[m_selection]);
    if (m_menu && hasInlineModifiers(m_menu->activeAction()) && !actions.contains(m_menu->activeAction()))
        actions.append(m_menu->activeAction());
    return actions;
}

bool DataTypeComboBox::inlineModifierVisible(QAction *action, const QString &id) const
{
    if (!hasInlineModifiers(action))
        return false;

    const bool highlighted = m_menu && action == m_menu->activeAction();
    if (highlighted)
        return true;
    if (action->isChecked())
        return inlineModifierSelectable(action, id);
    return false;
}

bool DataTypeComboBox::inlineModifierDrawnChecked(QAction *action, const QString &id, bool highlighted) const
{
    if (!action)
        return false;
    if (!inlineModifierSelectable(action, id))
        return false;
    if (!action->isChecked() && !highlighted)
        return false;
    return inlineModifierChecked(id);
}

bool DataTypeComboBox::inlineModifierSelectable(QAction *action, const QString &id) const
{
    for (const InlineModifier &modifier : m_actionModifiers.value(action))
        if (modifier.id == id)
            return modifier.selectable;
    return false;
}

QList<QPair<QString, QRect>> DataTypeComboBox::inlineModifierRects(QAction *action) const
{
    QList<QPair<QString, QRect>> rects;
    if (!m_menu || !hasInlineModifiers(action))
        return rects;

    const QRect row = m_menu->actionGeometry(action);
    if (row.isNull())
        return rects;

    constexpr int kRightInset = 10;
    constexpr int kHPad = 8;
    constexpr int kGap = 6;
    const int h = qMin(row.height() - 6, fontMetrics().height() + 6);
    int right = row.right() - kRightInset + 1;

    const QList<InlineModifier> modifiers = m_actionModifiers.value(action);
    QHash<QString, InlineModifier> actionModifierById;
    QHash<QString, InlineModifier> actionModifierByLabel;
    for (const InlineModifier &modifier : modifiers) {
        actionModifierById.insert(modifier.id, modifier);
        actionModifierByLabel.insert(modifier.text, modifier);
    }

    QSet<QString> reservedLabels;
    for (const QString &id : std::as_const(m_modifierOrder)) {
        const QString text = m_modifierLabels.value(id);
        if (reservedLabels.contains(text))
            continue;
        reservedLabels.insert(text);
        const int w = fontMetrics().horizontalAdvance(text) + 2 * kHPad;
        const QRect r(right - w,
                      row.top() + (row.height() - h) / 2,
                      w,
                      h);
        if (actionModifierById.contains(id)) {
            rects.append({ id, r });
        } else if (actionModifierByLabel.contains(text)) {
            rects.append({ actionModifierByLabel.value(text).id, r });
        }
        right = r.left() - kGap;
    }
    return rects;
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
