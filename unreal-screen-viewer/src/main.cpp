#include <QApplication>
#include <QIcon>

#include "MainWindow.h"
#include "crashhandler/crashhandler.h"

/// Install the application icon from the bundled Qt resource (shared with unreal-qt).
/// Used for the window / taskbar icon on Windows and Linux. On macOS the Dock
/// shows the bundle's .icns natively and QApplication::setWindowIcon would
/// replace it with a low-resolution pixmap, so it is skipped there.
static void setApplicationIcon(QApplication& app)
{
#ifndef Q_OS_MACOS
    QIcon icon;
    for (int size : {16, 32, 48, 64, 128, 256, 512, 1024})
    {
        icon.addFile(QStringLiteral(":/icons/app/appicon_%1.png").arg(size), QSize(size, size));
    }
    app.setWindowIcon(icon);
#else
    Q_UNUSED(app);
#endif
}

int main(int argc, char* argv[])
{
    auto crashHandler = std::unique_ptr<CrashHandler>(CrashHandler::create());
    crashHandler->install();

    QApplication app(argc, argv);

    // Set application metadata
    QApplication::setApplicationName("Unreal Screen Viewer");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setOrganizationName("Unreal-NG");
    setApplicationIcon(app);

    MainWindow window;
    window.show();

    return app.exec();
}
