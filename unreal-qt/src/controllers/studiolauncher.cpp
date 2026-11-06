#include "studiolauncher.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>

#include <thread>

#include "emulator/state/statenode.h"
#include "emulator/state/statenodejson.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
QString Str(const StateNode& node, const char* key)
{
    const StateNode* v = node.find(key);
    return v != nullptr && v->kind == StateNode::Kind::String ? QString::fromStdString(v->s) : QString();
}

bool Flag(const StateNode& node, const char* key)
{
    const StateNode* v = node.find(key);
    return v != nullptr && ((v->kind == StateNode::Kind::Bool && v->b) || (v->kind == StateNode::Kind::Int && v->i));
}

/// What Studio's GET /api/v1/connection says its deck is: "mirrors deck A1B2C3 (Stream Deck + XL)"; a link that is
/// not joined adds its state ("connecting", "waiting", "failed: ...")
QString ConnectionText(const StateNode& c)
{
    const QString model = Str(c, "model_name");
    const QString state = Str(c, "state");
    QString deck;
    if (Str(c, "mode") == QLatin1String("listen"))
        deck = QObject::tr("a protocol-test server (--listen)");
    else if (Str(c, "role") == QLatin1String("own"))
        deck = model.isEmpty() ? QObject::tr("its own deck") : QObject::tr("its own %1").arg(model);
    else if (const QString serial = Str(c, "deck"); !serial.isEmpty())
        deck = model.isEmpty() ? QObject::tr("mirrors deck %1").arg(serial)
                               : QObject::tr("mirrors deck %1 (%2)").arg(serial, model);
    else
        deck = QObject::tr("mirrors the first deck");
    if (state.isEmpty() || state == QLatin1String("joined"))
        return deck;
    if (const QString error = Str(c, "error"); !error.isEmpty())
        return QObject::tr("%1; %2: %3").arg(deck, state, error);
    return QObject::tr("%1; %2").arg(deck, state);
}

StudioLauncher::Hooks RealHooks()
{
    StudioLauncher::Hooks h;
    h.http = [](const std::string& method, const std::string& path, const std::string& body) {
        return netsock::LoopbackHttp(StudioLauncher::kApiPort, method, path, body, 500);
    };
    h.launch = [](const QString& path, const QStringList& args, QString& error) {
#ifdef Q_OS_MACOS
        // Through Launch Services: a bundle comes to the front, and a running one is activated, not started twice
        QStringList openArgs{QStringLiteral("-a"), path};
        if (!args.isEmpty())
            openArgs << QStringLiteral("--args") << args;
        const bool ok = QProcess::startDetached(QStringLiteral("/usr/bin/open"), openArgs);
#else
        const bool ok = QProcess::startDetached(path, args, QFileInfo(path).absolutePath());
#endif
        if (!ok)
            error = QObject::tr("cannot start %1").arg(QDir::toNativeSeparators(path));
        return ok;
    };
    h.clock = [] {
        static QElapsedTimer timer;
        if (!timer.isValid())
            timer.start();
        return timer.elapsed();
    };
    return h;
}
}  // namespace

StudioLauncher::StudioLauncher(QObject* parent) : StudioLauncher(RealHooks(), Locate(), parent) {}

StudioLauncher::StudioLauncher(Hooks hooks, QString path, QObject* parent)
    : QObject(parent), _hooks(std::move(hooks)), _path(std::move(path))
{
    _state = _path.isEmpty() ? State::Missing : State::Off;
}

QString StudioLauncher::Locate()
{
    const QDir dir(QFileInfo(QCoreApplication::applicationFilePath()).canonicalPath());
#ifdef Q_OS_MACOS
    // UnrealNG.app/Contents/MacOS -> Contents/Applications (the suite); bin/unreal-qt.app/Contents/MacOS -> bin/
    for (const QString& rel : {QStringLiteral("../Applications/Stream Deck Studio.app"),
                               QStringLiteral("../../../unreal-streamdeck-studio.app")})
        if (const QString path = QDir::cleanPath(dir.filePath(rel)); QFileInfo(path).isDir())
            return path;
#else
#ifdef Q_OS_WIN
    const QString exe = QStringLiteral("unreal-streamdeck-studio.exe");
#else
    const QString exe = QStringLiteral("unreal-streamdeck-studio");
#endif
    // beside us (the Windows ZIP, the AppImage's usr/bin, the DEB), beside a build tree's bundle
    for (const QString& rel : {QStringLiteral("."), QStringLiteral("../../..")})
        if (const QString path = QDir::cleanPath(dir.filePath(rel + QLatin1Char('/') + exe));
            QFileInfo(path).isExecutable())
            return path;
#endif
    return {};
}

QString StudioLauncher::StatusText() const
{
    switch (_state)
    {
        case State::Missing: return tr("Not in this build: no Stream Deck Studio beside unreal-qt");
        case State::Off: return tr("Not running");
        case State::Starting: return tr("Starting...");
        case State::Running: return _detail.isEmpty() ? tr("Running") : tr("Running: %1").arg(_detail);
        case State::Failed: return tr("Did not start: %1").arg(_detail);
    }
    return {};
}

QString StudioLauncher::ButtonText() const
{
    return _state == State::Running ? tr("Show Studio") : tr("Open Studio");
}

bool StudioLauncher::CanOpen() const
{
    return _state != State::Missing && _state != State::Starting;
}

void StudioLauncher::SetDaemonPorts(int viewersPort, int apiPort)
{
    if (viewersPort > 0)
        _viewersPort = viewersPort;
    if (apiPort > 0)
        _daemonApiPort = apiPort;
}

void StudioLauncher::SetViewers(int viewers)
{
    if (viewers == _viewers)
        return;
    _viewers = viewers;
    Settle();
}

void StudioLauncher::Open()
{
    if (!CanOpen())
        return;
    if (_state == State::Running)
    {
#ifdef Q_OS_WIN
        AllowSetForegroundWindow(ASFW_ANY);  // we are in front (the click): Studio may take the foreground
#endif
#ifdef Q_OS_MACOS
        QString ignored;
        _hooks.launch(_path, {}, ignored);  // Launch Services activates the running app
#endif
        // showNormal + raise: a minimized or covered window comes back
        std::thread([http = _hooks.http] { http("PUT", "/api/v1/window", R"({"raise":true})"); }).detach();
        return;
    }
    const QStringList args{QStringLiteral("--daemon"), QStringLiteral("127.0.0.1:%1").arg(_viewersPort),
                           QStringLiteral("--daemon-api"), QString::number(_daemonApiPort)};
    QString error;
    if (!_hooks.launch(_path, args, error))
    {
        _startedAt = -1;
        _launchError = error;
        Set(State::Failed, error);
        return;
    }
    _launchError.clear();
    _startedAt = _hooks.clock();
    Set(State::Starting, {});
    Probe();
}

void StudioLauncher::Probe()
{
    if (_state == State::Missing || _probing)
        return;
    _probing = true;
    QPointer<StudioLauncher> self(this);
    std::thread([self, http = _hooks.http] {
        const netsock::HttpReply reply = http("GET", "/api/v1/connection", {});
        if (QCoreApplication* app = QCoreApplication::instance())
            QMetaObject::invokeMethod(app, [self, reply] {
                if (self)
                    self->TakeProbe(reply);
            });
    }).detach();
}

void StudioLauncher::TakeProbe(const netsock::HttpReply& reply)
{
    _probing = false;
    StateNode body;
    // Studio's answer names a mode (client / listen): another program on the port is not Studio
    _answered = reply.Ok() && StateNodeFromJsonText(reply.body, body) && body.find("mode") != nullptr;
    _connection = _answered ? ConnectionText(body) : QString();
    Settle();
}

void StudioLauncher::Settle()
{
    if (_state == State::Missing)
        return;
    if (_answered)
    {
        _startedAt = -1;
        _launchError.clear();
        Set(State::Running, _connection);
    }
    else if (_viewers > 0)
    {
        _startedAt = -1;
        _launchError.clear();
        Set(State::Running, tr("a window joined the daemon (its automation port %1 is off)").arg(kApiPort));
    }
    else if (_startedAt >= 0 && _hooks.clock() - _startedAt < kStartTimeoutMs)
        Set(State::Starting, {});
    else if (_startedAt >= 0)
    {
        _startedAt = -1;
        _launchError = tr("started, but it did not answer on 127.0.0.1:%1 within %2 s")
                           .arg(kApiPort)
                           .arg(kStartTimeoutMs / 1000);
        Set(State::Failed, _launchError);
    }
    else if (!_launchError.isEmpty())
        Set(State::Failed, _launchError);
    else
        Set(State::Off, {});
}

void StudioLauncher::Set(State state, const QString& detail)
{
    if (state == _state && detail == _detail)
        return;
    _state = state;
    _detail = detail;
    emit Changed();
}
