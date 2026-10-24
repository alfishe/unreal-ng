#include "hostinputservice.h"

#include <chrono>
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
        IBackend* backend = nullptr;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            backend = BackendOfLocked(device);
        }
        return backend != nullptr && backend->SetRumble(device, levels);
    });
    // A device's feel follows its context and layers (the router decides, the backend's writer writes)
    _router->SetOutputSink([this](const HostDeviceId& device, const StepsOut& steps) { Output(device, steps); });
}

HostInputService::~HostInputService()
{
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
    return backend != nullptr && backend->HasLight(device) && backend->SetLight(device, color);
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
    IBackend* backend = nullptr;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        backend = BackendOfLocked(device);
    }
    if (backend != nullptr)
        backend->PlayClicks(device, *test.clicks);
}

void HostInputService::Identify(const HostDeviceId& device)
{
    for (const TestEffect& test : TestEffects(RumbleActuators(device)))
        if (test.id == "identify")
        {
            PlayTest(device, test);
            return;
        }
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
    // Under the lock, as HasRumble: a backend's Output only queues and never reports into this service
    std::lock_guard<std::mutex> lock(_mutex);
    IBackend* backend = BackendOfLocked(device);
    return backend != nullptr && (backend->Outputs(device) & kOutputHaptic) != 0 && backend->Output(device, out);
}

bool HostInputService::Output(const HostDeviceId& device, const StepsOut& out)
{
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
    // The family's role until somebody sets one (control surfaces Q-8: a Surface Dial is a debugger remote)
    if (!device.family.empty())
        _router->SetDefaultRole(device.id, DeviceDatabase::Builtin().ForFamily(device.family, device.layout).role);
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
    }
    _router->OnDeviceRemoved(device);
    {
        std::lock_guard<std::mutex> lock(_liveMutex);
        _live.erase(device);
    }
    ++_revision;
}

void HostInputService::OnControl(const ControlEvent& event)
{
    _router->OnControl(event);
    // An encoder's event is its steps since the last one: the live view shows where it has turned to since connect
    ControlInfo info;
    const bool encoder = ParseControl(event.control, info) && info.type == ControlType::Encoder;
    std::lock_guard<std::mutex> lock(_liveMutex);
    float& live = _live[event.device][event.control];
    live = encoder ? live + event.value : event.value;
}

std::map<std::string, float> HostInputService::Live(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_liveMutex);
    auto it = _live.find(device);
    return it == _live.end() ? std::map<std::string, float>{} : it->second;
}

/// endregion </IBackendSink>

}  // namespace controllers
