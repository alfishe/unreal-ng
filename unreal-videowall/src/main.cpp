// MSVC: winsock2.h must be included before windows.h to prevent redefinition errors
#ifdef _WIN32
    #define NOMINMAX
    #include <winsock2.h> // winsock2.h include MUST go before windows.h
    #include <windows.h>
#endif

#include <common/filehelper.h>

#include <QApplication>
#include <QDebug>

#include "videowall/VideoWallWindow.h"

int main(int argc, char* argv[])
{
    // Disable macOS state restoration via Qt before creating QApplication
    // Or use 'defaults write com.unrealng.videowall NSQuitAlwaysKeepsWindows -bool false' in Terminal
#if defined(__APPLE__)
    QCoreApplication::setAttribute(Qt::AA_DisableSessionManager, true);
#endif

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
