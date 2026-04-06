#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QPoint>

enum class ColorScheme { System = 0, Light = 1, Dark = 2 };

// Per-palette UI colour overrides. An invalid QColor means "use theme default".
// PE_WINDOW    → titlebar / window-panel background
// PE_WINDOWTEXT → text on title bar, panels, and status bar
// PE_TOOLBAR   → status-bar background
struct UiColourOverrides {
    QColor window;
    QColor windowText;
    QColor toolbar;
    QColor highlight;
};

// Applies Adwaita Light or Dark using the built-in Fusion style + palette + QSS.
// Pass System to auto-detect from the Qt colour scheme hint (default).
// Safe to call at runtime to switch themes.
void applyAdwaitaTheme(ColorScheme scheme = ColorScheme::System);

// Layer palette-defined UI colours on top of the current Adwaita theme.
// Passing a default-constructed struct (all colours invalid) clears any
// previous overrides. The current colour scheme is re-applied immediately.
void setUiColourOverrides(const UiColourOverrides &o);

// Returns the currently active UI colour overrides (all invalid = no override).
const UiColourOverrides &uiColourOverrides();

class QMenu;
// Call on every QMenu after construction so the stylesheet (including
// border-radius) renders correctly via a transparent frameless window.
void themeMenu(QMenu *menu);


class QWidget;
// Returns the border/separator colour used by the current theme.
QColor themeBorderColor();


#include <QWidget>
// A horizontal separator that always renders as exactly 1 physical pixel,
// regardless of DPI scale factor or sub-pixel widget position.
// It achieves this by painting a QRectF whose height is 1.0/devicePixelRatioF()
// logical pixels — which the painter's device transform maps to exactly 1
// physical pixel row on the backing surface.
class Hairline : public QWidget
{
    Q_OBJECT
public:
    explicit Hairline(QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *) override;
};

#ifndef Q_OS_WIN
class QIcon;
// Recolors a symbolic icon by compositing `color` through its alpha channel,
// the same technique GTK uses for -symbolic icons in Adwaita.  HiDPI-aware.
QIcon recoloredIcon(const QString &name, const QColor &color, int sz = 16);

// Recolors all QToolButton children of `parent` whose icon has a theme name.
// Call at construction time and from changeEvent on QEvent::PaletteChange.
void recolorToolButtons(QWidget *parent);
#endif

// Compute the global position for a menu anchored to a widget.  The menu is
// placed immediately below the anchor when there is enough space, and
// immediately above when there is not — so it never obscures the anchor.
// Pass rightAlign=true to pin the menu's right edge to the anchor's right edge.
QPoint smartMenuPos(const QWidget *anchor, const QMenu *menu, bool rightAlign = false);

#ifdef Q_OS_WIN
class QIcon;
class QColor;
// Render a single Segoe MDL2 Assets / Segoe Fluent Icons glyph as a
// DPR-aware QIcon.  Returns a null QIcon if neither font is available.
// logicalPx is the font/draw size in logical (device-independent) pixels.
QIcon segoeIcon(uint codePoint, const QColor &color, int logicalPx = 14);
#endif

#endif // THEME_H
