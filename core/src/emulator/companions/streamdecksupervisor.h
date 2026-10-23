#pragma once

/// @file streamdecksupervisor.h
/// @brief The Stream Deck daemon's supervisor (docs/inprogress/2026-10-19-streamdeck-integration/daemon.md §7,
/// architecture.md §8): unreal-qt starts the companion `unreal-streamdeck` when a deck is attached (Q-1), watches it,
/// restarts it after a crash (1 s, 2 s, 4 s, then a pause of 60 s), kills a hung one (three failed health checks),
/// adopts one that already runs (started by hand, by Stream Deck Studio), and on quit gives back its lease: the
/// daemon ends unless another starter still holds it. The emulator never links the daemon: a process and its
/// automation port (REST on 127.0.0.1).
///
/// The state machine is Step(nowMs): the supervisor's thread calls it every 250 ms; tests call it alone with a fake
/// launcher, a fake daemon and their own clock (no thread, no sleep). Everything else is thread-safe: the automation
/// planes (streamdeckapi.h), the settings page.
///
/// | State | Means |
/// |:--|:--|
/// | detached | no GUI host: the verbs answer 503 (headless, the video wall) |
/// | off | mode off and nothing of ours runs (a daemon started by hand still works) |
/// | waiting | mode "deck": no Stream Deck attached yet |
/// | starting | launched, its health check has not answered yet (at most 10 s) |
/// | running | ours, answering |
/// | adopted | another starter's daemon, answering; we hold a lease on it |
/// | foreign | a daemon answers that drives another host: left alone |
/// | backoff | ours ended: started again at `restart_in_ms` |
/// | failed | three ends in a row: a pause of 60 s, then waiting again |
/// | stopped | stopped by the user (Stop): nothing starts until Start, or the mode changes |

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <string>
#include <thread>
#include <vector>

#include "emulator/state/statenode.h"

namespace companion
{
namespace streamdeck
{

/// Start the daemon: never, when a Stream Deck is attached (default, Q-1), always (software decks without one)
enum class Mode
{
    Off,
    Deck,
    Always,
};
const char* ModeName(Mode mode);
bool ModeFromName(const std::string& name, Mode& out);

struct Settings
{
    Mode mode = Mode::Deck;
    std::string activity = "flash";  ///< Q-3: off | flash (default) | line
    int apiPort = 37053;             ///< the daemon's automation (REST + MCP)
    int viewersPort = 37050;         ///< the device wire (Stream Deck Studio windows)
    std::string binary;              ///< "" = the one beside the emulator; UNREAL_STREAMDECK_BIN wins over both
    std::map<std::string, int> brightness;  ///< per deck serial: set again when the daemon (re)starts
    /// per deck serial: its icon pack (absent = its scenario's); set again when the daemon (re)starts, and taken
    /// from the daemon when it changes there (Stream Deck Studio's toolbar, PUT /api/v1/decks/{serial}/pack)
    std::map<std::string, std::string> pack;

    bool operator==(const Settings& o) const
    {
        return mode == o.mode && activity == o.activity && apiPort == o.apiPort && viewersPort == o.viewersPort &&
               binary == o.binary && brightness == o.brightness && pack == o.pack;
    }
    bool operator!=(const Settings& o) const { return !(*this == o); }
};

/// The daemon process as the supervisor sees it (tests: a fake)
class IProcess
{
public:
    virtual ~IProcess() = default;
    virtual int Pid() const = 0;
    /// true once it ended (never blocks)
    virtual bool Exited(int& code, bool& crashed) = 0;
    virtual void Terminate() = 0;
    virtual void Kill() = 0;
};

struct LaunchSpec
{
    std::string path;
    std::vector<std::string> args;
    std::string logFile;
};

class ILauncher
{
public:
    virtual ~ILauncher() = default;
    virtual std::unique_ptr<IProcess> Start(const LaunchSpec& spec, std::string& error) = 0;
};

struct HttpResult
{
    int status = 0;  ///< 0 = no answer
    std::string body;
    std::string error;
    bool Ok() const { return status >= 200 && status < 300; }
};

/// The daemon's automation port (tests: a fake daemon)
class IDaemonHttp
{
public:
    virtual ~IDaemonHttp() = default;
    virtual HttpResult Call(int port, const std::string& method, const std::string& path, const std::string& body,
                            int timeoutMs) = 0;
};

/// What the GUI host tells the supervisor about itself
struct Host
{
    int webapiPort = 8090;   ///< the daemon's --host
    int ownPid = 0;          ///< its lease (--parent)
    std::string defaultBinary;  ///< unreal-streamdeck beside the emulator (in UnrealNG.app/Contents/MacOS, ...)
    std::string logFile;        ///< the daemon's output ("" = discarded)
    /// How many Stream Decks are attached (USB vendor 0x0FD9, matched, never opened); null = unknown (mode "deck"
    /// then never starts it)
    std::function<int()> decksAttached;
};

class Supervisor
{
public:
    static constexpr int64_t kStepMs = 250;
    static constexpr int64_t kHealthEveryMs = 2000;
    static constexpr int kHealthFailures = 3;     ///< in a row: hung (ours: killed) or gone (adopted)
    static constexpr int64_t kStartTimeoutMs = 10000;
    static constexpr int64_t kStopGraceMs = 2000;  ///< after POST /api/v1/quit, then terminate
    static constexpr int64_t kGiveUpMs = 60000;
    static constexpr int64_t kStableMs = 30000;    ///< running this long: the crash count starts again
    static constexpr int64_t kDeckCheckMs = 1000;
    static constexpr int64_t kQuickExitMs = 5000;  ///< a clean end sooner than this counts as a failed start

    /// The process's supervisor
    static Supervisor& Instance();

    Supervisor();
    ~Supervisor();
    Supervisor(const Supervisor&) = delete;
    Supervisor& operator=(const Supervisor&) = delete;

    /// unreal-qt at start: the host, the saved settings; `thread` false in tests (they call Step)
    void Attach(const Host& host, const Settings& settings, bool thread = true);
    /// unreal-qt at quit: the lease is given back (the daemon ends unless another starter holds it), the thread ends
    void Detach();
    bool Attached() const;

    /// Seams (tests); the defaults start a real process and call 127.0.0.1
    void SetLauncher(std::shared_ptr<ILauncher> launcher);
    void SetHttp(std::shared_ptr<IDaemonHttp> http);

    /// One step of the state machine (the thread's, or a test's)
    void Step(int64_t nowMs);

    /// region <Commands (any thread; applied by the next step)>
    void Start();    ///< also in mode off: the user asked
    void Stop();     ///< POST /api/v1/quit, terminate after 2 s; nothing starts again until Start
    void Restart();
    /// Changes the given fields; false with `error` for a bad value (nothing changed then)
    bool Update(const StateNode& changes, std::string& error);
    /// A deck's icon pack the daemon has just taken (the `pack` verb set it there): kept for that deck (scenario:
    /// none kept) and known as applied, so the next step neither sends it again nor takes it for a change there
    void KeepAppliedPack(const std::string& serial, const std::string& pack);
    Settings GetSettings() const;
    /// Called after the settings changed (unreal-qt saves them), on the caller's thread, no lock held
    void SetSettingsListener(std::function<void(const Settings&)> listener);
    /// endregion </Commands>

    /// {attached, state, mode, activity, api_port, viewers_port, binary, own, pid, deck_attached, restarts, failures,
    /// restart_in_ms, last_exit, error, log, brightness, pack, daemon: the daemon's /health (version, decks, viewers, parents, pairing,
    /// surface, host), decks: its /decks}
    StateNode Status() const;
    /// What the WebSocket topic `streamdeck.status` pushes: the fields of Status() that change with events, not with
    /// time (state, mode, own, pid, version, decks, viewers, deck_attached, pairing, surface, activity, error,
    /// restarts, pack: {serial: the deck's pack setting as the daemon reports it})
    StateNode Summary() const;

    /// A call to the running daemon's automation (forwarded verbs: pair, brightness); 503 when none answers. Blocks
    /// up to `timeoutMs`: never from the GUI thread
    HttpResult Daemon(const std::string& method, const std::string& path, const std::string& body = {},
                      int timeoutMs = 1500);

    /// The executable it starts: UNREAL_STREAMDECK_BIN, else the setting, else the host's
    std::string BinaryPath() const;
    /// The arguments it starts the daemon with (`run --wait --api ... --parent ...`)
    std::vector<std::string> LaunchArgs() const;

    void ResetForTesting();

private:
    enum class State
    {
        Detached,
        Off,
        Waiting,
        Starting,
        Running,
        Adopted,
        Foreign,
        Backoff,
        Failed,
        Stopped,
    };
    static const char* StateName(State state);

    struct Health
    {
        bool ok = false;
        StateNode body;   ///< /api/v1/health
        StateNode decks;  ///< /api/v1/decks's "decks"
        std::string host;
        int pid = 0;
    };
    Health Probe(int port);
    bool Wanted(int64_t now);
    void Launch(int64_t now);
    void Ended(int64_t now, int code, bool crashed, const std::string& why);
    std::vector<std::string> LaunchArgsLocked() const;
    std::string BinaryPathLocked() const;
    void Notify();
    void Run();

    mutable std::mutex _mutex;  ///< everything below
    std::mutex _stepMutex;      ///< one Step at a time
    std::shared_ptr<ILauncher> _launcher;
    std::shared_ptr<IDaemonHttp> _http;
    Host _host;
    Settings _settings;
    std::function<void(const Settings&)> _listener;
    State _state = State::Detached;
    std::unique_ptr<IProcess> _child;     ///< ours, while it runs (also after a mode change: still reaped)
    bool _released = false;               ///< the lease on _child / the adopted daemon was given back
    bool _userStopped = false, _userStarted = false, _restart = false, _stopping = false;
    int64_t _stopAt = 0;                  ///< quit asked at
    int64_t _startedAt = 0, _healthyAt = 0, _nextHealth = 0, _restartAt = 0, _failedUntil = 0, _nextDeckCheck = 0;
    int _healthFails = 0, _failures = 0, _restarts = 0, _decksAttached = 0;
    int _appliedPid = 0;                  ///< the daemon the settings were applied to
    int _daemonPort = 37053;              ///< the automation port of the daemon we talk to (ours or adopted)
    std::string _appliedActivity;
    std::map<std::string, int> _appliedBrightness;
    std::map<std::string, std::string> _appliedPack;  ///< per deck: the pack setting the daemon has (applied or seen)
    Health _health;
    std::string _error;
    StateNode _lastExit;

    std::thread _thread;
    std::condition_variable _wake;
    bool _quit = false, _kick = false;
};

/// The Linux udev rule for Stream Decks (companions/streamdeck/linux/70-unreal-streamdeck.rules) and how to install
/// it: an AppImage cannot install it, the settings page shows it
const char* UdevRuleText();

} // namespace streamdeck
} // namespace companion
