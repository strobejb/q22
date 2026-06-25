#ifndef DATATYPECOMBOBOX_H
#define DATATYPECOMBOBOX_H

#include "combos/valuecombobox.h"
#include <QHash>
#include <QList>
#include <QRect>

class QAction;
class QActionGroup;
class QLineEdit;

// ValueComboBox whose popup menu is built from its QComboBox item model.
// Empty-text items become separators. Call buildMenu() after adding items.
class DataTypeComboBox : public ValueComboBox
{
    Q_OBJECT
public:
    enum IconActionPosition {
        LeadingPosition
    };

    struct InlineModifier {
        QString id;
        QString text;
        bool selectable = true;
    };

    explicit DataTypeComboBox(QWidget *parent = nullptr);
    void     buildMenu(bool checkable = true);
    int      selection()     const { return m_selection; }
    QString  selectionText() const;

    // Associate an arbitrary value with the action whose display text matches
    // |text|.  Call after buildMenu().  Order-independent: renaming or reordering
    // items in the .ui file only requires updating these call sites, not consumers.
    void     setActionData(const QString &text, const QVariant &data);
    // Enable/disable the action whose display text matches |text| (e.g. a
    // permanently-present item that's only selectable once some prerequisite
    // is known). Call after buildMenu(), same as setActionData().
    void     setActionEnabled(const QString &text, bool enabled);
    QVariant selectionData() const;
    // Programmatically select the action whose data matches |data|.
    // Emits selectionChanged if the selection changes.
    void     selectByData(const QVariant &data);
    void     popupAbove(const QRect &anchorGlobal);
    void     setActionCloseButtonsEnabled(bool enabled);
    bool     actionCloseButtonsEnabled() const { return m_actionCloseButtonsEnabled; }

    // Append a pre-built action (e.g. QWidgetAction) directly to the menu and
    // m_actions list.  Call after buildMenu().  The caller is responsible for
    // the action's lifetime; deleting the action removes it from the menu automatically.
    void appendAction(QAction *action);

    // Adds a search box above the item list that narrows the menu to
    // matching items as the user types -- for combos populated from an
    // unbounded dataset where scrolling through every item isn't practical.
    // Call once after construction; the box is (re)created by every later
    // buildMenu() call, so it survives the menu being rebuilt from new
    // items. Matching items are read from the combo's underlying item model
    // (addItem()/itemText()/itemData()), NOT from m_actions: an open QMenu's
    // internal layout becomes pathologically slow to mutate once it holds
    // thousands of actions (confirmed empirically -- toggling individual
    // QAction::setVisible() calls on that many live actions hangs for many
    // seconds), so the actual QAction set is always rebuilt from scratch and
    // capped to a small number of matches, never left to grow with the
    // dataset.
    void     setFilterEnabled(bool enabled);
    // Like QComboBox's missing setItemEnabled(): disables the underlying
    // model item at |index| (relevant once a filter-enabled combo no longer
    // keeps a stable text-addressable QAction per item).
    void     setItemEnabled(int index, bool enabled);

    QAction *addIconAction(const QIcon &icon, IconActionPosition position = LeadingPosition);
    QAction *addIconAction(const QString &iconName, IconActionPosition position = LeadingPosition);
    void     setLeadingIcon(const QIcon &icon);
    void     setActionInlineModifiers(const QString &text, const QList<InlineModifier> &modifiers);
    bool     inlineModifierChecked(const QString &id) const;
    void     setInlineModifierChecked(const QString &id, bool checked);
    QString  inlineModifierText(const QString &id, QAction *action) const;
    QList<QPair<QString, QRect>> inlineModifierRects(QAction *action) const;
    QAction *visibleInlineModifierAction() const;
    QList<QAction*> visibleInlineModifierActions() const;
    bool     inlineModifierVisible(QAction *action, const QString &id) const;
    bool     inlineModifierDrawnChecked(QAction *action, const QString &id, bool highlighted) const;
    bool     inlineModifierSelectable(QAction *action, const QString &id) const;
    bool     hasInlineModifiers(QAction *action) const;
    bool     consumeOpeningMouseRelease();

signals:
    void selectionChanged(int index);
    void actionCloseRequested(int actionIndex, QVariant data);
    void inlineModifierToggled(const QString &id, bool checked);
    void popupClosed();

protected:
    QSize sizeHint()        const                   override;
    QSize minimumSizeHint() const                   override;
    void  paintEvent(QPaintEvent *)                 override;
    void  showPopup()                               override;
    void  keyPressEvent(QKeyEvent *e)               override;
    void  setPopupOpen(bool open);
    bool  eventFilter(QObject *obj, QEvent *e)      override;

private:
    void updateMenuMinimumWidth();
    void buildFilterBox();
    void applyFilter(const QString &text);
    void rebuildFilteredActions(const QString &text);
    QAction *createActionForItem(int index, QActionGroup *group, bool checkable);
    // (Re-)arms the one-shot "real close" handler that showPopup()/
    // popupAbove() each used to set up inline -- factored out so
    // rebuildFilteredActions() can re-arm it after its own internal
    // hide()+popup() cycle consumes the existing one-shot connection.
    void armCloseHandler();

    QMenu          *m_menu              = nullptr;
    QWidget        *m_swatchOverlay     = nullptr;
    QList<QAction*> m_actions;
    QAction        *m_leadingAction     = nullptr;
    QHash<QAction*, QList<InlineModifier>> m_actionModifiers;
    QHash<QString, bool> m_modifierState;
    QHash<QString, QString> m_modifierLabels;
    QList<QString> m_modifierOrder;
    int             m_selection         = 0;
    bool            m_actionCloseButtonsEnabled = false;
    bool            m_ignoreOpeningMouseRelease = false;
    bool            m_filterEnabled     = false;
    bool            m_lastCheckable     = true;
    QLineEdit      *m_filterEdit        = nullptr;
    // Separators rebuildFilteredActions() adds between items (e.g. between
    // "Entrypoint" and the matching list) -- tracked separately from
    // m_actions (which is index-addressed for selection/navigation and must
    // only ever contain real, selectable actions) so each rebuild can remove
    // its own previous separators; otherwise they'd never get cleaned up and
    // would pile up across rebuilds, since they aren't in m_actions.
    QList<QAction*> m_filteredSeparators;
    // True while rebuildFilteredActions() briefly hides+reshows the menu to
    // make mutating its action list cheap (see rebuildFilteredActions()) --
    // suppresses the showPopup()/popupAbove() aboutToHide handling, which is
    // a Qt::SingleShotConnection meant to fire once on a real close, not on
    // this internal hide/reshow cycle.
    bool            m_suppressCloseHandling = false;
    // Whether the currently-armed close handler should emit popupClosed()
    // (popupAbove()'s behavior) -- remembered so rebuildFilteredActions()
    // can re-arm the same flavor of handler after its internal hide/reshow.
    bool            m_emitPopupClosedOnHide = false;
};

#endif // DATATYPECOMBOBOX_H
