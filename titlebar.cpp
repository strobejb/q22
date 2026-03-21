#include "titlebar.h"
#include "theme.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QMenu>
#include <QMouseEvent>
#include <QWindow>
#include <QApplication>
#include <QIcon>

#ifdef Q_OS_LINUX
#include <QProcess>
#include <QProcessEnvironment>
#endif

#ifdef Q_OS_WIN
#include <QPainter>
#include <QFontDatabase>
#include <windows.h>
#include <dwmapi.h>

// ── Windows native colour helpers ─────────────────────────────────────────────

// Returns true when Windows is configured to use a light app theme.
static bool windowsIsLightMode()
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD val = 1, sz = sizeof(val);
        RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&val), &sz);
        RegCloseKey(key);
        return val != 0;
    }
    return true;
}

// Returns the native Windows 11 title-bar background colour.
//
// When the user has "Show accent colour on title bars and window borders"
// enabled (DWM\ColorPrevalence = 1), the accent colour is returned.
// Otherwise the Mica neutral tones (#F3F3F3 light / #202020 dark) are used —
// these match what Windows 11 paints on its own title bars.
static QColor windowsTitleBarBg()
{
    DWORD colorPrevalence = 0;
    {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                          L"SOFTWARE\\Microsoft\\Windows\\DWM",
                          0, KEY_READ, &key) == ERROR_SUCCESS) {
            DWORD sz = sizeof(colorPrevalence);
            RegQueryValueExW(key, L"ColorPrevalence", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&colorPrevalence), &sz);
            RegCloseKey(key);
        }
    }
    if (colorPrevalence) {
        DWORD colorization = 0;
        BOOL  opaque       = FALSE;
        if (SUCCEEDED(DwmGetColorizationColor(&colorization, &opaque))) {
            // DWM COLORREF-like value: 0xAARRGGBB
            return QColor(static_cast<int>((colorization >> 16) & 0xFF),
                          static_cast<int>((colorization >>  8) & 0xFF),
                          static_cast<int>((colorization >>  0) & 0xFF));
        }
    }
    return windowsIsLightMode() ? QColor(0xF3, 0xF3, 0xF3)
                                : QColor(0x20, 0x20, 0x20);
}
#endif


#ifdef Q_OS_WIN

// Render a single glyph from Segoe MDL2 Assets or Segoe Fluent Icons as a
// QIcon.  Both fonts ship with Windows 10/11 and provide the same native
// symbols that Windows itself uses for title-bar buttons and toolbars.
// Falls back to an empty QIcon if neither font is present (text fallbacks
// in the calling code then take over).
static QIcon segoeIcon(uint codePoint, const QColor &color, int logicalPx = 14)
{
    static const QString s_family = []() -> QString {
        const auto &fams = QFontDatabase::families();
        for (const QString &f : {QStringLiteral("Segoe Fluent Icons"),
                                  QStringLiteral("Segoe MDL2 Assets")})
            if (fams.contains(f)) return f;
        return {};
    }();
    if (s_family.isEmpty()) return {};

    // Create a DPR-aware pixmap so the glyph is sharp on high-DPI screens.
    // IMPORTANT: QPainter on a DPR-aware pixmap works in *logical* coordinates;
    // it applies the DPR scale internally.  The font size and draw rect must
    // therefore be in logical pixels — using physPx here would render the glyph
    // at dpr× the intended size (e.g. 1.5× too large at 150 % scaling).
    const qreal dpr    = qApp->devicePixelRatio();
    const int   physPx = qRound(logicalPx * dpr);
    QFont font(s_family);
    font.setPixelSize(logicalPx);                   // logical px

    QPixmap pm(physPx, physPx);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setFont(font);
    p.setPen(color);
    p.drawText(QRect(0, 0, logicalPx, logicalPx), Qt::AlignCenter,   // logical px
               QString(QChar(codePoint)));
    return QIcon(pm);
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
    setFixedHeight(40);
    setObjectName("TitleBar");

    bool dark   = QApplication::palette().window().color().lightness() < 128;
    QString bg      = dark ? "#3d3846" : "#e8e8e8";
    QString fg      = dark ? "#ffffff" : "#2e3436";
    QString hover   = dark ? "rgba(255,255,255,0.15)" : "rgba(0,0,0,0.10)";
    QString border  = dark ? "#1e1e2e" : "#c4c4c4";

#ifdef Q_OS_WIN
    {
        // Override with native Windows colours.  windowsTitleBarBg() reads the
        // DWM colorization registry key and returns either the system accent
        // colour (when "show accent on title bars" is on) or the Mica neutral
        // tones (#F3F3F3 / #202020) that Windows 11 uses on its own title bars.
        const QColor winBg = windowsTitleBarBg();
        dark   = winBg.lightness() < 128;
        bg     = winBg.name();
        fg     = dark ? "#ffffff" : "#000000";
        hover  = dark ? "rgba(255,255,255,0.08)" : "rgba(0,0,0,0.06)";
        border = dark ? "rgba(255,255,255,0.10)" : "rgba(0,0,0,0.12)";

        // Qt plain QWidget subclasses don't paint stylesheet background-color
        // unless WA_StyledBackground is explicitly set.  Painting via palette
        // + autoFillBackground is always reliable regardless of platform style.
        QPalette pal = palette();
        pal.setColor(QPalette::Window, winBg);
        setPalette(pal);
        setAutoFillBackground(true);
        setAttribute(Qt::WA_StyledBackground, true);
    }
#endif

    setStyleSheet(QString(R"(
        #TitleBar {
            background-color: %1;
            border-bottom: 1px solid %5;
        }
        #TitleBar QLabel { color: %2; font-weight: 600; }
        #TitleBar QToolButton {
            border: none;
            border-radius: 12px;
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
            border));

#ifdef Q_OS_WIN
    // Windows 11 caption-button style: square corners, red close button.
    setStyleSheet(styleSheet() + R"(
        #TitleBar QToolButton#close,
        #TitleBar QToolButton#minimize,
        #TitleBar QToolButton#maximize  { border-radius: 0; }
        #TitleBar QToolButton#close:hover   { background: #c42b1c; color: white; }
        #TitleBar QToolButton#close:pressed { background: #9a1c10; color: white; }
    )");
#endif

    // ── Hamburger button (always far-left) ────────────────────────────────
    m_menu = new QMenu(this);
    themeMenu(m_menu);

    m_hamburger = new QToolButton(this);
    m_hamburger->setObjectName("hamburger");
#ifdef Q_OS_WIN
    // Use the same ☰ text the Find dialog uses — it renders reliably on all
    // Windows versions without depending on Segoe glyph availability.
    m_hamburger->setText("☰");
#else
    {
        QIcon hamburgerIcon = QIcon::fromTheme("open-menu-symbolic");
        if (!hamburgerIcon.isNull())
            m_hamburger->setIcon(hamburgerIcon);
        else
            m_hamburger->setText("☰");
    }
#endif
    m_hamburger->setAutoRaise(true);
    m_hamburger->setFixedSize(32, 32);
    m_hamburger->setIconSize(QSize(14, 14));
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
    else if (QIcon si = segoeIcon(0xE721, QColor(fg), 14); !si.isNull()) // Search
        m_searchBtn->setIcon(si);
#endif
    else
        m_searchBtn->setText("🔍");
    m_searchBtn->setAutoRaise(true);
    m_searchBtn->setFixedSize(32, 32);
    m_searchBtn->setIconSize(QSize(14, 14));
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
#ifdef Q_OS_WIN
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
    m_viewBtn->setAutoRaise(true);
    m_viewBtn->setFixedSize(32, 32);
    m_viewBtn->setIconSize(QSize(14, 14));
    m_viewBtn->setMenu(m_viewMenu);
    m_viewBtn->setPopupMode(QToolButton::InstantPopup);
    // Reposition to right-align when the menu is shown.
    m_viewMenu->installEventFilter(this);

    // ── Main layout ───────────────────────────────────────────────────────
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(4);
    layout->addWidget(m_hamburger);
    layout->addWidget(leftGroup);
    layout->addStretch();
    layout->addWidget(m_title);
    layout->addStretch();
    layout->addWidget(m_searchBtn);
    layout->addWidget(m_viewBtn);
    layout->addWidget(rightGroup);
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
    btn->setAutoRaise(true);
#ifdef Q_OS_WIN
    // Windows 11: caption buttons are full title-bar height, ~46 px wide,
    // flush with no gap between them (set in the layout above).
    btn->setFixedSize(46, 40);
    btn->setIconSize(QSize(10, 10));
#else
    btn->setFixedSize(28, 28);
    btn->setIconSize(QSize(12, 12));
#endif

    QIcon icon = QIcon::fromTheme(s.icon);
#ifdef Q_OS_WIN
    if (icon.isNull()) {
        // Segoe MDL2 Assets / Segoe Fluent Icons glyphs — same ones Windows
        // uses in its own title bars (ChromeClose / ChromeMinimize / ChromeMaximize).
        static const QMap<QString, uint> segoeGlyphs = {
            {"close",    0xE8BB},   // ChromeClose
            {"minimize", 0xE921},   // ChromeMinimize
            {"maximize", 0xE922},   // ChromeMaximize
        };
        const bool dark = QApplication::palette().window().color().lightness() < 128;
        const QColor fg = dark ? QColor("#ffffff") : QColor("#2e3436");
        if (segoeGlyphs.contains(name))
            icon = segoeIcon(segoeGlyphs[name], fg, 10);
    }
#endif
    if (!icon.isNull())
        btn->setIcon(icon);
    else
        btn->setText(s.fallback);

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
    QIcon icon = QIcon::fromTheme(maximized ? "window-restore-symbolic"
                                            : "window-maximize-symbolic");
#ifdef Q_OS_WIN
    if (icon.isNull()) {
        // ChromeRestore (0xE923) / ChromeMaximize (0xE922)
        const bool dark = QApplication::palette().window().color().lightness() < 128;
        const QColor fg = dark ? QColor("#ffffff") : QColor("#2e3436");
        icon = segoeIcon(maximized ? 0xE923 : 0xE922, fg, 10);
    }
#endif
    if (!icon.isNull())
        m_btnMax->setIcon(icon);
    else
        m_btnMax->setText(maximized ? "❐" : "□");
}

bool TitleBar::eventFilter(QObject *obj, QEvent *e)
{
    if (e->type() == QEvent::Show) {
        QToolButton *btn  = nullptr;
        QMenu       *menu = nullptr;
        if      (obj == m_viewMenu)   { btn = m_viewBtn;   menu = m_viewMenu;   }
        else if (obj == m_searchMenu) { btn = m_searchBtn; menu = m_searchMenu; }
        if (btn) {
            const QSize  sz  = menu->sizeHint();
            const QPoint pos = btn->mapToGlobal(
                QPoint(btn->width() - sz.width(), btn->height()));
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
