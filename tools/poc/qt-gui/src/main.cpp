#include <QApplication>
#include <QIcon>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("unreal-ng"));
    app.setOrganizationName(QStringLiteral("unreal-ng"));
    app.setApplicationDisplayName(QStringLiteral("unreal-ng"));
    app.setQuitOnLastWindowClosed(true);

    // Set application icon with multiple resolutions (up to 1024 for Retina Dock)
    QIcon appIcon;
    appIcon.addFile(QStringLiteral(":/icons/app/appicon_16.png"), QSize(16, 16));
    appIcon.addFile(QStringLiteral(":/icons/app/appicon_32.png"), QSize(32, 32));
    appIcon.addFile(QStringLiteral(":/icons/app/appicon_48.png"), QSize(48, 48));
    appIcon.addFile(QStringLiteral(":/icons/app/appicon_64.png"), QSize(64, 64));
    appIcon.addFile(QStringLiteral(":/icons/app/appicon_128.png"), QSize(128, 128));
    appIcon.addFile(QStringLiteral(":/icons/app/appicon_256.png"), QSize(256, 256));
    appIcon.addFile(QStringLiteral(":/icons/app/appicon_512.png"), QSize(512, 512));
    appIcon.addFile(QStringLiteral(":/icons/app/appicon_1024.png"), QSize(1024, 1024));
    app.setWindowIcon(appIcon);

    MainWindow w;
    w.show();
    return app.exec();
}
