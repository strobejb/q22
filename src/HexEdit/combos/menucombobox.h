#ifndef MENUCOMBOBOX_H
#define MENUCOMBOBOX_H

#include <QComboBox>
#include <QMenu>
#include <QPoint>

class QFileDialog;

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
    void  setPopupOpen(bool open);

    QMenu  *m_menu      = nullptr;
    QPoint  m_closePos  { -1, -1 };

    bool isSameClickReopen();
    void recordMenuClose() { m_closePos = QCursor::pos(); }
};

// QFileDialog creates private ordinary QComboBox controls for "Look in" and
// file type filters. Install this on a non-native QFileDialog when those combos
// should use the same rounded/shadowed QMenu popup behaviour as MenuComboBox.
void installThemedFileDialogComboPopups(QFileDialog *dialog);

#endif // MENUCOMBOBOX_H
