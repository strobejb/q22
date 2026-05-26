#include "theme.h"
#include "settings/settings.h"
#include "chrome/dialog-chrome.h"
#include <functional>
#include <QAction>
#include <QApplication>
#include <QBitmap>
#include <QComboBox>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPointer>
#include <QCursor>
#include <QEasingCurve>
#include <QProxyStyle>
#include <QScrollBar>
#include <QStyleOption>
#include <QScreen>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOption>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVariantAnimation>
#include <QPointer>
#include <QWidget>
#include <QAbstractButton>
#ifdef HEXEDIT_HAVE_DBUS
#include <QDBusInterface>
#include <QDBusReply>
#endif
#ifndef Q_OS_WIN
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#endif

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
static QPalette          s_basePalette;
static QColor            s_platformAccent;

bool isDarkMode()
{
    if (s_currentScheme == ColorScheme::Dark)  return true;
    if (s_currentScheme == ColorScheme::Light) return false;
    return QApplication::palette().window().color().lightness() < 128;
}

static bool darkForScheme(ColorScheme scheme)
{
    switch (scheme) {
    case ColorScheme::Light: return false;
    case ColorScheme::Dark:  return true;
    default:
        return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    }
}

static QColor gnomeAccentColourFromName(QString name)
{
    name = name.trimmed();
    name.remove('\'');
    name = name.toLower();

    if (name == "blue")   return QColor("#3584e4");
    if (name == "teal")   return QColor("#2190a4");
    if (name == "green")  return QColor("#3a944a");
    if (name == "yellow") return QColor("#c88800");
    if (name == "orange") return QColor("#ed5b00");
    if (name == "red")    return QColor("#e62d42");
    if (name == "pink")   return QColor("#d56199");
    if (name == "purple") return QColor("#9141ac");
    if (name == "slate")  return QColor("#6f8396");

    return {};
}

static QColor gnomeAccentColour()
{
#ifdef Q_OS_LINUX
    const QString desktop = QProcessEnvironment::systemEnvironment()
                                .value("XDG_CURRENT_DESKTOP").toLower();
    if (!desktop.contains("gnome"))
        return {};

#ifdef HEXEDIT_HAVE_DBUS
    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.Settings"),
                          QDBusConnection::sessionBus());
    if (portal.isValid()) {
        const QDBusReply<QVariant> reply = portal.call(QStringLiteral("Read"),
                                                       QStringLiteral("org.gnome.desktop.interface"),
                                                       QStringLiteral("accent-color"));
        if (reply.isValid()) {
            const QColor c = gnomeAccentColourFromName(reply.value().toString());
            if (c.isValid())
                return c;
        }
    }
#endif

    QProcess proc;
    proc.start(QStringLiteral("gsettings"),
               {QStringLiteral("get"),
                QStringLiteral("org.gnome.desktop.interface"),
                QStringLiteral("accent-color")});
    if (proc.waitForFinished(500) && proc.exitCode() == 0) {
        const QColor c = gnomeAccentColourFromName(QString::fromUtf8(proc.readAllStandardOutput()));
        if (c.isValid())
            return c;
    }
#endif
    return {};
}

QColor platformAccentColour()
{
#ifdef Q_OS_WIN
    if (!s_platformAccent.isValid()) {
        DWORD accent = 0;
        BOOL opaque = FALSE;
        if (SUCCEEDED(DwmGetColorizationColor(&accent, &opaque))) {
            // DWM returns 0xAARRGGBB for the current Windows accent colour.
            s_platformAccent = QColor((accent >> 16) & 0xff,
                                      (accent >> 8)  & 0xff,
                                      accent         & 0xff);
        } else {
            // Fallback to the classic system highlight colour. COLORREF is
            // 0x00BBGGRR, so use the Win32 channel macros rather than shifts.
            const COLORREF highlight = GetSysColor(COLOR_HIGHLIGHT);
            s_platformAccent = QColor(GetRValue(highlight),
                                      GetGValue(highlight),
                                      GetBValue(highlight));
        }
    }
    if (s_platformAccent.isValid())
        return s_platformAccent;
#endif
#ifdef Q_OS_LINUX
    if (!s_platformAccent.isValid()) {
        // Qt exposes accent colours through QPalette::Accent rather than
        // QStyleHints, but the GNOME platform plugin may still return Qt's
        // generic blue. Prefer GNOME's own setting when it is available.
        s_platformAccent = gnomeAccentColour();
        if (!s_platformAccent.isValid())
            s_platformAccent = qApp->palette().color(QPalette::Accent);
    }
    if (s_platformAccent.isValid())
        return s_platformAccent;
#endif
    return qApp->palette().color(QPalette::Accent);//QColor("3584e4");
}

QColor matchLuminance(const QColor &source, const QColor &luminanceRef)
{
    const int hue = source.hslHue();
    if (hue < 0)
        return luminanceRef;
    return QColor::fromHsl(hue, luminanceRef.hslSaturation(), luminanceRef.lightness());
}

#ifdef Q_OS_WIN
#include "chrome/windows-chrome.h"
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
    if (m_edge == Edge::Left || m_edge == Edge::Right) {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setFixedWidth(1);
    } else {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(1);
    }
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
    const int logicalPx = qCeil(devicePixelRatioF());
    if (m_edge == Edge::Left || m_edge == Edge::Right)
        setFixedWidth(logicalPx);
    else
        setFixedHeight(logicalPx);
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
    const bool vertical  = m_edge == Edge::Left || m_edge == Edge::Right;
    const qreal extentPhys = (vertical ? width() : height()) * dpr;
    const qreal linePosPhys = (m_edge == Edge::Bottom || m_edge == Edge::Right)
                              ? (extentPhys - linePhys)
                              : 0.0;
    const qreal gapPhys = extentPhys - linePhys;

    p.scale(1.0 / dpr, 1.0 / dpr);

    // Fill the gap (non-line portion) with bgSource palette if provided.
    if (m_bgSource && gapPhys > 0) {
        const QColor bg = m_bgSource->palette().window().color();
        if (vertical) {
            const qreal gapLeft = (m_edge == Edge::Right) ? 0.0 : linePhys;
            p.fillRect(QRectF(gapLeft, 0, gapPhys, height() * dpr), bg);
        } else {
            const qreal gapTop = (m_edge == Edge::Bottom) ? 0.0 : linePhys;
            p.fillRect(QRectF(0, gapTop, width() * dpr, gapPhys), bg);
        }
    }

    if (vertical)
        p.fillRect(QRectF(linePosPhys, 0, linePhys, height() * dpr), themeBorderColor());
    else
        p.fillRect(QRectF(0, linePosPhys, width() * dpr, linePhys), themeBorderColor());
}

// ── Smart menu positioning ────────────────────────────────────────────────────

QColor themeBorderColor()
{
    const QColor override = uiColourOverrides().panelDividers;
    return override.isValid()
        ? override
        : QColor(QApplication::palette().mid().color().name());

    //bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    //return dark ? QColor("#4a4a4a") : QColor("#cdc7c2");
}

// Adwaita destructive / error red — tuned for legibility on both schemes.
QColor errorColour()
{
    return isDarkMode() ? QColor("#FF3F49") : QColor("#c01c28");
}

QColor warningBannerAccent()
{
    return QColor(QStringLiteral("#E39E0C"));
}

QColor warningBannerBackground(const QPalette &palette)
{
    const QColor base(QStringLiteral("#F2E4CA"));
    const bool dark = palette.window().color().lightness() < 128;
    return dark ? QColor(base.red(), base.green(), base.blue(), 70) : base;
}

#include <QIcon>
#include <QPixmap>
#include <QSet>
#include <QToolButton>

QIcon recoloredIcon(const QString &name, const QColor &color, int sz)
{
    // Load from the embedded SVG resource first — it is guaranteed to be a
    // pure alpha-keyed symbol, so SourceIn compositing always works correctly
    // regardless of platform icon engine behaviour (KDE's engine can return
    // pre-coloured pixmaps that break SourceIn).  Fall back to fromTheme() only
    // when the resource is absent, preserving support for icons not yet bundled.
    //
    // Callers that know the subdirectory pass a prefixed name ("actions/foo" or
    // "ui/foo") for a direct, single-probe lookup.  Bare names (no "/") probe
    // actions/ then ui/ via QFile::exists to avoid Qt SVG "Cannot open file"
    // warnings on the miss; this path handles dynamic / legacy callers.
    QIcon icon;
    if (name.contains(u'/')) {
        icon = QIcon(":/icons/" + name + ".svg");
    } else {
        auto tryPath = [](const QString &p) -> QIcon {
            return QFile::exists(p) ? QIcon(p) : QIcon{};
        };
        icon = tryPath(":/icons/actions/" + name + ".svg");
        if (icon.isNull())
            icon = tryPath(":/icons/ui/" + name + ".svg");
    }
    // Strip any "subdir/" prefix before the fromTheme fallback.
    if (icon.isNull()) {
        const int slash = name.lastIndexOf(u'/');
        icon = QIcon::fromTheme(slash >= 0 ? name.mid(slash + 1) : name);
    }
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
static QString iconThemeName(QObject *owner, const QIcon &icon)
{
    QString name = owner->property("iconThemeName").toString();
    if (name.isEmpty()) {
        if (icon.isNull()) return {};
        name = icon.name();
        if (!name.isEmpty())
            owner->setProperty("iconThemeName", name);
    }
    return name;
}

static QColor iconColorFor(QObject *owner, const QPalette &pal)
{
    const QString role = owner->property("iconColorRole").toString();
    if (role == QLatin1String("placeholderText"))
        return pal.placeholderText().color();
    return pal.buttonText().color();
}

static void recolorIconObject(QObject *owner, const QIcon &icon, int size,
                              const QPalette &pal,
                              const std::function<void(const QIcon &)> &setIcon)
{
    const QString name = iconThemeName(owner, icon);
    if (name.isEmpty()) return;

    const int propSize = owner->property("iconSize").toInt();
    const int iconSize = propSize > 0 ? propSize : qMax(size, 16);
    const QIcon ic = recoloredIcon(name, iconColorFor(owner, pal), iconSize);
    if (!ic.isNull())
        setIcon(ic);
}

void recolorToolButtons(QWidget *parent)
{
    const QPalette pal = parent->palette();
    for (auto *btn : parent->findChildren<QToolButton*>()) {
        recolorIconObject(btn, btn->icon(), btn->iconSize().width(), pal,
                          [btn](const QIcon &icon) { btn->setIcon(icon); });
    }

    QSet<QAction *> actions;
    for (QAction *action : parent->findChildren<QAction*>())
        actions.insert(action);
    for (QWidget *widget : parent->findChildren<QWidget*>()) {
        for (QAction *action : widget->actions())
            actions.insert(action);
    }
    for (QAction *action : actions) {
        const QWidget *ownerWidget = qobject_cast<QWidget *>(action->parent());
        const QPalette actionPal = ownerWidget ? ownerWidget->palette() : pal;
        recolorIconObject(action, action->icon(), 16, actionPal,
                          [action](const QIcon &icon) { action->setIcon(icon); });
    }
}

#include <QListWidget>

namespace {
// Enforces a minimum item height on a QListWidget without relying on QSS
// padding — which KDE/Breeze's style does not factor into sizeHint().
class PaddedListDelegate : public QStyledItemDelegate {
    int m_minH;
public:
    explicit PaddedListDelegate(int minH, QObject *parent)
        : QStyledItemDelegate(parent), m_minH(minH) {}

    QSize sizeHint(const QStyleOptionViewItem &opt,
                   const QModelIndex &idx) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        return QSize(s.width(), qMax(s.height(), m_minH));
    }
};
} // namespace

void applyListItemPadding(QListWidget *list, int vPad)
{
    if (!list)
        return;
    if (vPad < 0)
        vPad = qMax(2, list->fontMetrics().height() / 2 - 2);
    const int minH = list->fontMetrics().height() + 2 * vPad;
    list->setUniformItemSizes(true);
    list->setItemDelegate(new PaddedListDelegate(minH, list));
}

QPoint smartMenuPos(QRect ag, QSize sz, bool rightAlign)
{
    QScreen *screen = QGuiApplication::screenAt(ag.center());
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

QPoint smartMenuPos(const QWidget *anchor, const QWidget *popup, bool rightAlign)
{
    return smartMenuPos(QRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size()),
                        popup->sizeHint(), rightAlign);
}

int themedMenuRightAlignOffset()
{
#ifdef Q_OS_WIN
    return 0;
#else
    // The Linux QMenu shadow is drawn in an 8px QSS margin. menuShadowFilter()
    // moves the popup window left by that margin on show, so right-aligned menus
    // must be placed two margins to the right: one to cancel that move, one so
    // the visible frame's right edge, not the transparent shadow ring, aligns.
    return 2 * kMenuShadowMargin;
#endif
}

// ── Palette ───────────────────────────────────────────────────────────────────

static void applyPalette(bool dark)
{
    // Four anchor colours per mode — everything else is derived from these.
    // setColorScheme() is called in applyAdwaitaTheme() before this function so
    // the GNOME platform plugin may have already re-asserted the system palette;
    // setting QApplication::setPalette() here overwrites that side effect.
    const QColor window    = dark ? QColor("#242424") : QColor("#f6f5f4");
    const QColor windowText = dark ? QColor("#deddda") : QColor("#2e3436");
    const QColor base         = dark ? window.darker(120) : Qt::white;
    const QColor baseHighlight =  platformAccentColour();//baseHighlight;//matchLuminance(platformAccentColour());
    const QColor baseHlText   = baseHighlight.lightness() < 160 ? Qt::white : windowText;

    // Derived roles — no additional hard-coded values beyond the four above.
    const QColor button    = dark ? window.lighter(155) : window.darker(105);
    const QColor altBase   = dark ? base.lighter(112)   : base.darker(103);
    const QColor ph        = QColor(windowText.red(), windowText.green(),
                                    windowText.blue(), 128); // 50% opacity text

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
    p.setColor(QPalette::Highlight,       baseHighlight);
    p.setColor(QPalette::HighlightedText, baseHlText);
    p.setColor(QPalette::Accent,          baseHighlight);
    p.setColor(QPalette::Link,            dark ? baseHighlight.lighter(130) : baseHighlight);
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

    // Save the scheme palette before any per-palette overrides are applied.
    // systemPalette() returns this so that UI that should not shift colour
    // when a palette overrides Window/Highlight/etc. can use stable values.
    s_basePalette = p;

    // Layer UI palette overrides on top.
    if (s_uiOverrides.window.isValid()) {
        const QColor w = s_uiOverrides.window;
        p.setColor(QPalette::Window, w);
        // Re-derive Button and border tones from the override window colour so
        // that push-button hover/active shades and palette(button) references
        // everywhere track the custom colour rather than the Adwaita default.
        const QColor overrideBtn = dark ? w.lighter(155) : w.darker(105);
        p.setColor(QPalette::Button, overrideBtn);
        const QColor overrideMid = dark ? w.lighter(222) : w.darker(130);
        p.setColor(QPalette::Mid,      overrideMid);
        p.setColor(QPalette::Midlight, overrideMid.lighter(100));
    }
    if (s_uiOverrides.windowText.isValid()) {
        p.setColor(QPalette::WindowText, s_uiOverrides.windowText);
        p.setColor(QPalette::Text,       s_uiOverrides.windowText);
        p.setColor(QPalette::ButtonText, s_uiOverrides.windowText);
    }
    if (s_uiOverrides.uiAccent.isValid()) {
        const QColor hlOver     = s_uiOverrides.uiAccent;
        const QColor hlOverText = hlOver.lightness() < 160 ? Qt::white : windowText;
        p.setColor(QPalette::Highlight,       hlOver);
        p.setColor(QPalette::HighlightedText, hlOverText);
        p.setColor(QPalette::Accent,          hlOver);
        p.setColor(QPalette::Link,            dark ? hlOver.lighter(130) : hlOver);
    }

    QApplication::setPalette(p);
}

// ── Scrollbar geometry ────────────────────────────────────────────────────────
// All scrollbar sizing lives here.  Change kSbThumbWidth, kSbOuter, kSbInner;
// everything else derives automatically.
//
// Layout (vertical scrollbar, cross-axis = x):
//
//   ├─ kSbInner ─┤── kSbThumbWidth ──┤─ kSbOuter ─┤
//   ←──────────────── kSbWidth ───────────────────→  widget width / PM_ScrollBarExtent
//
// kSbOuter / kSbInner are applied as handle cross-axis margin so the visual
// thumb is kSbThumbWidth px wide.  No cross-axis margin on the widget itself —
// the widget fills the full allocated slot (hit area = widget width).
// Hover highlights when the mouse is directly over the handle subcontrol.
static constexpr int kSbThumbWidth  = 7;   // rendered thumb width
static constexpr int kSbOuter       = 4;   // gap on far side   (right / bottom)
static constexpr int kSbInner       = 3;   // gap on near side  (left  / top)
static constexpr int kSbWidth       = kSbThumbWidth + kSbOuter + kSbInner;  // = 14

static constexpr int kPanelSbThumbWidth = kSbThumbWidth - 2; // file properties panel
static constexpr int kPanelSbMask       = 1;
static constexpr int kPanelSbOuter      = kSbOuter - kPanelSbMask;
static constexpr int kPanelSbInner      = kSbWidth - kPanelSbOuter - kSbThumbWidth;
static_assert(kPanelSbThumbWidth == 5);
static_assert(kPanelSbOuter + kPanelSbMask == 4);

static constexpr int kPreferenceSbThumbWidth = 4; // preferences + slide overlays
static constexpr int kPreferenceSbMask       = 1;
static constexpr int kPreferenceSbOuter      = kSbOuter - kPreferenceSbMask;
static constexpr int kPreferenceSbInner      = kSbWidth - kPreferenceSbOuter
                                             - kPreferenceSbThumbWidth
                                             - 2 * kPreferenceSbMask;
static constexpr int kPreferenceSbRadius     = 3;
static_assert(kPreferenceSbThumbWidth == 4);
static_assert(kPreferenceSbOuter + kPreferenceSbMask == 4);

enum class ScrollBarArrowPressEffect {
    Shrink,
    Invert
};

static constexpr ScrollBarArrowPressEffect kScrollBarArrowPressEffect =
    ScrollBarArrowPressEffect::Shrink;
static constexpr qreal kSbArrowPressedScale = 0.70;
static constexpr int kSbArrowReleaseAnimMs = 120;

// ── Stylesheet ────────────────────────────────────────────────────────────────

static QString buildStylesheet(bool dark)
{
    // Derive the handful of colours that have no direct palette() role from
    // the current application palette (set by applyPalette moments earlier).
    const QPalette pal = QApplication::palette();

    const QColor btn  = pal.button().color();
    const bool darkBtn = btn.lightness() < 128;
    const QString fgDisabled       = pal.color(QPalette::Disabled, QPalette::WindowText).name();
    const QString btnHover         = (darkBtn ? btn.lighter(118) : btn.darker(103)).name();
    const QString btnActive        = (darkBtn ? btn.lighter(133) : btn.darker(106)).name();
    const bool    menuUseHighlight  = AppSettings::prefMenuHighlight();
    const bool    scrollbarArrows  = AppSettings::prefScrollbarArrows();
    const QString menuSelBg        = menuUseHighlight ? "palette(highlight)"  : btnHover;
    const QString menuSelFg        = menuUseHighlight ? "palette(highlighted-text)" : "palette(window-text)";
    const QColor  windowColor      = pal.window().color();
    const QString statusComboHover = (darkBtn ? windowColor.lighter(130) : windowColor.darker(107)).name();
    const QString statusComboOpen  = (darkBtn ? windowColor.lighter(155) : windowColor.darker(115)).name();
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
QDialog { background: palette(window); }

/* ── Menu bar ────────────────────────────────────────────────── */
QMenuBar {
    background: palette(window);
}
QMenuBar::item {
    background: transparent;
    padding: 4px 8px;
}
QMenuBar::item:selected {
    background: {menuSelBg};
    color: {menuSelFg};
    border-radius: 4px;
}
QMenuBar::item:pressed {
    background: {btnActive};
    border-radius: 4px;
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
    padding: 6px 28px 6px 28px;
    min-width: 180px;
    border-radius: 4px;
    margin: 1px 4px;
}
QMenu::item:selected {
    background: {menuSelBg};
    color: {menuSelFg};
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
/* Focus borders are 2 px, normal borders are 1 px. Keep margin at 0 so the
   whole widget rect is repainted on focus changes; a transparent 1 px margin
   can leave stale focus-border pixels behind on some styles/compositors.
   Compensate with 1 extra px of padding in the non-focused states instead. */
QLineEdit {
    border: 1px solid palette(mid);
    border-radius: 6px;
    padding: 6px 9px;
    margin: 0px;
    background: palette(base);
    selection-background-color: palette(highlight);
    selection-color: palette(highlighted-text);
}
QLineEdit:hover { border: 1px solid palette(mid); padding: 6px 9px; margin: 0px; }
QLineEdit:focus { border: 2px solid palette(highlight); padding: 5px 8px; margin: 0px; background: palette(base); }
QLineEdit:disabled { color: {fgDisabled}; background: palette(window); }
QLabel:disabled { color: {fgDisabled}; }
QCheckBox:disabled { color: {fgDisabled}; }

/* ── Plain text edits ────────────────────────────────────────── */
QPlainTextEdit {
    border: 1px solid palette(mid);
    border-radius: 6px;
    padding: 6px 9px;
    margin: 0px;
    background: palette(base);
    selection-background-color: palette(highlight);
    selection-color: palette(highlighted-text);
}
QPlainTextEdit:focus { border: 2px solid palette(highlight); padding: 5px 8px; margin: 0px; }
QPlainTextEdit:disabled { color: {fgDisabled}; }

/* ── Spin boxes ──────────────────────────────────────────────── */
QAbstractSpinBox {
    border: 1px solid palette(mid);
    border-radius: 6px;
    padding: 6px 9px;
    margin: 0px;
    min-height: {inputMinH}px;
    background: palette(base);
    selection-background-color: palette(highlight);
    selection-color: palette(highlighted-text);
}
QAbstractSpinBox:hover { border: 2px solid palette(mid); padding: 5px 8px; margin: 0px; }
QAbstractSpinBox:focus { border: 2px solid palette(highlight); padding: 5px 8px; margin: 0px; background: palette(base); }
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
/* Same padding-compensation rule as text inputs: the 1 px padding difference
   absorbs the thicker focus border without moving combo text or leaving an
   unpainted outer margin. */
QComboBox {
    background: palette(base);
    border: 1px solid palette(mid);
    border-radius: 6px;
    padding: 4px 9px;
    margin: 0px;
    selection-background-color: palette(highlight);
    selection-color: palette(highlighted-text);
}
QComboBox:hover            { border: 1px solid palette(mid); padding: 4px 9px; margin: 0px; background: palette(window); }
QComboBox:focus            { border: 2px solid palette(highlight); padding: 3px 8px; margin: 0px; background: palette(base); }
QComboBox:open             { background: palette(button); }
/* QMenu-backed combos set popupOpen manually.  The explicit false state forces
   QStyleSheetStyle to repaint the closed background after click-away close. */
QComboBox[popupOpen="false"] { background: palette(base); }
QComboBox[popupOpen="false"]:hover { background: palette(window); }
QComboBox[popupOpen="false"]:focus,
QComboBox[popupOpen="false"]:hover:focus { background: palette(window); }
QComboBox[popupOpen="true"],
QComboBox[popupOpen="true"]:hover,
QComboBox[popupOpen="true"]:focus,
QComboBox[popupOpen="true"]:hover:focus { background: {statusComboOpen};  }
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
/* Geometry tokens are substituted at runtime from the kSb* constants:        */
/*   {sbWidth}  = kSbWidth  (widget width / PM_ScrollBarExtent)                */
/*   {sbOuter}  = kSbOuter  (gap on far side)                                  */
/*   {sbInner}  = kSbInner  (gap on near side)                                 */
/*   {scrollbarArrowQss} = arrow sub-line/add-line rules (or zero-size stubs)  */
QAbstractScrollArea::corner { background: transparent; border: none; }
/* No cross-axis margin on the widget — it fills the full allocated slot so
   clicks anywhere in the gutter hit the scrollbar (no dead zones).
   The thumb's visual width comes from the handle's cross-axis margin alone. */
QScrollBar:vertical   { background: transparent; border-radius: 3px; width: {sbWidth}px; margin: {sbOuter}px 0 {sbOuter}px 0; }
QScrollBar:horizontal { background: transparent; border-radius: 3px; height: {sbWidth}px; margin: 0 {sbOuter}px 0 {sbOuter}px; }
/* handle margin = kSbInner (near) + kSbOuter (far) → visual thumb = kSbThumbWidth px wide. */
QScrollBar::handle:vertical   { background: palette(mid); border-radius: 3px; min-height: 24px; margin: 0 {sbOuter}px 0 {sbInner}px; }
QScrollBar::handle:horizontal { background: palette(mid); border-radius: 3px; min-width:  24px; margin: {sbInner}px 0 {sbOuter}px 0; }
/* Normal hover (mouse over handle, no button held).  Gutter-click hover —
   when the thumb slides under a held cursor — is handled by ScrollBarArrowPainter. */
QScrollBar::handle:vertical:hover,
QScrollBar::handle:horizontal:hover { background: palette(dark); }
QScrollBar[scrollHintOverlay=true]:vertical {
    margin: 2px 2px 12px 2px;
}
QScrollBar[filePropertiesScrollBar=true]:vertical {
    margin: 8px 0px 8px 0px;
}
QScrollBar[filePropertiesScrollBar=true]::handle:vertical {
    background: palette(mid);
    border-left: {panelSbMask}px solid transparent;
    border-right: {panelSbMask}px solid transparent;
    border-radius: 3px;
    margin: 0 {panelSbOuter}px 0 {panelSbInner}px;
}
QScrollBar[filePropertiesScrollBar=true]::handle:vertical:hover {
    background: palette(dark);
}
QScrollBar[filePropertiesScrollBar=true]:disabled,
QScrollBar[filePropertiesScrollBar=true]::handle:vertical:disabled {
    background: transparent;
    border: none;
}
QScrollBar[preferenceScrollBar=true]:vertical {
    margin: 8px 0px 8px 0px;
}
QScrollBar[preferenceScrollBar=true]::handle:vertical {
    background: palette(mid);
    border-left: {preferenceSbMask}px solid transparent;
    border-right: {preferenceSbMask}px solid transparent;
    border-radius: {preferenceSbRadius}px;
    margin: 0 {preferenceSbOuter}px 0 {preferenceSbInner}px;
}
QScrollBar[preferenceScrollBar=true]::handle:vertical:hover {
    background: palette(dark);
}
{scrollbarArrowQss}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical,
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

/* ── Status bar ──────────────────────────────────────────────── */
QStatusBar {
    background: palette(window);

    padding: 0;
    /*border-top: 1px solid palette(mid);*/
}
QStatusBar QComboBox {
    background: palette(window);
    border: 1px solid transparent;
    border-radius: 4px;
    margin: 0px;
}
QStatusBar QComboBox:hover { background: {statusComboHover}; border: 1px solid palette(mid); margin: 0px; }
QStatusBar QComboBox:focus { background: palette(window); border: 1px solid palette(mid); margin: 0px; }
QStatusBar QComboBox:open,
QStatusBar QComboBox:open:hover,
QStatusBar QComboBox:open:focus,
QStatusBar QComboBox:open:hover:focus { background: {statusComboOpen}; border: 1px solid palette(mid); margin: 0px; }
/* See the general popupOpen=false rule above.  Closed status-bar combos also
   need the transparent border restored when the cursor has left the control. */
QStatusBar QComboBox[popupOpen="false"] { background: palette(window); border: 1px solid transparent; margin: 0px; }
QStatusBar QComboBox[popupOpen="false"]:hover { background: {statusComboHover}; border: 1px solid palette(mid); margin: 0px; }
QStatusBar QComboBox[popupOpen="false"]:focus,
QStatusBar QComboBox[popupOpen="false"]:hover:focus { background: palette(window); border: 1px solid palette(mid); margin: 0px; }
QStatusBar QComboBox[popupOpen="true"],
QStatusBar QComboBox[popupOpen="true"]:hover,
QStatusBar QComboBox[popupOpen="true"]:focus,
QStatusBar QComboBox[popupOpen="true"]:hover:focus { background: {statusComboOpen}; border: 1px solid palette(mid); margin: 0px; }

/* ── Misc ────────────────────────────────────────────────────── */
QAbstractScrollArea { border: none; }
QToolTip {
    background: palette(tooltip-base);
    color: palette(tooltip-text);
    border: 1px solid palette(mid);
    border-radius: 6px;
    padding: {tooltipPad};
}

/* ── QFileDialog ─────────────────────────────────────────────── */
/* QFileDialog owns ordinary private controls. Keep the edit and combo padding
   equal here; installThemedFileDialogComboPopups() then syncs the two-row
   button box to the resulting styled input height. Focus padding is 1 px
   smaller so the 2 px focus border does not shift the text. */
QFileDialog QLineEdit         { padding: 5px 8px; margin: 0; }
QFileDialog QLineEdit:hover   { padding: 5px 8px; margin: 0; }
QFileDialog QLineEdit:focus   { padding: 4px 7px; margin: 0; }
QFileDialog QComboBox         { padding: 5px 8px; margin: 0; }
QFileDialog QComboBox:hover   { padding: 5px 8px; margin: 0; }
QFileDialog QComboBox:focus   { padding: 4px 7px; margin: 0; background: palette(base); }
QFileDialog QListView,
QFileDialog QTreeView {
    border: 1px solid palette(mid);
    background: palette(base);
}
QFileDialog QListView::item,
QFileDialog QTreeView::item {
    padding-top: 2px;
    padding-bottom: 2px;
}

)";

    QString ss = QString::fromLatin1(TMPL);
    ss.replace("{fgDisabled}",       fgDisabled);
    ss.replace("{inputMinH}",        inputMinH);
    ss.replace("{btnHover}",         btnHover);
    ss.replace("{btnActive}",        btnActive);
    ss.replace("{menuSelBg}",        menuSelBg);
    ss.replace("{menuSelFg}",        menuSelFg);
    ss.replace("{statusComboHover}", statusComboHover);
    ss.replace("{statusComboOpen}",  statusComboOpen);
#ifdef Q_OS_WIN
    ss.replace("{menuMargin}",  QString());
    ss.replace("{tooltipPad}",  "1px 6px");
#else
    ss.replace("{menuMargin}",  "margin: 8px;");
    ss.replace("{tooltipPad}",  "2px 6px");
#endif

    // Fill the scrollbar geometry tokens used in the QSS template above.
    const QString sbw  = QString::number(kSbWidth);
    const QString sbo  = QString::number(kSbOuter);
    const QString sbi  = QString::number(kSbInner);
    const QString psbo = QString::number(kPanelSbOuter);
    const QString psbi = QString::number(kPanelSbInner);
    const QString psbm = QString::number(kPanelSbMask);
    const QString prefSbo = QString::number(kPreferenceSbOuter);
    const QString prefSbi = QString::number(kPreferenceSbInner);
    const QString prefSbm = QString::number(kPreferenceSbMask);
    const QString prefSbr = QString::number(kPreferenceSbRadius);
    ss.replace("{sbWidth}",  sbw);
    ss.replace("{sbOuter}",  sbo);
    ss.replace("{sbInner}",  sbi);
    ss.replace("{panelSbOuter}", psbo);
    ss.replace("{panelSbInner}", psbi);
    ss.replace("{panelSbMask}",  psbm);
    ss.replace("{preferenceSbOuter}", prefSbo);
    ss.replace("{preferenceSbInner}", prefSbi);
    ss.replace("{preferenceSbMask}",  prefSbm);
    ss.replace("{preferenceSbRadius}", prefSbr);

    // Scrollbar arrow buttons — shown when the user setting is on, otherwise
    // collapsed to zero-size so no track space is reserved.
    // {scrollbarArrowQss} is always replaced (both branches emit all four rules).
    if (scrollbarArrows) {
        // Arrow buttons are scoped to scrollbars tagged with the hexViewScrollBar
        // property (set in HexView's constructor) so they never appear on other
        // scrollbars (e.g. preferences dialog panels).
        //
        // All QScrollBars first get explicit zero-size stubs so Qt doesn't
        // reserve space for sub-line/add-line subcontrols by default; the
        // hex-view-scoped rules below then override those to the full button size.
        //
        // The scroll-axis margin is widened to kSbWidth px at each end to reserve
        // space for the sub-line/add-line subcontrols.  Cross-axis margin stays 0
        // — the handle's cross-axis margin already handles the visual thumb inset,
        // and adding it here too would shrink the content area twice.
        //
        // The arrow triangles themselves are drawn by ScrollBarArrowPainter (an
        // application-wide event filter) on top of the normal Qt paint — no image
        // files needed, colours track the live palette.
        const QString pressedQss =
            kScrollBarArrowPressEffect == ScrollBarArrowPressEffect::Invert
                ? QStringLiteral("QScrollBar[hexViewScrollBar=true]::sub-line:pressed,\n"
                                 "QScrollBar[hexViewScrollBar=true]::add-line:pressed { background: palette(mid); }\n")
                : QString();
        ss.replace("{scrollbarArrowQss}", QStringLiteral(
            "QScrollBar::add-line:vertical,   QScrollBar::sub-line:vertical   { height: 0; }\n"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:  0; }\n"
            "\nQScrollBar[hexViewScrollBar=true]:vertical   { margin: %1px 0px %1px 0px; }\n"
            "QScrollBar[hexViewScrollBar=true]:horizontal { margin: 0px %1px 0px %1px; }\n"
            "QScrollBar[hexViewScrollBar=true]::sub-line:vertical   { height: %1px; subcontrol-position: top;    subcontrol-origin: margin; background: transparent; }\n"
            "QScrollBar[hexViewScrollBar=true]::add-line:vertical   { height: %1px; subcontrol-position: bottom; subcontrol-origin: margin; background: transparent; }\n"
            "QScrollBar[hexViewScrollBar=true]::sub-line:horizontal { width:  %1px; subcontrol-position: left;   subcontrol-origin: margin; background: transparent; }\n"
            "QScrollBar[hexViewScrollBar=true]::add-line:horizontal { width:  %1px; subcontrol-position: right;  subcontrol-origin: margin; background: transparent; }\n"
            "%2"
        ).arg(sbw).arg(pressedQss));
    } else {
        ss.replace("{scrollbarArrowQss}",
            "QScrollBar::add-line:vertical,   QScrollBar::sub-line:vertical   { height: 0; }\n"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:  0; }\n");
    }

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
            auto *combo = qobject_cast<QComboBox *>(const_cast<QWidget *>(w));
            // Keep "popupOpen" in sync with State_On so QSS [popupOpen="true"] rules
            // and ValueComboBox::leaveEvent work for combos using a QMenu popup.
            // Capture wasOpen BEFORE writing the new value so we can detect the
            // open→close transition below.
            bool popupJustClosed = false;
            if (combo) {
                const bool open = opt->state & State_On;
                const bool wasOpen = combo->property("popupOpen").toBool();
                if (wasOpen != open) {
                    // setProperty triggers an immediate QDynamicPropertyChangeEvent
                    // which causes QStyleSheetStyle to synchronously repolish the
                    // widget, updating combo->palette() before we return.
                    combo->setProperty("popupOpen", open);
                    popupJustClosed = wasOpen && !open;
                }
            }
            // When the popup is open (State_On), Qt fires a Leave event that drops
            // State_MouseOver so Fusion falls back to the flat default appearance.
            // Force State_MouseOver on so the combo looks active while the dropdown
            // is visible — matching the behaviour of the QMenu-based custom combos.
            bool needCopy = (opt->state & State_On) && !(opt->state & State_MouseOver);
            // Adwaita calls drawPrimitive(PE_FrameFocusRect) directly for CC_ComboBox
            // (not via proxy), bypassing our suppression.  Strip State_HasFocus here
            // so that path is never reached; QSS QComboBox:focus provides the border.
            needCopy = needCopy || (opt->state & State_HasFocus);
            // Strip stale State_Sunken (arrowState) that lingers after a Qt::Popup
            // auto-close.  When the user clicks outside, the popup window closes
            // before mouseReleaseEvent reaches the combo, so _q_resetButton() is
            // never called and the native style keeps drawing the pressed gradient.
            const bool stripSunken = (opt->state & State_Sunken) && !(opt->state & State_On);
            needCopy = needCopy || stripSunken;
            // If the popup just closed, force a copy so we can pull in the freshly
            // repolished palette.  QStyleSheetStyle already drew its background fill
            // using the OLD palette (palette(button) from [popupOpen="true"]) before
            // calling us; replacing opt->palette with the now-correct palette ensures
            // the native style draws with the right colours on this very frame.
            needCopy = needCopy || popupJustClosed;
            if (needCopy) {
                QStyleOptionComplex copy = *opt;
                if (opt->state & State_On)
                    copy.state |= State_MouseOver;
                if (stripSunken)
                    copy.state &= ~State_Sunken;
                copy.state &= ~State_HasFocus;
                if (popupJustClosed && combo)
                    copy.palette = combo->palette();  // fresh post-repolish palette
                QProxyStyle::drawComplexControl(cc, &copy, p, w);
                drawStandardComboArrow(&copy, p, w);
                return;
            }
        }
        QProxyStyle::drawComplexControl(cc, opt, p, w);
        if (cc == CC_ComboBox)
            drawStandardComboArrow(opt, p, w);
    }

    void drawStandardComboArrow(const QStyleOptionComplex *opt,
                                QPainter *p, const QWidget *w) const
    {
        const auto *combo = qobject_cast<const QComboBox *>(w);
        if (!combo || combo->metaObject()->className() == QByteArray("MenuComboBox"))
            return;

        const QRect r = proxy()->subControlRect(CC_ComboBox, opt, SC_ComboBoxArrow, w);
        if (!r.isValid())
            return;

        const QPoint c = r.center();
        const int h = qMax(4, qMin(r.width(), r.height()) / 4);
        const QColor color = opt->palette.color(
            (opt->state & State_Enabled) ? QPalette::ButtonText : QPalette::Mid);

        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(Qt::NoPen);
        p->setBrush(color);
        QPainterPath arrow;
        arrow.moveTo(c.x() - h, c.y() - h / 2);
        arrow.lineTo(c.x() + h, c.y() - h / 2);
        arrow.lineTo(c.x(), c.y() + h / 2);
        arrow.closeSubpath();
        p->drawPath(arrow);
        p->restore();
    }

};

// Paints scrollbar arrow triangles on top of the normal scrollbar paint.
// QStyleSheetStyle handles CC_ScrollBar entirely when scrollbar QSS rules
// exist — it renders the sub-line/add-line button backgrounds from QSS but
// never calls drawPrimitive for the arrow indicator itself.  This event
// filter intercepts QEvent::Paint on every QScrollBar, lets the normal Qt
// paint run (drawing the transparent button area), then draws the arrow
// triangle on top using live palette colours and real-time mouse state.
class ScrollBarArrowPainter : public QObject
{
    bool m_painting = false;

    // Per-scrollbar hover state tracked from events rather than queried at
    // paint time.  QCursor::pos() can be stale when a deferred paint fires
    // after the mouse has already left, causing the hover colour to stick.
    struct BtnState {
        bool subHov = false;
        bool addHov = false;
        bool subPressed = false;
        bool addPressed = false;
        qreal subScale = 1.0;
        qreal addScale = 1.0;
        QPointer<QVariantAnimation> subAnim;
        QPointer<QVariantAnimation> addAnim;
    };
    QHash<QScrollBar *, BtnState> m_state;

public:
    explicit ScrollBarArrowPainter(QObject *parent = nullptr) : QObject(parent) {}

    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        auto *sb = qobject_cast<QScrollBar *>(obj);
        if (!sb)
            return false;

        const auto t = ev->type();

        // Only activate for HexView scrollbars (tagged in HexView's constructor).
        // This mirrors the QSS scoping: other scrollbars have no arrow space reserved.
        if (!AppSettings::prefScrollbarArrows() || !sb->property("hexViewScrollBar").toBool())
            return false;

        if (t == QEvent::HoverMove) {
            // Update stored hover state from the event's position (accurate)
            // and repaint if it changed.
            const QPoint mp = static_cast<QHoverEvent *>(ev)->position().toPoint();
            QRect subR, addR;
            buttonRects(sb, subR, addR);
            BtnState &prev = m_state[sb];
            const bool subHov = subR.contains(mp);
            const bool addHov = addR.contains(mp);
            if (subHov != prev.subHov || addHov != prev.addHov) {
                prev.subHov = subHov;
                prev.addHov = addHov;
                sb->update(subR);
                sb->update(addR);
            }
            return false;
        }

        if (t == QEvent::HoverLeave || t == QEvent::Leave) {
            BtnState &s = m_state[sb];
            if (s.subHov || s.addHov) {
                s.subHov = false;
                s.addHov = false;
                QRect subR, addR;
                buttonRects(sb, subR, addR);
                sb->update(subR);
                sb->update(addR);
            }
            return false;
        }

        if (t == QEvent::MouseButtonPress || t == QEvent::MouseButtonRelease) {
            QRect subR, addR;
            buttonRects(sb, subR, addR);
            BtnState &s = m_state[sb];
            if (t == QEvent::MouseButtonPress) {
                const QPoint mp = static_cast<QMouseEvent *>(ev)->position().toPoint();
                if (subR.contains(mp)) {
                    stopArrowAnim(s, true);
                    s.subPressed = true;
                    s.subScale = kScrollBarArrowPressEffect == ScrollBarArrowPressEffect::Shrink
                        ? kSbArrowPressedScale : 1.0;
                } else if (addR.contains(mp)) {
                    stopArrowAnim(s, false);
                    s.addPressed = true;
                    s.addScale = kScrollBarArrowPressEffect == ScrollBarArrowPressEffect::Shrink
                        ? kSbArrowPressedScale : 1.0;
                }
            } else {
                if (s.subPressed) {
                    s.subPressed = false;
                    animateArrowScale(sb, true, s.subScale, 1.0);
                }
                if (s.addPressed) {
                    s.addPressed = false;
                    animateArrowScale(sb, false, s.addScale, 1.0);
                }
            }
            sb->update(subR);
            sb->update(addR);
            return false;
        }

        if (t == QEvent::Destroy) {
            m_state.remove(sb);
            return false;
        }

        if (m_painting || t != QEvent::Paint)
            return false;

        m_painting = true;
        QCoreApplication::sendEvent(sb, ev);
        m_painting = false;
        paintArrows(sb);
        return true;
    }

private:
    // Arrow buttons are kSbWidth × kSbWidth px squares — their scroll-axis
    // extent equals the widget's cross-axis width, which is also kSbWidth.
    static constexpr int kBtnSize = kSbWidth;

    static void stopArrowAnim(BtnState &s, bool sub)
    {
        QPointer<QVariantAnimation> &anim = sub ? s.subAnim : s.addAnim;
        if (anim) {
            anim->stop();
            anim->deleteLater();
            anim = nullptr;
        }
    }

    void animateArrowScale(QScrollBar *sb, bool sub, qreal from, qreal to)
    {
        if (kScrollBarArrowPressEffect != ScrollBarArrowPressEffect::Shrink) {
            BtnState &s = m_state[sb];
            (sub ? s.subScale : s.addScale) = to;
            return;
        }

        BtnState &s = m_state[sb];
        stopArrowAnim(s, sub);

        auto *anim = new QVariantAnimation(sb);
        anim->setDuration(kSbArrowReleaseAnimMs);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->setStartValue(from);
        anim->setEndValue(to);
        if (sub)
            s.subAnim = anim;
        else
            s.addAnim = anim;

        connect(anim, &QVariantAnimation::valueChanged, sb, [this, sb, sub](const QVariant &value) {
            BtnState &state = m_state[sb];
            (sub ? state.subScale : state.addScale) = value.toDouble();
            QRect subR, addR;
            buttonRects(sb, subR, addR);
            sb->update(sub ? subR : addR);
        });
        connect(anim, &QVariantAnimation::finished, sb, [this, sb, sub, anim]() {
            BtnState &state = m_state[sb];
            (sub ? state.subScale : state.addScale) = 1.0;
            QPointer<QVariantAnimation> &slot = sub ? state.subAnim : state.addAnim;
            if (slot == anim)
                slot = nullptr;
            anim->deleteLater();
        });
        anim->start();
    }

    // Returns the current handle rect in widget coordinates.
    // NOTE: only valid when arrow buttons are enabled (kBtnSize strips the
    // arrow zones; without arrows the track starts at kSbOuter, not kSbWidth).
    // Derived from our known geometry constants and the scrollbar's live
    // value/range — avoids QStyleSheetStyle::subControlRect which can return
    // the full widget rect when QStyleOptionSlider isn't initialised exactly
    // as Qt's internal code does.
    static QRect handleSubRect(const QScrollBar *sb)
    {
        const bool vert     = (sb->orientation() == Qt::Vertical);
        const int  total    = vert ? sb->height() : sb->width();
        const int  range    = sb->maximum() - sb->minimum();
        const int  trackLen = total - 2 * kBtnSize;  // strip the two arrow zones

        if (range <= 0 || trackLen <= 2)
            return QRect();

        const int ps        = sb->pageStep();
        const int handleLen = qMax(24, trackLen * ps / (range + ps));
        const int maxTravel = trackLen - handleLen;
        const int handleOff = kBtnSize +
                              (maxTravel > 0 ? maxTravel * (sb->value() - sb->minimum()) / range : 0);

        // Cross-axis position matches the handle margin in the stylesheet:
        //   vertical   → x = kSbInner, w = kSbThumbWidth
        //   horizontal → y = kSbInner, h = kSbThumbWidth
        return vert ? QRect(kSbInner, handleOff, kSbThumbWidth, handleLen)
                    : QRect(handleOff, kSbInner, handleLen,     kSbThumbWidth);
    }

    static void buttonRects(const QScrollBar *sb, QRect &subR, QRect &addR)
    {
        const QRect r = sb->rect();
        if (sb->orientation() == Qt::Vertical) {
            subR = QRect(r.left(), r.top(),                    r.width(), kBtnSize);
            addR = QRect(r.left(), r.bottom() - kBtnSize + 1, r.width(), kBtnSize);
        } else {
            subR = QRect(r.left(),                   r.top(), kBtnSize, r.height());
            addR = QRect(r.right() - kBtnSize + 1,  r.top(), kBtnSize, r.height());
        }
    }

    void paintArrows(QScrollBar *sb)
    {
        const bool vert = (sb->orientation() == Qt::Vertical);
        QRect subR, addR;
        buttonRects(sb, subR, addR);

        const BtnState &s   = m_state[sb];
        const bool      btn = QApplication::mouseButtons() & Qt::LeftButton;

        auto arrowCol = [&](bool hov, bool pressed) -> QColor {
            if (kScrollBarArrowPressEffect == ScrollBarArrowPressEffect::Invert && hov && pressed)
                return sb->palette().color(QPalette::Light);
            if (hov)        return sb->palette().color(QPalette::Dark);
            return                 sb->palette().color(QPalette::Mid);
        };

        // Centre the icon over the visual thumb track.
        // Thumb occupies [kSbInner, kSbInner+kSbThumbWidth] = [3, 10], centre = 6.5.
        // sz = 11 (odd) → crossOff = (kSbWidth - sz) / 2 = 1 → icon at [1,12], centre = 6.5 ✓
        const int sz        = kSbWidth - 3;                           // = 11
        const int crossOff  = (kSbWidth - sz) / 2;                   // = 1
        auto iconRect = [&](const QRect &btnR, qreal scale) -> QRect {
            const int scrollDim = vert ? btnR.height() : btnR.width();
            const int scrollOff = (scrollDim - sz) / 2;
            const QRect full = vert
                ? QRect(btnR.left() + crossOff, btnR.top() + scrollOff, sz, sz)
                : QRect(btnR.left() + scrollOff, btnR.top() + crossOff, sz, sz);
            if (kScrollBarArrowPressEffect != ScrollBarArrowPressEffect::Shrink)
                return full;

            const int scaled = qMax(1, qRound(sz * scale));
            return QRect(full.center().x() - scaled / 2,
                         full.center().y() - scaled / 2,
                         scaled, scaled);
        };

        QPainter p(sb);

        // If the left button is held and the thumb has slid under the cursor,
        // overpaint it in the hover colour.  Qt won't fire ::handle:hover while
        // a button is down, so we handle it manually here on every repaint.
        if (btn) {
            const QPoint cur = sb->mapFromGlobal(QCursor::pos());
            const QRect  hr  = handleSubRect(sb);
            if (hr.contains(cur)) {
                p.setPen(Qt::NoPen);
                p.setBrush(sb->palette().color(QPalette::Dark));
                p.setRenderHint(QPainter::Antialiasing);
                p.drawRoundedRect(hr, 3, 3);
            }
        }

        auto draw = [&](const QRect &btnR, const QString &name, bool hov, bool pressed, qreal scale) {
            const QRect ir = iconRect(btnR, scale);
            recoloredIcon(name, arrowCol(hov, pressed), qMin(ir.width(), ir.height())).paint(&p, ir);
        };

        if (vert) {
            draw(subR, QStringLiteral("ui/scrollbar-up-symbolic"),    s.subHov, s.subPressed, s.subScale);
            draw(addR, QStringLiteral("ui/scrollbar-down-symbolic"),  s.addHov, s.addPressed, s.addScale);
        } else {
            draw(subR, QStringLiteral("ui/scrollbar-left-symbolic"),  s.subHov, s.subPressed, s.subScale);
            draw(addR, QStringLiteral("ui/scrollbar-right-symbolic"), s.addHov, s.addPressed, s.addScale);
        }
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
            // Draw object-select-symbolic from bundled resources instead of
            // delegating to the base style, which renders a raster bitmap on
            // GNOME/Adwaita and looks pixelated at any non-native size.
            if (opt->state & State_On) {
                const bool sel = opt->state & State_Selected;
                const QColor color = opt->palette.color(
                    sel ? QPalette::HighlightedText : QPalette::WindowText);
                const QRect r = opt->rect.translated(kGlyphInset, 0);
                recoloredIcon("actions/object-select-symbolic", color, r.height()).paint(p, r);
            }
            // Unchecked: no-op — space is already reserved by CE_MenuItem.
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

static void ensureScrollBarArrowPainter()
{
    // Installed once on the application object.  The event filter checks
    // prefScrollbarArrows() at paint time so the setting can be toggled
    // without reinstalling the filter.
    //
    // Responsibilities:
    //  • Draw arrow triangles on sub-line/add-line buttons (QSS provides the
    //    button background; Qt's QStyleSheetStyle never draws the indicator).
    //  • Overpaint the thumb in hover colour when the mouse button is held and
    //    the thumb slides under the cursor (Qt suppresses ::handle:hover while
    //    any mouse button is down).
    static ScrollBarArrowPainter *s = nullptr;
    if (!s) {
        s = new ScrollBarArrowPainter(qApp);
        qApp->installEventFilter(s);
    }
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

void themePopupWidget(QWidget *popup)
{
    // Same frameless / transparent / shadow treatment as themeMenu(), without the
    // TightMenuStyle (which only handles check-column spacing inside QMenu items).
#ifdef Q_OS_WIN
    popup->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
#else
    popup->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
#endif
    popup->setAttribute(Qt::WA_TranslucentBackground);
    popup->setAttribute(Qt::WA_StyledBackground, true);
    popup->installEventFilter(menuShadowFilter());
#ifndef Q_OS_WIN
    auto *overlay = new MenuShadowOverlay(popup);
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
#ifdef Q_OS_WIN
                    themeMenu(menu);
#else
                    menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint);
                    menu->setAttribute(Qt::WA_TranslucentBackground);
                    menu->setStyle(tightMenuStyle());
                    auto *overlay = new MenuShadowOverlay(menu);
                    overlay->show();
                    overlay->raise();
#endif
                }
            }
        }
        return false;
    }
};


#ifndef Q_OS_WIN
// Reads the system UI font from whichever desktop environment is active.
// On KDE, kdeglobals stores the font in QFont::toString() wire format so it
// can be loaded directly.  On GNOME, gsettings returns "Family Size" as a
// single-quoted string.  Falls back to Qt's current default if neither is
// found, leaving whatever the platform plugin set (if anything) untouched.
static QFont detectSystemFont()
{
    // ── KDE: ~/.config/kdeglobals ─────────────────────────────────────────
    // General/font = "Noto Sans,10,-1,5,400,0,0,0,0,0,Regular"
    // This is exactly the format QFont::fromString() understands.
    {
        const QString path = QStandardPaths::locate(
            QStandardPaths::GenericConfigLocation, "kdeglobals");
        if (!path.isEmpty()) {
            QSettings s(path, QSettings::IniFormat);
            const QString str = s.value("General/font").toString();
            QFont f;
            if (!str.isEmpty() && f.fromString(str))
                return f;
        }
    }

    // ── GNOME: gsettings ──────────────────────────────────────────────────
    // gsettings get org.gnome.desktop.interface font-name → 'Cantarell 11'
    {
        QProcess p;
        p.start("gsettings",
                {"get", "org.gnome.desktop.interface", "font-name"});
        if (p.waitForFinished(500)) {
            QString out = p.readAllStandardOutput().trimmed();
            out.remove('\'').remove('"');
            const int sp = out.lastIndexOf(' ');
            if (sp > 0) {
                bool ok = false;
                const int sz = out.mid(sp + 1).toInt(&ok);
                if (ok && sz > 0)
                    return QFont(out.left(sp), sz);
            }
        }
    }

    // ── Fallback: leave Qt's current default untouched ────────────────────
    return QApplication::font();
}
#endif

void applyAdwaitaTheme(ColorScheme scheme)
{
    s_currentScheme = scheme;
    const bool dark = darkForScheme(scheme);

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
        ensureScrollBarArrowPainter();
    }

#ifndef Q_OS_WIN
    // GNOME Wayland: server-side decorations track the system colour scheme.
    // setColorScheme() is the only Qt API that signals the compositor to switch
    // the title bar between dark and light — it forwards the preference to the
    // Qt Wayland platform plugin which propagates it toward the compositor.
    //
    // Side effect: the GNOME platform plugin responds to colorSchemeChanged() by
    // calling QApplication::setPalette() with the system palette, overwriting
    // whatever the app had set.  We call applyPalette() immediately below so that
    // our custom palette is always the last one written.
    //
    // For ColorScheme::System we pass Unknown so the compositor follows its own
    // judgment, and any previous forced override is cleared.
    {
        const Qt::ColorScheme cs =
            (scheme == ColorScheme::Dark)  ? Qt::ColorScheme::Dark  :
            (scheme == ColorScheme::Light) ? Qt::ColorScheme::Light :
                                             Qt::ColorScheme::Unknown;
        QGuiApplication::styleHints()->setColorScheme(cs);
    }
#endif
    applyPalette(dark);
    qApp->setStyleSheet(buildStylesheet(dark));

#ifndef Q_OS_WIN
    // GNOME/Qt can apply the platform palette after setColorScheme() returns.
    // Re-assert the app palette/QSS on the next turn so explicit Light/Dark
    // switches take effect on the first click.
    QTimer::singleShot(0, qApp, [scheme]() {
        if (s_currentScheme != scheme)
            return;
        const bool settledDark = darkForScheme(scheme);
        applyPalette(settledDark);
        qApp->setStyleSheet(buildStylesheet(settledDark));
    });
#endif

    // One-time setup: font preference and tooltip filter.
    static bool firstRun = true;
    if (firstRun) {
        firstRun = false;
#ifndef Q_OS_WIN
        QApplication::setFont(detectSystemFont());
#endif
        qApp->installEventFilter(new TooltipFilter(qApp));
#ifdef Q_OS_WIN
        installDarkModeFilter();
#endif
        qApp->installEventFilter(new NativeMenuFilter(qApp));
    }

#ifdef Q_OS_WIN
    sweepDwmDarkMode();
#endif
}

void setUiColourOverrides(const UiColourOverrides &o)
{
    if (o.window == s_uiOverrides.window &&
        o.windowText == s_uiOverrides.windowText &&
        o.toolbar == s_uiOverrides.toolbar &&
        o.uiAccent == s_uiOverrides.uiAccent &&
        o.panelDividers == s_uiOverrides.panelDividers)
        return;
    s_uiOverrides = o;

    // Palette selection only changes colour roles layered on top of the current
    // scheme.  Rebuilding the whole application style here is noticeably slow
    // and unnecessary; reserve applyAdwaitaTheme() for actual light/dark scheme
    // changes.
    const bool dark = darkForScheme(s_currentScheme);
    applyPalette(dark);
    qApp->setStyleSheet(buildStylesheet(dark));
}

const UiColourOverrides &uiColourOverrides()
{
    return s_uiOverrides;
}

QPalette systemPalette()
{
    return s_basePalette;
}

void prepareDialogForShow(QDialog *dlg, const QSize &size)
{
    if (!dlg)
        return;

    installDialogChrome(dlg);

    QPalette pal = dlg->palette();
    pal.setColor(QPalette::Window, qApp->palette().color(QPalette::Window));
    dlg->setPalette(pal);
    // Avoid the Windows first-show white flash: before the hidden HWND exists,
    // make the top-level dialog paint/erase with the app palette instead of the
    // platform default COLOR_WINDOW brush.  Custom frameless dialogs on Windows
    // are translucent because their DialogShadowFrame paints the rounded
    // background and shadow margin, so do not auto-fill those top-levels.
    if (dlg->property("_qexedDialogShadowInstalled").toBool()) {
        dlg->setAttribute(Qt::WA_StyledBackground, false);
        dlg->setAutoFillBackground(false);
    } else {
        dlg->setAttribute(Qt::WA_StyledBackground, true);
        dlg->setAutoFillBackground(true);
    }
    dlg->ensurePolished();

    if (size.isValid()) {
        QSize actual = size;
#ifdef Q_OS_WIN
        const int left = dlg->property("_qexedDialogShadowLeft").toInt();
        const int top = dlg->property("_qexedDialogShadowTop").toInt();
        const int right = dlg->property("_qexedDialogShadowRight").toInt();
        const int bottom = dlg->property("_qexedDialogShadowBottom").toInt();
        if (left || top || right || bottom)
            actual += QSize(left + right, top + bottom);
#endif
        dlg->resize(actual);
    } else if (qobject_cast<QFileDialog *>(dlg)) {
        // QFileDialog's central file view is expanding. Calling adjustSize()
        // after import/export adds extra option rows can make the dialog grow
        // vertically on first show, so preserve Qt's chosen/default size.
    } else {
        dlg->adjustSize();
    }

    if (QWidget *par = dlg->parentWidget()) {
        const QSize sz = dlg->size();
        const QPoint c = par->frameGeometry().center();
        dlg->move(c.x() - sz.width() / 2, c.y() - sz.height() / 2);
    }
#ifdef Q_OS_WIN
    // Force HWND creation while hidden, then explicitly pin its native
    // geometry. Some Windows/Qt paths still create the hidden HWND at the
    // platform default position before applying QWidget::pos(); SetWindowPos
    // here ensures the first visible frame is already at the final geometry.
    HWND hwnd = reinterpret_cast<HWND>(dlg->winId());
    const QRect g = dlg->geometry();
    SetWindowPos(hwnd, nullptr, g.x(), g.y(), g.width(), g.height(),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
#endif
}

int execCentered(QDialog *dlg)
{
    if (!dlg)
        return QDialog::Rejected;

    prepareDialogForShow(dlg);
    return dlg->exec();
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

void styleMessageBox(QMessageBox *box)
{
    if (!box) return;
    removeDialogIcon(box);
    if (!AppSettings::prefNativeDialogs()) {
        box->setWindowFlag(Qt::FramelessWindowHint, true);
#ifdef Q_OS_WIN
        // QMessageBox finishes arranging its private grid late, so the titlebar
        // chrome is installed from the deferred polish pass below.  The Windows
        // transparent surface, however, must exist before show or the later
        // self-painted shadow margin can be backed by black pixels.
        box->setAttribute(Qt::WA_TranslucentBackground, true);
        box->setAutoFillBackground(false);
#endif
    }

    QTimer::singleShot(0, box, [box]() {
        auto *iconLabel = box->findChild<QLabel *>(QStringLiteral("qt_msgboxex_icon_label"));
        auto *textLabel = box->findChild<QLabel *>(QStringLiteral("qt_msgbox_label"));
        auto *infoLabel = box->findChild<QLabel *>(QStringLiteral("qt_msgbox_informativelabel"));
        if (!iconLabel || !textLabel)
            return;

#ifdef Q_OS_WIN
        textLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        if (infoLabel)
            infoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
#endif

        int textH = textLabel->sizeHint().height();
        if (infoLabel && infoLabel->isVisible())
            textH += infoLabel->sizeHint().height();

        const int iconH = iconLabel->sizeHint().height();
        const int topInset = qMax(0, (iconH - textH) / 2);
        textLabel->setContentsMargins(0, topInset, 0, 0);
    });

    if (auto *buttonBox = box->findChild<QDialogButtonBox *>()) {
        buttonBox->setContentsMargins(0, 24, 0, 0);
        if (buttonBox->layout())
            buttonBox->layout()->setSpacing(12);
        for (QAbstractButton *btn : buttonBox->buttons())
            btn->setIcon(QIcon());
    }

    QPointer<QMessageBox> guard(box);
    QTimer::singleShot(0, box, [guard]() {
        if (!guard)
            return;

        installDialogChrome(guard);

        QPalette pal = guard->palette();
        pal.setColor(QPalette::Window, qApp->palette().color(QPalette::Window));
        guard->setPalette(pal);
        if (guard->property("_qexedDialogShadowInstalled").toBool()) {
            guard->setAttribute(Qt::WA_StyledBackground, false);
            guard->setAutoFillBackground(false);
        } else {
            guard->setAttribute(Qt::WA_StyledBackground, true);
            guard->setAutoFillBackground(true);
        }
        guard->ensurePolished();
        guard->adjustSize();

        if (QWidget *parent = guard->parentWidget()) {
            const QSize sz = guard->size();
            const QPoint c = parent->frameGeometry().center();
            guard->move(c.x() - sz.width() / 2, c.y() - sz.height() / 2);
        }
    });
}

#ifndef Q_OS_WIN
#include <QLibrary>
void enableKWinShadow(QWidget *w)
{
    if (!w || !w->windowHandle()) return;

    // Dynamic symbol resolution: the binary is built without KDE headers (e.g.
    // on GitHub CI) and discovers the library at runtime on the user's machine.
    //
    // QLibrary is QObject-derived and therefore non-copyable.  Each attempt must
    // use its own stack object; they cannot be assigned or stored in containers.
    // We use setFileName / setFileNameAndVersion on a default-constructed object
    // rather than the value-initialising constructors to avoid this pitfall.
    //
    // The GCC-mangled names are stable ABI: KDE guarantees binary compatibility
    // within a major Frameworks version, and the symbols encode only the class
    // name, method name, and parameter types — none of which change for this API.
    //   KF6: KWindowEffects::enableShadow(QWindow*, bool)
    //        _ZN14KWindowEffects12enableShadowEP7QWindowb
    //   KF5: KWindowEffects::enableShadow(WId, bool, const QVector<uint>*)
    //        _ZN14KWindowEffects12enableShadowEmbPK7QVectorIjE
    //
    // On non-KDE desktops (GNOME, XFCE, …) neither library is present and the
    // function returns immediately on every call after the first.
    using Fn6 = void(*)(QWindow*, bool);
    using Fn5 = void(*)(unsigned long, bool, const void*);

    static Fn6  s_fn6     = nullptr;
    static Fn5  s_fn5     = nullptr;
    static bool s_resolved = false;

    if (!s_resolved) {
        s_resolved = true;

        // Helper: load a library by name+version and resolve one symbol.
        // Returns a null QFunctionPointer on any failure.
        // ver > 0 → look for libName.so.ver  (e.g. libKF6WindowSystem.so.6)
        // ver = 0 → look for libName.so       (bare symlink; present on dev machines)
        auto tryLoad = [](const QString &name, int ver, const char *sym) -> QFunctionPointer {
            QLibrary lib;
            if (ver > 0) lib.setFileNameAndVersion(name, ver);
            else         lib.setFileName(name);
            return lib.load() ? lib.resolve(sym) : nullptr;
        };

        constexpr const char *kSym6 = "_ZN14KWindowEffects12enableShadowEP7QWindowb";
        constexpr const char *kSym5 = "_ZN14KWindowEffects12enableShadowEmbPK7QVectorIjE";

        if (auto fp = tryLoad(QStringLiteral("KF6WindowSystem"), 6, kSym6))
            s_fn6 = reinterpret_cast<Fn6>(fp);
        else if (auto fp = tryLoad(QStringLiteral("KF6WindowSystem"), 0, kSym6))
            s_fn6 = reinterpret_cast<Fn6>(fp);
        else if (auto fp = tryLoad(QStringLiteral("KF5WindowSystem"), 5, kSym5))
            s_fn5 = reinterpret_cast<Fn5>(fp);
        else if (auto fp = tryLoad(QStringLiteral("KF5WindowSystem"), 0, kSym5))
            s_fn5 = reinterpret_cast<Fn5>(fp);
    }

    if      (s_fn6) s_fn6(w->windowHandle(), true);
    else if (s_fn5) s_fn5(static_cast<unsigned long>(w->winId()), true, nullptr);
}
#endif
