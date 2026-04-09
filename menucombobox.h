#ifndef MENUCOMBOBOX_H
#define MENUCOMBOBOX_H

#include <QComboBox>
#include <QMenu>
#include <QPoint>

// QComboBox subclass that replaces the native dropdown with a themed QMenu.
// Drop-in replacement for plain QComboBox in dialogs: populate with addItem()
// as normal. The menu is rebuilt from the item model each time showPopup() is
// called so model changes are always reflected.
class MenuComboBox : public QComboBox
{
    Q_OBJECT
public:
    explicit MenuComboBox(QWidget *parent = nullptr);

protected:
    QSize sizeHint()        const override;
    QSize minimumSizeHint() const override;
    void  paintEvent(QPaintEvent *) override;
    void  showPopup() override;

private:
    void  buildMenu();

    QMenu  *m_menu      = nullptr;
    QPoint  m_closePos  { -1, -1 };

    bool isSameClickReopen();
    void recordMenuClose() { m_closePos = QCursor::pos(); }
};

#endif // MENUCOMBOBOX_H
