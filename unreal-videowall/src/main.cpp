#include <QApplication>

#include "videowall/VideoWallWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // Set application metadata
    QCoreApplication::setOrganizationName("UnrealNG");
    QCoreApplication::setApplicationName("Unreal Video Wall");
    QCoreApplication::setApplicationVersion("0.1.0");

    VideoWallWindow window;
    window.show();

    return app.exec();
}
