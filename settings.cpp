#include "settings.h"
#include <QSettings>

// Returns a ready-to-use QSettings object for the app's config file.
// QSettings is non-copyable so callers construct it directly via this helper
// macro rather than a factory function.
#define OPEN_SETTINGS \
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "qexed", "qexed")

QStringList AppSettings::recentFiles()
{
    OPEN_SETTINGS;
    return s.value("recentFiles").toStringList();
}

void AppSettings::addRecentFile(const QString &path)
{
    OPEN_SETTINGS;
    QStringList files = s.value("recentFiles").toStringList();
    files.removeAll(path);          // remove any existing copy (dedup)
    files.prepend(path);            // most recent first
    while (files.size() > MaxRecentFiles)
        files.removeLast();
    s.setValue("recentFiles", files);
    // QSettings::~QSettings calls sync() automatically on destruction.
}
