#pragma once

/// @file zcontrollerspi.h
/// @brief Z-Controller SD interface: the Z80 side of an SPI bus with one SD
/// card, as on the ZX-Evo (BaseConf and TS-Conf) and the original
/// Z-Controller. Shared by every machine with one (tdd-storage-sd-ide-cd.md §1 S4).
///
/// Two registers (the owner decodes the ports, which differ per board):
///
///   config (#77)  write: D1 = /CS (1 = card deselected); other bits unused
///   data (#57)    write: sends the byte to the card
///                 read:  returns the byte received by the PREVIOUS exchange
///                        and starts a new exchange sending #FF
///
/// So a driver reads a response with `IN` twice: the first `IN` clocks the
/// byte in, the second returns it (the BaseConf FPGA latches the result at
/// the start of the next exchange, zports.v:833-836). An exchange completes
/// before the Z80 can issue the next port access, so every byte is instant.
///
/// Reset: card deselected, receive latch #FF (spihub.v: sdcs_n resets to 1).

#include <cstdint>
#include <type_traits>

class SpiDevice;

class ZControllerSpi
{
public:
    /// Everything the controller remembers: plain bytes for snapshots / TTD
    struct State
    {
        uint8_t csN = 1;        // 1 = card deselected
        uint8_t rxLatch = 0xFF;  // byte received by the last exchange
    };
    static_assert(std::is_trivially_copyable_v<State> && sizeof(State) == 2);

    ZControllerSpi() = default;

    /// The card on the bus (nullptr: empty slot, the line reads #FF)
    void SetDevice(SpiDevice* device);
    SpiDevice* GetDevice() const { return _device; }

    void Reset();

    void WriteConfig(uint8_t value);
    void WriteData(uint8_t value);
    uint8_t ReadData();

    bool IsSelected() const { return _state.csN == 0; }

    const State& GetState() const { return _state; }
    /// Restore a saved state; the card is told about the chip select again
    void SetState(const State& state);

private:
    uint8_t Exchange(uint8_t mosi);

    SpiDevice* _device = nullptr;
    State _state;
};
