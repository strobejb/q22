#include "settings.h"
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QSettings>
#include <QCoreApplication>

//#define ORG "Catch22"
//#define APP "HexEdit"


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
    return s.value("font/family", "").toString();
}

void AppSettings::setPrefFontFamily(const QString &family)
{
    OPEN_SETTINGS;
    s.setValue("font/family", family);
}

int AppSettings::prefFontSize()
{
    OPEN_SETTINGS;
    return s.value("font/size", 13).toInt();
}

void AppSettings::setPrefFontSize(int size)
{
    OPEN_SETTINGS;
    s.setValue("font/size", size);
}

QFont AppSettings::defaultHexFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(prefFontSize());
    return font;
}

QFont AppSettings::hexFont()
{
    const QString family = prefFontFamily();
    if (family.isEmpty())
        return defaultHexFont();

    return QFont(family, prefFontSize());
}

int AppSettings::prefHorizSpacing()
{
    OPEN_SETTINGS;
    return s.value("font/horizSpacing", 2).toInt();
}

void AppSettings::setPrefHorizSpacing(int px)
{
    OPEN_SETTINGS;
    s.setValue("font/horizSpacing", px);
}

int AppSettings::prefLineSpacing()
{
    OPEN_SETTINGS;
    return s.value("font/lineSpacing", 1).toInt();
}

void AppSettings::setPrefLineSpacing(int px)
{
    OPEN_SETTINGS;
    s.setValue("font/lineSpacing", px);
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
    return s.value("appearance/nativeMenu", defaultNativeMenu()).toBool();
}

void AppSettings::setPrefNativeMenu(bool on)
{
    OPEN_SETTINGS;
    s.setValue("appearance/nativeMenu", on);
}

bool AppSettings::prefNativeFileDialogs()
{
    OPEN_SETTINGS;
    return s.value("window/nativeFilePicker", false).toBool();
}

void AppSettings::setPrefNativeFileDialogs(bool on)
{
    OPEN_SETTINGS;
    s.setValue("window/nativeFilePicker", on);
}

bool AppSettings::prefNativeDialogs()
{
    OPEN_SETTINGS;
    return s.value("window/nativeDialogs", false).toBool();
}

void AppSettings::setPrefNativeDialogs(bool on)
{
    OPEN_SETTINGS;
    s.setValue("window/nativeDialogs", on);
}

bool AppSettings::prefRestoreWindowGeometry()
{
    OPEN_SETTINGS;
    return s.value("window/restoreGeometry", true).toBool();
}

void AppSettings::setPrefRestoreWindowGeometry(bool on)
{
    OPEN_SETTINGS;
    s.setValue("window/restoreGeometry", on);
}

QByteArray AppSettings::windowGeometry()
{
    OPEN_SETTINGS;
    return s.value("window/geometry").toByteArray();
}

void AppSettings::setWindowGeometry(const QByteArray &geometry)
{
    OPEN_SETTINGS;
    s.setValue("window/geometry", geometry);
}

bool AppSettings::prefMenuHighlight()
{
    OPEN_SETTINGS;
    return s.value("appearance/menuHighlight", false).toBool();
}

void AppSettings::setPrefMenuHighlight(bool on)
{
    OPEN_SETTINGS;
    s.setValue("appearance/menuHighlight", on);
}

bool AppSettings::prefScrollbarArrows()
{
    OPEN_SETTINGS;
    return s.value("appearance/scrollbarArrows", true).toBool();
}

void AppSettings::setPrefScrollbarArrows(bool on)
{
    OPEN_SETTINGS;
    s.setValue("appearance/scrollbarArrows", on);
}

int AppSettings::prefColorScheme()
{
    OPEN_SETTINGS;
    return s.value("theme/colorScheme", 0).toInt();
}

void AppSettings::setPrefColorScheme(int scheme)
{
    OPEN_SETTINGS;
    s.setValue("theme/colorScheme", scheme);
}

QString AppSettings::prefPaletteName()
{
    OPEN_SETTINGS;
    return s.value("theme/paletteName", "").toString();
}

void AppSettings::setPrefPaletteName(const QString &name)
{
    OPEN_SETTINGS;
    s.setValue("theme/paletteName", name);
}

QStringList AppSettings::prefRecentPalettes()
{
    OPEN_SETTINGS;
    const QString csv = s.value("theme/recentPalettes", "").toString();
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
    s.setValue("theme/recentPalettes", names.join(','));
}

bool AppSettings::prefRecentPaletteOrdering()
{
    OPEN_SETTINGS;
    return s.value("theme/recentPaletteOrdering", true).toBool();
}

void AppSettings::setPrefRecentPaletteOrdering(bool on)
{
    OPEN_SETTINGS;
    s.setValue("theme/recentPaletteOrdering", on);
}

bool AppSettings::prefBookmarkAutoExpand()
{
    OPEN_SETTINGS;
    return s.value("bookmarks/autoExpand", true).toBool();
}

void AppSettings::setPrefBookmarkAutoExpand(bool on)
{
    OPEN_SETTINGS;
    s.setValue("bookmarks/autoExpand", on);
}

bool AppSettings::prefBookmarkNested()
{
    OPEN_SETTINGS;
    return s.value("bookmarks/nestedBookmarks", false).toBool();
}

void AppSettings::setPrefBookmarkNested(bool on)
{
    OPEN_SETTINGS;
    s.setValue("bookmarks/nestedBookmarks", on);
}

bool AppSettings::prefBookmarkSelectionHighlights()
{
    OPEN_SETTINGS;
    return s.value("bookmarks/selectionHighlights", true).toBool();
}

void AppSettings::setPrefBookmarkSelectionHighlights(bool on)
{
    OPEN_SETTINGS;
    s.setValue("bookmarks/selectionHighlights", on);
}
