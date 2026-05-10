#include "mainwindow.h"
#include "settings.h"
#include "theme.h"
#include <QApplication>
#include <QIcon>


int main(int argc, char *argv[])
{
#ifdef ADWAITA_QT6_PLUGIN_DIR
    //QApplication::addLibraryPath(ADWAITA_QT6_PLUGIN_DIR);
#endif
    QApplication a(argc, argv);
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus, true);
    a.setOrganizationName("Catch22");
    a.setApplicationName("hex22");
    a.setApplicationVersion("0.1");
    a.setWindowIcon(QIcon(":/HexEdit.png"));

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
