// Stream Deck Studio: the software Stream Deck and scenario editor of unreal-ng. A viewer of the daemon
// (unreal-streamdeck) over the device wire v2: a mirror of the real deck by default, or a deck of its own.
//   unreal-streamdeck-studio [--own MODEL | --deck SERIAL] [--daemon HOST:PORT] [--daemon-api 37053]
//                            [--no-daemon-start] [--api-port 37052] [--record file.mp4 --fps 30]
//   unreal-streamdeck-studio --listen [PORT] [--model MODEL] [--any]   (v1 server for one host: protocol tests)
// Design: docs/inprogress/2026-10-19-streamdeck-integration/studio.md
#include <QApplication>
#include <QCommandLineParser>
#ifdef __APPLE__
#include <QDir>
#include <QFileInfo>
#include <mach-o/dyld.h>
#include <climits>
#include <cstdlib>
#endif
#ifndef _WIN32
#include <QSocketNotifier>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "mainwindow.h"
#include "sdb/devicewire.h"

namespace
{
// --listen takes an optional port: "--listen 37051" or "--listen" alone (37050). QCommandLineParser has no optional
// values, so a bare --listen gets its default here.
QStringList WithListenDefault(QStringList args)
{
    for (int i = 0; i < args.size(); ++i)
        if (args[i] == QLatin1String("--listen") && (i + 1 == args.size() || args[i + 1].startsWith(QLatin1Char('-'))))
            args.insert(i + 1, QString::number(sdb::wire::kDefaultPort));
    return args;
}

#ifndef _WIN32
// SIGTERM / SIGINT end the event loop like Quit: the window goes away normally, so the daemon gets Leave, the
// editor's run gives its deck back and a daemon the studio started ends. The handler only writes a byte.
int g_signalPipe[2] = {-1, -1};
void OnSignal(int)
{
    const char b = 1;
    [[maybe_unused]] const auto n = ::write(g_signalPipe[1], &b, 1);
}
void QuitOnSignals(QApplication& app)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalPipe) != 0)
        return;
    auto* n = new QSocketNotifier(g_signalPipe[0], QSocketNotifier::Read, &app);
    QObject::connect(n, &QSocketNotifier::activated, &app, [] {
        char b = 0;
        [[maybe_unused]] const auto r = ::read(g_signalPipe[0], &b, 1);
        QApplication::quit();
    });
    std::signal(SIGTERM, OnSignal);
    std::signal(SIGINT, OnSignal);
}
#endif

#ifdef __APPLE__
// Nested in the suite (UnrealNG.app/Contents/Applications/Stream Deck Studio.app, packaging.md): the frameworks come
// through the rpath, the plugins (cocoa first) from UnrealNG.app/Contents/PlugIns. Through the executable's real
// path: the DMG's link to the nested app (and a copy of that link in /Applications) would put a relative qt.conf
// prefix next to the link. Before QApplication, which loads the platform plugin.
void AddSuitePlugins()
{
    char exe[PATH_MAX];
    uint32_t size = sizeof(exe);
    char real[PATH_MAX];
    if (_NSGetExecutablePath(exe, &size) != 0 || !::realpath(exe, real))
        return;
    const QString plugins =
        QDir::cleanPath(QFileInfo(QString::fromUtf8(real)).absolutePath() + QStringLiteral("/../../../../PlugIns"));
    if (QFileInfo::exists(plugins + QStringLiteral("/platforms")))
        QCoreApplication::addLibraryPath(plugins);
}
#endif
}  // namespace

int main(int argc, char** argv)
{
#ifdef __APPLE__
    AddSuitePlugins();
#endif
    QApplication app(argc, argv);
#ifndef _WIN32
    QuitOnSignals(app);
#endif
    QApplication::setApplicationName(QStringLiteral("Stream Deck Studio"));
    QApplication::setOrganizationName(QStringLiteral("unreal-ng"));
    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("The software Stream Deck and scenario editor of unreal-ng"));
    cli.addHelpOption();
    const QString defaultPort = QString::number(sdb::wire::kDefaultPort);
    QCommandLineOption daemon(QStringLiteral("daemon"), QStringLiteral("unreal-streamdeck's device wire"),
                              QStringLiteral("host:port"), QStringLiteral("127.0.0.1:") + defaultPort);
    QCommandLineOption own(QStringLiteral("own"), QStringLiteral("a deck of its own of this model (not a mirror)"),
                           QStringLiteral("model"));
    QCommandLineOption deck(QStringLiteral("deck"), QStringLiteral("mirror this real deck (serial; default the first)"),
                            QStringLiteral("serial"), QStringLiteral("first"));
    QCommandLineOption listen(QStringLiteral("listen"),
                              QStringLiteral("protocol tests: be a v1 server for one host (sdctl --device emu:)"),
                              QStringLiteral("port"));
    QCommandLineOption model({QStringLiteral("m"), QStringLiteral("model")},
                             QStringLiteral("--listen: the model (name or PID)"), QStringLiteral("model"),
                             QStringLiteral("plusxl"));
    QCommandLineOption any(QStringLiteral("any"), QStringLiteral("--listen: every interface, not only localhost"));
    QCommandLineOption record(QStringLiteral("record"),
                              QStringLiteral("record the deck from the start: .mp4 .mov .mkv .webm .gif"),
                              QStringLiteral("file"));
    QCommandLineOption fps(QStringLiteral("fps"), QStringLiteral("recording frame rate"), QStringLiteral("fps"),
                           QStringLiteral("30"));
    QCommandLineOption apiPort(QStringLiteral("api-port"), QStringLiteral("automation WebAPI + MCP port (0 = off)"),
                               QStringLiteral("port"), QStringLiteral("37052"));
    QCommandLineOption daemonApi(QStringLiteral("daemon-api"),
                                 QStringLiteral("unreal-streamdeck's automation port (decks, brightness, run)"),
                                 QStringLiteral("port"), QStringLiteral("37053"));
    QCommandLineOption noDaemon(QStringLiteral("no-daemon-start"),
                                QStringLiteral("do not start unreal-streamdeck when none answers"));
    cli.addOptions({daemon, own, deck, listen, model, any, record, fps, apiPort, daemonApi, noDaemon});
    cli.process(WithListenDefault(QApplication::arguments()));

    StudioOptions o;
    const QString addr = cli.value(daemon);
    const qsizetype colon = addr.lastIndexOf(QLatin1Char(':'));
    o.daemonHost = colon > 0 ? addr.left(colon) : addr;
    o.daemonPort = colon > 0 ? static_cast<quint16>(addr.mid(colon + 1).toUInt()) : sdb::wire::kDefaultPort;
    o.own = cli.isSet(own);
    o.deck = cli.value(deck);
    o.model = o.own ? cli.value(own) : cli.value(model);
    o.listen = cli.isSet(listen);
    o.listenPort = static_cast<quint16>(cli.value(listen).toUInt());
    o.anyInterface = cli.isSet(any);
    o.apiPort = cli.value(apiPort).toInt();
    o.daemonApiPort = cli.value(daemonApi).toInt();
    o.startDaemon = !cli.isSet(noDaemon);
    MainWindow w(o);
    w.show();
    if (cli.isSet(record))
        w.StartRecording(cli.value(record), cli.value(fps).toInt());
    return QApplication::exec();
}
