#include "settings.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QCoreApplication>

//#define ORG "Catch22"
//#define APP "HexEdit"

// Returns a ready-to-use QSettings object for the app's config file.
// QSettings is non-copyable so callers construct it directly via this helper
// macro rather than a factory function.
#define OPEN_SETTINGS \
QSettings s(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName())

void AppSettings::ensureSettingsDir()
{
    QSettings probe(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
                    //, ORG, APP);
    QDir().mkpath(QFileInfo(probe.fileName()).absolutePath());
}

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

// Default native-menu behaviour:
//   Windows : false  — native menus clash with the custom title bar
//   Linux/KDE: true  — Plasma integrates native menu bars well
//   Linux/other: false  — GNOME and others don't render them reliably
//   macOS   : true   — native menu bar is the platform convention
static bool defaultNativeMenu()
{
#ifdef Q_OS_WIN
    return false;
#elif defined(Q_OS_LINUX)
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower().contains("kde");
#else
    return true;
#endif
}

bool AppSettings::prefNativeMenu()
{
    OPEN_SETTINGS;
    return s.value("preferences/nativeMenu", defaultNativeMenu()).toBool();
}

void AppSettings::setPrefNativeMenu(bool on)
{
    OPEN_SETTINGS;
    s.setValue("preferences/nativeMenu", on);
}

bool AppSettings::prefNativeFileDialogs()
{
    OPEN_SETTINGS;
    return s.value("preferences/nativeFileDialogs", false).toBool();
}

void AppSettings::setPrefNativeFileDialogs(bool on)
{
    OPEN_SETTINGS;
    s.setValue("preferences/nativeFileDialogs", on);
}

bool AppSettings::prefMenuHighlight()
{
    OPEN_SETTINGS;
    return s.value("preferences/menuHighlight", false).toBool();
}

void AppSettings::setPrefMenuHighlight(bool on)
{
    OPEN_SETTINGS;
    s.setValue("preferences/menuHighlight", on);
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

QString AppSettings::prefPaletteName()
{
    OPEN_SETTINGS;
    return s.value("preferences/paletteName", "").toString();
}

void AppSettings::setPrefPaletteName(const QString &name)
{
    OPEN_SETTINGS;
    s.setValue("preferences/paletteName", name);
}

QStringList AppSettings::prefRecentPalettes()
{
    OPEN_SETTINGS;
    const QString csv = s.value("preferences/recentPalettes", "").toString();
    QStringList result;
    for (const QString &part : csv.split(',', Qt::SkipEmptyParts)) {
        const QString name = part.trimmed();
        if (!name.isEmpty() && !result.contains(name))
            result.append(name);
        if (result.size() >= MaxRecentPalettes)
            break;
    }
    return result;
}

void AppSettings::addRecentPalette(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return;

    QStringList names = prefRecentPalettes();
    names.removeAll(trimmed);
    names.prepend(trimmed);
    while (names.size() > MaxRecentPalettes)
        names.removeLast();

    OPEN_SETTINGS;
    s.setValue("preferences/recentPalettes", names.join(','));
}

bool AppSettings::prefRecentPaletteOrdering()
{
    OPEN_SETTINGS;
    return s.value("preferences/recentPaletteOrdering", true).toBool();
}

void AppSettings::setPrefRecentPaletteOrdering(bool on)
{
    OPEN_SETTINGS;
    s.setValue("preferences/recentPaletteOrdering", on);
}
