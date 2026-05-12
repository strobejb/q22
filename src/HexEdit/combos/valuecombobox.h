#ifndef VALUECOMBOBOX_H
#define VALUECOMBOBOX_H

#include <QComboBox>
#include <QPoint>

class QEnterEvent;
class QMenu;

// QComboBox that paints a custom display string when collapsed, and provides
// shared helpers for QMenu-backed combo popups.
class ValueComboBox : public QComboBox
{
    Q_OBJECT
public:
    explicit ValueComboBox(QWidget *parent = nullptr);

    void setDisplayText(const QString &text);
    const QString &displayText() const { return m_displayText; }

    // Reset the minimum-width floor when the content changes mode so the new
    // mode can build its own independent floor from scratch.
    void resetMinWidth();

    QSize sizeHint() const override;

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void paintEvent(QPaintEvent *) override;

    void popupRight(QMenu *menu);
    bool isSameClickReopen();
    void recordMenuClose();

private:
    QString m_displayText;
    int     m_minWidth            = 0;
    bool    m_hovered             = false;
    bool    m_popupOpen           = false;
    bool    m_menuFilterInstalled = false;
    QPoint  m_closePos      { -1, -1 };
    QPoint  m_pendingMenuPos{ -1, -1 };
};

#endif // VALUECOMBOBOX_H
