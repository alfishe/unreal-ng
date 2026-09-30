#pragma once

/// @file neogsinterrupts.h
/// @brief NeoGS interrupt controller and timer (neogs-tdd.md §3.5; FPGA
/// interrupts/interrupts.v, interrupts/timer.v).
///
/// Three requests: bit 0 timer, bit 1 SD DMA done, bit 2 MP3 DMA done. Each is
/// masked by INTENA; /INT is a level held while any enabled request is
/// pending. Priority timer > SD > MP3; the acknowledge clears the request it
/// served. Vector {11, 1, ~p2, ~p1, 111}: timer #FF, SD #F7, MP3 #EF.
/// Writes to INTENA (#0C) and INTREQ (#0D): every bit set in d2..d0 takes the
/// value of d7. INTREQ reads {00000, req}.
///
/// The timer counts the 24 MHz crystal: /5, then a 17-bit counter. TIM_FREQ
/// selects which counter bit's falling edge is a tick (bit 6 = 37,500 Hz, then
/// bits 7, 8, 9, 10, 12, 14, 16). The counters are free-running from power-on:
/// neither a card reset nor a TIM_FREQ write clears them. Switching TIM_FREQ
/// from a bit that is 1 to one that is 0 is seen as a falling edge - one extra
/// tick.

#include <cstdint>

class NeoGSInterrupts
{
public:
    static constexpr uint8_t REQ_TIMER = 0x01;
    static constexpr uint8_t REQ_SD_DMA = 0x02;
    static constexpr uint8_t REQ_MP3_DMA = 0x04;

    /// Crystal clocks (24 MHz) per timer counter step
    static constexpr int64_t CRYSTAL_PER_COUNT = 5;

    /// FPGA reset: INTENA = 001, INTREQ = 000, TIM_FREQ = 0
    void reset()
    {
        _ena = 0x01;
        _req = 0x00;
        _timFreq = 0;
    }

    bool intLine() const { return (_req & _ena) != 0; }

    /// Vector byte put on the bus during the acknowledge
    uint8_t vector() const
    {
        const uint8_t p = priority();
        // {1,1,1, ~p2, ~p1, 1,1,1}
        return static_cast<uint8_t>(0xE7 | ((p & REQ_MP3_DMA) ? 0 : 0x10) | ((p & REQ_SD_DMA) ? 0 : 0x08));
    }

    /// The acknowledge cycle ended: clear the request that was served
    void acknowledge() { _req = static_cast<uint8_t>(_req & ~priority()); }

    /// A hardware request (timer tick, DMA done)
    void raise(uint8_t bit) { _req |= bit; }

    void writeEnable(uint8_t value) { _ena = apply(_ena, value); }
    void writeRequest(uint8_t value) { _req = apply(_req, value); }
    uint8_t readRequest() const { return static_cast<uint8_t>(_req & 0x07); }
    uint8_t enable() const { return _ena; }

    uint8_t timFreq() const { return _timFreq; }
    void setTimFreqRaw(uint8_t rate) { _timFreq = static_cast<uint8_t>(rate & 7); }
    void setRaw(uint8_t ena, uint8_t req)
    {
        _ena = static_cast<uint8_t>(ena & 7);
        _req = static_cast<uint8_t>(req & 7);
    }

    /// Counter bit TIM_FREQ selects
    static int timerBit(uint8_t rate)
    {
        static constexpr int bits[8] = {6, 7, 8, 9, 10, 12, 14, 16};
        return bits[rate & 7];
    }

    /// Crystal clocks between two ticks at `rate` (640 at rate 0)
    static int64_t tickPeriodCrystal(uint8_t rate) { return CRYSTAL_PER_COUNT << (timerBit(rate) + 1); }

    /// Value of the selected counter bit at crystal phase `crystal`
    static bool selectedBit(uint8_t rate, int64_t crystal)
    {
        const int64_t count = crystal / CRYSTAL_PER_COUNT;
        return ((count >> timerBit(rate)) & 1) != 0;
    }

    /// First tick strictly after crystal phase `crystal`
    static int64_t nextTickCrystal(uint8_t rate, int64_t crystal)
    {
        const int64_t period = tickPeriodCrystal(rate);
        return (crystal / period + 1) * period;
    }

    /// TIM_FREQ write at crystal phase `crystal`: returns true when the switch
    /// itself produces a tick (old selected bit 1, new one 0)
    bool writeTimFreq(uint8_t rate, int64_t crystal)
    {
        const uint8_t newRate = static_cast<uint8_t>(rate & 7);
        const bool edge = selectedBit(_timFreq, crystal) && !selectedBit(newRate, crystal);
        _timFreq = newRate;
        return edge;
    }

private:
    static uint8_t apply(uint8_t reg, uint8_t value)
    {
        const uint8_t mask = static_cast<uint8_t>(value & 0x07);
        return static_cast<uint8_t>((value & 0x80) ? (reg | mask) : (reg & ~mask));
    }

    /// Lowest pending enabled request (the one the acknowledge serves)
    uint8_t priority() const
    {
        const uint8_t pending = static_cast<uint8_t>(_req & _ena);
        return static_cast<uint8_t>(pending & (~pending + 1));
    }

    uint8_t _ena = 0x01;
    uint8_t _req = 0x00;
    uint8_t _timFreq = 0;
};
