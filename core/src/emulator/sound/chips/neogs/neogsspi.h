#pragma once

/// @file neogsspi.h
/// @brief The NeoGS FPGA's three SPI masters and their control/status ports
/// (neogs-tdd.md §3.7; FPGA common/spi.v, ports.v:577-623).
///
///  - SD: SD card, always SCK = card clock / 2
///  - MC: MP3 decoder control (SCI), divider /2../16 from SCTRL MCSPD
///  - MD: MP3 decoder data (SDI), /2, or /4 with SCTRL MDHLF; no chip select
///
/// Timing (card CPU clocks): a byte takes 16 clocks at /2 and 8 x div + 2 at
/// /4, /8, /16. A read before the byte completes returns the previous byte; an
/// access exactly at the completion time sees the new one (the loader's
/// OUT/NOP/OUT pairs are 16 T apart). A start while a byte is in flight
/// restarts the master: the old byte never reaches the device.
///
/// Devices see their exchange when the byte completes. Completion is applied
/// lazily - at the next access to the SPI ports or the next DMA step - which
/// is exact because nothing else observes the device between the two.

#include <cstdint>

class SpiDevice;

class NeoGSSpi
{
public:
    enum Master : uint8_t
    {
        SD = 0,
        MC = 1,
        MD = 2,
    };

    // SCTRL bits (ports.inc)
    static constexpr uint8_t SCTRL_SD_NCS = 0x01;
    static constexpr uint8_t SCTRL_MC_NCS = 0x02;
    static constexpr uint8_t SCTRL_MC_XRESET = 0x04;
    static constexpr uint8_t SCTRL_MCSPD0 = 0x08;
    static constexpr uint8_t SCTRL_MDHLF = 0x10;
    static constexpr uint8_t SCTRL_MCSPD1 = 0x20;
    static constexpr uint8_t SCTRL_RESET = 0x0B; // SD and MC deselected, XRESET low, MCSPD = 01

    struct MasterState
    {
        int64_t end = 0;       // completion time (card units); <= now: idle
        uint8_t tx = 0xFF;     // byte in flight
        uint8_t rx = 0xFF;     // last received byte (SD_READ / MC_READ)
        bool pending = false;  // byte in flight not yet delivered
    };

    NeoGSSpi();

    void attach(Master master, SpiDevice* device) { _device[master] = device; }
    SpiDevice* device(Master master) const { return _device[master]; }

    /// FPGA reset: SCTRL back to #0B (chip selects follow), masters keep any
    /// byte in flight (spi.v has no reset)
    void reset();

    /// Deliver every byte whose time has come
    void sync(int64_t now);

    // SCTRL (#11): every bit set in d5..d0 takes the value of d7
    void writeSctrl(uint8_t value, int64_t now);
    uint8_t readSctrl() const { return static_cast<uint8_t>(_sctrl & 0x3F); }

    /// Start a byte on a master. `unitsPerCycle` is the card clock at the start.
    void start(Master master, uint8_t tx, int64_t now, int64_t unitsPerCycle);

    /// Last received byte (after sync)
    uint8_t received(Master master) const { return _m[master].rx; }
    bool busy(Master master, int64_t now) const { return _m[master].end > now; }
    int64_t completion(Master master) const { return _m[master].end; }

    /// Byte time of a master in card clocks at the current SCTRL setting
    int byteClocks(Master master) const;

    const MasterState& state(Master master) const { return _m[master]; }
    MasterState& state(Master master) { return _m[master]; }
    uint8_t sctrlRaw() const { return _sctrl; }
    void setSctrlRaw(uint8_t value) { _sctrl = value; }

private:
    void applySelects(uint8_t oldSctrl);
    void deliver(Master master);

    SpiDevice* _device[3] = {nullptr, nullptr, nullptr};
    MasterState _m[3];
    uint8_t _sctrl = SCTRL_RESET;
};
