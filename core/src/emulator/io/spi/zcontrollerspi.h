#pragma once

/// @file zcontrollerspi.h
/// @brief Z-Controller SD interface: the Z80 side of an SPI bus with an SD
/// card, as on the ZX-Evo (BaseConf and TS-Conf) and the original
/// Z-Controller. Shared by every machine with one (tdd-storage-sd-ide-cd.md §1 S4).
///
/// Two registers (the owner decodes the ports, which differ per board):
///
///   config (#77)  write: D1 = SD /CS (1 = card deselected); further chip
///                        selects on other bits, see below
///   data (#57)    write: sends the byte to the bus
///                 read:  returns the byte received by the PREVIOUS exchange
///                        and starts a new exchange sending #FF
///
/// So a driver reads a response with `IN` twice: the first `IN` clocks the
/// byte in, the second returns it (the BaseConf FPGA latches the result at
/// the start of the next exchange, zports.v:833-836). An exchange completes
/// before the Z80 can issue the next port access, so every byte is instant.
///
/// More devices on the same bus (TS-Conf VDAC2 build: the FT812 on D2,
/// active high, zports.v:296-311): AttachDevice puts a device in slot 1..3
/// with its config bit and polarity. Every config write recomputes each
/// slot's select line. A selected device in slot 1..3 drives MISO instead of
/// the SD card, which is the FPGA's mux (top.v:1173-1178:
/// sdi = !ftcs_n ? ftdi : sddi); devices in slots 1..3 are only clocked
/// while selected (a deselected SPI slave ignores the clock anyway). Slot 0
/// keeps the single-device behavior every machine has had: its device sees
/// every byte and decides itself whether it is selected. Machines with one
/// device take the single-device path only (vdac2-integration-design.md §4).
///
/// Reset: every device deselected (config #02: SD /CS high, the active-high
/// selects low; spihub.v / zports.v: spi_cs_n resets to all ones), receive
/// latch #FF.

#include <array>
#include <cstdint>
#include <type_traits>

class SpiDevice;

class ZControllerSpi
{
public:
    static constexpr uint8_t kSlots = 4;
    static constexpr uint8_t kResetConfig = 0x02;

    /// Everything the controller remembers: plain bytes for snapshots / TTD
    struct State
    {
        uint8_t config = kResetConfig;  // last byte written to the config port
        uint8_t rxLatch = 0xFF;         // byte received by the last exchange
    };
    static_assert(std::is_trivially_copyable_v<State> && sizeof(State) == 2);

    ZControllerSpi() = default;

    /// The SD card on the bus: slot 0, D1 active low (nullptr: empty slot,
    /// the line reads #FF)
    void SetDevice(SpiDevice* device);
    SpiDevice* GetDevice() const { return _devices[0]; }

    /// A further device in slot 1..3, selected by `configMask` of the config
    /// byte, active high or low (nullptr detaches it)
    void AttachDevice(uint8_t slot, SpiDevice* device, uint8_t configMask, bool activeHigh);

    void Reset();

    void WriteConfig(uint8_t value);
    void WriteData(uint8_t value);
    uint8_t ReadData();

    /// The SD card (slot 0) is selected
    bool IsSelected() const { return IsSlotSelected(0); }
    /// Slot 0 has its select line even when empty (the SD socket); slots
    /// 1..3 exist only with a device in them
    bool IsSlotSelected(uint8_t slot) const
    {
        return slot < kSlots && (slot == 0 || _devices[slot] != nullptr) && SelectedBy(slot, _state.config);
    }

    const State& GetState() const { return _state; }
    /// Restore a saved state; every device is told about its chip select again
    void SetState(const State& state);

private:
    uint8_t Exchange(uint8_t mosi);
    bool SelectedBy(uint8_t slot, uint8_t config) const
    {
        const bool level = (config & _masks[slot]) != 0;
        return _activeHigh[slot] ? level : !level;
    }
    void AnnounceAll();

    std::array<SpiDevice*, kSlots> _devices{};
    std::array<uint8_t, kSlots> _masks{0x02, 0, 0, 0};
    std::array<bool, kSlots> _activeHigh{false, false, false, false};
    bool _multi = false;  // a device in slot 1..3
    State _state;
};
