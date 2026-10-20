// Plays a scenario on a device: input -> navigation / actions, the current menu -> key images and the strip.
// One thread (step / run) owns the device: it reads input and performs every write (macOS hidapi delivers no input
// when another thread writes the same handle). Actions run on a worker thread, one at a time, FIFO.
#pragma once
#include <atomic>
#include <condition_variable>
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
    [[nodiscard]] json State() const;
    [[nodiscard]] json CurrentPage() const;              // describePage of the current menu
    [[nodiscard]] json Nodes(const json& filter) const;  // listNodes of the loaded scenario

    // Called for nav {to: scenario, file} (the owner loads the file and calls reload).
    std::function<void(const std::string&)> onSwitchScenario;

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
        Action action;
        std::string node;
        json vars;
        std::string then;
        bool toggle = false;
    };
    void Handle(const Event& e, std::int64_t nowMs);
    void Activate(int key);
    void Enqueue(Job j);
    void RunJob(Job& j);
    void ApplyNav(const json& r);
    void Render(bool force);
    void SetBanner(const std::string& text, int ms);
    void NoteMenu();

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
    std::deque<Event> _injected;
    std::deque<json> _navRequests;
    std::optional<Scenario> _pendingReload;
    std::map<std::string, bool> _toggles;
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
