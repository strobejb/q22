#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QPalette>
#include <QPoint>

enum class ColorScheme { System = 0, Light = 1, Dark = 2 };

// Per-palette UI colour overrides. An invalid QColor means "use theme default".
// PE_WINDOW    → titlebar / window-panel background
// PE_WINDOWTEXT → text on title bar, panels, and status bar
// PE_TOOLBAR   → undocumented statusbar/panel background override
// PE_UI_ACCENT → app accent colour (focus rings, selected list rows, active controls)
// PE_PANEL_DIVIDERS → dock panel separators / hairlines
struct UiColourOverrides {
    QColor window;
    QColor windowText;
    QColor toolbar;
    QColor uiAccent;
    QColor panelDividers;
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
// when a palette overrides Window/WindowText/UI Accent.
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
// Apply the same frameless / transparent / shadow treatment as themeMenu() to
// any popup QWidget (e.g. a custom QFrame panel).  The caller is responsible for
// setting window flags to Qt::Popup before calling this.
void themePopupWidget(QWidget *popup);

// Compute the global position for a popup anchored to a widget.
// The popup is placed immediately below the anchor (or above if needed).
// rightAlign=true pins the popup's right edge to the anchor's right edge.
// Overload accepting a pre-computed global anchor rect (e.g. when the anchor
// is not a full QWidget, such as a drawn button inside a larger widget).
#include <QRect>
#include <QSize>
QPoint smartMenuPos(QRect anchorGlobal, QSize popupSize, bool rightAlign = false);

// Returns the border/separator colour used by the current theme.
QColor themeBorderColor();

// Returns the foreground colour used for selected QMenu items and menu tick glyphs.
QColor menuSelectedTextColor(const QPalette &palette);

// Returns the destructive-action / error red for the current scheme.
// Matches Adwaita's @error_color: brighter in dark mode for contrast.
QColor errorColour();

// Shared warm warning/banner colours used by reload and recalculation notices.
QColor warningBannerAccent();
QColor warningBannerBackground(const QPalette &palette);

class QListWidget;
// Installs a delegate that enforces a minimum item height of (font height +
// 2*vPad) pixels, independent of QSS.  On KDE/Breeze, QListWidget::item
// padding in a stylesheet is applied to text positioning but NOT to the item's
// sizeHint(), causing items to appear cramped.  This bypasses that entirely.
// Also enables uniformItemSizes for layout efficiency.
// vPad defaults to half the widget's font height minus 2px, clamped to at least 2px.
void applyListItemPadding(QListWidget *list, int vPad = -1);


#include <QWidget>
// A separator sized to exactly 1 physical pixel at any DPI.
// Edge::Top  — line at top,    remainder blends into widget below (default).
// Edge::Bottom — line at bottom, remainder blends into widget above.
class Hairline : public QWidget
{
public:
    enum class Edge { Top, Bottom, Left, Right };
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
// Tries :/icons/actions/<name>.svg then :/icons/ui/<name>.svg; falls back to
// QIcon::fromTheme() when neither embedded resource exists.
QIcon recoloredIcon(const QString &name, const QColor &color, int sz = 16);

// Recolors QToolButton children and child/widget QAction icons with iconThemeName.
// Call at construction time and from changeEvent on QEvent::PaletteChange.
void recolorToolButtons(QWidget *parent);

// Compute the global position for a menu anchored to a widget.  The menu is
// placed immediately below the anchor when there is enough space, and
// immediately above when there is not — so it never obscures the anchor.
// Pass rightAlign=true to pin the menu's right edge to the anchor's right edge.
QPoint smartMenuPos(const QWidget *anchor, const QWidget *popup, bool rightAlign = false);
int themedMenuRightAlignOffset();

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
void prepareDialogForShow(QDialog *dlg, const QSize &size = {});
int execCentered(QDialog *dlg);

QColor platformAccentColour();
QColor matchLuminance(const QColor &source, const QColor &luminanceRef);

#endif // THEME_H
