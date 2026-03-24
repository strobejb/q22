#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "HexView/hexview.h"
#include "HexView/seqbase.h"
#include "bookmarkdialog.h"
#include "finddialog.h"
#include "gotodialog.h"
#include "settings.h"
#include "statusbar.h"
#include "titlebar.h"
#include "theme.h"
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QIcon>
#include <QMenu>
#include <QStyle>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>

// DWMWA_WINDOW_CORNER_PREFERENCE and DWMWA_BORDER_COLOR were added in the
// Windows 11 SDK.  Define them ourselves so the build works with older SDKs
// and MinGW headers as well.
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#  define DWMWA_WINDOW_CORNER_PREFERENCE 33
#  define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_BORDER_COLOR
#  define DWMWA_BORDER_COLOR 34
#endif

// The window is created as WS_OVERLAPPEDWINDOW (no Qt::FramelessWindowHint on
// Windows), so DWM already owns the rounded corners, accent border, and
// drop-shadow.  This function just makes the corner preference explicit so it
// isn't accidentally overridden by a future style change, and optionally pins
// the border colour to the Adwaita palette instead of the system accent.
static void applyWindows11Styling(HWND hwnd, bool dark)
{
    // Rounded corners — Win11 Build 22000+ enables this for WS_OVERLAPPEDWINDOW
    // by default, but be explicit so a style-change can't reset it.
    DWORD cornerPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &cornerPref, sizeof(cornerPref));

    // Override the 1-pixel DWM border with Adwaita palette colours instead of
    // the system accent colour.  COLORREF format is 0x00BBGGRR.
    // Light: #cdc7c2 → R=0xCD G=0xC7 B=0xC2
    // Dark:  #4a4a4a → R=G=B=0x4A
    COLORREF borderColor = dark ? 0x004A4A4A : 0x00C2C7CD;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR,
                          &borderColor, sizeof(borderColor));
}
#endif

static const int RESIZE_MARGIN = 5;

static Qt::Edges edgesFromPos(const QPoint &pos, const QRect &rect) {
    Qt::Edges edges;
    if (pos.x() <= RESIZE_MARGIN)
        edges |= Qt::LeftEdge;
    if (pos.x() >= rect.width() - RESIZE_MARGIN)
        edges |= Qt::RightEdge;
    if (pos.y() <= RESIZE_MARGIN)
        edges |= Qt::TopEdge;
    if (pos.y() >= rect.height() - RESIZE_MARGIN)
        edges |= Qt::BottomEdge;
    return edges;
}

static QCursor cursorForEdges(Qt::Edges edges) {
    bool l = edges & Qt::LeftEdge, r = edges & Qt::RightEdge;
    bool t = edges & Qt::TopEdge, b = edges & Qt::BottomEdge;
    if ((l && t) || (r && b))
        return Qt::SizeFDiagCursor;
    if ((r && t) || (l && b))
        return Qt::SizeBDiagCursor;
    if (l || r)
        return Qt::SizeHorCursor;
    if (t || b)
        return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

    // On Windows use the Segoe MDL2 FolderOpen glyph (0xED25) so the icon
    // matches the monochrome Segoe style used throughout the title bar.
    // fromTheme() can return unexpected icons from Qt's built-in fallback theme.
#ifdef Q_OS_WIN
    {
        const bool   dark = palette().window().color().lightness() < 128;
        const QColor fg   = dark ? QColor("#ffffff") : QColor("#000000");
        QIcon icon = segoeIcon(0xED25, fg, 16);   // FolderOpen, slightly larger for menu
        if (icon.isNull())
            icon = QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon);
        ui->actionOpen->setIcon(icon);
    }
#else
    ui->actionOpen->setIcon(QIcon::fromTheme("document-open-symbolic"));
#endif

    // Remove native title bar.
    // On Windows, keep the native WS_OVERLAPPEDWINDOW (which includes
    // WS_THICKFRAME) so DWM automatically applies rounded corners, the accent
    // border, and drop-shadow.  nativeEvent() handles WM_NCCALCSIZE to
    // collapse the NC area to zero so the title-bar chrome never appears.
    // On all other platforms, use Qt's own frameless hint.
#ifndef Q_OS_WIN
    setWindowFlag(Qt::FramelessWindowHint);
#endif
    setWindowTitle("q22");

    // Custom title bar
    m_titleBar = new TitleBar(this);

    // Recent files submenu — attached to actionRecent before the hamburger
    // menu is built so the shared QAction already carries its submenu.
    m_recentMenu = new QMenu(this);
    themeMenu(m_recentMenu);
    ui->actionRecent->setMenu(m_recentMenu);
    updateRecentMenu();

    // Build a standalone copy of the File menu that shares the same QAction
    // objects (so shortcuts and connections remain intact) but is not a child
    // of the QMenuBar, which would interfere with popup display.
    auto *fileMenu = new QMenu(m_titleBar);
    for (QAction *a : ui->menuFile->actions()) {
        if (a->isSeparator())
            fileMenu->addSeparator();
        else
            fileMenu->addAction(a);
    }
    m_titleBar->setHamburgerMenu(fileMenu);

    // Search menu — same standalone-copy pattern.
    auto *searchMenu = new QMenu(m_titleBar);
    for (QAction *a : ui->menuSearh->actions())
        if (a->isSeparator())
            searchMenu->addSeparator();
        else
            searchMenu->addAction(a);
    m_titleBar->setSearchMenu(searchMenu);

    // Populate the view (right-side) menu with Tools actions.
    // Submenus need standalone QMenu copies (not children of the QMenuBar)
    // so Qt can display them as popups outside the menubar system.
    for (QAction *a : ui->menuTools->actions()) {
        if (a->isSeparator())
            m_titleBar->viewMenu()->addSeparator();
        else if (QMenu *sub = a->menu()) {
            auto *copy = new QMenu(sub->title(), m_titleBar);
            themeMenu(copy);
            for (QAction *sa : sub->actions())
                if (sa->isSeparator())
                    copy->addSeparator();
                else
                    copy->addAction(sa);
            m_titleBar->viewMenu()->addMenu(copy);
        } else {
            m_titleBar->viewMenu()->addAction(a);
        }
    }

    setMenuWidget(m_titleBar);

    m_hv = new HexView(this);
    m_hv->setObjectName("HexView");
    m_hv->setStyle(HVS_RESIZEBAR | HVS_SHOWMODS, HVS_RESIZEBAR | HVS_SHOWMODS);
    m_hv->setHexColour(HVC_HEXEVEN, 0x000000FF);
    m_hv->setHexColour(HVC_HEXODD, 0x00000080);
    m_hv->setGrouping(2);
    m_hv->setPadding(3, 3);
    m_hv->setFontSpacing(2, 2);
    // Container: HexView fills available space; FindDialog sits flush above
    // the status bar, hidden until activated.
    auto *central = new QWidget(this);
    auto *vlay    = new QVBoxLayout(central);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);
    vlay->addWidget(m_hv, 1);
    m_bookmarkDialog = new BookmarkDialog(this);
    m_findDialog = new FindDialog(central);
    m_gotoDialog = new GotoDialog(m_hv, central);
    vlay->addWidget(m_findDialog, 0);
    vlay->addWidget(m_gotoDialog, 0);
    setCentralWidget(central);

    // Build a standalone Edit menu for the HexView context menu, sharing the
    // same QActions so any connections added later apply automatically.
    auto *editMenu = new QMenu(this);
    themeMenu(editMenu);
    for (QAction *a : ui->menuEdit->actions()) {
        if (a->isSeparator()) {
            editMenu->addSeparator();
        } else if (QMenu *sub = a->menu()) {
            auto *copy = new QMenu(sub->title(), editMenu);
            themeMenu(copy);
            for (QAction *sa : sub->actions())
                if (sa->isSeparator()) copy->addSeparator();
                else                   copy->addAction(sa);
            editMenu->addMenu(copy);
        } else {
            editMenu->addAction(a);
        }
    }
    m_hv->setContextMenu(editMenu);

    // Actions in nested submenus of the replaced QMenuBar lose their shortcut
    // registration when the menubar is hidden. Re-register them on the window.
    addAction(ui->actionBookmark_here);
    addAction(ui->actionSelect_All);
    addAction(ui->actionHighlight_All_Occurances);

    // View submenu: exclusive action groups (radio behaviour within each section)
    QActionGroup *fmtGroup = new QActionGroup(this);
    fmtGroup->setExclusive(true);
    for (QAction *a : {ui->actionHexadecimal_2, ui->actionDecimal_2,
                       ui->actionOctal_2, ui->actionBinary_2})
        fmtGroup->addAction(a);

    QActionGroup *sizeGroup = new QActionGroup(this);
    sizeGroup->setExclusive(true);
    for (QAction *a : {ui->action8_bit_Byte, ui->action16_bit_Word,
                       ui->action32_bit_Dword_2, ui->action64_bit_Qword})
        sizeGroup->addAction(a);

    // Format → HexView::setStyle
    connect(ui->actionHexadecimal_2, &QAction::toggled, this, [this](bool on) {
        if (on)
            m_hv->setStyle(HVS_FORMAT_MASK, HVS_FORMAT_HEX);
    });
    connect(ui->actionDecimal_2, &QAction::toggled, this, [this](bool on) {
        if (on)
            m_hv->setStyle(HVS_FORMAT_MASK, HVS_FORMAT_DEC);
    });
    connect(ui->actionOctal_2, &QAction::toggled, this, [this](bool on) {
        if (on)
            m_hv->setStyle(HVS_FORMAT_MASK, HVS_FORMAT_OCT);
    });
    connect(ui->actionBinary_2, &QAction::toggled, this, [this](bool on) {
        if (on)
            m_hv->setStyle(HVS_FORMAT_MASK, HVS_FORMAT_BIN);
    });

    // Size → HexView::setGrouping
    connect(ui->action8_bit_Byte, &QAction::toggled, this, [this](bool on) {
        if (on)
            m_hv->setGrouping(1);
    });
    connect(ui->action16_bit_Word, &QAction::toggled, this, [this](bool on) {
        if (on)
            m_hv->setGrouping(2);
    });
    connect(ui->action32_bit_Dword_2, &QAction::toggled, this, [this](bool on) {
        if (on)
            m_hv->setGrouping(4);
    });
    connect(ui->action64_bit_Qword, &QAction::toggled, this, [this](bool on) {
        if (on)
            m_hv->setGrouping(8);
    });

    m_statusBar = new StatusBar(m_hv, ui->statusbar, this);

    connect(m_hv, &HexView::cursorChanged, m_statusBar, &StatusBar::update);
    connect(m_hv, &HexView::selectionChanged, this,
            [this](size_w, size_w) { m_statusBar->update(); });
    connect(m_hv, &HexView::lengthChanged, this,
            [this](size_w) { m_statusBar->update(); });

    connect(ui->actionBookmark_here, &QAction::triggered, this, [this]() {
        const size_w selSize = m_hv->selectionSize();
        const size_w offset  = selSize > 0 ? m_hv->selectionStart() : m_hv->cursorOffset();
        const size_w length  = selSize > 0 ? selSize : 1;
        m_bookmarkDialog->setOffset(offset);
        m_bookmarkDialog->setLength(length);
        m_bookmarkDialog->setForegroundColour(m_hv->palette().text().color());
        if (m_bookmarkDialog->exec() == QDialog::Accepted) {
            Bookmark bm;
            bm.offset   = m_bookmarkDialog->offset();
            bm.length   = static_cast<size_w>(m_bookmarkDialog->length());
            bm.name     = m_bookmarkDialog->bookmarkName();
            bm.fgColour = m_bookmarkDialog->foregroundColour().rgb();
            bm.bgColour = m_bookmarkDialog->selectedColour().isValid()
                              ? m_bookmarkDialog->selectedColour().rgb()
                              : m_hv->getHexColour(HVC_BOOKMARK_BG);
            m_hv->addBookmark(bm);
            m_gotoDialog->refreshBookmarks();
        }
    });

    connect(m_gotoDialog, &GotoDialog::bookmarkRequested,
            ui->actionBookmark_here, &QAction::trigger);

    connect(ui->actionFind, &QAction::triggered, this, [this]() {
        QWidget *fw = QApplication::focusWidget();
        if (m_findDialog->isVisible() && fw && m_findDialog->isAncestorOf(fw))
            m_findDialog->hide();
        else
            m_findDialog->activate();
    });

    connect(ui->action_Goto, &QAction::triggered, this, [this]() {
        QWidget *fw = QApplication::focusWidget();
        if (m_gotoDialog->isVisible() && fw && m_gotoDialog->isAncestorOf(fw))
            m_gotoDialog->hide();
        else
            m_gotoDialog->activate();
    });

    connect(m_findDialog, &FindDialog::searchRequested, this,
            [this](const QByteArray &pattern, uint flags) {
                m_hv->findInit(reinterpret_cast<const uint8_t *>(pattern.constData()),
                               (size_t)pattern.size());
                execFind(pattern, flags);
            });

    connect(ui->actionFind_Next,          &QAction::triggered,  this, [this] { runFind(true);  });
    connect(ui->actionFind_Previous,      &QAction::triggered,  this, [this] { runFind(false); });
    connect(m_findDialog, &FindDialog::findNext,     this, [this] { runFind(true);  });
    connect(m_findDialog, &FindDialog::findPrevious, this, [this] { runFind(false); });

    connect(m_hv, &HexView::findProgress, m_statusBar, &StatusBar::onFindProgress);
    connect(m_findDialog, &FindDialog::searchHexChanged, m_statusBar, &StatusBar::showSearchHex);

    connect(ui->actionOpen, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open File"));
        if (!path.isEmpty())
            openFile(path);
    });

    // Edge-resize event filter: catches mouse events on any child widget
    qApp->installEventFilter(this);
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::openFile(const QString &path) {
    m_hv->openFile(path);
    AppSettings::addRecentFile(path);
    updateRecentMenu();
}

void MainWindow::updateRecentMenu() {
    m_recentMenu->clear();
    const QStringList files = AppSettings::recentFiles();
    if (files.isEmpty()) {
        QAction *empty = m_recentMenu->addAction(tr("No recent files"));
        empty->setEnabled(false);
        return;
    }
    const QString home = QDir::homePath();
    for (const QString &path : files) {
        QString display =
            path.startsWith(home + '/') ? "~/" + path.mid(home.size() + 1) : path;
        QAction *a = m_recentMenu->addAction(display);
        a->setToolTip(path);
        connect(a, &QAction::triggered, this, [this, path] { openFile(path); });
    }
}

void MainWindow::execFind(const QByteArray &pattern, uint flags)
{
    m_lastPattern   = pattern;
    m_lastFindFlags = flags & ~HVFF_BACKWARD; // strip direction; stored flags are always "forward"
    size_w result   = 0;
    if (m_hv->findNext(&result, flags)) {
        m_hv->setCurSel(result, result + (size_t)pattern.size());
        m_hv->scrollTo(result);
        m_statusBar->showMessage({});
    } else {
        m_statusBar->showMessage(tr("Could not find any more data"));
    }
    m_statusBar->onFindDone();
}

void MainWindow::runFind(bool forward)
{
    if (m_lastPattern.isEmpty())
        return;
    uint flags = m_lastFindFlags | (forward ? 0 : HVFF_BACKWARD);
    execFind(m_lastPattern, flags);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    auto *w = qobject_cast<QWidget *>(obj);
    if (!w || w->window() != this)
        return false;

    const auto type = event->type();

    // ── Cursor feedback on hover ─────────────────────────────────────────────
    if (type == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(event);
        Qt::Edges edges =
            isMaximized()
            ? Qt::Edges{}
            : edgesFromPos(mapFromGlobal(me->globalPosition().toPoint()),
                                             rect());

        if (edges) {
            if (!m_inResizeZone) {
                m_inResizeZone = true;
                QApplication::setOverrideCursor(cursorForEdges(edges));
            } else {
                QApplication::changeOverrideCursor(cursorForEdges(edges));
            }
        } else if (m_inResizeZone) {
            m_inResizeZone = false;
            QApplication::restoreOverrideCursor();
        }
        return false; // don't consume — just update cursor
    }

    // Restore cursor if mouse leaves the window entirely
    if (type == QEvent::Leave && obj == this && m_inResizeZone) {
        m_inResizeZone = false;
        QApplication::restoreOverrideCursor();
        return false;
    }

    // ── Start resize on click at window edge ─────────────────────────────────
    if (type == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton || isMaximized())
            return false;

        Qt::Edges edges =
            edgesFromPos(mapFromGlobal(me->globalPosition().toPoint()), rect());
        if (!edges)
            return false;

        windowHandle()->startSystemResize(edges);
        return true;
    }

    return false;
}

#ifdef Q_OS_WIN
void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    bool dark = palette().window().color().lightness() < 128;
    applyWindows11Styling(reinterpret_cast<HWND>(winId()), dark);
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    MSG *msg = reinterpret_cast<MSG *>(message);
    if (msg->message == WM_NCCALCSIZE && msg->wParam == TRUE) {
        // Collapse the non-client area to zero: the title-bar and resize-border
        // chrome never appear, but DWM still sees WS_THICKFRAME and applies its
        // rounded corners, accent border, and drop-shadow.
        //
        // When maximized, Windows extends the window rect slightly off-screen
        // (by the frame thickness) to hide the thick-frame border.  Without
        // compensation our client area would bleed under the taskbar.  Trim it
        // back by the DPI-aware frame size so the content stays on-screen.
        if (IsZoomed(msg->hwnd)) {
            auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
            UINT dpi  = GetDpiForWindow(msg->hwnd);
            int  bx   = GetSystemMetricsForDpi(SM_CXFRAME, dpi)
                      + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            int  by   = GetSystemMetricsForDpi(SM_CYFRAME, dpi)
                      + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            params->rgrc[0].left   += bx;
            params->rgrc[0].right  -= bx;
            params->rgrc[0].top    += by;
            params->rgrc[0].bottom -= by;
        }
        *result = 0;
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif
