// Stream Deck Studio: the software Stream Deck and scenario editor of unreal-ng. A viewer of the daemon
// (unreal-streamdeck) over the device wire v2: a mirror of the real deck by default, or a deck of its own.
//   unreal-streamdeck-studio [--own MODEL | --deck SERIAL] [--daemon HOST:PORT] [--api-port 37052]
//                            [--record file.mp4 --fps 30]
//   unreal-streamdeck-studio --listen [PORT] [--model MODEL] [--any]   (v1 server for one host: protocol tests)
// Design: docs/inprogress/2026-10-19-streamdeck-integration/studio.md
#include <QApplication>
#include <QCommandLineParser>

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
}  // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
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
    cli.addOptions({daemon, own, deck, listen, model, any, record, fps, apiPort});
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
    MainWindow w(o);
    w.show();
    if (cli.isSet(record))
        w.StartRecording(cli.value(record), cli.value(fps).toInt());
    return QApplication::exec();
}
