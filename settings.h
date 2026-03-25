#ifndef SETTINGS_H
#define SETTINGS_H

#include <QStringList>

// Typed accessors for persistent application preferences.
// Data is stored as INI under the platform user-config location:
//   Linux:   ~/.config/qexed/qexed.ini
//   Windows: %APPDATA%\qexed\qexed.ini
//   macOS:   ~/Library/Preferences/qexed/qexed.ini
// (To use a hidden ~/.qexed/ directory instead, replace the QSettings
//  constructor in settings.cpp with the path-based overload.)
namespace AppSettings {

constexpr int MaxRecentFiles = 10;

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
int     prefColorScheme();          // 0=System, 1=Light, 2=Dark
void    setPrefColorScheme(int scheme);

} // namespace AppSettings

#endif // SETTINGS_H
