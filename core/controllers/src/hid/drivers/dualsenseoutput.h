#pragma once

/// @file dualsenseoutput.h
/// @brief The DualSense output report (haptics.md §9, H4), laid out as Linux's hid-playstation.c sends it (struct
/// dualsense_output_report_common / _usb / _bt, dualsense_output_worker): the compatible-vibration motors, the
/// adaptive triggers, the light bar and the player LEDs; USB report 0x02 and Bluetooth report 0x31 (sequence, tag 0x10,
/// CRC-32). Over Bluetooth the pad answers with its full input report 0x31, which SonyPad reads.

#include <cstdint>
#include <optional>
#include <vector>

#include "hostbackend.h"

namespace controllers
{

/// What one output report sets; what is absent keeps its state on the pad (its valid flags stay clear)
struct DualSenseOutputState
{
    std::optional<RumbleLevels> rumble;  ///< the motors and the trigger effects
    std::optional<LightColor> light;
    /// Release the firmware's own light bar first (light bar setup "light out"), once before the first colour
    bool releaseLight = false;
    std::optional<uint8_t> playerLeds;  ///< the five player LEDs, bit 0 the leftmost
};

class DualSenseOutput
{
public:
    /// DualSense and DualSense Edge
    static bool Handles(uint16_t vendor, uint16_t product);

    /// USB report 0x02 (48 bytes): low -> the left (heavy) motor, high -> the right one; a trigger level above zero is
    /// a continuous resistance of that force from the start of the pull, zero turns the trigger effect off
    static std::vector<uint8_t> UsbReport(const RumbleLevels& levels);
    /// USB report 0x02 that sets only the light bar (valid flag 1 bit 2, red / green / blue at 45-47 of the report): the
    /// motors and triggers are left as they are. @p release adds the light bar setup "light out" (valid flag 2 bit 1)
    /// that ends the firmware's own blue light, as hid-playstation and SDL send it once before the first colour
    static std::vector<uint8_t> UsbLightReport(const LightColor& color, bool release);
    /// Bluetooth report 0x31 (78 bytes): the sequence in the high nibble of byte 1, tag 0x10 at 2, the common part
    /// from 3, the CRC-32 of 0xA2 + bytes 0..73 at 74..77 (little endian)
    static std::vector<uint8_t> BluetoothReport(const RumbleLevels& levels, uint8_t sequence);
    /// Any mix of rumble, light and player LEDs, for USB (48 bytes, as UsbReport) or Bluetooth (78, as
    /// BluetoothReport)
    static std::vector<uint8_t> Report(const DualSenseOutputState& state, bool bluetooth, uint8_t sequence);
    /// hid-playstation's player_ids: player 1 the middle LED, 2 the two inner, 3 the middle three, 4 the four outer,
    /// 5 all five; @p player counts from 1 and wraps after 5; 0 none
    static uint8_t PlayerLeds(int player);

    static uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0);

private:
    /// The 47-byte common part: valid flags, motors, the trigger effects at 10 (right) and 21 (left)
    static void Common(const RumbleLevels& levels, uint8_t* common);
};

}  // namespace controllers
