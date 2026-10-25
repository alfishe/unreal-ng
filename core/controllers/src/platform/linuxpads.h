#pragma once

/// @file linuxpads.h
/// @brief The Linux evdev backend's naming and identity as pure logic, compiled on every OS so its tests run
/// everywhere (the key and axis codes are the kernel's UAPI values, fixed once released):
/// - the canonical control of a kernel driver's key and axis per pad kind: the positional names every driver uses
///   (BTN_SOUTH ..), xpad's Elite paddles, hid-playstation's DualSense Edge buttons (Linux 7.2), hid-nintendo's Switch
///   Pro controller and Joy-Cons (alone held sideways, or as the halves of a pair the backend joins, the same layouts
///   SDL and the macOS backend give them, switchpro.h), and joycond's combined Joy-Con device;
/// - the device id (architecture.md §5) from the evdev node's bus, ids, `uniq` and `phys`;
/// - the Joy-Cons joycond has taken (it grabs their evdev nodes and makes them root's) and the id of the combined
///   device it offers instead: the pair id the backend gives the same two Joy-Cons when it joins them itself.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "hid/hidpad.h"

namespace controllers
{

/// How a pad's evdev keys and axes are named
enum class LinuxPadKind : uint8_t
{
    Standard,       ///< positional names (xpad, hid-microsoft, xpadneo, hid-playstation, hid-sony, generic pads)
    DualSenseEdge,  ///< hid-playstation 7.2+: Fn1 / Fn2 / left / right paddle as BTN_TRIGGER_HAPPY1-4
    SwitchPro,      ///< hid-nintendo's Pro controller: BTN_Z is Capture (`share`)
    JoyConLeft,     ///< a left Joy-Con alone (hid-nintendo), held sideways
    JoyConRight,    ///< a right Joy-Con alone
    PairLeft,       ///< the left half of a pair the backend joined
    PairRight,      ///< the right half
    JoycondCombined,  ///< joycond's "Nintendo Switch Combined Joy-Cons" (uinput 057E:2008)
    /// hid-nintendo's Nintendo Switch Online pads (6.8+, nescon / n64con / gencon_button_mappings), named as
    /// SwitchProPad names them on macOS. The SNES pad's keys are the Pro's: Standard
    NsoNes,
    NsoN64,
    NsoGenesis,
};

/// The kind of a device: @p rightStick (it has ABS_RX) tells a half of the charging grip 057E:200E apart, whose product
/// id is the same for both; @p nintendoDriver: hid-nintendo serves it (else its keys are hid-generic's: Standard);
/// @p name the device's, which tells an NSO NES pad from a right Joy-Con (SwitchProPad::LayoutFor)
LinuxPadKind LinuxPadKindFor(uint16_t vendor, uint16_t product, bool rightStick, bool nintendoDriver,
                             const std::string& name = {});
/// A Joy-Con alone or a pair's half; the kind of a Joy-Con of that side
bool IsJoyConKind(LinuxPadKind kind);
bool IsLeftJoyConKind(LinuxPadKind kind);
LinuxPadKind JoyConKind(bool left, bool paired);

/// The canonical control of key @p code on a pad of @p kind; nullptr for a key without one. @p analogTriggers: the
/// device also reports ABS_Z / ABS_RZ (hid-playstation sends BTN_TL2 / BTN_TR2 beside them), so the keys are left out
/// and `lt` / `rt` stay analog (the router derives `lt.click` / `rt.click`)
const char* LinuxPadButton(LinuxPadKind kind, unsigned code, bool analogTriggers = false);

/// What an absolute axis of a pad is
struct LinuxPadAxis
{
    const char* control = nullptr;  ///< nullptr: none (the hat is the d-pad, handled apart)
    bool unit = false;              ///< a trigger: 0..1 over the range; else -1..1
    bool negate = false;            ///< the sideways Joy-Cons turn their stick a quarter
    bool dpad = false;              ///< X / Y are the cross (HidLayout::dpadAxes): past half way is pressed
};
LinuxPadAxis LinuxPadAxisOf(LinuxPadKind kind, unsigned code);

/// region <hid-generic>
/// A device hid-generic serves: the kernel names its keys by the HID usage's number, not by what the button is
/// (hid-input.c, hidinput_configure_usage: Button n is BTN_JOYSTICK / BTN_GAMEPAD + n-1 for n <= 16, else
/// BTN_TRIGGER_HAPPY1 + n-17; X Y Z Rx Ry Rz are ABS_X .. ABS_RZ, Brake / Accelerator ABS_BRAKE / ABS_GAS). Turned
/// back into the usage they come from, the family's HID map (HidLayoutFor(DeviceMatch), the one HidPad uses on
/// macOS and Windows) names them, so a pad has the same controls on every OS
/// The HID button number (from 0) of key @p code under hid-generic; -1 for a key that is no button usage
int LinuxGenericButtonIndex(unsigned code);
/// The consumer-page usage hid-input gives key @p code (KEY_HOMEPAGE AC Home 0x223, KEY_BACK AC Back 0x224,
/// KEY_RECORD Record 0xB2, ...); -1 for another key
int LinuxConsumerUsage(unsigned code);
/// The control of key @p code: the layout's name of the button, `button.<n>` when it names none; a consumer usage by
/// the layout's `consumer` map; "" for anything else
std::string LinuxGenericButton(const HidLayout& layout, unsigned code);
/// The control of absolute axis @p code under hid-generic with @p layout (control nullptr: `axis.<code>`)
LinuxPadAxis LinuxGenericAxis(const HidLayout& layout, unsigned code);
/// endregion </hid-generic>

/// region <Identity>
/// FNV-1a as 8 hex digits, for ids that must not carry a MAC address or a port path as such
std::string LinuxIdHash(const std::string& text);
/// architecture.md §5: `bt:<vid>:<pid>:<hash of uniq, else phys>`; `usb:<vid>:<pid>:<version>:<uniq>`;
/// `usb:<vid>:<pid>:<version>@<hash of phys, else the node>`; a virtual device (uinput: joycond) `virtual:` in place of
/// `usb:`
std::string LinuxDeviceId(uint16_t bus, uint16_t vendor, uint16_t product, uint16_t version, const std::string& uniq,
                          const std::string& phys, const std::string& node);

/// A Joy-Con's input device in sysfs (`class/input/inputN`)
struct LinuxJoyConHalf
{
    std::string id;     ///< LinuxDeviceId of it
    bool left = false;
    bool taken = false;  ///< its evdev node does not open for this user (joycond holds it)
};
/// The Joy-Cons under @p sysRoot (hid-nintendo's input devices 057E:2006 / 2007 / 200E, not their IMU nodes; a
/// grip's half by its stick), their event node under @p devRoot checked with @p readable
std::vector<LinuxJoyConHalf> FindJoyConHalves(const std::string& sysRoot, const std::string& devRoot,
                                              const std::function<bool(const std::string& node)>& readable);
/// joycond's combined device: the pair id (JoyConPairing::PairId) of the one taken left and the one taken right
/// Joy-Con; "" when that is not one each (no pair, or several, which joycond's device does not tell apart)
std::string JoycondPairId(const std::vector<LinuxJoyConHalf>& halves);
/// endregion </Identity>

/// region <hid-steam>
/// A Steam Controller / Steam Deck pad hid-steam has taken its input device away from: while the Steam client has
/// the pad's hidraw node open (hid-steam's layered "client" HID device), the driver unregisters the evdev pad so a
/// press does not arrive twice (hid-steam.c, steam_work_connect_cb), and only Steam's own virtual pads (uinput
/// 28DE:11FF, which this backend reads like any pad) carry it
struct LinuxSteamHeld
{
    std::string id;    ///< LinuxDeviceId of the HID device (no evdev node to take one from)
    uint16_t product = 0;
    std::string name;  ///< the HID device's (HID_NAME)
};
/// hid-steam's game pad interfaces under @p sysRoot (`bus/hid/drivers/hid-steam/<bus>:<vid>:<pid>.<n>`: no hidraw
/// node of their own, as hid-steam connects the real interface without one; the client devices and the lizard-mode
/// keyboard and mouse interfaces have one) that have no input device now. A wireless receiver's empty slots look the
/// same: one entry per receiver
std::vector<LinuxSteamHeld> FindSteamHeldPads(const std::string& sysRoot);
/// The Steam client runs: a process under @p procRoot whose `comm` is `steam`
bool SteamClientRunning(const std::string& procRoot);
/// What the dialog says under such a pad
std::string SteamHeldNotice();
/// endregion </hid-steam>

/// region <hidraw-only devices>
/// A HID device the backend reads through hidraw alone, as no kernel driver gives it an input device (the GameCube
/// controller adapter 057E:0337: its reports are on the Game Controls page, which hid-input leaves unmapped)
struct LinuxHidrawDevice
{
    std::string id;    ///< LinuxDeviceId of the HID device
    std::string node;  ///< `<devRoot>/hidrawN`
    std::string name;  ///< HID_NAME
};
/// The hidraw nodes under @p sysRoot (`class/hidraw/hidrawN/device/uevent`) of @p vendor : @p product, in node order;
/// the node path under @p devRoot
std::vector<LinuxHidrawDevice> FindHidrawDevices(const std::string& sysRoot, const std::string& devRoot,
                                                 uint16_t vendor, uint16_t product);
/// endregion </hidraw-only devices>

}  // namespace controllers
