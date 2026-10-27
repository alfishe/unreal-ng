#pragma once

/// @file wheelclaim.h
/// @brief Which of the client's windows take the system wheel from the OS (surface-dial.md §4.1). Windows feeds a
/// Surface Dial to its own RadialController as well: in the focused app its built-in tools (volume, scroll, zoom,
/// undo) act on every turn and a press and hold opens the wheel menu, while the emulator reads the same Dial through
/// HID. While a wheel device is connected, each window of the client holds a RadialController of its own with the
/// menu suppressed and the built-in tools cleared; when the last wheel goes, or the window, the hold is released.
/// This is the pure part: what to take and what to release; the client does the OS calls
/// (unreal-qt platform/windows/wheelmenu_windows).

#include <cstdint>
#include <vector>

#include "hostdevice.h"

namespace controllers
{

class DeviceDatabase;

class WheelClaim
{
public:
    /// A native window handle (an HWND on Windows); 0 is no window
    using Window = uintptr_t;

    struct Changes
    {
        std::vector<Window> take;     ///< hold the wheel for these
        std::vector<Window> release;  ///< give it back to the OS for these
        bool Empty() const
        {
            return take.empty() && release.empty();
        }
    };

    /// A device the OS also treats as a wheel: its family names a `knob` (the Surface Dial)
    static bool IsWheel(const HostDeviceInfo& device, const DeviceDatabase& database);
    /// Any of @p devices is a wheel
    static bool AnyWheel(const std::vector<HostDeviceInfo>& devices, const DeviceDatabase& database);

    /// The windows that hold the wheel now: every one of @p windows (zeros and repeats skipped) while @p wheel, none
    /// otherwise. A window gone from the list, or one whose handle changed, is released; the releases come first
    Changes Update(bool wheel, const std::vector<Window>& windows);
    /// Release every held window (the client quits)
    Changes ReleaseAll();

    const std::vector<Window>& Held() const
    {
        return _held;
    }

private:
    std::vector<Window> _held;
};

}  // namespace controllers
