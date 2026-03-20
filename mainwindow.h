#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

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

private:
    void openFile(const QString &path);
    void updateRecentMenu();

    Ui::MainWindow *ui;
    HexView        *m_hv           = nullptr;
    StatusBar      *m_statusBar    = nullptr;
    TitleBar       *m_titleBar     = nullptr;
    QMenu          *m_recentMenu   = nullptr;
    bool            m_inResizeZone = false;
};

#endif // MAINWINDOW_H
