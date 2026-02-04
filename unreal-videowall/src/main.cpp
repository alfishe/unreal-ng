#include <common/filehelper.h>

#include <QApplication>
#include <QDebug>
#include <QTimer>

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

    // WORKAROUND for macOS: Defer ENTIRE window creation until the event loop
    // is running. This prevents a crash in CoreText (TFont::InitShapingGlyphs)
    // during menu bar initialization when the app receives the activation event.
    //
    // The crash occurs because:
    // 1. VideoWallWindow constructor calls createMenus()
    // 2. createMenus() calls menuBar()->addMenu() which creates text items
    // 3. AppKit sends an activation event that triggers menu bar layout
    // 4. CoreText tries to initialize font shaping with a nil font reference
    //
    // Stack trace pattern when crashing:
    //   objc_msgSend → TFont::InitShapingGlyphs → CTLineCreateWithAttributedString
    //   → -[NSMenuBarItemView _ensureValidLineCache] → -[NSApplication _handleActivatedEvent:]
    //
    // Using QTimer::singleShot(0) ensures window creation happens AFTER
    // QApplication::exec() starts the event loop, giving Qt and the Cocoa
    // platform integration time to fully initialize fonts before any menu
    // text rendering occurs.
    
    VideoWallWindow* window = nullptr;
    
    QTimer::singleShot(0, [&window]() {
        window = new VideoWallWindow();
        window->show();
    });

    int result = app.exec();
    
    delete window;
    return result;
}
