#include "mainwindow.h"
#include "crashhandler/crashhandler.h"
#include "headless/headlessscriptrunner.h"
#ifdef __linux__
#include "platform/linux/steamdeckgamemode.h"
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QFontDatabase>
#include <QIcon>

#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "common/macosplatform.h"
#include <QDebug>
#include <QDir>
#include <common/filehelper.h>
#include <common/network/portplan.h>
#include <emulator/config.h>
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

/// --port-base <N | auto> and --ports-file <path> (common/network/portplan.h): every automation server reads its port
/// from a variable of its own when it starts, so the plan is those variables, set before the window (and with it the
/// automation) exists. `auto` takes a base whose every port can be bound now; a port another process takes between
/// this check and the server's own bind is reported by that server as before ("Port N is already in use").
/// The ports file is written here, where the values are final: the servers come up a moment later.
/// False with `error` when the options cannot be honored
static bool ApplyPortPlan(const QString& portBase, const QString& portsFile, QString& error)
{
    if (portBase.isEmpty())
    {
        if (!portsFile.isEmpty())
        {
            error = QStringLiteral("--ports-file needs --port-base <N | auto>");
            return false;
        }
        return true;
    }

    const qint64 pid = QCoreApplication::applicationPid();
    int base = 0;
    if (portBase.compare(QLatin1String("auto"), Qt::CaseInsensitive) == 0)
    {
        // Instances started together begin at different candidates, so they do not race for the same base
        portplan::SearchRange range;
        range.startSlot = static_cast<size_t>(pid);
        std::string why;
        base = portplan::FindFreePortBase(range, portplan::CanBindTcpPort, &why);
        if (base == 0)
        {
            error = QStringLiteral("--port-base auto: ") + QString::fromStdString(why);
            return false;
        }
    }
    else
    {
        bool isNumber = false;
        const qlonglong value = portBase.toLongLong(&isNumber);
        if (!isNumber || !portplan::IsValidPortBase(value))
        {
            error = QStringLiteral("--port-base takes a port number from 1 to %1 or auto, not '%2'")
                        .arg(65535 - portplan::kLastOffset)
                        .arg(portBase);
            return false;
        }
        base = static_cast<int>(value);
    }

    for (const portplan::PortRole& role : portplan::kRoles)
        qputenv(role.envVar, QByteArray::number(base + role.offset));

    if (!portsFile.isEmpty())
    {
        std::string why;
        if (!portplan::WritePortsFile(portsFile.toStdString(), base, static_cast<int64_t>(pid), &why))
        {
            error = QStringLiteral("--ports-file: ") + QString::fromStdString(why);
            return false;
        }
    }
    return true;
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

    // ZX-Evo NVRAM (the ERS / TS-BIOS settings) outlives the app like the board's battery; headless instances
    // (agents, tests) keep theirs for the session only, so they never change the user's settings
    // The VDAC4 ESP's flash (its module cache) likewise: headless instances get an empty one each, so every run of a
    // program starts from the same state and parallel instances never share it
    if (QGuiApplication::platformName() != QLatin1String("offscreen") && QGuiApplication::platformName() != QLatin1String("minimal"))
    {
        Config::SetDefaultNvramFolder(FileHelper::GetUserDataFolder("nvram"));
        Config::SetDefaultVdac4EspCacheFolder(FileHelper::GetUserDataFolder("vdac4/zm"));
    }

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
    // The GUI control plane's TCP listener (docs/inprogress/2026-11-07-gui-control-plane §4.8); the local socket is
    // always on
    QCommandLineOption guiControlPortOption(QStringLiteral("gui-control-port"),
                                            QStringLiteral("Serve the GUI control plane on this TCP port too (0: any "
                                                           "free port), without authentication"),
                                            QStringLiteral("port"));
    QCommandLineOption guiControlBindOption(QStringLiteral("gui-control-bind"),
                                            QStringLiteral("The address of that TCP port (default: all interfaces)"),
                                            QStringLiteral("address"));
    parser.addOption(guiControlPortOption);
    parser.addOption(guiControlBindOption);
    // The automation ports of this instance at once (parallel agents and tests): docs/emulator/environment-variables.md
    QCommandLineOption portBaseOption(QStringLiteral("port-base"),
                                      QStringLiteral("Automation ports from one base: WebAPI base, CLI +100, MCP +200, "
                                                     "GDB +300, DeZog +400, ZRCP +500 (over the UNREAL_*_PORT "
                                                     "variables); auto: a base whose ports are all free"),
                                      QStringLiteral("base"));
    QCommandLineOption portsFileOption(QStringLiteral("ports-file"),
                                       QStringLiteral("Write the ports of --port-base and the process id to this "
                                                      "JSON file"),
                                       QStringLiteral("file"));
    parser.addOption(portBaseOption);
    parser.addOption(portsFileOption);
    parser.process(app);

    {
        QString portPlanError;
        if (!ApplyPortPlan(parser.value(portBaseOption), parser.value(portsFileOption), portPlanError))
        {
            std::cerr << "unreal-qt: " << portPlanError.toStdString() << std::endl;
            StopStartupCleanup();
            unregisterFonts();
            return 2;
        }
    }

    // Instantiate main application window
    MainWindow window;
    window.show();
    {
        bool portOk = false;
        const int guiControlPort = parser.value(guiControlPortOption).toInt(&portOk);
        window.startGuiControl(portOk && guiControlPort >= 0 && guiControlPort <= 65535 ? guiControlPort : -1,
                               parser.value(guiControlBindOption));
    }

    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty())
    {
        const QString file = files.first();
        const QString zxpolyModel = parser.value(zxpolyModelOption);
        QTimer::singleShot(0, &window, [&window, file, zxpolyModel]() { window.openFromCommandLine(file, zxpolyModel); });
    }

    // Steam Deck Game Mode: full screen without chrome, a Pentagon 128K running (platform/linux/steamdeckgamemode.h)
#ifdef __linux__
    if (!headless)
        steamdeck::ApplyGameMode(window, files.isEmpty() && !parser.isSet(scriptOption));
#endif

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
