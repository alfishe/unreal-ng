#include "layout/z80size.h"

#include <string_view>

namespace unrealasm::layout
{
namespace
{
using ir::Operand;
using Kind = ir::Operand::Kind;

bool Is(const Operand& o, Kind kind, std::string_view text = {})
{
    return o.kind == kind && (text.empty() || o.text == text);
}

bool Reg8(const Operand& o)
{
    return o.kind == Kind::Register && o.text.size() == 1 && std::string_view("abcdehl").find(o.text[0]) != std::string_view::npos;
}

bool Half(const Operand& o)   // IXH IXL IYH IYL: an index prefix before the 8-bit form
{
    return o.kind == Kind::Register && (o.text == "ixh" || o.text == "ixl" || o.text == "iyh" || o.text == "iyl");
}

bool Index(const Operand& o)   // IX / IY as a 16-bit register
{
    return o.kind == Kind::Register && (o.text == "ix" || o.text == "iy");
}

bool Pair(const Operand& o)   // BC DE HL SP
{
    return o.kind == Kind::Register && (o.text == "bc" || o.text == "de" || o.text == "hl" || o.text == "sp");
}

bool Indexed(const Operand& o)   // (IX+d), and (IX) = (IX+0)
{
    return o.kind == Kind::Indexed || Is(o, Kind::Indirect, "ix") || Is(o, Kind::Indirect, "iy");
}

bool Value(const Operand& o)
{
    return o.kind == Kind::Immediate;
}

/// An 8-bit operand of the ALU, INC / DEC and LD forms: B C D E H L (HL) A = 1 byte of opcode, IXH.. one prefix more,
/// (IX+d) a prefix and the displacement; -1 = none of them
int Operand8(const Operand& o)
{
    if (Reg8(o) || Is(o, Kind::Indirect, "hl"))
        return 0;
    if (Half(o))
        return 1;
    if (Indexed(o))
        return 2;
    return -1;
}

int Fail(std::string& error, const ir::Statement& s)
{
    error = "no Z80 instruction: " + s.mnemonic;
    for (size_t k = 0; k < s.operands.size(); ++k)
        error += (k ? "," : " ") + std::string(s.operands[k].text.empty() ? "<expr>" : s.operands[k].text);
    return 0;
}

int Ld(const Operand& d, const Operand& v)
{
    const int d8 = Operand8(d), v8 = Operand8(v);
    if (d8 >= 0 && v8 >= 0)
    {
        if (Is(d, Kind::Indirect, "hl") && Is(v, Kind::Indirect, "hl"))
            return 0;   // that is HALT
        if (Indexed(d) && Indexed(v))
            return 0;
        if ((d8 == 2 && !Reg8(v)) || (v8 == 2 && !Reg8(d)))
            return 0;   // LD (IX+d),IXH / (HL): no such form
        if ((Half(d) || Half(v)) && (Is(d, Kind::Indirect) || Is(v, Kind::Indirect) || d.text == "h" || d.text == "l" || v.text == "h" ||
                                     v.text == "l"))
            return 0;
        return 1 + (d8 > v8 ? d8 : v8);
    }
    if (d8 >= 0 && Value(v))
        return 2 + d8;
    if (Is(d, Kind::Register, "a"))
    {
        if (Is(v, Kind::Indirect, "bc") || Is(v, Kind::Indirect, "de"))
            return 1;
        if (v.kind == Kind::Memory)
            return 3;
        if (Is(v, Kind::Register, "i") || Is(v, Kind::Register, "r"))
            return 2;
    }
    if ((Is(d, Kind::Register, "i") || Is(d, Kind::Register, "r")) && Is(v, Kind::Register, "a"))
        return 2;
    if ((Is(d, Kind::Indirect, "bc") || Is(d, Kind::Indirect, "de")) && Is(v, Kind::Register, "a"))
        return 1;
    if (d.kind == Kind::Memory && Is(v, Kind::Register, "a"))
        return 3;
    if (Pair(d) || Index(d))
    {
        if (Value(v))
            return Index(d) ? 4 : 3;
        if (v.kind == Kind::Memory)
            return d.text == "hl" ? 3 : 4;
        if (Is(d, Kind::Register, "sp") && (Is(v, Kind::Register, "hl") || Index(v)))
            return Index(v) ? 2 : 1;
        if (d.text != "sp" && v.kind == Kind::Register && (Pair(v) || Index(v)) && v.text != "sp")
        {
            // sjasmplus' fake LD rr,rr': two 8-bit loads (LD B,D : LD C,E; with IX: LD B,IXH : LD C,IXL)
            if (Index(d) && Index(v))
                return 0;
            if (Index(d) || Index(v))
                return d.text == "hl" || v.text == "hl" ? 0 : 4;
            return 2;
        }
        if (d.text != "sp" && !Index(d) && Indexed(v))
            return 6;   // fake LD rr,(IX+d): two indexed loads
    }
    if (d.kind == Kind::Memory && (Pair(v) || Index(v)))
        return v.text == "hl" ? 3 : 4;
    if (Indexed(d) && Pair(v) && v.text != "sp")
        return 6;   // fake LD (IX+d),rr
    return 0;
}

int Alu(const ir::Statement& s)
{
    const std::string& m = s.mnemonic;
    const auto& ops = s.operands;
    if (ops.size() == 2 && (Is(ops[0], Kind::Register, "hl") || Index(ops[0])) && (m == "add" || m == "adc" || m == "sbc" || m == "sub"))
    {
        const Operand& v = ops[1];
        if (!(Pair(v) || Index(v)))
            return 0;
        if (Index(ops[0]))
            return m == "add" && (v.text == ops[0].text || (!Index(v) && v.text != "hl")) ? 2 : 0;
        if (Index(v))
            return 0;
        if (m == "add")
            return 1;
        if (m == "sub")
            return 3;   // fake SUB HL,rr: OR A + SBC HL,rr
        return 2;
    }
    // "op a,x" or "op x"
    const Operand* v = nullptr;
    if (ops.size() == 2 && Is(ops[0], Kind::Register, "a"))
        v = &ops[1];
    else if (ops.size() == 1)
        v = &ops[0];
    if (!v)
        return 0;
    const int v8 = Operand8(*v);
    if (v8 >= 0)
        return 1 + v8;
    if (Value(*v))
        return 2;
    return 0;
}

int IncDec(const Operand& o)
{
    const int o8 = Operand8(o);
    if (o8 >= 0)
        return 1 + o8;
    if (Pair(o))
        return 1;
    if (Index(o))
        return 2;
    return 0;
}

int Rotate(const ir::Statement& s)   // RLC RRC RL RR SLA SRA SLL SLI SRL x [,r]
{
    const auto& ops = s.operands;
    if (ops.empty())
        return 0;
    if (Indexed(ops[0]))
        return ops.size() == 1 || (ops.size() == 2 && Reg8(ops[1])) ? 4 : 0;
    if (Reg8(ops[0]) || Is(ops[0], Kind::Indirect, "hl"))
        return ops.size() == 1 ? 2 : 0;
    return 0;
}

int Bit(const ir::Statement& s)   // BIT / RES / SET n,x [,r]
{
    const auto& ops = s.operands;
    if (ops.size() < 2 || !Value(ops[0]))
        return 0;
    if (Indexed(ops[1]))
        return ops.size() == 2 || (ops.size() == 3 && s.mnemonic != "bit" && Reg8(ops[2])) ? 4 : 0;
    if (Reg8(ops[1]) || Is(ops[1], Kind::Indirect, "hl"))
        return ops.size() == 2 ? 2 : 0;
    return 0;
}

constexpr std::string_view kOneByte[] = {"nop", "halt", "di", "ei", "exx", "rla", "rra", "rlca", "rrca", "daa", "cpl", "scf", "ccf"};
constexpr std::string_view kPrefixed[] = {"neg",  "rld",  "rrd", "ldi",  "ldir", "ldd",  "lddr", "cpi",  "cpir", "cpd", "cpdr",
                                          "ini",  "inir", "ind", "indr", "outi", "otir", "outd", "otdr", "reti", "retn"};

int SizeOf(const ir::Statement& s)
{
    const std::string& m = s.mnemonic;
    const auto& ops = s.operands;
    for (std::string_view one : kOneByte)
        if (m == one)
            return ops.empty() ? 1 : 0;
    for (std::string_view two : kPrefixed)
        if (m == two)
            return ops.empty() ? 2 : 0;
    if (m == "ld")
        return ops.size() == 2 ? Ld(ops[0], ops[1]) : 0;
    if (m == "add" || m == "adc" || m == "sub" || m == "sbc" || m == "and" || m == "xor" || m == "or" || m == "cp")
        return Alu(s);
    if (m == "inc" || m == "dec")
        return ops.size() == 1 ? IncDec(ops[0]) : 0;
    if (m == "rlc" || m == "rrc" || m == "rl" || m == "rr" || m == "sla" || m == "sra" || m == "sll" || m == "sli" || m == "srl")
        return Rotate(s);
    if (m == "bit" || m == "res" || m == "set")
        return Bit(s);
    if (m == "jp")
    {
        if (ops.size() == 1 && (Is(ops[0], Kind::Indirect, "hl") || Is(ops[0], Kind::Register, "hl")))
            return 1;
        if (ops.size() == 1 && (Is(ops[0], Kind::Indirect, "ix") || Is(ops[0], Kind::Indirect, "iy") || Index(ops[0])))
            return 2;
        if ((ops.size() == 1 && (Value(ops[0]) || ops[0].kind == Kind::Memory)) || (ops.size() == 2 && ops[0].kind == Kind::Condition))
            return 3;
        return 0;
    }
    if (m == "jr" || m == "djnz")
        return ops.size() == 1 || (m == "jr" && ops.size() == 2 && ops[0].kind == Kind::Condition) ? 2 : 0;
    if (m == "call")
        return ops.size() == 1 || (ops.size() == 2 && ops[0].kind == Kind::Condition) ? 3 : 0;
    if (m == "ret")
        return ops.empty() || (ops.size() == 1 && ops[0].kind == Kind::Condition) ? 1 : 0;
    if (m == "rst")
        return ops.size() == 1 ? 1 : 0;
    if (m == "im")
        return ops.size() == 1 ? 2 : 0;
    if (m == "push" || m == "pop")
    {
        if (ops.size() != 1)
            return 0;
        if (Index(ops[0]))
            return 2;
        return Pair(ops[0]) && ops[0].text != "sp" ? 1 : Is(ops[0], Kind::Register, "af") ? 1 : 0;
    }
    if (m == "ex")
    {
        if (ops.size() != 2)
            return 0;
        if ((Is(ops[0], Kind::Register, "af") && (Is(ops[1], Kind::Register, "af'") || Is(ops[1], Kind::Register, "af"))) || (Is(ops[0], Kind::Register, "de") && Is(ops[1], Kind::Register, "hl")) ||
            (Is(ops[0], Kind::Register, "hl") && Is(ops[1], Kind::Register, "de")))
            return 1;
        if (Is(ops[0], Kind::Indirect, "sp"))
            return Is(ops[1], Kind::Register, "hl") ? 1 : Index(ops[1]) ? 2 : 0;
        return 0;
    }
    if (m == "in")
    {
        if (ops.size() == 1 && Is(ops[0], Kind::Indirect, "c"))
            return 2;   // IN (C) / IN F,(C)
        if (ops.size() == 2 && Is(ops[1], Kind::Indirect, "c") && (Reg8(ops[0]) || Is(ops[0], Kind::Register, "f")))
            return 2;
        if (ops.size() == 2 && Is(ops[0], Kind::Register, "a") && ops[1].kind == Kind::Memory)
            return 2;
        return 0;
    }
    if (m == "inf")
        return ops.empty() ? 2 : 0;
    if (m == "out")
    {
        if (ops.size() == 2 && Is(ops[0], Kind::Indirect, "c") && (Reg8(ops[1]) || Value(ops[1])))
            return 2;
        if (ops.size() == 2 && ops[0].kind == Kind::Memory && Is(ops[1], Kind::Register, "a"))
            return 2;
        return 0;
    }
    return 0;
}

/// sjasmplus repeats these for every operand: PUSH AF,BC = PUSH AF : PUSH BC, and (without --syntax=a) SUB A,B =
/// SUB A : SUB B for the ALU operations that take one operand
bool TakesSeveral(const ir::Statement& s)
{
    const std::string& m = s.mnemonic;
    if (m == "push" || m == "pop" || m == "inc" || m == "dec")
        return true;
    if (m == "sub" || m == "and" || m == "xor" || m == "or" || m == "cp")
        return !s.operands.empty() && !(Is(s.operands[0], Kind::Register, "hl") || Index(s.operands[0]));   // SUB HL,rr: fake
    return false;
}
}  // namespace

int InstructionSize(const ir::Statement& s, std::string& error)
{
    if (TakesSeveral(s) && s.operands.size() > 1)
    {
        int total = 0;
        for (const Operand& o : s.operands)
        {
            ir::Statement one = s;
            one.operands = {o};
            const int size = SizeOf(one);
            if (size == 0)
                return Fail(error, s);
            total += size;
        }
        return total;
    }
    const int size = SizeOf(s);
    return size ? size : Fail(error, s);
}
}  // namespace unrealasm::layout
