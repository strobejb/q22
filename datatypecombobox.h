#ifndef DATATYPECOMBOBOX_H
#define DATATYPECOMBOBOX_H

#include "statusbar.h"

class QAction;
class QActionGroup;

// ValueComboBox whose popup menu is built from its QComboBox item model.
// Empty-text items become separators. Call buildMenu() after adding items.
class DataTypeComboBox : public ValueComboBox
{
    Q_OBJECT
public:
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

    void     setLeadingIcon(const QIcon &icon) { m_leadingIcon = icon; update(); }

signals:
    void selectionChanged(int index);

protected:
    QSize sizeHint()        const                   override;
    QSize minimumSizeHint() const                   override;
    void  paintEvent(QPaintEvent *)                 override;
    void  showPopup()                               override;
    bool  eventFilter(QObject *obj, QEvent *e)      override;
    void  setPopupOpen(bool open);

private:
    QMenu          *m_menu              = nullptr;
    QList<QAction*> m_actions;
    QIcon           m_leadingIcon;
    int             m_selection         = 0;
    int             m_targetTextScreenX = 0;  // set in showPopup, used in eventFilter
    int             m_targetMenuY       = 0;  // set in showPopup, used in eventFilter
};

#endif // DATATYPECOMBOBOX_H
