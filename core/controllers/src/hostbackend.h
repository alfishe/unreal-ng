#pragma once

/// @file hostbackend.h
/// @brief A source of host devices (architecture.md §4): IOHID, GameController, GameInput, evdev, ... and the fake
/// backend tests drive. A backend reports devices and control events to its sink, from any thread.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
    /// Device @p from was another endpoint of device @p to and gave way to it (architecture.md §5: a `gc:` device
    /// whose IOHID endpoint appeared); @p from was removed before. The settings chosen for it may move
    virtual void OnDevicePromoted(const HostDeviceId& from, const HostDeviceId& to)
    {
        (void)from;
        (void)to;
    }
};

/// Actuator levels 0..1 (haptics.md §2): the handle motors (low / high frequency) and the impulse triggers
struct RumbleLevels
{
    float low = 0;
    float high = 0;
    float leftTrigger = 0;
    float rightTrigger = 0;
    /// HD haptics (haptics.md §6.1 `hd`): the frequency the low / high level plays at on a voice-coil pad (Switch HD
    /// rumble, CoreHaptics sharpness), 0 its default; ERM motors ignore it
    uint16_t lowHz = 0;
    uint16_t highHz = 0;

    bool operator==(const RumbleLevels& other) const
    {
        return low == other.low && high == other.high && leftTrigger == other.leftTrigger &&
               rightTrigger == other.rightTrigger && lowHz == other.lowHz && highHz == other.highHz;
    }
    bool operator!=(const RumbleLevels& other) const
    {
        return !(*this == other);
    }
};

/// The actuators of a device (haptics.md §6.1 targets): bits of IBackend::RumbleActuators
enum RumbleActuator : uint8_t
{
    kRumbleLow = 1,
    kRumbleHigh = 2,
    kRumbleLeftTrigger = 4,
    kRumbleRightTrigger = 8,
    /// A clicker: discrete waveforms (a click, a buzz) and no levels - the Surface Dial's Simple Haptic Controller.
    /// It plays HapticClicks through IBackend::PlayClicks; the motor levels of the game's haptics do not reach it
    kHapticClicker = 16,
};

/// One effect of a clicker (kHapticClicker): a waveform played @p count times, @p periodMs apart
struct HapticClicks
{
    enum class Wave : uint8_t
    {
        Click,
        Buzz,
    };
    Wave wave = Wave::Click;
    uint8_t count = 1;
    uint16_t periodMs = 0;
};

/// region <Surface outputs (control surfaces controls-and-feedback.md §3-§4)>

/// The outputs of a control surface: bits of IBackend::Outputs
enum OutputKind : uint8_t
{
    kOutputHaptic = 1,  ///< plays a HapticPattern (a mark the hand feels)
    kOutputSteps = 2,   ///< steps per turn and whether each step clicks (the context's feel)
};

/// A pattern a profile names; the driver chooses the bytes (a Surface Dial: SurfaceDial::PatternReport)
enum class HapticPattern : uint8_t
{
    Tick,    ///< one click: a second
    Double,  ///< two clicks 60 ms apart: ten seconds, a bookmark
    Buzz,    ///< a buzz: the end of the history, a barrier
};

struct HapticOut
{
    HapticPattern pattern = HapticPattern::Tick;
};

/// The encoder's feel: steps per turn and a click at each step. clicks false: a free wheel, the steps stay (Q-6)
struct StepsOut
{
    uint16_t perTurn = 36;
    bool clicks = true;

    bool operator==(const StepsOut& other) const
    {
        return perTurn == other.perTurn && clicks == other.clicks;
    }
    bool operator!=(const StepsOut& other) const
    {
        return !(*this == other);
    }
};

/// endregion </Surface outputs>

/// region <Battery and light (architecture.md §10)>

/// A device's battery as the OS reports it (IBackend::Battery). Unknown is the honest default: GameController gives
/// no level for an Xbox Elite 2 (research-host-apis.md §2.3), GameInput has no battery API, a wired pad has none
struct BatteryState
{
    bool known = false;     ///< the OS reports something about the battery; nothing else is set while false
    int level = -1;         ///< percent 0..100; -1 when the OS gives only a state (charging, wired)
    bool coarse = false;    ///< the level is a step (XInput's four, the kernel's capacity_level), not a measurement
    bool charging = false;
    bool wired = false;     ///< on the cable: charging, full or not charging, or a wired pad without a battery

    bool operator==(const BatteryState& other) const
    {
        return known == other.known && level == other.level && coarse == other.coarse && charging == other.charging &&
               wired == other.wired;
    }
    bool operator!=(const BatteryState& other) const
    {
        return !(*this == other);
    }
};

/// "unknown", "80 %", "about 67 %", "charging, 40 %", "wired": the text every surface and the dialog show
inline std::string BatteryText(const BatteryState& battery)
{
    if (!battery.known)
        return "unknown";
    std::string level;
    if (battery.level >= 0)
        level = std::string(battery.coarse ? "about " : "") + std::to_string(battery.level) + " %";
    if (battery.charging)
        return level.empty() ? "charging" : "charging, " + level;
    if (battery.wired)
        return level.empty() ? "wired" : "wired, " + level;
    return level.empty() ? "unknown level" : level;
}

/// A light's colour (a DualSense / DualShock 4 light bar): 0..255 per channel, all zero = off
struct LightColor
{
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;

    bool operator==(const LightColor& other) const
    {
        return red == other.red && green == other.green && blue == other.blue;
    }
    bool operator!=(const LightColor& other) const
    {
        return !(*this == other);
    }
};

/// "#RRGGBB" / "RRGGBB", "r,g,b" (0..255 each) or "off"; false for anything else
inline bool ParseLightColor(const std::string& text, LightColor& out)
{
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    if (text == "off")
    {
        out = {};
        return true;
    }
    const std::string digits = !text.empty() && text[0] == '#' ? text.substr(1) : text;
    if (digits.size() == 6 && text.find(',') == std::string::npos)
    {
        uint8_t channel[3] = {};
        for (size_t index = 0; index < 3; index++)
        {
            const int high = hex(digits[index * 2]);
            const int low = hex(digits[index * 2 + 1]);
            if (high < 0 || low < 0)
                return false;
            channel[index] = static_cast<uint8_t>(high * 16 + low);
        }
        out = {channel[0], channel[1], channel[2]};
        return true;
    }
    int channel[3] = {};
    size_t at = 0;
    for (size_t index = 0; index < 3; index++)
    {
        const size_t end = index < 2 ? text.find(',', at) : text.size();
        if (end == std::string::npos || end == at || end - at > 3)
            return false;
        int value = 0;
        for (size_t c = at; c < end; c++)
        {
            if (text[c] < '0' || text[c] > '9')
                return false;
            value = value * 10 + (text[c] - '0');
        }
        if (value > 255)
            return false;
        channel[index] = value;
        at = end + 1;
    }
    out = {static_cast<uint8_t>(channel[0]), static_cast<uint8_t>(channel[1]), static_cast<uint8_t>(channel[2])};
    return true;
}

/// "#RRGGBB"
inline std::string LightColorText(const LightColor& color)
{
    static const char* digits = "0123456789abcdef";
    std::string text = "#";
    for (const uint8_t channel : {color.red, color.green, color.blue})
    {
        text += digits[channel >> 4];
        text += digits[channel & 0x0F];
    }
    return text;
}

/// endregion </Battery and light>

/// What a backend says about itself (architecture.md §4 rule 3: probes are cheap and explicit)
struct BackendProbe
{
    std::string name;
    bool running = false;
    std::string reason;  ///< why it did not start (no runtime, no permission); empty when running
    size_t devices = 0;
};

/// One endpoint of a host device (architecture.md §5): an IOHID interface, a GameController companion, the device
/// itself for a backend that sees one endpoint. The dialog's per-endpoint capability table (ui-ux.md §3.4)
struct EndpointInfo
{
    std::string backend;  ///< "iohid", "gamecontroller", "xinput"
    std::string name;     ///< what the OS calls it ("Xbox Wireless Controller"; a GCController's vendor name)
    std::string role;     ///< "input" (gives the device's input), "outputs" (adds outputs only), "none"
    size_t controls = 0;  ///< controls its driver decodes
    std::string driver;   ///< "hid descriptor", "gip", "surface dial", "gamecontroller", ...
    bool rumble = false;
    bool light = false;
    bool battery = false;  ///< reports a battery level
    std::string detail;    ///< anything else ("usage page 01 usage 05", "tentative match")
};

/// One input report as it reached the backend, for the test panel's raw view (ui-ux.md §3.4)
struct RawReport
{
    uint64_t timeMs = 0;  ///< steady clock
    std::vector<uint8_t> bytes;  ///< the report id first when the device has report ids; at most ReportTap::kBytes
    size_t size = 0;             ///< the report's whole length
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

    /// What the device cannot do here and how to fix it, for the dialog's device title (ui-ux.md, "Permission
    /// missing"): no hidraw access on Linux, paddles hidden by an onboard profile (D15). "" when nothing is missing
    virtual std::string DeviceNotice(const HostDeviceId& device) const
    {
        (void)device;
        return {};
    }

    /// region <Haptics (haptics.md §9)>
    /// The device has motors this backend can drive
    virtual bool HasRumble(const HostDeviceId& device) const
    {
        (void)device;
        return false;
    }
    /// RumbleActuator bits; the handle motors where HasRumble
    virtual uint8_t RumbleActuators(const HostDeviceId& device) const
    {
        return HasRumble(device) ? static_cast<uint8_t>(kRumbleLow | kRumbleHigh) : 0;
    }
    /// Why the device's motors cannot be driven here, in words a user can act on; "" where HasRumble
    virtual std::string RumbleUnavailable(const HostDeviceId& device) const
    {
        return HasRumble(device) ? std::string() : "The " + Name() + " backend drives no motors";
    }
    /// Set the actuators; a device without triggers ignores their levels. False when it has no motors here
    virtual bool SetRumble(const HostDeviceId& device, const RumbleLevels& levels)
    {
        (void)device;
        (void)levels;
        return false;
    }
    /// Play a clicker's effect (kHapticClicker); false for a device that is not one
    virtual bool PlayClicks(const HostDeviceId& device, const HapticClicks& clicks)
    {
        (void)device;
        (void)clicks;
        return false;
    }
    /// endregion </Haptics>

    /// region <Surface outputs (controls-and-feedback.md §3)>
    /// OutputKind bits of the device; 0 for a device without surface outputs
    virtual uint8_t Outputs(const HostDeviceId& device) const
    {
        (void)device;
        return 0;
    }
    /// Play a pattern. Never blocks: it is queued for the device's writer, a newer pattern replaces one not written
    /// yet. False for a device without kOutputHaptic
    virtual bool Output(const HostDeviceId& device, const HapticOut& out)
    {
        (void)device;
        (void)out;
        return false;
    }
    /// Set the steps and clicks. Never blocks, the latest wins; the backend applies it again after a reconnect.
    /// False for a device without kOutputSteps
    virtual bool Output(const HostDeviceId& device, const StepsOut& out)
    {
        (void)device;
        (void)out;
        return false;
    }
    /// endregion </Surface outputs>

    /// region <Battery and light (architecture.md §10)>
    /// The battery as the OS last reported it. Never blocks: a backend whose OS read may wait (a HID feature report
    /// over Bluetooth) refreshes a copy on its own thread and returns that
    virtual BatteryState Battery(const HostDeviceId& device) const
    {
        (void)device;
        return {};
    }
    /// The device has a light this backend can set (SetLight)
    virtual bool HasLight(const HostDeviceId& device) const
    {
        (void)device;
        return false;
    }
    /// Why the device's light cannot be set here, in words a user can act on; "" where HasLight
    virtual std::string LightUnavailable(const HostDeviceId& device) const
    {
        return HasLight(device) ? std::string() : "The " + Name() + " backend sets no light";
    }
    /// Set the light's colour; never blocks (queued, or a sysfs write). False when the device has no light here
    virtual bool SetLight(const HostDeviceId& device, const LightColor& color)
    {
        (void)device;
        (void)color;
        return false;
    }
    /// endregion </Battery and light>

    /// region <Diagnostics (ui-ux.md §3.4)>
    /// The device's endpoints as this backend sees them, the input one first; empty: the service describes the device
    /// as one endpoint from what the backend answers
    virtual std::vector<EndpointInfo> Endpoints(const HostDeviceId& device) const
    {
        (void)device;
        return {};
    }
    /// The device gives raw reports (RawReports answers them); false: why in RawReportsUnavailable
    virtual bool HasRawReports(const HostDeviceId& device) const
    {
        (void)device;
        return false;
    }
    virtual std::string RawReportsUnavailable(const HostDeviceId& device) const
    {
        return HasRawReports(device) ? std::string() : "The " + Name() + " backend keeps no raw reports";
    }
    /// The newest input reports of the device, oldest first. Reading starts the tap: the backend copies reports only
    /// while someone reads them (ReportTap: a few seconds after the last read it stops)
    virtual std::vector<RawReport> RawReports(const HostDeviceId& device)
    {
        (void)device;
        return {};
    }
    /// endregion </Diagnostics>
};

}  // namespace controllers
