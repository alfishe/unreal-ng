#include "runcontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include "editordocument.h"
#include "sdb/http.h"
#include "sdb/scenarioio.h"

using nlohmann::json;

RunController::RunController(EditorDocument* doc, QObject* parent) : QObject(parent), _doc(doc)
{
    _autosave.setSingleShot(true);
    _autosave.setInterval(250);
    connect(&_autosave, &QTimer::timeout, this, [this] {
        WriteRunFile();
        if (_reloaded)
        {  // a daemon that was not started on this file may not watch it
            QString err;
            if (!Reload(_runFile, &err))
                Line(QStringLiteral("reload failed: ") + err);
        }
    });
    connect(_doc, &EditorDocument::Changed, this, [this] {
        if (Running())
            _autosave.start();
    });
    _proc.setProcessChannelMode(QProcess::MergedChannels);
    connect(&_proc, &QProcess::readyRead, this, [this] {
        while (_proc.canReadLine())
        {
            const QString line = QString::fromUtf8(_proc.readLine()).trimmed();
            if (!line.isEmpty())
                Line(line);
        }
    });
    connect(&_proc, &QProcess::stateChanged, this, &RunController::StateChanged);
    connect(&_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        Line(QStringLiteral("unreal-streamdeck exited (%1)").arg(code));
    });
}

RunController::~RunController()
{
    Stop();
}

void RunController::Line(const QString& line)
{
    _output.append(line);
    if (_output.size() > 300)
        _output.removeFirst();
    emit OutputLine(line);
}

QString RunController::ApiUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(kApiPort);
}

bool RunController::Running() const
{
    return _reloaded || _proc.state() != QProcess::NotRunning;
}

QString RunController::How() const
{
    return _reloaded                               ? QStringLiteral("reloaded")
           : _proc.state() != QProcess::NotRunning ? QStringLiteral("started")
                                                   : QString();
}

QString RunController::DaemonPath()
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

void RunController::WriteRunFile()
{
    if (_runFile.isEmpty())
        return;
    std::string err;
    // includes expanded: the run file lives elsewhere, so relative include paths would not resolve from there
    if (!sdb::scn::io::SaveFile(_runFile.toStdString(), _doc->Resolved(), &err))
        Line(QStringLiteral("autosave failed: ") + QString::fromStdString(err));
}

bool RunController::Reload(const QString& file, QString* error)
{
    const auto r = sdb::HttpRequest(ApiUrl().toStdString(), "POST", "/api/v1/reload",
                                    json{{"file", file.toStdString()}}.dump(), {}, 1500);
    if (r.Ok())
        return true;
    if (error)
        *error = r.status == 0 ? QString::fromStdString(r.error)
                               : QStringLiteral("HTTP %1: %2").arg(r.status).arg(QString::fromStdString(r.body));
    return false;
}

bool RunController::Start(const QString& serial, QString* error)
{
    Stop();
    if (!_doc->Valid())
    {
        if (error)
            *error = QStringLiteral("the scenario has errors (see Issues)");
        return false;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/run");
    QDir().mkpath(dir);
    QString name = QString::fromStdString(_doc->Doc().value("name", "untitled"));
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    _runFile = QDir(dir).filePath(name + QStringLiteral(".deck.json"));
    WriteRunFile();
    _output.clear();

    // a daemon already plays a scenario: it takes ours, and gets its own back on Stop
    const auto current = sdb::HttpRequest(ApiUrl().toStdString(), "GET", "/api/v1/scenario", {}, {}, 800);
    if (current.Ok())
    {
        const json j = json::parse(current.body, nullptr, false);
        _previousFile = j.is_object() ? QString::fromStdString(j.value("file", "")) : QString();
        QString err;
        if (!Reload(_runFile, &err))
        {
            if (error)
                *error = QStringLiteral("unreal-streamdeck did not take the scenario: ") + err;
            return false;
        }
        _reloaded = true;
        Line(QStringLiteral("unreal-streamdeck on %1 plays %2 (it played %3)").arg(ApiUrl(), _runFile, _previousFile));
        emit StateChanged();
        return true;
    }

    QStringList args{QStringLiteral("run"),     _runFile,
                     QStringLiteral("--watch"), QStringLiteral("--api"),
                     QString::number(kApiPort), QStringLiteral("--with-parent")};  // ends with the studio
    if (!serial.isEmpty())
        args << QStringLiteral("--device") << serial;
    Line(DaemonPath() + QLatin1Char(' ') + args.join(QLatin1Char(' ')));
    _proc.start(DaemonPath(), args);
    if (!_proc.waitForStarted(3000))
    {
        if (error)
            *error = QStringLiteral("cannot start %1: %2").arg(DaemonPath(), _proc.errorString());
        return false;
    }
    // a failed open (no deck) exits at once: report it instead of "running"
    if (_proc.waitForFinished(700))
    {
        _proc.waitForReadyRead(50);
        if (error)
            *error = QStringLiteral("unreal-streamdeck stopped: %1").arg(_output.mid(1).join(QStringLiteral("; ")));
        return false;
    }
    emit StateChanged();
    return true;
}

void RunController::Stop()
{
    _autosave.stop();
    if (_reloaded)
    {
        _reloaded = false;
        QString err;
        if (!_previousFile.isEmpty() && !Reload(_previousFile, &err))
            Line(QStringLiteral("could not give unreal-streamdeck %1 back: %2").arg(_previousFile, err));
        else if (!_previousFile.isEmpty())
            Line(QStringLiteral("unreal-streamdeck plays %1 again").arg(_previousFile));
        emit StateChanged();
    }
    if (_proc.state() == QProcess::NotRunning)
        return;
    _proc.terminate();
    if (!_proc.waitForFinished(2000))
        _proc.kill();
    emit StateChanged();
}
