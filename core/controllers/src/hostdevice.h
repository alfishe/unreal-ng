#pragma once

/// @file hostdevice.h
/// @brief A host device as the library sees it (architecture.md §1, §5): its identity, what it is for, which
/// emulator instance it drives, and the control events a backend reports for it.

#include <cstdint>
#include <string>

namespace controllers
{

/// Stable across replug and restart: "usb:045e:0b00:0517:<serial>", "bt:045e:0b22:<mac-hash>", "gc:<vendor>#<n>",
/// "kbd:system", "mouse:system", "fake:<n>" (architecture.md §5)
using HostDeviceId = std::string;

/// What a host device is for (architecture.md §1)
enum class DeviceRole : uint8_t
{
    Player,          ///< the play layer (default)
    DebuggerRemote,  ///< every control is a debug binding: never reaches a connector
    Disabled,        ///< reaches nothing
};

/// "player", "debugger-remote", "disabled"
inline const char* DeviceRoleName(DeviceRole role)
{
    switch (role)
    {
        case DeviceRole::Player: return "player";
        case DeviceRole::DebuggerRemote: return "debugger-remote";
        case DeviceRole::Disabled: return "disabled";
    }
    return "player";
}
inline bool DeviceRoleFromName(const std::string& name, DeviceRole& out)
{
    for (const DeviceRole role : {DeviceRole::Player, DeviceRole::DebuggerRemote, DeviceRole::Disabled})
        if (name == DeviceRoleName(role))
        {
            out = role;
            return true;
        }
    return false;
}

/// Which emulator instance a device drives (architecture.md §9, DT-4)
enum class InstanceMode : uint8_t
{
    FollowFocus,  ///< the instance whose window has focus (default)
    Pinned,       ///< always one instance
    All,          ///< every instance (a demo on the video wall)
};

/// What a backend reports when a device arrives
struct HostDeviceInfo
{
    HostDeviceId id;
    std::string name;     ///< for display only
    std::string family;   ///< the device database family: "xbox-elite-2", "dualsense", "generic", "keyboard", "mouse"
    std::string layout;   ///< its layout: "xbox", "playstation", "nintendo", "generic" (profiles may name either)
    std::string backend;  ///< the backend that reported it
};

/// One change of one control (architecture.md §1)
struct ControlEvent
{
    HostDeviceId device;
    std::string control;  ///< canonical name (controls.h)
    float value = 0;      ///< a button 0 / 1, an axis -1..1, a trigger 0..1, a delta a count
    uint64_t hostTimeNs = 0;
};

}  // namespace controllers
