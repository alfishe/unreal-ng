#include "hostinputservice.h"

#include <utility>

#include "backendfactory.h"

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

    void Sample(uint64_t frame, std::vector<ConnectorSample>& out) override
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
    std::vector<Entry> backends;
    {
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

/// region <IBackendSink>

void HostInputService::OnDeviceAdded(const HostDeviceInfo& device)
{
    _router->OnDeviceAdded(device);
}

void HostInputService::OnDeviceRemoved(const HostDeviceId& device)
{
    _router->OnDeviceRemoved(device);
}

void HostInputService::OnControl(const ControlEvent& event)
{
    _router->OnControl(event);
}

/// endregion </IBackendSink>

}  // namespace controllers
