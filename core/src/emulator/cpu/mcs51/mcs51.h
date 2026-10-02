#pragma once

/// @file mcs51.h
/// @brief A cycle-exact Intel MCS-51 core (8031 / 8051, 8032 / 8052): every
/// opcode with its machine-cycle count, internal RAM (128 or 256 bytes), the
/// SFRs, timers 0 / 1 (modes 0..3) and timer 2 (8052: auto-reload, capture,
/// baud-rate generator), the serial port (modes 0..3), interrupts with two
/// priority levels, quasi-bidirectional ports and the external bus (MOVX)
/// through callbacks.
///
/// Time is counted in oscillator clocks (12 per machine cycle). The owner runs
/// the core up to a clock (`Run`); a callback can end the run early
/// (`RequestStop`), e.g. when the MCU has answered the Z80.
///
/// The serial port works on whole frames: a byte written to SBUF leaves after
/// its frame time (callback `serialOut`); a received byte is handed in with
/// `SerialIn` at the moment its stop bit is sampled, so RI rises when a real
/// receiver would raise it. `SerialBitClocks` gives the frame timing the
/// registers set, for whoever paces the line.
///
/// Reference: Intel "MCS-51 Microcontroller Family User's Manual" (272383),
/// Atmel AT89S52 datasheet (timer 2); ATM Turbo 2+ keyboard controller TDD:
/// docs/inprogress/2026-10-01-atm2-keyboard-controller/tdd-atm2-kbc.md §4.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace mcs51
{

class Mcs51
{
public:
    enum class Variant : uint8_t
    {
        I8051,   ///< 128 bytes RAM, timers 0 / 1 (8031 / 8051 / 1816VE31)
        I8052,   ///< 256 bytes RAM, timer 2 (8032 / 8052 / AT89S52)
    };

    /// SFR addresses
    static constexpr uint8_t kP0 = 0x80, kSp = 0x81, kDpl = 0x82, kDph = 0x83, kPcon = 0x87, kTcon = 0x88,
                             kTmod = 0x89, kTl0 = 0x8A, kTl1 = 0x8B, kTh0 = 0x8C, kTh1 = 0x8D, kP1 = 0x90,
                             kScon = 0x98, kSbuf = 0x99, kP2 = 0xA0, kIe = 0xA8, kP3 = 0xB0, kIp = 0xB8,
                             kT2con = 0xC8, kT2mod = 0xC9, kRcap2l = 0xCA, kRcap2h = 0xCB, kTl2 = 0xCC,
                             kTh2 = 0xCD, kPsw = 0xD0, kAcc = 0xE0, kB = 0xF0;

    /// Interrupt vectors
    static constexpr uint16_t kVectorIe0 = 0x03, kVectorTf0 = 0x0B, kVectorIe1 = 0x13, kVectorTf1 = 0x1B,
                              kVectorSerial = 0x23, kVectorTf2 = 0x2B;

    struct Bus
    {
        /// MOVX read / write; `address` is DPTR, or P2 latch:Ri for @Ri
        std::function<uint8_t(uint16_t address)> movxRead;
        std::function<void(uint16_t address, uint8_t value)> movxWrite;
        /// A port latch changed (port 0..3, the new latch)
        std::function<void(int port, uint8_t latch)> portOut;
        /// A serial frame left TXD (the byte; `ninth` = TB8 in modes 2 / 3)
        std::function<void(uint8_t byte, bool ninth)> serialOut;
    };

    explicit Mcs51(Variant variant = Variant::I8051);

    void SetBus(Bus bus) { _bus = std::move(bus); }
    /// Program memory (the ROM image); fetches past its end read #FF
    void SetProgram(std::vector<uint8_t> image) { _program = std::move(image); }
    Variant GetVariant() const { return _variant; }

    /// The RST pin: registers to their reset values, RAM kept (as on silicon)
    void Reset();

    /// Run until the oscillator clock reaches `untilClock` (whole
    /// instructions: it can end a few clocks later), or until RequestStop
    void Run(uint64_t untilClock);
    void RequestStop() { _stop = true; }
    /// RST asserted from a callback (the MCU resetting itself through a
    /// board line): taken after the current instruction, then the run stops
    void RequestReset()
    {
        _resetPending = true;
        _stop = true;
    }
    uint64_t Clock() const { return _clock; }
    /// Move the clock without running (power-down, or a restart of time)
    void SetClock(uint64_t clock) { _clock = clock; }

    /// An external pin level (port 0..3, bit 0..7). Reads see `latch AND
    /// pin`; P3.2 / P3.3 falling edges latch IE0 / IE1 (edge mode), P3.4 /
    /// P3.5 count for timers 0 / 1, P1.0 / P1.1 are T2 / T2EX (8052)
    void SetPin(int port, int bit, bool level);
    bool Pin(int port, int bit) const { return (_pins[port & 3] >> (bit & 7)) & 1; }
    uint8_t Latch(int port) const { return _sfr[(0x80 + 0x10 * (port & 3)) & 0x7F]; }

    /// A serial frame arrives (its stop bit sampled now): SBUF / RB8 / RI per
    /// the mode and SM2; lost if RI is still set or REN is off. False = lost
    bool SerialIn(uint8_t byte, bool ninth = true);
    /// Oscillator clocks one bit lasts at the current settings (TX or RX
    /// side: timer 1 or timer 2 as RCLK / TCLK select); 0 = the timer that
    /// clocks it does not run
    uint64_t SerialBitClocks(bool receive) const;
    /// Bits per frame of the current mode (10 or 11; mode 0: 8)
    int SerialFrameBits() const;

    // Direct access (debugging, tests, state reports)
    uint8_t Sfr(uint8_t address) const { return _sfr[address & 0x7F]; }
    void SetSfr(uint8_t address, uint8_t value) { WriteSfr(address, value); }
    uint8_t Ram(uint8_t address) const { return _ram[address]; }
    void SetRam(uint8_t address, uint8_t value) { _ram[address] = value; }
    uint16_t Pc() const { return _pc; }
    void SetPc(uint16_t pc) { _pc = pc; }
    uint8_t Acc() const { return _sfr[kAcc & 0x7F]; }
    uint8_t Psw() const;
    uint64_t Instructions() const { return _instructions; }
    bool Idle() const { return _idle; }

    /// Fixed-size state for TTD (trivial)
    struct State
    {
        uint64_t clock, instructions;
        uint16_t pc;
        uint8_t ram[256];
        uint8_t sfr[128];
        uint8_t pins[4];
        uint8_t inService;          ///< bit 0: a low-level ISR runs, bit 1: a high-level one
        uint8_t blockOne;           ///< RETI / IE / IP write: no interrupt before the next instruction
        uint8_t idle, powerDown;
        uint8_t sbufIn, txBusy, txNinth, txByte;
        uint8_t t1Half, reserved[3];
        uint32_t txTicks;           ///< baud ticks left in the frame on TXD
        uint32_t txTicksToTi;       ///< ticks until TI (the stop bit starts)
        uint32_t t2Prescale;        ///< oscillator clocks not yet counted by timer 2 (baud mode: fosc / 2)
        uint32_t reserved2;
        uint64_t visibleAt[6];      ///< when each request becomes visible to the interrupt poll
        uint8_t lastRequests, reserved3[7];
    };
    void SaveState(State& out) const;
    void LoadState(const State& in);

private:
    uint8_t Fetch() { return _pc < _program.size() ? _program[_pc++] : (_pc++, 0xFF); }
    uint8_t Code(uint16_t address) const { return address < _program.size() ? _program[address] : 0xFF; }

    uint8_t ReadDirect(uint8_t address, bool readModifyWrite = false);
    void WriteDirect(uint8_t address, uint8_t value);
    uint8_t ReadSfr(uint8_t address, bool readModifyWrite);
    void WriteSfr(uint8_t address, uint8_t value);
    uint8_t ReadIndirect(uint8_t address) const;
    void WriteIndirect(uint8_t address, uint8_t value);
    bool ReadBit(uint8_t bit, bool readModifyWrite = false);
    void WriteBit(uint8_t bit, bool value);

    uint8_t& A() { return _sfr[kAcc & 0x7F]; }
    uint8_t& Bref() { return _sfr[kB & 0x7F]; }
    uint8_t& PswRef() { return _sfr[kPsw & 0x7F]; }
    bool Cy() const { return (_sfr[kPsw & 0x7F] & 0x80) != 0; }
    void SetCy(bool v) { PswRef() = static_cast<uint8_t>((PswRef() & 0x7F) | (v ? 0x80 : 0)); }
    uint8_t RegAddress(int n) const { return static_cast<uint8_t>((_sfr[kPsw & 0x7F] & 0x18) + n); }
    uint8_t R(int n) const { return _ram[RegAddress(n)]; }
    void SetR(int n, uint8_t v) { _ram[RegAddress(n)] = v; }
    uint16_t Dptr() const { return static_cast<uint16_t>((_sfr[kDph & 0x7F] << 8) | _sfr[kDpl & 0x7F]); }
    void SetDptr(uint16_t v)
    {
        _sfr[kDph & 0x7F] = static_cast<uint8_t>(v >> 8);
        _sfr[kDpl & 0x7F] = static_cast<uint8_t>(v);
    }
    void Push(uint8_t v);
    uint8_t Pop();

    void Add(uint8_t value, bool withCarry);
    void Subb(uint8_t value);

    /// One instruction; returns its machine cycles
    int Execute();
    /// An interrupt taken now (LCALL to the vector, 2 machine cycles), or 0
    int ServiceInterrupts();
    /// Timers, serial port, external-pin sampling for `cycles` machine cycles
    void Tick(int cycles);
    void TickTimer01(int cycles);
    void TickTimer2(int cycles);
    void TimerOverflow1();
    void BaudTick(bool fromTimer2);
    void StartTransmit(uint8_t byte);
    void Port3Changed(uint8_t before, uint8_t after);
    /// The six interrupt request flags (IE0, TF0, IE1, TF1, RI|TI, TF2|EXF2)
    uint8_t RequestBits() const;
    /// Requests that rose since the last call become visible to the poll at `visibleAt`
    void NoteRequests(uint64_t visibleAt);

    Variant _variant;
    Bus _bus;
    std::vector<uint8_t> _program;
    std::array<uint8_t, 256> _ram{};
    std::array<uint8_t, 128> _sfr{};
    std::array<uint8_t, 4> _pins{0xFF, 0xFF, 0xFF, 0xFF};
    uint64_t _clock = 0;
    uint64_t _instructions = 0;
    uint16_t _pc = 0;
    uint8_t _inService = 0;
    bool _blockOne = false;
    bool _idle = false;
    bool _powerDown = false;
    bool _stop = false;
    bool _resetPending = false;

    uint8_t _sbufIn = 0;            ///< SBUF as read (received)
    bool _txBusy = false;
    bool _txNinth = false;
    uint8_t _txByte = 0;
    uint32_t _txTicks = 0;          ///< 1/16-bit ticks left in the frame (modes 1..3)
    uint32_t _txTicksToTi = 0;
    bool _t1Half = false;           ///< SMOD = 0: every other timer-1 overflow ticks the baud divider
    uint32_t _t2Prescale = 0;
    /// A flag is latched in S5P2 of the machine cycle it rises in and polled
    /// in the next one: the poll at an instruction boundary sees it only from
    /// here on (minimum latency 3 machine cycles, user's manual "Response time")
    std::array<uint64_t, 6> _visibleAt{};
    uint8_t _lastRequests = 0;
};

}  // namespace mcs51
