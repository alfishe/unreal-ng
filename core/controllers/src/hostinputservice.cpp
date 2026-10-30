#include "hostinputservice.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <utility>

#include "backendfactory.h"
#include "devicedatabase.h"
#include "haptics/profilehaptics.h"
#include "haptics/soundhaptics.h"
#include "hostcontrols.h"

namespace controllers
{

namespace
{
/// One emulator instance's view of the router
class RouterSource : public IHostInputSource
{
public:
    RouterSource(std::shared_ptr<InputRouter> router, std::string instance)
        : _router(std::move(router)), _instance(std::move(instance))
    {
    }
    ~RouterSource() override
    {
        _router->ForgetInstance(_instance);
    }

    void Sample(uint64_t frame, HostSample& out) override
    {
        _router->Sample(_instance, frame, out);
    }

private:
    std::shared_ptr<InputRouter> _router;
    std::string _instance;
};
}  // namespace

HostInputService::HostInputService() : _router(std::make_shared<InputRouter>())
{
    _haptics = std::make_unique<HapticMixer>([this](const HostDeviceId& device, const RumbleLevels& levels) {
        // The backend is written outside _mutex (a backend reports into it under its own lock); _hapticsWrite keeps
        // it alive against Stop
        std::lock_guard<std::mutex> write(_hapticsWrite);
        if (!_owner.load())
            return false;  // another client owns the devices: the mixer forgets this one
        IBackend* backend = nullptr;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            backend = BackendOfLocked(device);
        }
        return backend != nullptr && backend->SetRumble(device, levels);
    });
    // A device's feel follows its context and layers (the router decides, the backend's writer writes)
    _router->SetOutputSink([this](const HostDeviceId& device, const StepsOut& steps) { Output(device, steps); });
    _router->SetWakeSink([this] { WakeTicker(); });
}

HostInputService::~HostInputService()
{
    StopTicker();
    _router->SetOutputSink(nullptr);
    Stop();
}

/// region <Backends>

void HostInputService::StartNative()
{
    for (std::unique_ptr<IBackend>& backend : BackendFactory::CreateNative())
    {
        std::string reason;
        AddBackend(std::move(backend), reason);
    }
}

bool HostInputService::AddBackend(std::unique_ptr<IBackend> backend, std::string& reason)
{
    if (!backend)
    {
        reason = "no backend";
        return false;
    }
    // Started outside the lock: a backend may report its first devices from Start
    IBackend* raw = backend.get();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _backends.push_back({std::move(backend), false, {}});
    }
    const bool running = raw->Start(*this, reason);
    std::lock_guard<std::mutex> lock(_mutex);
    for (Entry& entry : _backends)
    {
        if (entry.backend.get() == raw)
        {
            entry.running = running;
            entry.reason = running ? std::string{} : reason;
        }
    }
    return running;
}

IBackend* HostInputService::FindBackend(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const Entry& entry : _backends)
    {
        if (entry.backend->Name() == name)
            return entry.backend.get();
    }
    return nullptr;
}

std::vector<BackendProbe> HostInputService::Probe() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<BackendProbe> list;
    for (const Entry& entry : _backends)
        list.push_back({entry.backend->Name(), entry.running, entry.reason, entry.backend->DeviceCount()});
    return list;
}

std::string HostInputService::DeviceNotice(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const IBackend* backend = BackendOfLocked(device);
    return backend != nullptr ? backend->DeviceNotice(device) : std::string();
}

BatteryState HostInputService::Battery(const HostDeviceId& device) const
{
    // Under the lock, as DeviceNotice: a backend's Battery returns its copy and never waits for the device
    std::lock_guard<std::mutex> lock(_mutex);
    const IBackend* backend = BackendOfLocked(device);
    return backend != nullptr ? backend->Battery(device) : BatteryState{};
}

std::vector<EndpointInfo> HostInputService::Endpoints(const HostDeviceId& device) const
{
    const size_t seen = Live(device).size();  // before _mutex: the two locks are never held together
    std::lock_guard<std::mutex> lock(_mutex);
    IBackend* backend = BackendOfLocked(device);
    if (backend == nullptr)
        return {};
    std::vector<EndpointInfo> endpoints = backend->Endpoints(device);
    for (EndpointInfo& endpoint : endpoints)
        if (endpoint.role == "input")
            endpoint.battery = backend->Battery(device).known;
    if (!endpoints.empty())
        return endpoints;
    // One endpoint: what the backend answers for the device
    EndpointInfo endpoint;
    endpoint.backend = backend->Name();
    endpoint.role = "input";
    endpoint.rumble = backend->HasRumble(device);
    endpoint.light = backend->HasLight(device);
    endpoint.battery = backend->Battery(device).known;
    endpoint.controls = seen;
    endpoint.detail = "controls seen so far";
    return {endpoint};
}

std::string HostInputService::RawReportsUnavailable(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const IBackend* backend = BackendOfLocked(device);
    if (backend == nullptr)
        return "The device is not connected";
    return backend->RawReportsUnavailable(device);
}

std::vector<RawReport> HostInputService::RawReports(const HostDeviceId& device)
{
    std::lock_guard<std::mutex> lock(_mutex);
    IBackend* backend = BackendOfLocked(device);
    return backend != nullptr ? backend->RawReports(device) : std::vector<RawReport>{};
}

bool HostInputService::HasLight(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const IBackend* backend = BackendOfLocked(device);
    return backend != nullptr && backend->HasLight(device);
}

std::string HostInputService::LightUnavailable(const HostDeviceId& device) const
{
    for (const HostDeviceInfo& info : _router->Devices())
        if (info.id == device && (info.family == "keyboard" || info.family == "mouse"))
            return "The emulator sets no light on keyboards and mice";
    std::lock_guard<std::mutex> lock(_mutex);
    const IBackend* backend = BackendOfLocked(device);
    if (backend == nullptr)
        return "The device is not connected";
    return backend->LightUnavailable(device);
}

bool HostInputService::SetLight(const HostDeviceId& device, const LightColor& color)
{
    // Under the lock, as Output: a backend's SetLight queues or writes one sysfs file and never reports into us
    std::lock_guard<std::mutex> lock(_mutex);
    IBackend* backend = BackendOfLocked(device);
    if (backend == nullptr || !backend->HasLight(device) || (_owner.load() && !backend->SetLight(device, color)))
        return false;
    _lights[device] = color;  // what Identify's blink goes back to, and what the owner sets again (SetOwner)
    return true;
}

void HostInputService::SetOwner(bool owner)
{
    if (_owner.load() == owner)
        return;
    if (!owner)
    {
        _haptics->StopAll();  // while still the owner: the motors are written to rest
        _owner = false;
        _router->SetSuspended(true);
        return;
    }
    _owner = true;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& [device, color] : _lights)
            if (IBackend* backend = BackendOfLocked(device); backend != nullptr && backend->HasLight(device))
                backend->SetLight(device, color);
    }
    _router->SetSuspended(false);  // the feel of every device again
}

void HostInputService::Stop()
{
    if (_haptics)
        _haptics->StopAll();  // H-5: every motor stopped on exit, not left at its last level
    std::vector<Entry> backends;
    {
        std::lock_guard<std::mutex> write(_hapticsWrite);
        std::lock_guard<std::mutex> lock(_mutex);
        backends.swap(_backends);
    }
    for (Entry& entry : backends)
    {
        if (entry.running)
            entry.backend->Stop();
    }
    for (const HostDeviceInfo& device : _router->Devices())
        _router->OnDeviceRemoved(device.id);
}

/// endregion </Backends>

/// region <Unknown devices>

namespace
{
DeviceIdentity IdentityOf(const HostDeviceInfo& device)
{
    DeviceIdentity identity;
    IdentityFromDeviceId(device.id, identity);
    identity.name = device.name;
    identity.backend = device.backend;
    return identity;
}
}  // namespace

void HostInputService::ApplyUserMapLocked(const HostDeviceInfo& device)
{
    const DeviceIdentity identity = IdentityOf(device);
    if (identity.vendor == 0 && identity.product == 0)
        return;
    const DeviceMatch match = DeviceDatabase::Active()->Lookup(identity);
    std::vector<MappedControl> map;
    std::string error;
    if (match.score >= 80 && !match.map.empty() && ParseControlMap(match.map, map, error))
        _maps[device.id] = std::move(map);
    else
        _maps.erase(device.id);  // the user family went, or never had a map
}

void HostInputService::RefreshUserMaps()
{
    std::lock_guard<std::mutex> lock(_mapMutex);
    for (const auto& [id, device] : _connected)
        ApplyUserMapLocked(device);
}

DeviceMatch HostInputService::LookupDevice(const HostDeviceInfo& device) const
{
    const DeviceIdentity identity = IdentityOf(device);
    const std::shared_ptr<const DeviceDatabase> database = DeviceDatabase::Active();
    if (identity.vendor == 0 && identity.product == 0)
    {
        // No VID / PID in its id (a GameController pad, the keyboard): the family its backend gave it
        DeviceMatch match = database->ForFamily(device.family, device.layout);
        match.name = device.name;
        match.step = "backend";
        return match;
    }
    return database->Lookup(identity);
}

void HostInputService::SetControlMap(const HostDeviceId& device, std::vector<MappedControl> map)
{
    std::lock_guard<std::mutex> lock(_mapMutex);
    if (map.empty())
        _maps.erase(device);
    else
        _maps[device] = std::move(map);
}

std::vector<MappedControl> HostInputService::ControlMap(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mapMutex);
    const auto it = _maps.find(device);
    return it != _maps.end() ? it->second : std::vector<MappedControl>{};
}

void HostInputService::StartLearn(const HostDeviceId& device, LearnSession session)
{
    session.Baseline(Live(device));
    std::lock_guard<std::mutex> lock(_mapMutex);
    _learn[device] = std::move(session);
}

bool HostInputService::Learn(const HostDeviceId& device, const std::function<void(LearnSession&)>& step)
{
    std::lock_guard<std::mutex> lock(_mapMutex);
    const auto it = _learn.find(device);
    if (it == _learn.end())
        return false;
    step(it->second);
    return true;
}

bool HostInputService::EndLearn(const HostDeviceId& device)
{
    std::lock_guard<std::mutex> lock(_mapMutex);
    return _learn.erase(device) != 0;
}

/// endregion </Unknown devices>

/// region <Router timer>

void HostInputService::WakeTicker()
{
    std::lock_guard<std::mutex> lock(_tickMutex);
    if (_tickStop)
        return;
    if (!_ticker.joinable())
        _ticker = std::thread([this] { TickerLoop(); });
    _tickWake = true;
    _tickCv.notify_one();
}

void HostInputService::TickerLoop()
{
    auto steady = [] {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
    };
    std::unique_lock<std::mutex> lock(_tickMutex);
    while (!_tickStop)
    {
        _tickWake = false;
        // The lights Identify blinked that are due go back (outside the lock: SetLight takes _mutex)
        std::vector<std::pair<HostDeviceId, LightColor>> restore;
        uint64_t blinkWait = UINT64_MAX;
        const uint64_t now = steady();
        for (auto it = _blinks.begin(); it != _blinks.end();)
        {
            if (it->second.first <= now)
            {
                restore.emplace_back(it->first, it->second.second);
                it = _blinks.erase(it);
            }
            else
            {
                blinkWait = std::min(blinkWait, it->second.first - now);
                ++it;
            }
        }
        lock.unlock();
        for (const auto& [device, color] : restore)
            SetLight(device, color);
        const uint64_t deadline = _router->NextDeadline();
        const uint64_t routerNow = _router->NowNs();
        if (deadline != UINT64_MAX && deadline <= routerNow)
        {
            _router->Poll();
            lock.lock();
            continue;
        }
        const uint64_t wait = std::min(blinkWait, deadline == UINT64_MAX ? UINT64_MAX : deadline - routerNow);
        lock.lock();
        if (_tickStop || _tickWake)
            continue;
        if (wait == UINT64_MAX)
            _tickCv.wait(lock, [this] { return _tickStop || _tickWake; });
        else
            _tickCv.wait_for(lock, std::chrono::nanoseconds(wait), [this] { return _tickStop || _tickWake; });
    }
}

void HostInputService::StopTicker()
{
    _router->SetWakeSink(nullptr);
    std::thread ticker;
    {
        std::lock_guard<std::mutex> lock(_tickMutex);
        _tickStop = true;
        ticker.swap(_ticker);
    }
    _tickCv.notify_all();
    if (ticker.joinable())
        ticker.join();
}

/// endregion </Router timer>

std::shared_ptr<IHostInputSource> HostInputService::SourceFor(const std::string& instance)
{
    return std::make_shared<RouterSource>(_router, instance);
}

/// region <Haptics>

IBackend* HostInputService::BackendOfLocked(const HostDeviceId& device) const
{
    auto name = _deviceBackend.find(device);
    if (name == _deviceBackend.end())
        return nullptr;
    for (const Entry& entry : _backends)
        if (entry.running && entry.backend->Name() == name->second)
            return entry.backend.get();
    return nullptr;
}

bool HostInputService::HasRumble(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const IBackend* backend = BackendOfLocked(device);
    return backend != nullptr && backend->HasRumble(device);
}

std::string HostInputService::HapticsUnavailable(const HostDeviceId& device) const
{
    for (const HostDeviceInfo& info : _router->Devices())
        if (info.id == device && (info.family == "keyboard" || info.family == "mouse"))
            return "Keyboards and mice have no motors";
    std::lock_guard<std::mutex> lock(_mutex);
    const IBackend* backend = BackendOfLocked(device);
    if (backend == nullptr)
        return "The device is not connected";
    return backend->RumbleUnavailable(device);
}

uint8_t HostInputService::RumbleActuators(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const IBackend* backend = BackendOfLocked(device);
    return backend != nullptr ? backend->RumbleActuators(device) : 0;
}

void HostInputService::PlayEffect(const HostDeviceId& device, const HapticEffect& effect)
{
    _haptics->Play(device, effect);
}

void HostInputService::PlayTest(const HostDeviceId& device, const TestEffect& test)
{
    if (!test.clicks)
    {
        _haptics->Play(device, test.effect);
        return;
    }
    std::lock_guard<std::mutex> write(_hapticsWrite);
    if (!_owner.load())
        return;
    IBackend* backend = nullptr;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        backend = BackendOfLocked(device);
    }
    if (backend != nullptr)
        backend->PlayClicks(device, *test.clicks);
}

HostInputService::Identified HostInputService::Identify(const HostDeviceId& device)
{
    Identified done;
    if (HapticsUnavailable(device).empty())
        for (const TestEffect& test : TestEffects(RumbleActuators(device)))
            if (test.id == "identify")
            {
                PlayTest(device, test);
                done.rumble = true;
                break;
            }
    if (HasLight(device))
    {
        LightColor was;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            const auto it = _lights.find(device);
            if (it != _lights.end())
                was = it->second;
        }
        {
            // A second Identify inside the blink keeps the colour the first one goes back to
            std::lock_guard<std::mutex> lock(_tickMutex);
            if (const auto it = _blinks.find(device); it != _blinks.end())
                was = it->second.second;
        }
        if (SetLight(device, LightColor{255, 255, 255}))
        {
            done.light = true;
            const uint64_t back = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                            std::chrono::steady_clock::now().time_since_epoch())
                                                            .count()) +
                                  static_cast<uint64_t>(_identifyBlinkMs.load()) * 1'000'000;
            {
                std::lock_guard<std::mutex> lock(_tickMutex);
                _blinks[device] = {back, was};
            }
            WakeTicker();
        }
    }
    return done;
}

std::function<std::vector<HostDeviceId>()> HostInputService::TargetsFor(const std::string& instance)
{
    return [this, instance] {
        std::vector<HostDeviceId> targets;
        for (const HostDeviceInfo& device : _router->Devices())
            if (_router->Drives(device.id, instance) && HasRumble(device.id))
                targets.push_back(device.id);
        return targets;
    };
}

std::shared_ptr<IHapticFeed> HostInputService::HapticFeedFor(const std::string& instance)
{
    return std::make_shared<SoundHaptics>(*_haptics, TargetsFor(instance), _hapticsDsp.load());
}

std::shared_ptr<IHapticFeed> HostInputService::HapticFeedFor(const std::string& instance, const HapticProfile& profile,
                                                             const std::string& name)
{
    return std::make_shared<ProfileHaptics>(profile, name, *_haptics, TargetsFor(instance),
                                            [this](const HostDeviceId& device) { return RumbleActuators(device); });
}

void HostInputService::SetHapticsGate(bool open)
{
    _haptics->SetGate(open);
}

void HostInputService::SetHapticsStrength(float strength)
{
    _haptics->SetStrength(strength);
}

/// endregion </Haptics>

/// region <Surface outputs>

uint8_t HostInputService::Outputs(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const IBackend* backend = BackendOfLocked(device);
    return backend != nullptr ? backend->Outputs(device) : 0;
}

bool HostInputService::Output(const HostDeviceId& device, const HapticOut& out)
{
    if (!_owner.load())
        return false;
    // Under the lock, as HasRumble: a backend's Output only queues and never reports into this service
    std::lock_guard<std::mutex> lock(_mutex);
    IBackend* backend = BackendOfLocked(device);
    return backend != nullptr && (backend->Outputs(device) & kOutputHaptic) != 0 && backend->Output(device, out);
}

bool HostInputService::Output(const HostDeviceId& device, const StepsOut& out)
{
    if (!_owner.load())
        return false;
    // Under the lock, as HasRumble: a backend's Output only queues and never reports into this service
    std::lock_guard<std::mutex> lock(_mutex);
    IBackend* backend = BackendOfLocked(device);
    return backend != nullptr && (backend->Outputs(device) & kOutputSteps) != 0 && backend->Output(device, out);
}

HostInputService::MarksPlayed HostInputService::PlayMarks(const HostDeviceId& device,
                                                          const std::vector<std::string>& marks, uint64_t nowMs)
{
    MarksPlayed result;
    if (marks.empty())
    {
        result.reason = "no marks";
        return result;
    }
    if ((Outputs(device) & kOutputHaptic) == 0)
    {
        result.reason = "the device plays no patterns";
        return result;
    }
    LayerHaptics feel;
    if (!_router->Feel(device, feel))
    {
        result.reason = "none of the device's layers has a feel (its debug profile's haptics)";
        return result;
    }
    if (!PatternForMarks(feel, marks, result.pattern, result.mark))
    {
        result.reason = "the layer plays none of these marks";
        return result;
    }
    if (nowMs == 0)
        nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
    {
        std::lock_guard<std::mutex> lock(_marksMutex);
        if (!_marks.Admit(device, result.mark, nowMs, feel.minIntervalMs))
        {
            result.reason = "a mark played less than " + std::to_string(feel.minIntervalMs) + " ms ago";
            return result;
        }
    }
    result.played = Output(device, HapticOut{result.pattern});
    if (!result.played)
        result.reason = "the device refused the pattern";
    return result;
}

/// endregion </Surface outputs>

/// region <IBackendSink>

void HostInputService::OnDeviceAdded(const HostDeviceInfo& device)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _deviceBackend[device.id] = device.backend;
    }
    {
        // A device of a user family with a `map` (the learn wizard's, an import's) gets it
        std::lock_guard<std::mutex> lock(_mapMutex);
        _connected[device.id] = device;
        ApplyUserMapLocked(device);
    }
    // The family's role until somebody sets one (control surfaces Q-8: a Surface Dial is a debugger remote)
    if (!device.family.empty())
        _router->SetDefaultRole(device.id, DeviceDatabase::Active()->ForFamily(device.family, device.layout).role);
    _router->OnDeviceAdded(device);
    ++_revision;
}

void HostInputService::OnDeviceRemoved(const HostDeviceId& device)
{
    _haptics->Forget(device);
    {
        std::lock_guard<std::mutex> lock(_marksMutex);
        _marks.Forget(device);
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _deviceBackend.erase(device);
        _lights.erase(device);
    }
    {
        std::lock_guard<std::mutex> lock(_mapMutex);
        _connected.erase(device);
        _learn.erase(device);
    }
    _router->OnDeviceRemoved(device);
    {
        std::lock_guard<std::mutex> lock(_liveMutex);
        _live.erase(device);
    }
    ++_revision;
}

void HostInputService::OnDevicePromoted(const HostDeviceId& from, const HostDeviceId& to)
{
    _router->Promote(from, to);
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _promotions.emplace_back(from, to);
    }
    ++_revision;
}

std::vector<std::pair<HostDeviceId, HostDeviceId>> HostInputService::TakePromotions()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return std::exchange(_promotions, {});
}

void HostInputService::OnControl(const ControlEvent& event)
{
    bool learning = false;
    std::vector<ControlEvent> mapped;
    {
        std::lock_guard<std::mutex> lock(_mapMutex);
        if (auto it = _learn.find(event.device); it != _learn.end())
        {
            // The wizard has the device: its events answer the prompts and reach nothing else
            it->second.Feed(event.control, event.value);
            learning = true;
        }
        else if (auto map = _maps.find(event.device); map != _maps.end())
            mapped = TranslateControl(map->second, event);
    }
    if (!learning)
    {
        _router->OnControl(event);
        for (const ControlEvent& canonical : mapped)
            _router->OnControl(canonical);
    }
    // An encoder's event is its steps since the last one: the live view shows where it has turned to since connect
    ControlInfo info;
    const bool encoder = ParseControl(event.control, info) && info.type == ControlType::Encoder;
    std::lock_guard<std::mutex> lock(_liveMutex);
    std::map<std::string, float>& live = _live[event.device];
    float& value = live[event.control];
    value = encoder ? value + event.value : event.value;
    if (!learning)
        for (const ControlEvent& canonical : mapped)
            live[canonical.control] = canonical.value;
}

std::map<std::string, float> HostInputService::Live(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_liveMutex);
    auto it = _live.find(device);
    return it == _live.end() ? std::map<std::string, float>{} : it->second;
}

/// endregion </IBackendSink>

}  // namespace controllers
