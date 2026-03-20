#include "mainwindow.h"
#include "theme.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName("qexed");
    a.setApplicationName("qexed");
    applyAdwaitaTheme();

    MainWindow w;
    w.show();
    return a.exec();
}
