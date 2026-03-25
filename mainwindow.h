#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QByteArray>
#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class BookmarkDialog;
class FindDialog;
class GotoDialog;
class PreferencesDialog;
class HexView;
class QMenu;
class StatusBar;
class TitleBar;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void showEvent(QShowEvent *e) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif

private:
    void openFile(const QString &path);
    void updateRecentMenu();
    void runFind(bool forward);
    void execFind(const QByteArray &pattern, uint flags);
    void applyMenuMode(bool useCustomTitleBar);

    Ui::MainWindow *ui;
    HexView        *m_hv           = nullptr;
    StatusBar      *m_statusBar    = nullptr;
    TitleBar       *m_titleBar     = nullptr;
    QMenu          *m_recentMenu   = nullptr;
    BookmarkDialog    *m_bookmarkDialog = nullptr;
    FindDialog        *m_findDialog     = nullptr;
    GotoDialog        *m_gotoDialog     = nullptr;
    PreferencesDialog *m_prefsDialog    = nullptr;
    bool            m_useCustomTitleBar = true;
    bool            m_inResizeZone      = false;
    QByteArray      m_lastPattern;
    uint            m_lastFindFlags = 0;
};

#endif // MAINWINDOW_H
