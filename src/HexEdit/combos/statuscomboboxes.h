#ifndef STATUSCOMBOBOXES_H
#define STATUSCOMBOBOXES_H

#include "combos/valuecombobox.h"

#include <QAction>
#include <QList>
#include <QStringList>

class QMenu;

// ValueComboBox whose dropdown is a QMenu with mutually-exclusive checkable
// items (radio behaviour via QActionGroup).
class RadioComboBox : public ValueComboBox
{
    Q_OBJECT
public:
    explicit RadioComboBox(const QStringList &items, QWidget *parent = nullptr);
    int  selection() const { return m_selection; }
    void setSelection(int index);

signals:
    void selectionChanged(int index);

protected:
    void showPopup() override;

private:
    QMenu          *m_menu      = nullptr;
    QList<QAction*> m_actions;
    int             m_selection = 0;
    bool            m_updating  = false;
};

// ValueComboBox whose dropdown is a QMenu with checkable format toggles and an
// exclusive data-size selector.
class ValueOptionsComboBox : public ValueComboBox
{
    Q_OBJECT
public:
    explicit ValueOptionsComboBox(QWidget *parent = nullptr);

    bool isSigned()    const { return m_signed; }
    bool isBigEndian() const { return m_bigEndian; }
    bool isHex()       const { return m_hex; }
    int  dataSize()    const { return m_dataSize; }

signals:
    void optionsChanged();

protected:
    void showPopup() override;

private:
    QMenu   *m_menu           = nullptr;
    QAction *m_actSigned      = nullptr;
    QAction *m_actBigEndian   = nullptr;
    QAction *m_actHex         = nullptr;
    QAction *m_sizeActions[6] = {};

    bool m_signed    = false;
    bool m_bigEndian = false;
    bool m_hex       = false;
    int  m_dataSize  = 0;
};

#endif // STATUSCOMBOBOXES_H
