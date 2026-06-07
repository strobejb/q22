#ifndef TITLEBAR_H
#define TITLEBAR_H

#include <QColor>
#include <QWidget>
#include <QStringList>

#ifdef Q_OS_WIN
// Returns the Windows 11 chrome background colour for the given activation state.
// active=true  → #F3F3F3 / #202020  (focused, neutral white/grey)
// active=false → #EBEBEB / #2D2D2D  (unfocused, dimmed neutral)
QColor windowsChromeBg(bool active);
#endif

class QLabel;
class QToolButton;
class QMenu;
class QHBoxLayout;

struct TitleBarOptions
{
    bool showFileMenu = true;
    bool showSearchMenu = true;
    bool showFileInfoButton = true;
    bool showViewMenu = true;
    bool showMinimize = true;
    bool showMaximize = true;
    bool showClose = true;
    bool allowMaximizeOnDoubleClick = true;
    bool compact = false;
    bool leftAlignTitle = false;
    bool roundedAppButtonsOnWindows = true;
};

class TitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit TitleBar(QWidget *parent = nullptr, const TitleBarOptions &options = {});

    QMenu *hamburgerMenu()              const { return m_menu; }
    void   setHamburgerMenu(QMenu *menu);
    QMenu *viewMenu()                   const { return m_viewMenu; }
    void   setSearchMenu(QMenu *menu);
    void   setFileInfoPanelOpen(bool open);
    void   refreshStylesheet();

signals:
    void fileInfoToggled();

protected:
    void mousePressEvent(QMouseEvent *event)       override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *e)                    override;
    bool event(QEvent *e)                          override;
    bool eventFilter(QObject *obj, QEvent *e)      override;

private:
    void addWindowButtons(QHBoxLayout *layout, const QStringList &names);
    QToolButton *makeWindowButton(const QString &name);
    void updateMaxButton();
    void clearStaleButtonHover();

    int m_btnRadius = 6;

    QToolButton *m_hamburger  = nullptr;
    QToolButton *m_searchBtn  = nullptr;
    QToolButton *m_fileInfoBtn = nullptr;
    QToolButton *m_viewBtn    = nullptr;
    QToolButton *m_btnMin    = nullptr;
    QToolButton *m_btnMax    = nullptr;
    QToolButton *m_btnClose  = nullptr;
    QLabel      *m_title     = nullptr;
    QMenu       *m_menu       = nullptr;
    QMenu       *m_viewMenu   = nullptr;
    QMenu       *m_searchMenu = nullptr;
    TitleBarOptions m_options;
};

#endif // TITLEBAR_H
