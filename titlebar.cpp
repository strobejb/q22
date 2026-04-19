#include "titlebar.h"
#include "theme.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QShowEvent>
#include <QPalette>
#include <QStyle>
#include <QToolButton>
#include <QWindow>

#ifdef Q_OS_LINUX
#include <QProcess>
#include <QProcessEnvironment>
#endif

#ifdef Q_OS_WIN

// Returns the Windows 11 chrome background for the given activation state.
// Active:   #F3F3F3 (light) / #202020 (dark) — neutral white/light-grey.
// Inactive: #EBEBEB (light) / #2D2D2D (dark) — slightly dimmed neutral.
QColor windowsChromeBg(bool active)
{
    const bool light = QApplication::palette().window().color().lightness() >= 128;
    return active ? (light ? QColor(0xF3, 0xF3, 0xF3) : QColor(0x20, 0x20, 0x20))
                  : (light ? QColor(0xEB, 0xEB, 0xEB) : QColor(0x2D, 0x2D, 0x2D));
}

#endif

// ── Platform: detect GNOME button layout ─────────────────────────────────────

// Returns a layout string like ":minimize,maximize,close" or "close:"
// Falls back to a sensible default if gsettings is unavailable.
static QString platformButtonLayout()
{
#ifdef Q_OS_LINUX
    // Only bother querying if we're actually inside a GNOME session
    const QString desktop = QProcessEnvironment::systemEnvironment()
                                .value("XDG_CURRENT_DESKTOP").toLower();
    if (desktop.contains("gnome") || desktop.contains("unity")) {
        QProcess proc;
        proc.start("gsettings",
                   {"get", "org.gnome.desktop.wm.preferences", "button-layout"});
        if (proc.waitForFinished(500) && proc.exitCode() == 0) {
            QString s = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            s.remove('\''); // gsettings wraps string values in single-quotes
            if (!s.isEmpty())
                return s;
        }
    }
#endif
    return ":minimize,maximize,close"; // universal fallback
}

static void parseButtonLayout(const QString &layout,
                               QStringList &left, QStringList &right)
{
    auto split = [](const QString &s) {
        return s.split(',', Qt::SkipEmptyParts);
    };
    int colon = layout.indexOf(':');
    if (colon < 0) {
        right = split(layout);
    } else {
        left  = split(layout.left(colon));
        right = split(layout.mid(colon + 1));
    }
}

// ── TitleBar ──────────────────────────────────────────────────────────────────

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    // Match the system toolbar height rather than hardcoding.
    // PM_ToolBarIconSize is 24 px on GNOME/Adwaita, giving ~46 px total —
    // the same as an Adwaita header bar.  On KDE it scales similarly.
    const int iconSz    = QApplication::style()->pixelMetric(QStyle::PM_ToolBarIconSize);
    int barH            = qMax(36, iconSz + 22);   // 46 on GNOME, ≥36 everywhere
#ifdef Q_OS_WIN
    // Windows Fluent apps typically use ~40 px title bars (tighter than the
    // GNOME 46 px formula, wider than the bare system-caption minimum ~31 px).
    barH = qMax(36, iconSz + 16);   // 24+16 = 40 px at standard DPI
#endif
    // All buttons leave ~6 px margin top/bottom so they sit centred in the bar.
    // 16 px symbolic icons are the standard for header-bar buttons on GNOME/KDE.
    const int btnSz        = barH - 12;    // ~34 px on GNOME
    const int btnIconSz    = 16;           // window-control glyphs / standard icons
    const int menuBtnIconSz = 14;          // file / search / tools — one step smaller
    const int btnRadius    = 6;            // rounded-rect, not circular

    setFixedHeight(barH);
    setObjectName("TitleBar");

#ifdef Q_OS_WIN
    {
        const QColor winBg = windowsChromeBg(true);
        QPalette pal = palette();
        pal.setColor(QPalette::Window, winBg);
        setPalette(pal);
        setAutoFillBackground(true);
        setAttribute(Qt::WA_StyledBackground, true);
    }
#endif

    m_btnRadius = btnRadius;

    // ── Hamburger button (always far-left) ────────────────────────────────
    m_menu = new QMenu(this);
    themeMenu(m_menu);

    m_hamburger = new QToolButton(this);
    m_hamburger->setObjectName("hamburger");
    // Both platforms use the file-open icon: the button opens the File menu
    // and should visually match the Open action inside it.
#if 0//def Q_OS_WIN
    // 0xED25 = FolderOpen in Segoe MDL2 Assets / Segoe Fluent Icons —
    // same monochrome Segoe style as the search and caption buttons.
    if (QIcon si = segoeIcon(0xED25, palette().windowText().color(), menuBtnIconSz); !si.isNull())
        m_hamburger->setIcon(si);
    else
        m_hamburger->setIcon(
            QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
#else
    {
        QIcon hamburgerIcon = QIcon::fromTheme("document-open-symbolic");
        if (!hamburgerIcon.isNull())
            m_hamburger->setIcon(hamburgerIcon);
        else
            m_hamburger->setIcon(
                QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
    }
#endif
    m_hamburger->setFocusPolicy(Qt::NoFocus);
    m_hamburger->setAutoRaise(true);
#ifdef Q_OS_WIN
    m_hamburger->setFixedSize(40, barH);
#else
    m_hamburger->setFixedSize(btnSz, btnSz);
#endif
    m_hamburger->setIconSize(QSize(menuBtnIconSz, menuBtnIconSz));
    m_hamburger->setMenu(m_menu);
    m_hamburger->setPopupMode(QToolButton::InstantPopup);

    // ── Search button (left side, next to hamburger) ──────────────────────
    m_searchMenu = new QMenu(this);
    themeMenu(m_searchMenu);

    m_searchBtn = new QToolButton(this);
    m_searchBtn->setObjectName("searchBtn");
    QIcon searchIcon = QIcon::fromTheme("edit-find-symbolic");
    if (!searchIcon.isNull())
        m_searchBtn->setIcon(searchIcon);
#ifdef Q_OS_WIN
    else if (QIcon si = segoeIcon(0xE721, palette().windowText().color(), menuBtnIconSz); !si.isNull()) // Search
        m_searchBtn->setIcon(si);
#endif
    else
        m_searchBtn->setText("🔍");
    m_searchBtn->setFocusPolicy(Qt::NoFocus);
    m_searchBtn->setAutoRaise(true);
#ifdef Q_OS_WIN
    m_searchBtn->setFixedSize(40, barH);
#else
    m_searchBtn->setFixedSize(btnSz, btnSz);
#endif
    m_searchBtn->setIconSize(QSize(menuBtnIconSz, menuBtnIconSz));
    m_searchBtn->setMenu(m_searchMenu);
    m_searchBtn->setPopupMode(QToolButton::InstantPopup);

    // ── Title label ───────────────────────────────────────────────────────
    m_title = new QLabel(this);
    m_title->setText(window()->windowTitle());
    m_title->setAlignment(Qt::AlignCenter);

    // ── Detect layout and build window-control buttons ────────────────────
    QStringList leftBtns, rightBtns;
    parseButtonLayout(platformButtonLayout(), leftBtns, rightBtns);

    auto *leftGroup  = new QWidget(this);
    auto *rightGroup = new QWidget(this);
    auto *leftLay    = new QHBoxLayout(leftGroup);
    auto *rightLay   = new QHBoxLayout(rightGroup);
    leftLay ->setContentsMargins(0,0,0,0);  leftLay ->setSpacing(2);
    rightLay->setContentsMargins(0,0,0,0);  rightLay->setSpacing(2);
#ifdef Q_OS_WIN
    // Windows 11: caption buttons sit flush against each other with no gap.
    leftLay ->setSpacing(0);
    rightLay->setSpacing(0);
#endif

    addWindowButtons(leftLay,  leftBtns);
    addWindowButtons(rightLay, rightBtns);

    // ── View menu button (right side, left of window controls) ───────────
    m_viewMenu = new QMenu(this);
    themeMenu(m_viewMenu);

    m_viewBtn = new QToolButton(this);
    m_viewBtn->setObjectName("viewMenu");
#if 0//def Q_OS_WIN
    // Same approach as m_hamburger and the Find dialog options button.
    m_viewBtn->setText("☰");
#else
    {
        QIcon viewIcon = QIcon::fromTheme("open-menu-symbolic");
        if (!viewIcon.isNull())
            m_viewBtn->setIcon(viewIcon);
        else
            m_viewBtn->setText("☰");
    }
#endif
    m_viewBtn->setFocusPolicy(Qt::NoFocus);
    m_viewBtn->setAutoRaise(true);
#ifdef Q_OS_WIN
    m_viewBtn->setFixedSize(40, barH);
#else
    m_viewBtn->setFixedSize(btnSz, btnSz);
#endif
    m_viewBtn->setIconSize(QSize(menuBtnIconSz, menuBtnIconSz));
    m_viewBtn->setMenu(m_viewMenu);
    m_viewBtn->setPopupMode(QToolButton::InstantPopup);
    // Reposition to right-align when the menu is shown.
    m_viewMenu->installEventFilter(this);

    // ── Main layout ───────────────────────────────────────────────────────
    auto *layout = new QHBoxLayout(this);
#ifdef Q_OS_WIN
    // Flush to the window frame edges so the leftmost and rightmost button hover
    // backgrounds extend all the way to the frame border with no gap.
    layout->setContentsMargins(0, 0, 0, 0);
#else
    layout->setContentsMargins(6, 0, 6, 0);
#endif
    layout->setSpacing(4);
    layout->addWidget(m_hamburger);
    layout->addWidget(leftGroup);
    layout->addStretch();
    layout->addWidget(m_title);
    layout->addStretch();
    layout->addWidget(m_searchBtn);
    layout->addWidget(m_viewBtn);
    layout->addWidget(rightGroup);

    // WindowTitleChange is delivered to the top-level window, not to child
    // widgets.  Watch the parent window so we can update our label.
    if (parent)
        parent->installEventFilter(this);

    refreshStylesheet();
}

// Creates a window-control button for the given name ("close", "minimize",
// "maximize") and registers it in m_btnMin / m_btnMax / m_btnClose.
QToolButton *TitleBar::makeWindowButton(const QString &name)
{
    struct Spec { const char *icon; const char *fallback; const char *objName; };
    static const QMap<QString, Spec> specs = {
        {"close",    {"window-close-symbolic",    "✕", "close"}},
        {"minimize", {"window-minimize-symbolic", "─", "minimize"}},
        {"maximize", {"window-maximize-symbolic", "□", "maximize"}},
    };

    if (!specs.contains(name))
        return nullptr;

    const Spec &s = specs[name];
    auto *btn = new QToolButton(this);
    btn->setObjectName(s.objName);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setAutoRaise(true);
#ifdef Q_OS_WIN
    // Windows 11: caption buttons are full title-bar height, ~46 px wide,
    // flush with no gap between them (set in the layout above).
    btn->setFixedSize(46, height());
    btn->setIconSize(QSize(10, 10));
#else
    {
        const int iconSz = QApplication::style()->pixelMetric(QStyle::PM_ToolBarIconSize);
        const int sz     = qMax(36, iconSz + 22) - 12;   // same formula as constructor
        btn->setFixedSize(sz, sz);
        btn->setIconSize(QSize(16, 16));
    }
#endif

#ifdef Q_OS_WIN
    // Use setText() with the raw Segoe glyph rather than a pre-rendered
    // pixmap icon.  The stylesheet sets font-family to Segoe MDL2/Fluent so
    // the glyph renders correctly, and CSS color: applies to text — which is
    // how the close button icon turns white over the red hover background.
    {
        static const QMap<QString, uint> segoeGlyphs = {
            {"close",    0xE8BB},   // ChromeClose
            {"minimize", 0xE921},   // ChromeMinimize
            {"maximize", 0xE922},   // ChromeMaximize
        };
        if (segoeGlyphs.contains(name))
            btn->setText(QString(QChar(segoeGlyphs[name])));
        else
            btn->setText(s.fallback);
    }
#else
    {
        QIcon icon = QIcon::fromTheme(s.icon);
        if (!icon.isNull())
            btn->setIcon(icon);
        else
            btn->setText(s.fallback);
    }
#endif

    if (name == "close") {
        m_btnClose = btn;
        connect(btn, &QToolButton::clicked, this, [this] { window()->close(); });
    } else if (name == "minimize") {
        m_btnMin = btn;
        connect(btn, &QToolButton::clicked, this, [this] { window()->showMinimized(); });
    } else if (name == "maximize") {
        m_btnMax = btn;
        connect(btn, &QToolButton::clicked, this, [this] {
            window()->isMaximized() ? window()->showNormal() : window()->showMaximized();
        });
    }

    return btn;
}

void TitleBar::addWindowButtons(QHBoxLayout *layout, const QStringList &names)
{
    for (const QString &name : names) {
        if (QToolButton *btn = makeWindowButton(name.trimmed()))
            layout->addWidget(btn);
    }
}

void TitleBar::refreshStylesheet()
{
    bool dark   = QApplication::palette().window().color().lightness() < 128;
    QString bg      = dark ? "#3d3846" : "#e8e8e8";
    QString fg      = dark ? "#ffffff" : "#2e3436";
    QString hover   = dark ? "rgba(255,255,255,0.15)" : "rgba(0,0,0,0.10)";
    QString border  = dark ? "#1e1e2e" : "#c4c4c4";

#ifdef Q_OS_WIN
    {
        bool active        = window() ? window()->isActiveWindow() : true;
        const QColor winBg = windowsChromeBg(active);
        dark   = winBg.lightness() < 128;
        bg     = winBg.name();
        fg     = dark ? "#ffffff" : "#000000";
        hover  = dark ? "rgba(255,255,255,0.08)" : "rgba(0,0,0,0.06)";
        border = dark ? "rgba(255,255,255,0.10)" : "rgba(0,0,0,0.12)";

        QPalette pal = palette();
        pal.setColor(QPalette::Window, winBg);
        setPalette(pal);
    }
#endif

    // Layer UI palette colour overrides on top of platform defaults.
    const UiColourOverrides &uiOvr = uiColourOverrides();
    if (uiOvr.window.isValid()) {
        dark   = uiOvr.window.lightness() < 128;
        bg     = uiOvr.window.name();
        hover  = dark ? "rgba(255,255,255,0.15)" : "rgba(0,0,0,0.10)";
        border = dark ? "rgba(255,255,255,0.20)" : "rgba(0,0,0,0.12)";
        QPalette pal = palette();
        pal.setColor(QPalette::Window, uiOvr.window);
        setPalette(pal);
        setAutoFillBackground(true);
    }
    if (uiOvr.windowText.isValid())
        fg = uiOvr.windowText.name();

    setStyleSheet(QString(R"(
        #TitleBar {
            background-color: %1;
            /* border-bottom: 1px solid %5; */
        }
        #TitleBar QLabel { color: %2; font-weight: 600; }
        #TitleBar QToolButton {
            border: none;
            border-radius: %6px;
            background: transparent;
            color: %2;
            font-size: 13px;
        }
        #TitleBar QToolButton:hover   { background: %3; }
        #TitleBar QToolButton:pressed { background: %4; }
        #TitleBar QToolButton#hamburger::menu-indicator  { image: none; width: 0; }
        #TitleBar QToolButton#viewMenu::menu-indicator   { image: none; width: 0; }
        #TitleBar QToolButton#searchBtn::menu-indicator  { image: none; width: 0; }
    )").arg(bg, fg, hover,
            dark ? "rgba(255,255,255,0.25)" : "rgba(0,0,0,0.18)",
            border,
            QString::number(m_btnRadius)));

#ifdef Q_OS_WIN
    setStyleSheet(styleSheet() + R"(
        #TitleBar QToolButton#hamburger,
        #TitleBar QToolButton#searchBtn,
        #TitleBar QToolButton#viewMenu  { border-radius: 0; }

        #TitleBar QToolButton#close,
        #TitleBar QToolButton#minimize,
        #TitleBar QToolButton#maximize  {
            border-radius: 0;
            font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets";
            font-size: 10px;
        }
        #TitleBar QToolButton#close:hover   { background: #c42b1c; color: white; }
        #TitleBar QToolButton#close:pressed { background: #9a1c10; color: white; }
    )");
#endif
    // Recolor symbolic icons to match the current foreground color.
    // QIcon::fromTheme() returns uncolored pixmaps; we tint via alpha compositing.
    {
        const QColor fgColor(fg);
        auto recolor = [&](QToolButton *btn, const QString &name, int sz = 16) {
            if (!btn) return;
            QIcon ic = recoloredIcon(name, fgColor, sz);
            if (!ic.isNull()) btn->setIcon(ic);
        };
        recolor(m_hamburger, "document-open-symbolic",  14);
        recolor(m_searchBtn,  "edit-find-symbolic",      14);
        recolor(m_viewBtn,    "open-menu-symbolic",      14);
#ifndef Q_OS_WIN
        // Caption buttons on Windows use Segoe text glyphs, not icons.
        recolor(m_btnClose,   "window-close-symbolic");
        recolor(m_btnMin,     "window-minimize-symbolic");
        if (m_btnMax) {
            bool maximized = window() && window()->isMaximized();
            recolor(m_btnMax, maximized ? "window-restore-symbolic" : "window-maximize-symbolic");
        }
#endif
    }
}

void TitleBar::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::PaletteChange) {
        // Recolor icons only — do NOT call refreshStylesheet() here, since that
        // calls setStyleSheet() which modifies the widget palette (via the color:
        // property) and re-fires PaletteChange, causing infinite recursion.
        // The full stylesheet is updated via explicit refreshStylesheet() calls
        // at the mainwindow level whenever the colour scheme actually changes.
        const QColor fg = palette().windowText().color();
        auto recolor = [&](QToolButton *btn, const QString &name, int sz = 16) {
            if (!btn) return;
            QIcon ic = recoloredIcon(name, fg, sz);
            if (!ic.isNull()) btn->setIcon(ic);
        };
        recolor(m_hamburger, "document-open-symbolic",  14);
        recolor(m_searchBtn,  "edit-find-symbolic",      14);
        recolor(m_viewBtn,    "open-menu-symbolic",      14);
#ifndef Q_OS_WIN
        // Caption buttons on Windows use Segoe text glyphs, not icons.
        recolor(m_btnClose,   "window-close-symbolic");
        recolor(m_btnMin,     "window-minimize-symbolic");
        if (m_btnMax) {
            const bool maximized = window() && window()->isMaximized();
            recolor(m_btnMax, maximized ? "window-restore-symbolic" : "window-maximize-symbolic");
        }
#endif
    }
    QWidget::changeEvent(e);
}

void TitleBar::setHamburgerMenu(QMenu *menu)
{
    themeMenu(menu);
    m_hamburger->setMenu(menu);
    m_menu = menu;
}

void TitleBar::setSearchMenu(QMenu *menu)
{
    themeMenu(menu);
    m_searchBtn->setMenu(menu);
    m_searchMenu = menu;
    m_searchMenu->installEventFilter(this);
}

// Switch maximize ↔ restore icon whenever the window state changes.
void TitleBar::updateMaxButton()
{
    if (!m_btnMax) return;
    const bool maximized = window()->isMaximized();
#ifdef Q_OS_WIN
    // Caption buttons use text rendering; update the glyph directly.
    // ChromeRestore = 0xE923, ChromeMaximize = 0xE922 (Segoe MDL2/Fluent).
    m_btnMax->setText(QString(QChar(maximized ? 0xE923 : 0xE922)));
#else
    {
        const QString name = maximized ? "window-restore-symbolic" : "window-maximize-symbolic";
        const QColor fg = QApplication::palette().windowText().color();
        QIcon icon = recoloredIcon(name, fg);
        if (!icon.isNull())
            m_btnMax->setIcon(icon);
        else
            m_btnMax->setText(maximized ? "❐" : "□");
    }
#endif
}

bool TitleBar::eventFilter(QObject *obj, QEvent *e)
{
    if (e->type() == QEvent::WindowTitleChange && obj == window())
        m_title->setText(window()->windowTitle());

    if (e->type() == QEvent::Show) {
        QToolButton *btn  = nullptr;
        QMenu       *menu = nullptr;
        if      (obj == m_viewMenu)   { btn = m_viewBtn;   menu = m_viewMenu;   }
        else if (obj == m_searchMenu) { btn = m_searchBtn; menu = m_searchMenu; }
        if (btn) {
            // PM_MenuPanelWidth is the shadow inset on Adwaita (9px): the menu
            // window is that much wider than the visible frame on each side.
            // Adding it here shifts the window right so the visible right edge
            // aligns with the button's right edge.  Returns 0 on Windows/Fusion.
            const int panel = menu->style()->pixelMetric(
                QStyle::PM_MenuPanelWidth, nullptr, menu);
            const QPoint pos = btn->mapToGlobal(
                QPoint(btn->width() - menu->width() + panel, btn->height()));
            menu->move(pos);
        }
    }
    return QWidget::eventFilter(obj, e);
}

bool TitleBar::event(QEvent *e)
{
    if (e->type() == QEvent::WindowTitleChange)
        m_title->setText(window()->windowTitle());
    else if (e->type() == QEvent::WindowStateChange)
        updateMaxButton();
    return QWidget::event(e);
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        QMenu menu(this);
        themeMenu(&menu);
        const bool maximized = window()->isMaximized();
        QAction *actRestore  = menu.addAction(tr("Restore"));
        QAction *actMin      = menu.addAction(tr("Minimize"));
        QAction *actMax      = menu.addAction(tr("Maximize"));
        menu.addSeparator();
        QAction *actClose    = menu.addAction(tr("Close"));
        actRestore->setEnabled(maximized);
        actMax->setEnabled(!maximized);
        if (m_btnMin) actMin->setEnabled(true);
        QAction *chosen = menu.exec(event->globalPosition().toPoint());
        if      (chosen == actClose)   window()->close();
        else if (chosen == actMin)     window()->showMinimized();
        else if (chosen == actMax)     window()->showMaximized();
        else if (chosen == actRestore) window()->showNormal();
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    // Narrow zone at the very top triggers top-edge resize
    if (!window()->isMaximized() && event->pos().y() <= 4) {
        window()->windowHandle()->startSystemResize(Qt::TopEdge);
    } else {
        window()->windowHandle()->startSystemMove();
    }
    event->accept();
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        window()->isMaximized() ? window()->showNormal() : window()->showMaximized();
}
