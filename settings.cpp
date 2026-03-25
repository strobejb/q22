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

QString AppSettings::prefFontFamily()
{
    OPEN_SETTINGS;
    return s.value("preferences/fontFamily", "").toString();
}

void AppSettings::setPrefFontFamily(const QString &family)
{
    OPEN_SETTINGS;
    s.setValue("preferences/fontFamily", family);
}

int AppSettings::prefFontSize()
{
    OPEN_SETTINGS;
    return s.value("preferences/fontSize", 13).toInt();
}

void AppSettings::setPrefFontSize(int size)
{
    OPEN_SETTINGS;
    s.setValue("preferences/fontSize", size);
}

int AppSettings::prefHorizSpacing()
{
    OPEN_SETTINGS;
    return s.value("preferences/horizSpacing", 2).toInt();
}

void AppSettings::setPrefHorizSpacing(int px)
{
    OPEN_SETTINGS;
    s.setValue("preferences/horizSpacing", px);
}

int AppSettings::prefLineSpacing()
{
    OPEN_SETTINGS;
    return s.value("preferences/lineSpacing", 2).toInt();
}

void AppSettings::setPrefLineSpacing(int px)
{
    OPEN_SETTINGS;
    s.setValue("preferences/lineSpacing", px);
}

bool AppSettings::prefNativeMenu()
{
    OPEN_SETTINGS;
    return s.value("preferences/nativeMenu", true).toBool();
}

void AppSettings::setPrefNativeMenu(bool on)
{
    OPEN_SETTINGS;
    s.setValue("preferences/nativeMenu", on);
}

int AppSettings::prefColorScheme()
{
    OPEN_SETTINGS;
    return s.value("preferences/colorScheme", 0).toInt();
}

void AppSettings::setPrefColorScheme(int scheme)
{
    OPEN_SETTINGS;
    s.setValue("preferences/colorScheme", scheme);
}
