#pragma once

/// @file nextctc.h
/// @brief The Next's Z80-CTC (device/ctc.vhd, ctc_chan.vhd): four channels at ports #183B-#1B3B (A10:A8 = channel; 4-7
/// select nothing and read 0), clocked by the 28 MHz system clock through the 16 / 256 prescaler whatever the CPU speed,
/// ZC/TO of channel n chained to the trigger of channel n + 1 and channel 3's to channel 0's (zxnext.vhd i_clk_trg).
/// Interrupts go to the IM2 controller (NextInterruptSource); the vector register is kept but the controller supplies
/// the vector (NR #C0).
///
/// Time: the prescaler is a free-running 8-bit counter that is held at 0 while the channel is not in its run state, so
/// the ticks of a channel are at `runStart + k * prescale` clocks (16 or 256, both on the same grid). The ZC/TO pulse
/// reaches the next channel's edge detector 1 clock later (rising edge selected in its control word) or 2 (falling).

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
    /// A channel's ZC/TO pulse, whatever its interrupt enable (the joystick connector's pin 7 follows channel 3's)
    std::function<void(unsigned channel)> onZeroCount;

    void Reset();
    /// Bring the counters up to the 28 MHz system clock value `now`
    void Advance(uint64_t now);
    void Write(unsigned channel, uint8_t value, uint64_t now);
    uint8_t Read(unsigned channel, uint64_t now);
    /// An external clock edge on a channel's CLK/TRG (a test; the chain goes through the pending list)
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
    /// Tests: off = every Advance walks the channels (the reference the gated one is compared with)
    void SetEventGate(bool on)
    {
        _eventGate = on;
        _quietUntil = 0;
    }
    bool Running(unsigned channel) const { return _ch[channel].running; }

private:
    friend struct NextStateCodec;  // the TTD state codec (nextstatecodec.cpp)
    struct Channel
    {
        uint8_t control = 0;
        uint16_t tc = 256;
        uint16_t counter = 256;
        bool waitTc = false;
        bool running = false;
        bool waitTrigger = false;
        uint64_t runStart = 0;  ///< system clock at which the prescaler counted 0 (the channel entered its run state)
        uint64_t phase = 0;     ///< system clock up to which `counter` is valid (the last count or reload)
        uint32_t zeros = 0;
        /// ZC/TO pulses of the previous channel on their way to this channel's edge detector (clock of the count), oldest first
        uint64_t pending[4] = {};
        uint8_t pendingCount = 0;
    };

    uint32_t Prescale(const Channel& c) const { return (c.control & kPrescale256) ? 256 : 16; }
    void ZeroCount(unsigned channel, uint64_t when);
    /// One count of a counter-mode channel at clock `when` (an edge reached its detector)
    void CountEdge(unsigned channel, uint64_t when);
    void StartTimer(Channel& c, uint64_t now);
    /// Prescaler ticks of a running timer in the clocks (from, to]
    uint64_t Ticks(const Channel& c, uint64_t from, uint64_t to) const;
    /// The count of a running timer at clock `now` (counter is valid at `phase`)
    uint16_t CountAt(const Channel& c, uint64_t now) const;
    uint64_t NextZero(const Channel& c) const;

    Channel _ch[kChannels];
    uint8_t _vector = 0;
    uint64_t _now = 0;
    /// No zero count and no ZC/TO pulse is due before this clock (derived from the channels, not state): Advance returns at once
    /// below it. 0: not known, the next Advance works it out. Everything that changes a channel clears it
    uint64_t _quietUntil = 0;
    bool _eventGate = true;
};
