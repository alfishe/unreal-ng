#include <QApplication>

#include "MainWindow.h"
#include "crashhandler/crashhandler.h"


int main(int argc, char* argv[])
{
    auto crashHandler = std::unique_ptr<CrashHandler>(CrashHandler::create());
    crashHandler->install();

    QApplication app(argc, argv);

    // Set application metadata
    QApplication::setApplicationName("Unreal Screen Viewer");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setOrganizationName("Unreal-NG");

    MainWindow window;
    window.show();

    return app.exec();
}
