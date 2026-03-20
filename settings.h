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

} // namespace AppSettings

#endif // SETTINGS_H
