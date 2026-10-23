#pragma once

/// @file windowsdevices.h
/// @brief The decisions of the Windows backends (architecture.md §4, §5) without the Windows API, so their tests run on
/// every OS: what a HID device path says (bus, VID / PID, an XInput-class endpoint), which reader serves a HID top-level
/// collection (Raw Input pads, the Surface Dial through HidD / ReadFile, the Xbox pads left to GameInput or XInput),
/// the device id, a HID field located from the bits HidP_Set* wrote (the descriptor Windows does not hand out), the
/// report buffer HidD_Set* takes, and GameInput's gamepad state as canonical controls.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "hid/hiddescriptor.h"
#include "hid/hidpad.h"

namespace controllers
{

/// What a Windows HID device interface path tells (`\\?\HID#VID_045E&PID_0B12&IG_00#...`,
/// `\\?\HID#{00001124-0000-1000-8000-00805f9b34fb}_VID&0002054c_PID&0ce6#...`, BLE `{00001812-...}_Dev_VID&02045e_...`)
struct WinHidPath
{
    std::string bus;  ///< "usb", "bt", "" when the path does not say
    uint16_t vendor = 0;
    uint16_t product = 0;
    bool haveIds = false;
    /// `&IG_` in the path: the HID endpoint of an XInput-class pad (an Xbox pad, xinputhid over Bluetooth), served by
    /// GameInput or XInput; read through HID it is the "fake" endpoint (both triggers on one axis, no rumble)
    bool xinput = false;
};

WinHidPath ParseWinHidPath(const std::string& path);

/// Which Windows reader takes a HID top-level collection
enum class WinHidRole : uint8_t
{
    Skip,  ///< not a pad, or a pad another backend serves (one device once, architecture.md §4 rule 2)
    Pad,   ///< Raw Input, decoded by HidPad
    Dial,  ///< a System Multi-Axis Controller (0x01:0x0E): HidD_* and overlapped ReadFile, decoded by SurfaceDial
};

/// @p usagePage / @p usage of the top-level collection and its interface @p path. Joysticks, game pads and multi-axis
/// controllers are pads unless XInput-class (GameInput or XInput has them)
WinHidRole WinRawInputRole(uint16_t usagePage, uint16_t usage, const std::string& path);

/// architecture.md §5, as the IOHID backend makes it: usb:<vid>:<pid>:<version>:<serial>, usb:<vid>:<pid>:<version>@
/// <path hash> without a serial, bt:<vid>:<pid>:<hash of the serial or the path>; `hid:` instead of `usb:` when the
/// path names no bus (I2C, a virtual device). The path's case is folded first (Windows hands out the same path in
/// either case)
std::string WinDeviceId(const std::string& bus, uint16_t vendor, uint16_t product, uint16_t version,
                        const std::string& serial, const std::string& path);

/// One field located by its bits: @p cleared and @p set are the same report with the field all zeros and all ones
/// (HidP_SetUsageValue / HidP_SetUsages on a report HidP_InitializeReportForID made). Byte 0 is the report id; the
/// offset counts from byte 1 (HidField::bitOffset). False when no bit or a gap differs, or more than 32 bits
bool LocateHidBits(const uint8_t* cleared, const uint8_t* set, size_t size, uint32_t& bitOffset, uint8_t& bitSize);

/// The logical maximum as the descriptor meant it: Windows sign-extends an unsigned maximum (0..255 in one byte reads
/// 0..-1), the descriptor parser reads it unsigned when the minimum is not negative
int32_t WinLogicalMax(int32_t logicalMin, int32_t logicalMax, uint8_t bitSize);

/// A report for HidD_SetFeature / HidD_SetOutputReport: zero-padded to the collection's report length (the largest
/// report of that kind; the driver sends the report id's own size). Never shorter than @p report
std::vector<uint8_t> PadHidReport(const std::vector<uint8_t>& report, size_t length);

/// region <GameInput>

/// GameInputDeviceFamily (GameInput.h v3): XboxOne 1 (GIP), Xbox360 2 (XUSB), Hid 3
constexpr int kGameInputFamilyXboxOne = 1;
constexpr int kGameInputFamilyXbox360 = 2;

/// GameInput serves the XInput-class pads (Xbox One / Series over GIP, Xbox 360 over XUSB): paddles, the impulse
/// triggers. A HID pad (family Hid) stays with Raw Input and the shared HID drivers, so a DualSense decodes the same on
/// every OS
bool GameInputServes(int deviceFamily);

/// GameInputGamepadState (GameInput.h v3) as plain data: the button bits and the six axes (sticks + up, -1..1;
/// triggers 0..1)
struct GameInputPadState
{
    uint32_t buttons = 0;
    float leftTrigger = 0;
    float rightTrigger = 0;
    float leftX = 0;
    float leftY = 0;
    float rightX = 0;
    float rightY = 0;
};

/// The canonical controls a GameInput gamepad reports (the paddles where the device has them)
std::vector<std::string> GameInputPadControls();

/// Every control that changed from @p was to @p now goes to @p sink; @p first (no reading before): the buttons held
/// and every axis. The sticks' y turns to the vocabulary's + down; the Elite paddles: right upper / lower P1 / P2,
/// left upper / lower P3 / P4 (Xbox Accessories, SDL)
void DiffGameInputPad(const GameInputPadState& was, const GameInputPadState& now, bool first, const ControlSink& sink);

/// endregion </GameInput>

}  // namespace controllers
