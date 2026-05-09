#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QByteArray>
#include <QCloseEvent>
#include <QMainWindow>
#include "palettes.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class BookmarkDialog;
class FindDialog;
class GotoDialog;
class PreferencesDialog;
class HexView;
class Hairline;
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
    void closeEvent(QCloseEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void changeEvent(QEvent *e) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#else
    void paintEvent(QPaintEvent *e) override;
#endif

private:
    void openFile(const QString &path);
    bool maybeSave();       // prompt if modified; returns false if user cancelled
    void updateRecentMenu();
    void updateEditActions();
    void runFind(bool forward);
    void execFind(const QByteArray &pattern, uint flags);
    void applyMenuMode(bool useCustomTitleBar);
#ifdef Q_OS_WIN
    void updateWinChromeColors();
#else
    void applyShadowMargin();  // set / clear window content-margins for shadow area
#endif

    Ui::MainWindow *ui;
    HexView        *m_hv           = nullptr;
    StatusBar      *m_statusBar    = nullptr;
    TitleBar       *m_titleBar      = nullptr;
    Hairline       *m_titleHairline = nullptr;
    QMenu          *m_recentMenu   = nullptr;
    BookmarkDialog    *m_bookmarkDialog = nullptr;
    FindDialog        *m_findDialog     = nullptr;
    GotoDialog        *m_gotoDialog     = nullptr;
    PreferencesDialog *m_prefsDialog    = nullptr;
    bool            m_useCustomTitleBar = true;
    bool            m_inResizeZone      = false;
#ifndef Q_OS_WIN
    QWidget        *m_cornerClipper     = nullptr;
#endif
    QByteArray      m_lastPattern;
    uint            m_lastFindFlags  = 0;
    bool            m_findRunning     = false;
    bool            m_canPaste        = false; // text or hexview data; updated from dataChanged
    bool            m_canPasteSpecial = false; // any clipboard format; updated from dataChanged
    PaletteInfo     m_currentPalette;           // last applied palette; re-used on scheme change
};

#endif // MAINWINDOW_H
