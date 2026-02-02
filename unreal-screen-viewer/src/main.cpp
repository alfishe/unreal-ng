#include <QApplication>

#include "MainWindow.h"


int main(int argc, char* argv[])
{
    // Enable high DPI scaling and high quality image rendering (improves Windows quality)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication app(argc, argv);

    // Set application metadata
    QApplication::setApplicationName("Unreal Screen Viewer");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setOrganizationName("Unreal-NG");

    MainWindow window;
    window.show();

    return app.exec();
}
