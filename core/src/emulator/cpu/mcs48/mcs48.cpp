#include "emulator/cpu/mcs48/mcs48.h"

#include <cstring>

namespace mcs48
{

Mcs48::Mcs48(uint16_t ramSize) : _ramSize(ramSize == 128 || ramSize == 256 ? ramSize : 64)
{
    Reset();
}

void Mcs48::Reset()
{
    // User's manual "Reset": PC, SP, register bank, memory bank to 0; BUS floats; P1 / P2 to 1s;
    // interrupts disabled; timer stopped, its flag cleared; F0 / F1 cleared. RAM keeps its contents
    _pc = 0;
    _psw = 0;
    _f1 = false;
    _mb = false;
    _ie = false;
    _tcntie = false;
    _inIsr = false;
    _tf = false;
    _timerPending = false;
    _timerMode = TimerMode::Stopped;
    _prescaler = 0;
    for (int port = 0; port < 3; ++port)
        WritePort(port, 0xFF);
}

void Mcs48::SetT1(bool level)
{
    // Event counter mode: a high-to-low edge on T1 counts
    if (_t1 && !level && _timerMode == TimerMode::Counter)
        TimerIncrement();
    _t1 = level;
}

void Mcs48::SetPin(int port, int bit, bool level)
{
    const uint8_t mask = static_cast<uint8_t>(1u << (bit & 7));
    uint8_t& pins = _pins[port & 3];
    pins = static_cast<uint8_t>(level ? (pins | mask) : (pins & ~mask));
}

void Mcs48::SetPins(int port, uint8_t levels)
{
    _pins[port & 3] = levels;
}

void Mcs48::WritePort(int port, uint8_t value)
{
    const uint8_t before = _latch[port];
    _latch[port] = value;
    // BUS writes strobe even with an unchanged value (OUTL BUS is an output cycle); P1 / P2 report changes
    if (_bus.portOut && (port == 0 || before != value))
        _bus.portOut(port, value);
}

void Mcs48::Run(uint64_t untilClock)
{
    _stop = false;
    while (_clock < untilClock && !_stop)
        Step();
}

int Mcs48::Step()
{
    int cycles;
    // The timer counts the cycles of an instruction in the mode it ran under: STRT T does not count its own cycle,
    // STOP TCNT does (as MAME, which burns the cycle before the mode changes)
    const bool timing = _timerMode == TimerMode::Timer;
    // /INT (level, active low) before the timer interrupt; none while a routine runs (RETR ends it)
    if (!_inIsr && ((_ie && !_int) || (_tcntie && _timerPending)))
    {
        const bool external = _ie && !_int;
        if (!external)
            _timerPending = false;
        Push();
        _pc = external ? kVectorInt : kVectorTimer;
        _inIsr = true;
        cycles = 2;
    }
    else
    {
        cycles = Execute();
        ++_instructions;
    }
    _clock += static_cast<uint64_t>(cycles) * kClocksPerCycle;
    if (timing)
        Tick(cycles);
    return cycles;
}

void Mcs48::Tick(int cycles)
{
    // The timer counts machine cycles divided by 32
    for (int n = 0; n < cycles; ++n)
    {
        if (++_prescaler >= 32)
        {
            _prescaler = 0;
            TimerIncrement();
        }
    }
}

void Mcs48::TimerIncrement()
{
    if (++_timer == 0)
    {
        // Overflow #FF -> #00: the flag JTF tests, and the interrupt request when enabled
        _tf = true;
        if (_tcntie)
            _timerPending = true;
    }
}

void Mcs48::Push()
{
    // The stack lives in RAM 8..23: PC low, then PC 8..11 with PSW 4..7 (CY, AC, F0, BS) above
    const uint8_t sp = static_cast<uint8_t>(_psw & 0x07);
    const uint8_t address = static_cast<uint8_t>(8 + 2 * sp);
    _ram[address] = static_cast<uint8_t>(_pc);
    _ram[address + 1] = static_cast<uint8_t>(((_pc >> 8) & 0x0F) | (_psw & 0xF0));
    _psw = static_cast<uint8_t>((_psw & 0xF8) | ((sp + 1) & 0x07));
}

void Mcs48::Pop(bool restorePsw)
{
    const uint8_t sp = static_cast<uint8_t>((_psw - 1) & 0x07);
    const uint8_t address = static_cast<uint8_t>(8 + 2 * sp);
    _pc = static_cast<uint16_t>(_ram[address] | ((_ram[address + 1] & 0x0F) << 8));
    if (restorePsw)
        _psw = static_cast<uint8_t>((_ram[address + 1] & 0xF0) | sp);
    else
        _psw = static_cast<uint8_t>((_psw & 0xF8) | sp);
}

void Mcs48::Add(uint8_t value, bool withCarry)
{
    const unsigned carryIn = (withCarry && Carry()) ? 1u : 0u;
    const unsigned sum = static_cast<unsigned>(_a) + value + carryIn;
    const unsigned low = static_cast<unsigned>(_a & 0x0F) + (value & 0x0F) + carryIn;
    _psw = static_cast<uint8_t>(_psw & ~(kPswCy | kPswAc));
    if (low > 0x0F)
        _psw |= kPswAc;
    if (sum > 0xFF)
        _psw |= kPswCy;
    _a = static_cast<uint8_t>(sum);
}

uint16_t Mcs48::LongTarget(uint8_t op, uint8_t low) const
{
    const uint16_t bank = (_mb && !_inIsr) ? 0x0800 : 0x0000;
    return static_cast<uint16_t>(bank | ((op & 0xE0) << 3) | low);
}

void Mcs48::JumpIf(bool condition)
{
    const uint16_t operand = _pc;
    const uint8_t low = Fetch();
    if (condition)
        _pc = static_cast<uint16_t>((operand & 0x0F00) | low);
}

void Mcs48::Expander(int port, int op)
{
    // MOVD A,Pp / MOVD Pp,A / ORLD / ANLD: a 4-bit port of an 8243 on P20..P23 + PROG
    if (op == 0)
    {
        _a = static_cast<uint8_t>((_bus.expanderRead ? _bus.expanderRead(port) : 0x0F) & 0x0F);
        return;
    }
    uint8_t nibble = static_cast<uint8_t>(_a & 0x0F);
    if (op != 1)
    {
        const uint8_t current = static_cast<uint8_t>((_bus.expanderRead ? _bus.expanderRead(port) : 0x0F) & 0x0F);
        nibble = op == 2 ? static_cast<uint8_t>(current | nibble) : static_cast<uint8_t>(current & nibble);
    }
    if (_bus.expanderWrite)
        _bus.expanderWrite(port, nibble);
}

int Mcs48::Execute()
{
    const uint8_t op = Fetch();
    const int r = op & 0x07;
    const int i = op & 0x01;

    switch (op)
    {
        // region <Accumulator>
        case 0x03: Add(Fetch(), false); return 2;                                    // ADD A,#data
        case 0x13: Add(Fetch(), true); return 2;                                     // ADDC A,#data
        case 0x60: case 0x61: Add(Indirect(i), false); return 1;                    // ADD A,@Ri
        case 0x70: case 0x71: Add(Indirect(i), true); return 1;                     // ADDC A,@Ri
        case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E: case 0x6F:
            Add(Reg(r), false); return 1;                                            // ADD A,Rr
        case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F:
            Add(Reg(r), true); return 1;                                             // ADDC A,Rr
        case 0x53: _a &= Fetch(); return 2;                                          // ANL A,#data
        case 0x50: case 0x51: _a &= Indirect(i); return 1;                          // ANL A,@Ri
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            _a &= Reg(r); return 1;                                                  // ANL A,Rr
        case 0x43: _a |= Fetch(); return 2;                                          // ORL A,#data
        case 0x40: case 0x41: _a |= Indirect(i); return 1;                          // ORL A,@Ri
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
            _a |= Reg(r); return 1;                                                  // ORL A,Rr
        case 0xD3: _a ^= Fetch(); return 2;                                          // XRL A,#data
        case 0xD0: case 0xD1: _a ^= Indirect(i); return 1;                          // XRL A,@Ri
        case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
            _a ^= Reg(r); return 1;                                                  // XRL A,Rr
        case 0x17: ++_a; return 1;                                                   // INC A
        case 0x07: --_a; return 1;                                                   // DEC A
        case 0x27: _a = 0; return 1;                                                 // CLR A
        case 0x37: _a = static_cast<uint8_t>(~_a); return 1;                         // CPL A
        case 0x47: _a = static_cast<uint8_t>((_a << 4) | (_a >> 4)); return 1;       // SWAP A
        case 0x57:                                                                   // DA A
            if ((_a & 0x0F) > 0x09 || (_psw & kPswAc))
            {
                if (_a > 0xF9)
                    SetCarry(true);
                _a = static_cast<uint8_t>(_a + 0x06);
            }
            if ((_a & 0xF0) > 0x90 || Carry())
            {
                _a = static_cast<uint8_t>(_a + 0x60);
                SetCarry(true);
            }
            return 1;
        case 0xE7: _a = static_cast<uint8_t>((_a << 1) | (_a >> 7)); return 1;       // RL A
        case 0xF7:                                                                   // RLC A
        {
            const bool carry = (_a & 0x80) != 0;
            _a = static_cast<uint8_t>((_a << 1) | (Carry() ? 1 : 0));
            SetCarry(carry);
            return 1;
        }
        case 0x77: _a = static_cast<uint8_t>((_a >> 1) | (_a << 7)); return 1;       // RR A
        case 0x67:                                                                   // RRC A
        {
            const bool carry = (_a & 0x01) != 0;
            _a = static_cast<uint8_t>((_a >> 1) | (Carry() ? 0x80 : 0));
            SetCarry(carry);
            return 1;
        }
        // endregion </Accumulator>

        // region <Registers and data moves>
        case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
            ++Reg(r); return 1;                                                      // INC Rr
        case 0x10: case 0x11: ++Indirect(i); return 1;                              // INC @Ri
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF:
            --Reg(r); return 1;                                                      // DEC Rr
        case 0x23: _a = Fetch(); return 2;                                           // MOV A,#data
        case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD: case 0xFE: case 0xFF:
            _a = Reg(r); return 1;                                                   // MOV A,Rr
        case 0xF0: case 0xF1: _a = Indirect(i); return 1;                           // MOV A,@Ri
        case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
            Reg(r) = _a; return 1;                                                   // MOV Rr,A
        case 0xA0: case 0xA1: Indirect(i) = _a; return 1;                           // MOV @Ri,A
        case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
            Reg(r) = Fetch(); return 2;                                              // MOV Rr,#data
        case 0xB0: case 0xB1:                                                        // MOV @Ri,#data
        {
            const uint8_t value = Fetch();
            Indirect(i) = value;
            return 2;
        }
        case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
        {
            const uint8_t t = Reg(r);                                                // XCH A,Rr
            Reg(r) = _a;
            _a = t;
            return 1;
        }
        case 0x20: case 0x21:                                                        // XCH A,@Ri
        {
            uint8_t& cell = Indirect(i);
            const uint8_t t = cell;
            cell = _a;
            _a = t;
            return 1;
        }
        case 0x30: case 0x31:                                                        // XCHD A,@Ri
        {
            uint8_t& cell = Indirect(i);
            const uint8_t t = cell;
            cell = static_cast<uint8_t>((t & 0xF0) | (_a & 0x0F));
            _a = static_cast<uint8_t>((_a & 0xF0) | (t & 0x0F));
            return 1;
        }
        case 0xC7: _a = Psw(); return 1;                                             // MOV A,PSW
        case 0xD7: SetPsw(_a); return 1;                                             // MOV PSW,A
        case 0xA3: _a = Code(static_cast<uint16_t>((_pc & 0x0F00) | _a)); return 2;  // MOVP A,@A
        case 0xE3: _a = Code(static_cast<uint16_t>(0x0300 | _a)); return 2;          // MOVP3 A,@A
        case 0x80: case 0x81: _a = _bus.movxRead ? _bus.movxRead(Reg(i)) : 0xFF; return 2;   // MOVX A,@Ri
        case 0x90: case 0x91:                                                        // MOVX @Ri,A
            if (_bus.movxWrite)
                _bus.movxWrite(Reg(i), _a);
            return 2;
        // endregion </Registers and data moves>

        // region <Flags>
        case 0x97: SetCarry(false); return 1;                                        // CLR C
        case 0xA7: SetCarry(!Carry()); return 1;                                     // CPL C
        case 0x85: _psw = static_cast<uint8_t>(_psw & ~kPswF0); return 1;            // CLR F0
        case 0x95: _psw = static_cast<uint8_t>(_psw ^ kPswF0); return 1;             // CPL F0
        case 0xA5: _f1 = false; return 1;                                            // CLR F1
        case 0xB5: _f1 = !_f1; return 1;                                             // CPL F1
        // endregion </Flags>

        // region <Branches>
        case 0x04: case 0x24: case 0x44: case 0x64: case 0x84: case 0xA4: case 0xC4: case 0xE4:   // JMP addr
        {
            const uint8_t low = Fetch();
            _pc = LongTarget(op, low);
            return 2;
        }
        case 0x14: case 0x34: case 0x54: case 0x74: case 0x94: case 0xB4: case 0xD4: case 0xF4:   // CALL addr
        {
            const uint8_t low = Fetch();
            Push();
            _pc = LongTarget(op, low);
            return 2;
        }
        case 0x83: Pop(false); return 2;                                             // RET
        case 0x93: Pop(true); _inIsr = false; return 2;                              // RETR
        case 0xB3:                                                                   // JMPP @A
        {
            const uint16_t page = static_cast<uint16_t>(_pc & 0x0F00);
            _pc = static_cast<uint16_t>(page | Code(static_cast<uint16_t>(page | _a)));
            return 2;
        }
        case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0xEC: case 0xED: case 0xEE: case 0xEF:   // DJNZ Rr,addr
            JumpIf(--Reg(r) != 0);
            return 2;
        case 0x12: case 0x32: case 0x52: case 0x72: case 0x92: case 0xB2: case 0xD2: case 0xF2:   // JBb addr
            JumpIf(((_a >> (op >> 5)) & 1) != 0);
            return 2;
        case 0xF6: JumpIf(Carry()); return 2;                                        // JC
        case 0xE6: JumpIf(!Carry()); return 2;                                       // JNC
        case 0xC6: JumpIf(_a == 0); return 2;                                        // JZ
        case 0x96: JumpIf(_a != 0); return 2;                                        // JNZ
        case 0x36: JumpIf(_t0); return 2;                                            // JT0
        case 0x26: JumpIf(!_t0); return 2;                                           // JNT0
        case 0x56: JumpIf(_t1); return 2;                                            // JT1
        case 0x46: JumpIf(!_t1); return 2;                                           // JNT1
        case 0xB6: JumpIf((_psw & kPswF0) != 0); return 2;                           // JF0
        case 0x76: JumpIf(_f1); return 2;                                            // JF1
        case 0x86: JumpIf(!_int); return 2;                                          // JNI
        case 0x16:                                                                   // JTF: tests and clears the timer flag
        {
            const bool flag = _tf;
            _tf = false;
            JumpIf(flag);
            return 2;
        }
        // endregion </Branches>

        // region <Control>
        case 0x05: _ie = true; return 1;                                             // EN I
        case 0x15: _ie = false; return 1;                                            // DIS I
        case 0x25: _tcntie = true; return 1;                                         // EN TCNTI
        case 0x35: _tcntie = false; _timerPending = false; return 1;                 // DIS TCNTI
        case 0xC5: _psw = static_cast<uint8_t>(_psw & ~kPswBs); return 1;            // SEL RB0
        case 0xD5: _psw = static_cast<uint8_t>(_psw | kPswBs); return 1;             // SEL RB1
        case 0xE5: _mb = false; return 1;                                            // SEL MB0
        case 0xF5: _mb = true; return 1;                                             // SEL MB1
        case 0x75: return 1;                                                         // ENT0 CLK (clock output on T0)
        // endregion </Control>

        // region <Timer / counter>
        case 0x42: _a = _timer; return 1;                                            // MOV A,T
        case 0x62: _timer = _a; return 1;                                            // MOV T,A
        case 0x55: _timerMode = TimerMode::Timer; _prescaler = 0; return 1;          // STRT T
        case 0x45: _timerMode = TimerMode::Counter; return 1;                        // STRT CNT
        case 0x65: _timerMode = TimerMode::Stopped; return 1;                        // STOP TCNT
        // endregion </Timer / counter>

        // region <Input / output>
        case 0x08: _a = _bus.busIn ? _bus.busIn() : 0xFF; return 2;                  // INS A,BUS
        case 0x02: WritePort(0, _a); return 2;                                       // OUTL BUS,A
        case 0x88: WritePort(0, static_cast<uint8_t>(_latch[0] | Fetch())); return 2;   // ORL BUS,#data
        case 0x98: WritePort(0, static_cast<uint8_t>(_latch[0] & Fetch())); return 2;   // ANL BUS,#data
        case 0x09: _a = ReadPort(1); return 2;                                       // IN A,P1
        case 0x0A: _a = ReadPort(2); return 2;                                       // IN A,P2
        case 0x39: WritePort(1, _a); return 2;                                       // OUTL P1,A
        case 0x3A: WritePort(2, _a); return 2;                                       // OUTL P2,A
        case 0x89: WritePort(1, static_cast<uint8_t>(_latch[1] | Fetch())); return 2;   // ORL P1,#data
        case 0x8A: WritePort(2, static_cast<uint8_t>(_latch[2] | Fetch())); return 2;   // ORL P2,#data
        case 0x99: WritePort(1, static_cast<uint8_t>(_latch[1] & Fetch())); return 2;   // ANL P1,#data
        case 0x9A: WritePort(2, static_cast<uint8_t>(_latch[2] & Fetch())); return 2;   // ANL P2,#data
        case 0x0C: case 0x0D: case 0x0E: case 0x0F: Expander(4 + (op & 3), 0); return 2;   // MOVD A,Pp
        case 0x3C: case 0x3D: case 0x3E: case 0x3F: Expander(4 + (op & 3), 1); return 2;   // MOVD Pp,A
        case 0x8C: case 0x8D: case 0x8E: case 0x8F: Expander(4 + (op & 3), 2); return 2;   // ORLD Pp,A
        case 0x9C: case 0x9D: case 0x9E: case 0x9F: Expander(4 + (op & 3), 3); return 2;   // ANLD Pp,A
        // endregion </Input / output>

        // NOP and the undefined opcodes (01 06 0B 22 33 38 3B 63 66 73 82 87 8B 9B A2 A6 B7 C0..C3 D6 E0..E2 F3):
        // one machine cycle, nothing else
        default: return 1;
    }
}

void Mcs48::SaveState(State& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.clock = _clock;
    out.instructions = _instructions;
    out.pc = _pc;
    out.a = _a;
    out.psw = _psw;
    std::memcpy(out.ram, _ram.data(), sizeof(out.ram));
    std::memcpy(out.latch, _latch.data(), sizeof(out.latch));
    std::memcpy(out.pins, _pins.data(), sizeof(out.pins));
    out.f1 = _f1 ? 1 : 0;
    out.mb = _mb ? 1 : 0;
    out.ie = _ie ? 1 : 0;
    out.tcntie = _tcntie ? 1 : 0;
    out.inIsr = _inIsr ? 1 : 0;
    out.tf = _tf ? 1 : 0;
    out.timerPending = _timerPending ? 1 : 0;
    out.timerMode = static_cast<uint8_t>(_timerMode);
    out.timer = _timer;
    out.prescaler = _prescaler;
    out.t0 = _t0 ? 1 : 0;
    out.t1 = _t1 ? 1 : 0;
    out.intPin = _int ? 1 : 0;
}

void Mcs48::LoadState(const State& in)
{
    _clock = in.clock;
    _instructions = in.instructions;
    _pc = static_cast<uint16_t>(in.pc & 0x0FFF);
    _a = in.a;
    _psw = static_cast<uint8_t>(in.psw & 0xF7);
    std::memcpy(_ram.data(), in.ram, sizeof(in.ram));
    std::memcpy(_latch.data(), in.latch, sizeof(in.latch));
    std::memcpy(_pins.data(), in.pins, sizeof(in.pins));
    _f1 = in.f1 != 0;
    _mb = in.mb != 0;
    _ie = in.ie != 0;
    _tcntie = in.tcntie != 0;
    _inIsr = in.inIsr != 0;
    _tf = in.tf != 0;
    _timerPending = in.timerPending != 0;
    _timerMode = in.timerMode <= 2 ? static_cast<TimerMode>(in.timerMode) : TimerMode::Stopped;
    _timer = in.timer;
    _prescaler = in.prescaler;
    _t0 = in.t0 != 0;
    _t1 = in.t1 != 0;
    _int = in.intPin != 0;
}

}  // namespace mcs48
