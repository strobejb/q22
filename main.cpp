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
    a.setApplicationName("q22");
    a.setApplicationVersion("0.1");
    a.setWindowIcon(QIcon(":/qexed.png"));

    // Prepend the bundled hicolor theme so QIcon::fromTheme() works on all
    // platforms.  On Linux the system theme is still tried first; the bundled
    // icons are only used when a name isn't found in the system theme.
    QIcon::setThemeSearchPaths(QStringList(":/icons") + QIcon::themeSearchPaths());
    QIcon::setFallbackThemeName("hicolor");

    applyAdwaitaTheme(static_cast<ColorScheme>(AppSettings::prefColorScheme()));

    MainWindow w;
    w.show();
    return a.exec();
}
