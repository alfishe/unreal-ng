#include <QApplication>
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("CallTrace Viz PoC");

    MainWindow window;
    window.show();

    return app.exec();
}
