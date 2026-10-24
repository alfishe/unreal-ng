#pragma once

/// @file hidrawnode.h
/// @brief The Linux hidraw side of the evdev backend (research-host-apis.md §3.3, surface-dial.md §4) as pure logic,
/// compiled on every OS so its tests run everywhere: which devices want their hidraw node and what for, the node of
/// an evdev node's HID device found through sysfs (`/sys/class/input/eventN/device/device/hidraw/hidrawK`, its
/// `uevent` checked for the same VID:PID), the endpoint merge of one evdev node and one hidraw node into one logical
/// device, and the reasons the dialog shows when the node cannot be opened (the udev rule to install).

#include <cstdint>
#include <map>
#include <string>

#include "hid/hidpad.h"

namespace controllers
{

/// What a device's hidraw node is opened for: bits, one device may want several (the Elite 2 over Bluetooth without
/// FF_RUMBLE: Input and Rumble; a DualSense Edge on a kernel before 7.2: Input and Triggers)
enum class HidrawUse : uint8_t
{
    None = 0,
    /// read: the paddles and buttons evdev does not report. The Xbox Elite 2 over Bluetooth (HidPad: paddles, onboard
    /// profile), the DualSense Edge where hid-playstation predates its extra buttons (Linux 7.2: SonyPad)
    Input = 1,
    Output = 2,  ///< write: the Surface Dial's clicks (output report 1) and detents (feature report 1)
    Rumble = 4,  ///< write: an Xbox One / Series pad over Bluetooth whose evdev node gives no FF_RUMBLE (report 3)
    /// write: a DualSense's adaptive triggers (output report 2 / 0x31 with only the trigger flags) beside
    /// hid-playstation, which drives the motors (FF_RUMBLE), the light bar and the player LEDs itself
    Triggers = 8,
    /// write: a Switch Pro controller's or a Joy-Con's HD rumble (report 0x10 at the effect's frequencies) beside
    /// hid-nintendo, whose FF_RUMBLE plays fixed 160 / 320 Hz
    SwitchRumble = 16,
    /// read and write: a PlayStation or Switch pad no kernel driver of its own serves (hid-generic: a kernel before
    /// hid-playstation / hid-nintendo, the module not loaded). The whole pad through hidraw as on macOS: SonyPad /
    /// SwitchProPad input and the SonyProtocol / SwitchProtocol set-up and outputs (rumble, light, player LEDs)
    Protocol = 32,
};

constexpr HidrawUse operator|(HidrawUse a, HidrawUse b)
{
    return static_cast<HidrawUse>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
/// @p uses has the bit of @p use
constexpr bool HasUse(HidrawUse uses, HidrawUse use)
{
    return (static_cast<uint8_t>(uses) & static_cast<uint8_t>(use)) != 0;
}

/// Where a Linux pad's motors are driven (haptics.md §9)
enum class LinuxRumble : uint8_t
{
    None,
    Evdev,   ///< FF_RUMBLE on the evdev node: the handle motors (xpad, hid-microsoft, xpadneo, hid-playstation, ...)
    Hidraw,  ///< output report 3 on the hidraw node (XboxBluetoothOutput): both motors and the impulse triggers
};

/// @p evdevRumble: the evdev node opened for writing and offers FF_RUMBLE, then evdev drives the motors (a kernel
/// driver owns them); else an Xbox One / Series pad over Bluetooth (@p xboxBluetooth, XboxBluetoothOutput::Handles)
/// gets report 3 through hidraw: hid-generic through BlueZ's uhid, without hid-microsoft or xpadneo
LinuxRumble LinuxRumbleFor(bool evdevRumble, bool xboxBluetooth);

/// The kernel driver bound to a HID device, from its uevent's DRIVER= (the name under /sys/bus/hid/drivers)
enum class LinuxHidDriver : uint8_t
{
    Unknown,      ///< not read (no HID device: xpad's USB interface; a test without a tree)
    Generic,      ///< hid-generic: the descriptor's usages through hid-input, nothing of the pad's own protocol
    PlayStation,  ///< hid-playstation (DualSense since 5.12, DualShock 4 since 6.2)
    Sony,         ///< hid-sony (DualShock 3; the DualShock 4 before 6.2)
    Nintendo,     ///< hid-nintendo (Switch Pro controller, Joy-Cons, the charging grip; since 5.16)
    Microsoft,    ///< hid-microsoft: the Xbox pads over Bluetooth get FF_RUMBLE, their keys stay hid-input's
    Stadia,       ///< hid-google-stadiaff: the Stadia controller's FF_RUMBLE, its keys hid-input's
    Steam,        ///< hid-steam (Steam Controller, Steam Deck): no input device while the Steam client runs
    Other,        ///< any other (xpadneo, a vendor driver)
};
/// "hid-generic" -> Generic, "playstation", "sony", "nintendo", "microsoft", "stadia", "hid-steam"; "" -> Unknown
LinuxHidDriver LinuxHidDriverOf(const std::string& driver);
/// The driver leaves the keys and axes to hid-input's mapping of the HID usages (Button n is the n-th key, not a
/// position): hid-generic and the drivers that only add force feedback. The family's HID map names them
bool LinuxHidUsageNamed(LinuxHidDriver driver);

/// What the evdev backend knows of a device when it decides its hidraw uses
struct LinuxPadFacts
{
    bool bluetooth = false;  ///< the evdev node's bus
    uint16_t vendor = 0;
    uint16_t product = 0;
    std::string name;    ///< the device's own (an NSO NES pad tells itself from a Joy-Con by it)
    std::string family;  ///< the device database's
    std::string layout;
    LinuxHidDriver driver = LinuxHidDriver::Unknown;
    bool evdevRumble = false;  ///< as LinuxRumbleFor
    /// The evdev node has the DualSense Edge's extra buttons (hid-playstation 7.2+: BTN_TRIGGER_HAPPY1-4)
    bool edgeButtons = false;
    /// The family's HID map names controls hid-input does not put on the pad's evdev node: vendor bits (no usage at
    /// all). Read through hidraw. Its consumer usages are KEY_* on the pad's node (LinuxGenericButton)
    bool hidExtras = false;
};

/// The hidraw uses of a device (bits). The kernel driver is preferred where it has the path: the PlayStation pads
/// under hid-playstation / hid-sony and the Switch pads under hid-nintendo give their controls, motors, light and
/// battery through evdev and sysfs; hidraw adds only what the driver lacks (Triggers, SwitchRumble, the Edge's Input
/// on kernels before 7.2). An Unknown driver is taken as the kernel's own driver for those pads. Under hid-generic
/// they are Protocol pads
HidrawUse HidrawUseFor(const LinuxPadFacts& facts);
/// @p bluetooth: the evdev node's bus; @p family, @p layout: the device database's; @p evdevRumble as LinuxRumbleFor
HidrawUse HidrawUseFor(bool bluetooth, uint16_t vendor, uint16_t product, const std::string& family,
                       const std::string& layout, bool evdevRumble);

/// A HID device's `uevent` (`HID_ID=0005:0000045E:00000B22`, `HID_NAME=`, `HID_PHYS=`, `HID_UNIQ=`)
struct HidUevent
{
    uint16_t bus = 0;
    uint16_t vendor = 0;
    uint16_t product = 0;
    std::string name;
    std::string phys;
    std::string uniq;
    std::string driver;  ///< DRIVER= (the bound HID driver's name), "" when unbound
};

/// False when the text has no valid HID_ID line (not a HID device: xpad's USB interface, a platform device)
bool ParseHidUevent(const std::string& text, HidUevent& out);

struct HidrawNode
{
    std::string node;       ///< `<devRoot>/hidrawK`
    std::string hidDevice;  ///< the HID device's sysfs directory
    HidUevent uevent;
};

/// The hidraw node of evdev node @p eventName ("event7") under @p sysRoot ("/sys") and @p devRoot ("/dev"): false
/// when the node's parent is not a HID device, has no hidraw child, or is not @p vendor : @p product
bool FindHidrawNode(const std::string& sysRoot, const std::string& devRoot, const std::string& eventName,
                    uint16_t vendor, uint16_t product, HidrawNode& out);

/// The udev rule shipped in data/controllers/70-unreal-ng.rules and how to install it
extern const char* const kHidrawRulesHint;

/// Why a device's hidraw node is not in use for @p use (one bit), in words a user can act on. @p node empty: no hidraw
/// node was found; else @p error is the errno of the failed open() and @p errorText its text
/// @p family (the device database's) words Input for the DualSense Edge, whose paddles and Fn buttons it is, and
/// Protocol for the GameCube adapter
/// (any other named family: its vendor bits; empty: the Elite 2 paddles)
std::string HidrawUnavailable(HidrawUse use, const std::string& node, int error, const std::string& errorText,
                              const std::string& family = {});

/// The open() access a use needs: Input reads, Protocol reads and writes, the others write
bool HidrawUseReads(HidrawUse use);
bool HidrawUseWrites(HidrawUse use);

/// The D15 hint for the Elite 2 over USB on Linux: xpad reports the paddles on onboard profile 0 only, and the
/// profile itself is not readable there
std::string XpadPaddlesNotice();

/// One logical device from two endpoints (architecture.md §4 endpoint merge): an evdev node and the hidraw node of
/// the same HID device. Only the active source reaches the sink, so nothing arrives twice; hidraw is the source while
/// it is open (its HidPad gives every control, the paddles, the profile and the mirror undo, D19), evdev before and
/// after. A switch releases what the source switched away from still held
class HidrawSourceMerge
{
public:
    enum class Source : uint8_t
    {
        Evdev,
        Hidraw,
    };

    Source Active() const
    {
        return _active;
    }
    /// A control from @p source: to @p sink when that source is active, else dropped
    void Control(Source source, const std::string& control, float value, const ControlSink& sink);
    /// Make @p to the source; every control the other one left non-zero goes to 0 through @p sink
    void Switch(Source to, const ControlSink& sink);

private:
    Source _active = Source::Evdev;
    std::map<std::string, float> _held;  ///< the active source's controls that are not at rest
};

}  // namespace controllers
