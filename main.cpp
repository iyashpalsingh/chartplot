#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Battery Data Plotter");
    app.setStyle("Fusion");
    MainWindow w;
    w.show();
    return app.exec();
}
