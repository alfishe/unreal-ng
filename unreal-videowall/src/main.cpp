#include <common/filehelper.h>

#include <QApplication>
#include <QDebug>
#include <QTimer>
#include <QStandardPaths>
#include <QDir>

#include "videowall/VideoWallWindow.h"

/// Clear macOS saved application state to prevent crash on startup after previous crash
/// macOS saves window state and may try to restore it, which can cause QuartzCore crashes
/// if the saved state is corrupted or incompatible with current app version
static void clearMacOSSavedState()
{
#ifdef Q_OS_MACOS
    // Delete saved state directory for this app
    // This must be called AFTER QApplication exists (QStandardPaths requires it)
    QString savedStatePath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + "/Saved Application State/com.unrealng.videowall.savedState";
    QDir savedStateDir(savedStatePath);
    if (savedStateDir.exists()) {
        qDebug() << "Clearing macOS saved application state:" << savedStatePath;
        savedStateDir.removeRecursively();
    }
#endif
}

int main(int argc, char* argv[])
{
    // Disable macOS state restoration via Qt before creating QApplication
    // Or use 'defaults write com.unrealng.videowall NSQuitAlwaysKeepsWindows -bool false' in Terminal
#if defined(__APPLE__)
    QCoreApplication::setAttribute(Qt::AA_DisableSessionManager, true);
#endif

    QApplication app(argc, argv);
    
    // Clear macOS saved state AFTER QApplication but BEFORE window creation
#ifdef Q_OS_MACOS
    clearMacOSSavedState();
#endif

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
