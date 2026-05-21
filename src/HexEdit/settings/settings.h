#ifndef SETTINGS_H
#define SETTINGS_H

#include <QStringList>

// Returns a ready-to-use QSettings object for the app's config file.
// QSettings is non-copyable so callers construct it directly via this helper
// macro rather than a factory function.
#define OPEN_SETTINGS \
QSettings s(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName())


// Typed accessors for persistent application preferences.
// Data is stored as INI under the platform user-config location, using the
// org/app names set in main() via QCoreApplication::setOrganizationName /
// setApplicationName.  With org="catch22" and app="hexedit":
//   Linux:   ~/.config/catch22/hexedit.ini
//   Windows: %APPDATA%\catch22\hexedit.ini
//   macOS:   ~/Library/Preferences/catch22/hexedit.ini
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

// Bookmark behaviour
bool    prefBookmarkAutoExpand();  // true = enable both automatic bookmark expansion modes
void    setPrefBookmarkAutoExpand(bool on);
bool    prefBookmarkNested();      // true = allow overlapping bookmarks (default false)
void    setPrefBookmarkNested(bool on);
bool    prefBookmarkSelectionHighlights(); // true = activating a bookmark selects its byte range (default true)
void    setPrefBookmarkSelectionHighlights(bool on);

} // namespace AppSettings

#endif // SETTINGS_H
