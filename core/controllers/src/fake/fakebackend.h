#pragma once

/// @file fakebackend.h
/// @brief A backend whose devices and controls are set by calls: the tests' host devices, and later a scripted
/// pad for headless runs (TODO.md C2).

#include <map>
#include <mutex>
#include <set>
#include <string>

#include "hostbackend.h"

namespace controllers
{

class FakeBackend : public IBackend
{
public:
    std::string Name() const override
    {
        return "fake";
    }
    bool Start(IBackendSink& sink, std::string& reason) override;
    void Stop() override;
    size_t DeviceCount() const override;
    bool HasRumble(const HostDeviceId& device) const override;
    bool SetRumble(const HostDeviceId& device, const RumbleLevels& levels) override;
    /// What the last SetRumble wrote to a device
    RumbleLevels Rumble(const HostDeviceId& device) const;

    /// A device arrives (id, name, family); false when the id is already connected or the backend is stopped
    bool Connect(const HostDeviceInfo& device);
    bool Disconnect(const HostDeviceId& device);
    /// A control changes: a button 0 / 1, an axis -1..1, a trigger 0..1. False for an unknown device
    bool Set(const HostDeviceId& device, const std::string& control, float value, uint64_t hostTimeNs = 0);
    bool Press(const HostDeviceId& device, const std::string& control)
    {
        return Set(device, control, 1.0f);
    }
    bool Release(const HostDeviceId& device, const std::string& control)
    {
        return Set(device, control, 0.0f);
    }

private:
    mutable std::mutex _mutex;
    IBackendSink* _sink = nullptr;
    std::set<HostDeviceId> _devices;
    std::map<HostDeviceId, RumbleLevels> _rumble;
};

}  // namespace controllers
