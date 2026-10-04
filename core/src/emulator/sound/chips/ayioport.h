#pragma once

#include <cstdint>

/// I/O ports of the AY-3-8910 / YM2149 and of the YM2203 SSG: the pins as the board sees them.
///
/// Hardware (docs/inprogress/2026-10-03-zx-multisound/tdd-midi-line.md §2.0, ML-0):
///   - R7 bit 6 sets port A (R14) to output, bit 7 port B (R15); 0 = input. Identical on the
///     AY-3-8910, the YM2149 and the YM2203 SSG (register 07 "IN/OUT IOB IOA").
///   - In input mode every pin is pulled high by an on-chip pull-up (AY-3-8910: "when in the input
///     mode, all pins will read normally high"; YM2149 / YM2203: 60-600 kOhm), so an input port
///     presents #FF to the board.
///   - In output mode the pins carry the R14 / R15 latch. The output stage drives both levels
///     (YM2149 VOH >= 2.5 V at 100 uA, YM2203 VOH >= 2.4 V at 0.4 mA - more than the pull-up can
///     source), so a pin follows its latch bit without a load-dependent delay.
///   - Reset clears every register: both ports are inputs, the pins read #FF.
///
/// The interface is chip-agnostic so the YM2203 SSG (the MultiSound's Ym2203Pair, MS-1) can drive the
/// same listeners as SoundChip_AY8910.

/// Receives level changes of the I/O port pins. Called only when the pin byte of a port changes
/// (a register write that changes nothing on the pins is not reported).
class IAyIoPortListener
{
public:
    virtual ~IAyIoPortListener() = default;

    /// t: the time of the register write that changed the pins, on the owner's time axis
    /// (SoundChip_TurboSound passes its audio T-state axis). port: AyIoPort::PortA / PortB.
    /// pins: the pin levels seen outside the chip (#FF for a port in input mode)
    virtual void OnIoPortPins(uint64_t t, int port, uint8_t pins) = 0;
};

/// The pin model shared by every chip with the AY I/O port block
class AyIoPort
{
public:
    static constexpr int PortA = 0;            // R14, direction R7 bit 6
    static constexpr int PortB = 1;            // R15, direction R7 bit 7
    static constexpr int PortCount = 2;
    static constexpr uint8_t InputPins = 0xFF; // pulled up

    static constexpr uint8_t RegMixer = 7;
    static constexpr uint8_t RegPortA = 14;
    static constexpr uint8_t RegPortB = 15;

    /// R7 bit that makes `port` an output
    static constexpr uint8_t DirectionBit(int port) { return static_cast<uint8_t>(0x40u << port); }

    /// Pins of `port` for a mixer / direction register `r7` and the port's data latch
    static constexpr uint8_t Pins(uint8_t r7, int port, uint8_t latch)
    {
        return (r7 & DirectionBit(port)) ? latch : InputPins;
    }

    /// Whether a write to register `reg` can change any pin (R7, R14, R15)
    static constexpr bool AffectsPins(uint8_t reg)
    {
        return reg == RegMixer || reg == RegPortA || reg == RegPortB;
    }
};
