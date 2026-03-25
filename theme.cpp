#include "theme.h"
#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QMenu>
#include <QPlatformSurfaceEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QProxyStyle>
#include <QScreen>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOption>
#include <QWidget>

#ifdef Q_OS_WIN
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <windows.h>
#include <dwmapi.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#  define DWMWA_WINDOW_CORNER_PREFERENCE 33
#  define DWMWCP_ROUND 2
#endif

QIcon segoeIcon(uint codePoint, const QColor &color, int logicalPx)
{
    static const QString s_family = []() -> QString {
        const auto &fams = QFontDatabase::families();
        for (const QString &f : {QStringLiteral("Segoe Fluent Icons"),
                                  QStringLiteral("Segoe MDL2 Assets")})
            if (fams.contains(f)) return f;
        return {};
    }();
    if (s_family.isEmpty()) return {};

    // QPainter on a DPR-aware pixmap works in *logical* coordinates;
    // font size and draw rect must be in logical px (not physical px).
    const qreal dpr    = qApp->devicePixelRatio();
    const int   physPx = qRound(logicalPx * dpr);
    QFont font(s_family);
    font.setPixelSize(logicalPx);
    QPixmap pm(physPx, physPx);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setFont(font);
    p.setPen(color);
    p.drawText(QRect(0, 0, logicalPx, logicalPx), Qt::AlignCenter,
               QString(QChar(codePoint)));
    return QIcon(pm);
}
#endif

// ── DWM shadow for popup menus (Win11) ───────────────────────────────────────
// Qt's NoDropShadowWindowHint removes CS_DROPSHADOW, which we need to keep
// off because it paints a rectangular shadow that clips rounded corners.
// Instead, we use DwmExtendFrameIntoClientArea with {-1,-1,-1,-1} margins
// to enable the DWM composition shadow — this is the proper Win11 shadow
// that respects the window shape.  We also hint rounded corners via
// DWMWA_WINDOW_CORNER_PREFERENCE so DWM co-operates with our border-radius.
namespace {
struct MenuShadowFilter : public QObject
{
    explicit MenuShadowFilter(QObject *parent) : QObject(parent) {}

    bool eventFilter(QObject *obj, QEvent *e) override
    {
#ifdef Q_OS_WIN
        if (e->type() == QEvent::PlatformSurface) {
            auto *pse = static_cast<QPlatformSurfaceEvent *>(e);
            if (pse->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
                auto *w = qobject_cast<QWidget *>(obj);
                if (w) {
                    HWND hwnd = reinterpret_cast<HWND>(w->winId());
                    // Enable DWM composition shadow via extended frame
                    MARGINS margins = {-1, -1, -1, -1};
                    DwmExtendFrameIntoClientArea(hwnd, &margins);
                    // Hint rounded corners to DWM
                    DWORD cornerPref = DWMWCP_ROUND;
                    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                                          &cornerPref, sizeof(cornerPref));
                }
            }
        }
#else
        Q_UNUSED(obj);
        Q_UNUSED(e);
#endif
        return false;
    }
};
} // namespace

// ── Smart menu positioning ────────────────────────────────────────────────────

QColor themeBorderColor()
{
    return QColor(QApplication::palette().mid().color().name());

    //bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    //return dark ? QColor("#4a4a4a") : QColor("#cdc7c2");
}

#ifndef Q_OS_WIN
#include <QIcon>
#include <QPixmap>
#include <QToolButton>

QIcon recoloredIcon(const QString &name, const QColor &color, int sz)
{
    QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull()) return icon;
    // Request a pixmap at the logical size; it may come back at a higher physical
    // resolution on HiDPI screens, so we copy the devicePixelRatio to dst so that
    // Qt knows the correct logical size and doesn't scale the result down.
    QPixmap src = icon.pixmap(sz, sz);
    QPixmap dst(src.size());
    dst.setDevicePixelRatio(src.devicePixelRatio());
    dst.fill(Qt::transparent);
    QPainter p(&dst);
    p.drawPixmap(0, 0, src);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(dst.rect(), color);
    return QIcon(dst);
}

void recolorToolButtons(QWidget *parent)
{
    const QColor fg = parent->palette().buttonText().color();
    for (auto *btn : parent->findChildren<QToolButton*>()) {
        const QString name = btn->icon().name();
        if (name.isEmpty()) continue;
        const int sz = btn->iconSize().width();
        QIcon ic = recoloredIcon(name, fg, sz > 0 ? sz : 16);
        if (!ic.isNull())
            btn->setIcon(ic);
    }
}
#endif

QPoint smartMenuPos(const QWidget *anchor, const QMenu *menu, bool rightAlign)
{
    const QSize sz    = menu->sizeHint();
    const QRect ag    = QRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    QScreen *screen   = QGuiApplication::screenAt(ag.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect avail = screen->availableGeometry();

    // Horizontal: align left or right edge to anchor, then clamp to screen.
    int x = rightAlign ? ag.right() - sz.width() + 1 : ag.left();
    x = qBound(avail.left(), x, avail.right() - sz.width());

    // Vertical: prefer immediately below; fall back to immediately above.
    // If neither fits fully, pick whichever side has the most space.
    const int yBelow = ag.bottom() + 1;
    const int yAbove = ag.top() - sz.height();
    int y;
    if (yBelow + sz.height() <= avail.bottom() + 1)
        y = yBelow;
    else if (yAbove >= avail.top())
        y = yAbove;
    else
        y = (avail.bottom() + 1 - yBelow >= ag.top() - avail.top()) ? yBelow : yAbove;

    return {x, y};
}

// ── Palette ───────────────────────────────────────────────────────────────────

static void applyPalette(bool dark)
{
    QPalette p;

    if (!dark) {
        p.setColor(QPalette::Window,          QColor("#fafafa"));
        p.setColor(QPalette::WindowText,      QColor("#2e3436"));
        p.setColor(QPalette::Base,            QColor("#ffffff"));
        p.setColor(QPalette::AlternateBase,   QColor("#f6f5f4"));
        p.setColor(QPalette::Text,            QColor("#2e3436"));
        p.setColor(QPalette::BrightText,      QColor("#ffffff"));
        p.setColor(QPalette::Button,          QColor("#e0dedb"));
        p.setColor(QPalette::ButtonText,      QColor("#2e3436"));
        p.setColor(QPalette::Highlight,       QColor("#3584e4"));
        p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        p.setColor(QPalette::Link,            QColor("#1a73e8"));
        p.setColor(QPalette::LinkVisited,     QColor("#865e3c"));
        p.setColor(QPalette::Light,           QColor("#ffffff"));
        p.setColor(QPalette::Midlight,        QColor("#ebebeb"));
        p.setColor(QPalette::Mid,             QColor("#cdc7c2"));
        p.setColor(QPalette::Dark,            QColor("#bfbab5"));
        p.setColor(QPalette::Shadow,          QColor("#9a9996"));
        p.setColor(QPalette::ToolTipBase,     QColor("#1e1e1e"));
        p.setColor(QPalette::ToolTipText,     QColor("#f6f5f4"));
        p.setColor(QPalette::PlaceholderText, QColor("#9a9996"));
        p.setColor(QPalette::Inactive, QPalette::Highlight,       QColor("#b0adb0"));
        p.setColor(QPalette::Inactive, QPalette::HighlightedText, QColor("#2e3436"));
        p.setColor(QPalette::Disabled, QPalette::WindowText,      QColor("#9a9996"));
        p.setColor(QPalette::Disabled, QPalette::Text,            QColor("#9a9996"));
        p.setColor(QPalette::Disabled, QPalette::ButtonText,      QColor("#9a9996"));
        p.setColor(QPalette::Disabled, QPalette::Highlight,       QColor("#c0beba"));
        p.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#9a9996"));
    } else {
        p.setColor(QPalette::Window,          QColor("#242424"));
        p.setColor(QPalette::WindowText,      QColor("#deddda"));
        p.setColor(QPalette::Base,            QColor("#1e1e1e"));
        p.setColor(QPalette::AlternateBase,   QColor("#2a2a2a"));
        p.setColor(QPalette::Text,            QColor("#deddda"));
        p.setColor(QPalette::BrightText,      QColor("#ffffff"));
        p.setColor(QPalette::Button,          QColor("#3d3d3d"));
        p.setColor(QPalette::ButtonText,      QColor("#deddda"));
        p.setColor(QPalette::Highlight,       QColor("#3584e4"));
        p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        p.setColor(QPalette::Link,            QColor("#78aeed"));
        p.setColor(QPalette::LinkVisited,     QColor("#c49a6c"));
        p.setColor(QPalette::Light,           QColor("#4a4a4a"));
        p.setColor(QPalette::Midlight,        QColor("#3a3a3a"));
        p.setColor(QPalette::Mid,             QColor("#3d3d3d"));
        p.setColor(QPalette::Dark,            QColor("#202020"));
        p.setColor(QPalette::Shadow,          QColor("#141414"));
        p.setColor(QPalette::ToolTipBase,     QColor("#f6f5f4"));
        p.setColor(QPalette::ToolTipText,     QColor("#2e3436"));
        p.setColor(QPalette::PlaceholderText, QColor("#6c6c6c"));
        p.setColor(QPalette::Inactive, QPalette::Highlight,       QColor("#4a4a52"));
        p.setColor(QPalette::Inactive, QPalette::HighlightedText, QColor("#deddda"));
        p.setColor(QPalette::Disabled, QPalette::WindowText,      QColor("#6c6c6c"));
        p.setColor(QPalette::Disabled, QPalette::Text,            QColor("#6c6c6c"));
        p.setColor(QPalette::Disabled, QPalette::ButtonText,      QColor("#6c6c6c"));
        p.setColor(QPalette::Disabled, QPalette::Highlight,       QColor("#4a4a4a"));
        p.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#6c6c6c"));
    }

    QApplication::setPalette(p);
}

// ── Stylesheet ────────────────────────────────────────────────────────────────

static QString buildStylesheet(bool dark)
{
    // Named colour tokens
    const QString accent      = "#3584e4";
    const QString accentHover = "#4a91e8";
    const QString accentFg    = "#ffffff";
    const QString danger      = "#e01b24";

    QString fg, border, btnBg, btnHover, btnActive,
            menuBg, inputBg,
            scrollHandle, scrollHover,
            statusBg, statusComboHover;

    if (!dark) {
        fg              = "#2e3436";
        border          = "#cdc7c2";
        btnBg           = "#e0dedb";
        btnHover        = "#d3d1ce";
        btnActive       = "#c8c6c3";
        menuBg          = "#ffffff";
        inputBg         = "#ffffff";
        scrollHandle    = "#cdc7c2";
        scrollHover     = "#9a9996";
        statusBg        = "#f6f5f4";
        statusComboHover= "#e8e6e3";
    } else {
        fg              = "#deddda";
        border          = "#4a4a4a";
        btnBg           = "#3d3d3d";
        btnHover        = "#484848";
        btnActive       = "#525252";
        menuBg          = "#2e2e2e";
        inputBg         = "#1e1e1e";
        scrollHandle    = "#5a5a5a";
        scrollHover     = "#787878";
        statusBg        = "#2a2a2a";
        statusComboHover= "#3a3a3a";
    }

    // Use a raw template with named tokens replaced below
    static const char TMPL[] = R"(

/* ── Global ──────────────────────────────────────────────────── */
QWidget {
    color: {fg};
}

/* ── Push buttons ────────────────────────────────────────────── */
QPushButton {
    background: {btnBg};
    border: 1px solid {border};
    border-radius: 6px;
    padding: 5px 16px;
    min-width: 80px;
}
QPushButton:hover   { background: {btnHover}; }
QPushButton:pressed { background: {btnActive}; border-color: {accent}; }
QPushButton:disabled { color: palette(disabled, windowtext); }
QPushButton[default="true"] {
    background: {accent};
    color: {accentFg};
    border-color: {accent};
}
QPushButton[default="true"]:hover   { background: {accentHover}; }
QPushButton[default="true"]:pressed { background: {accent}; }

/* ── Menus ───────────────────────────────────────────────────── */
QMenu {
    background: {menuBg};
    border: 1px solid {border};
    border-radius: 8px;
    padding: 6px 0;
    {menuMargin}
}
QMenu::item {
    padding: 6px 28px 6px 16px;
    min-width: 180px;
    border-radius: 4px;
    margin: 1px 4px;
}
QMenu::item:selected {
    background: palette(highlight);
    color: palette(highlighted-text);
}
QMenu::item:disabled { color: palette(disabled, windowtext); }
QMenu::separator {
    height: 1px;
    background: {border};
    margin: 4px 8px;
}
QMenu::icon {
    width: 14px;
    height: 14px;
    margin-left: 0;
    left: 4px;
}
QMenu::indicator {
    width: 14px;
    height: 14px;
    margin-left: 0;
}

/* ── ComboBox ────────────────────────────────────────────────── */
QComboBox {
    background: {inputBg};
    border: 1px solid {border};
    border-radius: 6px;
    padding: 3px 8px;
    selection-background-color: {accent};
    selection-color: {accentFg};
}
QComboBox:hover { border-color: palette(shadow); }
QComboBox:focus { border: 2px solid {accent}; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox QAbstractItemView {
    background: {menuBg};
    border: 1px solid {border};
    border-radius: 6px;
    selection-background-color: {accent};
    selection-color: {accentFg};
    outline: none;
    padding: 4px;
}

/* ── Scroll bars ─────────────────────────────────────────────── */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px;
}
QScrollBar::handle:vertical {
    background: {scrollHandle};
    border-radius: 5px;
    min-height: 24px;
}
QScrollBar::handle:vertical:hover  { background: {scrollHover}; }
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical      { height: 0; }
QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical      { background: transparent; }

QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 2px;
}
QScrollBar::handle:horizontal {
    background: {scrollHandle};
    border-radius: 5px;
    min-width: 24px;
}
QScrollBar::handle:horizontal:hover  { background: {scrollHover}; }
QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal      { width: 0; }
QScrollBar::add-page:horizontal,
QScrollBar::sub-page:horizontal      { background: transparent; }

/* ── Status bar ──────────────────────────────────────────────── */
QStatusBar {
    background: {statusBg};
    border-top: 1px solid {border};
    padding: 6px 0;
}
QStatusBar QComboBox {
    border: 1px solid transparent;
    background: transparent;
    border-radius: 4px;
}
QStatusBar QComboBox:hover { background: {statusComboHover}; }

/* ── Misc ────────────────────────────────────────────────────── */
QAbstractScrollArea { border: none; }
#HexView { border-top: 1px solid {border}; }
QToolTip {
    background: palette(tooltip-base);
    color: palette(tooltip-text);
    border: 1px solid {border};
    border-radius: 6px;
    padding: 2px 6px;
}

)";

    QString ss = QString::fromLatin1(TMPL);
    ss.replace("{fg}",               fg);
    ss.replace("{border}",           border);
    ss.replace("{btnBg}",            btnBg);
    ss.replace("{btnHover}",         btnHover);
    ss.replace("{btnActive}",        btnActive);
    ss.replace("{menuBg}",           menuBg);
    ss.replace("{inputBg}",          inputBg);
    ss.replace("{scrollHandle}",     scrollHandle);
    ss.replace("{scrollHover}",      scrollHover);
    ss.replace("{statusBg}",         statusBg);
    ss.replace("{statusComboHover}", statusComboHover);
    ss.replace("{accent}",           accent);
    ss.replace("{accentHover}",      accentHover);
    ss.replace("{accentFg}",         accentFg);
    ss.replace("{danger}",           danger);
#ifdef Q_OS_WIN
    ss.replace("{menuMargin}",       QString());
#else
    ss.replace("{menuMargin}",       "margin: 8px;");
#endif
    return ss;
}

// ── Public entry point ────────────────────────────────────────────────────────

// ── Self-drawn drop shadow for menus (Linux) ─────────────────────────────────
// The QMenu QSS adds "margin: 8px" on Linux, which leaves a transparent ring
// around the menu window (WA_TranslucentBackground makes it truly transparent).
// MenuShadowOverlay is a child widget that paints a soft shadow into that ring,
// clipping away the content area so the menu items show through underneath.
// This mirrors exactly how GTK draws shadows on its own popup menus.
#ifndef Q_OS_WIN
namespace {
struct MenuShadowOverlay : public QWidget
{
    static constexpr int S = 8;   // shadow width — must match QSS "margin: 8px"
    static constexpr int R = 8;   // corner radius — must match QSS border-radius

    explicit MenuShadowOverlay(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setGeometry(parent->rect());
        // Track parent resizes (menu size is calculated lazily at popup time).
        parent->installEventFilter(this);
    }

    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (obj == parent() && e->type() == QEvent::Resize)
            setGeometry(parentWidget()->rect());
        return false;
    }

    void paintEvent(QPaintEvent *) override
    {
        const QRectF full(rect());
        const QRectF inner = full.adjusted(S, S, -S, -S);
        if (!inner.isValid()) return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Restrict painting to the margin ring — leave the content area untouched
        // so the menu's own paint (background + items) shows through.
        QPainterPath clip;
        clip.addRect(full);
        QPainterPath hole;
        hole.addRoundedRect(inner, R, R);
        clip -= hole;
        p.setClipPath(clip);

        // Draw outer → inner with CompositionMode_Source so each smaller rect
        // replaces (not accumulates over) the larger one behind it.
        // This gives a clean gradient: ~2% opacity at the window edge, ~12% at
        // the menu border — close to GNOME/Mutter's compositor shadow.
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.setPen(Qt::NoPen);
        for (int i = S; i >= 1; --i) {
            const int alpha = (S - i + 1) * 4;   // 4 (outer) → 32 (inner, ~12%)
            p.setBrush(QColor(0, 0, 0, alpha));
            p.drawRoundedRect(inner.adjusted(-i, -i + 1, i, i),
                              R + i * 0.4, R + i * 0.4);
        }
    }
};
} // namespace
#endif

// ── Check-mark inset style ────────────────────────────────────────────────────
// When qApp->setStyleSheet() is active, QStyleSheetStyle becomes the effective
// style for all QMenu drawing — per-widget setStyle() overrides for drawControl
// and pixelMetric are bypassed.  The one thing that IS reliably dispatched
// through the per-widget style is drawPrimitive(PE_IndicatorMenuCheckMark),
// which Fusion calls via proxy() during CE_MenuItem.  We use that single hook
// to nudge checkmark glyphs right so they sit inside the selection box.
namespace {
struct TightMenuStyle : public QProxyStyle
{
    explicit TightMenuStyle(QStyle *base) : QProxyStyle(base) {}

    static constexpr int kGlyphInset = 4;

    void drawPrimitive(PrimitiveElement pe, const QStyleOption *opt,
                       QPainter *p, const QWidget *w) const override
    {
        if (pe == PE_IndicatorMenuCheckMark) {
            QStyleOption shifted = *opt;
            shifted.rect = opt->rect.translated(kGlyphInset, 0);
            QProxyStyle::drawPrimitive(pe, &shifted, p, w);
            return;
        }
        QProxyStyle::drawPrimitive(pe, opt, p, w);
    }
};
} // namespace

static TightMenuStyle *tightMenuStyle()
{
    // Lazily created, parented to qApp so it is cleaned up on exit.
    static TightMenuStyle *s = nullptr;
    if (!s) {
        s = new TightMenuStyle(qApp->style());
        s->setParent(qApp);
    }
    return s;
}

static MenuShadowFilter *menuShadowFilter()
{
    static MenuShadowFilter *s = nullptr;
    if (!s) {
        s = new MenuShadowFilter(qApp);
    }
    return s;
}

void themeMenu(QMenu *menu)
{
    // A frameless, transparent window lets the QSS border-radius actually clip
    // the corners.  Qt::Popup is preserved so click-outside still closes the menu.
    // NoDropShadowWindowHint is only needed on Windows, where we suppress the
    // rectangular Win32 CS_DROPSHADOW and replace it with a shaped DWM shadow via
    // the event filter below.  On Linux the compositor provides its own shadow for
    // popup windows, which already respects the window shape.
#ifdef Q_OS_WIN
    menu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
#else
    menu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
#endif
    menu->setAttribute(Qt::WA_TranslucentBackground);
    // Apply the compact check-column style.  widget::setStyle() does not
    // transfer ownership, so we use a long-lived singleton parented to qApp.
    menu->setStyle(tightMenuStyle());
    // On Windows, install an event filter that applies DWM shadow when the
    // popup's native surface is created.
    menu->installEventFilter(menuShadowFilter());
#ifndef Q_OS_WIN
    // Self-drawn shadow: child overlay paints into the 8px transparent QSS margin.
    auto *overlay = new MenuShadowOverlay(menu);
    overlay->show();
    overlay->raise();
#endif
}

// ── Tooltip rounded-corner clipping ──────────────────────────────────────────
//
// setMask() does not work on Wayland popup surfaces.  Instead, setting
// WA_TranslucentBackground during QEvent::Polish (before the native window is
// created) allocates an alpha channel for the window.  Qt's QSS engine then
// clips the background fill to the border-radius shape through that alpha
// channel, producing genuinely rounded corners without any custom painting.

class TooltipFilter : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (obj->inherits("QTipLabel") && e->type() == QEvent::Polish)
            static_cast<QWidget *>(obj)->setAttribute(Qt::WA_TranslucentBackground);
        return false;
    }
};

void applyAdwaitaTheme(ColorScheme scheme)
{
    // Fusion is always available — use it as the base style.
    // Safe to call multiple times; Qt is idempotent about the same style.
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    bool dark;
    switch (scheme) {
    case ColorScheme::Light: dark = false; break;
    case ColorScheme::Dark:  dark = true;  break;
    default:  // System: read from Qt 6.5+ colour scheme hint
        dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    }

    applyPalette(dark);
    qApp->setStyleSheet(buildStylesheet(dark));

    // One-time setup: font preference and tooltip filter.
    static bool firstRun = true;
    if (firstRun) {
        firstRun = false;
        if (QFontDatabase::families().contains("Cantarell")) {
            QFont f = QApplication::font();
            f.setFamily("Cantarell");
            QApplication::setFont(f);
        }
        qApp->installEventFilter(new TooltipFilter(qApp));
    }
}
