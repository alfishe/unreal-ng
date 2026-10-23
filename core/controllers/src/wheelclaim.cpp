#include "wheelclaim.h"

#include <algorithm>

#include "devicedatabase.h"

namespace controllers
{

bool WheelClaim::IsWheel(const HostDeviceInfo& device, const DeviceDatabase& database)
{
    const DeviceMatch match = database.ForFamily(device.family, device.layout);
    return std::find(match.controls.begin(), match.controls.end(), "knob") != match.controls.end();
}

bool WheelClaim::AnyWheel(const std::vector<HostDeviceInfo>& devices, const DeviceDatabase& database)
{
    return std::any_of(devices.begin(), devices.end(),
                       [&database](const HostDeviceInfo& device) { return IsWheel(device, database); });
}

WheelClaim::Changes WheelClaim::Update(bool wheel, const std::vector<Window>& windows)
{
    std::vector<Window> wanted;
    if (wheel)
        for (Window window : windows)
            if (window != 0 && std::find(wanted.begin(), wanted.end(), window) == wanted.end())
                wanted.push_back(window);

    Changes changes;
    for (Window window : _held)
        if (std::find(wanted.begin(), wanted.end(), window) == wanted.end())
            changes.release.push_back(window);
    for (Window window : wanted)
        if (std::find(_held.begin(), _held.end(), window) == _held.end())
            changes.take.push_back(window);
    _held = std::move(wanted);
    return changes;
}

WheelClaim::Changes WheelClaim::ReleaseAll()
{
    return Update(false, {});
}

}  // namespace controllers
