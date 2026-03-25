#ifndef TITLEBAR_H
#define TITLEBAR_H

#include <QWidget>
#include <QStringList>

class QLabel;
class QToolButton;
class QMenu;
class QHBoxLayout;

class TitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit TitleBar(QWidget *parent = nullptr);

    QMenu *hamburgerMenu()              const { return m_menu; }
    void   setHamburgerMenu(QMenu *menu);
    QMenu *viewMenu()                   const { return m_viewMenu; }
    void   setSearchMenu(QMenu *menu);
    void   refreshStylesheet();

protected:
    void mousePressEvent(QMouseEvent *event)       override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    bool event(QEvent *e)                          override;
    bool eventFilter(QObject *obj, QEvent *e)      override;

private:
    void addWindowButtons(QHBoxLayout *layout, const QStringList &names);
    QToolButton *makeWindowButton(const QString &name);
    void updateMaxButton();

    int m_btnRadius = 6;

    QToolButton *m_hamburger  = nullptr;
    QToolButton *m_searchBtn  = nullptr;
    QToolButton *m_viewBtn    = nullptr;
    QToolButton *m_btnMin    = nullptr;
    QToolButton *m_btnMax    = nullptr;
    QToolButton *m_btnClose  = nullptr;
    QLabel      *m_title     = nullptr;
    QMenu       *m_menu       = nullptr;
    QMenu       *m_viewMenu   = nullptr;
    QMenu       *m_searchMenu = nullptr;
};

#endif // TITLEBAR_H
