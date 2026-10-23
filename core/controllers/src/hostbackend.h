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

/// Actuator levels 0..1 (haptics.md §2): the handle motors (low / high frequency) and the impulse triggers
struct RumbleLevels
{
    float low = 0;
    float high = 0;
    float leftTrigger = 0;
    float rightTrigger = 0;

    bool operator==(const RumbleLevels& other) const
    {
        return low == other.low && high == other.high && leftTrigger == other.leftTrigger &&
               rightTrigger == other.rightTrigger;
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
};

}  // namespace controllers
