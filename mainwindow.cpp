#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "HexView/hexview.h"
#include "settings.h"
#include "statusbar.h"
#include "titlebar.h"
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QIcon>
#include <QMenu>
#include <QMouseEvent>
#include <QWindow>

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

    ui->actionOpen->setIcon(QIcon::fromTheme("document-open-symbolic"));

    // Remove native title bar
    setWindowFlag(Qt::FramelessWindowHint);
    setWindowTitle("qexed");

    // Custom title bar
    m_titleBar = new TitleBar(this);

    // Recent files submenu — attached to actionRecent before the hamburger
    // menu is built so the shared QAction already carries its submenu.
    m_recentMenu = new QMenu(this);
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
    setCentralWidget(m_hv);

    // Build a standalone Edit menu for the HexView context menu, sharing the
    // same QActions so any connections added later apply automatically.
    auto *editMenu = new QMenu(this);
    for (QAction *a : ui->menuEdit->actions())
        if (a->isSeparator())
            editMenu->addSeparator();
        else
            editMenu->addAction(a);
    m_hv->setContextMenu(editMenu);

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
