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

}  // namespace controllers
