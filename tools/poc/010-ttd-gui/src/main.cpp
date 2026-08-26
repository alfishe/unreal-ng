//
// main.cpp — QApplication entry point for the TTD GUI PoC.
//

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include "widgets/main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("poc_ttd_gui"));
    QApplication::setOrganizationName(QStringLiteral("UnrealNG"));
    QApplication::setApplicationVersion(QStringLiteral("0.1"));

    // No global stylesheet — let Qt use the native system theme (light or dark).
    // Individual widgets that need custom painting (timeline, screen) use
    // QPalette-aware colors internally.

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("TTD GUI PoC — Qt Scrubber & Screen Renderer"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption loadOpt(
        QStringList() << "load",
        QStringLiteral("Load a .ttd file at startup"),
        QStringLiteral("path"));
    parser.addOption(loadOpt);
    parser.process(app);

    ttd::MainWindow window;

    // Default: try to load testdata/active_demo.ttd relative to the executable
    QString defaultPath;
    if (parser.isSet(loadOpt)) {
        defaultPath = parser.value(loadOpt);
    } else {
        QDir exeDir(QCoreApplication::applicationDirPath());
        // Prefer the real captured demo; fall back to the synthetic fixtures
        // if the real capture isn't present (e.g. fresh checkout).
        QStringList searchPaths = {
            exeDir.filePath("testdata/action_demo.ttd"),
            exeDir.filePath("../tools/poc/02-ttd-gui/testdata/action_demo.ttd"),
            exeDir.filePath("../../tools/poc/02-ttd-gui/testdata/action_demo.ttd"),
            QStringLiteral("testdata/action_demo.ttd"),
            exeDir.filePath("testdata/active_demo.ttd"),
            exeDir.filePath("../testdata/ttd/active_demo.ttd"),
            exeDir.filePath("../../testdata/ttd/active_demo.ttd"),
            exeDir.filePath("../../../testdata/ttd/active_demo.ttd"),
            QStringLiteral("testdata/ttd/active_demo.ttd"),
        };
        for (const auto& p : searchPaths) {
            if (QFileInfo::exists(p)) {
                defaultPath = p;
                break;
            }
        }
    }

    window.show();

    if (!defaultPath.isEmpty()) {
        QMetaObject::invokeMethod(&window, [&, defaultPath]() {
            window.loadTtd(defaultPath);
        }, Qt::QueuedConnection);
    }

    return app.exec();
}
