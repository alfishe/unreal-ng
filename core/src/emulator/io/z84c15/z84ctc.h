#pragma once

#include <cstdint>
#include <functional>

/// @file z84ctc.h
/// @brief The Z84C15's four-channel CTC (Sprinter tdd-accel-sound-input §5; Zilog Z80 CTC data sheet).
///
/// Ports #10-#13 (any high byte), one per channel. A write with bit 0 = 1 is a
/// control word (bit 7 interrupt enable, 6 counter mode, 5 prescaler 256 / 16,
/// 2 a time constant follows, 1 software reset); with bit 0 = 0 to channel 0 it
/// is the interrupt vector (bits 7-3). The time constant (0 = 256) loads the
/// down-counter; a read returns the current count.
///
/// Timer mode counts the system clock through the prescaler. The clock comes
/// from the owner (SetClock, in CPU clocks); the count is derived when read, so
/// a channel costs nothing per instruction. Counter mode (CLK/TRG inputs) holds
/// its count: no input is wired in v1. Zero-count outputs and interrupts are
/// not raised yet (BIOS 3.04 programs the CTC for the serial mouse only; the
/// daisy chain comes with the mouse in phase S4).
///
/// Worked example: control #25 (timer, prescaler 256, time constant follows),
/// then #0A: the channel counts 10, 9, ... 1, 10 ..., one step per 256 clocks;
/// 512 clocks after the load a read returns 8.
class Z84Ctc
{
public:
    struct ChannelState
    {
        uint8_t control = 0x03;      ///< last control word (power-on: reset)
        uint8_t timeConstant = 0;    ///< 0 = 256
        uint8_t awaitingConstant = 0;
        uint8_t running = 0;
        uint64_t loadClock = 0;      ///< clock of the load (timer mode)
    };

    void Reset();

    uint8_t Read(uint8_t channel);
    void Write(uint8_t channel, uint8_t value);

    uint8_t Vector() const { return _vector; }
    const ChannelState& GetChannel(uint8_t channel) const { return _ch[channel & 3]; }

    /// The system clock in CPU clocks (monotonic)
    void SetClock(std::function<uint64_t()> clock) { _clock = std::move(clock); }

private:
    uint64_t Now() const { return _clock ? _clock() : 0; }

    ChannelState _ch[4];
    uint8_t _vector = 0;
    std::function<uint64_t()> _clock;
};
