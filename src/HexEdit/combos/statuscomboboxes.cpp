#include "statuscomboboxes.h"

#include "theme.h"

#include <QActionGroup>
#include <QMenu>

RadioComboBox::RadioComboBox(const QStringList &items, QWidget *parent)
    : ValueComboBox(parent)
{
    m_menu = new QMenu(this);
    themeMenu(m_menu);
    QActionGroup *group = new QActionGroup(this);
    group->setExclusive(true);

    for (int i = 0; i < items.size(); ++i) {
        QAction *a = m_menu->addAction(items[i]);
        a->setCheckable(true);
        a->setChecked(i == 0);
        group->addAction(a);
        m_actions.append(a);

        connect(a, &QAction::toggled, this, [this, i](bool checked) {
            if (!m_updating && checked) {
                m_selection = i;
                emit selectionChanged(i);
            }
        });
    }
}

void RadioComboBox::setSelection(int index)
{
    if (index < 0 || index >= m_actions.size() || index == m_selection)
        return;
    m_updating = true;
    m_actions[index]->setChecked(true);
    m_updating = false;
    m_selection = index;
}

void RadioComboBox::showPopup()
{
    popupRight(m_menu);
}

ValueOptionsComboBox::ValueOptionsComboBox(QWidget *parent)
    : ValueComboBox(parent)
{
    m_menu = new QMenu(this);
    themeMenu(m_menu);

    m_actSigned = m_menu->addAction("Signed");
    m_actSigned->setCheckable(true);
    m_actSigned->setChecked(false);

    m_actBigEndian = m_menu->addAction("Big Endian");
    m_actBigEndian->setCheckable(true);
    m_actBigEndian->setChecked(false);

    m_actHex = m_menu->addAction("Hexadecimal");
    m_actHex->setCheckable(true);
    m_actHex->setChecked(false);

    m_menu->addSeparator();

    QActionGroup *sizeGroup = new QActionGroup(this);
    sizeGroup->setExclusive(true);
    const char *sizeLabels[6] = {
        "8-bit Byte", "16-bit Word", "32-bit Dword",
        "64-bit Qword", "Float (32-bit IEEE)", "Double (64-bit IEEE)"
    };
    for (int i = 0; i < 6; ++i) {
        m_sizeActions[i] = m_menu->addAction(sizeLabels[i]);
        m_sizeActions[i]->setCheckable(true);
        sizeGroup->addAction(m_sizeActions[i]);
    }
    m_sizeActions[0]->setChecked(true);

    connect(m_actSigned, &QAction::toggled, this, [this](bool v) {
        m_signed = v;
        emit optionsChanged();
    });
    connect(m_actBigEndian, &QAction::toggled, this, [this](bool v) {
        m_bigEndian = v;
        emit optionsChanged();
    });
    connect(m_actHex, &QAction::toggled, this, [this](bool v) {
        m_hex = v;
        emit optionsChanged();
    });
    for (int i = 0; i < 6; ++i) {
        connect(m_sizeActions[i], &QAction::toggled, this, [this, i](bool checked) {
            if (checked) {
                m_dataSize = i;
                emit optionsChanged();
            }
        });
    }
}

void ValueOptionsComboBox::showPopup()
{
    popupRight(m_menu);
}
