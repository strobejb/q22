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
// A horizontal separator sized to exactly 1 physical pixel at any DPI.
// Edge::Top  — line at top,    remainder blends into widget below (default).
// Edge::Bottom — line at bottom, remainder blends into widget above.
class Hairline : public QWidget
{
public:
    enum class Edge { Top, Bottom };
    // bgSource: widget whose palette().window() fills the non-line portion.
    // Pass e.g. the TitleBar so the gap colour tracks active/inactive changes.
    // If null, the gap is left transparent (shows parent background).
    explicit Hairline(QWidget *parent = nullptr, Edge edge = Edge::Top,
                      QWidget *bgSource = nullptr);
    void setBgSource(QWidget *bgSource);
protected:
    void showEvent(QShowEvent *) override;
    void paintEvent(QPaintEvent *) override;
    bool eventFilter(QObject *obj, QEvent *e) override;
private:
    Edge    m_edge;
    QWidget *m_bgSource;
};

class QIcon;
// Recolors a symbolic icon by compositing `color` through its alpha channel,
// the same technique GTK uses for -symbolic icons in Adwaita.  HiDPI-aware.
// Tries QIcon::fromTheme() first; falls back to the embedded resource at
// :/icons/hicolor/scalable/actions/<name>.svg when the theme lookup fails.
QIcon recoloredIcon(const QString &name, const QColor &color, int sz = 16);

// Recolors all QToolButton children of `parent` whose icon has a theme name.
// Call at construction time and from changeEvent on QEvent::PaletteChange.
void recolorToolButtons(QWidget *parent);

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


#include <QDialog>
// Clears the window icon on a dialog so no app icon appears in the title bar.
void removeDialogIcon(QDialog *dlg);

#endif // THEME_H
