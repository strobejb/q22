#include "datatypecombobox.h"
#include "theme.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSet>
#include <QStandardItemModel>
#include <QStyleOptionComboBox>
#include <QStyleOptionMenuItem>
#include <QStylePainter>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>

// Transparent overlay that paints swatch circles on top of the QMenu's own
// rendering.  QStyleSheetStyle owns the entire CE_MenuItem paint pass and
// suppresses QAction icon drawing, so we bypass the style system entirely
// and paint directly in a raised child widget, exactly like MenuShadowOverlay.
namespace {
constexpr bool kToggleModifierWhenSelectingDifferentRow = false;
constexpr bool kRevealInlineModifiersOnlyForSelectedRows = true;

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
                        QAction *rowAction = actionAt(me->pos());
                        if (rowAction
                                && kRevealInlineModifiersOnlyForSelectedRows
                                && m_combo->hasInlineModifiers(rowAction)
                                && modifierLaneAt(me->pos()) == rowAction
                                && rowAction->isCheckable()
                                && !rowAction->isChecked()) {
                            m_pressedSelectAction = rowAction;
                            if (auto *menu = qobject_cast<QMenu *>(parentWidget()))
                                menu->setActiveAction(rowAction);
                            rowAction->setChecked(true);
                            updateModifierHover(me->pos());
                            e->accept();
                            return true;
                        }
                        if (rowAction
                                && kRevealInlineModifiersOnlyForSelectedRows
                                && m_combo->hasInlineModifiers(rowAction)
                                && modifierLaneAt(me->pos()) == rowAction) {
                            m_pressedSelectAction = rowAction;
                            if (auto *menu = qobject_cast<QMenu *>(parentWidget()))
                                menu->setActiveAction(rowAction);
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
                    if (me->button() == Qt::LeftButton && m_combo->consumeOpeningMouseRelease()) {
                        e->accept();
                        return true;
                    }
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
                    if (m_pressedSelectAction) {
                        m_pressedSelectAction = nullptr;
                        e->accept();
                        return true;
                    }
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

    QAction *modifierLaneAt(const QPoint &pos) const
    {
        if (!m_combo)
            return nullptr;
        for (QAction *action : m_combo->visibleInlineModifierActions()) {
            QRect lane;
            for (const auto &entry : m_combo->inlineModifierRects(action)) {
                lane = lane.isNull() ? entry.second : lane.united(entry.second);
            }
            if (lane.isNull())
                continue;
            const QRect row = qobject_cast<QMenu *>(parentWidget())->actionGeometry(action);
            lane.setTop(row.top());
            lane.setBottom(row.bottom());
            lane.setRight(row.right());
            if (lane.contains(pos))
                return action;
        }
        return nullptr;
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
        if (m_hoverCloseAction || m_pressedCloseAction || m_pressedSelectAction || !m_hoverModifierId.isEmpty() || m_pressedModifierAction) {
            m_hoverCloseAction = nullptr;
            m_pressedCloseAction = nullptr;
            m_pressedSelectAction = nullptr;
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
    QAction *m_pressedSelectAction = nullptr;
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

QAction *DataTypeComboBox::createActionForItem(int index, QActionGroup *group, bool checkable)
{
    const int actionIndex = m_actions.size();
    QAction *a = m_menu->addAction(itemText(index));
    a->setData(itemData(index));
    const QModelIndex mi = model()->index(index, modelColumn(), rootModelIndex());
    if (!model()->flags(mi).testFlag(Qt::ItemIsEnabled))
        a->setEnabled(false);

    // If the item has a QColor stored as DecorationRole, attach it as a
    // property so SwatchOverlay can paint a swatch circle for that item.
    const QVariant decoVar = itemData(index, Qt::DecorationRole);
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
        if (group) group->addAction(a);
        connect(a, &QAction::toggled, this, [this, actionIndex](bool checked) {
            if (checked) { m_selection = actionIndex; emit selectionChanged(actionIndex); }
        });
    } else {
        connect(a, &QAction::triggered, this, [this, actionIndex]() {
            m_selection = actionIndex; emit selectionChanged(actionIndex);
        });
    }
    m_actions.append(a);
    return a;
}

void DataTypeComboBox::buildMenu(bool checkable)
{
    // Capture and suppress BEFORE m_menu->clear(): a populate-while-open call
    // (e.g. a filter-enabled combo's backing data refreshing while the user
    // has the dropdown open) clears the menu down to zero actions for a
    // moment, and Qt can auto-close a popup that briefly has none -- if that
    // happens, rebuildFilteredActions()'s own isVisible() check (run after
    // clear()) would wrongly see the menu as never having been open and skip
    // reopening it, leaving the user looking at a combo that silently closed
    // out from under them.
    const bool wasVisible = m_menu->isVisible();
    const QPoint reopenPos = m_menu->pos();
    if (wasVisible) {
        m_suppressCloseHandling = true;
        m_menu->hide();
    }

    m_menu->clear();
    m_actions.clear();
    m_actionModifiers.clear();
    m_modifierState.clear();
    m_modifierLabels.clear();
    m_modifierOrder.clear();
    m_filterEdit = nullptr; // owned by the menu; destroyed by clear() above
    m_filteredSeparators.clear(); // ditto -- already destroyed by clear() above
    m_lastCheckable = checkable;

    if (m_filterEnabled) {
        buildFilterBox();
        rebuildFilteredActions(QString());
    } else {
        QActionGroup *group = checkable ? new QActionGroup(m_menu) : nullptr;
        if (group) group->setExclusive(true);

        for (int i = 0; i < count(); ++i) {
            if (itemText(i).isEmpty()) {
                m_menu->addSeparator();
                continue;
            }
            createActionForItem(i, group, checkable);
        }
    }

    if (wasVisible) {
        m_menu->popup(reopenPos);
        if (m_filterEdit)
            m_filterEdit->setFocus(Qt::PopupFocusReason);
        m_suppressCloseHandling = false;
        armCloseHandler();
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

void DataTypeComboBox::setActionEnabled(const QString &text, bool enabled)
{
    for (QAction *a : m_actions)
        if (a->text() == text) { a->setEnabled(enabled); return; }
}

void DataTypeComboBox::setItemEnabled(int index, bool enabled)
{
    if (auto *m = qobject_cast<QStandardItemModel *>(model()))
        if (auto *it = m->item(index))
            it->setEnabled(enabled);
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

bool DataTypeComboBox::consumeOpeningMouseRelease()
{
    if (!m_ignoreOpeningMouseRelease)
        return false;
    m_ignoreOpeningMouseRelease = false;
    return true;
}

void DataTypeComboBox::updateMenuMinimumWidth()
{
    if (!m_menu)
        return;

    constexpr int kTextLeftPadding = 28;
    constexpr int kTextRightPadding = 28;
    constexpr int kModifierRightInset = 10;
    constexpr int kModifierHPad = 8;
    constexpr int kModifierGap = 6;
    constexpr int kSafetyGap = 14;

    // Measuring every action's text here is O(n) in item count on top of
    // whatever QMenu's own layout already does -- fine for a handful of
    // items, but for a combo populated from an unbounded dataset (hundreds
    // or thousands of entries) this loop alone is what hangs showPopup()
    // solid, confirmed empirically (not QMenu's own sizing, not the themed
    // popup chrome -- this specific scan). Sampling the first N is a
    // reasonable approximation: it only sets a minimum-width floor, so an
    // unsampled wider item still gets its own correct width from QMenu's
    // normal layout regardless.
    constexpr int kMaxMeasuredActions = 300;
    int maxTextWidth = 0;
    int measured = 0;
    for (QAction *action : std::as_const(m_actions)) {
        if (!action || action->isSeparator())
            continue;
        maxTextWidth = qMax(maxTextWidth, fontMetrics().horizontalAdvance(action->text()));
        if (++measured >= kMaxMeasuredActions)
            break;
    }

    int modifierLaneWidth = 0;
    QSet<QString> reservedLabels;
    for (const QString &id : std::as_const(m_modifierOrder)) {
        const QString text = m_modifierLabels.value(id);
        if (text.isEmpty() || reservedLabels.contains(text))
            continue;
        reservedLabels.insert(text);
        if (modifierLaneWidth > 0)
            modifierLaneWidth += kModifierGap;
        modifierLaneWidth += fontMetrics().horizontalAdvance(text) + 2 * kModifierHPad;
    }
    if (modifierLaneWidth > 0)
        modifierLaneWidth += kModifierRightInset;

    QStyleOptionMenuItem opt;
    opt.initFrom(m_menu);
    opt.menuItemType = QStyleOptionMenuItem::Normal;
    opt.text = QStringLiteral("M");
    opt.maxIconWidth = 0;

    QSize content(maxTextWidth + kTextLeftPadding + kTextRightPadding
                  + (modifierLaneWidth > 0 ? modifierLaneWidth + kSafetyGap : 0),
                  fontMetrics().height());
    QSize menuSize = m_menu->style()->sizeFromContents(QStyle::CT_MenuItem, &opt, content, m_menu);

#ifndef Q_OS_WIN
    // Linux QMenus have an 8 px transparent shadow margin on each side.
    // minimumWidth applies to the whole popup widget, so include the margin
    // explicitly to keep the visible item body wide enough for the pill lane.
    menuSize.rwidth() += 16;
#endif

    m_menu->setMinimumWidth(qMax(m_menu->minimumWidth(), menuSize.width()));
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
        return !kRevealInlineModifiersOnlyForSelectedRows || action->isChecked();
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

bool DataTypeComboBox::isPopupOpen() const
{
    return m_menu->isVisible();
}

void DataTypeComboBox::setFilterEnabled(bool enabled)
{
    m_filterEnabled = enabled;
    if (enabled && m_swatchOverlay) {
        // MenuActionOverlay exists for swatch colors, inline modifiers, and
        // action-close buttons -- a filter-enabled combo (built for a large,
        // data-driven action list, not a small fixed set) uses none of
        // those. It's not just dead weight: its eventFilter() repaints the
        // overlay (looping over every action to check for a swatch color)
        // on every single MouseMove inside the menu, and forces
        // setActiveAction(nullptr) on every Leave -- real per-event
        // overhead and an actual interference with the menu's own
        // active-item tracking that's very plausibly part of why scrolling
        // through ~300 actions felt so broken. Disable it outright.
        m_menu->removeEventFilter(m_swatchOverlay);
        m_swatchOverlay->hide();
    }
}

void DataTypeComboBox::buildFilterBox()
{
    // Mirrors bookmarkpopup.cpp's QWidgetAction pattern: a transparent
    // container so the menu's own background shows through, with the height
    // pre-computed and pinned via setMinimumHeight() because QMenu queries a
    // widget action's sizeHint() before it has a real width (width()==0 at
    // that point), which otherwise comes up short.
    auto *container = new QWidget;
    container->setAutoFillBackground(false);
    auto *lay = new QVBoxLayout(container);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(0);

    m_filterEdit = new QLineEdit(container);
    m_filterEdit->setPlaceholderText(tr("Filter..."));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->installEventFilter(this);
    // sizeHint() comes up short here (the global QLineEdit QSS's
    // padding/border doesn't appear to be reflected yet, since the widget
    // is queried before it's ever shown/polished within the menu) -- giving
    // the CONTAINER extra height around it doesn't help, since the line
    // edit's own rect is still too short for its own border+padding+text,
    // it just adds dead padding around a still-clipped field. Compute the
    // line edit's own height directly from its font metrics plus the
    // border+padding the global QSS applies (1px border + 6px padding, top
    // and bottom, in QLineEdit's base rule in theme.cpp) and fix it there
    // instead of trusting sizeHint().
    constexpr int kLineEditVerticalChrome = 2 * (1 + 6); // border + padding, top+bottom
    constexpr int kLineEditHeightSlack = 4;
    m_filterEdit->setFixedHeight(m_filterEdit->fontMetrics().height()
                                  + kLineEditVerticalChrome + kLineEditHeightSlack);
    lay->addWidget(m_filterEdit);

    const auto &cm = lay->contentsMargins();
    container->setMinimumHeight(m_filterEdit->height() + cm.top() + cm.bottom());

    auto *act = new QWidgetAction(m_menu);
    act->setDefaultWidget(container);
    m_menu->addAction(act);
    m_menu->addSeparator();

    connect(m_filterEdit, &QLineEdit::textChanged, this, &DataTypeComboBox::applyFilter);
}

void DataTypeComboBox::applyFilter(const QString &text)
{
    rebuildFilteredActions(text);
}

void DataTypeComboBox::rebuildFilteredActions(const QString &text)
{
    // Always rebuilt from scratch and capped, rather than toggling
    // QAction::setVisible() on whatever's already there: confirmed
    // empirically that toggling visibility on thousands of live actions in
    // an open QMenu hangs for many seconds regardless (presumably an O(item
    // count) internal layout recompute per call), independent of the
    // showPopup() positioning fix above. Rebuilding bounded to a small
    // count avoids the menu ever holding that many actions at once.
    constexpr int kMaxFilteredActions = 300;

    const bool wasVisible = m_menu->isVisible();
    const QPoint reopenPos = m_menu->pos();
    if (wasVisible) {
        // hide() fires aboutToHide, which would otherwise consume and act on
        // showPopup()/popupAbove()'s one-shot "real close" handler for what
        // is actually just an internal cycle -- suppress it for the
        // duration and re-arm a fresh one-shot handler below once the menu
        // is back open.
        m_suppressCloseHandling = true;
        m_menu->hide();
    }

    for (QAction *a : std::as_const(m_actions)) {
        m_menu->removeAction(a);
        a->deleteLater();
    }
    m_actions.clear();
    for (QAction *a : std::as_const(m_filteredSeparators)) {
        m_menu->removeAction(a);
        a->deleteLater();
    }
    m_filteredSeparators.clear();

    QActionGroup *group = m_lastCheckable ? new QActionGroup(m_menu) : nullptr;
    if (group) group->setExclusive(true);

    int shown = 0;
    for (int i = 0; i < count() && shown < kMaxFilteredActions; ++i) {
        const QString itemTxt = itemText(i);
        if (itemTxt.isEmpty()) {
            m_filteredSeparators.append(m_menu->addSeparator());
            continue;
        }
        if (!text.isEmpty() && !itemTxt.contains(text, Qt::CaseInsensitive))
            continue;
        createActionForItem(i, group, m_lastCheckable);
        ++shown;
    }

    if (wasVisible) {
        m_menu->popup(reopenPos);
        // popup() grabs keyboard focus for the menu itself; claim it back
        // for the filter box so subsequent typing keeps landing in the line
        // edit instead of being swallowed as menu navigation.
        if (m_filterEdit)
            m_filterEdit->setFocus(Qt::PopupFocusReason);
        m_suppressCloseHandling = false;
        armCloseHandler(); // re-arm: the previous one-shot connection was consumed by hide() above
    }
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

bool DataTypeComboBox::eventFilter(QObject *obj, QEvent *e)
{
    // The filter box's QLineEdit normally owns key input while it has focus;
    // forward navigation/activation/dismissal keys to the menu itself so
    // arrow keys, Enter, and Escape behave exactly as if the menu had focus,
    // while ordinary character keys still reach the line edit for typing.
    if (obj == m_filterEdit && e->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(e);
        switch (ke->key()) {
        case Qt::Key_Down:
        case Qt::Key_Up:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Escape:
            QCoreApplication::sendEvent(m_menu, ke);
            return true;
        default:
            break;
        }
    }
    return ValueComboBox::eventFilter(obj, e);
}

void DataTypeComboBox::armCloseHandler()
{
    connect(m_menu, &QMenu::aboutToHide, this, [this]() {
        if (m_suppressCloseHandling)
            return;
        recordMenuClose();
        setPopupOpen(false);
        emit popupClosed();
    }, Qt::SingleShotConnection);
}

void DataTypeComboBox::showPopup()
{
    if (m_menu->isVisible()) { m_menu->hide(); return; }
    if (isSameClickReopen()) return;

    updateMenuMinimumWidth();

    armCloseHandler();

    // Fresh search every time the popup opens, rather than carrying over
    // whatever was typed the last time it was shown.
    if (m_filterEdit)
        m_filterEdit->clear();

    // smartMenuPos() needs popup->sizeHint() to position the menu -- but
    // querying sizeHint() on a QMenu BEFORE it's actually shown picks a
    // multi-column layout sized for "infinite" height instead of the
    // single-column-with-scroll-arrows layout QMenu correctly picks once
    // it's actually popped up on a real, bounded screen. This isn't just a
    // huge-list problem: ANY list too tall to fit one column on screen
    // triggers it (confirmed: even just 300 items fills the screen with
    // multiple wide columns instead of staying a normal scrollable single
    // column). A filter-enabled combo's item count is data-driven and
    // routinely exceeds one column, so it always skips the pre-sized smart
    // positioning and lets popup() size itself from a clean slate against
    // the real screen instead -- exactly like when nothing ever queries
    // sizeHint() up front.
    if (m_filterEnabled)
    {
        // SH_Menu_Scrollable now keeps this single-column, but with nothing
        // capping height it still grows to use the entire available screen
        // height -- a reasonable number of visible rows is plenty; the rest
        // is a scroll away.
        constexpr int kFilteredMenuMaxHeight = 420;
        m_menu->setMaximumHeight(kFilteredMenuMaxHeight);

        // Anchor below the combo the same way smartMenuPos() does for every
        // other menu (prefer below, fall back above, clamp to the screen)
        // -- but WITHOUT letting it call popup->sizeHint() itself, which is
        // what triggers the multi-column blowup in the first place. Feed it
        // a known-safe size instead: minimumWidth() only ever measures a
        // capped sample of actions (see updateMenuMinimumWidth()), and the
        // height matches the cap just applied above.
        const QRect anchorGlobal(mapToGlobal(QPoint(0, 0)), size());
        const QSize safeSize(qMax(m_menu->minimumWidth(), width()), kFilteredMenuMaxHeight);
        // Neither setMaximumHeight() nor resize() before popup() changes
        // anything: QMenu::popup()'s own internal "where do I fit" decision
        // for scrollable content pins it to the top of the screen regardless
        // of the position passed in. Let popup() do its own thing first,
        // then relocate the now-already-correctly-sized-and-scrollable
        // window to where it actually belongs.
        //
        // This is a one-time correction on open only, not a standing lock:
        // QMenu implements scrolling for tall content by moving the entire
        // top-level window itself, using its screen position AS the scroll
        // offset rather than scrolling content within a fixed frame --
        // reasserting this position on every subsequent scroll-driven Move
        // was tried and reverted, since it doesn't just look janky, it
        // cancels the scroll itself (confirmed: scrolling stopped working
        // entirely with that in place). The popup will drift back toward
        // the top of the screen as the user scrolls; that's a real
        // constraint of QMenu's native scrollable-menu mechanism, not
        // something fixable without replacing it with a custom popup.
        m_menu->popup(QPoint(0, 0));
        m_menu->move(smartMenuPos(anchorGlobal, m_menu->size(), /*rightAlign=*/false));
    }
    else
    {
        QPoint pos = smartMenuPos(this, m_menu, /*rightAlign=*/false);
#ifndef Q_OS_WIN
        constexpr int kMenuShadowMargin = 8;
        const QRect anchorGlobal(mapToGlobal(QPoint(0, 0)), size());
        if (pos.y() >= anchorGlobal.bottom())
            pos.ry() += kMenuShadowMargin;
        else
            pos.ry() -= kMenuShadowMargin;
        m_ignoreOpeningMouseRelease = true;
#endif
        m_menu->popup(pos);
    }
    setPopupOpen(true);

    // Focus has to be set after popup() actually shows the menu's native
    // window, not before.
    if (m_filterEdit)
        QTimer::singleShot(0, m_filterEdit, [this]() {
            if (m_filterEdit) m_filterEdit->setFocus(Qt::PopupFocusReason);
        });
}

void DataTypeComboBox::popupAbove(const QRect &anchorGlobal)
{
    if (m_menu->isVisible()) {
        m_menu->hide();
        return;
    }

    armCloseHandler();

    updateMenuMinimumWidth();
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
