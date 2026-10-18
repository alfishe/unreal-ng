#pragma once

/// @file hostbackend.h
/// @brief A source of host devices (architecture.md §4): IOHID, GameController, GameInput, evdev, ... and the fake
/// backend tests drive. A backend reports devices and control events to its sink, from any thread.

#include <cstddef>
#include <string>

#include "hostdevice.h"

namespace controllers
{

/// Where a backend reports (HostInputService). Thread-safe: backends call it from their own threads
class IBackendSink
{
public:
    virtual ~IBackendSink() = default;
    virtual void OnDeviceAdded(const HostDeviceInfo& device) = 0;
    /// The device's held controls are released
    virtual void OnDeviceRemoved(const HostDeviceId& device) = 0;
    virtual void OnControl(const ControlEvent& event) = 0;
};

/// What a backend says about itself (architecture.md §4 rule 3: probes are cheap and explicit)
struct BackendProbe
{
    std::string name;
    bool running = false;
    std::string reason;  ///< why it did not start (no runtime, no permission); empty when running
    size_t devices = 0;
};

class IBackend
{
public:
    virtual ~IBackend() = default;
    virtual std::string Name() const = 0;
    /// Start reporting to @p sink; false with the reason when the backend cannot run on this host
    virtual bool Start(IBackendSink& sink, std::string& reason) = 0;
    /// Stop reporting; the sink is not called after this returns
    virtual void Stop() = 0;
    virtual size_t DeviceCount() const = 0;
};

}  // namespace controllers
