#pragma once

/// @file fakebackend.h
/// @brief A backend whose devices and controls are set by calls: the tests' host devices, and later a scripted
/// pad for headless runs (TODO.md C2).

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

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

    /// The notice of @p device (DeviceNotice); "" removes it
    void SetNotice(const HostDeviceId& device, const std::string& notice);
    std::string DeviceNotice(const HostDeviceId& device) const override;

    /// region <Battery and light>
    /// The battery @p device reports (Battery); the default is unknown
    void SetBattery(const HostDeviceId& device, const BatteryState& battery);
    BatteryState Battery(const HostDeviceId& device) const override;
    /// Give @p device a light (before or after it connects); false takes it away
    void SetHasLight(const HostDeviceId& device, bool light);
    bool HasLight(const HostDeviceId& device) const override;
    std::string LightUnavailable(const HostDeviceId& device) const override;
    bool SetLight(const HostDeviceId& device, const LightColor& color) override;
    /// The colours SetLight was given, in order
    std::vector<LightColor> Lights(const HostDeviceId& device) const;
    /// endregion </Battery and light>

    /// region <Surface outputs: a fake Surface Dial>
    /// Give @p device these OutputKind bits (before or after it connects)
    void SetOutputs(const HostDeviceId& device, uint8_t outputs);
    uint8_t Outputs(const HostDeviceId& device) const override;
    bool Output(const HostDeviceId& device, const HapticOut& out) override;
    bool Output(const HostDeviceId& device, const StepsOut& out) override;
    /// What the device was given, in order
    std::vector<HapticPattern> Patterns(const HostDeviceId& device) const;
    std::vector<StepsOut> Steps(const HostDeviceId& device) const;
    /// endregion </Surface outputs>

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
    std::map<HostDeviceId, std::string> _notices;
    std::map<HostDeviceId, BatteryState> _batteries;
    std::set<HostDeviceId> _lightable;
    std::map<HostDeviceId, std::vector<LightColor>> _lights;
    /// Its own lock: a device's arrival reports from under _mutex, and the router asks for its outputs at once
    mutable std::mutex _outputMutex;
    std::map<HostDeviceId, uint8_t> _outputs;
    std::map<HostDeviceId, std::vector<HapticPattern>> _patterns;
    std::map<HostDeviceId, std::vector<StepsOut>> _steps;
};

}  // namespace controllers
