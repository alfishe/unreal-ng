#include "hostinputservice.h"

#include <utility>

#include "backendfactory.h"
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
}

HostInputService::~HostInputService()
{
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
    return std::make_shared<SoundHaptics>(*_haptics, TargetsFor(instance));
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

/// region <IBackendSink>

void HostInputService::OnDeviceAdded(const HostDeviceInfo& device)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _deviceBackend[device.id] = device.backend;
    }
    _router->OnDeviceAdded(device);
    ++_revision;
}

void HostInputService::OnDeviceRemoved(const HostDeviceId& device)
{
    _haptics->Forget(device);
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
