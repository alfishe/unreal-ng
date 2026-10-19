#pragma once

/// @file dualsenseoutput.h
/// @brief The DualSense output report (haptics.md §9, H4), laid out as Linux's hid-playstation.c sends it: the
/// compatible-vibration motors and the adaptive triggers. USB only for now: over Bluetooth report 0x31 switches the pad
/// to its extended input report, which the descriptor parser does not read (TODO.md H4).

#include <cstdint>
#include <vector>

#include "hostbackend.h"

namespace controllers
{

class DualSenseOutput
{
public:
    /// DualSense and DualSense Edge
    static bool Handles(uint16_t vendor, uint16_t product);

    /// USB report 0x02 (48 bytes): low -> the left (heavy) motor, high -> the right one; a trigger level above zero is
    /// a continuous resistance of that force from the start of the pull, zero turns the trigger effect off
    static std::vector<uint8_t> UsbReport(const RumbleLevels& levels);
    /// Bluetooth report 0x31 (78 bytes) with the sequence tag and the CRC32 over 0xA2 + the report; kept for the
    /// extended input parser that has to come with it
    static std::vector<uint8_t> BluetoothReport(const RumbleLevels& levels, uint8_t sequence);

    static uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0);

private:
    /// The 47-byte common part: valid flags, motors, the trigger effects at 10 (right) and 21 (left)
    static void Common(const RumbleLevels& levels, uint8_t* common);
};

}  // namespace controllers
