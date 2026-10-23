#pragma once

/// @file devicestatus.h
/// @brief The pure parts of the backends' battery and light (architecture.md §10), compiled on every OS so their
/// tests run everywhere: GameController's and XInput's battery values as a BatteryState, the Linux power supply class
/// of a HID device (`<hid>/power_supply/<name>/{type,scope,status,capacity,capacity_level}`, as hid-playstation,
/// hid-sony, hid-nintendo and xpadneo register it) and its LED class (`<hid>/leds/<name>`: one multicolor LED with
/// `multi_index` / `multi_intensity`, hid-playstation's DualSense, or one LED per channel `<name>:red|green|blue`,
/// hid-sony's DualShock 4), and the sysfs writes that set a colour there.

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "hostbackend.h"

namespace controllers
{

/// GCDeviceBattery: @p level 0..1 and @p state (GCDeviceBatteryState: -1 unknown, 0 discharging, 1 charging, 2 full).
/// An unknown state with level 0 is unknown (an Xbox Elite 2 says nothing through GameController); full is on the
/// cable
BatteryState BatteryFromGameController(float level, int state);

/// XINPUT_BATTERY_INFORMATION: @p type (0 disconnected, 1 wired, 2 alkaline, 3 NiMH, 0xFF unknown) and @p level
/// (0 empty, 1 low, 2 medium, 3 full: the coarse steps 0, 33, 67, 100 %)
BatteryState BatteryFromXInput(uint8_t type, uint8_t level);

/// A power supply's `status` (Charging, Discharging, Full, Not charging, Unknown), `capacity` (0..100) and
/// `capacity_level` (Critical, Low, Normal, High, Full, Unknown: hid-nintendo's only one, taken as the coarse steps
/// 5, 20, 50, 80, 100 % where no capacity is given). Empty text: the file is absent. Trailing newlines are ignored
BatteryState BatteryFromPowerSupply(const std::string& status, const std::string& capacity,
                                    const std::string& capacityLevel);

/// The HID device's sysfs directory of evdev node @p eventName ("event7") under @p sysRoot ("/sys"):
/// `class/input/eventN/device/device`, resolved; "" when there is none
std::string HidDeviceDir(const std::string& sysRoot, const std::string& eventName);

/// The battery power supply under HID device @p hidDir (`power_supply/<name>`, type Battery, the device's scope
/// first); "" when the driver registers none
std::string FindPowerSupply(const std::string& hidDir);
/// Read a power supply directory (BatteryFromPowerSupply over its files); unknown when it is gone. May wait for
/// the device (hid-input asks a feature report): call it off the threads that must not wait
BatteryState ReadPowerSupply(const std::string& dir);

/// A HID device's light in the LED class
struct SysfsLight
{
    enum class Kind : uint8_t
    {
        None,
        Multicolor,  ///< one LED: `multi_index` names the channels, `multi_intensity` takes their values
        Channels,    ///< one LED per colour: `<name>:red`, `<name>:green`, `<name>:blue`
    };
    Kind kind = Kind::None;
    std::string multicolor;                ///< Multicolor: its directory
    std::vector<std::string> index;        ///< Multicolor: `multi_index`, in order ("red green blue")
    int maxBrightness = 255;               ///< Multicolor: its `max_brightness`
    std::array<std::string, 3> channels;   ///< Channels: the red, green and blue LEDs' directories
    std::array<int, 3> channelMax{{255, 255, 255}};

    /// The files a colour is written to: their write access decides whether the light can be set
    std::vector<std::string> Files() const;
};

/// The light under HID device @p hidDir (`leds/`): a multicolor LED with red, green and blue channels first, else a
/// red / green / blue triple of one prefix. Player and indicator LEDs of one colour are not a light
SysfsLight FindLight(const std::string& hidDir);

/// The writes that set @p color, in order: (file, text). Multicolor: `multi_intensity` (each channel scaled to
/// max_brightness, a channel other than red / green / blue at 0), then `brightness` (max, or 0 for off); Channels:
/// each LED's `brightness`
std::vector<std::pair<std::string, std::string>> LightWrites(const SysfsLight& light, const LightColor& color);

/// Why a Linux light cannot be set: none found, or @p file is not writable for this user (the udev rule)
std::string SysfsLightUnavailable(const SysfsLight& light, const std::string& file);

}  // namespace controllers
