#ifndef DATATYPECOMBOBOX_H
#define DATATYPECOMBOBOX_H

#include "combos/valuecombobox.h"
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

    // Append a pre-built action (e.g. QWidgetAction) directly to the menu and
    // m_actions list.  Call after buildMenu().  The caller is responsible for
    // the action's lifetime; deleting the action removes it from the menu automatically.
    void appendAction(QAction *action);

    QAction *addIconAction(const QIcon &icon, IconActionPosition position = LeadingPosition);
    QAction *addIconAction(const QString &iconName, IconActionPosition position = LeadingPosition);
    void     setLeadingIcon(const QIcon &icon);

signals:
    void selectionChanged(int index);
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
    int             m_selection         = 0;
};

#endif // DATATYPECOMBOBOX_H
