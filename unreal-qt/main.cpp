#include "mainwindow.h"

#include <QApplication>
#include "logviewer/logviewer.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow window;

    window.show();

    LogViewer logViewer;
    logViewer.show();


    return app.exec();
}
