// Plays a scenario on a device: input -> navigation / actions, the current menu -> key images and the strip.
// One thread (step / run) owns the device: it reads input and performs every write (macOS hidapi delivers no input
// when another thread writes the same handle). Actions run on a worker thread, one at a time, FIFO.
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "sdb/actions.h"
#include "sdb/device.h"
#include "sdb/navigator.h"

namespace sdb::scn
{

struct RuntimeOptions
{
    bool syncActions = false;  // run actions inside step() (tests); otherwise on the worker thread
    bool dryRun = false;       // log actions instead of running them (navigation still works)
    int holdMs = 600;          // back key held this long = home
    // runs the `action` kind for the system host; null = its REST fallback (sdb/actionexec.h). The daemon's surface
    // client sets its own here.
    std::shared_ptr<ActionExecutor> executor{};
    int readoutPollMs = 250;  // state-bound faces: how often the host is asked for readouts nobody pushes
};

class Runtime
{
public:
    // `host` null = the system host (WebAPI, shell, ...). The device must outlive the runtime.
    Runtime(Device& dev, Scenario s, std::unique_ptr<ActionHost> host = nullptr, RuntimeOptions opt = {});
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // One iteration of the device thread: read input (up to timeoutMs), handle it, render what changed.
    void Step(int timeoutMs);
    // step() until `stop` is set; starts the action worker.
    void Run(const std::atomic<bool>& stop);

    // Thread-safe controls (API, file watcher). They are applied by the device thread on its next step.
    void Reload(Scenario s);
    void Inject(const Event& e);                                          // as if the device sent it
    bool PressNode(const std::string& id, std::string* error = nullptr);  // a key on the current page
    void Navigate(const json& request);                                   // {to: back|home|node, node}
    void SetBrightness(int percent);                                      // 0..100, sent on the next step
    [[nodiscard]] int Brightness() const;
    // The device: there or unplugged (it reconnects underneath, the runtime goes on), and a reconnect on purpose.
    [[nodiscard]] bool Connected() const
    {
        return _dev.Connected();
    }
    bool Reconnect(std::chrono::milliseconds away)
    {
        return _dev.Reconnect(away);
    }
    // Everything posted so far (Inject, PressNode, Navigate, Reload), for a caller that answers once the device thread
    // has applied it (the /press and /navigate routes): Wait returns true after the step that handled it, false on
    // the timeout or when the runtime is gone first. The ticket may outlive the runtime.
    class Ticket
    {
    public:
        bool Wait(std::chrono::milliseconds timeout) const;

    private:
        friend class Runtime;
        struct Signal
        {
            std::mutex m;
            std::condition_variable cv;
            std::uint64_t applied = 0;
            bool closed = false;
        };
        std::shared_ptr<Signal> _signal;
        std::uint64_t _seq = 0;
    };
    [[nodiscard]] Ticket Posted() const;
    [[nodiscard]] json State() const;
    [[nodiscard]] json CurrentPage() const;              // describePage of the current menu
    [[nodiscard]] json Nodes(const json& filter) const;  // listNodes of the loaded scenario

    // Called for nav {to: scenario, file} (the owner loads the file and calls reload).
    std::function<void(const std::string&)> onSwitchScenario;

    // State-bound faces (scenarios.md §4): a readout's value (true = face_on). Pushed by the surface client (S4 / SD4)
    // or polled from the host (ActionHost::Readout) while nobody pushes it.
    void SetReadout(const std::string& path, const json& value);
    // A context from the core (ctx, architecture §6): the deck goes to the nearest menu declaring it.
    void OnContext(const std::string& context);
    // An action another surface ran (the activity feed, scenarios.md §5): the keys bound to the same id flash, and
    // with activity.strip "line" the strip says `text` for activity.seconds.
    void ShowActivity(const std::string& actionId, const std::string& text = {});
    // The surface context the deck asked for last (ctx.set when a menu with "context" is entered; "" = none).
    [[nodiscard]] std::string Context() const;

    struct Stats
    {
        std::size_t keyWrites = 0, stripWrites = 0, actions = 0;
    };
    [[nodiscard]] Stats GetStats() const
    {
        std::lock_guard l(_mu);
        return _stats;
    }

private:
    struct Job
    {
        Job() = default;
        Job(Action a, std::string n, json v, std::string t, bool tg = false)
            : action(std::move(a)), node(std::move(n)), vars(std::move(v)), then(std::move(t)), toggle(tg)
        {
        }
        Action action;
        std::string node;
        json vars;
        std::string then;
        bool toggle = false;
        int phase = 0;          // +1 / -1: press / release of an action step with "hold"
        std::string state;      // the node's readout path (state-bound face)
    };
    void Handle(const Event& e, std::int64_t nowMs);
    void Activate(int key);
    void Enqueue(Job j);
    void RunJob(Job& j);
    void ApplyNav(const json& r);
    void Render(bool force);
    void SetBanner(const std::string& text, int ms);
    void NoteMenu();
    [[nodiscard]] bool IsOn(const Node& n) const;
    [[nodiscard]] std::map<std::string, bool> OnStates() const;
    void PollReadouts();

    Device& _dev;
    const Model& _m;
    Scenario _s;
    Navigator _nav;
    Page _page;
    std::unique_ptr<ActionHost> _host;
    RuntimeOptions _opt;
    int _safePad = 0;
    int _faceSize = 144;  // px a face is drawn at: the key's own size

    mutable std::recursive_mutex
        _mu;  // guards (recursive: a synchronous action may navigate): s_, nav_, page_, queues, state below
    std::uint64_t _posted = 0;  // requests posted (the queues below); Ticket's sequence
    std::shared_ptr<Ticket::Signal> _steps = std::make_shared<Ticket::Signal>();
    std::deque<Event> _injected;
    std::deque<json> _navRequests;
    std::optional<Scenario> _pendingReload;
    std::map<std::string, bool> _toggles;
    std::map<std::string, json> _readouts;     // readout path -> value
    std::map<std::string, bool> _pushed;       // readouts a surface client delivers (not polled)
    std::map<int, Job> _holds;                 // keys / dials (1000 + index) held down: their release job
    std::map<int, std::int64_t> _flashUntil;   // activity flash per key
    std::string _context;                      // the context sent last (ctx.set)
    bool _contextSent = false;
    std::vector<bool> _pressed;
    std::map<int, std::int64_t> _downAt;
    std::map<int, std::pair<std::int64_t, double>> _turnRate;  // per dial: last turn (ms), detents a second
    std::vector<std::size_t> _sentKey;                         // face hash per key (0 = never sent)
    std::size_t _sentStrip = 0;
    Rgb _stripPrev;         // the strip as last sent: the next write sends only the columns that differ
    std::string _sentPage;  // last published page JSON
    std::string _banner;
    std::string _status;   // the strip's status readout (time-travel position), until the menu changes
    std::thread _monitor;  // strip.status "ttd": polls time travel
    std::atomic<bool> _monitorKick{false};
    [[nodiscard]] static std::string TtdReadout(const json& t, double fps);
    static constexpr size_t kStatusChars = 36;
    std::int64_t _bannerUntil = 0, _lastActivity = 0;
    int _brightness = 70;
    bool _brightnessDirty = true;
    json _last = json::array();  // recent action results
    Stats _stats;

    std::mutex _qm;
    std::condition_variable _qcv;
    std::deque<Job> _jobs;
    std::atomic<bool> _cancel{false}, _workerStop{false};
    std::thread _worker;
};

}  // namespace sdb::scn
