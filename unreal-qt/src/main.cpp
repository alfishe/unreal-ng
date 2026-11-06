#include "mainwindow.h"
#include "crashhandler/crashhandler.h"
#include "headless/headlessscriptrunner.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QFontDatabase>
#include <QIcon>

#include <memory>
#include <string_view>

#include "common/macosplatform.h"
#include <QDebug>
#include <QDir>
#include <common/filehelper.h>
#include <common/threadhelper.h>
#include <emulator/startupcleanup.h>
#include "debugger/ttd/ttdrecordingfolders.h"
#include "emulator/emulator.h"
#include "emulator/coordinator/machineop.h"

int fontID = -1;

/// Install the application icon from the bundled Qt resource.
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

void registerFonts(QApplication& app)
{
    //return;
    /// region <Load monospace font>
    // Note: All fonts and resources used on windows must be loaded before window object(s) instantiated

    // Use FileHelper to get the resources path (handles macOS app bundle automatically)
    std::string resourcesPathStr = FileHelper::GetResourcesPath();
    QString resourcesPath = QString::fromStdString(resourcesPathStr);
    QDir filePath(resourcesPath);
    
    QString fontPath = filePath.filePath("fonts/consolas.ttf");
    qDebug() << "Looking for font at:" << fontPath;

    QFile fontFile(fontPath);
    if (fontFile.open(QIODevice::ReadOnly))
    {
        qDebug() << "Font file found at:" << fontPath;
        QByteArray fontdata = fontFile.readAll();
        if (!fontdata.isEmpty())
        {
            fontID = QFontDatabase::addApplicationFontFromData(fontdata);
            if (fontID == -1)
            {
                qCritical() << "Unable to load fonts/consolas.ttf";
                exit(1);
            }
        }

        fontFile.close();
    }
    else
    {
        qCritical() << "Unable to open font at:" << fontPath << fontFile.errorString();
    }

    QStringList fontFamilies = QFontDatabase::families();

#ifdef _DEBUG
    for (int i = 0; i < fontFamilies.length(); i++)
    {
        if (fontFamilies.at(i) == "Consolas")
        {
            qDebug() << "Consolas font family registered";
            break;
        }
    }
#endif
    /// endregion </Load monospace font>
}

void unregisterFonts()
{
    if (fontID != -1)
    {
        QFontDatabase::removeApplicationFont(fontID);
    }
}

int main(int argc, char *argv[])
{
    ThreadHelper::setThreadName("qt-main");
    coordinator::SetThreadSource(coordinator::OpSource::Qt);   // the operation log names the UI's operations
    // Windows: no EcoQoS when another window has the foreground (the sound stuttered)
    ThreadHelper::disableProcessPowerThrottling();

    auto crashHandler = std::unique_ptr<CrashHandler>(CrashHandler::create());
    crashHandler->install();

#ifdef Q_OS_MACOS
    MacOSPlatform::disableAutomaticFullScreenMenuItem();  // Before any menu bar exists
#endif
    // --headless: no windows (the offscreen platform), chosen before the application exists. An explicit
    // QT_QPA_PLATFORM wins
    bool headless = false;
    for (int i = 1; i < argc; i++)
        headless |= std::string_view(argv[i]) == "--headless";
    if (headless && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
#ifdef Q_OS_MACOS
    // No windows to show (agents' automation instances): not a Dock app that looks hung
    if (QGuiApplication::platformName() == QLatin1String("offscreen") || QGuiApplication::platformName() == QLatin1String("minimal"))
        MacOSPlatform::runAsBackgroundProcess();
#endif

    // Housekeeping (crashed TTD recordings, ...) in the background: start-up never waits for it
    StartStartupCleanup();
    // The engine writes every recording as it records, a folder per session in ~/.unreal-ng/ttd (Phase 5, 3c)
    Emulator::SetDefaultTtdRecordingRoot(ttd::RecordingsRoot());

    // Load non-system fonts before any GUI rendered
    registerFonts(app);
    setApplicationIcon(app);

    // Command line: an optional file to open (as File -> Open or drag and drop
    // would); --zxpoly-model picks the machine of a ZX-Poly group without asking
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Snapshot, tape, disk or ZX-Poly (.zxp) file to open"));
    QCommandLineOption zxpolyModelOption(QStringLiteral("zxpoly-model"),
                                         QStringLiteral("Model of the four ZX-Poly modules (e.g. PENTAGON, 128k)"),
                                         QStringLiteral("model"));
    parser.addOption(zxpolyModelOption);
    // The headless test runner (gs-debugger requirements X13)
    QCommandLineOption headlessOption(QStringLiteral("headless"),
                                      QStringLiteral("No windows (the offscreen platform); the emulator muted"));
    QCommandLineOption scriptOption(QStringLiteral("script"),
                                    QStringLiteral("Lua script to run once the emulator is up (its print on stdout)"),
                                    QStringLiteral("file"));
    QCommandLineOption exitOnScriptOption(QStringLiteral("exit-on-script"),
                                          QStringLiteral("Quit when the script ends, with its exit code (the "
                                                         "chunk's result: an integer, true 0, false 1; an error 1)"));
    parser.addOption(headlessOption);
    parser.addOption(scriptOption);
    parser.addOption(exitOnScriptOption);
    parser.process(app);

    // Instantiate main application window
    MainWindow window;
    window.show();

    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty())
    {
        const QString file = files.first();
        const QString zxpolyModel = parser.value(zxpolyModelOption);
        QTimer::singleShot(0, &window, [&window, file, zxpolyModel]() { window.openFromCommandLine(file, zxpolyModel); });
    }

    std::unique_ptr<HeadlessScriptRunner> scriptRunner;
    if (parser.isSet(scriptOption))
    {
        // A file to open brings its machine; without one the runner starts the Machine menu's
        std::function<void()> start;
        if (files.isEmpty())
            start = [&window]() { window.startEmulatorIfNone(); };
        scriptRunner = std::make_unique<HeadlessScriptRunner>(parser.value(scriptOption),
                                                              parser.isSet(exitOnScriptOption), headless, start,
                                                              [&window]() { window.closeUnattended(); });
        scriptRunner->Start();
    }

    // Start application main loop
    int result =  app.exec();
    if (scriptRunner && scriptRunner->Finished() && parser.isSet(exitOnScriptOption))
        result = scriptRunner->ExitCode();
    scriptRunner.reset();
    StopStartupCleanup();

    // Unload non-system fonts
    unregisterFonts();

    return result;
}
