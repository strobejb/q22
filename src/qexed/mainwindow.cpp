#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "HexView/hexview.h"
#include "HexView/seqbase.h"
#include "bookmarkdialog.h"
#include "dlgabout.h"
#include "dlgcopyas.h"
#include "dlgexport.h"
#include "dlgimport.h"
#include "dlgpastespecial.h"
#include "dockpanelhost.h"
#include "finddialog.h"
#include "gotodialog.h"
#include "palettes.h"
#include "preferences.h"
#include "settings.h"
#include "statusbar.h"
#include "titlebar.h"
#include "theme.h"
#include <functional>
#include <QActionGroup>
#include <QShortcut>
#include <QTimer>
#include <QVector>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QMimeData>
#include <QAbstractButton>
#include <QPushButton>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QHelpEvent>
#include <QToolTip>
#include <QWidgetAction>
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

// ── Shadow / resize constants ─────────────────────────────────────────────────
// On Linux/KDE the window draws its own drop-shadow (Firefox-style: transparent
// margin + painted gradient, no KWin API required).  The margin also acts as
// the edge-resize hit zone so the user can grab the window from the shadow.
// On GNOME and other compositors the WM provides the shadow; only a narrow
// resize strip is needed (same as Windows).
// On Windows DWM provides the shadow; only a narrow resize strip is needed.
#ifndef Q_OS_WIN
static constexpr int kShadowSize   = 18; // KDE: shadow margin (also the resize zone)
static constexpr int kCornerRadius = 10; // must match paintEvent's drawRoundedRect
static constexpr int kResizeMargin =  5; // non-KDE: narrow strip at the window edge
#else
static constexpr int RESIZE_MARGIN = 5;
#endif

#ifndef Q_OS_WIN
// Returns true when running inside a KDE Plasma session.  KDE draws compositor
// shadows only via KWindowEffects, so the app must paint its own shadow (the
// gradient-in-transparent-margin approach).  On GNOME and other compositors the
// WM provides a shadow automatically — painting our own would double it.
static bool isKDE()
{
    static const bool kde =
        qgetenv("XDG_CURRENT_DESKTOP").toUpper().contains("KDE");
    return kde;
}
#endif

// ── CornerClipper ─────────────────────────────────────────────────────────────
// Transparent overlay that restores the shadow gradient in the four corner
// triangles of the content rect after child widgets have overdrawn them with
// opaque backgrounds.
//
// MainWindow::paintEvent paints the shadow rings (including the corner areas)
// before children paint.  Child widgets subsequently paint their full rects,
// overwriting those corners with opaque pixels.  The CornerClipper re-runs the
// same shadow loop (clipped to the corner-triangle path) using
// CompositionMode_Source to overwrite whatever the children painted.
//
// It is raised to the top of the z-order so it paints last, and installs event
// filters on every sibling so it can re-run its paintEvent in the same
// backing-store flush cycle whenever a sibling repaints.
#ifndef Q_OS_WIN
namespace {
class CornerClipper : public QWidget
{
public:
    explicit CornerClipper(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);
        setGeometry(parent->rect());
        raise();
        parent->installEventFilter(this);
        // Watch all existing direct children so we re-clear after their repaints.
        for (QObject *child : parent->children()) {
            if (auto *w = qobject_cast<QWidget *>(child); w && w != this)
                w->installEventFilter(this);
        }
    }

    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (obj == parentWidget()) {
            if (e->type() == QEvent::Resize) {
                const auto *re = static_cast<QResizeEvent *>(e);
                setGeometry(QRect(QPoint(0, 0), re->size()));
                raise();
            } else if (e->type() == QEvent::ChildAdded) {
                // A new child was added — watch it too.
                if (auto *w = qobject_cast<QWidget *>(
                        static_cast<QChildEvent *>(e)->child());
                        w && w != this)
                    w->installEventFilter(this);
            }
        } else if (obj != this && e->type() == QEvent::Paint) {
            // A sibling just painted and may have overdrawn our cleared corners.
            // Re-raise and queue a re-clear in the same backing-store pass.
            // update() is deferred but safe: Qt hasn't flushed the backing store
            // yet when Paint is dispatched, so the clipper will be included in
            // the same flush cycle (painted last due to its raised z-order).
            raise();
            update();
        }

        return false;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        auto *w = parentWidget();
        if (!w || w->isMaximized() || w->isFullScreen()) return;

        const QRectF content = QRectF(rect()).adjusted(
            kShadowSize, kShadowSize, -kShadowSize, -kShadowSize);
        if (content.isEmpty()) return;

        // The corner triangles (inside content rect, outside rounded rect) were
        // painted with the correct shadow gradient by MainWindow::paintEvent, but
        // child widgets then overdrew them with opaque backgrounds.  We restore
        // the shadow by re-running the same shadow loop, clipped to only those
        // triangle areas.  CompositionMode_Source overwrites whatever the child
        // widgets drew with the original shadow colour.
        QPainterPath contentPath, roundedPath;
        contentPath.addRect(content);
        roundedPath.addRoundedRect(content, kCornerRadius, kCornerRadius);
        const QPainterPath corners = contentPath - roundedPath;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setClipPath(corners);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.setPen(Qt::NoPen);

        for (int dist = kShadowSize; dist >= 1; --dist) {
            const qreal t   = qreal(kShadowSize - dist) / (kShadowSize - 1);
            const int alpha = int(80 * t * t);
            const QRectF r  = content.adjusted(-dist, -dist, dist, dist);
            p.setBrush(QColor(0, 0, 0, alpha));
            p.drawRoundedRect(r, kCornerRadius + dist, kCornerRadius + dist);
        }
    }
};
} // namespace
#endif // Q_OS_WIN

static Qt::Edges edgesFromPos(const QPoint &pos, const QRect &rect, int margin) {
    Qt::Edges edges;
    if (pos.x() <= margin)                  edges |= Qt::LeftEdge;
    if (pos.x() >= rect.width()  - margin)  edges |= Qt::RightEdge;
    if (pos.y() <= margin)                  edges |= Qt::TopEdge;
    if (pos.y() >= rect.height() - margin)  edges |= Qt::BottomEdge;
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

// ─── ThemePickerWidget ────────────────────────────────────────────────────────
// Three circles (Light / System / Dark) embedded as a QWidgetAction at the top
// of the Tools menu.  No Q_OBJECT needed — uses a std::function callback.

class ThemePickerWidget : public QWidget
{
public:
    static constexpr int VPAD     = 14;  // vertical padding above & below
    static constexpr int TARGET_D = 52;  // desired circle diameter
    static constexpr int GAP      = 12;  // fixed gap between circle edges

    ThemePickerWidget(ColorScheme current,
                      std::function<void(ColorScheme)> cb,
                      QWidget *parent = nullptr)
        : QWidget(parent), m_current(current), m_cb(std::move(cb))
    {
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    QSize sizeHint() const override
    {
        // Height drives d() — set it so circles come out TARGET_D tall.
        // Width is just a sensible minimum; the menu stretches us to its width.
        return QSize(TARGET_D * 3 + GAP * 2 + TARGET_D, VPAD + TARGET_D + VPAD);
    }

    void setCurrent(ColorScheme s) { m_current = s; update(); }

    // Always derived from the actual rendered height so it stays in sync.
    int d() const { return height() - 2 * VPAD; }

private:

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QPalette &pal  = palette();
        const QColor    bord = pal.color(QPalette::Mid);
        const QColor    sel  = pal.color(QPalette::Highlight);

        static const QColor sLight("#f8f8f8");
        static const QColor sDark ("#242424");

        for (int i = 0; i < 3; ++i) {
            const QRectF cr   = QRectF(circleRect(i)).adjusted(0.5, 0.5, -0.5, -0.5);
            const bool   hov  = (m_hovered == i);
            const bool   curr = (static_cast<int>(m_current) == i);

            if (i == 1) {
                // System — left half dark, right half light
                p.setPen(Qt::NoPen);
                QPainterPath clip;
                clip.addEllipse(cr);
                p.setClipPath(clip);
                p.setBrush(sDark);
                p.drawRect(QRectF(cr.left(), cr.top(), cr.width() / 2, cr.height()));
                p.setBrush(sLight);
                p.drawRect(QRectF(cr.left() + cr.width() / 2, cr.top(),
                                  cr.width() / 2, cr.height()));
                p.setClipping(false);
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(i == 0 ? sLight : sDark);
                p.drawEllipse(cr);
            }

            // Border: accent if selected, hover-lightened if hovered, normal otherwise
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(curr ? sel : (hov ? bord.lighter(140) : bord),
                          curr ? 2.5 : 1.0));
            p.drawEllipse(cr);
        }
    }

    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::ToolTip) {
            auto *he = static_cast<QHelpEvent *>(e);
            const int h = hitCircle(he->pos());
            static const char *tips[] = { "Light", "System", "Dark" };
            if (h >= 0)
                QToolTip::showText(he->globalPos(), tips[h], this);
            else
                QToolTip::hideText();
            return true;
        }
        return QWidget::event(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int h = hitCircle(e->pos());
        if (h != m_hovered) { m_hovered = h; update(); }
    }

    void leaveEvent(QEvent *) override
    {
        if (m_hovered != -1) { m_hovered = -1; update(); }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) return;
        const int h = hitCircle(e->pos());
        if (h >= 0) {
            m_current = static_cast<ColorScheme>(h);
            update();
            m_cb(m_current);
        }
    }

private:
    // Circles grouped with GAP between them, centered in the widget width.
    QRect circleRect(int i) const
    {
        const int diam   = d();
        const int groupW = diam * 3 + GAP * 2;
        const int startX = (width() - groupW) / 2;
        const int cx     = startX + diam / 2 + i * (diam + GAP);
        const int cy     = height() / 2;
        return QRect(cx - diam / 2, cy - diam / 2, diam, diam);
    }

    int hitCircle(QPoint pos) const
    {
        for (int i = 0; i < 3; ++i)
            if (circleRect(i).adjusted(-6, -6, 6, 6).contains(pos))
                return i;
        return -1;
    }

    ColorScheme                      m_current;
    int                              m_hovered = -1;
    std::function<void(ColorScheme)> m_cb;
};

// QWidgetAction subclass: overrides createWidget so every menu that receives
// this action gets its own properly-parented ThemePickerWidget instance.
// (setDefaultWidget only works for the *first* container added; subsequent
// containers get null from the default createWidget implementation.)
class ThemePickerAction : public QWidgetAction
{
public:
    ThemePickerAction(ColorScheme current,
                      std::function<void(ColorScheme)> cb,
                      QObject *parent = nullptr)
        : QWidgetAction(parent), m_current(current), m_cb(std::move(cb)) {}

protected:
    QWidget *createWidget(QWidget *parent) override
    {
        return new ThemePickerWidget(m_current, m_cb, parent);
    }

private:
    ColorScheme                      m_current;
    std::function<void(ColorScheme)> m_cb;
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    ui->menuView->menuAction()->setVisible(false);

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

    setWindowTitle(QApplication::applicationName());

    // Custom title bar
    m_titleBar = new TitleBar(this);

    // Recent files submenu — attached to actionRecent before the hamburger
    // menu is built so the shared QAction already carries its submenu.
    m_recentMenu = new QMenu(this);
    themeMenu(m_recentMenu);
    ui->actionRecent->setMenu(m_recentMenu);
    updateRecentMenu();

    // Ctrl+O, R — open most recently used file
    auto *recentShortcut = new QShortcut(QKeySequence("Ctrl+Shift+R"), this);
    connect(recentShortcut, &QShortcut::activated, this, [this] {
        const QStringList files = AppSettings::recentFiles();
        if (!files.isEmpty())
            openFile(files.first());
    });

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

    // Theme picker — inserted at the top of both the native menubar Tools menu
    // and the titlebar viewMenu copy.
    const auto currentScheme = static_cast<ColorScheme>(AppSettings::prefColorScheme());
    auto themeCb = [this](ColorScheme s) {
        AppSettings::setPrefColorScheme(static_cast<int>(s));
        applyAdwaitaTheme(s);
        if (!m_currentPalette.name.isEmpty()) {
            applyPalette(m_hv, m_currentPalette);
            applyUiPalette(m_currentPalette);  // internally re-applies theme with UI overrides
        }
        m_titleBar->refreshStylesheet();  // after palette so UI overrides are already active
#ifdef Q_OS_WIN
        if (m_useCustomTitleBar)
            updateWinChromeColors();
#endif
    };
    auto *pickerAction = new ThemePickerAction(currentScheme, themeCb, this);

    ui->menuTools->insertAction(ui->menuTools->actions().first(), pickerAction);
    ui->menuTools->insertSeparator(ui->menuTools->actions().at(1));

    m_titleBar->viewMenu()->addAction(pickerAction);
    m_titleBar->viewMenu()->addSeparator();

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

    m_hv = new HexView(this);
    m_hv->setObjectName("HexView");
    m_hv->setFrameShape(QFrame::NoFrame);
    m_hv->setStyle(HVS_RESIZEBAR | HVS_SHOWMODS| HVS_INVERTSELECTION,
                   HVS_RESIZEBAR | HVS_SHOWMODS );
    m_hv->setHexColour(HVC_HEXEVEN, QColor(0, 0, 255));
    m_hv->setHexColour(HVC_HEXODD,  QColor(0, 0, 128));
    m_hv->setGrouping(2);
    m_hv->setPadding(3, 3);
    // Container: HexView fills available space; FindDialog sits flush above
    // the status bar, hidden until activated.
    auto *central = new QWidget(this);
    auto *vlay    = new QVBoxLayout(central);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);
    vlay->addWidget(m_titleBar);   // sits at the top of the content area in custom-titlebar mode
    m_titleHairline = new Hairline(central, Hairline::Edge::Bottom, m_titleBar);  // bgSource swapped in applyMenuMode
    vlay->addWidget(m_titleHairline);
    vlay->addWidget(m_hv, 1);
    m_bookmarkDialog = new BookmarkDialog(this);
    auto *dockPanelHost = new DockPanelHost(m_hv, central);
    m_findDialog = new FindDialog(dockPanelHost);
    m_findDialog->setObjectName("FindDialog");
    m_findDialog->setWindowFlags(Qt::Widget); // embedded panel — no native QWidgetWindow
    m_gotoDialog = new GotoDialog(m_hv, dockPanelHost);
    m_gotoDialog->setObjectName("GotoDialog");
    m_gotoDialog->setWindowFlags(Qt::Widget); // embedded panel — no native QWidgetWindow
    dockPanelHost->addPanel(m_findDialog);
    dockPanelHost->addPanel(m_gotoDialog);
    vlay->addWidget(dockPanelHost, 0);
    vlay->addWidget(new Hairline(central));  // separator between content and status bar
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
    ui->statusbar->setContentsMargins(0, 0, 0, 2);
    ui->statusbar->setSizeGripEnabled(false);

    // ── Edit menu ─────────────────────────────────────────────────────────────
    // Shortcuts not set in the .ui file are assigned here so they are also
    // registered on the window (and therefore work even when the menu is hidden).
    ui->actionC_ut->setShortcut(QKeySequence::Cut);
    ui->action_Copy->setShortcut(QKeySequence::Copy);
    // Paste / Delete already have Ctrl+V / Del in the .ui but we add the actions
    // to the window so the shortcuts fire even when the menu bar is hidden.
    addAction(ui->actionC_ut);
    addAction(ui->action_Copy);
    addAction(ui->action_Paste);
    addAction(ui->action_Delete);
    addAction(ui->actionUndo);
    addAction(ui->actionRedo);

    connect(ui->actionUndo,     &QAction::triggered, this, [this] { m_hv->undo();      });
    connect(ui->actionRedo,     &QAction::triggered, this, [this] { m_hv->redo();      });
    connect(ui->actionC_ut,     &QAction::triggered, this, [this] { m_hv->cut();       });
    connect(ui->action_Copy,    &QAction::triggered, this, [this] { m_hv->copy();      });
    connect(ui->action_Paste,   &QAction::triggered, this, [this] { m_hv->paste();     });
    connect(ui->action_Delete,  &QAction::triggered, this, [this] { m_hv->clear();     });
    connect(ui->actionSelect_All, &QAction::triggered, this, [this] { m_hv->selectAll(); });
    connect(ui->actionCopy_As,      &QAction::triggered, this, [this] { CopyAsDlg(m_hv, this); });
    connect(ui->actionPaste_Special,&QAction::triggered, this, [this] { HexPasteSpecialDlg(m_hv, this); });

    // Keep Edit action enabled-states in sync with HexView and clipboard state.
    connect(m_hv, &HexView::selectionChanged, this, [this](size_w, size_w) { updateEditActions(); });
    connect(m_hv, &HexView::contentChanged,   this, [this](size_w, size_w, uint) { updateEditActions(); });
    connect(m_hv, &HexView::editModeChanged,  this, [this](uint) {
        m_statusBar->update();
        updateEditActions();
    });
    auto refreshClipboardState = [this]() {
        const QMimeData *mime = QApplication::clipboard()->mimeData();
        m_canPaste        = mime && (mime->hasFormat("application/x-hexview-snapshot")
                                     || mime->hasFormat("application/octet-stream")
                                     || mime->hasText());
        m_canPasteSpecial = mime && !mime->formats().isEmpty();
        updateEditActions();
    };
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, refreshClipboardState);
    connect(ui->menuEdit, &QMenu::aboutToShow, this, &MainWindow::updateEditActions);
    // Defer the initial clipboard check until the event loop is running so that
    // X11/Wayland clipboard queries can complete (they need event processing).
    QTimer::singleShot(0, this, refreshClipboardState);
    updateEditActions(); // set initial state (flags still false, but enables other actions)

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
        {
            QVector<QColor> swatchColours;
            for (int i = 0; i < 7; ++i)
                swatchColours.append(QColor(m_hv->getHexColour(HvColorSlot(HVC_BOOKMARK1 + i))));
            m_bookmarkDialog->setSwatchColours(swatchColours);
        }
        if (execCentered(m_bookmarkDialog) == QDialog::Accepted) {
            Bookmark bm;
            bm.offset   = m_bookmarkDialog->offset();
            bm.length   = static_cast<size_w>(m_bookmarkDialog->length());
            bm.name     = m_bookmarkDialog->bookmarkName();
            bm.fgColour     = 0; // palette bookmarks auto-contrast their text
            bm.colourIndex  = qMax(0, m_bookmarkDialog->selectedColourIndex());
            m_hv->addBookmark(bm);
            m_gotoDialog->refreshBookmarks();
        }
    });

    connect(m_gotoDialog, &GotoDialog::bookmarkRequested,
            ui->actionBookmark_here, &QAction::trigger);

    connect(m_hv, &HexView::bookmarksChanged,
            m_gotoDialog, &GotoDialog::refreshBookmarks);

    connect(m_hv, &HexView::paneFocusRequested, this, [this]() {
        if (m_findDialog->isVisible())
            m_findDialog->activate();
        else if (m_gotoDialog->isVisible())
            m_gotoDialog->activate();
    });

    connect(ui->actionFind, &QAction::triggered, this, [this]() {
        QWidget *fw = QApplication::focusWidget();
        if (m_findDialog->isVisible() && fw && m_findDialog->isAncestorOf(fw))
            m_findDialog->hide();
        else
            m_findDialog->activate({}, m_hv->activePane());
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

    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::close);

    connect(ui->actionSave, &QAction::triggered, this, [this]() {
        const QString path = m_hv->filePath();
        if (path.isEmpty()) {
            // No path yet — fall through to Save As behaviour.
            const QString dest = QFileDialog::getSaveFileName(this, tr("Save As"));
            if (dest.isEmpty()) return;
            if (!m_hv->saveFile(dest)) return;
            openFile(dest);         // reopen: clears undo, sets filePath, updates title/recent
        } else {
            if (!m_hv->saveFile(path)) return;
            openFile(path);         // reopen: clears undo, resets view state
        }
    });

    connect(ui->actionSave_As, &QAction::triggered, this, [this]() {
        const QString dest = QFileDialog::getSaveFileName(this, tr("Save As"));
        if (dest.isEmpty()) return;
        if (!m_hv->saveFile(dest)) return;
        openFile(dest);             // reopen: clears undo, sets filePath, updates title/recent
    });

    connect(ui->actionNew, &QAction::triggered, this, [this]() {
        if (!maybeSave()) return;
        m_hv->clearFile();
        setWindowTitle(QApplication::applicationName());
    });

    connect(ui->actionOpen, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open File"));
        if (!path.isEmpty())
            openFile(path);
    });

    connect(ui->actionImport, &QAction::triggered, this, [this]() {
        static const QStringList kImportFilters = {
            tr("Raw binary (*.*)"),
            tr("Plain text (*.txt)"),
            tr("Hex string (*.txt)"),
            tr("HTML (*.htm *.html)"),
            tr("C/C++ source (*.c *.cpp *.h)"),
            tr("Assembler source (*.asm *.s)"),
            tr("Intel Hex Records (*.hex)"),
            tr("Motorola S-Records (*.s19 *.s28 *.s37)"),
            tr("Base64 (*.b64 *.txt)"),
            tr("UUEncode (*.uue *.txt)"),
        };

        QFileDialog dlg(this, tr("Import"));
        dlg.setAcceptMode(QFileDialog::AcceptOpen);
        dlg.setFileMode(QFileDialog::ExistingFile);
        dlg.setNameFilters(kImportFilters);
        dlg.selectNameFilter(kImportFilters.value((int)g_ImportOptions.format));

        if (dlg.exec() != QDialog::Accepted) return;
        const QString path = dlg.selectedFiles().value(0);
        if (path.isEmpty()) return;

        const int idx = kImportFilters.indexOf(dlg.selectedNameFilter());
        if (idx >= 0)
            g_ImportOptions.format = (IMPEXP_FORMAT)idx;

        ImportFile(path, m_hv, &g_ImportOptions);
    });

    connect(ui->actionExport, &QAction::triggered, this, [this]() {
        // Filter order must match IMPEXP_FORMAT enum values (0–9)
        static const QStringList kExportFilters = {
            tr("Raw binary (*.*)"),
            tr("Plain text (*.txt)"),
            tr("Hex string (*.txt)"),
            tr("HTML (*.htm *.html)"),
            tr("C/C++ source (*.c *.cpp *.h)"),
            tr("Assembler source (*.asm *.s)"),
            tr("Intel Hex Records (*.hex)"),
            tr("Motorola S-Records (*.s19 *.s28 *.s37)"),
            tr("Base64 (*.b64 *.txt)"),
            tr("UUEncode (*.uue *.txt)"),
        };

        QFileDialog dlg(this, tr("Export"));
        dlg.setAcceptMode(QFileDialog::AcceptSave);
        dlg.setNameFilters(kExportFilters);
        dlg.selectNameFilter(kExportFilters.value((int)g_ExportOptions.format));

        if (dlg.exec() != QDialog::Accepted)
            return;

        const QString path = dlg.selectedFiles().value(0);
        if (path.isEmpty())
            return;

        const int idx = kExportFilters.indexOf(dlg.selectedNameFilter());
        if (idx >= 0)
            g_ExportOptions.format = (IMPEXP_FORMAT)idx;

        Export(path, m_hv, &g_ExportOptions);
    });

    m_prefsDialog = new PreferencesDialog(this);
    connect(m_prefsDialog, &PreferencesDialog::fontChanged,
            this, [this](const QFont &font) {
        m_hv->setFont(font, AppSettings::prefHorizSpacing(), AppSettings::prefLineSpacing());
    });
    connect(m_prefsDialog, &PreferencesDialog::fontSpacingChanged,
            this, [this](int hSpacing, int lineSpacing) {
        m_hv->setFont(m_hv->font(), hSpacing, lineSpacing);
    });
    connect(m_prefsDialog, &PreferencesDialog::nativeMenuChanged,
            this, [this](bool native) { applyMenuMode(!native); });
    connect(m_prefsDialog, &PreferencesDialog::menuHighlightChanged,
            this, [this](bool) { applyAdwaitaTheme(static_cast<ColorScheme>(AppSettings::prefColorScheme())); });
    connect(m_prefsDialog, &PreferencesDialog::paletteSelected,
            this, [this](const PaletteInfo &info) {
        m_currentPalette = info;
        applyPalette(m_hv, info);
        applyUiPalette(info);
        m_titleBar->refreshStylesheet();
        AppSettings::setPrefPaletteName(info.name);
    });
    connect(ui->actionPreferences, &QAction::triggered, this, [this]() {
        m_prefsDialog->prepareShow(); // pre-create HWND at correct pos before show()
        m_prefsDialog->show();
        m_prefsDialog->raise();
        m_prefsDialog->activateWindow();
    });
    connect(ui->actionAbout, &QAction::triggered, this, [this]() {
        ShowAboutDlg(this);
    });

    // Apply saved font (family + size + spacing)
    const QString savedFamily = AppSettings::prefFontFamily();
    if (!savedFamily.isEmpty())
        m_hv->setFont(QFont(savedFamily, AppSettings::prefFontSize()),
                      AppSettings::prefHorizSpacing(), AppSettings::prefLineSpacing());

    // Apply saved palette
    const QString savedPalette = AppSettings::prefPaletteName();
    if (!savedPalette.isEmpty()) {
        QList<PaletteInfo> palettes = loadAllPalettes();
        for (const PaletteInfo &info : palettes) {
            if (info.name == savedPalette) {
                m_currentPalette = info;
                applyPalette(m_hv, info);
                applyUiPalette(info);
                m_titleBar->refreshStylesheet();
                break;
            }
        }
    }

    // Apply rounded corners / DWM shadow / compact style to the native QMenuBar
    // drop-down menus so they match the custom-titlebar menus.  Must happen
    // before first show so the window flags take effect.
    {
        std::function<void(QMenu *)> themeRecursive = [&](QMenu *m) {
            themeMenu(m);
            for (QAction *a : m->actions())
                if (QMenu *sub = a->menu())
                    themeRecursive(sub);
        };
        for (QAction *a : ui->menubar->actions())
            if (QMenu *m = a->menu())
                themeRecursive(m);
    }

    // Apply saved menu mode (may switch away from the default custom titlebar)
    applyMenuMode(!AppSettings::prefNativeMenu());

    // Edge-resize event filter: catches mouse events on any child widget
    qApp->installEventFilter(this);
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::updateEditActions()
{
    const bool hasSel     = m_hv->selectionSize() > 0;
    const bool insertMode = m_hv->editMode() == HVMODE_INSERT;
    const bool readOnly   = m_hv->editMode() == HVMODE_READONLY;

    // Undo / Redo reflect the sequence's event stack.
    ui->actionUndo->setEnabled(m_hv->canUndo());
    ui->actionRedo->setEnabled(m_hv->canRedo());

    // Cut requires a selection AND insert mode (overwrite can't remove bytes).
    ui->actionC_ut->setEnabled(hasSel && insertMode);

    // Delete: insert mode — always on; overwrite — selection required; read-only — off.
    ui->action_Delete->setEnabled(!readOnly && (insertMode || hasSel));

    // Copy / Copy As only need a selection.
    ui->action_Copy->setEnabled(hasSel);
    ui->actionCopy_As->setEnabled(hasSel);

    // Paste / Paste Special: use cached clipboard state (m_clipboardReady), updated
    // from dataChanged where clipboard queries are safe.  Querying mimeData() here
    // (from aboutToShow) can fail on X11/Wayland because the popup-window event
    // grab blocks the clipboard round-trip.
    const bool writable = m_hv->editMode() != HVMODE_READONLY;
    ui->action_Paste->setEnabled(writable && m_canPaste);
    ui->actionPaste_Special->setEnabled(writable && m_canPasteSpecial);
}

bool MainWindow::maybeSave()
{
    if (!m_hv->canUndo())
        return true;

    const QString name = m_hv->filePath().isEmpty()
                         ? tr("Untitled")
                         : QFileInfo(m_hv->filePath()).fileName();

    QMessageBox mb(QMessageBox::Warning,
                   tr("Unsaved changes"),
                   tr("Save changes to \"%1\"?").arg(name),
                   QMessageBox::NoButton,
                   this);
    QPushButton*saveBtn    = mb.addButton(tr("Save"),    QMessageBox::AcceptRole);
    QPushButton*discardBtn = mb.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    QPushButton*cancelBtn  = mb.addButton(tr("Cancel"),  QMessageBox::RejectRole);
    mb.setDefaultButton(saveBtn);
    styleMessageBox(&mb);

    mb.exec();
    QPushButton *clicked = qobject_cast<QPushButton*>(mb.clickedButton());

    if (!clicked || clicked == cancelBtn)
        return false;                       // Cancel (or dialog closed via Esc)

    if (clicked == saveBtn) {
        const QString path = m_hv->filePath();
        if (!path.isEmpty()) {
            // Real file on disk — overwrite directly
            if (!m_hv->saveFile(path))
                return false;
        } else {
            // Untitled / memory-backed — ask for a filename
            const QString dest = QFileDialog::getSaveFileName(this, tr("Save As"));
            if (dest.isEmpty())
                return false;
            if (!m_hv->saveFile(dest))
                return false;
        }
    }
    Q_UNUSED(discardBtn); // fall-through: discard changes and continue

    return true;
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    QMainWindow::closeEvent(e);
}

void MainWindow::openFile(const QString &path) {
    m_hv->openFile(path);
    AppSettings::addRecentFile(path);
    updateRecentMenu();
    setWindowTitle(QFileInfo(path).fileName() + " \u2013 " + QApplication::applicationName());
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
    if (m_lastPattern.isEmpty()) {
        // No prior search — initialise from whatever is in the find field.
        const QByteArray pat = m_findDialog->buildPattern();
        if (pat.isEmpty())
            return;
        m_hv->findInit(reinterpret_cast<const uint8_t *>(pat.constData()),
                       (size_t)pat.size());
        execFind(pat, forward ? 0 : HVFF_BACKWARD);
        return;
    }
    uint flags = m_lastFindFlags | (forward ? 0 : HVFF_BACKWARD);
    execFind(m_lastPattern, flags);
}

void MainWindow::applyMenuMode(bool useCustomTitleBar)
{
    m_useCustomTitleBar = useCustomTitleBar;

    // TitleBar lives inside the central-widget layout; the standard QMenuBar
    // lives in its normal QMainWindow position.  We never swap menu widgets at
    // runtime (doing so causes Qt to reparent and orphan widgets, which crashes).
    // Instead we just show/hide each side and toggle the window frame flag.
    if (useCustomTitleBar) {
        ui->menubar->hide();
        m_titleBar->show();
        if (m_titleHairline) {
            m_titleHairline->show();
            m_titleHairline->setBgSource(m_titleBar);
        }
    } else {
        m_titleBar->hide();
        ui->menubar->show();
        if (m_titleHairline) {
            m_titleHairline->show();
            m_titleHairline->setBgSource(ui->menubar);
        }
    }

#ifndef Q_OS_WIN
    // Changing FramelessWindowHint requires the platform plugin to destroy and
    // recreate the native window.  On Linux (xcb / Wayland) doing this at
    // runtime while a child dialog is open causes a hard platform crash that
    // the C++ debugger cannot intercept.  Apply the flag only before the window
    // is first shown; at runtime the frame stays as-is and takes full effect on
    // the next launch.
    //
    // WA_TranslucentBackground must be set together with FramelessWindowHint:
    // it lets transparent corner pixels show through.  paintEvent() fills only
    // a rounded rect within the shadow margin, so the corner pixels stay
    // transparent — giving smooth antialiased corners on any ARGB compositor.
    if (!isVisible()) {
        setWindowFlag(Qt::FramelessWindowHint, useCustomTitleBar);
        setAttribute(Qt::WA_TranslucentBackground, useCustomTitleBar);
    }

    // KDE only: expand the shadow margin and manage the CornerClipper overlay.
    // On non-KDE (GNOME, …) the compositor provides the shadow — applyShadowMargin()
    // is a no-op there and we skip the CornerClipper entirely.
    applyShadowMargin();
    if (isKDE()) {
        if (useCustomTitleBar && !m_cornerClipper)
            m_cornerClipper = new CornerClipper(this);
        if (m_cornerClipper)
            m_cornerClipper->setVisible(useCustomTitleBar);
    }
#else
    // On Windows the window is never recreated; we just tell DWM to
    // re-evaluate the NC area so nativeEvent() starts or stops collapsing it.
    if (isVisible())
        SetWindowPos(reinterpret_cast<HWND>(winId()), nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
#endif
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    auto *w = qobject_cast<QWidget *>(obj);
    if (!w || w->window() != this)
        return false;

    // Edge-resize is handled by the custom titlebar only; the native window
    // frame takes care of it in standard menu mode.
    if (!m_useCustomTitleBar)
        return false;

    const auto type = event->type();

    // ── Cursor feedback on hover ─────────────────────────────────────────────
    auto syncResizeCursor = [&](QPointF globalPos) {
        Qt::Edges edges =
            isMaximized()
            ? Qt::Edges{}
#ifndef Q_OS_WIN
            : edgesFromPos(mapFromGlobal(globalPos.toPoint()), rect(),
                           isKDE() ? kShadowSize : kResizeMargin);
#else
            : edgesFromPos(mapFromGlobal(globalPos.toPoint()), rect(), RESIZE_MARGIN);
#endif

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
    };

    if (type == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(event);
        syncResizeCursor(me->globalPosition());
        return false; // don't consume — just update cursor
    }

    // Enter events fire regardless of mouse-tracking, so they catch transitions
    // to widgets like TitleBar buttons where MouseMove is never generated.
    // Without this, a stale override cursor persists after the mouse leaves a
    // resize-zone edge and enters a non-tracked widget.
    if (type == QEvent::Enter) {
        auto *ee = static_cast<QEnterEvent *>(event);
        syncResizeCursor(ee->globalPosition());
        return false;
    }

    // Restore cursor if mouse leaves the window entirely
    if (type == QEvent::Leave && obj == this && m_inResizeZone) {
        m_inResizeZone = false;
        QApplication::restoreOverrideCursor();
        return false;
    }

    // Clear any stale override cursor when the main window regains focus
    // (e.g. after a dialog closes without the mouse having left the resize zone).
    if (type == QEvent::WindowActivate && obj == this) {
        if (m_inResizeZone) {
            m_inResizeZone = false;
            QApplication::restoreOverrideCursor();
        }
        return false;
    }

    // ── Start resize on click at window edge ─────────────────────────────────
    if (type == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton || isMaximized())
            return false;

        Qt::Edges edges =
#ifndef Q_OS_WIN
            edgesFromPos(mapFromGlobal(me->globalPosition().toPoint()), rect(),
                         isKDE() ? kShadowSize : kResizeMargin);
#else
            edgesFromPos(mapFromGlobal(me->globalPosition().toPoint()), rect(),
                         RESIZE_MARGIN);
#endif
        if (!edges)
            return false;

        windowHandle()->startSystemResize(edges);
        return true;
    }

    return false;
}

void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
#ifdef Q_OS_WIN
    if (m_useCustomTitleBar) {
        bool dark = palette().window().color().lightness() < 128;
        applyWindows11Styling(reinterpret_cast<HWND>(winId()), dark);
        updateWinChromeColors();
    }
#else
    applyShadowMargin();
    // Force the CornerClipper to repaint after the initial layout pass so it
    // punches the corners on first display.  The deferred timer lets all child
    // widgets finish their first paint before we clear.
    if (m_cornerClipper) {
        m_cornerClipper->raise();
        QTimer::singleShot(0, m_cornerClipper, [this] {
            if (m_cornerClipper) { m_cornerClipper->raise(); m_cornerClipper->update(); }
        });
    }
#endif
}

#ifndef Q_OS_WIN
void MainWindow::paintEvent(QPaintEvent *)
{
    if (!m_useCustomTitleBar) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    if (!isKDE()) {
        // Non-KDE (GNOME, …): the compositor provides the shadow.  Just paint
        // the window background as a rounded rect so the corner pixels stay
        // transparent — that gives the window its rounded shape.
        p.setBrush(palette().window());
        if (isMaximized() || isFullScreen())
            p.drawRect(rect());
        else
            p.drawRoundedRect(rect(), kCornerRadius, kCornerRadius);
        return;
    }

    // KDE: self-drawn drop shadow (Firefox / GTK CSD style).
    // The window is expanded by kShadowSize on all sides (via setContentsMargins
    // in applyShadowMargin).  We paint a gradient shadow into that transparent
    // margin; KWin composites it normally — no KWin API required.
    const QRectF full    = QRectF(rect());
    const QRectF content = full.adjusted(kShadowSize, kShadowSize,
                                         -kShadowSize, -kShadowSize);

    if (isMaximized() || isFullScreen()) {
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.setBrush(palette().window());
        p.drawRect(full);
        return;
    }

    p.setCompositionMode(QPainter::CompositionMode_Source);
    for (int dist = kShadowSize; dist >= 1; --dist) {
        const qreal t     = qreal(kShadowSize - dist) / (kShadowSize - 1);
        const int   alpha = int(80 * t * t);
        const QRectF r    = content.adjusted(-dist, -dist, dist, dist);
        const qreal  rad  = kCornerRadius + dist;
        p.setBrush(QColor(0, 0, 0, alpha));
        p.drawRoundedRect(r, rad, rad);
    }

    p.setBrush(palette().window());
    p.drawRoundedRect(content, kCornerRadius, kCornerRadius);
}

void MainWindow::applyShadowMargin()
{
    // KDE only: expand the window contents by kShadowSize on all sides so
    // paintEvent() can paint a gradient shadow in the transparent margin.
    // On non-KDE compositors (GNOME, …) the WM provides the shadow; adding a
    // margin would cause a double shadow, so leave margins at zero.
    if (!isKDE()) return;
    if (m_useCustomTitleBar && !isMaximized() && !isFullScreen())
        setContentsMargins(kShadowSize, kShadowSize, kShadowSize, kShadowSize);
    else
        setContentsMargins(0, 0, 0, 0);
}
#endif

void MainWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::ActivationChange && m_useCustomTitleBar) {
        // Refresh title bar so it picks up the new focused/unfocused colour.
        m_titleBar->refreshStylesheet();
#ifdef Q_OS_WIN
        updateWinChromeColors();
#endif
    }
#ifndef Q_OS_WIN
    if (e->type() == QEvent::WindowStateChange && m_useCustomTitleBar) {
        applyShadowMargin();
        if (m_cornerClipper)
            m_cornerClipper->update();
    }
#endif
}

#ifdef Q_OS_WIN
// Updates the status bar and inline dialog backgrounds to match the title bar
// chrome colour, switching between the focused (Mica/accent) and unfocused
// (neutral grey) states to mirror Windows 11's native window behaviour.
void MainWindow::updateWinChromeColors()
{
    const bool   active = isActiveWindow();

    // Prefer the toolbar colour override (status-bar / chrome-panel specific).
    // Fall back to windowsChromeBg() which handles the window-panel override and
    // the hardcoded Win11 neutrals.  The same active/inactive dimming ratios used
    // in windowsChromeBg() are applied here (3 % darker for light, 41 % lighter
    // for dark) so focus/unfocus transitions look consistent.
    const QColor tbOver = uiColourOverrides().toolbar;
    const QColor bg     = tbOver.isValid()
        ? (active ? tbOver
                  : (tbOver.lightness() >= 128 ? tbOver.darker(103)
                                               : tbOver.lighter(141)))
        : windowsChromeBg(active);

    const bool  dark          = bg.lightness() < 128;
    const QColor comboHoverBg = dark ? bg.lighter(130) : bg.darker(107);

    // Status bar palette drives both the bar background and the combo colours:
    //   Window role → QStatusBar { background: palette(window); }            (normal)
    //                  QStatusBar QComboBox { background: palette(window); }  (normal)
    //   Button role → QStatusBar QComboBox:hover { background: palette(button); }
    //                  QStatusBar QComboBox:focus { background: palette(button); }
    // Both roles resolve from this palette because all QSS rules use descendant
    // selectors ("QStatusBar QComboBox …"), so Qt looks up palette() on the
    // ancestor (status bar), not the combo itself.
    QPalette sbPal;
    sbPal.setColor(QPalette::Window, bg);
    sbPal.setColor(QPalette::Button, comboHoverBg);
    ui->statusbar->setPalette(sbPal);
    ui->statusbar->setStyleSheet(QString());

    // FindDialog and GotoDialog use WA_StyledBackground — updating their
    // Window palette role is enough to repaint without touching their stylesheets.
    QPalette p;
    p.setColor(QPalette::Window, bg);
    m_findDialog->setPalette(p);
    m_gotoDialog->setPalette(p);
}
#endif

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    MSG *msg = reinterpret_cast<MSG *>(message);
    if (m_useCustomTitleBar && msg->message == WM_NCCALCSIZE && msg->wParam == TRUE) {
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
