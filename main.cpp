#include "mainwindow.h"
#include "settings.h"
#include "theme.h"
#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus, true);
    a.setOrganizationName("qexed");
    a.setApplicationName("qexed");
    a.setWindowIcon(QIcon(":/qexed.png"));
    applyAdwaitaTheme(static_cast<ColorScheme>(AppSettings::prefColorScheme()));

    MainWindow w;
    w.show();
    return a.exec();
}
