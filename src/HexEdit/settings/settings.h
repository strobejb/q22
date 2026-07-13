#ifndef SETTINGS_H
#define SETTINGS_H

#include "settings/appconfig.h"

#include <QByteArray>
#include <QFont>
#include <QStringList>

// Returns a ready-to-use QSettings object for the app's config file.
// QSettings is non-copyable so callers construct it directly via this helper
// macro rather than a factory function.
#define OPEN_SETTINGS \
QSettings s(AppSettings::settingsFilePath(), QSettings::IniFormat)


// Typed accessors for persistent application preferences.
// Data is stored as INI under the platform user-config location, using the
// org/app names set in main() via QCoreApplication::setOrganizationName /
// setApplicationName.  With org="catch22" and app="q22":
//   Linux:   ~/.config/catch22/q22/q22.ini
//   Windows: %APPDATA%\catch22\q22\q22.ini
//   macOS:   ~/Library/Preferences/catch22/q22/q22.ini
namespace AppSettings {

// Call once at startup (before any other AppSettings function) to guarantee
// the settings directory exists before any QSettings object is constructed.
// Qt caches writability at construction time, so the directory must exist first.
void ensureSettingsDir();

constexpr int MaxRecentFiles = 10;
constexpr int MaxRecentPalettes = 5;

QStringList recentFiles();
void        addRecentFile(const QString &path);

// Preferences dialog settings
QString prefFontFamily();
void    setPrefFontFamily(const QString &family);
int     prefFontSize();
void    setPrefFontSize(int size);
QFont   defaultHexFont();
QFont   hexFont();
int     prefHorizSpacing();
void    setPrefHorizSpacing(int px);
int     prefLineSpacing();
void    setPrefLineSpacing(int px);
bool    prefNativeMenu();
void    setPrefNativeMenu(bool on);
bool    prefNativeFileDialogs();
void    setPrefNativeFileDialogs(bool on);
bool    prefNativeDialogs();
void    setPrefNativeDialogs(bool on);
bool    prefRestoreWindowGeometry();
void    setPrefRestoreWindowGeometry(bool on);
QByteArray windowGeometry();
void       setWindowGeometry(const QByteArray &geometry);
bool    prefMenuHighlight();        // true = use palette(highlight) for menu selection
void    setPrefMenuHighlight(bool on);
bool    prefScrollbarArrows();      // true = show arrow buttons on scrollbar hover
void    setPrefScrollbarArrows(bool on);
int     prefColorScheme();          // 0=System, 1=Light, 2=Dark
void    setPrefColorScheme(int scheme);
QString prefPaletteName();          // empty = no saved palette (use defaults)
void    setPrefPaletteName(const QString &name);
QStringList prefRecentPalettes();   // CSV-backed, most recent first
void        addRecentPalette(const QString &name);
bool    prefRecentPaletteOrdering(); // true = compact palette picker uses recent-first order
void    setPrefRecentPaletteOrdering(bool on);
bool    prefStatusbarToolsRight();
void    setPrefStatusbarToolsRight(bool on);
bool    prefStatusbarInfoRight();
void    setPrefStatusbarInfoRight(bool on);

// Hex display format
int     prefBytesPerLine();
void    setPrefBytesPerLine(int bytes);
int     prefBytesPerGroup();
void    setPrefBytesPerGroup(int bytes);
QString prefDataFormat();          // "hex", "dec", "oct", or "bin"
void    setPrefDataFormat(const QString &format);

// Bookmark behaviour
bool    prefBookmarkAutoExpand();  // true = enable both automatic bookmark expansion modes
void    setPrefBookmarkAutoExpand(bool on);
bool    prefBookmarkNested();      // true = allow overlapping bookmarks (default false)
void    setPrefBookmarkNested(bool on);
bool    prefBookmarkSelectionHighlights(); // true = activating a bookmark selects its byte range (default true)
void    setPrefBookmarkSelectionHighlights(bool on);

bool    prefSectionHeaderDoubleClick();    // true = double-click section header triggers expand/scroll-to-top
void    setPrefSectionHeaderDoubleClick(bool on);

QStringList sidePanelSectionOrder();
void        setSidePanelSectionOrder(const QStringList &order);
int         sidePanelWidth();              // 0 = use default width
void        setSidePanelWidth(int width);

} // namespace AppSettings

#endif // SETTINGS_H
