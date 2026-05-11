#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QPalette>
#include <QPoint>

enum class ColorScheme { System = 0, Light = 1, Dark = 2 };

// Per-palette UI colour overrides. An invalid QColor means "use theme default".
// PE_WINDOW    → titlebar / window-panel background
// PE_WINDOWTEXT → text on title bar, panels, and status bar
// PE_TOOLBAR   → status-bar background
// PE_PANELBORDERS → dock panel separators / hairlines
struct UiColourOverrides {
    QColor window;
    QColor windowText;
    QColor toolbar;
    QColor highlight;
    QColor panelBorders;
};

// Applies Adwaita Light or Dark using the built-in Fusion style + palette + QSS.
// Pass System to auto-detect from the Qt colour scheme hint (default).
// Safe to call at runtime to switch themes.
void applyAdwaitaTheme(ColorScheme scheme = ColorScheme::System);

// Returns true when the app is currently displaying in dark mode.
// Accounts for the explicit Dark/Light setting and, for System mode,
// the current application palette lightness.
bool isDarkMode();

// Layer palette-defined UI colours on top of the current Adwaita theme.
// Passing a default-constructed struct (all colours invalid) clears any
// previous overrides. The current colour scheme is re-applied immediately.
void setUiColourOverrides(const UiColourOverrides &o);

// Returns the currently active UI colour overrides (all invalid = no override).
const UiColourOverrides &uiColourOverrides();

// Returns the base Adwaita palette for the current scheme, without any active
// UI colour overrides applied. Use this for UI that should not shift colour
// when a palette overrides Window/WindowText/Highlight.
QPalette systemPalette();

// Takes the hue of `source` and returns a new colour with that hue but with
// the HSL saturation and lightness of `luminanceRef` (default: #A2D7FF).
// Achromatic inputs (grays) return `luminanceRef` unchanged.
QColor matchLuminance(const QColor &source, const QColor &luminanceRef = QColor("#A2D7FF"));

class QMenu;
// Call on every QMenu after construction so the stylesheet (including
// border-radius) renders correctly via a transparent frameless window.
void themeMenu(QMenu *menu);

class QWidget;
// Returns the border/separator colour used by the current theme.
QColor themeBorderColor();

class QListWidget;
// Installs a delegate that enforces a minimum item height of (font height +
// 2*vPad) pixels, independent of QSS.  On KDE/Breeze, QListWidget::item
// padding in a stylesheet is applied to text positioning but NOT to the item's
// sizeHint(), causing items to appear cramped.  This bypasses that entirely.
// Also enables uniformItemSizes for layout efficiency.
// vPad defaults to half the widget's font height, clamped to at least 4px.
void applyListItemPadding(QListWidget *list, int vPad = -1);


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

// Recolors QToolButton children and child/widget QAction icons with iconThemeName.
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
#include <QPoint>
#include <QSize>
class QMessageBox;
// Clears the window icon on a dialog so no app icon appears in the title bar.
void removeDialogIcon(QDialog *dlg);
void styleMessageBox(QMessageBox *box);

#if !defined(Q_OS_WIN)
// Requests the compositor shadow for a frameless window on KDE/KWin by
// dynamically loading libKF6WindowSystem or libKF5WindowSystem and calling
// KWindowEffects::enableShadow().  Silent no-op if neither library is present
// (GNOME, other DEs) so the call is safe on any Linux/BSD desktop.
// Must be called after the window's native handle exists (i.e. post-show).
void enableKWinShadow(QWidget *w);
#endif

// Centers dlg over its parent widget and calls exec() with no position flash.
//
// On Windows, Qt's normal show sequence is a two-step: CreateWindowEx places
// the HWND at a default position, then SetWindowPos corrects it — causing a
// visible jump.  Calling move() first sets Qt::WA_Moved so QDialog::setVisible
// skips its own adjustPosition() pass.  Calling winId() then forces the HWND
// to be created NOW, while the window is still hidden, so exec()'s ShowWindow
// just flips visibility in-place with no subsequent repositioning.
inline int execCentered(QDialog *dlg)
{
    if (QWidget *par = dlg->parentWidget()) {
        dlg->adjustSize();
        const QSize  sz = dlg->size();
        const QPoint c  = par->frameGeometry().center();
        dlg->move(c.x() - sz.width() / 2, c.y() - sz.height() / 2);
    }
#ifdef Q_OS_WIN
    (void)dlg->winId(); // force HWND creation while still hidden
#endif
    return dlg->exec();
}

QColor platformAccentColour();
QColor matchLuminance(const QColor &source, const QColor &luminanceRef);

#endif // THEME_H
