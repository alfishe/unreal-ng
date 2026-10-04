#pragma once

#include <cstdint>

#include "emulator/sound/chips/ayioportinput.h"

class EmulatorContext;

/// The board wiring on AY I/O port A of the Sinclair 128K family: the 128K (toastrack), the grey +2 (same board),
/// the +2A / +3 (the keypad socket renamed AUX, same bits, same line driver / receiver pair).
///
/// Hardware (docs/inprogress/2026-10-04-ay-reset/TODO.md item 4 has the source table):
///   - The AY-3-8912 bonds out port A only. Bits 0-3 feed a 1488 RS-232 line driver (128K IC33, +3 IC12): bit 0
///     keypad / AUX pin 2, bit 1 keypad / AUX pin 4, bit 2 RS-232 CTS, bit 3 RS-232 RXD (also MIDI out). A driver
///     input is a load, it does not pull its pin.
///   - Bits 4-7 come from a 1489 RS-232 line receiver (128K IC34, +3 IC13): bit 4 keypad / AUX pin 3, bit 5 keypad
///     / AUX pin 5, bit 6 RS-232 DTR (0 = the device is ready), bit 7 RS-232 TXD (data in). The receiver drives
///     its pin; with its RS-232 input open it outputs high.
///   - So with nothing plugged in every pin is high: `OUT 65533,14: PRINT IN 65533` shows 255 on a real 128K
///     (fruitcake.plus.com keypad measurements). Fuse's #BF ("always allow serial output") is an emulator
///     convenience that reports DTR as ready, not the hardware.
///   - Port B (R15) has no pins on the 8912: nothing to wire, the chip's own pull-ups decide.
///
/// The receiver outputs are the hook for devices plugged into the keypad / AUX and RS-232 sockets: such a device
/// sets the levels (SetReceiverOutputs) and a read of R14 sees them through the chip's rule (a pin held low reads
/// 0 in both directions). Nothing drives them yet.
///
/// Machine knowledge stays here: the 128K / +2 and +2A / +3 port decoders own one each and attach it to the AY in
/// the board's AY socket (Attach); no shared code tests a model id.
class Spectrum128AyIoPort final : public IAyIoPortInput
{
public:
    /// Port A bits driven by the 1489 receiver (keypad / AUX pins 3 and 5, RS-232 DTR and TXD)
    static constexpr uint8_t ReceiverBits = 0xF0;
    static constexpr uint8_t BitKeypadPin3 = 0x10;  ///< 128K keypad pin 3 (light gun sensor), +3 AUX pin 3
    static constexpr uint8_t BitKeypadPin5 = 0x20;  ///< 128K keypad pin 5 (keypad data in), +3 AUX pin 5
    static constexpr uint8_t BitRs232Dtr = 0x40;    ///< RS-232 DTR in: 0 = the device is ready
    static constexpr uint8_t BitRs232Txd = 0x80;    ///< RS-232 TXD in: the device's data

    /// What a read of R14 sees from the board with nothing plugged in: every receiver output high
    static constexpr uint8_t NothingConnected = 0xFF;

    uint8_t AyIoPortBoardLevels(int port) const override
    {
        return port == 0 ? _portA : NothingConnected;
    }

    /// The 1489 outputs (bits 4-7) as the plugged-in devices make them; bits 0-3 are not the board's to drive
    void SetReceiverOutputs(uint8_t levels)
    {
        _portA = static_cast<uint8_t>((levels & ReceiverBits) | (NothingConnected & ~ReceiverBits));
    }
    uint8_t ReceiverOutputs() const
    {
        return _portA;
    }

    /// Connect `wiring` to the AY in the board's AY socket, or disconnect it (null). The socket chip is chip 0 of
    /// the TurboSound slot: a plain AY, the first AY of a TurboSound board or the first YM2203 of a TurboSound FM
    /// board (both NedoPC boards take IOA / IOB of the socket to their first chip; the second chip's pins stay
    /// unconnected). An empty slot has nothing to wire
    static void Attach(EmulatorContext* context, const IAyIoPortInput* wiring);

private:
    uint8_t _portA = NothingConnected;
};
