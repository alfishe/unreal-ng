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
#include "sdb/surfacereadouts.h"
#include "sdb/valueface.h"

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

    // State-bound faces (scenarios.md §4): a readout's value (true = face_on); value faces (§4.2): the value shown.
    // Pushed by the surface client (S4 / SD4) or polled from the host (ActionHost::Readout) while nobody pushes it.
    void SetReadout(const std::string& path, const json& value);
    // A context from the core (ctx, architecture §6): the deck goes to the nearest menu declaring it.
    void OnContext(const std::string& context);
    // An action another surface ran (the activity feed, scenarios.md §5): the keys bound to the same id flash, and
    // with activity.strip "line" the strip says `text` for activity.seconds.
    void ShowActivity(const std::string& actionId, const std::string& text = {});
    // The surface context the deck asked for last (ctx.set when a menu with "context" is entered; "" = none).
    [[nodiscard]] std::string Context() const;

    // The surface client (SD5, sdb/surfaceclient.h). Live: the host pushes the readouts, nothing is polled (the
    // monitor idles); not live after live: the strip's target line says the link is down, polling resumes.
    void SetLive(bool live);
    // A readout for the strip (ttd.position: the TTD timeline; run.state, media.tape, machine: the status words;
    // ttd.marks: the marks of the last seek)
    void SetStripReadout(const std::string& name, const json& value);
    // The surface's target ({instance, index, model, ram_kb, view, mode}; null: none): strip.target and status target
    void SetTarget(const json& target);
    // A lasting line on the strip, over the menu's content ("" clears it): "COMPANION REVOKED - TOUCH TO PAIR"
    void SetNotice(const std::string& text);
    [[nodiscard]] std::string Notice() const;
    // A tap on the strip while a notice shows (NOT PAIRED, REVOKED, a pairing's state): this instead of the page's
    // tap (the daemon starts a pairing). Set before Run; called on the device thread with the runtime's lock held:
    // it must only start something, never wait or call into the daemon's decks
    std::function<void()> onNoticeTap;
    // The activity look over the scenario's settings.activity (Q-3; unreal-qt's settings page, PUT /api/v1/activity):
    // "" = the scenario's, "off", "flash" (the keys bound to the action), "line" (the keys and a strip line)
    void SetActivityLook(const std::string& look);
    // The icon pack of the glyph faces over the scenario's settings.pack (the deck's setting, PUT
    // /api/v1/decks/{serial}/pack): "" = the scenario's. The keys showing a glyph are drawn again at the next step.
    void SetPack(const std::string& pack);
    [[nodiscard]] std::string PackSetting() const;  // "" = the scenario's
    [[nodiscard]] std::string Pack() const;         // the pack drawn: the setting, settings.pack, else magicwb
    [[nodiscard]] std::string ScenarioPack() const;  // the scenario's settings.pack ("" = none)
    // What the loaded scenario needs from the core: readouts, state paths, target
    [[nodiscard]] SurfaceNeeds Needs() const;
    // After a reload is applied (the device thread, no lock held): the surface client asks Needs() again
    std::function<void()> onScenarioChanged;

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
    [[nodiscard]] ValueShown ShownLocked(const FaceValue& v) const;  // a value face's text now (_mu held)
    [[nodiscard]] json PageLocked() const;  // DescribePage and what the value faces show (_mu held)
    void PollReadouts();
    void EnsureMonitor();

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
    bool _live = false, _wasLive = false;  // readouts pushed by the surface client; was so once
    std::map<std::string, json> _strip;    // strip readouts (SetStripReadout)
    json _target;                          // the surface's target (null: none)
    std::string _notice;                   // a lasting strip line
    std::string _activityLook;             // SetActivityLook ("" = the scenario's)
    std::string _pack;                     // SetPack ("" = the scenario's)
    [[nodiscard]] std::string PackLocked() const;
    std::string _activityLine;             // activity.strip "line": what another surface did
    std::int64_t _activityUntil = 0;
    std::thread _monitor;  // the REST fallback's poller (strip.status "ttd", faces' readouts); none while live
    bool _monitorRunning = false;  // under _mu
    bool _running = false;         // Run() drives the steps (tests call Step() alone: no threads)
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
