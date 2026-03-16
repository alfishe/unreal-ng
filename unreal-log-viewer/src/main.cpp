#include <QApplication>

#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName("Unreal Log Viewer");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setOrganizationName("Unreal-NG");

    MainWindow window;
    window.show();

    return app.exec();
}
