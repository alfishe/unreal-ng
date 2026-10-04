#pragma once

#include <cstdint>

/// What a board applies to the I/O port pins of an AY-3-8910 / YM2149 (and the YM2203 SSG) from outside the chip.
///
/// The chip side (SoundChip_AY8910::readRegisterOnBus) owns the electrical rule; the board side only says which
/// pins it holds low. A machine that wires something to the port (the Sinclair 128K family: keypad and RS-232 /
/// MIDI lines on port A) implements this interface and attaches it to its AY; every other machine attaches
/// nothing and the read path stays as it is (docs/inprogress/2026-10-04-ay-reset/TODO.md, item 4).
///
/// This is the input direction only. The output direction (a board listening to the pins the chip drives, e.g.
/// a MIDI line) is a separate listener interface; both can sit on the same chip.
class IAyIoPortInput
{
public:
    virtual ~IAyIoPortInput() = default;

    /// The levels the board presents to the pins of `port` (0 = port A / R14, 1 = port B / R15): a 1 bit leaves the
    /// pin to the chip (its pull-up while an input, its latch while an output), a 0 bit holds the pin low.
    /// Called only on a bus read of R14 / R15 (IN #FFFD, the YM2203 data read), never per sample or per write
    virtual uint8_t AyIoPortBoardLevels(int port) const = 0;
};
