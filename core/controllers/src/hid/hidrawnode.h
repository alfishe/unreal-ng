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

/// What a device's hidraw node is opened for
enum class HidrawUse : uint8_t
{
    None,
    Input,   ///< read: the Xbox Elite 2 over Bluetooth, whose paddles and onboard profile evdev does not report
    Output,  ///< write: the Surface Dial's clicks (output report 1) and detents (feature report 1)
};

/// @p bluetooth: the evdev node's bus; @p family: the device database family
HidrawUse HidrawUseFor(bool bluetooth, uint16_t vendor, uint16_t product, const std::string& family);

/// A HID device's `uevent` (`HID_ID=0005:0000045E:00000B22`, `HID_NAME=`, `HID_PHYS=`, `HID_UNIQ=`)
struct HidUevent
{
    uint16_t bus = 0;
    uint16_t vendor = 0;
    uint16_t product = 0;
    std::string name;
    std::string phys;
    std::string uniq;
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

/// Why a device's hidraw node is not in use, in words a user can act on. @p node empty: no hidraw node was found;
/// else @p error is the errno of the failed open() and @p errorText its text
std::string HidrawUnavailable(HidrawUse use, const std::string& node, int error, const std::string& errorText);

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
