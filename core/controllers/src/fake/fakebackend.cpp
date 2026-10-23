#include "fakebackend.h"

namespace controllers
{

bool FakeBackend::Start(IBackendSink& sink, std::string& reason)
{
    std::lock_guard<std::mutex> lock(_mutex);
    reason.clear();
    _sink = &sink;
    return true;
}

void FakeBackend::Stop()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _sink = nullptr;
    _devices.clear();
    _rumble.clear();
}

size_t FakeBackend::DeviceCount() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _devices.size();
}

bool FakeBackend::Connect(const HostDeviceInfo& device)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_sink == nullptr || !_devices.insert(device.id).second)
        return false;
    HostDeviceInfo info = device;
    info.backend = Name();
    _sink->OnDeviceAdded(info);
    return true;
}

bool FakeBackend::Disconnect(const HostDeviceId& device)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_sink == nullptr || _devices.erase(device) == 0)
        return false;
    _sink->OnDeviceRemoved(device);
    return true;
}

bool FakeBackend::Set(const HostDeviceId& device, const std::string& control, float value, uint64_t hostTimeNs)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_sink == nullptr || _devices.count(device) == 0)
        return false;
    _sink->OnControl({device, control, value, hostTimeNs});
    return true;
}

bool FakeBackend::HasRumble(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _devices.count(device) != 0;
}

bool FakeBackend::SetRumble(const HostDeviceId& device, const RumbleLevels& levels)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_devices.count(device) == 0)
        return false;
    _rumble[device] = levels;
    return true;
}

RumbleLevels FakeBackend::Rumble(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _rumble.find(device);
    return it == _rumble.end() ? RumbleLevels{} : it->second;
}

void FakeBackend::SetNotice(const HostDeviceId& device, const std::string& notice)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (notice.empty())
        _notices.erase(device);
    else
        _notices[device] = notice;
}

std::string FakeBackend::DeviceNotice(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _notices.find(device);
    return it == _notices.end() ? std::string() : it->second;
}

void FakeBackend::SetBattery(const HostDeviceId& device, const BatteryState& battery)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _batteries[device] = battery;
}

BatteryState FakeBackend::Battery(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _batteries.find(device);
    return it == _batteries.end() || _devices.count(device) == 0 ? BatteryState{} : it->second;
}

void FakeBackend::SetHasLight(const HostDeviceId& device, bool light)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (light)
        _lightable.insert(device);
    else
        _lightable.erase(device);
}

bool FakeBackend::HasLight(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _devices.count(device) != 0 && _lightable.count(device) != 0;
}

std::string FakeBackend::LightUnavailable(const HostDeviceId& device) const
{
    return HasLight(device) ? std::string() : "The fake device has no light";
}

bool FakeBackend::SetLight(const HostDeviceId& device, const LightColor& color)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_devices.count(device) == 0 || _lightable.count(device) == 0)
        return false;
    _lights[device].push_back(color);
    return true;
}

std::vector<LightColor> FakeBackend::Lights(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _lights.find(device);
    return it == _lights.end() ? std::vector<LightColor>{} : it->second;
}

void FakeBackend::SetOutputs(const HostDeviceId& device, uint8_t outputs)
{
    std::lock_guard<std::mutex> lock(_outputMutex);
    _outputs[device] = outputs;
}

uint8_t FakeBackend::Outputs(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_outputMutex);
    const auto it = _outputs.find(device);
    return it == _outputs.end() ? 0 : it->second;
}

bool FakeBackend::Output(const HostDeviceId& device, const HapticOut& out)
{
    std::lock_guard<std::mutex> lock(_outputMutex);
    const auto it = _outputs.find(device);
    if (it == _outputs.end() || (it->second & kOutputHaptic) == 0)
        return false;
    _patterns[device].push_back(out.pattern);
    return true;
}

bool FakeBackend::Output(const HostDeviceId& device, const StepsOut& out)
{
    std::lock_guard<std::mutex> lock(_outputMutex);
    const auto it = _outputs.find(device);
    if (it == _outputs.end() || (it->second & kOutputSteps) == 0)
        return false;
    _steps[device].push_back(out);
    return true;
}

std::vector<HapticPattern> FakeBackend::Patterns(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_outputMutex);
    const auto it = _patterns.find(device);
    return it == _patterns.end() ? std::vector<HapticPattern>{} : it->second;
}

std::vector<StepsOut> FakeBackend::Steps(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_outputMutex);
    const auto it = _steps.find(device);
    return it == _steps.end() ? std::vector<StepsOut>{} : it->second;
}

}  // namespace controllers
