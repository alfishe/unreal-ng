#include "daemoncontrol.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include "sdb/http.h"

using nlohmann::json;

namespace
{
// A serial as a path segment
std::string Segment(const QString& serial)
{
    return QUrl::toPercentEncoding(serial.isEmpty() ? QStringLiteral("first") : serial).toStdString();
}
}  // namespace

DaemonControl::DaemonControl(QObject* parent) : QObject(parent)
{
    _proc.setProcessChannelMode(QProcess::MergedChannels);
    connect(&_proc, &QProcess::readyRead, this, [this] {
        while (_proc.canReadLine())
        {
            const QString line = QString::fromUtf8(_proc.readLine()).trimmed();
            if (!line.isEmpty())
                Line(line);
        }
    });
    connect(&_proc, &QProcess::stateChanged, this, &DaemonControl::StateChanged);
    connect(&_proc, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus) { Line(QStringLiteral("unreal-streamdeck exited (%1)").arg(code)); });
}

DaemonControl::~DaemonControl()
{
    Terminate();
}

void DaemonControl::Line(const QString& line)
{
    _output.append(line);
    if (_output.size() > 300)
        _output.removeFirst();
    emit OutputLine(line);
}

QString DaemonControl::ApiUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(_apiPort);
}

QString DaemonControl::Path()
{
    const QString env = qEnvironmentVariable("UNREAL_STREAMDECK_BIN");
    if (!env.isEmpty())
        return env;
#ifdef Q_OS_WIN
    const QString exe = QStringLiteral("unreal-streamdeck.exe");
#else
    const QString exe = QStringLiteral("unreal-streamdeck");
#endif
    const QDir app(QCoreApplication::applicationDirPath());
    // beside the studio; beside its bundle (a build tree); in the suite's bundle (nested studio app)
    for (const QString& rel : {QStringLiteral("."), QStringLiteral("../../.."), QStringLiteral("../../../../MacOS")})
        if (const QString p = QDir::cleanPath(app.filePath(rel + QLatin1Char('/') + exe)); QFileInfo::exists(p))
            return p;
    return exe;  // PATH
}

DaemonControl::Result DaemonControl::Call(const std::string& method, const std::string& path, const json& body,
                                          int timeoutMs) const
{
    const auto r = sdb::HttpRequest(ApiUrl().toStdString(), method, path, body.is_null() ? std::string() : body.dump(),
                                    {}, timeoutMs);
    Result out;
    out.status = r.status;
    if (r.status == 0)
    {
        out.error = QStringLiteral("unreal-streamdeck automation at %1: %2")
                        .arg(ApiUrl(), QString::fromStdString(r.error));
        return out;
    }
    out.body = json::parse(r.body, nullptr, false);
    if (out.body.is_discarded())
        out.body = json{{"text", r.body}};
    if (!out.Ok())
    {
        std::string why = "HTTP " + std::to_string(r.status);
        if (const auto e = out.body.find("error"); out.body.is_object() && e != out.body.end() && e->is_string())
            why = e->get<std::string>();
        out.error = QStringLiteral("unreal-streamdeck: %1").arg(QString::fromStdString(why));
    }
    return out;
}

bool DaemonControl::Answers(int timeoutMs) const
{
    const Result r = Call("GET", "/api/v1/health", nullptr, timeoutMs);
    return r.Ok() && r.body.is_object() && r.body.value("ok", false);
}

json DaemonControl::Decks(QString* error) const
{
    const Result r = Call("GET", "/api/v1/decks");
    if (!r.Ok())
    {
        if (error)
            *error = r.error;
        return json::array();
    }
    return r.body.value("decks", json::array());
}

bool DaemonControl::Brightness(const QString& serial, int* percent, QString* error) const
{
    const Result r = Call("GET", "/api/v1/decks/" + Segment(serial) + "/brightness");
    if (!r.Ok())
    {
        if (error)
            *error = r.error;
        return false;
    }
    if (percent)
        *percent = r.body.value("brightness", 0);
    return true;
}

bool DaemonControl::SetBrightness(const QString& serial, int percent, QString* error) const
{
    const Result r = Call("PUT", "/api/v1/decks/" + Segment(serial) + "/brightness", json{{"value", percent}});
    if (!r.Ok() && error)
        *error = r.error;
    return r.Ok();
}

bool DaemonControl::PlayScenario(const QString& serial, const QString& file, QString* error) const
{
    const Result r = Call("PUT", "/api/v1/decks/" + Segment(serial) + "/scenario",
                          json{{"file", file.toStdString()}, {"watch", true}}, 3000);
    if (!r.Ok() && error)
        *error = r.error;
    return r.Ok();
}

bool DaemonControl::RestoreScenario(const QString& serial, QString* error) const
{
    const Result r = Call("DELETE", "/api/v1/decks/" + Segment(serial) + "/scenario", nullptr, 3000);
    if (!r.Ok() && error)
        *error = r.error;
    return r.Ok();
}

bool DaemonControl::Launch(const QStringList& args, QString* error, bool checkEarlyExit)
{
    Terminate();
    _output.clear();
    Line(Path() + QLatin1Char(' ') + args.join(QLatin1Char(' ')));
    _proc.start(Path(), args);
    if (!_proc.waitForStarted(3000))
    {
        if (error)
            *error = QStringLiteral("cannot start %1: %2").arg(Path(), _proc.errorString());
        return false;
    }
    // a failed start (no deck, a bad scenario, a taken port) exits at once: report it instead of "running"
    if (checkEarlyExit && _proc.waitForFinished(700))
    {
        _proc.waitForReadyRead(50);
        const QByteArray rest = _proc.readAll();
        for (const QByteArray& l : rest.split('\n'))
            if (!l.trimmed().isEmpty())
                Line(QString::fromUtf8(l.trimmed()));
        if (error)
            *error = QStringLiteral("unreal-streamdeck stopped: %1").arg(_output.mid(1).join(QStringLiteral("; ")));
        return false;
    }
    emit StateChanged();
    return true;
}

void DaemonControl::Terminate()
{
    if (_proc.state() == QProcess::NotRunning)
        return;
    _proc.terminate();
    if (!_proc.waitForFinished(2000))
        _proc.kill();
    emit StateChanged();
}
