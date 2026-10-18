#pragma once

/// @file hostinputservice.h
/// @brief The process-wide entry of the controllers library (architecture.md §2-§3): it runs the backends, receives
/// their devices and control events, routes them (InputRouter), and gives each emulator instance the
/// IHostInputSource its ControllerManager samples at every frame boundary.

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "emulator/io/controllers/ihostinputsource.h"
#include "hostbackend.h"
#include "router/inputrouter.h"

namespace controllers
{

class HostInputService : public IBackendSink
{
public:
    HostInputService();
    ~HostInputService() override;

    HostInputService(const HostInputService&) = delete;
    HostInputService& operator=(const HostInputService&) = delete;

    /// region <Backends>

    /// The native backends of this OS (BackendFactory), started; the ones that cannot run are listed by Probe
    void StartNative();
    /// Start @p backend and keep it; false with the reason when it cannot run (it is still listed by Probe)
    bool AddBackend(std::unique_ptr<IBackend> backend, std::string& reason);
    /// The backend of that name, or nullptr
    IBackend* FindBackend(const std::string& name) const;
    std::vector<BackendProbe> Probe() const;
    /// Stop every backend: their devices are removed
    void Stop();

    /// endregion </Backends>

    InputRouter& Router()
    {
        return *_router;
    }

    /// The source an emulator instance's ControllerManager samples (ControllerManager::SetHostInputSource);
    /// @p instance is the emulator's id. Dropping the source forgets the instance
    std::shared_ptr<IHostInputSource> SourceFor(const std::string& instance);

    /// region <IBackendSink>
    void OnDeviceAdded(const HostDeviceInfo& device) override;
    void OnDeviceRemoved(const HostDeviceId& device) override;
    void OnControl(const ControlEvent& event) override;
    /// endregion </IBackendSink>

private:
    struct Entry
    {
        std::unique_ptr<IBackend> backend;
        bool running = false;
        std::string reason;
    };

    mutable std::mutex _mutex;
    std::vector<Entry> _backends;
    std::shared_ptr<InputRouter> _router;
};

}  // namespace controllers
