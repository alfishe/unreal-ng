#pragma once

/// @file spidevice.h
/// @brief A device on an SPI bus, as seen from the master: chip select and a
/// full-duplex byte exchange. Used by the NeoGS SPI masters (SD card, MP3
/// decoder control and data) and meant for any other SD/SPI host
/// (neogs-tdd.md §5.4).

#include <cstdint>

class SpiDevice
{
public:
    virtual ~SpiDevice() = default;

    /// Chip select changed (true = selected). Devices with no chip-select pin
    /// (the VS10xx data port) never see this.
    virtual void select(bool selected) = 0;

    /// One byte clocked out (MOSI) while one is clocked in (the return, MISO).
    /// An idle or absent device returns #FF (the line is pulled up).
    virtual uint8_t exchange(uint8_t mosi) = 0;

    /// A byte in flight was cut off by the master (it restarted before the
    /// eighth bit). The device never received a whole byte.
    virtual void truncatedByte() {}
};
