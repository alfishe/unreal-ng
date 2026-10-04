#pragma once

/// @file mcs48.h
/// @brief An Intel MCS-48 core (8048 / 8035 / 8049 / 8039 and the Soviet
/// 1816VE48 / 1816VE35): every opcode with its machine-cycle count, the data
/// memory (64, 128 or 256 bytes) with the two register banks and the 8-level
/// stack, the PSW, the timer / event counter with its overflow interrupt, the
/// external interrupt, the test inputs T0 / T1, the quasi-bidirectional ports
/// P1 / P2, the BUS port and the 8243 expander through callbacks.
///
/// Time is counted in oscillator clocks: one machine cycle is 15 of them
/// (an 8 MHz crystal gives 1.875 us cycles). The owner runs the core up to a
/// clock (`Run`); a callback can end the run early (`RequestStop`), e.g. when
/// the MCU has answered the Z80.
///
/// Program memory: the image handed in (up to 4 KB); fetches past its end
/// read #FF (an external EPROM that is not selected). The program counter
/// increments in its low 11 bits; bit 11 comes from the memory bank flag on
/// JMP / CALL, and is 0 for them inside an interrupt routine.
///
/// /INT is level-sensitive: the core samples the pin before every instruction
/// and takes the interrupt (a CALL to 3, two machine cycles) while it is low,
/// interrupts are enabled and no interrupt routine runs (RETR ends it).
///
/// Reference: Intel "MCS-48 Family of Single Chip Microcomputers User's
/// Manual" (1980), instruction set chapter; MAME mcs48.cpp for the flag
/// details of DA A. Design: docs/inprogress/2026-10-01-profi-v3-v5/design.md
/// section "Keyboard".

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <vector>

namespace mcs48
{

class Mcs48
{
public:
    /// Oscillator clocks per machine cycle (the ALE period)
    static constexpr uint32_t kClocksPerCycle = 15;

    /// PSW bits
    static constexpr uint8_t kPswCy = 0x80, kPswAc = 0x40, kPswF0 = 0x20, kPswBs = 0x10;

    /// Interrupt vectors
    static constexpr uint16_t kVectorReset = 0x000, kVectorInt = 0x003, kVectorTimer = 0x007;

    struct Bus
    {
        /// MOVX A,@Ri / MOVX @Ri,A: the 8-bit address on the BUS (R0 / R1), the data byte
        std::function<uint8_t(uint8_t address)> movxRead;
        std::function<void(uint8_t address, uint8_t value)> movxWrite;
        /// A port latch changed: 0 = BUS (OUTL / ANL / ORL BUS), 1 = P1, 2 = P2
        std::function<void(int port, uint8_t latch)> portOut;
        /// INS A,BUS: the byte on the BUS (no callback: #FF, the pull-ups)
        std::function<uint8_t()> busIn;
        /// MOVD A,Pp (p = 4..7): the expander's nibble (no callback: #F)
        std::function<uint8_t(int port)> expanderRead;
        /// MOVD / ORLD / ANLD Pp,A: the nibble the 8243 port gets after the operation
        std::function<void(int port, uint8_t nibble)> expanderWrite;
    };

    /// `ramSize`: 64 (8048 / 8035), 128 (8049 / 8039) or 256 (8050 / 8040)
    explicit Mcs48(uint16_t ramSize = 64);

    void SetBus(Bus bus) { _bus = std::move(bus); }
    void SetProgram(std::vector<uint8_t> image) { _program = std::move(image); }
    uint16_t RamSize() const { return _ramSize; }

    /// The RESET pin: PC 0, bank 0, register bank 0, SP 0, ports to #FF,
    /// interrupts off, timer stopped, F0 / F1 cleared; RAM keeps its contents
    void Reset();

    /// Run until the clock reaches `untilClock` (whole instructions: it can
    /// end a few clocks later), or until RequestStop
    void Run(uint64_t untilClock);
    /// One instruction (or one interrupt entry); returns its machine cycles
    int Step();
    void RequestStop() { _stop = true; }
    uint64_t Clock() const { return _clock; }
    /// Move the clock without running (a restart of time)
    void SetClock(uint64_t clock) { _clock = clock; }

    /// Input pins. T1 counts its high-to-low edges in counter mode; /INT is
    /// active low and level-sensitive (false = asserted)
    void SetT0(bool level) { _t0 = level; }
    void SetT1(bool level);
    void SetInt(bool level) { _int = level; }
    bool T0() const { return _t0; }
    bool T1() const { return _t1; }
    bool IntPin() const { return _int; }
    /// An external level on P1 / P2 (port 1 or 2, bit 0..7): reads see latch AND pin
    void SetPin(int port, int bit, bool level);
    void SetPins(int port, uint8_t levels);
    /// Port latches: 0 = BUS, 1 = P1, 2 = P2
    uint8_t Latch(int port) const { return _latch[port & 3]; }

    // Direct access (debugging, tests, state reports)
    uint16_t Pc() const { return _pc; }
    void SetPc(uint16_t pc) { _pc = static_cast<uint16_t>(pc & 0x0FFF); }
    uint8_t A() const { return _a; }
    void SetA(uint8_t a) { _a = a; }
    /// PSW as MOV A,PSW reads it (bit 3 is always 1)
    uint8_t Psw() const { return static_cast<uint8_t>(_psw | 0x08); }
    void SetPsw(uint8_t psw) { _psw = static_cast<uint8_t>(psw & 0xF7); }
    bool F1() const { return _f1; }
    bool Mb() const { return _mb; }
    bool InterruptsEnabled() const { return _ie; }
    bool TimerInterruptEnabled() const { return _tcntie; }
    bool InInterrupt() const { return _inIsr; }
    bool TimerFlag() const { return _tf; }
    uint8_t Timer() const { return _timer; }
    uint8_t Ram(uint8_t address) const { return _ram[address & (_ramSize - 1)]; }
    void SetRam(uint8_t address, uint8_t value) { _ram[address & (_ramSize - 1)] = value; }
    /// Register Rn of the selected bank
    uint8_t R(int n) const { return _ram[RegAddress(n)]; }
    uint64_t Instructions() const { return _instructions; }
    uint8_t Code(uint16_t address) const { return address < _program.size() ? _program[address] : 0xFF; }

    /// Fixed-size state for TTD (trivial)
    struct State
    {
        uint64_t clock, instructions;
        uint16_t pc;
        uint8_t a, psw;
        uint8_t ram[256];
        uint8_t latch[4];            ///< BUS, P1, P2, unused
        uint8_t pins[4];             ///< BUS, P1, P2 external levels, unused
        uint8_t f1, mb, ie, tcntie;
        uint8_t inIsr, tf, timerPending, timerMode;   ///< timerMode: 0 stopped, 1 timer, 2 counter
        uint8_t timer, prescaler, t0, t1;
        uint8_t intPin, reserved[7];
    };
    static_assert(std::is_trivial_v<State>, "Mcs48::State is cleared and copied as bytes");
    void SaveState(State& out) const;
    void LoadState(const State& in);

private:
    enum class TimerMode : uint8_t
    {
        Stopped = 0,
        Timer = 1,
        Counter = 2,
    };

    /// Fetch at PC; the PC increments in its low 11 bits (bit 11 stays)
    uint8_t Fetch()
    {
        const uint8_t value = Code(_pc);
        _pc = static_cast<uint16_t>((_pc & 0x0800) | ((_pc + 1) & 0x07FF));
        return value;
    }
    uint8_t RegAddress(int n) const { return static_cast<uint8_t>(((_psw & kPswBs) ? 24 : 0) + (n & 7)); }
    uint8_t& Reg(int n) { return _ram[RegAddress(n)]; }
    uint8_t& Indirect(int n) { return _ram[Reg(n) & (_ramSize - 1)]; }

    void Push();
    void Pop(bool restorePsw);
    void Add(uint8_t value, bool withCarry);
    void SetCarry(bool carry) { _psw = static_cast<uint8_t>(carry ? (_psw | kPswCy) : (_psw & ~kPswCy)); }
    bool Carry() const { return (_psw & kPswCy) != 0; }
    /// JMP / CALL target: bits 8..10 from the opcode, bit 11 from the bank flag (0 inside an interrupt routine)
    uint16_t LongTarget(uint8_t op, uint8_t low) const;
    /// A conditional jump: the target is in the page of the operand byte
    void JumpIf(bool condition);
    void WritePort(int port, uint8_t value);
    uint8_t ReadPort(int port) const { return static_cast<uint8_t>(_latch[port] & _pins[port]); }
    void Expander(int port, int op);
    void Tick(int cycles);
    void TimerIncrement();
    /// One instruction; returns its machine cycles
    int Execute();

    uint16_t _ramSize;
    Bus _bus;
    std::vector<uint8_t> _program;
    std::array<uint8_t, 256> _ram{};
    std::array<uint8_t, 4> _latch{0xFF, 0xFF, 0xFF, 0xFF};
    std::array<uint8_t, 4> _pins{0xFF, 0xFF, 0xFF, 0xFF};
    uint64_t _clock = 0;
    uint64_t _instructions = 0;
    uint16_t _pc = 0;
    uint8_t _a = 0;
    uint8_t _psw = 0;
    bool _f1 = false;
    bool _mb = false;
    bool _ie = false;
    bool _tcntie = false;
    bool _inIsr = false;
    bool _tf = false;
    bool _timerPending = false;
    TimerMode _timerMode = TimerMode::Stopped;
    uint8_t _timer = 0;
    uint8_t _prescaler = 0;
    bool _t0 = true;
    bool _t1 = true;
    bool _int = true;
    bool _stop = false;
};

}  // namespace mcs48
