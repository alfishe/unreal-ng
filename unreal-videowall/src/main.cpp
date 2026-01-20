#include <common/filehelper.h>

#include <QApplication>
#include <QDebug>

#include "videowall/VideoWallWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // Set application metadata
    QCoreApplication::setOrganizationName("UnrealNG");
    QCoreApplication::setApplicationName("Unreal Video Wall");
    QCoreApplication::setApplicationVersion("0.1.0");

    // CRITICAL: Initialize FileHelper resources path for ROM loading
    // Without this, emulator Init() fails when trying to load ROM files
    std::string resourcesPath = FileHelper::GetResourcesPath();
    qDebug() << "Resources path:" << QString::fromStdString(resourcesPath);

    VideoWallWindow window;
    window.show();

    return app.exec();
}
