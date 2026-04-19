#include "theme.h"
#include <QApplication>
#include <QComboBox>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QMenu>
#include <QPlatformSurfaceEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleOption>
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

// Shadow margin: QSS "margin: Npx" on Linux leaves an N-px transparent ring
// around the menu window for the self-drawn shadow.  Must stay in sync with the
// {menuMargin} substitution in buildStylesheet() and MenuShadowOverlay::S.
static constexpr int kMenuShadowMargin = 8;

// ── UI colour overrides ───────────────────────────────────────────────────────
// Set by applyUiPalette() when an active palette defines window/toolbar colours.
// applyAdwaitaTheme() layers these on top of the base Adwaita palette + QSS.
static ColorScheme       s_currentScheme = ColorScheme::System;
static UiColourOverrides s_uiOverrides;

#ifdef Q_OS_WIN
// ── DWM dark-mode title bars ──────────────────────────────────────────────────
// Qt does not call DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE) on
// dialogs, so they always get a light title bar even in dark mode.  We fix
// this with a global event filter that applies the attribute whenever any
// non-frameless top-level window is shown, plus a sweep of existing windows
// when the colour scheme changes.

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#  define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static void applyDwmDarkMode(QWidget *w)
{
    if (!w->isWindow() || (w->windowFlags() & Qt::FramelessWindowHint))
        return;

    HWND hwnd = reinterpret_cast<HWND>(w->winId());


    const bool dark = (s_currentScheme == ColorScheme::Dark) ||
                      (s_currentScheme == ColorScheme::System &&
                       QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    BOOL val = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &val, sizeof(val));
}

namespace {
struct DarkModeFilter : public QObject {
    explicit DarkModeFilter(QObject *p) : QObject(p) {}
    bool eventFilter(QObject *obj, QEvent *e) override {
        auto *w = qobject_cast<QWidget *>(obj);
        if (!w) return false;
        if (e->type() == QEvent::Show)
            applyDwmDarkMode(w);
        return false;
    }
};
} // namespace
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
        // The QSS shadow margin shifts the visible menu content inward by
        // kMenuShadowMargin px on every side.  Compensate so the menu appears
        // exactly where Qt placed it rather than offset down and to the right.
        if (e->type() == QEvent::Show) {
            auto *w = qobject_cast<QWidget *>(obj);
            if (w)
                w->move(w->pos() - QPoint(kMenuShadowMargin, kMenuShadowMargin));
        }
#endif
        return false;
    }
};
} // namespace

// ── Hairline separator ────────────────────────────────────────────────────────

Hairline::Hairline(QWidget *parent, Edge edge, QWidget *bgSource)
    : QWidget(parent), m_edge(edge), m_bgSource(bgSource)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(1);
    if (m_bgSource)
        m_bgSource->installEventFilter(this);
}

void Hairline::setBgSource(QWidget *bgSource)
{
    if (m_bgSource)
        m_bgSource->removeEventFilter(this);
    m_bgSource = bgSource;
    if (m_bgSource)
        m_bgSource->installEventFilter(this);
    update();
}

void Hairline::showEvent(QShowEvent *e)
{
    setFixedHeight(qCeil(devicePixelRatioF()));
    QWidget::showEvent(e);
}

bool Hairline::eventFilter(QObject *obj, QEvent *e)
{
    // Repaint when the bgSource palette changes (e.g. titlebar active/inactive).
    if (obj == m_bgSource && e->type() == QEvent::PaletteChange)
        update();
    return QWidget::eventFilter(obj, e);
}

void Hairline::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const qreal dpr      = devicePixelRatioF();
    const qreal linePhys = qRound(dpr);
    const qreal topPhys  = (m_edge == Edge::Bottom)
                           ? (height() * dpr - linePhys)
                           : 0.0;
    const qreal gapPhys  = height() * dpr - linePhys;

    p.scale(1.0 / dpr, 1.0 / dpr);

    // Fill the gap (non-line portion) with bgSource palette if provided.
    if (m_bgSource && gapPhys > 0) {
        const QColor bg = m_bgSource->palette().window().color();
        const qreal gapTop = (m_edge == Edge::Bottom) ? 0.0 : linePhys;
        p.fillRect(QRectF(0, gapTop, width() * dpr, gapPhys), bg);
    }

    p.fillRect(QRectF(0, topPhys, width() * dpr, linePhys), palette().mid());
}

// ── Smart menu positioning ────────────────────────────────────────────────────

QColor themeBorderColor()
{
    return QColor(QApplication::palette().mid().color().name());

    //bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    //return dark ? QColor("#4a4a4a") : QColor("#cdc7c2");
}

#include <QIcon>
#include <QPixmap>
#include <QToolButton>

QIcon recoloredIcon(const QString &name, const QColor &color, int sz)
{
    QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull())
        icon = QIcon(":/icons/hicolor/scalable/actions/" + name + ".svg");
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
        // On the first call the icon still has a theme name; save it for
        // subsequent calls (after recolor the icon is a plain pixmap, name="").
        QString name = btn->property("iconThemeName").toString();
        if (name.isEmpty()) {
            name = btn->icon().name();
            if (name.isEmpty()) continue;
            btn->setProperty("iconThemeName", name);
        }
        const int sz = btn->iconSize().width();
        QIcon ic = recoloredIcon(name, fg, sz > 0 ? sz : 16);
        if (!ic.isNull())
            btn->setIcon(ic);
    }
}


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
    // Four anchor colours per mode — everything else is derived from these.
    // We avoid setColorScheme(): on GNOME the platform plugin responds to it
    // by re-asserting the system palette, permanently overriding light mode.
    const QColor window    = dark ? QColor("#242424") : QColor("#f6f5f4");
    const QColor windowText = dark ? QColor("#deddda") : QColor("#2e3436");
    const QColor base      = dark ? window.darker(120) : Qt::white;
    const QColor highlight = s_uiOverrides.highlight.isValid()
                           ? s_uiOverrides.highlight : QColor("#3584e4");

    // Derived roles — no additional hard-coded values beyond the four above.
    const QColor button    = dark ? window.lighter(155) : window.darker(105);
    const QColor altBase   = dark ? base.lighter(112)   : base.darker(103);
    const QColor ph        = QColor(windowText.red(), windowText.green(),
                                    windowText.blue(), 128); // 50% opacity text
    const QColor hlText    = highlight.lightness() < 160 ? Qt::white : windowText;

    // QPalette(button, window) auto-computes Light, Midlight, Mid, Dark, Shadow.
    // Mid (used for borders and separators) comes out near-invisible in dark mode
    // because both button and window are dark, so override it explicitly.
    const QColor mid = dark ? window.lighter(222) : window.darker(130);
    QPalette p(button, window);
    p.setColor(QPalette::Mid,             mid);
    p.setColor(QPalette::Midlight,        mid.lighter(100));
    p.setColor(QPalette::WindowText,      windowText);
    p.setColor(QPalette::Text,            windowText);
    p.setColor(QPalette::ButtonText,      windowText);
    p.setColor(QPalette::BrightText,      Qt::white);
    p.setColor(QPalette::Base,            base);
    p.setColor(QPalette::AlternateBase,   altBase);
    p.setColor(QPalette::Highlight,       highlight);
    p.setColor(QPalette::HighlightedText, hlText);
    p.setColor(QPalette::Link,            dark ? highlight.lighter(130) : highlight);
    p.setColor(QPalette::PlaceholderText, ph);
    // Tooltips: inverted pair so they stand out against the window background.
    p.setColor(QPalette::ToolTipBase,     windowText);
    p.setColor(QPalette::ToolTipText,     window);


    // Disabled group — Fusion auto-derives this poorly. Set it explicitly to a
    // clearly mid-toned gray that's readable in both light and dark modes.
    // Mix windowText toward window at 35% (text) / 65% (background).
    const QColor disabled(
        qRound(windowText.red()   * 0.35 + window.red()   * 0.65),
        qRound(windowText.green() * 0.35 + window.green() * 0.65),
        qRound(windowText.blue()  * 0.35 + window.blue()  * 0.65));
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text,       disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);

    // Layer UI palette overrides on top.
    if (s_uiOverrides.window.isValid())
        p.setColor(QPalette::Window, s_uiOverrides.window);
    if (s_uiOverrides.windowText.isValid()) {
        p.setColor(QPalette::WindowText, s_uiOverrides.windowText);
        p.setColor(QPalette::Text,       s_uiOverrides.windowText);
        p.setColor(QPalette::ButtonText, s_uiOverrides.windowText);
    }
    if (s_uiOverrides.highlight.isValid()) {
        p.setColor(QPalette::Highlight,       s_uiOverrides.highlight);
        p.setColor(QPalette::HighlightedText, hlText);
    }

    QApplication::setPalette(p);
}

// ── Stylesheet ────────────────────────────────────────────────────────────────

static QString buildStylesheet(bool dark)
{
    // Derive the handful of colours that have no direct palette() role from
    // the current application palette (set by applyPalette moments earlier).
    const QPalette pal = QApplication::palette();

    const QColor btn  = pal.button().color();
    const bool darkBtn = btn.lightness() < 128;
    const QString fgDisabled      = pal.color(QPalette::Disabled, QPalette::WindowText).name();
    const QString btnHover        = (darkBtn ? btn.lighter(118) : btn.darker(103)).name();
    const QString btnActive       = (darkBtn ? btn.lighter(133) : btn.darker(106)).name();

    const QColor toolbarColor = s_uiOverrides.toolbar.isValid()
                               ? s_uiOverrides.toolbar
                               : pal.alternateBase().color();
    const QString statusBg         = toolbarColor.name();
    const QString statusComboHover = (toolbarColor.lightness() < 128)
                                     ? toolbarColor.lighter(130).name()
                                     : toolbarColor.darker(107).name();

    // min-height sets the *content area* minimum; padding (5px top+bottom) and
    // border (1px each) are added on top, giving total = font + 12 — matching
    // QLineEdit's sizeHint.  QAbstractSpinBox ignores QSS padding for its own
    // height calculation so we drive it with min-height instead.
    const QString inputMinH = QString::number(
        QFontMetrics(QApplication::font()).height());

    // Everything else uses palette() references so the stylesheet automatically
    // tracks palette changes without any hard-coded colours.
    static const char TMPL[] = R"(

/* ── Global ──────────────────────────────────────────────────── */
/* outline:none suppresses Qt's QSS focus-outline indicator globally;
   PE_FrameFocusRect is suppressed separately in NoFocusRectStyle. */
QWidget { color: palette(window-text); outline: none; }

/* ── Menu bar ────────────────────────────────────────────────── */
QMenuBar {
    background: palette(window);
}

/* ── Push buttons ────────────────────────────────────────────── */
QPushButton {
    background: palette(button);
    border: 1px solid palette(mid);
    border-radius: 6px;
    padding: 5px 16px;
    min-width: 80px;
    font-weight: normal;
}
QPushButton:hover   { background: {btnHover}; }
QPushButton:focus   { border: 2px solid palette(highlight); padding: 4px 15px; }
QPushButton:pressed { background: {btnActive}; border-color: palette(highlight); }
QPushButton:disabled { color: {fgDisabled}; }
QPushButton[default="true"] {
    background: palette(highlight);
    color: palette(highlighted-text);
    border-color: palette(highlight);
}
QPushButton[default="true"]:hover   { background: palette(highlight); }
QPushButton[default="true"]:pressed { background: palette(highlight); }

/* ── Menus ───────────────────────────────────────────────────── */
QMenu {
    background: palette(base);
    border: 1px solid palette(mid);
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
QMenu::item:disabled { color: {fgDisabled}; }
QMenu::separator {
    height: 1px;
    background: palette(mid);
    margin: 4px 8px;
}
QMenu::icon     { width: 14px; height: 14px; margin-left: 0; left: 4px; }
QMenu::indicator { width: 14px; height: 14px; margin-left: 0; }

/* ── Line edits ──────────────────────────────────────────────── */
QLineEdit {
    border: 1px solid palette(mid);
    border-radius: 6px;
    padding: 5px 8px;
    margin: 1px;
    background: palette(base);
    selection-background-color: palette(highlight);
    selection-color: palette(highlighted-text);
}
QLineEdit:hover { border: 1px solid palette(mid); margin: 1px; }
QLineEdit:focus { border: 2px solid palette(highlight); margin: 0px; background: palette(base); }
QLineEdit:disabled { color: {fgDisabled}; background: palette(window); }
QLabel:disabled { color: {fgDisabled}; }
QCheckBox:disabled { color: {fgDisabled}; }

/* ── Plain text edits ────────────────────────────────────────── */
QPlainTextEdit {
    border: 1px solid palette(mid);
    border-radius: 6px;
    padding: 5px 8px;
    margin: 1px;
    background: palette(base);
    selection-background-color: palette(highlight);
    selection-color: palette(highlighted-text);
}
QPlainTextEdit:focus { border: 2px solid palette(highlight); margin: 0px; }
QPlainTextEdit:disabled { color: {fgDisabled}; }

/* ── Spin boxes ──────────────────────────────────────────────── */
QAbstractSpinBox {
    border: 1px solid palette(mid);
    border-radius: 6px;
    padding: 5px 8px;
    margin: 1px;
    min-height: {inputMinH}px;
    background: palette(base);
    selection-background-color: palette(highlight);
    selection-color: palette(highlighted-text);
}
QAbstractSpinBox:hover { border: 2px solid palette(mid); margin: 0px; }
QAbstractSpinBox:focus { border: 2px solid palette(highlight); margin: 0px; background: palette(base); }
QAbstractSpinBox:disabled { color: {fgDisabled}; }
QAbstractSpinBox::up-button {
    subcontrol-origin: border;
    subcontrol-position: top right;
    width: 18px;
    border-left: 1px solid palette(mid);
    border-top-right-radius: 5px;
    background: palette(window);
}
QAbstractSpinBox::up-button:hover { background: palette(mid); }
QAbstractSpinBox::down-button {
    subcontrol-origin: border;
    subcontrol-position: bottom right;
    width: 18px;
    border-left: 1px solid palette(mid);
    border-top: 1px solid palette(mid);
    border-bottom-right-radius: 5px;
    background: palette(window);
}
QAbstractSpinBox::down-button:hover { background: palette(mid); }

/* ── ComboBox ────────────────────────────────────────────────── */
QComboBox {
    background: palette(base);
    border: 1px solid palette(mid);
    border-radius: 6px;
    padding: 3px 8px;
    margin: 1px;
    selection-background-color: palette(highlight);
    selection-color: palette(highlighted-text);
}
QComboBox:hover            { border: 1px solid palette(mid); margin: 1px; background: palette(window); }
QComboBox:focus            { border: 2px solid palette(highlight); margin: 0px; }
QComboBox:open             { background: palette(button); }
QComboBox[popupOpen="true"],
QComboBox[popupOpen="true"]:hover,
QComboBox[popupOpen="true"]:focus,
QComboBox[popupOpen="true"]:hover:focus { background: palette(button); }
QComboBox:disabled { background: palette(window); color: palette(mid); border-color: palette(mid); }
QComboBox QLineEdit { border: none; background: transparent; padding: 0; }
QComboBox QLineEdit:focus { border: none; }
QComboBox::drop-down  { border: none; width: 20px; }
QComboBox QAbstractItemView {
    background: palette(base);
    border: 1px solid palette(mid);
    border-radius: 6px;
    selection-background-color: palette(highlight);
    selection-color: palette(highlighted-text);
    outline: none;
    padding: 4px;
}

/* ── Item views ──────────────────────────────────────────────── */
QAbstractItemView::item:hover    { background: palette(window); }
QAbstractItemView::item:selected { background: palette(highlight); color: palette(highlighted-text); }

/* ── Scroll bars ─────────────────────────────────────────────── */
QScrollBar:vertical   { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:vertical   { background: palette(mid); border-radius: 5px; min-height: 24px; }
QScrollBar::handle:horizontal { background: palette(mid); border-radius: 5px; min-width:  24px; }
QScrollBar::handle:vertical:hover,
QScrollBar::handle:horizontal:hover { background: palette(shadow); }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical   { height: 0; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical,
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

/* ── Status bar ──────────────────────────────────────────────── */
QStatusBar {
    background: {statusBg};

    padding: 0;
    /*border-top: 1px solid palette(mid);*/
}
QStatusBar QComboBox {
    border: 1px solid transparent;
    background: transparent;
    border-radius: 4px;
    margin: 1px;
}
QStatusBar QComboBox:hover { background: {statusComboHover}; border: 1px solid palette(mid); margin:1px; }
QStatusBar QComboBox:focus { background: {statusComboHover}; border: 1px solid palette(mid); margin:0;}

/* ── Misc ────────────────────────────────────────────────────── */
QAbstractScrollArea { border: none; }
QPlainTextEdit { border: 1px solid palette(mid); margin:1px; border-radius: 6px; }
QPlainTextEdit:focus { border: 2px solid palette(highlight); margin: 0px; }
QToolTip {
    background: palette(tooltip-base);
    color: palette(tooltip-text);
    border: 1px solid palette(mid);
    border-radius: 6px;
    padding: {tooltipPad};
}

)";

    QString ss = QString::fromLatin1(TMPL);
    ss.replace("{fgDisabled}",       fgDisabled);
    ss.replace("{inputMinH}",        inputMinH);
    ss.replace("{btnHover}",         btnHover);
    ss.replace("{btnActive}",        btnActive);
    ss.replace("{statusBg}",         statusBg);
    ss.replace("{statusComboHover}", statusComboHover);
#ifdef Q_OS_WIN
    ss.replace("{menuMargin}",  QString());
    ss.replace("{tooltipPad}",  "1px 6px");
#else
    ss.replace("{menuMargin}",  "margin: 8px;");
    ss.replace("{tooltipPad}",  "2px 6px");
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
    static constexpr int S = kMenuShadowMargin;   // shadow width — must match QSS "margin: Npx"
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
struct NoFocusRectStyle : public QProxyStyle
{
    explicit NoFocusRectStyle(QStyle *base) : QProxyStyle(base) {}

    void drawPrimitive(PrimitiveElement pe, const QStyleOption *opt,
                       QPainter *p, const QWidget *w) const override
    {
        if (pe == PE_FrameFocusRect)
            return;
        QProxyStyle::drawPrimitive(pe, opt, p, w);
    }

    int pixelMetric(PixelMetric metric, const QStyleOption *opt,
                    const QWidget *widget) const override
    {
        if (metric == PM_IndicatorWidth || metric == PM_IndicatorHeight) {
            const QFontMetrics fm = opt ? opt->fontMetrics
                                        : (widget ? widget->fontMetrics()
                                                  : QFontMetrics(QApplication::font()));
            return fm.height();
        }
        return QProxyStyle::pixelMetric(metric, opt, widget);
    }

#ifdef Q_OS_WIN
    QSize sizeFromContents(ContentsType ct, const QStyleOption *opt,
                           const QSize &sz, const QWidget *w) const override
    {
        QSize s = QProxyStyle::sizeFromContents(ct, opt, sz, w);
        if (ct == CT_ItemViewItem) {
            // windows11 style inflates item height via native UxTheme metrics.
            // Clamp to 2× font height to match Linux/Adwaita proportions.
            const QFontMetrics fm = opt ? opt->fontMetrics : QFontMetrics(QApplication::font());
            s.setHeight(qMin(s.height(), fm.height() * 2));
        }
        return s;
    }
#endif

    void drawControl(ControlElement ce, const QStyleOption *opt,
                     QPainter *p, const QWidget *w) const override
    {
        if (ce == CE_CheckBox) {
            const auto *btn = qstyleoption_cast<const QStyleOptionButton *>(opt);
            if (!btn) { QProxyStyle::drawControl(ce, opt, p, w); return; }

            // Adwaita registers checkboxes with an animation subsystem that draws
            // hover transitions at partial opacity.  Any pixel not covered by the
            // full-opacity final frame bleeds stale backing-store content through
            // the semi-transparent intermediate frame.  By custom-drawing we bypass
            // the animation path entirely — all pixels written are fully opaque.

            p->save();
            p->setRenderHint(QPainter::Antialiasing);

            // Fill the full widget rect so no stale pixels remain behind the label.
            const QBrush winBg = (w && w->parentWidget())
                                 ? w->parentWidget()->palette().window()
                                 : opt->palette.window();
            p->fillRect(opt->rect, winBg);

            const QRect     indRect = proxy()->subElementRect(SE_CheckBoxIndicator, opt, w);
            const bool      enabled = opt->state & State_Enabled;
            const bool      hovered = opt->state & State_MouseOver;
            const bool      checked = opt->state & State_On;
            const bool      partial = opt->state & State_NoChange;
            const QPalette &pal     = opt->palette;

            const qreal  radius = 3.0;
            const QRectF rf     = QRectF(indRect).adjusted(0.5, 0.5, -0.5, -0.5);

            // Box fill and border
            QColor fill, border;
            if (!enabled) {
                fill   = pal.color(QPalette::Disabled, QPalette::Button);
                border = pal.color(QPalette::Disabled, QPalette::Mid);
            } else if (checked || partial) {
                fill   = pal.highlight().color();
                border = pal.highlight().color();
            } else {
                fill   = hovered ? pal.button().color() : pal.base().color();
                border = pal.mid().color();
            }
            // Draw fill inset by 1px so it doesn't bleed over the border stroke
            p->setPen(Qt::NoPen);
            p->setBrush(fill);
            p->drawRoundedRect(QRectF(indRect).adjusted(1, 1, -1, -1),
                               qMax(0.0, radius - 0.5), qMax(0.0, radius - 0.5));
            // Draw border on top
            p->setPen(QPen(border, 1));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(rf, radius, radius);

            // Checkmark or dash
            if (partial) {
                p->setPen(QPen(pal.highlightedText().color(), 2,
                               Qt::SolidLine, Qt::RoundCap));
                p->drawLine(QPointF(rf.left() + 3.5, rf.center().y()),
                            QPointF(rf.right() - 3.5, rf.center().y()));
            } else if (checked) {
                p->setPen(QPen(enabled ? pal.highlightedText().color()
                                       : pal.mid().color(),
                               1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p->setBrush(Qt::NoBrush);
                const QRectF cr = rf.adjusted(3, 3, -3, -3);
                QPainterPath tick;
                tick.moveTo(cr.left(),                    cr.top() + cr.height() * 0.5);
                tick.lineTo(cr.left() + cr.width() * 0.4, cr.bottom());
                tick.lineTo(cr.right(),                   cr.top());
                p->drawPath(tick);
            }

            p->restore();

            // Label — Adwaita handles text, shortcut underline, disabled dimming
            QStyleOptionButton lbl = *btn;
            lbl.rect = proxy()->subElementRect(SE_CheckBoxContents, opt, w);
            QProxyStyle::drawControl(CE_CheckBoxLabel, &lbl, p, w);
            return;
        }
        QProxyStyle::drawControl(ce, opt, p, w);
    }

    void drawComplexControl(ComplexControl cc, const QStyleOptionComplex *opt,
                            QPainter *p, const QWidget *w) const override
    {
        if (cc == CC_ComboBox && opt) {
            // Keep the "popupOpen" dynamic property in sync for all QComboBoxes
            // (not just DataTypeComboBox) so QSS [popupOpen="..."] rules work
            // universally.  State_On is Qt's standard "popup is open" flag.
            if (auto *combo = qobject_cast<QComboBox *>(const_cast<QWidget *>(w))) {
                const bool open = opt->state & State_On;
                if (combo->property("popupOpen").toBool() != open) {
                    combo->setProperty("popupOpen", open);
                    // Defer polish so we don't trigger a style change mid-paint.
                    QMetaObject::invokeMethod(combo, [combo]() {
                        combo->style()->unpolish(combo);
                        combo->style()->polish(combo);
                        combo->update();
                    }, Qt::QueuedConnection);
                }
            }
            // When the native popup is open (State_On) Qt issues a Leave event
            // that clears underMouse() and drops State_MouseOver, so Fusion falls
            // back to the default (non-hover) appearance.  Force State_MouseOver
            // back on so the combo stays visually active while the dropdown is
            // open — matching the behaviour of DataTypeComboBox which uses QMenu.
            bool needCopy = (opt->state & State_On) && !(opt->state & State_MouseOver);
            // Adwaita calls drawPrimitive(PE_FrameFocusRect) directly (not via
            // proxy()) for CC_ComboBox, bypassing our PE_FrameFocusRect suppression.
            // Strip State_HasFocus so that code path is never reached; our QSS
            // QComboBox:focus rule provides the visible focus indication instead.
            needCopy = needCopy || (opt->state & State_HasFocus);
            if (needCopy) {
                QStyleOptionComplex copy = *opt;
                if (opt->state & State_On)
                    copy.state |= State_MouseOver;
                copy.state &= ~State_HasFocus;
                QProxyStyle::drawComplexControl(cc, &copy, p, w);
                return;
            }
        }
        QProxyStyle::drawComplexControl(cc, opt, p, w);
    }

};

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
// On Windows, WA_TranslucentBackground is not reliable for popup tooltip
// windows — the platform plugin may not allocate per-pixel alpha for them.
// Instead, apply a rounded window region via setMask() on Show/Resize.
// setMask() maps to SetWindowRgn on Windows, which clips the window shape
// at the OS level and matches the QSS border-radius exactly.
//
// On other platforms (X11/Wayland), setMask() does not work on Wayland
// popup surfaces.  WA_TranslucentBackground is set at Polish time instead,
// allocating an alpha channel so Qt's QSS engine can clip through it.

class TooltipFilter : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (!obj->inherits("QTipLabel"))
            return false;
        auto *w = static_cast<QWidget *>(obj);
#ifdef Q_OS_WIN
        if (e->type() == QEvent::Show || e->type() == QEvent::Resize)
            applyRoundedMask(w);
#else
        if (e->type() == QEvent::Polish)
            w->setAttribute(Qt::WA_TranslucentBackground);
#endif
        return false;
    }
#ifdef Q_OS_WIN
private:
    static void applyRoundedMask(QWidget *w)
    {
        constexpr int R = 6;
        QBitmap bm(w->size());
        bm.fill(Qt::color0);
        QPainter p(&bm);
        p.setBrush(Qt::color1);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(w->rect(), R, R);
        w->setMask(bm);
    }
#endif
};

#ifndef Q_OS_WIN
// Applies the same transparent-background + shadow-overlay treatment as
// themeMenu() to any QMenu that Qt creates internally (e.g. right-click
// context menus in editable widgets) so they don't show the QSS margin: 8px
// ring as a solid platform-background rectangle.
class NativeMenuFilter : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (e->type() == QEvent::Polish) {
            if (QMenu *menu = qobject_cast<QMenu *>(obj)) {
                if (!menu->testAttribute(Qt::WA_TranslucentBackground)) {
                    menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint);
                    menu->setAttribute(Qt::WA_TranslucentBackground);
                    menu->setStyle(tightMenuStyle());
                    auto *overlay = new MenuShadowOverlay(menu);
                    overlay->show();
                    overlay->raise();
                }
            }
        }
        return false;
    }
};
#endif

void applyAdwaitaTheme(ColorScheme scheme)
{
    s_currentScheme = scheme;

    bool dark;
    switch (scheme) {
    case ColorScheme::Light: dark = false; break;
    case ColorScheme::Dark:  dark = true;  break;
    default:  // System: read from Qt 6.5+ colour scheme hint
        dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    }

    // On Windows use the built-in windows11 style; elsewhere prefer the native
    // Adwaita plugin and fall back to Fusion.
    // Wrap in NoFocusRectStyle to suppress dotted PE_FrameFocusRect indicators.
    {
#if 0//def Q_OS_WIN
        QStyle *base = QStyleFactory::create("windows11");
        if (!base) base = QStyleFactory::create("windowsvista");
        if (!base) base = QStyleFactory::create("Fusion");
#else
        QStyle *base = QStyleFactory::create(dark ? "adwaita-dark" : "adwaita");
        if (!base) base = QStyleFactory::create("Fusion");
#endif
        QApplication::setStyle(new NoFocusRectStyle(base));
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
#ifdef Q_OS_WIN
        qApp->installEventFilter(new DarkModeFilter(qApp));
#else
        qApp->installEventFilter(new NativeMenuFilter(qApp));
#endif
    }

#ifdef Q_OS_WIN
    // Re-apply dark-mode title bars to all currently visible top-levels
    // so that already-open dialogs update immediately on a scheme change.
    for (QWidget *w : QApplication::topLevelWidgets())
        if (w->isVisible())
            applyDwmDarkMode(w);
#endif
}

void setUiColourOverrides(const UiColourOverrides &o)
{
    if (o.window == s_uiOverrides.window &&
        o.windowText == s_uiOverrides.windowText &&
        o.toolbar == s_uiOverrides.toolbar && o.highlight == s_uiOverrides.highlight)
        return;
    s_uiOverrides = o;
    applyAdwaitaTheme(s_currentScheme);
}

const UiColourOverrides &uiColourOverrides()
{
    return s_uiOverrides;
}


void removeDialogIcon(QDialog *dlg)
{
#ifdef Q_OS_WIN
    QPixmap px(32, 32);
    px.fill(Qt::transparent);
    dlg->setWindowIcon(QIcon(px));
    // Remove WS_THICKFRAME so Windows doesn't show a resize cursor on the edges.
    dlg->setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);
#endif
}