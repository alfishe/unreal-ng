#include "debugger/analyzers/analysis/z80opinfo.h"

namespace analysis
{

namespace reg
{
const char* Name(int bit)
{
    static const char* const kNames[] = {"A",   "F",   "B",   "C",  "D", "E", "H", "L",
                                         "IXH", "IXL", "IYH", "IYL", "SP", "I", "R", "alt"};
    return bit >= 0 && bit < 16 ? kNames[bit] : "?";
}
}  // namespace reg

namespace
{

using namespace reg;

/// 0: no index prefix, 1: DD (IX), 2: FD (IY)
uint16_t PairHL(int idx)
{
    return idx == 1 ? kIX : idx == 2 ? kIY : kHL;
}

uint16_t RegH(int idx)
{
    return idx == 1 ? kIXH : idx == 2 ? kIYH : kH;
}

uint16_t RegL(int idx)
{
    return idx == 1 ? kIXL : idx == 2 ? kIYL : kL;
}

MemForm MemOfHL(int idx)
{
    return idx == 1 ? MemForm::IX : idx == 2 ? MemForm::IY : MemForm::HL;
}

/// r[n]: B C D E H L (HL) A; 6 (the memory operand) gives no register
uint16_t R8(int r, int idx)
{
    switch (r)
    {
        case 0:
            return kB;
        case 1:
            return kC;
        case 2:
            return kD;
        case 3:
            return kE;
        case 4:
            return RegH(idx);
        case 5:
            return RegL(idx);
        case 7:
            return kA;
        default:
            return 0;
    }
}

/// rp[p]: BC DE HL SP
uint16_t RP(int p, int idx)
{
    switch (p)
    {
        case 0:
            return kBC;
        case 1:
            return kDE;
        case 2:
            return PairHL(idx);
        default:
            return kSP;
    }
}

/// rp2[p]: BC DE HL AF
uint16_t RP2(int p, int idx)
{
    return p == 3 ? kAF : RP(p, idx);
}

void Imm(OpInfo& o, int bytes, bool address)
{
    o.immOffset = o.length;
    o.immBytes = static_cast<uint8_t>(bytes);
    o.length = static_cast<uint8_t>(o.length + bytes);
    o.immIsAddress = address;
}

/// The (HL) / (IX+d) / (IY+d) operand; an index prefix adds the displacement byte
void MemHL(OpInfo& o, int idx, bool read, bool write)
{
    o.mem = MemOfHL(idx);
    o.memRead = read;
    o.memWrite = write;
    o.regsRead |= PairHL(idx);
    if (idx != 0)
    {
        o.dispOffset = 2;
        o.length = static_cast<uint8_t>(o.length + 1);
    }
}

void Stack(OpInfo& o, bool read, bool write)
{
    o.mem = MemForm::Stack;
    o.memRead = read;
    o.memWrite = write;
    o.regsRead |= kSP;
    o.regsWritten |= kSP;
}

void Alu8(OpInfo& o, int y)
{
    o.aluOp = static_cast<uint8_t>(y);
    o.cls = y == 7 ? OpClass::Compare : OpClass::Alu;
    o.regsRead |= kA;
    if (y == 1 || y == 3)
        o.regsRead |= kF;  // ADC / SBC take the carry
    o.regsWritten |= kF;
    if (y != 7)
        o.regsWritten |= kA;
}

OpInfo Base(uint8_t op, int idx)
{
    OpInfo o;
    o.length = static_cast<uint8_t>(idx != 0 ? 2 : 1);
    const int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    switch (x)
    {
        case 0:
            switch (z)
            {
                case 0:
                    if (y == 0)
                        o.cls = OpClass::Nop;
                    else if (y == 1)
                    {
                        o.cls = OpClass::Exchange;
                        o.regsRead = o.regsWritten = kAF | kAlt;
                    }
                    else if (y == 2)
                    {
                        o.cls = OpClass::Jump;
                        o.flow = Flow::Djnz;
                        Imm(o, 1, false);
                        o.regsRead = o.regsWritten = kB;
                    }
                    else
                    {
                        o.cls = OpClass::Jump;
                        o.flow = Flow::Jr;
                        Imm(o, 1, false);
                        if (y >= 4)
                        {
                            o.cond = static_cast<uint8_t>(y - 4);
                            o.regsRead |= kF;
                        }
                    }
                    break;
                case 1:
                    if (q == 0)
                    {
                        o.cls = OpClass::Load;
                        Imm(o, 2, false);
                        o.immDest = RP(p, idx);
                        o.regsWritten = o.immDest;
                    }
                    else
                    {
                        o.cls = OpClass::Alu;
                        o.regsRead = PairHL(idx) | RP(p, idx);
                        o.regsWritten = PairHL(idx) | kF;
                    }
                    break;
                case 2:
                    o.cls = OpClass::Load;
                    if (q == 0)
                    {
                        if (p == 0 || p == 1)
                        {
                            o.mem = p == 0 ? MemForm::BC : MemForm::DE;
                            o.memWrite = true;
                            o.regsRead = (p == 0 ? kBC : kDE) | kA;
                        }
                        else
                        {
                            Imm(o, 2, true);
                            o.mem = MemForm::Abs;
                            o.memWrite = true;
                            o.regsRead = p == 2 ? PairHL(idx) : kA;
                        }
                    }
                    else
                    {
                        if (p == 0 || p == 1)
                        {
                            o.mem = p == 0 ? MemForm::BC : MemForm::DE;
                            o.memRead = true;
                            o.regsRead = p == 0 ? kBC : kDE;
                            o.regsWritten = o.loadDest = kA;
                        }
                        else
                        {
                            Imm(o, 2, true);
                            o.mem = MemForm::Abs;
                            o.memRead = true;
                            o.regsWritten = o.loadDest = p == 2 ? PairHL(idx) : kA;
                        }
                    }
                    break;
                case 3:
                    o.cls = OpClass::IncDec;
                    o.regsRead = o.regsWritten = RP(p, idx);
                    break;
                case 4:
                case 5:
                    o.cls = OpClass::IncDec;
                    if (y == 6)
                        MemHL(o, idx, true, true);
                    else
                    {
                        o.regsRead |= R8(y, idx);
                        o.regsWritten |= R8(y, idx);
                    }
                    o.regsWritten |= kF;
                    break;
                case 6:
                    o.cls = OpClass::Load;
                    if (y == 6)
                    {
                        MemHL(o, idx, false, true);
                        Imm(o, 1, false);
                    }
                    else
                    {
                        Imm(o, 1, false);
                        o.immDest = R8(y, idx);
                        o.regsWritten = o.immDest;
                    }
                    break;
                default:  // z == 7
                    if (y <= 3)
                    {
                        o.cls = OpClass::Rotate;
                        o.regsRead = (y >= 2) ? kAF : kA;
                        o.regsWritten = kAF;
                    }
                    else
                    {
                        o.cls = OpClass::Alu;
                        if (y == 4)  // DAA
                        {
                            o.regsRead = kAF;
                            o.regsWritten = kAF;
                        }
                        else if (y == 5)  // CPL
                        {
                            o.regsRead = kA;
                            o.regsWritten = kAF;
                        }
                        else if (y == 6)  // SCF
                            o.regsWritten = kF;
                        else  // CCF
                        {
                            o.regsRead = kF;
                            o.regsWritten = kF;
                        }
                    }
                    break;
            }
            break;

        case 1:
            if (y == 6 && z == 6)
            {
                o.cls = OpClass::Control;
                o.flow = Flow::Halt;
                break;
            }
            o.cls = OpClass::Load;
            if (y == 6)
            {
                MemHL(o, idx, false, true);
                o.regsRead |= R8(z, 0);
            }
            else if (z == 6)
            {
                MemHL(o, idx, true, false);
                o.regsWritten = o.loadDest = R8(y, 0);
            }
            else
            {
                o.copySrc = R8(z, idx);
                o.copyDest = R8(y, idx);
                o.regsRead = o.copySrc;
                o.regsWritten = o.copyDest;
            }
            break;

        case 2:
            Alu8(o, y);
            if (z == 6)
                MemHL(o, idx, true, false);
            else
                o.regsRead |= R8(z, idx);
            if (z == 7 && (y == 2 || y == 5))
                o.regsRead &= static_cast<uint16_t>(~kA);  // SUB A / XOR A: a definition, not a use
            break;

        default:  // x == 3
            switch (z)
            {
                case 0:
                    o.cls = OpClass::Ret;
                    o.flow = Flow::Ret;
                    o.cond = static_cast<uint8_t>(y);
                    o.regsRead |= kF;
                    Stack(o, true, false);
                    break;
                case 1:
                    if (q == 0)
                    {
                        o.cls = OpClass::Stack;
                        Stack(o, true, false);
                        o.loadDest = RP2(p, idx);
                        o.regsWritten |= o.loadDest;
                    }
                    else if (p == 0)
                    {
                        o.cls = OpClass::Ret;
                        o.flow = Flow::Ret;
                        Stack(o, true, false);
                    }
                    else if (p == 1)
                    {
                        o.cls = OpClass::Exchange;
                        o.regsRead = o.regsWritten = kBC | kDE | kHL | kAlt;
                    }
                    else if (p == 2)
                    {
                        o.cls = OpClass::Jump;
                        o.flow = Flow::JpIndirect;
                        o.regsRead = PairHL(idx);
                    }
                    else
                    {
                        o.cls = OpClass::Stack;
                        o.regsRead = PairHL(idx);
                        o.regsWritten = kSP;
                    }
                    break;
                case 2:
                    o.cls = OpClass::Jump;
                    o.flow = Flow::Jp;
                    o.cond = static_cast<uint8_t>(y);
                    o.regsRead = kF;
                    Imm(o, 2, true);
                    break;
                case 3:
                    switch (y)
                    {
                        case 0:
                            o.cls = OpClass::Jump;
                            o.flow = Flow::Jp;
                            Imm(o, 2, true);
                            break;
                        case 2:
                            o.cls = OpClass::Io;
                            o.ioOut = true;
                            Imm(o, 1, false);
                            o.regsRead = kA;
                            break;
                        case 3:
                            o.cls = OpClass::Io;
                            o.ioIn = true;
                            Imm(o, 1, false);
                            o.regsRead = kA;
                            o.regsWritten = o.loadDest = kA;
                            break;
                        case 4:
                            o.cls = OpClass::Stack;
                            Stack(o, true, true);
                            o.regsRead |= PairHL(idx);
                            o.regsWritten |= PairHL(idx);
                            break;
                        case 5:
                            o.cls = OpClass::Exchange;
                            o.regsRead = o.regsWritten = kDE | kHL;
                            break;
                        default:  // 1: CB (never here), 6: DI, 7: EI
                            o.cls = OpClass::Control;
                            break;
                    }
                    break;
                case 4:
                    o.cls = OpClass::Call;
                    o.flow = Flow::Call;
                    o.cond = static_cast<uint8_t>(y);
                    o.regsRead = kF;
                    Imm(o, 2, true);
                    Stack(o, false, true);
                    break;
                case 5:
                    if (q == 0)
                    {
                        o.cls = OpClass::Stack;
                        o.regsRead = RP2(p, idx);
                        Stack(o, false, true);
                    }
                    else  // p == 0: CALL nn (1..3: the prefixes, decoded elsewhere)
                    {
                        o.cls = OpClass::Call;
                        o.flow = Flow::Call;
                        Imm(o, 2, true);
                        Stack(o, false, true);
                    }
                    break;
                case 6:
                    Alu8(o, y);
                    Imm(o, 1, false);
                    break;
                default:  // z == 7
                    o.cls = OpClass::Call;
                    o.flow = Flow::Rst;
                    o.rstTarget = static_cast<uint8_t>(y * 8);
                    Stack(o, false, true);
                    break;
            }
            break;
    }
    return o;
}

OpInfo Cb(uint8_t op)
{
    OpInfo o;
    o.length = 2;
    const int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    o.cls = x == 0 ? OpClass::Rotate : OpClass::Bit;
    const bool writes = x != 1;
    if (z == 6)
        MemHL(o, 0, true, writes);
    else
    {
        o.regsRead |= R8(z, 0);
        if (writes)
            o.regsWritten |= R8(z, 0);
    }
    if (x == 0 && (y == 2 || y == 3))
        o.regsRead |= kF;  // RL / RR shift the carry in
    if (x <= 1)
        o.regsWritten |= kF;
    return o;
}

OpInfo IndexCb(uint8_t op, int idx)
{
    OpInfo o;
    o.length = 2;  // the prefix and CB; MemHL adds d, then the opcode
    const int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    o.cls = x == 0 ? OpClass::Rotate : OpClass::Bit;
    const bool writes = x != 1;
    MemHL(o, idx, true, writes);
    o.length = 4;
    if (writes && z != 6)
        o.regsWritten |= R8(z, 0);  // the undocumented copy into a register
    if (x == 0 && (y == 2 || y == 3))
        o.regsRead |= kF;
    if (x <= 1)
        o.regsWritten |= kF;
    return o;
}

OpInfo Ed(uint8_t op)
{
    OpInfo o;
    o.length = 2;
    const int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    if (x == 1)
    {
        switch (z)
        {
            case 0:
                o.cls = OpClass::Io;
                o.ioIn = true;
                o.regsRead = kBC;
                o.regsWritten = kF;
                if (y != 6)
                {
                    o.loadDest = R8(y, 0);
                    o.regsWritten |= o.loadDest;
                }
                break;
            case 1:
                o.cls = OpClass::Io;
                o.ioOut = true;
                o.regsRead = kBC | (y != 6 ? R8(y, 0) : 0);
                break;
            case 2:
                o.cls = OpClass::Alu;
                o.regsRead = kHL | RP(p, 0) | kF;
                o.regsWritten = kHL | kF;
                break;
            case 3:
                o.cls = OpClass::Load;
                Imm(o, 2, true);
                o.mem = MemForm::Abs;
                if (q == 0)
                {
                    o.memWrite = true;
                    o.regsRead = RP(p, 0);
                }
                else
                {
                    o.memRead = true;
                    o.regsWritten = o.loadDest = RP(p, 0);
                }
                break;
            case 4:
                o.cls = OpClass::Alu;
                o.regsRead = kA;
                o.regsWritten = kAF;
                break;
            case 5:
                o.cls = OpClass::Ret;
                o.flow = y == 1 ? Flow::Reti : Flow::Retn;
                Stack(o, true, false);
                break;
            case 6:
                o.cls = OpClass::Control;
                break;
            default:  // z == 7
                switch (y)
                {
                    case 0:
                        o.cls = OpClass::Load;
                        o.regsRead = kA;
                        o.regsWritten = kI;
                        break;
                    case 1:
                        o.cls = OpClass::Load;
                        o.regsRead = kA;
                        o.regsWritten = kR;
                        break;
                    case 2:
                    case 3:
                        o.cls = OpClass::Load;
                        o.regsRead = y == 2 ? kI : kR;
                        o.regsWritten = kAF;
                        break;
                    case 4:
                    case 5:
                        o.cls = OpClass::Rotate;
                        MemHL(o, 0, true, true);
                        o.regsRead |= kA;
                        o.regsWritten |= kAF;
                        break;
                    default:
                        o.cls = OpClass::Nop;
                        break;
                }
                break;
        }
        return o;
    }
    if (x == 2 && z <= 3 && y >= 4)
    {
        o.cls = OpClass::Block;
        o.mem = MemForm::Block;
        o.repeats = y >= 6;
        switch (z)
        {
            case 0:  // LDI LDD LDIR LDDR
                o.memRead = o.memWrite = true;
                o.regsRead = kHL | kDE | kBC;
                o.regsWritten = kHL | kDE | kBC | kF;
                break;
            case 1:  // CPI CPD CPIR CPDR
                o.memRead = true;
                o.regsRead = kA | kHL | kBC;
                o.regsWritten = kHL | kBC | kF;
                break;
            case 2:  // INI IND INIR INDR
                o.memWrite = true;
                o.ioIn = true;
                o.regsRead = kHL | kBC;
                o.regsWritten = kHL | kB | kF;
                break;
            default:  // OUTI OUTD OTIR OTDR
                o.memRead = true;
                o.ioOut = true;
                o.regsRead = kHL | kBC;
                o.regsWritten = kHL | kB | kF;
                break;
        }
        return o;
    }
    o.cls = OpClass::Nop;
    return o;
}

struct Tables
{
    OpInfo base[256];
    OpInfo cb[256];
    OpInfo ed[256];
    OpInfo dd[256];
    OpInfo fd[256];
    OpInfo ddcb[256];
    OpInfo fdcb[256];
    OpInfo prefix;

    Tables()
    {
        for (int i = 0; i < 256; ++i)
        {
            const auto op = static_cast<uint8_t>(i);
            base[i] = Base(op, 0);
            cb[i] = Cb(op);
            ed[i] = Ed(op);
            dd[i] = Base(op, 1);
            fd[i] = Base(op, 2);
            ddcb[i] = IndexCb(op, 1);
            fdcb[i] = IndexCb(op, 2);
        }
        prefix.length = 1;
        prefix.cls = OpClass::Prefix;
    }
};

const Tables& TheTables()
{
    static const Tables tables;
    return tables;
}

}  // namespace

const OpInfo& DecodeOp(const uint8_t* bytes)
{
    const Tables& t = TheTables();
    const uint8_t b0 = bytes[0];
    if (b0 == 0xCB)
        return t.cb[bytes[1]];
    if (b0 == 0xED)
        return t.ed[bytes[1]];
    if (b0 == 0xDD || b0 == 0xFD)
    {
        const uint8_t b1 = bytes[1];
        if (b1 == 0xDD || b1 == 0xFD || b1 == 0xED)
            return t.prefix;
        if (b1 == 0xCB)
            return b0 == 0xDD ? t.ddcb[bytes[3]] : t.fdcb[bytes[3]];
        return b0 == 0xDD ? t.dd[b1] : t.fd[b1];
    }
    return t.base[b0];
}

const char* OpClassName(OpClass cls)
{
    switch (cls)
    {
        case OpClass::Nop:
            return "nop";
        case OpClass::Load:
            return "load";
        case OpClass::Alu:
            return "alu";
        case OpClass::Compare:
            return "compare";
        case OpClass::IncDec:
            return "incdec";
        case OpClass::Rotate:
            return "rotate";
        case OpClass::Bit:
            return "bit";
        case OpClass::Jump:
            return "jump";
        case OpClass::Call:
            return "call";
        case OpClass::Ret:
            return "ret";
        case OpClass::Stack:
            return "stack";
        case OpClass::Io:
            return "io";
        case OpClass::Block:
            return "block";
        case OpClass::Exchange:
            return "exchange";
        case OpClass::Control:
            return "control";
        case OpClass::Prefix:
            return "prefix";
    }
    return "?";
}

const char* FlowName(Flow flow)
{
    switch (flow)
    {
        case Flow::None:
            return "none";
        case Flow::Jp:
            return "jp";
        case Flow::Jr:
            return "jr";
        case Flow::Djnz:
            return "djnz";
        case Flow::Call:
            return "call";
        case Flow::Rst:
            return "rst";
        case Flow::Ret:
            return "ret";
        case Flow::Reti:
            return "reti";
        case Flow::Retn:
            return "retn";
        case Flow::JpIndirect:
            return "jp-indirect";
        case Flow::Halt:
            return "halt";
    }
    return "?";
}

}  // namespace analysis
