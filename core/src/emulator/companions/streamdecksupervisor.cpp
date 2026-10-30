#include "stdafx.h"

#include "streamdecksupervisor.h"

#include <chrono>
#include <cstdlib>

#include "common/childprocess.h"
#include "common/logger.h"
#include "common/network/loopbackhttp.h"
#include "emulator/state/statenodejson.h"

namespace companion
{
namespace streamdeck
{

namespace
{
class ChildProcess : public IProcess
{
public:
    explicit ChildProcess(std::unique_ptr<childprocess::Child> child) : _child(std::move(child)) {}
    int Pid() const override { return _child->Pid(); }
    bool Exited(int& code, bool& crashed) override { return _child->Exited(code, crashed); }
    void Terminate() override { _child->Terminate(); }
    void Kill() override { _child->Kill(); }

private:
    std::unique_ptr<childprocess::Child> _child;
};

class ProcessLauncher : public ILauncher
{
public:
    std::unique_ptr<IProcess> Start(const LaunchSpec& spec, std::string& error) override
    {
        childprocess::Spec s;
        s.path = spec.path;
        s.args = spec.args;
        s.logFile = spec.logFile;
        auto child = childprocess::Child::Start(s, error);
        if (!child)
            return nullptr;
        return std::make_unique<ChildProcess>(std::move(child));
    }
};

class LoopbackDaemonHttp : public IDaemonHttp
{
public:
    HttpResult Call(int port, const std::string& method, const std::string& path, const std::string& body,
                    int timeoutMs) override
    {
        const netsock::HttpReply r = netsock::LoopbackHttp(port, method, path, body, timeoutMs);
        return {r.status, r.body, r.error};
    }
};

int64_t SteadyMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

StateNode Parse(const std::string& text)
{
    StateNode node;
    if (!StateNodeFromJsonText(text, node))
        return StateNode::Object();
    return node;
}

std::string StrOf(const StateNode* node)
{
    return node && node->kind == StateNode::Kind::String ? node->s : std::string();
}

int64_t IntOf(const StateNode* node, int64_t fallback = 0)
{
    if (!node)
        return fallback;
    if (node->kind == StateNode::Kind::Int)
        return node->i;
    if (node->kind == StateNode::Kind::Double)
        return static_cast<int64_t>(node->d);
    if (node->kind == StateNode::Kind::String && !node->s.empty())
        return std::atoll(node->s.c_str());
    return fallback;
}

bool BoolOf(const StateNode* node, bool& out)
{
    if (!node)
        return false;
    if (node->kind == StateNode::Kind::Bool)
        out = node->b;
    else if (node->kind == StateNode::Kind::Int)
        out = node->i != 0;
    else if (node->kind == StateNode::Kind::String && (node->s == "true" || node->s == "false"))
        out = node->s == "true";
    else
        return false;
    return true;
}

/// A daemon's --host drives this emulator: http://127.0.0.1:<port>, localhost, or (a daemon that does not say)
/// the default port
bool SameHost(const std::string& host, int webapiPort)
{
    if (host.empty())
        return webapiPort == 8090;
    const std::string suffix = ":" + std::to_string(webapiPort);
    std::string h = host;
    while (!h.empty() && h.back() == '/')
        h.pop_back();
    const bool local = h.find("127.0.0.1") != std::string::npos || h.find("localhost") != std::string::npos;
    return local && h.size() >= suffix.size() && h.compare(h.size() - suffix.size(), suffix.size(), suffix) == 0;
}
} // namespace

const char* ModeName(Mode mode)
{
    switch (mode)
    {
        case Mode::Off: return "off";
        case Mode::Deck: return "deck";
        case Mode::Always: return "always";
    }
    return "deck";
}

bool ModeFromName(const std::string& name, Mode& out)
{
    for (Mode mode : {Mode::Off, Mode::Deck, Mode::Always})
        if (name == ModeName(mode))
        {
            out = mode;
            return true;
        }
    return false;
}

const char* Supervisor::StateName(State state)
{
    switch (state)
    {
        case State::Detached: return "detached";
        case State::Off: return "off";
        case State::Waiting: return "waiting";
        case State::Starting: return "starting";
        case State::Running: return "running";
        case State::Adopted: return "adopted";
        case State::Foreign: return "foreign";
        case State::Backoff: return "backoff";
        case State::Failed: return "failed";
        case State::Stopped: return "stopped";
        case State::Missing: return "missing";
    }
    return "detached";
}

Supervisor& Supervisor::Instance()
{
    static Supervisor* supervisor = new Supervisor();  // never destroyed: threads may still ask at exit
    return *supervisor;
}

Supervisor::Supervisor()
    : _launcher(std::make_shared<ProcessLauncher>()), _http(std::make_shared<LoopbackDaemonHttp>())
{
}

Supervisor::~Supervisor()
{
    Detach();
}

void Supervisor::SetLauncher(std::shared_ptr<ILauncher> launcher)
{
    std::lock_guard l(_mutex);
    _launcher = launcher ? std::move(launcher) : std::make_shared<ProcessLauncher>();
}

void Supervisor::SetHttp(std::shared_ptr<IDaemonHttp> http)
{
    std::lock_guard l(_mutex);
    _http = http ? std::move(http) : std::make_shared<LoopbackDaemonHttp>();
}

void Supervisor::Attach(const Host& host, const Settings& settings, bool thread)
{
    Detach();
    {
        std::lock_guard l(_mutex);
        _host = host;
        _settings = settings;
        _state = State::Waiting;
        _child.reset();
        _released = false;
        _userStopped = _userStarted = _restart = _stopping = false;
        _healthFails = _failures = _restarts = 0;
        _decksAttached = 0;
        _nextHealth = _restartAt = _failedUntil = _nextDeckCheck = 0;
        _appliedPid = 0;
        _daemonPort = settings.apiPort;
        _appliedBrightness.clear();
        _appliedPack.clear();
        _health = Health{};
        _error.clear();
        _lastExit = StateNode();
        _quit = false;
    }
    if (thread)
        _thread = std::thread([this] { Run(); });
}

void Supervisor::Detach()
{
    {
        std::lock_guard l(_mutex);
        _quit = true;
    }
    _wake.notify_all();
    if (_thread.joinable())
        _thread.join();
    std::lock_guard step(_stepMutex);
    std::unique_lock l(_mutex);
    if (_state == State::Detached)
        return;
    const bool leased = !_released && (_child || _state == State::Adopted || _state == State::Running ||
                                       _state == State::Starting);
    const int port = _daemonPort;
    const int pid = _host.ownPid;
    auto http = _http;
    _state = State::Detached;
    _child.reset();  // not killed: its own lease (our pid) ends it when nobody else holds it
    l.unlock();
    if (leased && pid > 0)
        (void)http->Call(port, "DELETE", "/api/v1/parents/" + std::to_string(pid), {}, 1000);
}

bool Supervisor::Attached() const
{
    std::lock_guard l(_mutex);
    return _state != State::Detached;
}

void Supervisor::Run()
{
    std::unique_lock l(_mutex);
    while (!_quit)
    {
        l.unlock();
        Step(SteadyMs());
        l.lock();
        _wake.wait_for(l, std::chrono::milliseconds(kStepMs), [this] { return _quit || _kick; });
        _kick = false;
    }
}

void Supervisor::Notify()
{
    {
        std::lock_guard l(_mutex);
        _kick = true;
    }
    _wake.notify_all();
}

void Supervisor::Start()
{
    {
        std::lock_guard l(_mutex);
        _userStopped = false;
        _userStarted = true;
        _failedUntil = 0;
        _restartAt = 0;
        _failures = 0;
        if (_state == State::Failed || _state == State::Backoff || _state == State::Stopped ||
            _state == State::Missing)
            _state = State::Waiting;
    }
    Notify();
}

void Supervisor::Stop()
{
    {
        std::lock_guard l(_mutex);
        _userStopped = true;
        _userStarted = false;
        _restart = false;
    }
    Notify();
}

void Supervisor::Restart()
{
    {
        std::lock_guard l(_mutex);
        _userStopped = false;
        _restart = true;
        _failures = 0;
        _failedUntil = 0;
        _restartAt = 0;
    }
    Notify();
}

Settings Supervisor::GetSettings() const
{
    std::lock_guard l(_mutex);
    return _settings;
}

void Supervisor::SetSettingsListener(std::function<void(const Settings&)> listener)
{
    std::lock_guard l(_mutex);
    _listener = std::move(listener);
}

bool Supervisor::Update(const StateNode& changes, std::string& error)
{
    Settings next;
    std::function<void(const Settings&)> listener;
    {
        std::lock_guard l(_mutex);
        next = _settings;
        listener = _listener;
    }
    if (const StateNode* mode = changes.find("mode"))
        if (!ModeFromName(StrOf(mode), next.mode))
        {
            error = "mode: off, deck or always";
            return false;
        }
    if (const StateNode* activity = changes.find("activity"))
    {
        const std::string look = StrOf(activity);
        if (look != "off" && look != "flash" && look != "line")
        {
            error = "activity: off, flash or line";
            return false;
        }
        next.activity = look;
    }
    for (const auto& [name, field] : {std::pair<const char*, int*>{"api_port", &next.apiPort},
                                      std::pair<const char*, int*>{"viewers_port", &next.viewersPort}})
        if (const StateNode* port = changes.find(name))
        {
            const int64_t value = IntOf(port, -1);
            if (value < 1 || value > 65535)
            {
                error = std::string(name) + ": 1-65535";
                return false;
            }
            *field = static_cast<int>(value);
        }
    if (const StateNode* binary = changes.find("binary"))
        next.binary = StrOf(binary);
    if (const StateNode* brightness = changes.find("brightness"); brightness && brightness->isObject())
        for (const auto& [serial, value] : brightness->members)
        {
            const int64_t percent = IntOf(&value, -1);
            if (percent < 0 || percent > 100)
            {
                error = "brightness: 0-100 per deck";
                return false;
            }
            next.brightness[serial] = static_cast<int>(percent);
        }
    if (const StateNode* pack = changes.find("pack"); pack && pack->isObject())
        for (const auto& [serial, value] : pack->members)
        {
            const std::string name = StrOf(&value);
            if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
            {
                error = "pack: an icon pack name per deck, or scenario";
                return false;
            }
            if (name == "scenario")
                next.pack.erase(serial);
            else
                next.pack[serial] = name;
        }
    bool changed = false;
    {
        std::lock_guard l(_mutex);
        changed = next != _settings;
        if (changed)
        {
            const bool restart = (next.apiPort != _settings.apiPort || next.viewersPort != _settings.viewersPort ||
                                  next.binary != _settings.binary) &&
                                 (_child || _state == State::Running);
            if (_state == State::Missing && next.binary != _settings.binary)
                _failedUntil = 0;  // the binary named now: tried at the next step
            if (_state == State::Foreign && next.apiPort != _settings.apiPort)
            {
                // another host's daemon on the old port is no reason to stay: ours on the new one
                _state = State::Waiting;
                _health = Health{};
                _healthFails = 0;
                _error.clear();
            }
            _settings = next;
            if (restart)
                _restart = true;  // ours takes the new ports or binary
        }
    }
    if (changed)
    {
        if (listener)
            listener(next);
        Notify();
    }
    return true;
}

void Supervisor::KeepAppliedPack(const std::string& serial, const std::string& pack)
{
    std::function<void(const Settings&)> listener;
    Settings next;
    bool changed = false;
    {
        std::lock_guard l(_mutex);
        _appliedPack[serial] = pack;
        next = _settings;
        if (pack == "scenario")
            next.pack.erase(serial);
        else
            next.pack[serial] = pack;
        changed = next != _settings;
        _settings = next;
        listener = _listener;
    }
    if (changed)
    {
        if (listener)
            listener(next);
        Notify();
    }
}

std::string Supervisor::BinaryPathLocked() const
{
    if (const char* env = std::getenv("UNREAL_STREAMDECK_BIN"); env && *env)
        return env;
    if (!_settings.binary.empty())
        return _settings.binary;
    return _host.defaultBinary;
}

std::string Supervisor::BinaryPath() const
{
    std::lock_guard l(_mutex);
    return BinaryPathLocked();
}

std::vector<std::string> Supervisor::LaunchArgsLocked() const
{
    return {"run",
            "--wait",
            "--api",
            std::to_string(_settings.apiPort),
            "--viewers",
            std::to_string(_settings.viewersPort),
            "--host",
            "127.0.0.1:" + std::to_string(_host.webapiPort),
            "--parent",
            std::to_string(_host.ownPid),
            "--activity",
            _settings.activity};
}

std::vector<std::string> Supervisor::LaunchArgs() const
{
    std::lock_guard l(_mutex);
    return LaunchArgsLocked();
}

Supervisor::Health Supervisor::Probe(int port)
{
    std::shared_ptr<IDaemonHttp> http;
    {
        std::lock_guard l(_mutex);
        http = _http;
    }
    Health h;
    const HttpResult r = http->Call(port, "GET", "/api/v1/health", {}, 1000);
    if (!r.Ok())
        return h;
    h.body = Parse(r.body);
    bool ok = false;
    if (!BoolOf(h.body.find("ok"), ok) || !ok)
        return h;
    h.ok = true;
    h.host = StrOf(h.body.find("host"));
    h.pid = static_cast<int>(IntOf(h.body.find("pid")));
    const HttpResult d = http->Call(port, "GET", "/api/v1/decks", {}, 1000);
    if (d.Ok())
    {
        StateNode list = Parse(d.body);
        if (const StateNode* decks = list.find("decks"))
            h.decks = *decks;
    }
    if (h.decks.kind != StateNode::Kind::Array)
        h.decks = StateNode::Array();
    return h;
}

bool Supervisor::Wanted(int64_t now)
{
    (void)now;
    if (_userStopped)
        return false;
    if (_userStarted || _settings.mode == Mode::Always)
        return true;
    if (_settings.mode == Mode::Off)
        return false;
    // "deck": started when one is attached; it stays when the deck goes (its runtime waits for it, own decks work)
    return _decksAttached > 0 || _child != nullptr || _state == State::Adopted;
}

void Supervisor::Launch(int64_t now)
{
    LaunchSpec spec;
    spec.path = BinaryPathLocked();
    if (spec.path.empty())
    {
        _error = "this build has no unreal-streamdeck beside unreal-qt (configure with -DUNREAL_STREAMDECK=ON), or set "
                 "UNREAL_STREAMDECK_BIN or the binary setting";
        _state = State::Missing;
        _failedUntil = now + kMissingCheckMs;
        return;
    }
    spec.args = LaunchArgsLocked();
    spec.logFile = _host.logFile;
    std::string error;
    auto launcher = _launcher;
    auto child = launcher->Start(spec, error);
    if (!child)
    {
        _error = error.empty() ? "cannot start " + spec.path : error;
        _state = State::Failed;
        _failedUntil = now + kGiveUpMs;
        LOGWARNING("Stream Deck daemon: %s", _error.c_str());
        return;
    }
    LOGINFO("Stream Deck daemon: started %s (pid %d)", spec.path.c_str(), child->Pid());
    _child = std::move(child);
    _daemonPort = _settings.apiPort;
    _released = false;
    _state = State::Starting;
    _startedAt = now;
    _nextHealth = now + 200;  // the first look soon: it listens within a few ms
    _healthFails = 0;
    _error.clear();
}

void Supervisor::Ended(int64_t now, int code, bool crashed, const std::string& why)
{
    _lastExit = StateNode::Object();
    _lastExit["code"] = code;
    _lastExit["crashed"] = crashed;
    _lastExit["at_ms"] = now;
    if (!why.empty())
        _lastExit["why"] = why;
    _health = Health{};
    _appliedPid = 0;
    if (_stopping)
    {
        _stopping = false;
        if (_restart)
        {
            _restart = false;
            Launch(now);
        }
        else
            _state = State::Stopped;
        return;
    }
    if (_released)
    {
        _state = _settings.mode == Mode::Off ? State::Off : State::Waiting;
        return;
    }
    if (code == 0 && !crashed && now - _startedAt >= kQuickExitMs)
    {
        // a clean end of its own after a while (another starter's POST /api/v1/quit): look again, adopt what answers
        _state = State::Waiting;
        return;
    }
    // a clean end at once is a second start that found a daemon running (it handed our lease to it): adopted after
    // the backoff if it answers on our port, else counted as the failure it is (a daemon on other ports)
    ++_restarts;
    ++_failures;
    if (_failures > 3)
    {
        _state = State::Failed;
        _failedUntil = now + kGiveUpMs;
        _failures = 0;
        _error = "unreal-streamdeck ended " + std::to_string(3 + 1) + " times in a row: trying again in 60 s";
        LOGWARNING("Stream Deck daemon: %s", _error.c_str());
        return;
    }
    _state = State::Backoff;
    _restartAt = now + (int64_t{1000} << (_failures - 1));  // 1 s, 2 s, 4 s
    const std::string what = !why.empty() ? why
                             : crashed      ? std::string("crashed")
                             : code == 0    ? std::string("ended at once: another daemon holds the lock?")
                                            : "exit " + std::to_string(code);
    _error = "unreal-streamdeck ended (" + what + "): starting it again";
    LOGWARNING("Stream Deck daemon: %s", _error.c_str());
}

void Supervisor::Step(int64_t now)
{
    std::lock_guard step(_stepMutex);
    std::unique_lock l(_mutex);
    if (_state == State::Detached)
        return;
    auto http = _http;
    const int ownPid = _host.ownPid;
    // the port of the daemon we talk to: where ours was started / the adopted one answered (a new setting applies
    // to the next start)
    const int port = (_child || _state == State::Adopted || _state == State::Foreign || _stopping) ? _daemonPort
                                                                                                 : _settings.apiPort;
    bool fresh = false;  // _health was read in this step (else it may predate a PUT this thread made)

    // the decks on USB (matched, never opened)
    if (now >= _nextDeckCheck)
    {
        auto count = _host.decksAttached;
        _nextDeckCheck = now + kDeckCheckMs;
        l.unlock();
        const int n = count ? count() : 0;
        l.lock();
        _decksAttached = n;
    }

    // ours ended?
    if (_child)
    {
        int code = 0;
        bool crashed = false;
        if (_child->Exited(code, crashed))
        {
            _child.reset();
            Ended(now, code, crashed, std::string());
        }
    }

    // a stop or restart asked for
    const bool live = (_child != nullptr && !_released) || _state == State::Adopted;
    if (_restart && !live && !_stopping)
    {
        _restart = false;  // nothing runs: a restart is a start
        _userStarted = true;
    }
    if ((_userStopped || _restart) && live && !_stopping)
    {
        _stopping = true;
        _stopAt = now;
        l.unlock();
        (void)http->Call(port, "POST", "/api/v1/quit", "{}", 1000);
        l.lock();
        if (!_child)
        {
            // the adopted daemon: it goes by itself; ours to start once it no longer answers
            _state = State::Stopped;
            _health = Health{};
            _nextHealth = now;
        }
    }
    if (_stopping && _child)
    {
        if (now - _stopAt >= 2 * kStopGraceMs)
            _child->Kill();
        else if (now - _stopAt >= kStopGraceMs)
            _child->Terminate();
    }
    if (_stopping && !_child && _state != State::Starting)
    {
        // an adopted daemon told to quit: wait until it is gone
        if (now >= _nextHealth)
        {
            _nextHealth = now + 200;
            l.unlock();
            const Health h = Probe(port);
            l.lock();
            if (!h.ok || now - _stopAt >= 4 * kStopGraceMs)
            {
                _stopping = false;
                if (_restart)
                {
                    _restart = false;
                    Launch(now);
                }
                else
                    _state = State::Stopped;
            }
        }
        return;
    }
    if (_stopping)
        return;

    // the mode changed to off: the lease goes back (it ends unless another starter holds it); ours is still reaped
    if (!Wanted(now) && !_userStopped && !_released && live)
    {
        _released = true;
        l.unlock();
        (void)http->Call(port, "DELETE", "/api/v1/parents/" + std::to_string(ownPid), {}, 1000);
        l.lock();
        _state = State::Off;
        _health = Health{};
    }
    // wanted again while the released one still runs (another starter holds it): lease it again
    if (Wanted(now) && _released && _child)
    {
        l.unlock();
        const HttpResult lease =
            http->Call(port, "POST", "/api/v1/parents", "{\"pid\":" + std::to_string(ownPid) + "}", 1000);
        l.lock();
        if (lease.Ok() && _child)
        {
            _released = false;
            _state = State::Running;
            _healthyAt = now;
            _nextHealth = now;
        }
    }

    // health
    const bool watched = (_child && !_released) || _state == State::Adopted || _state == State::Foreign;
    if (watched && now >= _nextHealth)
    {
        _nextHealth = now + kHealthEveryMs;
        l.unlock();
        Health h = Probe(port);
        l.lock();
        if (_state == State::Detached)
            return;
        if (h.ok && _child && h.pid != 0 && h.pid != _child->Pid())
            h.ok = false;  // another daemon on our port: ours will end (it found the lock taken), then adopt
        if (h.ok)
        {
            _healthFails = 0;
            if (_state == State::Starting)
            {
                _state = State::Running;
                _healthyAt = now;
            }
            if (_state == State::Running && now - _healthyAt >= kStableMs)
                _failures = 0;
            _health = std::move(h);
            fresh = true;
        }
        else
        {
            ++_healthFails;
            if (_state == State::Starting && now - _startedAt >= kStartTimeoutMs && _child)
            {
                _error = "unreal-streamdeck did not answer within 10 s";
                _child->Kill();  // reaped by a later step: counted as an end
            }
            else if (_state == State::Running && _healthFails >= kHealthFailures && _child)
            {
                _error = "unreal-streamdeck stopped answering: killed";
                LOGWARNING("Stream Deck daemon: %s", _error.c_str());
                _child->Kill();
            }
            else if ((_state == State::Adopted || _state == State::Foreign) && _healthFails >= kHealthFailures)
            {
                _state = State::Waiting;  // it went: ours if still wanted
                _health = Health{};
            }
        }
    }

    // nothing of ours runs: start one, or adopt the one that answers
    if (!_child && _state != State::Adopted && _state != State::Foreign)
    {
        if (!Wanted(now))
        {
            _state = _userStopped ? State::Stopped : (_settings.mode == Mode::Off ? State::Off : State::Waiting);
        }
        else if (!(_state == State::Backoff && now < _restartAt) &&
                 !((_state == State::Failed || _state == State::Missing) && now < _failedUntil))
        {
            const int wantPort = _settings.apiPort;
            l.unlock();
            Health h = Probe(wantPort);
            l.lock();
            if (_state == State::Detached || _child)
                return;
            _daemonPort = wantPort;
            if (h.ok)
            {
                if (SameHost(h.host, _host.webapiPort))
                {
                    l.unlock();
                    const HttpResult lease = http->Call(wantPort, "POST", "/api/v1/parents",
                                                        "{\"pid\":" + std::to_string(ownPid) + "}", 1000);
                    l.lock();
                    _state = State::Adopted;
                    _released = !lease.Ok();  // an older daemon without leases: adopted, nothing to give back
                    LOGINFO("Stream Deck daemon: adopted the one running (pid %d)", h.pid);
                }
                else
                {
                    _state = State::Foreign;
                    _error = "a Stream Deck daemon on port " + std::to_string(wantPort) + " drives " + h.host;
                }
                _healthFails = 0;
                _nextHealth = now + kHealthEveryMs;
                _health = std::move(h);
                fresh = true;
            }
            else
                Launch(now);
        }
    }

    // the settings the daemon should have: the activity look, the brightness of each deck it shows
    if (_health.ok && (_state == State::Running || _state == State::Adopted))
    {
        std::vector<std::pair<std::string, std::string>> puts;  // path, body
        if (_appliedPid != _health.pid || _appliedActivity != _settings.activity)
        {
            if (_appliedPid != _health.pid)
            {
                _appliedBrightness.clear();
                _appliedPack.clear();
            }
            puts.emplace_back("/api/v1/activity", "{\"look\":\"" + _settings.activity + "\"}");
            _appliedActivity = _settings.activity;
            _appliedPid = _health.pid;
        }
        for (const StateNode& deck : _health.decks.items)
        {
            const std::string serial = StrOf(deck.find("serial"));
            auto want = _settings.brightness.find(serial);
            if (want == _settings.brightness.end())
                continue;
            auto applied = _appliedBrightness.find(serial);
            if (applied != _appliedBrightness.end() && applied->second == want->second)
                continue;
            _appliedBrightness[serial] = want->second;
            puts.emplace_back("/api/v1/decks/" + serial + "/brightness",
                              "{\"value\":" + std::to_string(want->second) + "}");
        }
        // the icon pack per deck: the kept one set again on a (re)started daemon and after a change here (the
        // settings page, the `pack` verb); a change made at the daemon (the studio, sdctl, an agent) is kept for that
        // deck. Changes at the daemon are read from a fresh health only. A daemon without the setting (older)
        // reports none: left alone
        bool learned = false;
        for (const StateNode& deck : _health.decks.items)
        {
            const std::string serial = StrOf(deck.find("serial"));
            const std::string reported = StrOf(deck.find("pack_setting"));
            if (serial.empty() || reported.empty())
                continue;
            const auto want = _settings.pack.find(serial);
            const std::string wanted = want != _settings.pack.end() ? want->second : std::string("scenario");
            const auto applied = _appliedPack.find(serial);
            if (applied == _appliedPack.end())
            {
                if (want != _settings.pack.end() && wanted != reported)
                    puts.emplace_back("/api/v1/decks/" + serial + "/pack", "{\"pack\":\"" + wanted + "\"}");
                _appliedPack[serial] = want != _settings.pack.end() ? wanted : reported;
                continue;
            }
            if (fresh && reported != applied->second)
            {
                applied->second = reported;  // changed at the daemon
                if (reported == wanted)
                    continue;
                if (reported == "scenario")
                    _settings.pack.erase(serial);
                else
                    _settings.pack[serial] = reported;
                learned = true;
                continue;
            }
            if (wanted != applied->second)
            {
                applied->second = wanted;  // changed here
                puts.emplace_back("/api/v1/decks/" + serial + "/pack", "{\"pack\":\"" + wanted + "\"}");
            }
        }
        if (!puts.empty())
        {
            l.unlock();
            for (const auto& [path, body] : puts)
                (void)http->Call(port, "PUT", path, body, 1000);
            l.lock();
        }
        if (learned)
        {
            const auto listener = _listener;
            const Settings next = _settings;
            l.unlock();
            if (listener)
                listener(next);  // kept (unreal-qt's settings)
            Notify();
        }
    }
}

HttpResult Supervisor::Daemon(const std::string& method, const std::string& path, const std::string& body,
                              int timeoutMs)
{
    std::shared_ptr<IDaemonHttp> http;
    int port = 0;
    {
        std::lock_guard l(_mutex);
        if (_state != State::Running && _state != State::Adopted)
        {
            HttpResult none;
            none.error = _state == State::Detached ? "no supervisor here (it lives in unreal-qt)"
                                                   : std::string("the Stream Deck daemon is not running (") +
                                                         StateName(_state) + ")";
            return none;
        }
        http = _http;
        port = _daemonPort;
    }
    return http->Call(port, method, path, body, timeoutMs);
}

StateNode Supervisor::Status() const
{
    std::lock_guard l(_mutex);
    StateNode s = StateNode::Object();
    s["attached"] = _state != State::Detached;
    s["state"] = StateName(_state);
    s["mode"] = ModeName(_settings.mode);
    s["activity"] = _settings.activity;
    s["api_port"] = _settings.apiPort;
    s["daemon_port"] = _daemonPort;
    s["viewers_port"] = _settings.viewersPort;
    s["binary"] = BinaryPathLocked();
    s["binary_setting"] = _settings.binary;
    s["own"] = _child != nullptr && !_released;
    s["pid"] = _child ? _child->Pid() : _health.pid;
    s["deck_attached"] = _decksAttached;
    s["restarts"] = _restarts;
    s["failures"] = _failures;
    if (_state == State::Backoff)
        s["restart_in_ms"] = std::max<int64_t>(0, _restartAt - SteadyMs());
    if (_state == State::Failed)
        s["retry_in_ms"] = std::max<int64_t>(0, _failedUntil - SteadyMs());
    s["last_exit"] = _lastExit;
    s["error"] = _error;
    s["log"] = _host.logFile;
    StateNode brightness = StateNode::Object();
    for (const auto& [serial, value] : _settings.brightness)
        brightness[serial] = value;
    s["brightness"] = brightness;
    StateNode pack = StateNode::Object();
    for (const auto& [serial, value] : _settings.pack)
        pack[serial] = value;
    s["pack"] = pack;
    s["daemon"] = _health.ok ? _health.body : StateNode();
    s["decks"] = _health.ok ? _health.decks : StateNode::Array();
    return s;
}

StateNode Supervisor::Summary() const
{
    const StateNode status = Status();
    StateNode s = StateNode::Object();
    for (const char* key : {"attached", "state", "mode", "activity", "own", "pid", "deck_attached", "restarts", "error"})
        if (const StateNode* value = status.find(key))
            s[key] = *value;
    const StateNode* daemon = status.find("daemon");
    const bool up = daemon && daemon->isObject();
    s["version"] = up && daemon->find("version") ? *daemon->find("version") : StateNode();
    s["decks"] = up && daemon->find("decks") ? *daemon->find("decks") : StateNode(0);
    s["viewers"] = up && daemon->find("viewers") ? *daemon->find("viewers") : StateNode(0);
    s["surface"] = up && daemon->find("surface") ? *daemon->find("surface") : StateNode();
    StateNode pairing = StateNode::Object();
    if (const StateNode* p = up ? daemon->find("pairing") : nullptr)
        for (const char* key : {"state", "code", "paired", "companion_id", "error"})
            if (const StateNode* value = p->find(key))
                pairing[key] = *value;
    s["pairing"] = pairing;
    // the icon pack setting of each deck as the daemon reports it (a pack, or scenario): set here, in the studio, by
    // an agent
    StateNode pack = StateNode::Object();
    if (const StateNode* decks = up ? status.find("decks") : nullptr)
        for (const StateNode& deck : decks->items)
        {
            const StateNode* serial = deck.find("serial");
            const StateNode* setting = deck.find("pack_setting");
            if (serial && serial->kind == StateNode::Kind::String && setting)
                pack[serial->s] = *setting;
        }
    s["pack"] = pack;
    return s;
}

void Supervisor::ResetForTesting()
{
    Detach();
    std::lock_guard l(_mutex);
    _launcher = std::make_shared<ProcessLauncher>();
    _http = std::make_shared<LoopbackDaemonHttp>();
    _listener = nullptr;
    _settings = Settings{};
    _host = Host{};
    _state = State::Detached;
}

const char* UdevRuleText()
{
    return "# /etc/udev/rules.d/70-unreal-streamdeck.rules: the user at the seat may open Elgato Stream Decks\n"
           "SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"0fd9\", TAG+=\"uaccess\"\n"
           "SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"0fd9\", TAG+=\"uaccess\"\n"
           "\n"
           "# install it, then unplug the deck and plug it back in:\n"
           "#   sudo install -m 0644 70-unreal-streamdeck.rules /etc/udev/rules.d/\n"
           "#   sudo udevadm control --reload-rules && sudo udevadm trigger\n"
           "# a user without a seat (SSH only): MODE=\"0660\", GROUP=\"plugdev\" in place of TAG+=\"uaccess\"\n";
}

} // namespace streamdeck
} // namespace companion
