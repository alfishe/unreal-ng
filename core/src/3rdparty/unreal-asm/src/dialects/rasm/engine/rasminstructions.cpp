// rasm's Z80 instruction layer (see rasminstructions.h). Each function below follows the rasm.c function of the same
// name statement by statement: the operand checks, the order of the bytes and deferred expressions, the NOP / T-state
// counters and the error texts are rasm's, including its quirks (they are listed in research-rasm-instructions.md).
// rasm compares operand words by CRC; for the words of up to four characters (every register name) its CRC is the
// characters themselves, so a plain string comparison is the same test.

#include "dialects/rasm/engine/rasminstructions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace unrealasm::rasm
{
namespace
{
using Words = std::vector<std::string>;
using EvaluateNow = std::function<std::optional<double>(const std::string& expression)>;

/// rasm's StringIsMem: the word is one parenthesised expression "( ... )" (not "(5)+1")
bool StringIsMem(const std::string& w)
{
    if (w.empty() || w[0] != '(')
        return false;
    int p = 1;
    size_t idx = 1;
    while (idx < w.size())
    {
        switch (w[idx])
        {
            case '\\':
                if (idx + 1 < w.size())
                    idx++;
                break;
            case '\'':
                if (idx + 1 < w.size() && w[idx + 1] != '\\')
                    idx++;
                break;
            case '(':
                p++;
                break;
            case ')':
                p--;
                if (!p && idx + 1 < w.size())
                    return false;
                break;
            default:
                break;
        }
        idx++;
    }
    return w[idx - 1] == ')';
}

/// rasm's IsRegister
bool IsRegister(const std::string& w)
{
    static const char* const names[] = {"F",  "I",  "R",  "A",  "B",  "C",  "D",  "E",   "H",   "L",   "BC",
                                        "DE", "HL", "IX", "IY", "SP", "AF", "XH", "XL",  "YH",  "YL",  "HX",
                                        "LX", "HY", "LY", "IXL", "IXH", "IYL", "IYH"};
    return std::any_of(std::begin(names), std::end(names), [&](const char* n) { return w == n; });
}

/// strncmp(w,"(IX",3)==0 && w[3] is '+', '-' or ')': the (IX+d) / (IY+d) addressing rasm recognises
bool IsIndexMem(const std::string& w, char reg)
{
    return w.size() >= 4 && w[0] == '(' && w[1] == 'I' && w[2] == reg && (w[3] == '+' || w[3] == '-' || w[3] == ')');
}

/// B C D E H L (HL) A -> 0..7, -1 otherwise
int Reg8(const std::string& w)
{
    static const char* const names[] = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
    for (int i = 0; i < 8; i++)
        if (w == names[i])
            return i;
    return -1;
}

/// The index register halves and their aliases: prefix #DD / #FD and 0 (high) / 1 (low); false when not one
bool IndexHalf(const std::string& w, uint8_t& prefix, int& low)
{
    if (w == "IXH" || w == "HX" || w == "XH")
    {
        prefix = 0xDD;
        low = 0;
    }
    else if (w == "IXL" || w == "LX" || w == "XL")
    {
        prefix = 0xDD;
        low = 1;
    }
    else if (w == "IYH" || w == "HY" || w == "YH")
    {
        prefix = 0xFD;
        low = 0;
    }
    else if (w == "IYL" || w == "LY" || w == "YL")
    {
        prefix = 0xFD;
        low = 1;
    }
    else
        return false;
    return true;
}

int ConditionIndex(const std::string& w, int count)
{
    // NZ Z NC C PO PE P M: the order of the condition field cc of the opcodes
    static const char* const names[] = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};
    for (int i = 0; i < count; i++)
        if (w == names[i])
            return i;
    return -1;
}

std::string Format(const char* format, long long a, long long b = 0)
{
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), format, a, b);
    return buffer;
}

/// One statement being encoded: rasm's ae->wl[idx..] (the operand words), ___output, PushExpression, MakeError
class Encoder
{
public:
    Encoder(const Words& ops, const EncoderOptions& options, const EvaluateNow& evaluateNow)
        : _ops(ops), _options(options), _evaluateNow(evaluateNow)
    {
    }

    Encoded Run(const std::string& mnemonic);

private:
    const Words& _ops;
    const EncoderOptions& _options;
    const EvaluateNow& _evaluateNow;
    Encoded _out;

    size_t N() const { return _ops.size(); }
    const std::string& Op(size_t i) const { return _ops[i]; }

    void Byte(int b) { _out.bytes.push_back(static_cast<uint8_t>(b)); }
    void Bytes(std::initializer_list<int> list)
    {
        for (int b : list)
            Byte(b);
    }
    void Time(int nops, int ticks)
    {
        _out.nops += nops;
        _out.ticks += ticks;
    }
    void Error(const std::string& message)
    {
        if (_out.error.empty())
            _out.error = message;
    }
    void EnforceNoAddressingMode(const std::string& w)
    {
        if (StringIsMem(w))
            Error("Ambiguous and potentially erroneous mnemonic entry. Expecting immediate value instead of immediate "
                  "addressing value");
    }

    /// PushExpression: records the fixup, reserves its bytes; `$` is rasm's codeadr after its "$ hack"
    void Push(FixupKind kind, const std::string& word, uint8_t extra = 0);
    /// The second half of LD rr,(IX+n) / LD (IX+n),rr: outside loops / macros it sees the first half's `$`
    void PairSecondHalf()
    {
        if (!_options.insideLoopOrMacro && _out.fixups.size() >= 2)
            _out.fixups.back().instructionOffset = _out.fixups[_out.fixups.size() - 2].instructionOffset;
    }
    /// RoundComputeExpressionCore for the repetition counts
    std::optional<int> Count(const std::string& w)
    {
        std::optional<double> v = _evaluateNow ? _evaluateNow(w) : std::nullopt;
        if (!v)
        {
            Error("cannot evaluate the repetition count [" + w + "] now");
            return std::nullopt;
        }
        return static_cast<int>(std::floor(*v + _options.rough));
    }

    void Alu8(const std::string& w, int base, int immediate, const char* name, bool indexRegisterError);
    void Alu(const char* name, int base, int immediate);
    void In();
    void Out();
    void Ex();
    void Ret();
    void Call();
    void Jr();
    void Jp();
    void IncDec(bool inc);
    void PushPop(bool push);
    void Im();
    void Rst();
    void Djnz();
    void Ld();
    void LdIndexStore(const std::string& dst, const std::string& src, uint8_t prefix);
    void Rotate(const std::string& mnemonic);
    void Srl8();
    void BitResSet(const std::string& mnemonic);
    bool Simple(const std::string& mnemonic);
};

void Encoder::Push(FixupKind kind, const std::string& word, uint8_t extra)
{
    const size_t pos = _out.bytes.size();
    size_t hack = 0;
    size_t reserve = 0;
    switch (kind)
    {
        case FixupKind::J8:
        case FixupKind::V8:
        case FixupKind::Im:
            hack = 1;
            reserve = 1;
            break;
        case FixupKind::J16:
        case FixupKind::V16:
            hack = 1;
            reserve = 2;
            break;
        case FixupKind::IV8:
        case FixupKind::IV81:
            hack = 2;
            reserve = 1;
            break;
        case FixupKind::IV16:
            hack = 2;
            reserve = 2;
            break;
        case FixupKind::V8At3:
            hack = 3;
            reserve = 1;
            break;
        case FixupKind::V8At0:
        case FixupKind::Rst:
        case FixupKind::RstConditional:
            reserve = 1;
            break;
        case FixupKind::V16At0:
            reserve = 2;
            break;
        case FixupKind::V32At0:
            reserve = 4;
            break;
        case FixupKind::RealAmsdos:
        case FixupKind::RealMicrosoft:
            reserve = 5;
            break;
        case FixupKind::Float24:
            reserve = 3;
            break;
        case FixupKind::BitResSet:
            break;
    }
    Fixup f;
    f.kind = kind;
    f.offset = static_cast<uint32_t>(pos);
    f.instructionOffset = static_cast<uint32_t>(pos >= hack ? pos - hack : 0);
    f.expression = (kind == FixupKind::IV8 || kind == FixupKind::IV81) ? "(0" + word.substr(3) : word;
    f.extra = extra;
    _out.fixups.push_back(std::move(f));
    _out.bytes.resize(pos + reserve, 0);
}

// The 8-bit operand of ADD / ADC / SUB / SBC / AND / XOR / OR / CP
void Encoder::Alu8(const std::string& w, int base, int immediate, const char* name, bool indexRegisterError)
{
    const int r = Reg8(w);
    uint8_t prefix = 0;
    int low = 0;
    if (r == 6)
    {
        Byte(base + 6);
        Time(2, 7);
    }
    else if (r >= 0)
    {
        Byte(base + r);
        Time(1, 4);
    }
    else if (IndexHalf(w, prefix, low))
    {
        Bytes({prefix, base + 4 + low});
        Time(2, 8);
    }
    else if (indexRegisterError && (w == "IX" || w == "IY"))
    {
        Error(std::string("Use ") + name + " with A,B,C,D,E,H,L,XH,XL,YH,YL,(HL),(IX+n),(IY+n)");
    }
    else if (!w.empty() && w[0] == '(')
    {
        if (IsIndexMem(w, 'X') || IsIndexMem(w, 'Y'))
        {
            const uint8_t p = w[2] == 'X' ? 0xDD : 0xFD;
            Bytes({p, base + 6});
            Push(FixupKind::IV8, w, p);
            Time(5, 19);
        }
        else
        {
            Byte(immediate);
            EnforceNoAddressingMode(w);
            Push(FixupKind::V8, w);
            Time(2, 7);
        }
    }
    else
    {
        Byte(immediate);
        Push(FixupKind::V8, w);
        Time(2, 7);
    }
}

// _ADD _ADC _SBC _SUB _CP _AND _OR _XOR
void Encoder::Alu(const char* name, int base, int immediate)
{
    const std::string n = name;
    const bool implicitA = N() == 1 || (N() == 2 && Op(0) == "A");
    const bool acceptsA = n == "ADD" || n == "ADC" || n == "SBC" || n == "SUB" || n == "CP";
    if ((acceptsA && implicitA) || (!acceptsA && N() == 1))
    {
        Alu8(Op(N() - 1), base, immediate, name, n == "ADD" || n == "ADC" || n == "SBC" || n == "SUB");
        return;
    }
    if (n == "SUB" || n == "AND" || n == "OR" || n == "XOR")
    {
        Error("Use " + n + " with A,B,C,D,E,H,L,XH,XL,YH,YL,(HL),(IX+n),(IY+n)");
        return;
    }
    if (n == "CP")
    {
        Error("Syntax is CP reg8/(reg16)");
        return;
    }
    if (N() != 2)
    {
        Error("Invalid syntax for " + n);
        return;
    }
    static const char* const pairs[] = {"BC", "DE", "HL", "SP"};
    const std::string& dst = Op(0);
    const std::string& src = Op(1);
    if (n == "ADD")
    {
        if (dst == "HL" || dst == "IX" || dst == "IY")
        {
            const std::string self = dst;
            int rr = -1;
            for (int i = 0; i < 4; i++)
                if (src == (i == 2 ? self.c_str() : pairs[i]))
                    rr = i;
            if (rr < 0)
            {
                Error("syntax is ADD " + dst + ",[BC,DE," + dst + ",SP]");
                return;
            }
            if (dst == "HL")
            {
                Byte(0x09 + rr * 0x10);
                Time(3, 11);
            }
            else
            {
                Bytes({dst == "IX" ? 0xDD : 0xFD, 0x09 + rr * 0x10});
                Time(4, 15);
            }
        }
        else
            Error("syntax is ADD [HL,IX,IY],reg16");
        return;
    }
    // ADC / SBC HL,rr
    const std::string message = "syntax is " + n + " HL,[BC,DE,HL,SP]";
    int rr = -1;
    for (int i = 0; i < 4; i++)
        if (src == pairs[i])
            rr = i;
    if (dst != "HL" || rr < 0)
    {
        Error(message);
        return;
    }
    Bytes({0xED, (n == "ADC" ? 0x4A : 0x42) + rr * 0x10});
    Time(4, 15);
}

void Encoder::In()
{
    if (N() != 2)
    {
        Error("IN [0,F,A,B,C,D,E,H,L],(C) or IN A,(n) only");
        return;
    }
    if (Op(1) == "(C)")
    {
        const std::string& r = Op(0);
        const int code = (r == "0" || r == "F") ? 0x70 : (Reg8(r) >= 0 && r != "(HL)") ? 0x40 + Reg8(r) * 8 : -1;
        if (code < 0)
            Error("syntax is IN [0,F,A,B,C,D,E,H,L],(C)");
        else
        {
            Bytes({0xED, code});
            Time(4, 12);
        }
    }
    else if (Op(0) == "A" && StringIsMem(Op(1)))
    {
        Byte(0xDB);
        Push(FixupKind::V8, Op(1));
        Time(3, 11);
    }
    else
        Error("IN [0,F,A,B,C,D,E,H,L],(C) or IN A,(n) only");
}

void Encoder::Out()
{
    if (N() != 2)
    {
        Error("OUT (C),[0,A,B,C,D,E,H,L] or OUT (n),A only");
        return;
    }
    if (Op(0) == "(C)")
    {
        const std::string& r = Op(1);
        const int code = r == "0" ? 0x71 : (Reg8(r) >= 0 && r != "(HL)") ? 0x41 + Reg8(r) * 8 : -1;
        if (code < 0)
            Error("syntax is OUT (C),[0,A,B,C,D,E,H,L]");
        else
        {
            Bytes({0xED, code});
            Time(4, 12);
        }
    }
    else if (Op(1) == "A" && StringIsMem(Op(0)))
    {
        Byte(0xD3);
        Push(FixupKind::V8, Op(0));
        Time(3, 11);
    }
    else
        Error("OUT (C),[0,A,B,C,D,E,H,L] or OUT (n),A only");
}

void Encoder::Ex()
{
    if (N() != 2)
    {
        Error("Use EX reg16,[DE|(SP)]");
        return;
    }
    const std::string& a = Op(0);
    const std::string& b = Op(1);
    auto exSp = [&](int prefix)
    {
        if (prefix)
        {
            Bytes({prefix, 0xE3});
            Time(7, 23);
        }
        else
        {
            Byte(0xE3);
            Time(6, 19);
        }
    };
    if (a == "HL")
    {
        if (b == "DE")
        {
            Byte(0xEB);
            Time(1, 4);
        }
        else if (b == "(SP)")
            exSp(0);
        else
            Error("syntax is EX HL,[(SP),DE]");
    }
    else if (a == "AF")
    {
        if (b == "AF")
        {
            Byte(0x08);
            Time(1, 4);
        }
        else
            Error("syntax is EX AF,AF' or EXA");
    }
    else if (a == "(SP)")
    {
        if (b == "HL")
            exSp(0);
        else if (b == "IX")
            exSp(0xDD);
        else if (b == "IY")
            exSp(0xFD);
        else
            Error("syntax is EX (SP),[HL,IX,IY]");
    }
    else if (a == "DE")
    {
        if (b == "HL")
        {
            Byte(0xEB);
            Time(1, 4);
        }
        else
            Error("syntax is EX DE,HL");
    }
    else if (a == "IX" || a == "IY")
    {
        if (b == "(SP)")
            exSp(a == "IX" ? 0xDD : 0xFD);
        else
            Error("syntax is EX " + a + ",(SP)");
    }
    else
        Error("syntax is EX [AF,DE,HL,(SP),IX,IY],reg16");
}

void Encoder::Ret()
{
    if (N() == 1)
    {
        const int cc = ConditionIndex(Op(0), 8);
        if (cc < 0)
            Error("Available flags for RET are C,NC,Z,NZ,PE,PO,P,M");
        else
        {
            Byte(0xC0 + cc * 8);
            Time(2, 5);
        }
    }
    else if (N() == 0)
    {
        Byte(0xC9);
        Time(3, 10);
    }
    else
        Error("Invalid RET syntax");
}

void Encoder::Call()
{
    if (N() == 2)
    {
        const int cc = ConditionIndex(Op(0), 8);
        if (cc < 0)
            Error("Available flags for CALL are C,NC,Z,NZ,PE,PO,P,M");
        else
        {
            Byte(0xC4 + cc * 8);
            Time(3, 10);
        }
        Push(FixupKind::J16, Op(1));
    }
    else if (N() == 1)
    {
        Byte(0xCD);
        Push(FixupKind::J16, Op(0));
        Time(5, 17);
    }
    else
        Error("Invalid CALL syntax");
}

void Encoder::Jr()
{
    if (N() == 2)
    {
        const int cc = ConditionIndex(Op(0), 4);
        if (cc < 0)
            Error("Available flags for JR are C,NC,Z,NZ");
        else
        {
            Byte(0x20 + cc * 8);
            Time(2, 7);
        }
        Push(FixupKind::J8, Op(1));
    }
    else if (N() == 1)
    {
        Byte(0x18);
        Push(FixupKind::J8, Op(0));
        Time(3, 12);
    }
    else
        Error("Invalid JR syntax");
}

void Encoder::Jp()
{
    if (N() == 2)
    {
        const int cc = ConditionIndex(Op(0), 8);
        if (cc < 0)
            Error("Available flags for JP are C,NC,Z,NZ,PE,PO,P,M");
        else
        {
            Byte(0xC2 + cc * 8);
            Time(3, 10);
        }
        if (Op(1) == "(IX)" || Op(1) == "(IY)")
            Error("conditionnal JP cannot use register addressing");
        else
            Push(FixupKind::J16, Op(1));
    }
    else if (N() == 1)
    {
        const std::string& w = Op(0);
        if (w == "HL" || w == "(HL)")
        {
            Byte(0xE9);
            Time(1, 4);
        }
        else if (w == "IX" || w == "(IX)")
        {
            Bytes({0xDD, 0xE9});
            Time(2, 8);
        }
        else if (w == "IY" || w == "(IY)")
        {
            Bytes({0xFD, 0xE9});
            Time(2, 8);
        }
        else
        {
            EnforceNoAddressingMode(w);
            Byte(0xC3);
            Push(FixupKind::J16, w);
            Time(3, 10);
        }
    }
    else
        Error("Invalid JP syntax");
}

// _INC / _DEC: every operand in turn (INC A,B,HL is a super-instruction)
void Encoder::IncDec(bool inc)
{
    const std::string name = inc ? "INC" : "DEC";
    const std::string usage = "Use " + name + " with A,B,C,D,E,H,L,XH,XL,YH,YL,BC,DE,HL,SP,(HL),(IX+n),(IY+n)";
    if (N() == 0)
    {
        Error(usage);
        return;
    }
    const int r8 = inc ? 0x04 : 0x05;
    const int r16 = inc ? 0x03 : 0x0B;
    for (const std::string& w : _ops)
    {
        const int r = Reg8(w);
        uint8_t prefix = 0;
        int low = 0;
        if (r == 6)
        {
            Byte(r8 + 0x30);
            Time(3, 11);
        }
        else if (r >= 0)
        {
            Byte(r8 + r * 8);
            Time(1, 4);
        }
        else if (IndexHalf(w, prefix, low))
        {
            Bytes({prefix, r8 + 0x20 + low * 8});
            Time(2, 8);
        }
        else if (w == "BC" || w == "DE" || w == "HL" || w == "SP")
        {
            const int rr = w == "BC" ? 0 : w == "DE" ? 1 : w == "HL" ? 2 : 3;
            Byte(r16 + rr * 0x10);
            Time(2, 6);
        }
        else if (w == "IX" || w == "IY")
        {
            Bytes({w == "IX" ? 0xDD : 0xFD, r16 + 0x20});
            Time(3, 10);
        }
        else if (!w.empty() && w[0] == '(')
        {
            if (IsIndexMem(w, 'X') || IsIndexMem(w, 'Y'))
            {
                const uint8_t p = w[2] == 'X' ? 0xDD : 0xFD;
                Bytes({p, r8 + 0x30});
                Push(FixupKind::IV8, w, p);
                Time(6, 23);
            }
            else
            {
                Error("invalid " + name + " addressing mode, use only A,B,C,D,E,H,L,XH,XL,YH,YL,(HL),(IX+n),(IY+n)");
                return;
            }
        }
        else
            Error(usage);
    }
}

// _PUSH / _POP: every operand in turn (PUSH BC,DE,HL is a super-instruction)
void Encoder::PushPop(bool push)
{
    const std::string name = push ? "PUSH" : "POP";
    if (N() == 0)
    {
        Error(name + " need at least one parameter");
        return;
    }
    static const char* const pairs[] = {"BC", "DE", "HL", "AF"};
    for (const std::string& w : _ops)
    {
        int rr = -1;
        for (int i = 0; i < 4; i++)
            if (w == pairs[i])
                rr = i;
        if (rr >= 0)
        {
            Byte((push ? 0xC5 : 0xC1) + rr * 0x10);
            Time(push ? 4 : 3, push ? 11 : 10);
        }
        else if (w == "IX" || w == "IY")
        {
            Bytes({w == "IX" ? 0xDD : 0xFD, push ? 0xE5 : 0xE1});
            Time(push ? 5 : 4, push ? 15 : 14);
        }
        else
            Error("Use " + name + " with AF,BC,DE,HL,IX,IY");
    }
}

void Encoder::Im()
{
    if (N() != 1)
    {
        Error("IM need one parameter");
        return;
    }
    Byte(0xED);
    Push(FixupKind::Im, Op(0));
    Time(2, 8);
}

void Encoder::Rst()
{
    if (N() == 0)
    {
        Error("RST need at least one parameter");
        return;
    }
    if (N() == 2)
    {
        // RST cc,#38 = JR cc,$+1 (the displacement #FF is the RST #38 opcode)
        const int cc = ConditionIndex(Op(0), 4);
        if (cc < 0)
            Error("Available flags for RST condition,#38 are C,NC,Z,NZ");
        else
        {
            Byte(0x20 + cc * 8);
            Time(2, 7);
        }
        Push(FixupKind::RstConditional, Op(1));
    }
    else if (N() == 1)
    {
        if (Op(0) == "(IY)" || Op(0) == "(IX)")
            Error("RST cannot use IX or IY");
        else
            Push(FixupKind::Rst, Op(0));
        Time(4, 11);
    }
    else
        Error("usage is RST address or RST condition,#38");
}

void Encoder::Djnz()
{
    if (N() != 1)
    {
        Error("DJNZ need one parameter");
        return;
    }
    if (IsRegister(Op(0)) || Op(0) == "(IX)" || Op(0) == "(IY)")
        Error("DJNZ cannot use register");
    else
    {
        Byte(0x10);
        Push(FixupKind::J8, Op(0));
        Time(4, 13);
    }
}

// LD (IX+d),src / LD (IY+d),src
void Encoder::LdIndexStore(const std::string& dst, const std::string& src, uint8_t p)
{
    const int r = Reg8(src);
    if (r >= 0 && r != 6)
    {
        Bytes({p, 0x70 + r});
        Push(FixupKind::IV8, dst, p);
        Time(5, 19);
    }
    else if (src == "HL" || src == "DE" || src == "BC")
    {
        const int high = src == "HL" ? 4 : src == "DE" ? 2 : 0;
        Bytes({p, 0x70 + high});
        Push(FixupKind::IV81, dst, p);
        Bytes({p, 0x70 + high + 1});
        Push(FixupKind::IV8, dst, p);
        PairSecondHalf();
        Time(10, 38);
    }
    else
    {
        Bytes({p, 0x36});
        EnforceNoAddressingMode(src);
        Push(FixupKind::IV8, dst, p);
        Push(FixupKind::V8At3, src);
        Time(6, 23);
    }
}

void Encoder::Ld()
{
    if (N() != 2)
    {
        Error("LD needs two parameters");
        return;
    }
    const std::string& dst = Op(0);
    const std::string& src = Op(1);
    const int d = Reg8(dst);
    const int s = Reg8(src);
    uint8_t prefix = 0;
    int low = 0;

    // LD r,(IX+d) / (IY+d) / (nn) / n for the 8-bit destinations B C D E H L A
    auto load8 = [&](int code, bool memoryForm)
    {
        if (!src.empty() && src[0] == '(')
        {
            if (IsIndexMem(src, 'X') || IsIndexMem(src, 'Y'))
            {
                const uint8_t p = src[2] == 'X' ? 0xDD : 0xFD;
                Bytes({p, 0x46 + code * 8});
                Push(FixupKind::IV8, src, p);
                Time(5, 19);
                return;
            }
            if (memoryForm)
            {
                if (StringIsMem(src))
                {
                    Byte(0x3A);
                    Push(FixupKind::V16, src);
                    Time(4, 13);
                }
                else
                {
                    Byte(0x3E);
                    Push(FixupKind::V8, src);
                    Time(2, 7);
                }
                return;
            }
            Byte(0x06 + code * 8);
            EnforceNoAddressingMode(src);
            Push(FixupKind::V8, src);
            Time(2, 7);
            return;
        }
        Byte(0x06 + code * 8);
        Push(FixupKind::V8, src);
        Time(2, 7);
    };

    if (dst == "A")
    {
        if (src == "I" || src == "R")
        {
            Bytes({0xED, src == "I" ? 0x57 : 0x5F});
            Time(3, 9);
        }
        else if (s == 6)
        {
            Byte(0x7E);
            Time(2, 7);
        }
        else if (s >= 0)
        {
            Byte(0x78 + s);
            Time(1, 4);
        }
        else if (IndexHalf(src, prefix, low))
        {
            Bytes({prefix, 0x7C + low});
            Time(2, 8);
        }
        else if (src == "(BC)" || src == "(DE)")
        {
            Byte(src == "(BC)" ? 0x0A : 0x1A);
            Time(2, 7);
        }
        else
            load8(7, true);
    }
    else if (dst == "I" || dst == "R")
    {
        if (src == "A")
        {
            Bytes({0xED, dst == "I" ? 0x47 : 0x4F});
            Time(3, 9);
        }
        else
            Error("LD " + dst + ",A only");
    }
    else if (d >= 0 && d <= 3)
    {
        // B C D E: registers, index halves, (HL)
        if (s == 6)
        {
            Byte(0x46 + d * 8);
            Time(2, 7);
        }
        else if (s >= 0)
        {
            Byte(0x40 + d * 8 + s);
            Time(1, 4);
        }
        else if (IndexHalf(src, prefix, low))
        {
            Bytes({prefix, 0x44 + d * 8 + low});
            Time(2, 8);
        }
        else
            load8(d, false);
    }
    else if (IndexHalf(dst, prefix, low))
    {
        uint8_t srcPrefix = 0;
        int srcLow = 0;
        const int base = low ? 0x68 : 0x60;
        if (s >= 0 && s <= 3)
        {
            Bytes({prefix, base + s});
            Time(2, 8);
        }
        else if (IndexHalf(src, srcPrefix, srcLow) && srcPrefix == prefix)
        {
            Bytes({prefix, base + 4 + srcLow});
            Time(2, 8);
        }
        else if (src == "A")
        {
            Bytes({prefix, base + 7});
            Time(2, 8);
        }
        else
        {
            Bytes({prefix, low ? 0x2E : 0x26});
            EnforceNoAddressingMode(src);
            Push(FixupKind::V8, src);
            Time(3, 11);
        }
    }
    else if (d == 4 || d == 5)
    {
        // H L: registers and (HL), no index halves
        if (s == 6)
        {
            Byte(0x46 + d * 8);
            Time(2, 7);
        }
        else if (s >= 0)
        {
            Byte(0x40 + d * 8 + s);
            Time(1, 4);
        }
        else
            load8(d, false);
    }
    else if (d == 6)
    {
        if (s >= 0 && s != 6)
        {
            Byte(0x70 + s);
            Time(2, 7);
        }
        else if (!StringIsMem(src))
        {
            Byte(0x36);
            Push(FixupKind::V8, src);
            Time(3, 10);
        }
        else
            Error("LD (HL),n/r only");
    }
    else if (dst == "(BC)" || dst == "(DE)")
    {
        if (src == "A")
        {
            Byte(dst == "(BC)" ? 0x02 : 0x12);
            Time(2, 7);
        }
        else
            Error("LD " + dst + ",A only");
    }
    else if (dst == "HL" || dst == "BC" || dst == "DE")
    {
        // high / low register codes of the destination pair
        const int hi = dst == "BC" ? 0 : dst == "DE" ? 2 : 4;
        if (src == "BC" || src == "DE" || src == "HL")
        {
            const int shi = src == "BC" ? 0 : src == "DE" ? 2 : 4;
            Bytes({0x40 + hi * 8 + shi, 0x40 + (hi + 1) * 8 + shi + 1});
            Time(2, 8);
        }
        else if (dst == "HL" && src == "SP")
        {
            Bytes({0x21, 0x00, 0x00, 0x39});  // LD HL,0 : ADD HL,SP
            Time(6, 21);
        }
        else if (dst != "HL" && (src == "IX" || src == "IY"))
        {
            const int p = src == "IX" ? 0xDD : 0xFD;
            Bytes({p, 0x40 + hi * 8 + 4, p, 0x40 + (hi + 1) * 8 + 5});
            Time(4, 16);
        }
        else if (!src.empty() && src[0] == '(' && (IsIndexMem(src, 'X') || IsIndexMem(src, 'Y')))
        {
            const uint8_t p = src[2] == 'X' ? 0xDD : 0xFD;
            Bytes({p, 0x46 + hi * 8});
            Push(FixupKind::IV81, src, p);
            Bytes({p, 0x46 + (hi + 1) * 8});
            Push(FixupKind::IV8, src, p);
            PairSecondHalf();
            Time(10, 38);
        }
        else if (!src.empty() && src[0] == '(' && StringIsMem(src))
        {
            if (dst == "HL")
            {
                Byte(0x2A);
                Push(FixupKind::V16, src);
                Time(5, 16);
            }
            else
            {
                Bytes({0xED, dst == "BC" ? 0x4B : 0x5B});
                Push(FixupKind::IV16, src);
                Time(6, 20);
            }
        }
        else
        {
            Byte(dst == "BC" ? 0x01 : dst == "DE" ? 0x11 : 0x21);
            Push(FixupKind::V16, src);
            Time(3, 10);
        }
    }
    else if (dst == "IX" || dst == "IY")
    {
        const int p = dst == "IX" ? 0xDD : 0xFD;
        if (src == "BC" || src == "DE")
        {
            const int shi = src == "BC" ? 0 : 2;
            Bytes({p, 0x60 + shi, p, 0x68 + shi + 1});
            Time(4, 16);
        }
        else if (StringIsMem(src))
        {
            Bytes({p, 0x2A});
            Push(FixupKind::IV16, src);
            Time(6, 20);
        }
        else
        {
            Bytes({p, 0x21});
            Push(FixupKind::IV16, src);
            Time(4, 14);
        }
    }
    else if (dst == "SP")
    {
        if (src == "HL")
        {
            Byte(0xF9);
            Time(2, 6);
        }
        else if (src == "IX" || src == "IY")
        {
            Bytes({src == "IX" ? 0xDD : 0xFD, 0xF9});
            Time(3, 10);
        }
        else if (StringIsMem(src))
        {
            Bytes({0xED, 0x7B});
            Push(FixupKind::IV16, src);
            Time(6, 20);
        }
        else
        {
            Byte(0x31);
            Push(FixupKind::V16, src);
            Time(3, 10);
        }
    }
    else if (IsIndexMem(dst, 'X') || IsIndexMem(dst, 'Y'))
    {
        LdIndexStore(dst, src, dst[2] == 'X' ? 0xDD : 0xFD);
    }
    else if (StringIsMem(dst))
    {
        if (src == "A")
        {
            Byte(0x32);
            Push(FixupKind::V16, dst);
            Time(4, 13);
        }
        else if (src == "HL")
        {
            Byte(0x22);
            Push(FixupKind::V16, dst);
            Time(5, 16);
        }
        else if (src == "BC" || src == "DE" || src == "SP")
        {
            Bytes({0xED, src == "BC" ? 0x43 : src == "DE" ? 0x53 : 0x73});
            Push(FixupKind::IV16, dst);
            Time(6, 20);
        }
        else if (src == "IX" || src == "IY")
        {
            Bytes({src == "IX" ? 0xDD : 0xFD, 0x22});
            Push(FixupKind::IV16, dst);
            Time(6, 20);
        }
        else
            Error("LD (#nnnn),[A,BC,DE,HL,SP,IX,IY] only");
    }
    else
        Error("Unknown LD format");
}

struct RotateOp
{
    const char* mnemonic;
    const char* name;  ///< the name in rasm's messages (SL1 -> SLL, SHL -> SLA, SHR -> SRL)
    int base;
    int pairLength;    ///< bytes of the BC / DE / HL super-instruction
    int pairNops;
    int pairTicks;
    uint8_t pairs[3][8];
};

const RotateOp rotateOps[] = {
    {"RLC", "RLC", 0x00, 8, 8, 32,
     {{0xCB, 0x20, 0xCB, 0x11, 0xCB, 0x18, 0xCB, 0x00},
      {0xCB, 0x22, 0xCB, 0x13, 0xCB, 0x1A, 0xCB, 0x02},
      {0xCB, 0x24, 0xCB, 0x15, 0xCB, 0x1C, 0xCB, 0x04}}},
    {"RRC", "RRC", 0x08, 8, 8, 32,
     {{0xCB, 0x38, 0xCB, 0x19, 0xCB, 0x10, 0xCB, 0x08},
      {0xCB, 0x3A, 0xCB, 0x1B, 0xCB, 0x12, 0xCB, 0x0A},
      {0xCB, 0x3C, 0xCB, 0x1D, 0xCB, 0x14, 0xCB, 0x0C}}},
    {"RL", "RL", 0x10, 4, 4, 16, {{0xCB, 0x11, 0xCB, 0x10}, {0xCB, 0x13, 0xCB, 0x12}, {0xCB, 0x15, 0xCB, 0x14}}},
    {"RR", "RR", 0x18, 4, 4, 16, {{0xCB, 0x18, 0xCB, 0x19}, {0xCB, 0x1A, 0xCB, 0x1B}, {0xCB, 0x1C, 0xCB, 0x1D}}},
    {"SLA", "SLA", 0x20, 4, 4, 16, {{0xCB, 0x21, 0xCB, 0x10}, {0xCB, 0x23, 0xCB, 0x12}, {0xCB, 0x25, 0xCB, 0x14}}},
    {"SHL", "SLA", 0x20, 4, 4, 16, {{0xCB, 0x21, 0xCB, 0x10}, {0xCB, 0x23, 0xCB, 0x12}, {0xCB, 0x25, 0xCB, 0x14}}},
    {"SRA", "SRA", 0x28, 4, 4, 16, {{0xCB, 0x28, 0xCB, 0x19}, {0xCB, 0x2A, 0xCB, 0x1B}, {0xCB, 0x2C, 0xCB, 0x1D}}},
    {"SLL", "SLL", 0x30, 4, 4, 16, {{0xCB, 0x31, 0xCB, 0x10}, {0xCB, 0x33, 0xCB, 0x12}, {0xCB, 0x35, 0xCB, 0x14}}},
    {"SL1", "SLL", 0x30, 4, 4, 16, {{0xCB, 0x31, 0xCB, 0x10}, {0xCB, 0x33, 0xCB, 0x12}, {0xCB, 0x35, 0xCB, 0x14}}},
    {"SRL", "SRL", 0x38, 4, 4, 16, {{0xCB, 0x38, 0xCB, 0x19}, {0xCB, 0x3A, 0xCB, 0x1B}, {0xCB, 0x3C, 0xCB, 0x1D}}},
    {"SHR", "SRL", 0x38, 4, 4, 16, {{0xCB, 0x38, 0xCB, 0x19}, {0xCB, 0x3A, 0xCB, 0x1B}, {0xCB, 0x3C, 0xCB, 0x1D}}},
};

// _RLC _RRC _RL _RR _SLA _SRA _SLL _SRL (and the aliases SL1 SHL SHR)
void Encoder::Rotate(const std::string& mnemonic)
{
    const RotateOp* op = nullptr;
    for (const RotateOp& r : rotateOps)
        if (mnemonic == r.mnemonic)
            op = &r;
    const std::string name = op->name;
    // No operand: rasm looks at the word after the statement. RL ... SRL (the last statement of a file, or followed
    // by another statement) end in their final "or" message; RLC / RRC have no final branch and read the next
    // statement's mnemonic as their operand (an error, or nothing at the end of the file): the port reports the error
    // of their one-operand branch
    if (N() == 0 && op->base >= 0x10)
    {
        if (op->base == 0x10 || op->base == 0x18)
            Error("syntax is " + name + " (IX+n),reg8 or " + name + " reg8/(HL)/(IX+n)/(IY+n)");
        else
            Error("syntax is " + name + " reg8/(HL)/(IX+n)/(IY+n) or " + name + " (IX+n),reg8");
        return;
    }
    if (N() <= 1)
    {
        const std::string w = N() ? Op(0) : std::string();
        const int r = Reg8(w);
        const int pair = w == "BC" ? 0 : w == "DE" ? 1 : w == "HL" ? 2 : -1;
        if (pair >= 0)
        {
            for (int i = 0; i < op->pairLength; i++)
                Byte(op->pairs[pair][i]);
            Time(op->pairNops, op->pairTicks);
        }
        else if (r == 6)
        {
            Bytes({0xCB, op->base + 6});
            Time(4, 15);
        }
        else if (r >= 0)
        {
            Bytes({0xCB, op->base + r});
            Time(2, 8);
        }
        else if (IsIndexMem(w, 'X') || IsIndexMem(w, 'Y'))
        {
            const uint8_t p = w[2] == 'X' ? 0xDD : 0xFD;
            Bytes({p, 0xCB});
            Push(FixupKind::IV8, w, p);
            Byte(op->base + 6);
            Time(7, 23);
        }
        else
            Error("syntax is " + name + " reg8/(HL)/(IX+n)/(IY+n)");
        return;
    }
    // (IX+d),r: the undocumented copy to a register; operands after the second one are ignored, as by rasm
    const std::string& m = Op(0);
    const std::string message = "syntax is " + name + " (IX+n),reg8";
    uint8_t p = 0;
    if (IsIndexMem(m, 'X'))
        p = 0xDD;
    else if (IsIndexMem(m, 'Y'))
        p = 0xFD;
    if (p)
        Byte(p);
    else
        Error(message);
    Byte(0xCB);
    const int r = Reg8(Op(1));
    if (r >= 0 && r != 6)
    {
        if (p)
            Push(FixupKind::IV8, m, p);
        else
            _out.bytes.push_back(0);  // rasm patches a non-index word: the statement is an error anyway
        Byte(op->base + r);
        Time(7, 23);
    }
    else
        Error(message);
}

void Encoder::Srl8()
{
    if (N() == 0)
    {
        Error("syntax is SRL8 BC/DE/HL/IX/IY");
        return;
    }
    if (N() > 1)
        return;  // rasm writes nothing and reports nothing
    const std::string& w = Op(0);
    if (w == "BC")
        Bytes({0x48, 0x06, 0x00});
    else if (w == "DE")
        Bytes({0x5A, 0x16, 0x00});
    else if (w == "HL")
        Bytes({0x6C, 0x26, 0x00});
    else if (w == "IX" || w == "IY")
    {
        const int p = w == "IX" ? 0xDD : 0xFD;
        Bytes({p, 0x6C, p, 0x26, 0x00});
        Time(5, 19);
        return;
    }
    else
    {
        Error("syntax is SRL8 BC/DE/HL/IX/IY");
        return;
    }
    Time(3, 11);
}

// _BIT _RES _SET
void Encoder::BitResSet(const std::string& mnemonic)
{
    const bool bit = mnemonic == "BIT";
    const int base = bit ? 0x40 : mnemonic == "RES" ? 0x80 : 0xC0;
    if (N() == 2)
    {
        const std::string& w = Op(1);
        const int r = Reg8(w);
        if (r >= 0)
        {
            Byte(0xCB);
            Push(FixupKind::BitResSet, Op(0), static_cast<uint8_t>(base + r));
            Byte(base + r);
            if (r == 6)
                Time(bit ? 3 : 4, bit ? 12 : 15);
            else
                Time(2, 8);
        }
        else if (IsIndexMem(w, 'X') || IsIndexMem(w, 'Y'))
        {
            const uint8_t p = w[2] == 'X' ? 0xDD : 0xFD;
            Bytes({p, 0xCB});
            Push(FixupKind::IV8, w, p);
            Push(FixupKind::BitResSet, Op(0), static_cast<uint8_t>(base + 6));
            Byte(base + 6);
            Time(bit ? 6 : 7, bit ? 20 : 23);
        }
        else
            Error("syntax is " + mnemonic + " n,reg8/(HL)/(IX+n)/(IY+n)");
        return;
    }
    if (N() == 3)
    {
        if (bit)
        {
            Error("there is no syntax BIT n,(IX+n),reg8");
            return;
        }
        const std::string& m = Op(1);
        const std::string message = "syntax is " + mnemonic + " n,(IX+n),reg8";
        if (!IsIndexMem(m, 'X') && !IsIndexMem(m, 'Y'))
        {
            Error(message);
            return;
        }
        const uint8_t p = m[2] == 'X' ? 0xDD : 0xFD;
        Bytes({p, 0xCB});
        const int r = Reg8(Op(2));
        if (r >= 0 && r != 6)
        {
            Push(FixupKind::IV8, m, p);
            Push(FixupKind::BitResSet, Op(0), static_cast<uint8_t>(base + r));
            Byte(base + r);
            Time(7, 23);
        }
        else
            Error(message);
        return;
    }
    Error(bit ? "syntax is BIT n,reg8/(HL)/(IX+n)/(IY+n)"
              : "syntax is " + mnemonic + " n,reg8/(HL)/(IX+n)[,reg8]/(IY+n)[,reg8]");
}

/// Instructions without operands, some with an optional repetition count
struct SimpleOp
{
    const char* mnemonic;
    uint8_t length;
    uint8_t bytes[2];
    int nops;
    int ticks;
    /// 0: no repetition; 1: count >= 1 (else repetitionError); 2: count >= 0
    int repetition;
    const char* parameterError;
    const char* repetitionError;
};

const SimpleOp simpleOps[] = {
    {"NEG", 2, {0xED, 0x44}, 2, 8, 0, "NEG does not need parameter", nullptr},
    {"DAA", 1, {0x27, 0}, 1, 4, 0, "DAA does not need parameter", nullptr},
    {"CPL", 1, {0x2F, 0}, 1, 4, 0, "CPL does not need parameter", nullptr},
    {"RETI", 2, {0xED, 0x4D}, 4, 14, 0, "RETI does not need parameter", nullptr},
    {"SCF", 1, {0x37, 0}, 1, 4, 0, "SCF does not need parameter", nullptr},
    {"LDDR", 2, {0xED, 0xB8}, 5, 16, 0, "LDDR does not need parameter", nullptr},
    {"LDIR", 2, {0xED, 0xB0}, 5, 16, 0, "LDIR does not need parameter", nullptr},
    {"CCF", 1, {0x3F, 0}, 1, 4, 0, "CCF does not need parameter", nullptr},
    {"CPD", 2, {0xED, 0xA9}, 4, 16, 0, "CPD does not need parameter", nullptr},
    {"CPDR", 2, {0xED, 0xB9}, 4, 16, 0, "CPDR does not need parameter", nullptr},
    {"CPI", 2, {0xED, 0xA1}, 4, 16, 0, "CPI does not need parameter", nullptr},
    {"CPIR", 2, {0xED, 0xB1}, 4, 16, 0, "CPIR does not need parameter", nullptr},
    {"OTDR", 2, {0xED, 0xBB}, 5, 16, 0, "OTDR does not need parameter", nullptr},
    {"OTIR", 2, {0xED, 0xB3}, 5, 16, 0, "OTIR does not need parameter", nullptr},
    {"RETN", 2, {0xED, 0x45}, 4, 14, 0, "RETN does not need parameter", nullptr},
    {"INDR", 2, {0xED, 0xBA}, 5, 16, 0, "INDR does not need parameter", nullptr},
    {"INIR", 2, {0xED, 0xB2}, 5, 16, 0, "INIR does not need parameter", nullptr},
    {"EXX", 1, {0xD9, 0}, 1, 4, 0, "EXX does not need parameter", nullptr},
    {"RLA", 1, {0x17, 0}, 1, 4, 0, "RLA does not need parameter", nullptr},
    {"RRA", 1, {0x1F, 0}, 1, 4, 0, "RRA does not need parameter", nullptr},
    {"RLD", 2, {0xED, 0x6F}, 5, 18, 0, "RLD does not need parameter", nullptr},
    {"RRD", 2, {0xED, 0x67}, 5, 18, 0, "RRD does not need parameter", nullptr},
    {"EXA", 1, {0x08, 0}, 1, 4, 0, "EXA alias does not need parameter", nullptr},
    {"DI", 1, {0xF3, 0}, 1, 4, 0, "DI does not need parameter", nullptr},
    {"EI", 1, {0xFB, 0}, 1, 4, 0, "EI does not need parameter", nullptr},
    {"RLCA", 1, {0x07, 0}, 1, 4, 1, "RLCA does not need parameter except if you want repetition",
     "RLCA <repetition> allows only non zero positive values"},
    {"RRCA", 1, {0x0F, 0}, 1, 4, 2, "RRCA does not need parameter except if you want repetition",
     "RLCA <repetition> allows only non zero positive values"},
    {"LDD", 2, {0xED, 0xA8}, 5, 16, 1, "LDD does not need parameter except if you want repetition",
     "LDD minimum count is 1"},
    {"LDI", 2, {0xED, 0xA0}, 5, 16, 1, "LDI does not need parameter except if you want repetition",
     "LDI minimum count is 1"},
    {"OUTD", 2, {0xED, 0xAB}, 5, 16, 1, "OUTD does not need parameter except if you want repetition",
     "OUTD minimum count is 1"},
    {"OTD", 2, {0xED, 0xAB}, 5, 16, 1, "OUTD does not need parameter except if you want repetition",
     "OUTD minimum count is 1"},
    {"OUTI", 2, {0xED, 0xA3}, 5, 16, 1, "OUTI does not need parameter except if you want repetition",
     "OUTI minimum count is 1"},
    {"OTI", 2, {0xED, 0xA3}, 5, 16, 1, "OUTI does not need parameter except if you want repetition",
     "OUTI minimum count is 1"},
    {"IND", 2, {0xED, 0xAA}, 5, 16, 1, "IND does not need parameter except if you want repetition",
     "IND minimum count is 1"},
    {"INI", 2, {0xED, 0xA2}, 5, 16, 1, "INI does not need parameter except if you want repetition",
     "INI minimum count is 1"},
    {"HALT", 1, {0x76, 0}, 1, 4, 2, "HALT does not need parameter (except if you want repetition)",
     "HALT <repetition> must use posivite value"},
    {"NOP", 1, {0x00, 0}, 1, 4, 2, "NOP is supposed to be used without parameter or with one optional parameter",
     "NOP <repetition> must use positive value"},
};

bool Encoder::Simple(const std::string& mnemonic)
{
    const SimpleOp* op = nullptr;
    for (const SimpleOp& s : simpleOps)
        if (mnemonic == s.mnemonic)
            op = &s;
    if (!op)
        return false;
    auto emit = [&]()
    {
        for (int i = 0; i < op->length; i++)
            Byte(op->bytes[i]);
        Time(op->nops, op->ticks);
    };
    if (N() == 0)
        emit();
    else if (N() == 1 && op->repetition)
    {
        std::optional<int> count = Count(Op(0));
        if (!count)
            return true;
        if (*count > 0 || (*count == 0 && op->repetition == 2))
        {
            for (int i = 0; i < *count; i++)
                emit();
        }
        else
            Error(op->repetitionError);
    }
    else
        Error(op->parameterError);
    return true;
}

Encoded Encoder::Run(const std::string& m)
{
    if (m == "LD")
        Ld();
    else if (m == "ADD")
        Alu("ADD", 0x80, 0xC6);
    else if (m == "ADC")
        Alu("ADC", 0x88, 0xCE);
    else if (m == "SUB")
        Alu("SUB", 0x90, 0xD6);
    else if (m == "SBC")
        Alu("SBC", 0x98, 0xDE);
    else if (m == "AND")
        Alu("AND", 0xA0, 0xE6);
    else if (m == "XOR")
        Alu("XOR", 0xA8, 0xEE);
    else if (m == "OR")
        Alu("OR", 0xB0, 0xF6);
    else if (m == "CP")
        Alu("CP", 0xB8, 0xFE);
    else if (m == "INC" || m == "DEC")
        IncDec(m == "INC");
    else if (m == "PUSH" || m == "POP")
        PushPop(m == "PUSH");
    else if (m == "IN")
        In();
    else if (m == "OUT")
        Out();
    else if (m == "EX")
        Ex();
    else if (m == "RET")
        Ret();
    else if (m == "CALL")
        Call();
    else if (m == "JR")
        Jr();
    else if (m == "JP")
        Jp();
    else if (m == "IM")
        Im();
    else if (m == "RST")
        Rst();
    else if (m == "DJNZ")
        Djnz();
    else if (m == "SRL8")
        Srl8();
    else if (m == "BIT" || m == "RES" || m == "SET")
        BitResSet(m);
    else if (std::any_of(std::begin(rotateOps), std::end(rotateOps), [&](const RotateOp& r) { return m == r.mnemonic; }))
        Rotate(m);
    else if (!Simple(m))
        Error("unknown instruction [" + m + "]");
    return std::move(_out);
}

const char* const instructionNames[] = {
    "LD",   "DEC",  "INC",  "ADD",  "SUB",  "OR",   "AND",  "XOR",  "POP",  "PUSH", "DJNZ", "JR",   "JP",   "CALL", "RET",
    "EX",   "ADC",  "SBC",  "EXA",  "EXX",  "CP",   "BIT",  "RES",  "SET",  "IN",   "OUT",  "RLC",  "RRC",  "RL",   "RR",
    "SLA",  "SRA",  "SLL",  "SL1",  "SRL",  "SRL8", "RST",  "HALT", "DI",   "EI",   "NOP",  "LDI",  "LDIR", "OUTI", "INI",
    "RLCA", "RRCA", "NEG",  "RLA",  "RRA",  "RLD",  "RRD",  "DAA",  "CPL",  "SCF",  "LDD",  "LDDR", "CCF",  "OUTD", "IND",
    "RETI", "RETN", "IM",   "CPD",  "CPI",  "CPDR", "CPIR", "OTDR", "OTIR", "INDR", "INIR", "OTD",  "OTI",  "SHL",  "SHR"};

// ---------------------------------------------------------------------------------------------------------------------
// GETNOP / GETTICK / GETSIZE: rasm's simplified parser of the code string

/// rasm's TxtReplace: every occurrence left to right; `recurse` restarts from the beginning after each replacement
void TxtReplace(std::string& s, const std::string& from, const std::string& to, bool recurse)
{
    if (s.empty() || from.empty())
        return;
    size_t pos = s.find(from);
    while (pos != std::string::npos)
    {
        s.replace(pos, from.size(), to);
        pos = recurse ? s.find(from) : s.find(from, pos + to.size());
    }
}

struct TimingStatement
{
    std::string token;               ///< rasm's opcode[idx] in the error texts: the piece up to the opcode's end
    std::string opcode;
    std::optional<std::string> arg;  ///< null when nothing follows the opcode, "" when only blanks do
};

std::vector<TimingStatement> SplitTimingCode(std::string_view code)
{
    std::string s(code);
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    TxtReplace(s, "\t", " ", false);
    TxtReplace(s, "  ", " ", true);
    TxtReplace(s, ": ", ":", true);
    TxtReplace(s, "IY", "IX", false);
    static const char* const halves[] = {"IXL", "IXH", "LX", "HX", "LY", "HY", "YL", "XH", "YH"};
    for (const char* h : halves)
        TxtReplace(s, h, "XL", false);

    // TxtSplitWithChar: pieces between ':', no piece after a final ':'
    std::vector<std::string> pieces;
    size_t start = 0;
    while (start < s.size())
    {
        size_t end = s.find(':', start);
        if (end == std::string::npos)
        {
            pieces.push_back(s.substr(start));
            break;
        }
        pieces.push_back(s.substr(start, end - start));
        start = end + 1;
    }

    std::vector<TimingStatement> out;
    for (const std::string& piece : pieces)
    {
        size_t first = 0;
        while (first < piece.size() && piece[first] == ' ')
            first++;
        size_t terminator = first;
        while (terminator < piece.size() && piece[terminator] != ' ')
            terminator++;
        TimingStatement st;
        st.token = piece.substr(0, terminator);
        st.opcode = piece.substr(first, terminator - first);
        if (terminator < piece.size())
        {
            std::string arg = piece.substr(terminator + 1);
            arg.erase(std::remove(arg.begin(), arg.end(), ' '), arg.end());
            st.arg = arg;
        }
        if (!st.opcode.empty())
            out.push_back(std::move(st));
    }
    return out;
}

bool In(const std::string& w, std::initializer_list<const char*> list)
{
    return std::any_of(list.begin(), list.end(), [&](const char* n) { return w == n; });
}

bool Contains(const std::optional<std::string>& arg, const char* what)
{
    return arg && arg->find(what) != std::string::npos;
}

bool IsParenthesised(const std::string& w)
{
    return !w.empty() && w[0] == '(' && w.back() == ')';
}

/// TxtSplitWithChar(arg, ',')
std::vector<std::string> SplitComma(const std::string& s)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start < s.size())
    {
        size_t end = s.find(',', start);
        if (end == std::string::npos)
        {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

enum class TimingMode
{
    Nop,
    Tick,
    Size
};

/// The value of one statement for GETNOP / GETTICK / GETSIZE (the three switch statements of rasm, case by case)
class TimingCounter
{
public:
    explicit TimingCounter(TimingMode mode) : _mode(mode) {}
    TimingSum Run(std::string_view code)
    {
        for (TimingStatement& st : SplitTimingCode(code))
            Statement(st);
        return {_value, _error};
    }

private:
    TimingMode _mode;
    int _value = 0;
    std::string _error;

    const char* Name() const { return _mode == TimingMode::Nop ? "GETNOP" : _mode == TimingMode::Tick ? "GETTICK" : "GETSIZE"; }
    int Pick(int nop, int tick, int size) const
    {
        return _mode == TimingMode::Nop ? nop : _mode == TimingMode::Tick ? tick : size;
    }
    void Add(int nop, int tick, int size) { _value += Pick(nop, tick, size); }
    void Error(const std::string& message)
    {
        if (_error.empty())
            _error = message;
    }
    void Unsupported(const TimingStatement& st)
    {
        Error("unsupported opcode [" + st.token + "] for " + Name() + ", see documentation about this directive");
    }
    void UnsupportedLd(const std::vector<std::string>& args)
    {
        Error("unsupported LD " + args[0] + "," + args[1] + " for " + Name() + ", see documentation");
    }
    void Statement(TimingStatement& st);
    void Ld(const TimingStatement& st);
    void LdNop(const std::vector<std::string>& a);
    void LdTick(const std::vector<std::string>& a);
    void LdSize(const std::vector<std::string>& a);
};

void TimingCounter::Statement(TimingStatement& st)
{
    const std::string& op = st.opcode;
    std::optional<std::string>& arg = st.arg;
    const bool nop = _mode == TimingMode::Nop;
    const bool tick = _mode == TimingMode::Tick;
    const bool size = _mode == TimingMode::Size;

    // the one-value groups
    if (In(op, {"RLA", "RLCA", "RRCA", "RRA", "NOP", "CCF", "DAA", "SCF", "CPL", "EXX", "EI", "DI"}))
    {
        Add(1, 4, 1);
        return;
    }
    if (op == "EXA" && nop)
    {
        Add(1, 0, 0);
        return;
    }
    if (op == "HALT" && size)
    {
        Add(0, 0, 1);
        return;
    }
    if (op == "SRL8" && nop)
    {
        _value += 3;
        if (Contains(arg, "IX"))
            _value += 2;
        if (Contains(arg, "IY"))
            _value += 2;
        return;
    }
    if (op == "IM" || op == "NEG")
    {
        Add(2, 8, 2);
        return;
    }
    if (op == "RST")
    {
        Add(4, 11, 1);
        return;
    }
    if (op == "RETN" || op == "RETI")
    {
        Add(4, 14, 2);
        return;
    }
    if (In(op, {"CPDR", "CPIR", "CPD", "CPI"}))
    {
        Add(4, 16, 2);
        return;
    }
    if (op == "RLD" || op == "RRD")
    {
        Add(5, 18, 2);
        return;
    }
    if (In(op, {"LDD", "LDI", "OUTI", "OUTD", "LDIR", "LDDR", "INIR", "INDR", "OTIR", "OTDR", "IND", "INI"}))
    {
        Add(5, 16, 2);
        return;
    }
    if (op == "EX")
    {
        if (!arg)
        {
            Unsupported(st);
            return;
        }
        if (Contains(arg, "AF") || Contains(arg, "DE"))
            Add(1, 4, 1);
        else if (Contains(arg, "(SP)") && Contains(arg, "HL"))
            Add(6, 19, 1);
        else if (Contains(arg, "(SP)") && Contains(arg, "IX"))
            Add(7, 23, 2);
        return;
    }
    if (op == "PUSH" || op == "POP")
    {
        if (!arg)
        {
            Unsupported(st);
            return;
        }
        const bool ix = *arg == "IX";
        if (op == "PUSH")
            Add(ix ? 5 : 4, ix ? 15 : 11, ix ? 2 : 1);
        else
            Add(ix ? 4 : 3, ix ? 14 : 10, ix ? 2 : 1);
        return;
    }
    if (In(op, {"SLA", "SLL", "SRA", "SRL", "RL", "RLC", "RR", "RRC"}))
    {
        if (!arg)
        {
            Unsupported(st);
            return;
        }
        if (Contains(arg, "(HL)"))
            Add(4, 15, 2);
        else if (Contains(arg, "(IX"))
            Add(7, 23, 4);
        else
            Add(2, 8, 2);
        return;
    }
    if (op == "OUT" || op == "IN")
    {
        if (size)
        {
            _value += 2;
            return;
        }
        if (!arg)
        {
            Unsupported(st);
            return;
        }
        if (nop)
            _value += Contains(arg, "(C)") ? 4 : 3;
        else if (op == "OUT")
            _value += Contains(arg, "(C),") ? 12 : 11;
        else
            _value += Contains(arg, "(C)") ? 12 : 11;
        return;
    }
    if (op == "ADD")
    {
        if (!arg)
        {
            Unsupported(st);
            return;
        }
        std::string& a = *arg;
        TxtReplace(a, "A,", "", false);
        const bool r8 = (a[0] >= 'A' && a[0] <= 'E') || a[0] == 'H' || a[0] == 'L';
        if (nop)
        {
            if (In(a, {"IX,BC", "IX,DE", "IX,IX", "IX,SP"}))
                _value += 4;
            else if (In(a, {"HL,BC", "HL,DE", "HL,HL", "HL,SP"}))
                _value += 3;
            else if (Contains(arg, "(HL)") || a == "XL")
                _value += 2;
            else if (Contains(arg, "(IX"))
                _value += 5;
            else
                _value += r8 ? 1 : 2;
        }
        else if (tick)
        {
            if (In(a, {"IX,BC", "IX,DE", "IX,IX", "IX,SP"}))
                _value += 15;
            else if (In(a, {"HL,BC", "HL,DE", "HL,HL", "HL,SP"}))
                _value += 11;
            else if (Contains(arg, "(HL)"))
                _value += 7;
            else if (Contains(arg, "(IX"))
                _value += 19;
            else if (Contains(arg, "XL"))
                _value += 8;
            else
                _value += r8 ? 4 : 7;
        }
        else
        {
            if (a == "IX," || a == "XL")
                _value += 2;
            else if (Contains(arg, "(IX"))
                _value += 3;
            else if (Contains(arg, "HL") || r8)
                _value += 1;
            else
                _value += 2;
        }
        return;
    }
    if (In(op, {"ADC", "SBC", "SUB", "XOR", "AND", "OR", "CP"}))
    {
        if ((op == "ADC" || op == "SBC") && arg)
        {
            TxtReplace(*arg, "A,", "", false);
            if (In(*arg, {"HL,BC", "HL,DE", "HL,HL", "HL,SP"}))
            {
                Add(4, 15, 2);
                return;
            }
        }
        if ((op == "ADC" || op == "SBC" || op == "SUB") && arg)
            TxtReplace(*arg, "A,", "", false);
        if (!arg)
        {
            Unsupported(st);
            return;
        }
        const std::string& a = *arg;
        const bool r8 = !a.empty() && ((a[0] >= 'A' && a[0] <= 'E') || a[0] == 'H' || a[0] == 'L');
        if (nop)
        {
            if (Contains(arg, "(HL)") || a == "XL")
                _value += 2;
            else if (Contains(arg, "(IX"))
                _value += 5;
            else
                _value += r8 ? 1 : 2;
        }
        else if (tick)
        {
            if (Contains(arg, "(HL)"))
                _value += 7;
            else if (Contains(arg, "(IX"))
                _value += 19;
            else if (Contains(arg, "XL"))
                _value += 8;
            else
                _value += r8 ? 4 : 7;
        }
        else
        {
            if (Contains(arg, "(IX"))
                _value += 3;
            else if (a == "XL")
                _value += 2;
            else if (a == "(HL)" || r8)
                _value += 1;
            else
                _value += 2;
        }
        return;
    }
    if (op == "BIT" || op == "RES" || op == "SET")
    {
        if (!arg)
        {
            Unsupported(st);  // rasm dereferences the missing argument
            return;
        }
        if (size)
            _value += Contains(arg, "(IX") ? 4 : 2;
        else if (op == "BIT")
            _value += Contains(arg, "(HL)") ? Pick(3, 12, 0) : Contains(arg, "(IX") ? Pick(6, 20, 0) : Pick(2, 8, 0);
        else
            _value += Contains(arg, "(HL)") ? Pick(4, 15, 0) : Contains(arg, "(IX") ? Pick(7, 23, 0) : Pick(2, 8, 0);
        return;
    }
    if (op == "DEC" || op == "INC")
    {
        if (!arg)
        {
            Unsupported(st);  // rasm dereferences the missing argument
            return;
        }
        const std::string& a = *arg;
        if (nop)
        {
            if (In(a, {"XL", "SP", "BC", "DE", "HL"}))
                _value += 2;
            else if (a == "IX" || a == "(HL)")
                _value += 3;
            else if (a.compare(0, 3, "(IX") == 0)
                _value += 6;
            else
                _value += 1;
        }
        else if (tick)
        {
            if (a == "XL")
                _value += 8;
            else if (In(a, {"SP", "BC", "DE", "HL"}))
                _value += 6;
            else if (a == "IX")
                _value += 10;
            else if (a == "(HL)")
                _value += 11;
            else if (a.compare(0, 3, "(IX") == 0)
                _value += 23;
            else
                _value += 4;
        }
        else
        {
            if (In(a, {"(HL)", "SP", "BC", "DE", "HL"}))
                _value += 1;
            else if (a == "IX" || a == "XL")
                _value += 2;
            else if (a.compare(0, 3, "(IX") == 0)
                _value += 3;
            else
                _value += 1;
        }
        return;
    }
    if (op == "JP")
    {
        if (arg && Contains(arg, "IX"))
            Add(2, 8, 2);
        else if (arg && Contains(arg, "HL"))
            Add(1, 4, 1);
        else
            Add(3, 10, 3);
        return;
    }
    if (op == "DJNZ")
    {
        Add(4, 13, 2);
        return;
    }
    if (op == "CALL")
    {
        if (nop)
        {
            _value += 5;
            if (Contains(arg, "Z"))
                _value -= 2;
            if (Contains(arg, "C"))
                _value -= 2;
        }
        else
            Add(0, 10, 3);
        return;
    }
    if (op == "JR")
    {
        Add(3, 12, 2);
        return;
    }
    if (op == "RET")
    {
        if (size)
            _value += 1;
        else if (!arg)
            Add(3, 10, 0);
        else
            Add(2, 5, 0);
        return;
    }
    if (op == "LD")
    {
        Ld(st);
        return;
    }
    Unsupported(st);
}

void TimingCounter::Ld(const TimingStatement& st)
{
    if (!st.arg || st.arg->find(',') == std::string::npos)
    {
        Error(std::string("unsupported opcode LD for ") + Name() + (_mode == TimingMode::Tick ? ", requires" : ", need") +
              " 2 arguments [" + (st.arg ? *st.arg : std::string("(null)")) + "]");
        return;
    }
    std::vector<std::string> a = SplitComma(*st.arg);
    if (a.size() < 2)
    {
        // GETSIZE does not check this case (rasm reads a null pointer); the GETNOP / GETTICK message is used
        Error(std::string("missing LD argument for ") + Name() + ", see documentation");
        return;
    }
    if (_mode == TimingMode::Nop)
        LdNop(a);
    else if (_mode == TimingMode::Tick)
        LdTick(a);
    else
        LdSize(a);
}

void TimingCounter::LdNop(const std::vector<std::string>& a)
{
    const std::string& d = a[0];
    const std::string& s = a[1];
    const bool sR8 = In(s, {"A", "B", "C", "D", "E", "H", "L"});
    if (d == "I" || d == "R")
    {
        if (s == "A")
            _value += 3;
        else
            UnsupportedLd(a);
    }
    else if (In(d, {"A", "B", "C", "D", "E", "H", "L"}))
    {
        if (sR8)
            _value += 1;
        else if (s == "I" || s == "R")
        {
            if (d == "A")
                _value += 3;
            else
                UnsupportedLd(a);
        }
        else if (s == "(BC)" || s == "(DE)")
        {
            if (d != "A")
                UnsupportedLd(a);
            else
                _value += 2;
        }
        else if (s == "XL" || s == "(HL)")
            _value += 2;
        else if (s.compare(0, 3, "(IX") == 0)
            _value += 5;
        else if (IsParenthesised(s))
        {
            if (d == "A")
                _value += 4;
            else
                UnsupportedLd(a);
        }
        else
            _value += 2;
    }
    else if (d == "XL")
        _value += (sR8 || s == "XL") ? 2 : 3;
    else if (d == "BC" || d == "DE")
        _value += IsParenthesised(s) ? 6 : 3;
    else if (d == "HL")
        _value += IsParenthesised(s) ? 5 : 3;
    else if (d == "SP")
        _value += s == "HL" ? 2 : s == "IX" ? 3 : IsParenthesised(s) ? 6 : 3;
    else if (d == "IX")
        _value += IsParenthesised(s) ? 6 : 4;
    else if (d == "(BC)" || d == "(DE)")
    {
        if (s == "A")
            _value += 2;
        else
            UnsupportedLd(a);
    }
    else if (d == "(HL)")
        _value += sR8 ? 2 : 3;
    else if (d.compare(0, 3, "(IX") == 0)
        _value += sR8 ? 5 : 6;
    else if (IsParenthesised(d))
    {
        if (s == "A")
            _value += 4;
        else if (s == "HL")
            _value += 5;
        else if (In(s, {"BC", "DE", "SP", "IX"}))
            _value += 6;
        else
            UnsupportedLd(a);
    }
    else
        UnsupportedLd(a);
}

void TimingCounter::LdTick(const std::vector<std::string>& a)
{
    const std::string& d = a[0];
    const std::string& s = a[1];
    const bool sR8 = In(s, {"A", "B", "C", "D", "E", "H", "L"});
    if (d == "I" || d == "R")
    {
        if (s == "A")
            _value += 9;
        else
            UnsupportedLd(a);
    }
    else if (In(d, {"A", "B", "C", "D", "E", "H", "L"}))
    {
        if (sR8)
            _value += 4;
        else if (s == "I" || s == "R")
        {
            if (d == "A")
                _value += 9;
            else
                UnsupportedLd(a);
        }
        else if (s == "(BC)" || s == "(DE)")
        {
            if (d != "A")
                UnsupportedLd(a);
            else
                _value += 7;
        }
        else if (s == "(HL)")
            _value += 7;
        else if (s == "XL")
            _value += 8;
        else if (s.compare(0, 3, "(IX") == 0)
            _value += 19;
        else if (IsParenthesised(s))
        {
            if (d == "A")
                _value += 13;
            else
                UnsupportedLd(a);
        }
        else
            _value += 7;
    }
    else if (d == "XL")
        _value += (sR8 || s == "XL") ? 8 : 11;
    else if (d == "BC" || d == "DE")
        _value += IsParenthesised(s) ? 20 : 10;
    else if (d == "HL")
        _value += IsParenthesised(s) ? 16 : 10;
    else if (d == "SP")
        _value += s == "HL" ? 6 : s == "IX" ? 10 : IsParenthesised(s) ? 20 : 10;
    else if (d == "IX")
        _value += IsParenthesised(s) ? 20 : 14;
    else if (d == "(BC)" || d == "(DE)")
    {
        if (s == "A")
            _value += 7;
        else
            UnsupportedLd(a);
    }
    else if (d == "(HL)")
        _value += sR8 ? 7 : 10;
    else if (d.compare(0, 3, "(IX") == 0)
        _value += sR8 ? 19 : 23;
    else if (IsParenthesised(d))
    {
        if (s == "A")
            _value += 13;
        else if (s == "HL")
            _value += 16;
        else if (In(s, {"BC", "DE", "SP", "IX"}))
            _value += 20;
        else
            UnsupportedLd(a);
    }
    else
        UnsupportedLd(a);
}

void TimingCounter::LdSize(const std::vector<std::string>& a)
{
    const std::string& d = a[0];
    const std::string& s = a[1];
    const bool sR8 = In(s, {"A", "B", "C", "D", "E", "H", "L"});
    if (d == "I" || d == "R")
    {
        if (s == "A")
            _value += 2;
        else
            UnsupportedLd(a);
    }
    else if (In(d, {"A", "B", "C", "D", "E", "H", "L"}))
    {
        if (s == "(BC)" || s == "(DE)")
        {
            if (d != "A")
                UnsupportedLd(a);
            else
                _value += 1;
        }
        else if (sR8 || s == "(HL)")
            _value += 1;
        else if (s == "I" || s == "R")
        {
            if (d == "A")
                _value += 2;
            else
                UnsupportedLd(a);
        }
        else if (s == "XL")
            _value += 2;
        else if (s.compare(0, 3, "(IX") == 0)
            _value += 3;
        else if (IsParenthesised(s))
        {
            if (d == "A")
                _value += 3;
            else
                UnsupportedLd(a);
        }
        else
            _value += 2;
    }
    else if (d == "XL")
        _value += (sR8 || s == "XL") ? 2 : 3;
    else if (d == "BC" || d == "DE")
        _value += IsParenthesised(s) ? 4 : 3;
    else if (d == "HL")
        _value += 3;
    else if (d == "SP")
        _value += s == "HL" ? 1 : s == "IX" ? 2 : IsParenthesised(s) ? 4 : 3;
    else if (d == "IX")
        _value += 4;
    else if (d == "(BC)" || d == "(DE)")
    {
        if (s == "A")
            _value += 1;
        else
            UnsupportedLd(a);
    }
    else if (d == "(HL)")
        _value += sR8 ? 1 : 2;
    else if (d.compare(0, 3, "(IX") == 0)
        _value += sR8 ? 3 : 4;
    else if (IsParenthesised(d))
    {
        if (s == "A" || s == "HL")
            _value += 3;
        else if (In(s, {"BC", "DE", "SP", "IX"}))
            _value += 4;
        else
            UnsupportedLd(a);
    }
    else
        UnsupportedLd(a);
}
}  // namespace

bool IsInstruction(std::string_view mnemonic)
{
    return std::any_of(std::begin(instructionNames), std::end(instructionNames),
                       [&](const char* n) { return mnemonic == n; });
}

Encoded EncodeInstruction(std::string_view mnemonic, const std::vector<std::string>& operands, const EncoderOptions& options,
                          const std::function<std::optional<double>(const std::string& expression)>& evaluateNow)
{
    Encoder encoder(operands, options, evaluateNow);
    return encoder.Run(std::string(mnemonic));
}

FixupResult ApplyFixup(std::vector<uint8_t>& bytes, const Fixup& fixup, double value, uint32_t instructionAddress,
                       const EncoderOptions& options, std::string_view shownExpression)
{
    FixupResult result;
    const std::string shown = shownExpression.empty() ? fixup.expression : std::string(shownExpression);
    long long r = static_cast<long long>(std::floor(value + options.rough));
    const uint32_t at = fixup.offset;
    auto need = [&](uint32_t count)
    {
        if (bytes.size() < static_cast<size_t>(at) + count)
            bytes.resize(static_cast<size_t>(at) + count, 0);
    };
    auto warn = [&](const std::string& text)
    {
        if (!options.noWarning)
            result.warning = text;
    };
    const long long dollar = static_cast<long long>(instructionAddress) + fixup.instructionOffset;
    switch (fixup.kind)
    {
        case FixupKind::J8:
            need(1);
            r = r - dollar - 2;
            if (r < -128 || r > 127)
                result.error = Format("relative offset %lld too far [", r) + shown + "]";
            bytes[at] = static_cast<uint8_t>(r);
            break;
        case FixupKind::IV81:
        case FixupKind::IV8:
            need(1);
            if (fixup.kind == FixupKind::IV81)
                r++;
            // rasm normalises the Maxam-mode wraparound of "0-4" (65532) before the range check
            if (r > 32767)
                r -= 65536;
            if (r > 127 || r < -128)
            {
                const std::string word = std::string("(I") + (fixup.extra == 0xFD ? "Y" : "X") +
                                         (shown.size() > 2 ? shown.substr(2) : std::string());
                result.error = "offset modifier " + word + Format(" must be between -128 and 127 inclusive : current value = %lld", r);
            }
            bytes[at] = static_cast<uint8_t>(r);
            break;
        case FixupKind::V8At0:
        case FixupKind::V8At3:
        case FixupKind::V8:
            need(1);
            if (r > 255 || r < -128)
                warn(Format("truncating value #%llX to #%llX", static_cast<long long>(static_cast<uint32_t>(r)), r & 0xFF));
            bytes[at] = static_cast<uint8_t>(r);
            break;
        case FixupKind::J16:
        case FixupKind::IV16:
        case FixupKind::V16:
        case FixupKind::V16At0:
            need(2);
            if (r > 65535 || r < -32768)
                warn(Format("truncating value #%llX to #%llX", static_cast<long long>(static_cast<uint32_t>(r)), r & 0xFFFF));
            bytes[at] = static_cast<uint8_t>(r & 0xFF);
            bytes[at + 1] = static_cast<uint8_t>((r & 0xFF00) >> 8);
            break;
        case FixupKind::V32At0:
            need(4);
            if (value > 4294967295.0 || value < -2147483648.0)
                warn("truncating value");
            for (int i = 0; i < 4; i++)
                bytes[at + i] = static_cast<uint8_t>((r >> (8 * i)) & 0xFF);
            break;
        case FixupKind::RealAmsdos:
        case FixupKind::RealMicrosoft:
            result.error = "real values are written by the data directives, not by the instruction layer";
            break;
        case FixupKind::Float24:
        {
            need(3);
            const float f = static_cast<float>(value);
            uint32_t bits = 0;
            std::memcpy(&bits, &f, sizeof(bits));
            const uint32_t exponent = (((bits >> 23) & 0xFF) - 64) | ((bits >> 24) & 0x80);
            bytes[at] = static_cast<uint8_t>(exponent & 0xFF);
            bytes[at + 2] = static_cast<uint8_t>((bits >> 15) & 0xFF);
            bytes[at + 1] = static_cast<uint8_t>((bits >> 7) & 0xFF);
            break;
        }
        case FixupKind::Im:
            need(1);
            if (r == 0)
                bytes[at] = 0x46;
            else if (r == 1)
                bytes[at] = 0x56;
            else if (r == 2)
                bytes[at] = 0x5E;
            else
            {
                result.error = "IM 0,1 or 2 only";
                bytes[at] = 0;
            }
            break;
        case FixupKind::RstConditional:
            need(1);
            if (r == 0x38)
                bytes[at] = 0xFF;
            else
                result.error = "RST condition,#38 only";
            break;
        case FixupKind::Rst:
            need(1);
            if (r >= 0 && r <= 0x38 && (r & 7) == 0)
                bytes[at] = static_cast<uint8_t>(0xC7 + r);
            else
            {
                result.error = "RST #0,#8,#10,#18,#20,#28,#30,#38 only";
                bytes[at] = 0;
            }
            break;
        case FixupKind::BitResSet:
            need(1);
            if (r >= 0 && r < 8)
                bytes[at] = static_cast<uint8_t>(bytes[at] + r * 8);
            else
                result.error = "SET,RES,BIT shift value from 0 to 7 only";
            break;
    }
    return result;
}

Timing InstructionTiming(std::string_view mnemonic, const std::vector<std::string>& operands)
{
    // repetition counts: plain decimal or #hex numbers only
    auto plainNumber = [](const std::string& text) -> std::optional<double>
    {
        if (text.empty())
            return std::nullopt;
        const bool hex = text[0] == '#';
        const std::string digits = hex ? text.substr(1) : text;
        if (digits.empty())
            return std::nullopt;
        double v = 0;
        for (char c : digits)
        {
            int d = -1;
            if (c >= '0' && c <= '9')
                d = c - '0';
            else if (hex && c >= 'A' && c <= 'F')
                d = c - 'A' + 10;
            if (d < 0)
                return std::nullopt;
            v = v * (hex ? 16 : 10) + d;
        }
        return v;
    };
    const Encoded e = EncodeInstruction(mnemonic, operands, EncoderOptions(), plainNumber);
    Timing t;
    t.nops = e.nops;
    t.ticks = e.ticks;
    t.size = static_cast<int>(e.bytes.size());
    t.known = e.error.empty();
    return t;
}

TimingSum GetNop(std::string_view code)
{
    return TimingCounter(TimingMode::Nop).Run(code);
}

TimingSum GetTick(std::string_view code)
{
    return TimingCounter(TimingMode::Tick).Run(code);
}

TimingSum GetSize(std::string_view code)
{
    return TimingCounter(TimingMode::Size).Run(code);
}

TimingSum Duration(std::string_view code, bool zxTicks)
{
    return zxTicks ? GetTick(code) : GetNop(code);
}
}  // namespace unrealasm::rasm
