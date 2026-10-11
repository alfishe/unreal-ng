#pragma once

/// @file nextctc.h
/// @brief The Next's Z80-CTC: four channels at ports #183B-#1B3B (A10:A8 = channel), clocked by the 28 MHz system
/// clock through the 16 / 256 prescaler, ZC/TO of channel n chained to the trigger of channel n + 1 and channel 3's
/// to channel 0's (research-fpga-vhdl.md section 19). Interrupts go to the IM2 controller (NextInterruptSource); the
/// vector register is kept but the controller supplies the vector (NR #C0).

#include <cstdint>
#include <functional>

class NextCtc
{
public:
    static constexpr unsigned kChannels = 4;
    static constexpr uint8_t kPortLow = 0x3B;

    /// Control word bits
    static constexpr uint8_t kInterrupt = 0x80;
    static constexpr uint8_t kCounterMode = 0x40;
    static constexpr uint8_t kPrescale256 = 0x20;
    static constexpr uint8_t kRisingEdge = 0x10;
    static constexpr uint8_t kTriggerClk = 0x08;
    static constexpr uint8_t kTimeConstantFollows = 0x04;
    static constexpr uint8_t kSoftReset = 0x02;
    static constexpr uint8_t kControl = 0x01;

    /// A channel reached zero with its interrupt enabled
    std::function<void(unsigned channel)> onInterrupt;

    void Reset();
    /// Bring the counters up to the 28 MHz system clock value `now`
    void Advance(uint64_t now);
    void Write(unsigned channel, uint8_t value, uint64_t now);
    uint8_t Read(unsigned channel, uint64_t now);
    /// An external pulse on a channel's CLK/TRG (the chain, or a test)
    void Trigger(unsigned channel, uint64_t now);

    /// The channels' interrupt enables (control bit 7), one bit a channel: NR #C5 reads and writes them (zxnext.vhd ctc_int_en)
    uint8_t InterruptEnables() const
    {
        uint8_t bits = 0;
        for (unsigned i = 0; i < kChannels; i++)
            bits |= static_cast<uint8_t>(((_ch[i].control & kInterrupt) ? 1 : 0) << i);
        return bits;
    }
    void SetInterruptEnables(uint8_t bits)
    {
        for (unsigned i = 0; i < kChannels; i++)
            _ch[i].control = static_cast<uint8_t>((_ch[i].control & ~kInterrupt) | (((bits >> i) & 1) ? kInterrupt : 0));
    }
    uint8_t Control(unsigned channel) const { return _ch[channel].control; }
    uint16_t TimeConstant(unsigned channel) const { return _ch[channel].tc; }
    uint32_t Zeros(unsigned channel) const { return _ch[channel].zeros; }
    bool Running(unsigned channel) const { return _ch[channel].running; }

private:
    struct Channel
    {
        uint8_t control = 0;
        uint16_t tc = 256;
        uint16_t counter = 256;
        bool waitTc = false;
        bool running = false;
        bool waitTrigger = false;
        uint64_t phase = 0;  ///< system clock of the prescaler's last wrap
        uint32_t zeros = 0;
    };

    uint32_t Prescale(const Channel& c) const { return (c.control & kPrescale256) ? 256 : 16; }
    void ZeroCount(unsigned channel, uint64_t when);
    uint64_t NextZero(const Channel& c) const;

    Channel _ch[kChannels];
    uint8_t _vector = 0;
    uint64_t _now = 0;
};
