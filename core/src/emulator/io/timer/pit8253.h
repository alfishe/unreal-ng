#pragma once
/// @file pit8253.h
/// @brief Intel 8253 / KR580VI53 programmable interval timer: three 16-bit down counters.
///
/// Each counter takes a control word (register 3: SC1 SC0 the counter, RW1 RW0 latch / LSB / MSB / LSB then MSB,
/// M2..M0 the mode, BCD), a count (its own register) and runs in one of the six modes of the data sheet:
///   0 interrupt on terminal count, 1 hardware-retriggerable one-shot, 2 rate generator, 3 square wave,
///   4 software-triggered strobe, 5 hardware-triggered strobe;
/// binary (modulus 65536) or BCD (modulus 10000); a count of 0 is the modulus. A read returns the counting element
/// (or the value the counter latch command froze), LSB / MSB as RW says. Control word SC = 11 is the 8254's
/// read-back command: the 8253 ignores it, and so does this model. The control register cannot be read (#FF).
///
/// Timing follows the data sheet's CLK rules: a count written takes effect at the next CLK pulse (the load), mode 0
/// raises OUT N pulses after the load, mode 2 drops OUT for the pulse at which the counting element is 1, mode 3
/// holds OUT high for (N + 1) / 2 and low for N / 2 pulses, mode 4 / 5 strobe OUT low for one pulse when the count
/// reaches 0. A count written while mode 2 / 3 counts is taken at the next reload (mode 3: the end of the current
/// half-cycle). Mode 3's counting element reads N, N - 2, ..., 2 in each half for an even N; for an odd N the read
/// value is approximated the same way (the half lengths are exact).
///
/// Time: nothing runs on its own. The caller passes its clock (base T-states of the emulator, 3.5 MHz) to every
/// access; the counters advance by the CLK pulses that fell into the elapsed time (clockHz / baseClockHz of it, the
/// remainder kept) in closed form per mode - no per-pulse loop, no per-instruction work. The three CLK inputs share
/// one frequency (the ZX Profi+ board: 1.5 MHz, docs/inprogress/2026-10-04-profi-plus/design.md).
///
/// GATE: every gate reads high (tied to +5 V) unless the board drives it (SetGate). The 8253 has no RESET pin: a
/// machine reset does not reach it; the power-on state is every counter idle with OUT high and no count.

#include <cstdint>

class Pit8253
{
public:
    static constexpr uint8_t kCounter0 = 0;
    static constexpr uint8_t kCounter1 = 1;
    static constexpr uint8_t kCounter2 = 2;
    static constexpr uint8_t kControl = 3;

    /// One counter's state (fixed size, trivially copyable: part of the TTD blob)
    struct Counter
    {
        uint8_t control = 0;       ///< RW1 RW0 M2 M1 M0 BCD (bits 5-0 of its last control word)
        uint8_t out = 1;           ///< the OUT pin
        uint8_t gate = 1;          ///< the GATE input
        uint8_t hasCount = 0;      ///< a complete count was written since the control word
        uint8_t counting = 0;      ///< the counting element was loaded and counts
        uint8_t loadPending = 0;   ///< the next CLK pulse loads the counting element (a count written, a trigger)
        uint8_t newCount = 0;      ///< modes 2 / 3: a count written while counting, taken at the next reload
        uint8_t fired = 0;         ///< modes 0 / 1 / 4 / 5: the count reached zero since the load
        uint8_t writeMsb = 0;      ///< RW = 3: the next write is the MSB
        uint8_t readMsb = 0;       ///< RW = 3: the next read is the MSB
        uint8_t latched = 0;       ///< the counter latch command froze `latch`
        uint8_t lsb = 0;           ///< RW = 3: the LSB written, waiting for its MSB
        uint16_t cr = 0;           ///< the count register as written (BCD-coded in BCD mode)
        uint16_t latch = 0;        ///< the output latch (raw, as a read returns it)
        uint32_t reload = 0;       ///< modes 2 / 3: the count in effect, numeric (1..modulus)
        uint32_t ce = 0;           ///< the counting element, numeric (modes 0 / 1 / 4 / 5: 0..modulus-1; mode 2: 1..reload)
        uint32_t phaseLeft = 0;    ///< mode 3: CLK pulses until OUT toggles
    };
    static_assert(sizeof(Counter) == 28, "Pit8253::Counter layout changed");

    /// The chip (the TTD blob, PeripheralId::Pit8253)
    struct State
    {
        Counter counter[3];
        uint32_t reserved = 0;
        uint64_t lastNow = 0;      ///< the caller's clock at the last advance
        uint64_t fraction = 0;     ///< elapsed base T x clockHz not yet a whole CLK pulse (in units of 1 / baseHz)
    };
    static_assert(sizeof(State) == 3 * 28 + 4 + 16, "Pit8253::State layout changed");

    /// @param clockHz the CLK inputs' frequency
    /// @param baseClockHz the frequency of the caller's clock (`now` of every call)
    explicit Pit8253(uint32_t clockHz = 1500000, uint32_t baseClockHz = 3500000);

    /// Power-on: every counter idle (no count, OUT high, gate high); the clock starts at `now`
    void PowerOn(uint64_t now = 0);

    void Write(uint8_t reg, uint8_t value, uint64_t now);
    /// A counter's register reads its counting element or output latch; the control register floats (#FF)
    uint8_t Read(uint8_t reg, uint64_t now);

    /// Count the CLK pulses up to `now`. A clock that went back (the machine's counter restarted) moves the chip
    /// onto the new time base without counting
    void Advance(uint64_t now);

    /// Drive a counter's GATE (call at the instant it changes): low suspends modes 0, 2, 3, 4 (2 / 3 also force OUT
    /// high); a rising edge triggers modes 1 and 5 and restarts modes 2 and 3
    void SetGate(uint8_t counter, bool level, uint64_t now);

    /// The OUT pin of a counter as of the last advance
    bool Out(uint8_t counter) const { return _state.counter[counter % 3].out != 0; }

    /// The period of a counter's output in CLK pulses when it is a clock (mode 2 or 3, a count loaded or about to
    /// be, the gate high), else 0. A count written while counting counts from the next reload on
    uint32_t OutputPeriod(uint8_t counter) const;

    /// The counting element as an unlatched read would see it (no side effects: status views, tests)
    uint16_t PeekCount(uint8_t counter) const;

    uint32_t ClockHz() const { return _clockHz; }
    uint32_t BaseClockHz() const { return _baseHz; }

    const State& GetState() const { return _state; }
    void SetState(const State& state) { _state = state; }

    // Counter control word fields
    static uint8_t ModeOf(const Counter& c)
    {
        const uint8_t m = static_cast<uint8_t>((c.control >> 1) & 0x07);
        return m >= 6 ? static_cast<uint8_t>(m - 4) : m;   // 110 = mode 2, 111 = mode 3
    }
    static uint8_t RwOf(const Counter& c) { return static_cast<uint8_t>((c.control >> 4) & 0x03); }
    static bool BcdOf(const Counter& c) { return (c.control & 0x01) != 0; }

private:
    void AdvanceCounter(Counter& c, uint64_t pulses);
    void Load(Counter& c);
    void CountWritten(Counter& c);
    static uint32_t Modulus(const Counter& c) { return BcdOf(c) ? 10000u : 65536u; }
    /// The count register as a number of pulses (0 = the modulus)
    static uint32_t Numeric(const Counter& c, uint16_t raw);
    /// A counting element value as a read returns it (BCD-coded in BCD mode)
    static uint16_t Raw(const Counter& c, uint32_t numeric);
    static uint32_t HighHalf(uint32_t n) { return (n + 1) / 2; }
    static uint32_t LowHalf(uint32_t n) { return n > 1 ? n / 2 : 1; }
    uint16_t CountValue(const Counter& c) const;

    State _state;
    uint32_t _clockHz;
    uint32_t _baseHz;
    uint64_t _num = 3;   ///< clockHz / baseHz, reduced
    uint64_t _den = 7;
};
