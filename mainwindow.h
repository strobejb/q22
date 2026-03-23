#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QByteArray>
#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class FindDialog;
class GotoDialog;
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
#ifdef Q_OS_WIN
    void showEvent(QShowEvent *e) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif

private:
    void openFile(const QString &path);
    void updateRecentMenu();
    void runFind(bool forward);
    void execFind(const QByteArray &pattern, uint flags);

    Ui::MainWindow *ui;
    HexView        *m_hv           = nullptr;
    StatusBar      *m_statusBar    = nullptr;
    TitleBar       *m_titleBar     = nullptr;
    QMenu          *m_recentMenu   = nullptr;
    FindDialog     *m_findDialog   = nullptr;
    GotoDialog     *m_gotoDialog   = nullptr;
    bool            m_inResizeZone = false;
    QByteArray      m_lastPattern;
    uint            m_lastFindFlags = 0;
};

#endif // MAINWINDOW_H
