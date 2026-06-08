#ifndef DATATYPECOMBOBOX_H
#define DATATYPECOMBOBOX_H

#include "combos/valuecombobox.h"
#include <QHash>
#include <QList>
#include <QRect>

class QAction;
class QActionGroup;

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

private:
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
};

#endif // DATATYPECOMBOBOX_H
