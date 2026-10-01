#pragma once

#include <cstdint>
#include <functional>

/// @file z84sio.h
/// @brief The Z84C15's two-channel SIO, asynchronous mode (Sprinter
/// tdd-accel-sound-input §5; Zilog Z80 SIO / Z84C15 data sheets).
///
/// Ports (any high byte): #18 A data, #19 A control, #1A B data, #1B B control.
/// v1 scope: WR0-WR7 stored through the WR0 register pointer; RR0 (bit 0 a
/// received character is waiting, bit 2 transmit buffer empty), RR1 (bit 0 all
/// sent, bit 5 receive overrun), RR2 (channel B: the interrupt vector); a
/// 3-byte receive FIFO per channel filled by the host side (Receive); transmit
/// goes to a sink at once (keyboard commands, mouse power). Interrupts are not
/// raised yet: BIOS 3.04 keeps WR1 = 0 and polls RR0 (SETUP KeyboardInit #A373,
/// KeyboardInterrupt #9EF0); the daisy chain comes with the keyboard in phase S4.
///
/// Worked example (SETUP KeyboardInit): OUT (#19),#00 / #01 selects WR1 and
/// writes #00 to it (no interrupts); #03 / #C1 sets WR3 = #C1 (8 bits, receiver
/// on); #04 / #07, #05 / #62. A scan code #1C pushed with Receive(0, #1C) makes
/// IN A,(#19) return bit 0 = 1, IN A,(#18) return #1C and bit 0 = 0 again.
class Z84Sio
{
public:
    static constexpr uint8_t kFifoDepth = 3;

    struct Channel
    {
        uint8_t wr[8] = {};
        uint8_t pointer = 0;     ///< register for the next control access (WR0 bits 2-0)
        uint8_t fifo[kFifoDepth] = {};
        uint8_t fifoCount = 0;
        uint8_t lastData = 0xFF; ///< what a read of an empty FIFO returns
        uint8_t overrun = 0;
    };

    void Reset();

    /// A host-side byte arrives on channel `ch` (0 = A, 1 = B). Returns false on overrun
    bool Receive(uint8_t ch, uint8_t value);

    uint8_t ReadData(uint8_t ch);
    void WriteData(uint8_t ch, uint8_t value);
    uint8_t ReadControl(uint8_t ch);
    void WriteControl(uint8_t ch, uint8_t value);

    /// Port access by the low address byte #18-#1B
    uint8_t Read(uint8_t port);
    void Write(uint8_t port, uint8_t value);

    const Channel& GetChannel(uint8_t ch) const { return _ch[ch & 1]; }

    /// Bytes the guest transmits (channel, byte)
    void SetTransmitSink(std::function<void(uint8_t, uint8_t)> sink) { _transmit = std::move(sink); }

private:
    void ResetChannel(uint8_t ch);

    Channel _ch[2];
    std::function<void(uint8_t, uint8_t)> _transmit;
};
