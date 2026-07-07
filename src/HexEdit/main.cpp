#include "mainwindow.h"
#include "settings/settings.h"
#include "theme.h"
#include "version.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QTimer>


int main(int argc, char *argv[])
{
#ifdef ADWAITA_QT6_PLUGIN_DIR
    //QApplication::addLibraryPath(ADWAITA_QT6_PLUGIN_DIR);
#endif
    QApplication a(argc, argv);
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus, true);
    a.setOrganizationName("catch22");
    a.setApplicationName("q22");
    a.setApplicationDisplayName("q22");
    a.setDesktopFileName("q22");
    a.setApplicationVersion(PRODUCT_VERSION_STRING);

    QCommandLineParser parser;
    parser.setApplicationDescription(QApplication::applicationDisplayName());
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QApplication::translate("main", "File to open."));
    parser.process(a);
    const QStringList positionalArgs = parser.positionalArguments();
    if (positionalArgs.size() > 1)
        parser.showHelp(1);

#ifndef Q_OS_WIN
    // On Windows the taskbar/titlebar icon comes from the Win32 ICON resource
    // embedded in the EXE (hexedit.rc).  Setting a QIcon here would make Qt
    // call SetClassLongPtr(GCLP_HICON) with a downscaled PNG, overriding the
    // properly multi-sized RC icon.  Leave it unset so Windows uses the resource.
    a.setWindowIcon(QIcon(":/q22.png"));
#endif
    AppSettings::ensureSettingsDir(); // must be called before any AppSettings read/write

    applyAdwaitaTheme(static_cast<ColorScheme>(AppSettings::prefColorScheme()));

    MainWindow w;
    w.show();
    if (!positionalArgs.isEmpty()) {
        const QString path = positionalArgs.constFirst();
        QTimer::singleShot(0, &w, [&w, path]() { w.openFile(path); });
    }
    return a.exec();
}
