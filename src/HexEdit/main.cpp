#include "mainwindow.h"
#include "settings/settings.h"
#include "theme.h"
#include <QApplication>
#include <QIcon>
#include <QDir>


int main(int argc, char *argv[])
{
#ifdef ADWAITA_QT6_PLUGIN_DIR
    //QApplication::addLibraryPath(ADWAITA_QT6_PLUGIN_DIR);
#endif
    QApplication a(argc, argv);
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus, true);
    a.setOrganizationName("catch22");
    a.setApplicationName("hexedit");
    a.setApplicationDisplayName("HexEdit");
    a.setDesktopFileName("hexedit");
    a.setApplicationVersion("3.0.1");
#ifndef Q_OS_WIN
    // On Windows the taskbar/titlebar icon comes from the Win32 ICON resource
    // embedded in the EXE (hexedit.rc).  Setting a QIcon here would make Qt
    // call SetClassLongPtr(GCLP_HICON) with a downscaled PNG, overriding the
    // properly multi-sized RC icon.  Leave it unset so Windows uses the resource.
    a.setWindowIcon(QIcon(":/hexedit.png"));
#endif
    AppSettings::ensureSettingsDir(); // must be called before any AppSettings read/write

    // Use only the bundled hicolor theme for icon lookups.  This ensures
    // QIcon::fromTheme() always resolves to our embedded resources, regardless
    // of what icon themes are installed on the host system.  Correct for a
    // self-contained binary/AppImage on both GNOME and KDE.
    QIcon::setThemeSearchPaths({":/icons"});
    QIcon::setThemeName("hicolor");

    applyAdwaitaTheme(static_cast<ColorScheme>(AppSettings::prefColorScheme()));

    MainWindow w;
    w.show();
    return a.exec();
}
