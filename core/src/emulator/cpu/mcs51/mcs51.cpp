#include "emulator/cpu/mcs51/mcs51.h"

#include <cstring>

namespace mcs51
{

namespace
{
// Machine cycles per opcode (Intel MCS-51 user's manual, instruction set
// summary): 1 unless listed here
constexpr std::array<uint8_t, 256> MakeCycles()
{
    std::array<uint8_t, 256> c{};
    for (auto& v : c)
        v = 1;
    const uint8_t two[] = {
        0x01, 0x21, 0x41, 0x61, 0x81, 0xA1, 0xC1, 0xE1,   // AJMP
        0x11, 0x31, 0x51, 0x71, 0x91, 0xB1, 0xD1, 0xF1,   // ACALL
        0x02, 0x12, 0x22, 0x32,                           // LJMP, LCALL, RET, RETI
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,   // JBC, JB, JNB, JC, JNC, JZ, JNZ, SJMP
        0x43, 0x53, 0x63, 0x72, 0x73, 0x75,               // ORL/ANL/XRL dir,#; ORL C,bit; JMP @A+DPTR; MOV dir,#
        0x82, 0x83, 0x85, 0x86, 0x87,                     // ANL C,bit; MOVC A,@A+PC; MOV dir,dir; MOV dir,@Ri
        0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,   // MOV dir,Rn
        0x90, 0x92, 0x93, 0xA0, 0xA3, 0xA6, 0xA7,         // MOV DPTR; MOV bit,C; MOVC A,@A+DPTR; ORL C,/bit; INC DPTR; MOV @Ri,dir
        0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,   // MOV Rn,dir
        0xB0, 0xB4, 0xB5, 0xB6, 0xB7,                     // ANL C,/bit; CJNE
        0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,   // CJNE Rn
        0xC0, 0xD0, 0xD5,                                 // PUSH, POP, DJNZ dir
        0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,   // DJNZ Rn
        0xE0, 0xE2, 0xE3, 0xF0, 0xF2, 0xF3,               // MOVX
    };
    for (uint8_t op : two)
        c[op] = 2;
    c[0x84] = 4;   // DIV AB
    c[0xA4] = 4;   // MUL AB
    return c;
}
constexpr std::array<uint8_t, 256> kCycles = MakeCycles();

bool Parity(uint8_t v)
{
    v ^= static_cast<uint8_t>(v >> 4);
    v ^= static_cast<uint8_t>(v >> 2);
    v ^= static_cast<uint8_t>(v >> 1);
    return (v & 1) != 0;
}

constexpr uint8_t S(uint8_t sfr) { return static_cast<uint8_t>(sfr & 0x7F); }

// TCON bits
constexpr uint8_t kIt0 = 0x01, kIe0 = 0x02, kIt1 = 0x04, kIe1 = 0x08, kTr0 = 0x10, kTf0 = 0x20, kTr1 = 0x40,
                  kTf1 = 0x80;
// SCON bits
constexpr uint8_t kRi = 0x01, kTi = 0x02, kRb8 = 0x04, kTb8 = 0x08, kRen = 0x10, kSm2 = 0x20;
// T2CON bits
constexpr uint8_t kCpRl2 = 0x01, kCT2 = 0x02, kTr2 = 0x04, kExen2 = 0x08, kTclk = 0x10, kRclk = 0x20,
                  kExf2 = 0x40, kTf2 = 0x80;
}  // namespace

Mcs51::Mcs51(Variant variant) : _variant(variant)
{
    Reset();
}

void Mcs51::Reset()
{
    // SFR reset values (user's manual table "SFR reset values"); RAM keeps its contents
    _sfr.fill(0x00);
    _sfr[S(kSp)] = 0x07;
    for (uint8_t port : {kP0, kP1, kP2, kP3})
        _sfr[S(port)] = 0xFF;
    _pc = 0;
    _inService = 0;
    _blockOne = false;
    _idle = false;
    _powerDown = false;
    _sbufIn = 0;
    _txBusy = false;
    _txTicks = _txTicksToTi = 0;
    _t1Half = false;
    _t2Prescale = 0;
    _visibleAt.fill(0);
    _lastRequests = 0;
    if (_bus.portOut)
    {
        for (int p = 0; p < 4; ++p)
            _bus.portOut(p, 0xFF);
    }
}

uint8_t Mcs51::Psw() const
{
    const uint8_t a = _sfr[S(kAcc)];
    return static_cast<uint8_t>((_sfr[S(kPsw)] & 0xFE) | (Parity(a) ? 1 : 0));
}

/// region <Memory spaces>

uint8_t Mcs51::ReadIndirect(uint8_t address) const
{
    if (_variant == Variant::I8051 && address >= 0x80)
        return 0xFF;   // no upper RAM on the 8051
    return _ram[address];
}

void Mcs51::WriteIndirect(uint8_t address, uint8_t value)
{
    if (_variant == Variant::I8051 && address >= 0x80)
        return;
    _ram[address] = value;
}

uint8_t Mcs51::ReadDirect(uint8_t address, bool readModifyWrite)
{
    if (address < 0x80)
        return _ram[address];
    return ReadSfr(address, readModifyWrite);
}

void Mcs51::WriteDirect(uint8_t address, uint8_t value)
{
    if (address < 0x80)
        _ram[address] = value;
    else
        WriteSfr(address, value);
}

uint8_t Mcs51::ReadSfr(uint8_t address, bool readModifyWrite)
{
    switch (address)
    {
        case kP0:
        case kP1:
        case kP2:
        case kP3:
        {
            // Pins read as latch AND the outside world; read-modify-write reads the latch
            const uint8_t latch = _sfr[S(address)];
            return readModifyWrite ? latch : static_cast<uint8_t>(latch & _pins[(address >> 4) & 3]);
        }
        case kPsw: return Psw();
        case kSbuf: return _sbufIn;
        default: return _sfr[S(address)];
    }
}

void Mcs51::WriteSfr(uint8_t address, uint8_t value)
{
    switch (address)
    {
        case kP0:
        case kP1:
        case kP2:
        case kP3:
        {
            const uint8_t before = _sfr[S(address)];
            _sfr[S(address)] = value;
            if (address == kP3 && before != value)
                Port3Changed(static_cast<uint8_t>(before & _pins[3]), static_cast<uint8_t>(value & _pins[3]));
            if (before != value && _bus.portOut)
                _bus.portOut((address >> 4) & 3, value);
            return;
        }
        case kSbuf:
            StartTransmit(value);
            return;
        case kIe:
        case kIp:
            _sfr[S(address)] = value;
            _blockOne = true;   // one more instruction runs before an interrupt is taken
            return;
        case kPcon:
            _sfr[S(address)] = value;
            if (value & 0x02)
                _powerDown = true;
            else if (value & 0x01)
                _idle = true;
            return;
        case kPsw:
            _sfr[S(address)] = static_cast<uint8_t>(value & 0xFE);   // P is computed
            return;
        default:
            _sfr[S(address)] = value;
            return;
    }
}

bool Mcs51::ReadBit(uint8_t bit, bool readModifyWrite)
{
    if (bit < 0x80)
        return (_ram[0x20 + (bit >> 3)] >> (bit & 7)) & 1;
    const uint8_t address = static_cast<uint8_t>(bit & 0xF8);
    return (ReadSfr(address, readModifyWrite) >> (bit & 7)) & 1;
}

void Mcs51::WriteBit(uint8_t bit, bool value)
{
    const uint8_t mask = static_cast<uint8_t>(1u << (bit & 7));
    if (bit < 0x80)
    {
        uint8_t& b = _ram[0x20 + (bit >> 3)];
        b = static_cast<uint8_t>(value ? (b | mask) : (b & ~mask));
        return;
    }
    // A bit write is a read-modify-write of the whole register (ports: the latch)
    const uint8_t address = static_cast<uint8_t>(bit & 0xF8);
    const uint8_t current = ReadSfr(address, true);
    if (address == kSbuf)
        return;   // not bit-addressable
    WriteSfr(address, static_cast<uint8_t>(value ? (current | mask) : (current & ~mask)));
}

void Mcs51::Push(uint8_t v)
{
    const uint8_t sp = static_cast<uint8_t>(_sfr[S(kSp)] + 1);
    _sfr[S(kSp)] = sp;
    WriteIndirect(sp, v);
}

uint8_t Mcs51::Pop()
{
    const uint8_t sp = _sfr[S(kSp)];
    _sfr[S(kSp)] = static_cast<uint8_t>(sp - 1);
    return ReadIndirect(sp);
}

/// endregion </Memory spaces>

/// region <Arithmetic>

void Mcs51::Add(uint8_t value, bool withCarry)
{
    const uint8_t a = A();
    const unsigned c = (withCarry && Cy()) ? 1u : 0u;
    const unsigned sum = static_cast<unsigned>(a) + value + c;
    const bool ac = ((a & 0x0F) + (value & 0x0F) + c) > 0x0F;
    const bool c6 = ((a & 0x7F) + (value & 0x7F) + c) > 0x7F;
    const bool c7 = sum > 0xFF;
    uint8_t psw = static_cast<uint8_t>(PswRef() & ~(0x80 | 0x40 | 0x04));
    if (c7)
        psw |= 0x80;
    if (ac)
        psw |= 0x40;
    if (c6 != c7)
        psw |= 0x04;
    PswRef() = psw;
    A() = static_cast<uint8_t>(sum);
}

void Mcs51::Subb(uint8_t value)
{
    const uint8_t a = A();
    const int c = Cy() ? 1 : 0;
    const int diff = static_cast<int>(a) - value - c;
    const bool ac = (static_cast<int>(a & 0x0F) - (value & 0x0F) - c) < 0;
    const bool b6 = (static_cast<int>(a & 0x7F) - (value & 0x7F) - c) < 0;
    const bool b7 = diff < 0;
    uint8_t psw = static_cast<uint8_t>(PswRef() & ~(0x80 | 0x40 | 0x04));
    if (b7)
        psw |= 0x80;
    if (ac)
        psw |= 0x40;
    if (b6 != b7)
        psw |= 0x04;
    PswRef() = psw;
    A() = static_cast<uint8_t>(diff);
}

/// endregion </Arithmetic>

void Mcs51::Run(uint64_t untilClock)
{
    _stop = false;
    while (_clock < untilClock && !_stop)
    {
        if (_powerDown)
        {
            _clock = untilClock;   // only a reset ends power-down
            break;
        }
        int cycles = 0;
        if (!_blockOne)
            cycles = ServiceInterrupts();
        _blockOne = false;
        if (cycles == 0)
        {
            if (_idle)
                cycles = 1;   // the CPU sleeps, the peripherals run
            else
            {
                cycles = Execute();
                ++_instructions;
            }
        }
        _clock += static_cast<uint64_t>(cycles) * 12u;
        Tick(cycles);
        if (_resetPending)
        {
            _resetPending = false;
            Reset();
        }
    }
}

uint8_t Mcs51::RequestBits() const
{
    const uint8_t tcon = _sfr[S(kTcon)];
    const uint8_t scon = _sfr[S(kScon)];
    const uint8_t t2con = _sfr[S(kT2con)];
    uint8_t bits = 0;
    if (tcon & kIe0) bits |= 0x01;
    if (tcon & kTf0) bits |= 0x02;
    if (tcon & kIe1) bits |= 0x04;
    if (tcon & kTf1) bits |= 0x08;
    if (scon & (kRi | kTi)) bits |= 0x10;
    if (_variant == Variant::I8052 && (t2con & (kTf2 | kExf2))) bits |= 0x20;
    return bits;
}

void Mcs51::NoteRequests(uint64_t visibleAt)
{
    const uint8_t now = RequestBits();
    const uint8_t rose = static_cast<uint8_t>(now & ~_lastRequests);
    for (int n = 0; n < 6; ++n)
    {
        if (rose & (1u << n))
            _visibleAt[n] = visibleAt;
    }
    _lastRequests = now;
}

int Mcs51::ServiceInterrupts()
{
    NoteRequests(_clock + 12);   // flags set by software since the last cycle
    const uint8_t ie = _sfr[S(kIe)];
    if (!(ie & 0x80) || (_inService & 2))
        return 0;
    // Requests in the polling order IE0, TF0, IE1, TF1, serial, TF2, each
    // once it has been latched and polled
    const uint8_t bits = _lastRequests;
    bool request[6];
    for (int n = 0; n < 6; ++n)
        request[n] = (bits & (1u << n)) && _clock >= _visibleAt[n];
    static constexpr uint16_t kVectors[6] = {kVectorIe0, kVectorTf0, kVectorIe1, kVectorTf1, kVectorSerial, kVectorTf2};
    const uint8_t ip = _sfr[S(kIp)];
    for (int level = 1; level >= 0; --level)
    {
        if (level == 0 && (_inService & 1))
            break;   // a low-level ISR runs: only high-level requests get through
        for (int n = 0; n < 6; ++n)
        {
            const uint8_t bit = static_cast<uint8_t>(1u << n);
            if (!request[n] || !(ie & bit) || (((ip & bit) != 0) != (level == 1)))
                continue;
            _idle = false;
            // The hardware clears the edge flags and the timer 0 / 1 overflow flags
            uint8_t& tc = _sfr[S(kTcon)];
            if (n == 0 && (tc & kIt0))
                tc = static_cast<uint8_t>(tc & ~kIe0);
            if (n == 1)
                tc = static_cast<uint8_t>(tc & ~kTf0);
            if (n == 2 && (tc & kIt1))
                tc = static_cast<uint8_t>(tc & ~kIe1);
            if (n == 3)
                tc = static_cast<uint8_t>(tc & ~kTf1);
            Push(static_cast<uint8_t>(_pc & 0xFF));
            Push(static_cast<uint8_t>(_pc >> 8));
            _pc = kVectors[n];
            _inService = static_cast<uint8_t>(_inService | (level == 1 ? 2 : 1));
            NoteRequests(_clock);
            return 2;   // the hardware LCALL
        }
    }
    return 0;
}

/// region <Peripherals>

void Mcs51::SetPin(int port, int bit, bool level)
{
    port &= 3;
    const uint8_t mask = static_cast<uint8_t>(1u << (bit & 7));
    const uint8_t before = _pins[port];
    const uint8_t after = static_cast<uint8_t>(level ? (before | mask) : (before & ~mask));
    if (before == after)
        return;
    _pins[port] = after;
    struct Note
    {
        Mcs51* cpu;
        ~Note() { cpu->NoteRequests(cpu->_clock + 24); }
    } note{this};
    const uint8_t latch = _sfr[S(static_cast<uint8_t>(0x80 + 0x10 * port))];
    if (port == 3)
        Port3Changed(static_cast<uint8_t>(latch & before), static_cast<uint8_t>(latch & after));
    else if (port == 1 && _variant == Variant::I8052)
    {
        const uint8_t t2con = _sfr[S(kT2con)];
        const uint8_t b = static_cast<uint8_t>(latch & before), a = static_cast<uint8_t>(latch & after);
        // T2 (P1.0) counts falling edges in counter mode
        if ((b & 1) && !(a & 1) && (t2con & kTr2) && (t2con & kCT2) && !(t2con & (kRclk | kTclk)))
        {
            uint16_t t2 = static_cast<uint16_t>((_sfr[S(kTh2)] << 8) | _sfr[S(kTl2)]);
            if (++t2 == 0)
            {
                _sfr[S(kT2con)] |= kTf2;
                if (!(t2con & kCpRl2))
                    t2 = static_cast<uint16_t>((_sfr[S(kRcap2h)] << 8) | _sfr[S(kRcap2l)]);
            }
            _sfr[S(kTh2)] = static_cast<uint8_t>(t2 >> 8);
            _sfr[S(kTl2)] = static_cast<uint8_t>(t2);
        }
        // T2EX (P1.1) falling edge with EXEN2: capture or reload, EXF2
        if ((b & 2) && !(a & 2) && (t2con & kExen2))
        {
            _sfr[S(kT2con)] |= kExf2;
            if (!(t2con & (kRclk | kTclk)))
            {
                if (t2con & kCpRl2)
                {
                    _sfr[S(kRcap2h)] = _sfr[S(kTh2)];
                    _sfr[S(kRcap2l)] = _sfr[S(kTl2)];
                }
                else
                {
                    _sfr[S(kTh2)] = _sfr[S(kRcap2h)];
                    _sfr[S(kTl2)] = _sfr[S(kRcap2l)];
                }
            }
        }
    }
}

void Mcs51::Port3Changed(uint8_t before, uint8_t after)
{
    uint8_t& tcon = _sfr[S(kTcon)];
    // INT0 / INT1 falling edges (edge mode); level mode follows in Tick
    if ((before & 0x04) && !(after & 0x04) && (tcon & kIt0))
        tcon |= kIe0;
    if ((before & 0x08) && !(after & 0x08) && (tcon & kIt1))
        tcon |= kIe1;
    // T0 / T1 counter inputs
    const uint8_t tmod = _sfr[S(kTmod)];
    if ((before & 0x10) && !(after & 0x10) && (tmod & 0x04) && (tcon & kTr0))
    {
        const bool gate = (tmod & 0x08) != 0;
        if (!gate || (after & 0x04))
        {
            // One count on timer 0 (low half in mode 3)
            const int mode = tmod & 3;
            uint8_t& tl = _sfr[S(kTl0)];
            uint8_t& th = _sfr[S(kTh0)];
            if (mode == 0)
            {
                unsigned v = ((static_cast<unsigned>(th) << 5) | (tl & 0x1F)) + 1;
                if (v >= 0x2000)
                {
                    v = 0;
                    tcon |= kTf0;
                }
                th = static_cast<uint8_t>(v >> 5);
                tl = static_cast<uint8_t>((tl & 0xE0) | (v & 0x1F));
            }
            else if (mode == 1)
            {
                unsigned v = ((static_cast<unsigned>(th) << 8) | tl) + 1;
                if (v > 0xFFFF)
                {
                    v = 0;
                    tcon |= kTf0;
                }
                th = static_cast<uint8_t>(v >> 8);
                tl = static_cast<uint8_t>(v);
            }
            else
            {
                if (++tl == 0)
                {
                    tcon |= kTf0;
                    if (mode == 2)
                        tl = th;
                }
            }
        }
    }
    if ((before & 0x20) && !(after & 0x20) && (tmod & 0x40) && (tcon & kTr1) && (tmod & 0x03) != 0x03)
    {
        const bool gate = (tmod & 0x80) != 0;
        const int mode = (tmod >> 4) & 3;
        if ((!gate || (after & 0x08)) && mode != 3)
        {
            uint8_t& tl = _sfr[S(kTl1)];
            uint8_t& th = _sfr[S(kTh1)];
            bool overflow = false;
            if (mode == 0)
            {
                unsigned v = ((static_cast<unsigned>(th) << 5) | (tl & 0x1F)) + 1;
                overflow = v >= 0x2000;
                v &= 0x1FFF;
                th = static_cast<uint8_t>(v >> 5);
                tl = static_cast<uint8_t>((tl & 0xE0) | (v & 0x1F));
            }
            else if (mode == 1)
            {
                unsigned v = ((static_cast<unsigned>(th) << 8) | tl) + 1;
                overflow = v > 0xFFFF;
                th = static_cast<uint8_t>(v >> 8);
                tl = static_cast<uint8_t>(v);
            }
            else if (++tl == 0)
            {
                overflow = true;
                tl = th;
            }
            if (overflow)
                TimerOverflow1();
        }
    }
}

void Mcs51::TimerOverflow1()
{
    // Timer 0 in mode 3 owns TF1 (through TH0): timer 1 then only clocks the serial port
    if ((_sfr[S(kTmod)] & 0x03) != 0x03)
        _sfr[S(kTcon)] |= kTf1;
    // Baud divider: SMOD = 0 halves the overflow rate
    const bool smod = (_sfr[S(kPcon)] & 0x80) != 0;
    if (smod)
        BaudTick(false);
    else
    {
        _t1Half = !_t1Half;
        if (!_t1Half)
            BaudTick(false);
    }
}

void Mcs51::Tick(int cycles)
{
    const uint64_t start = _clock - static_cast<uint64_t>(cycles) * 12u;
    for (int c = 0; c < cycles; ++c)
    {
        // Level-triggered external interrupts follow the pins
        uint8_t& tcon = _sfr[S(kTcon)];
        const uint8_t p3 = static_cast<uint8_t>(_sfr[S(kP3)] & _pins[3]);
        if (!(tcon & kIt0))
            tcon = static_cast<uint8_t>((p3 & 0x04) ? (tcon & ~kIe0) : (tcon | kIe0));
        if (!(tcon & kIt1))
            tcon = static_cast<uint8_t>((p3 & 0x08) ? (tcon & ~kIe1) : (tcon | kIe1));

        TickTimer01(1);
        if (_variant == Variant::I8052)
            TickTimer2(1);

        // Fixed-rate serial modes count oscillator clocks
        if (_txBusy)
        {
            const int mode = _sfr[S(kScon)] >> 6;
            if (mode == 0 || mode == 2)
            {
                const uint32_t step = 12;
                const uint32_t before = _txTicks;
                _txTicks = _txTicks > step ? _txTicks - step : 0;
                if (_txTicksToTi && before > _txTicksToTi && _txTicks <= _txTicksToTi)
                    _sfr[S(kScon)] |= kTi;
                if (_txTicks == 0)
                {
                    if (mode == 0)
                        _sfr[S(kScon)] |= kTi;
                    _txBusy = false;
                    if (_bus.serialOut)
                        _bus.serialOut(_txByte, _txNinth);
                }
            }
        }
        // Raised in this cycle: polled in the next one
        NoteRequests(start + 12u * static_cast<uint64_t>(c + 1) + 12u);
    }
}

void Mcs51::TickTimer01(int cycles)
{
    const uint8_t tmod = _sfr[S(kTmod)];
    uint8_t& tcon = _sfr[S(kTcon)];
    const uint8_t p3 = static_cast<uint8_t>(_sfr[S(kP3)] & _pins[3]);
    for (int c = 0; c < cycles; ++c)
    {
        // Timer 0 (timer mode; counter mode counts T0 edges in Port3Changed)
        const int mode0 = tmod & 3;
        const bool run0 = (tcon & kTr0) && (!(tmod & 0x08) || (p3 & 0x04));
        if (run0 && !(tmod & 0x04))
        {
            uint8_t& tl = _sfr[S(kTl0)];
            uint8_t& th = _sfr[S(kTh0)];
            switch (mode0)
            {
                case 0:
                {
                    unsigned v = ((static_cast<unsigned>(th) << 5) | (tl & 0x1F)) + 1;
                    if (v >= 0x2000)
                    {
                        v = 0;
                        tcon |= kTf0;
                    }
                    th = static_cast<uint8_t>(v >> 5);
                    tl = static_cast<uint8_t>((tl & 0xE0) | (v & 0x1F));
                    break;
                }
                case 1:
                {
                    unsigned v = ((static_cast<unsigned>(th) << 8) | tl) + 1;
                    if (v > 0xFFFF)
                    {
                        v = 0;
                        tcon |= kTf0;
                    }
                    th = static_cast<uint8_t>(v >> 8);
                    tl = static_cast<uint8_t>(v);
                    break;
                }
                case 2:
                    if (++tl == 0)
                    {
                        tl = th;
                        tcon |= kTf0;
                    }
                    break;
                default:   // mode 3: TL0 here; TH0 below
                    if (++tl == 0)
                        tcon |= kTf0;
                    break;
            }
        }
        if (mode0 == 3 && (tcon & kTr1))
        {
            // TH0 in mode 3: an 8-bit timer run by TR1, overflowing into TF1
            if (++_sfr[S(kTh0)] == 0)
                tcon |= kTf1;
        }

        // Timer 1 (stops in mode 3; with timer 0 in mode 3 it runs without TF1)
        const int mode1 = (tmod >> 4) & 3;
        const bool run1 = (mode0 == 3 || (tcon & kTr1)) && (!(tmod & 0x80) || (p3 & 0x08));
        if (run1 && !(tmod & 0x40) && mode1 != 3)
        {
            uint8_t& tl = _sfr[S(kTl1)];
            uint8_t& th = _sfr[S(kTh1)];
            bool overflow = false;
            if (mode1 == 0)
            {
                unsigned v = ((static_cast<unsigned>(th) << 5) | (tl & 0x1F)) + 1;
                overflow = v >= 0x2000;
                v &= 0x1FFF;
                th = static_cast<uint8_t>(v >> 5);
                tl = static_cast<uint8_t>((tl & 0xE0) | (v & 0x1F));
            }
            else if (mode1 == 1)
            {
                unsigned v = ((static_cast<unsigned>(th) << 8) | tl) + 1;
                overflow = v > 0xFFFF;
                th = static_cast<uint8_t>(v >> 8);
                tl = static_cast<uint8_t>(v);
            }
            else if (++tl == 0)
            {
                overflow = true;
                tl = th;
            }
            if (overflow)
                TimerOverflow1();
        }
    }
}

void Mcs51::TickTimer2(int cycles)
{
    const uint8_t t2con = _sfr[S(kT2con)];
    if (!(t2con & kTr2) || (t2con & kCT2))
        return;   // stopped, or counting T2 pin edges (SetPin)
    const bool baud = (t2con & (kRclk | kTclk)) != 0;
    // Baud-rate mode counts at fosc / 2 (6 per machine cycle), the other modes once per machine cycle
    const int counts = baud ? cycles * 6 : cycles;
    for (int n = 0; n < counts; ++n)
    {
        uint16_t t2 = static_cast<uint16_t>((_sfr[S(kTh2)] << 8) | _sfr[S(kTl2)]);
        ++t2;
        if (t2 == 0)
        {
            if (baud)
            {
                t2 = static_cast<uint16_t>((_sfr[S(kRcap2h)] << 8) | _sfr[S(kRcap2l)]);
                BaudTick(true);
            }
            else
            {
                _sfr[S(kT2con)] |= kTf2;
                if (!(t2con & kCpRl2))
                    t2 = static_cast<uint16_t>((_sfr[S(kRcap2h)] << 8) | _sfr[S(kRcap2l)]);
            }
        }
        _sfr[S(kTh2)] = static_cast<uint8_t>(t2 >> 8);
        _sfr[S(kTl2)] = static_cast<uint8_t>(t2);
    }
}

void Mcs51::BaudTick(bool fromTimer2)
{
    // A tick of the 1/16-bit clock for the variable-rate modes 1 and 3
    if (!_txBusy)
        return;
    const uint8_t scon = _sfr[S(kScon)];
    const int mode = scon >> 6;
    if (mode != 1 && mode != 3)
        return;
    const bool txFromTimer2 = _variant == Variant::I8052 && (_sfr[S(kT2con)] & kTclk);
    if (txFromTimer2 != fromTimer2)
        return;
    if (_txTicks > 0)
        --_txTicks;
    if (_txTicks == _txTicksToTi)
        _sfr[S(kScon)] |= kTi;   // TI rises as the stop bit starts
    if (_txTicks == 0)
    {
        _txBusy = false;
        if (_bus.serialOut)
            _bus.serialOut(_txByte, _txNinth);
    }
}

void Mcs51::StartTransmit(uint8_t byte)
{
    // A write while a frame is on the line restarts it with the new byte (the
    // shift register reloads; software waits for TI)
    const uint8_t scon = _sfr[S(kScon)];
    const int mode = scon >> 6;
    _txByte = byte;
    _txNinth = (scon & kTb8) != 0;
    _txBusy = true;
    switch (mode)
    {
        case 0:   // 8 bits at fosc / 12
            _txTicks = 8 * 12;
            _txTicksToTi = 0;
            break;
        case 2:   // 11 bits at fosc / 64 (SMOD: / 32); TI at the stop bit
        {
            const uint32_t bit = (_sfr[S(kPcon)] & 0x80) ? 32u : 64u;
            _txTicks = 11 * bit;
            _txTicksToTi = bit;
            break;
        }
        case 1:   // 10 bits, 16 ticks each; TI as the stop bit starts
            _txTicks = 10 * 16;
            _txTicksToTi = 16;
            break;
        default:  // mode 3: 11 bits
            _txTicks = 11 * 16;
            _txTicksToTi = 16;
            break;
    }
}

bool Mcs51::SerialIn(uint8_t byte, bool ninth)
{
    struct Note
    {
        Mcs51* cpu;
        ~Note() { cpu->NoteRequests(cpu->_clock + 12); }
    } note{this};
    uint8_t& scon = _sfr[S(kScon)];
    const int mode = scon >> 6;
    if (!(scon & kRen) || (scon & kRi) || mode == 0)
        return false;   // the frame is lost (the receiver is off, or RI was not cleared)
    if (mode == 1)
    {
        // RB8 = the stop bit; with SM2 a missing stop bit keeps RI low
        if ((scon & kSm2) && !ninth)
            return false;
        _sbufIn = byte;
        scon = static_cast<uint8_t>((scon & ~kRb8) | (ninth ? kRb8 : 0) | kRi);
        return true;
    }
    if ((scon & kSm2) && !ninth)
        return false;   // multiprocessor mode: only address frames
    _sbufIn = byte;
    scon = static_cast<uint8_t>((scon & ~kRb8) | (ninth ? kRb8 : 0) | kRi);
    return true;
}

uint64_t Mcs51::SerialBitClocks(bool receive) const
{
    const uint8_t scon = _sfr[S(kScon)];
    const int mode = scon >> 6;
    const bool smod = (_sfr[S(kPcon)] & 0x80) != 0;
    if (mode == 0)
        return 12;
    if (mode == 2)
        return smod ? 32 : 64;
    const uint8_t t2con = _sfr[S(kT2con)];
    if (_variant == Variant::I8052 && (t2con & (receive ? kRclk : kTclk)))
    {
        if (!(t2con & kTr2) || (t2con & kCT2))
            return 0;
        const uint32_t reload = static_cast<uint32_t>((_sfr[S(kRcap2h)] << 8) | _sfr[S(kRcap2l)]);
        return 16ull * 2ull * (65536u - reload);
    }
    const uint8_t tmod = _sfr[S(kTmod)];
    const bool t1runs = ((_sfr[S(kTcon)] & kTr1) || (tmod & 0x03) == 0x03) && !(tmod & 0x40);
    if (!t1runs)
        return 0;
    uint64_t overflowCycles = 0;
    switch ((tmod >> 4) & 3)
    {
        case 0: overflowCycles = 8192; break;
        case 1: overflowCycles = 65536; break;
        case 2: overflowCycles = 256u - _sfr[S(kTh1)]; break;
        default: return 0;
    }
    return 16ull * overflowCycles * 12ull * (smod ? 1u : 2u);
}

int Mcs51::SerialFrameBits() const
{
    const int mode = _sfr[S(kScon)] >> 6;
    return mode == 0 ? 8 : (mode == 1 ? 10 : 11);
}

/// endregion </Peripherals>

/// region <Instructions>

int Mcs51::Execute()
{
    const uint8_t op = Fetch();
    const int cycles = kCycles[op];
    auto rel = [this](uint8_t offset) { _pc = static_cast<uint16_t>(_pc + static_cast<int8_t>(offset)); };

    // AJMP / ACALL: 11-bit address within the current 2 KB page
    if ((op & 0x1F) == 0x01 || (op & 0x1F) == 0x11)
    {
        const uint8_t low = Fetch();
        const uint16_t target = static_cast<uint16_t>((_pc & 0xF800) | ((op & 0xE0) << 3) | low);
        if (op & 0x10)
        {
            Push(static_cast<uint8_t>(_pc & 0xFF));
            Push(static_cast<uint8_t>(_pc >> 8));
        }
        _pc = target;
        return cycles;
    }

    // The register / indirect forms share the low nibble: 6, 7 = @R0, @R1; 8..F = R0..R7
    const int low = op & 0x0F;
    auto operand = [&]() -> uint8_t {
        if (low >= 8)
            return R(low - 8);
        return ReadIndirect(R(low - 6));
    };
    auto setOperand = [&](uint8_t v) {
        if (low >= 8)
            SetR(low - 8, v);
        else
            WriteIndirect(R(low - 6), v);
    };

    switch (op)
    {
        case 0x00: break;   // NOP
        case 0xA5: break;   // reserved: executes as a 1-cycle no-op

        case 0x02: { const uint8_t h = Fetch(); const uint8_t l = Fetch(); _pc = static_cast<uint16_t>((h << 8) | l); break; }
        case 0x12:
        {
            const uint8_t h = Fetch();
            const uint8_t l = Fetch();
            Push(static_cast<uint8_t>(_pc & 0xFF));
            Push(static_cast<uint8_t>(_pc >> 8));
            _pc = static_cast<uint16_t>((h << 8) | l);
            break;
        }
        case 0x22: { const uint8_t h = Pop(); const uint8_t l = Pop(); _pc = static_cast<uint16_t>((h << 8) | l); break; }
        case 0x32:
        {
            const uint8_t h = Pop();
            const uint8_t l = Pop();
            _pc = static_cast<uint16_t>((h << 8) | l);
            // Ends the highest-level service in progress
            if (_inService & 2)
                _inService = static_cast<uint8_t>(_inService & ~2);
            else
                _inService = static_cast<uint8_t>(_inService & ~1);
            _blockOne = true;
            break;
        }
        case 0x80: rel(Fetch()); break;   // SJMP
        case 0x73: _pc = static_cast<uint16_t>(Dptr() + A()); break;   // JMP @A+DPTR

        // Rotates
        case 0x03: A() = static_cast<uint8_t>((A() >> 1) | (A() << 7)); break;   // RR
        case 0x23: A() = static_cast<uint8_t>((A() << 1) | (A() >> 7)); break;   // RL
        case 0x13:   // RRC
        {
            const bool c = A() & 1;
            A() = static_cast<uint8_t>((A() >> 1) | (Cy() ? 0x80 : 0));
            SetCy(c);
            break;
        }
        case 0x33:   // RLC
        {
            const bool c = (A() & 0x80) != 0;
            A() = static_cast<uint8_t>((A() << 1) | (Cy() ? 1 : 0));
            SetCy(c);
            break;
        }

        // INC / DEC
        case 0x04: ++A(); break;
        case 0x14: --A(); break;
        case 0x05: { const uint8_t a = Fetch(); WriteDirect(a, static_cast<uint8_t>(ReadDirect(a, true) + 1)); break; }
        case 0x15: { const uint8_t a = Fetch(); WriteDirect(a, static_cast<uint8_t>(ReadDirect(a, true) - 1)); break; }
        case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F:
            setOperand(static_cast<uint8_t>(operand() + 1));
            break;
        case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
            setOperand(static_cast<uint8_t>(operand() - 1));
            break;
        case 0xA3: SetDptr(static_cast<uint16_t>(Dptr() + 1)); break;

        // Bit jumps
        case 0x10:   // JBC bit,rel (read-modify-write)
        {
            const uint8_t bit = Fetch();
            const uint8_t r = Fetch();
            if (ReadBit(bit, true))
            {
                WriteBit(bit, false);
                rel(r);
            }
            break;
        }
        case 0x20: { const uint8_t bit = Fetch(); const uint8_t r = Fetch(); if (ReadBit(bit)) rel(r); break; }    // JB
        case 0x30: { const uint8_t bit = Fetch(); const uint8_t r = Fetch(); if (!ReadBit(bit)) rel(r); break; }   // JNB
        case 0x40: { const uint8_t r = Fetch(); if (Cy()) rel(r); break; }    // JC
        case 0x50: { const uint8_t r = Fetch(); if (!Cy()) rel(r); break; }   // JNC
        case 0x60: { const uint8_t r = Fetch(); if (A() == 0) rel(r); break; }   // JZ
        case 0x70: { const uint8_t r = Fetch(); if (A() != 0) rel(r); break; }   // JNZ

        // ADD / ADDC / SUBB
        case 0x24: Add(Fetch(), false); break;
        case 0x25: Add(ReadDirect(Fetch()), false); break;
        case 0x26: case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
            Add(operand(), false);
            break;
        case 0x34: Add(Fetch(), true); break;
        case 0x35: Add(ReadDirect(Fetch()), true); break;
        case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F:
            Add(operand(), true);
            break;
        case 0x94: Subb(Fetch()); break;
        case 0x95: Subb(ReadDirect(Fetch())); break;
        case 0x96: case 0x97: case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
            Subb(operand());
            break;

        // ORL / ANL / XRL
        case 0x42: { const uint8_t a = Fetch(); WriteDirect(a, static_cast<uint8_t>(ReadDirect(a, true) | A())); break; }
        case 0x43: { const uint8_t a = Fetch(); const uint8_t d = Fetch(); WriteDirect(a, static_cast<uint8_t>(ReadDirect(a, true) | d)); break; }
        case 0x44: A() |= Fetch(); break;
        case 0x45: A() |= ReadDirect(Fetch()); break;
        case 0x46: case 0x47: case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
            A() |= operand();
            break;
        case 0x52: { const uint8_t a = Fetch(); WriteDirect(a, static_cast<uint8_t>(ReadDirect(a, true) & A())); break; }
        case 0x53: { const uint8_t a = Fetch(); const uint8_t d = Fetch(); WriteDirect(a, static_cast<uint8_t>(ReadDirect(a, true) & d)); break; }
        case 0x54: A() &= Fetch(); break;
        case 0x55: A() &= ReadDirect(Fetch()); break;
        case 0x56: case 0x57: case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            A() &= operand();
            break;
        case 0x62: { const uint8_t a = Fetch(); WriteDirect(a, static_cast<uint8_t>(ReadDirect(a, true) ^ A())); break; }
        case 0x63: { const uint8_t a = Fetch(); const uint8_t d = Fetch(); WriteDirect(a, static_cast<uint8_t>(ReadDirect(a, true) ^ d)); break; }
        case 0x64: A() ^= Fetch(); break;
        case 0x65: A() ^= ReadDirect(Fetch()); break;
        case 0x66: case 0x67: case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E: case 0x6F:
            A() ^= operand();
            break;

        // Carry-bit logic
        case 0x72: { const uint8_t b = Fetch(); SetCy(Cy() || ReadBit(b)); break; }     // ORL C,bit
        case 0xA0: { const uint8_t b = Fetch(); SetCy(Cy() || !ReadBit(b)); break; }    // ORL C,/bit
        case 0x82: { const uint8_t b = Fetch(); SetCy(Cy() && ReadBit(b)); break; }     // ANL C,bit
        case 0xB0: { const uint8_t b = Fetch(); SetCy(Cy() && !ReadBit(b)); break; }    // ANL C,/bit
        case 0xA2: SetCy(ReadBit(Fetch())); break;                                       // MOV C,bit
        case 0x92: WriteBit(Fetch(), Cy()); break;                                       // MOV bit,C
        case 0xB2: { const uint8_t b = Fetch(); WriteBit(b, !ReadBit(b, true)); break; } // CPL bit
        case 0xB3: SetCy(!Cy()); break;                                                  // CPL C
        case 0xC2: WriteBit(Fetch(), false); break;                                      // CLR bit
        case 0xC3: SetCy(false); break;                                                  // CLR C
        case 0xD2: WriteBit(Fetch(), true); break;                                       // SETB bit
        case 0xD3: SetCy(true); break;                                                   // SETB C

        // MOV
        case 0x74: A() = Fetch(); break;
        case 0x75: { const uint8_t a = Fetch(); WriteDirect(a, Fetch()); break; }
        case 0x76: case 0x77: case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F:
            setOperand(Fetch());
            break;
        case 0x85:   // MOV dir,dir: source first in the encoding
        {
            const uint8_t src = Fetch();
            const uint8_t dst = Fetch();
            WriteDirect(dst, ReadDirect(src));
            break;
        }
        case 0x86: case 0x87: case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F:
        {
            const uint8_t a = Fetch();
            WriteDirect(a, operand());
            break;
        }
        case 0x90: { const uint8_t h = Fetch(); const uint8_t l = Fetch(); SetDptr(static_cast<uint16_t>((h << 8) | l)); break; }
        case 0xA6: case 0xA7: case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
            setOperand(ReadDirect(Fetch()));
            break;
        case 0xE5: A() = ReadDirect(Fetch()); break;
        case 0xE6: case 0xE7: case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0xEC: case 0xED: case 0xEE: case 0xEF:
            A() = operand();
            break;
        case 0xF5: WriteDirect(Fetch(), A()); break;
        case 0xF6: case 0xF7: case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD: case 0xFE: case 0xFF:
            setOperand(A());
            break;

        // MOVC / MOVX
        case 0x83: A() = Code(static_cast<uint16_t>(_pc + A())); break;
        case 0x93: A() = Code(static_cast<uint16_t>(Dptr() + A())); break;
        case 0xE0: A() = _bus.movxRead ? _bus.movxRead(Dptr()) : 0xFF; break;
        case 0xE2: case 0xE3:
        {
            const uint16_t address = static_cast<uint16_t>((_sfr[S(kP2)] << 8) | R(op & 1));
            A() = _bus.movxRead ? _bus.movxRead(address) : 0xFF;
            break;
        }
        case 0xF0: if (_bus.movxWrite) _bus.movxWrite(Dptr(), A()); break;
        case 0xF2: case 0xF3:
        {
            const uint16_t address = static_cast<uint16_t>((_sfr[S(kP2)] << 8) | R(op & 1));
            if (_bus.movxWrite)
                _bus.movxWrite(address, A());
            break;
        }

        // MUL / DIV / DA / SWAP / CLR / CPL
        case 0xA4:
        {
            const unsigned product = static_cast<unsigned>(A()) * Bref();
            A() = static_cast<uint8_t>(product);
            Bref() = static_cast<uint8_t>(product >> 8);
            PswRef() = static_cast<uint8_t>((PswRef() & ~(0x80 | 0x04)) | (product > 0xFF ? 0x04 : 0));
            break;
        }
        case 0x84:
        {
            const uint8_t b = Bref();
            uint8_t psw = static_cast<uint8_t>(PswRef() & ~(0x80 | 0x04));
            if (b == 0)
                psw |= 0x04;   // A and B undefined: left as they were
            else
            {
                const uint8_t a = A();
                A() = static_cast<uint8_t>(a / b);
                Bref() = static_cast<uint8_t>(a % b);
            }
            PswRef() = psw;
            break;
        }
        case 0xD4:   // DA A: CY is only ever set
        {
            unsigned a = A();
            bool cy = Cy();
            if ((a & 0x0F) > 9 || (PswRef() & 0x40))
            {
                a += 0x06;
                if (a > 0xFF)
                    cy = true;
            }
            if (((a >> 4) & 0x0F) > 9 || cy || a > 0xFF)
            {
                a += 0x60;
                if (a > 0xFF)
                    cy = true;
            }
            A() = static_cast<uint8_t>(a);
            SetCy(cy);
            break;
        }
        case 0xC4: A() = static_cast<uint8_t>((A() << 4) | (A() >> 4)); break;
        case 0xE4: A() = 0; break;
        case 0xF4: A() = static_cast<uint8_t>(~A()); break;

        // XCH / XCHD
        case 0xC5: { const uint8_t a = Fetch(); const uint8_t v = ReadDirect(a); WriteDirect(a, A()); A() = v; break; }
        case 0xC6: case 0xC7: case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF:
        {
            const uint8_t v = operand();
            setOperand(A());
            A() = v;
            break;
        }
        case 0xD6: case 0xD7:
        {
            const uint8_t address = R(op & 1);
            const uint8_t v = ReadIndirect(address);
            WriteIndirect(address, static_cast<uint8_t>((v & 0xF0) | (A() & 0x0F)));
            A() = static_cast<uint8_t>((A() & 0xF0) | (v & 0x0F));
            break;
        }

        // PUSH / POP
        case 0xC0: Push(ReadDirect(Fetch())); break;
        case 0xD0: { const uint8_t a = Fetch(); WriteDirect(a, Pop()); break; }

        // CJNE: CY = destination < source
        case 0xB4: { const uint8_t d = Fetch(); const uint8_t r = Fetch(); SetCy(A() < d); if (A() != d) rel(r); break; }
        case 0xB5:
        {
            const uint8_t v = ReadDirect(Fetch());
            const uint8_t r = Fetch();
            SetCy(A() < v);
            if (A() != v)
                rel(r);
            break;
        }
        case 0xB6: case 0xB7: case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        {
            const uint8_t d = Fetch();
            const uint8_t r = Fetch();
            const uint8_t v = operand();
            SetCy(v < d);
            if (v != d)
                rel(r);
            break;
        }

        // DJNZ
        case 0xD5:
        {
            const uint8_t a = Fetch();
            const uint8_t r = Fetch();
            const uint8_t v = static_cast<uint8_t>(ReadDirect(a, true) - 1);
            WriteDirect(a, v);
            if (v != 0)
                rel(r);
            break;
        }
        case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        {
            const uint8_t r = Fetch();
            const uint8_t v = static_cast<uint8_t>(R(op & 7) - 1);
            SetR(op & 7, v);
            if (v != 0)
                rel(r);
            break;
        }

        default: break;   // every opcode is handled above; AJMP / ACALL before the switch
    }
    return cycles;
}

/// endregion </Instructions>

void Mcs51::SaveState(State& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.clock = _clock;
    out.instructions = _instructions;
    out.pc = _pc;
    std::memcpy(out.ram, _ram.data(), sizeof(out.ram));
    std::memcpy(out.sfr, _sfr.data(), sizeof(out.sfr));
    std::memcpy(out.pins, _pins.data(), sizeof(out.pins));
    out.inService = _inService;
    out.blockOne = _blockOne ? 1 : 0;
    out.idle = _idle ? 1 : 0;
    out.powerDown = _powerDown ? 1 : 0;
    out.sbufIn = _sbufIn;
    out.txBusy = _txBusy ? 1 : 0;
    out.txNinth = _txNinth ? 1 : 0;
    out.txByte = _txByte;
    out.t1Half = _t1Half ? 1 : 0;
    out.txTicks = _txTicks;
    out.txTicksToTi = _txTicksToTi;
    out.t2Prescale = _t2Prescale;
    for (int n = 0; n < 6; ++n)
        out.visibleAt[n] = _visibleAt[n];
    out.lastRequests = _lastRequests;
}

void Mcs51::LoadState(const State& in)
{
    _clock = in.clock;
    _instructions = in.instructions;
    _pc = in.pc;
    std::memcpy(_ram.data(), in.ram, sizeof(in.ram));
    std::memcpy(_sfr.data(), in.sfr, sizeof(in.sfr));
    std::memcpy(_pins.data(), in.pins, sizeof(in.pins));
    _inService = in.inService;
    _blockOne = in.blockOne != 0;
    _idle = in.idle != 0;
    _powerDown = in.powerDown != 0;
    _sbufIn = in.sbufIn;
    _txBusy = in.txBusy != 0;
    _txNinth = in.txNinth != 0;
    _txByte = in.txByte;
    _t1Half = in.t1Half != 0;
    _txTicks = in.txTicks;
    _txTicksToTi = in.txTicksToTi;
    _t2Prescale = in.t2Prescale;
    for (int n = 0; n < 6; ++n)
        _visibleAt[n] = in.visibleAt[n];
    _lastRequests = in.lastRequests;
}

}  // namespace mcs51
