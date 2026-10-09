// z84c15.h - the Zilog Z84C15's on-chip block (C++), around the CPU core of
// z84cpu.h.
//
// The chip (Zilog PS0182, "Z84C13/C15 IPC/EIPC"; research-cpu-z84c15.md
// section 4) adds to its Z84C00 core:
//  - the wait-state generator (WCR, MWBR): programmed waits on the core's own
//    bus cycles, with the power-on window (WCR reads #FF and acts as #FF for
//    the first 15 M1 cycles unless written);
//  - two chip selects (CSBR, MCR D0/D1), MCR's clock-divider / reset-output /
//    CRC bits, the watchdog (WDTMR, WDTCR);
//  - a CTC, an SIO and a PIO on an internal interrupt daisy chain whose order
//    register #F4 sets (MAME tmpz84c015.cpp:144-150).
// The fixed ports are decoded by A7-A0 only and have no image (PS0182
// p. 310): #10-#13 CTC, #18-#1B SIO, #1C-#1F PIO, #EE/#EF system control,
// #F0/#F1 watchdog, #F4 interrupt priority.
//
// The host routes those ports to Read / Write from its own port path (so its
// tracing and journals see them), asks IntPending / AcknowledgeInterrupt at an
// instruction boundary (the chain comes before the board's own /INT), and
// supplies the clock the CTC and the watchdog count (a monotonic time base, the
// length of a CPU clock in it, the CTC's CLK/TRG wiring).
//
// Worked example (power-on): after PowerOn a NOP costs 4 T + 3 memory waits
// (MWBR #F0 covers all of 64K) + 1 M1 extension = 8 T for the first 15 M1
// cycles; reading WCR (OUT (#EE),0 : IN A,(#EF)) returns #FF meanwhile.

#ifndef Z84C15_H
#define Z84C15_H

#include <cstddef>
#include <cstdint>
#include <functional>

#include "z84cpu.h"

namespace Z84Lib
{

namespace Detail
{
/// floor(a x b / c) without overflowing a x b while (c - 1) x b fits (c > 0)
inline uint64_t MulDivFloor(uint64_t a, uint64_t b, uint64_t c)
{
    return (a / c) * b + ((a % c) * b) / c;
}
/// ceil(a x b / c), same range
inline uint64_t MulDivCeil(uint64_t a, uint64_t b, uint64_t c)
{
    return MulDivFloor(a, b, c) + (((a % c) * b) % c != 0 ? 1u : 0u);
}
}  // namespace Detail

/// The four-channel CTC (Zilog Z80 CTC data sheet, PS0181; the Z84C15's CTC
/// is the same block, PS0182 p. 293). Ports #10-#13, one per channel. A write
/// with bit 0 = 1 is a control word: bit 7 interrupt enable, 6 counter mode
/// (else timer), 5 prescaler 256 (else 16), 4 rising CLK/TRG edge (else
/// falling), 3 timer started by a CLK/TRG edge (else by the time constant), 2
/// a time constant follows, 1 software reset. With bit 0 = 0 to channel 0 it is
/// the interrupt vector (bits 7-3; channel n answers base | n x 2). The time
/// constant (0 = 256) loads the down-counter; a read returns the current count.
///
/// The down-counter steps on the prescaler's output (timer mode: the system
/// clock / 16 or / 256) or on the selected CLK/TRG edge (counter mode). At
/// zero it reloads the time constant, pulses ZC/TO (channels 0-2 have the pin)
/// and, with its interrupt enabled, requests an interrupt. A time constant
/// written to a running channel takes effect at the next reload; a software
/// reset stops the channel and keeps its count.
///
/// Time. Nothing runs by itself: the owner supplies a monotonic clock in
/// "units" of its choice (SetClock) and how many units one system clock lasts
/// (SetSystemClockPeriod, a ratio, default 1 / 1 = the clock counts system
/// clocks). Counts are derived from the clock when read or polled, so a
/// channel costs nothing per instruction. When the system clock changes speed
/// (a turbo switch) the owner calls SetSystemClockPeriod at that instant: the
/// timers keep their count and continue at the new rate.
///
/// CLK/TRG inputs are the board's (SetTrigger): nothing, a fixed-frequency
/// clock in real time (needs SetUnitsPerSecond), or the ZC/TO output of a
/// lower channel (cascade). A counter without an input holds its count; a
/// timer waiting for its trigger edge waits.
///
/// Simplifications (all below one input period): the timer starts at the
/// trigger edge or the time-constant write (the data sheet: the second system
/// clock after it), and a clock input's rising and falling edges count at the
/// same instant (the edge select shifts a count by half an input period).
///
/// Worked example (the Sprinter, MAME sprinter.cpp:1993-2008): TRG2 is a
/// 875 kHz clock, ZC/TO2 drives TRG3. Channel 2 gets control #57 (counter,
/// rising edge, time constant follows) and 112: ZC/TO2 pulses at 875 000 / 112
/// = 7 812.5 Hz. Channel 3 gets #D7 (interrupt, counter) and 160: it counts
/// those pulses and interrupts at 7 812.5 / 160 = 48.83 Hz, with vector base
/// #00 answering #06.
class Z84Ctc
{
public:
    /// What drives a channel's CLK/TRG input
    enum class TriggerKind : uint8_t
    {
        None = 0,     ///< not connected: a counter holds, a triggered timer waits
        Clock = 1,    ///< a fixed-frequency clock in real time (`hz`)
        Cascade = 2,  ///< the ZC/TO output of channel `source` (a lower channel)
    };
    struct Trigger
    {
        TriggerKind kind = TriggerKind::None;
        uint32_t hz = 0;     ///< Clock: the frequency
        uint8_t source = 0;  ///< Cascade: the channel whose ZC/TO it is
    };

    struct ChannelState
    {
        uint8_t control = 0x03;       ///< last control word (power-on: reset)
        uint8_t timeConstant = 0;     ///< 0 = 256
        uint8_t awaitingConstant = 0;
        uint8_t running = 0;          ///< counting, or a timer waiting for its trigger edge
        uint8_t waitingTrigger = 0;   ///< timer with bit 3: the first CLK/TRG edge after `anchor` starts it
        uint8_t down = 0;             ///< the count at the anchor (0 = 256)
        uint64_t anchor = 0;          ///< timer: the clock at `down`; counter (or waiting): the input edges counted by then
        uint64_t zeroBase = 0;        ///< zero counts (ZC/TO pulses) before the anchor, since the reset
        uint64_t zeroSeen = 0;        ///< zero counts already turned into interrupt requests
        uint8_t ip = 0;               ///< interrupt pending
        uint8_t ius = 0;              ///< interrupt under service
    };

    void Reset();

    uint8_t Read(uint8_t channel);
    void Write(uint8_t channel, uint8_t value);

    uint8_t Vector() const { return _vector; }
    /// The interrupt vector base as a state restore sets it (Z84C15::LoadState)
    void SetVector(uint8_t vector) { _vector = vector; }
    const ChannelState& GetChannel(uint8_t channel) const { return _ch[channel & 3]; }
    ChannelState& Channel(uint8_t channel) { return _ch[channel & 3]; }

    /// region <Time base and inputs (the board's wiring)>
    /// The clock in units (monotonic)
    void SetClock(std::function<uint64_t()> clock) { _clock = std::move(clock); }
    /// Units per second of that clock (for the Clock triggers; 0 = unknown, Clock triggers never fire)
    void SetUnitsPerSecond(uint64_t unitsPerSecond);
    uint64_t UnitsPerSecond() const { return _unitsPerSecond; }
    /// One system clock lasts num / den units. At a change the timers keep their count (folded at now)
    void SetSystemClockPeriod(uint32_t num, uint32_t den);
    uint32_t SystemClockNum() const { return _clkNum; }
    uint32_t SystemClockDen() const { return _clkDen; }
    /// Restore the period without folding (a state restore: the anchors were saved under it)
    void RestoreSystemClockPeriod(uint32_t num, uint32_t den);
    /// Connect channel `channel`'s CLK/TRG (a Cascade source must be a lower channel; anything else is None)
    void SetTrigger(uint8_t channel, Trigger trigger);
    const Trigger& GetTrigger(uint8_t channel) const { return _trg[channel & 3]; }
    /// endregion

    /// region <Live view (no side effects: debuggers, automation, the board's ZC/TO consumers)>
    /// The down-counter now (1-256 as a byte, 0 = 256)
    uint8_t Count(uint8_t channel) const;
    /// ZC/TO pulses since the reset, up to now
    uint64_t ZeroCounts(uint8_t channel) const;
    /// The ZC/TO frequency from the channel's programming and input (0 = stopped, no input or still waiting)
    double OutputHz(uint8_t channel) const;
    /// endregion

    /// Turn the zero counts up to now into interrupt requests (channels with the interrupt enabled)
    void Poll();
    /// The channels were changed from outside (a state restore): recompute when Poll has work next
    void Refresh() { UpdateNextDue(); }
    /// The clock (SetClock units) of the earliest zero count Poll still has to turn into an interrupt request
    /// (a channel with its interrupt enabled), UINT64_MAX with none. Before it, Poll changes nothing; it may lie
    /// in the past until the next Poll
    uint64_t NextDue() const { return _nextDue; }

private:
    uint64_t Now() const { return _clock ? _clock() : 0; }
    /// The channel as it stands at `now`: a triggered timer whose edge came is running from that edge
    ChannelState Effective(uint8_t channel, uint64_t now) const;
    /// Input edges (counter) seen by channel `channel` up to `now`
    uint64_t InputEdges(uint8_t channel, uint64_t now) const;
    /// The clock of input edge number `edge` (UINT64_MAX: never)
    uint64_t EdgeTime(uint8_t channel, uint64_t edge) const;
    /// Down-counter steps since the anchor of `ch` (an effective, running channel)
    uint64_t Steps(uint8_t channel, const ChannelState& ch, uint64_t now) const;
    /// Total zero counts of `channel` at `now`
    uint64_t ZeroCountsAt(uint8_t channel, uint64_t now) const;
    /// The clock of zero count number `k` (total, k > zeroBase; UINT64_MAX: never)
    uint64_t ZeroTime(uint8_t channel, uint64_t k) const;
    /// The down-counter of `ch` at `now` (1-256)
    uint32_t CountAt(uint8_t channel, const ChannelState& ch, uint64_t now) const;
    /// Re-anchor a running channel at `now` (its count, zero counts and phase kept)
    void Fold(uint8_t channel, uint64_t now);
    /// The earliest clock a channel with its interrupt enabled reaches its next zero count
    void UpdateNextDue();
    uint64_t Prescaler(const ChannelState& ch) const { return (ch.control & 0x20) ? 256u : 16u; }

    ChannelState _ch[4];
    Trigger _trg[4];
    uint8_t _vector = 0;
    std::function<uint64_t()> _clock;
    uint64_t _unitsPerSecond = 0;
    uint32_t _clkNum = 1;  ///< one system clock = _clkNum / _clkDen units
    uint32_t _clkDen = 1;
    /// Clock triggers: edges = clock x _trgNum[ch] / _trgDen[ch] (hz / units per second, reduced)
    uint64_t _trgNum[4] = {};
    uint64_t _trgDen[4] = {1, 1, 1, 1};
    uint64_t _nextDue = UINT64_MAX;
};

/// The two-channel SIO, asynchronous mode. Ports (any high byte): #18 A data,
/// #19 A control, #1A B data, #1B B control. WR0-WR7 stored through the WR0
/// register pointer; RR0 (bit 0 a received character is waiting, bit 2
/// transmit buffer empty), RR1 (bit 0 all sent, bit 5 receive overrun), RR2
/// (channel B: the interrupt vector, modified by the status when WR1B bit 2 is
/// set); a 3-byte receive FIFO per channel filled by the host (Receive);
/// transmit goes to a sink at once.
///
/// Receive overrun (Zilog Z80 SIO technical manual, RR1 bit 5; Toshiba
/// TMPZ84C015B data book §3.6 RR1 D5; MAME z80sio.cpp queue_received): a
/// character that completes while the FIFO holds three overwrites the newest
/// one (the third) and carries the overrun flag. RR1 shows the status of the
/// character at the top of the FIFO: bit 5 sets when the flagged character
/// gets there and stays set (latched) until the Error Reset command (WR0
/// command 6). In the interrupt-on-first-character mode (WR1 bits 4-3 = 01) a
/// read does not advance the FIFO past that character until the Error Reset
/// (MAME data_read). Nothing in the SIO holds the sender off.
///
/// Worked example: #E0 #F0 #72 wait, #E0 then #72 arrive: the FIFO holds
/// #E0 #F0 #72 (the last #72 written over the first, flagged). RR1 bit 5 = 0;
/// two reads give #E0 #F0, now RR1 bit 5 = 1; the third gives #72.
///
/// Interrupts: receive only (WR1 bits 4-3: 01 the first character after
/// "enable INT on next Rx character", 10 / 11 every character while one is
/// waiting). Transmit and external / status interrupts are not modeled.
///
/// Worked example: OUT (#19),#00 / #01 selects WR1 and writes #00 to it (no
/// interrupts); #03 / #C1 sets WR3 = #C1 (8 bits, receiver on); #04 / #07,
/// #05 / #62. A scan code #1C pushed with Receive(0, #1C) makes IN A,(#19)
/// return bit 0 = 1, IN A,(#18) return #1C and bit 0 = 0 again.
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
        /// Bit 0: RR1 bit 5 (latched until Error Reset); bits 1-3: FIFO entry 0-2 was written over (the flag
        /// that rides with the character into RR1 when it reaches the top)
        uint8_t overrun = 0;
        uint8_t rxFirstArmed = 0;  ///< WR1 mode 01: the next character interrupts
        uint8_t rxFirstIp = 0;     ///< WR1 mode 01: that character's request
        uint8_t rxIus = 0;         ///< the receive interrupt is under service
    };

    void Reset();

    /// A host-side byte arrives on channel `ch` (0 = A, 1 = B). Returns false on overrun: the FIFO was full and
    /// the byte replaced its newest character
    bool Receive(uint8_t ch, uint8_t value);
    /// RR1 bit 5 as the CPU reads it (the latched overrun)
    bool OverrunLatched(uint8_t ch) const { return (_ch[ch & 1].overrun & kOverrunLatch) != 0; }

    uint8_t ReadData(uint8_t ch);
    void WriteData(uint8_t ch, uint8_t value);
    uint8_t ReadControl(uint8_t ch);
    void WriteControl(uint8_t ch, uint8_t value);

    /// Port access by the low address byte #18-#1B
    uint8_t Read(uint8_t port);
    void Write(uint8_t port, uint8_t value);

    const Channel& GetChannel(uint8_t ch) const { return _ch[ch & 1]; }
    Channel& ChannelState(uint8_t ch) { return _ch[ch & 1]; }

    /// Bytes the guest transmits (channel, byte)
    void SetTransmitSink(std::function<void(uint8_t, uint8_t)> sink) { _transmit = std::move(sink); }

    /// A receive interrupt is requested on channel `ch`
    bool RxIp(uint8_t ch) const;
    /// The vector for channel `ch`'s receive interrupt (WR2 of channel B, status-modified by WR1B bit 2)
    uint8_t RxVector(uint8_t ch) const;

    /// WR0 command 7 on channel A (Return from interrupt): ends the SIO's highest service
    std::function<void()> onReturnFromInt;

private:
    static constexpr uint8_t kOverrunLatch = 0x01;
    /// The flag of FIFO entry `index` in Channel::overrun
    static constexpr uint8_t EntryFlag(uint8_t index) { return static_cast<uint8_t>(0x02u << index); }

    void ResetChannel(uint8_t ch);
    /// The character now at the top of the FIFO was written over: RR1 bit 5 latches
    static void LatchTopStatus(Channel& c);

    Channel _ch[2];
    std::function<void(uint8_t, uint8_t)> _transmit;
};

/// The PIO as a register file. Ports (any high byte, MAME read_alt order):
/// #1C port A data, #1D port A control, #1E port B data, #1F port B control.
/// Control words: mode (bits 3-0 = %1111, mode in bits 7-6; mode 3 takes a
/// direction byte next), interrupt control (bits 3-0 = %0111: bit 7 enable,
/// 6 AND / OR, 5 active high / low, 4 a mask follows), interrupt enable only
/// (%xxxx0011), the vector (bit 0 = 0). A data read returns the output latch
/// on output lines and the inputs (SetInputs; #FF while nothing drives them).
///
/// Interrupts: mode 3 only, when the monitored input lines (input direction,
/// mask bit 0) meet the AND / OR, high / low condition - on the edge where it
/// becomes true. The handshake modes 0-2 have no strobe wired: no interrupts.
///
/// Worked example (BIOS 3.04 #0154): OUT (#1D),#CF = mode 3 (bit control),
/// OUT (#1D),#00 = all port A lines outputs, OUT (#1C),#EA = POST code: a read
/// of #1C returns #EA.
class Z84Pio
{
public:
    struct Port
    {
        uint8_t mode = 1;            ///< 0 output, 1 input, 2 bidirectional, 3 bit control
        uint8_t direction = 0xFF;    ///< mode 3: 1 = input line
        uint8_t output = 0;
        uint8_t vector = 0;
        uint8_t intControl = 0;
        uint8_t mask = 0xFF;         ///< mode 3: 1 = the line is not monitored
        uint8_t next = 0;            ///< 1 = a direction byte follows, 2 = a mask follows
        uint8_t inputs = 0xFF;       ///< the lines' input levels
        uint8_t condition = 0;       ///< the interrupt condition was true at the last evaluation
        uint8_t ip = 0;
        uint8_t ius = 0;
    };

    void Reset();

    uint8_t Read(uint8_t port);
    void Write(uint8_t port, uint8_t value);

    const Port& GetPort(uint8_t p) const { return _port[p & 1]; }
    Port& PortState(uint8_t p) { return _port[p & 1]; }
    void SetInputs(uint8_t p, uint8_t value);

private:
    uint8_t ReadData(uint8_t p) const;
    void WriteControl(uint8_t p, uint8_t value);
    void Evaluate(uint8_t p);

    Port _port[2];
};

/// The system control registers (#EE pointer, #EF data: 0 WCR, 1 MWBR, 2 CSBR,
/// 3 MCR), the watchdog (#F0 WDTMR, #F1 WDTCR) and the interrupt priority
/// (#F4), as the software wrote them. WCR and MWBR also program the core's
/// wait generator (Z84C15::Write).
struct Z84SystemRegs
{
    uint8_t scrp = 0;
    uint8_t wcr = 0x00;     ///< as written; reads #FF in the power-on window
    uint8_t mwbr = 0xF0;
    uint8_t csbr = 0xFF;    ///< reset: xxxx1111
    uint8_t mcr = 0x01;     ///< D0 CS0 on, D1 CS1 on, D2 CRC, D3 reset output off, D4 clock / 1
    uint8_t wdtmr = 0xFB;   ///< D7 enable, D6-5 period 2^(16 + 2n), D4-3 halt mode, D2-0 %011
    uint8_t wdtcr = 0;      ///< the last command written
    uint8_t irqPriority = 0;

    /// First address outside CS0 (#10000 = CS0 covers everything; 0 = CS0 off).
    /// CS0 is active for CSBR[3:0] >= A15-A12 >= 0
    uint32_t Cs0End() const;
    /// CS1 is active for CSBR[7:4] >= A15-A12 > CSBR[3:0] (MCR D1)
    bool Cs1Selects(uint16_t addr) const;
};

class Z84C15
{
public:
    Z84C15();
    ~Z84C15();
    Z84C15(const Z84C15&) = delete;
    Z84C15& operator=(const Z84C15&) = delete;

    /// The CPU core (z84cpu.h): the host wires its bus and steps it
    Z84CPU* Cpu() const { return _cpu; }

    /// Power-on reset: the system registers to their reset values, the wait
    /// generator's power-on window armed, the watchdog started, the
    /// peripherals reset. The core's registers are the host's (Z84CpuReset)
    void PowerOn();
    /// /RESET: the peripherals restart; the system control registers keep
    /// their values (MAME sets them at device start only), the wait
    /// generator keeps its programming
    void Reset();

    /// The chip answers this port itself (A7-A0 only)
    static bool Owns(uint16_t port);
    uint8_t Read(uint8_t lowByte);
    void Write(uint8_t lowByte, uint8_t value);

    /// The clock (monotonic, in units of the owner's choice) for the CTC and the
    /// watchdog; with the default system clock period of 1 / 1 it counts CPU clocks
    void SetClock(std::function<uint64_t()> clock);
    /// One system (CPU) clock lasts num / den units. Call it at the instant the
    /// clock changes speed: the watchdog and the CTC timers continue at the new rate
    void SetSystemClockPeriod(uint32_t num, uint32_t den);
    /// Units per second of the clock (the CTC's fixed-frequency trigger inputs)
    void SetUnitsPerSecond(uint64_t unitsPerSecond) { ctc.SetUnitsPerSecond(unitsPerSecond); }

    /// /WDTOUT: called once per watchdog timeout. Not set: the output is not
    /// connected (the Sprinter, research-cpu-z84c15.md Q3)
    void SetWatchdogHandler(std::function<void()> handler) { _watchdogHandler = std::move(handler); }

    /// region <Daisy chain>
    /// An on-chip source requests an interrupt the chain lets through (no
    /// higher-priority source under service). Also polls the CTC and the watchdog
    bool IntPending();
    /// The INT acknowledge when IntPending: the winning source goes under
    /// service and supplies its vector
    uint8_t AcknowledgeInterrupt();
    /// RETI on the bus: the highest-priority source under service ends its service
    void OnReti();
    /// Some on-chip source is under service (the chain blocks the board's lower-priority /INT devices)
    bool AnyUnderService() const;
    /// endregion </Daisy chain>

    /// The watchdog: running and its timeout clock (for tests and debuggers; at the current system clock rate)
    bool WatchdogRunning() const { return _wdtRunning; }
    uint64_t WatchdogDeadline() const;

    /// The clock now (SetClock's function), in its units
    uint64_t Clock() const { return Now(); }
    /// The earliest clock (Clock() units) at which the chip may act on its own: IntPending turn true or /WDTOUT
    /// fire, with no register access, no Receive, no acknowledge and no RETI in between - the next zero count of
    /// an interrupting CTC channel, the watchdog's timeout while a handler is connected and it has not fired.
    /// UINT64_MAX: nothing is due (the SIO and the PIO request only after an access or a Receive). May lie in
    /// the past (then ask IntPending now). A host that runs a halted CPU's idle cycles in one go stops there
    uint64_t NextEventClock() const;

    /// region <State (snapshots, time travel)>
    /// Everything the chip carries from one instruction to the next besides the
    /// core's register file: the system registers, the wait generator (with the
    /// power-on window's M1 counter and the RETI rule's "after ED" flag), the
    /// watchdog, the CTC, the SIO (receive FIFOs included) and the PIO, with the
    /// interrupt state of the daisy chain (each source's IP / IUS). A fixed
    /// little-endian layout of kStateSize bytes, so a host can store it as a blob
    /// and version it itself. Not in it: the CPU registers (the host's register
    /// file, Z84CpuAttachRegisterFile), the clock and the callbacks.
    ///
    /// Layout: system 8 (SCRP, WCR, MWBR, CSBR, MCR, WDTMR, WDTCR, #F4) | wait
    /// generator 6 (WCR as written, effective WCR, MWBR, M1 cycles left in the
    /// power-on window, after-ED, active) | watchdog 18 (running, fired, start
    /// clock u64, system clocks counted before the start u64) | CTC 137 (system
    /// clock period num u32, den u32, vector, then per channel: control, time
    /// constant, awaiting constant, running, waiting for the trigger, count at
    /// the anchor, anchor u64, zero counts before it u64, zero counts seen u64,
    /// IP, IUS) | SIO 36 (per channel: WR0-WR7, pointer, FIFO[3], FIFO count,
    /// last data, overrun, Rx-first armed, Rx-first IP, Rx IUS) | PIO 22 (per
    /// port: mode, direction, output, vector, interrupt control, mask, next,
    /// inputs, condition, IP, IUS). The CTC's inputs (SetTrigger) and the units
    /// per second are the board's wiring, not state
    static constexpr size_t kStateSize = 8 + 6 + 18 + 137 + 36 + 22;
    void SaveState(uint8_t* dst) const;
    void LoadState(const uint8_t* src);
    /// endregion </State>

    Z84Ctc ctc;
    Z84Sio sio;
    Z84Pio pio;
    Z84SystemRegs system;

private:
    /// One daisy-chain source: device 0 CTC (channels 0-3), 1 SIO (A Rx, B Rx), 2 PIO (A, B)
    struct Source
    {
        uint8_t device;
        uint8_t index;
    };

    uint64_t Now() const { return _clock ? _clock() : 0; }
    void PollWatchdog();
    void ClearWatchdog();
    bool Ip(Source s) const;
    bool Ius(Source s) const;
    void SetIus(Source s, bool value);
    uint8_t Vector(Source s) const;
    void ClearIpOnAcknowledge(Source s);
    /// Sources in priority order (#F4); returns the count
    int Order(Source* out) const;
    void ReturnFromIntInSio();

    Z84CPU* _cpu = nullptr;
    std::function<uint64_t()> _clock;
    std::function<void()> _watchdogHandler;
    bool _wdtRunning = false;
    bool _wdtFired = false;
    uint64_t _wdtStart = 0;       ///< the clock the watchdog's current count started at
    uint64_t _wdtClocksBefore = 0; ///< system clocks counted before _wdtStart (a clock speed change folds)
};

}  // namespace Z84Lib

#endif  // Z84C15_H
