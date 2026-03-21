#include "mainwindow.h"
#include "theme.h"
#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName("qexed");
    a.setApplicationName("qexed");
    a.setWindowIcon(QIcon(":/qexed.png"));
    applyAdwaitaTheme();

    MainWindow w;
    w.show();
    return a.exec();
}
