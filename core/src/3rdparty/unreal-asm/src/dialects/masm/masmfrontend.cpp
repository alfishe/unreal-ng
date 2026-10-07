#include "dialects/masm/masmfrontend.h"

#include <cctype>
#include <set>

#include "dialects/common/z80.h"

namespace unrealasm::dialects
{
namespace
{
using ir::Expr;
using ir::Op;
using ir::Operand;
using ir::Statement;

struct Failure
{
    std::string reason;
};

/// MASM 1.1's label characters (its SKIN routine): a letter, _ ? @ first, then digits and . too
bool IsLabelStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '?' || c == '@';
}

bool IsLabelChar(char c)
{
    return IsLabelStart(c) || std::isdigit(static_cast<unsigned char>(c)) || c == '.';
}

bool IsHexDigit(char c)   // capitals only (the PH1 routine)
{
    return std::isdigit(static_cast<unsigned char>(c)) || (c >= 'A' && c <= 'F');
}

/// Expressions as MASM 1.1's BITE / CALC routines read them: terms joined left to right, no priorities, no
/// parentheses, no unary minus, 16-bit words; @ is XOR, | OR
struct ExpressionParser
{
    std::string_view t;
    size_t i = 0;

    void Blanks()
    {
        while (i < t.size() && t[i] == ' ')
            ++i;
    }
    char Peek() const { return i < t.size() ? t[i] : '\0'; }

    Expr Digits(size_t from, size_t to, int base, ir::NumberSpelling spelling)
    {
        if (to == from)
            throw Failure{"a number without digits"};
        return Expr::Number(std::stoll(std::string(t.substr(from, to - from)), nullptr, base) & 0xFFFF, spelling, static_cast<int>(to - from));
    }

    Expr Term()
    {
        Blanks();
        const char c = Peek();
        if (c == '#')
        {
            size_t j = ++i;
            while (j < t.size() && IsHexDigit(t[j]))
                ++j;
            const Expr e = Digits(i, j, 16, ir::NumberSpelling::Hex);
            i = j;
            return e;
        }
        if (c == '%')
        {
            size_t j = ++i;
            while (j < t.size() && (t[j] == '0' || t[j] == '1'))
                ++j;
            const Expr e = Digits(i, j, 2, ir::NumberSpelling::Binary);
            i = j;
            return e;
        }
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            // Decimal, or hex digits ending in H (0C000H)
            size_t j = i;
            while (j < t.size() && IsHexDigit(t[j]))
                ++j;
            if (j < t.size() && t[j] == 'H')
            {
                const Expr e = Digits(i, j, 16, ir::NumberSpelling::Hex);
                i = j + 1;
                return e;
            }
            j = i;
            while (j < t.size() && std::isdigit(static_cast<unsigned char>(t[j])))
                ++j;
            const Expr e = Digits(i, j, 10, ir::NumberSpelling::Decimal);
            i = j;
            return e;
        }
        if (c == '"')
        {
            // A character constant: the last two characters make the word, "" is one quote (the KOB routine)
            std::string chars;
            size_t j = i + 1;
            while (j < t.size())
            {
                if (t[j] == '"')
                {
                    if (j + 1 < t.size() && t[j + 1] == '"')
                    {
                        chars.push_back('"');
                        j += 2;
                        continue;
                    }
                    break;
                }
                chars.push_back(t[j++]);
            }
            i = j < t.size() ? j + 1 : j;
            int64_t value = 0;
            for (const char ch : chars)
                value = ((value << 8) | static_cast<unsigned char>(ch)) & 0xFFFF;
            Expr e = Expr::Number(value, ir::NumberSpelling::Character, static_cast<int>(chars.size()));
            e.text = chars;
            return e;
        }
        if (c == '$')
        {
            ++i;
            return Expr::Make(Expr::Kind::Current);
        }
        if (IsLabelStart(c))
        {
            size_t j = i;
            while (j < t.size() && IsLabelChar(t[j]))
                ++j;
            Expr e = Expr::Symbol(std::string(t.substr(i, j - i)));
            i = j;
            return e;
        }
        throw Failure{std::string("unexpected '") + c + "' in an expression"};
    }

    Expr Whole()
    {
        Expr acc = Term();
        while (true)
        {
            Blanks();
            Op op;
            switch (Peek())
            {
                case '+': op = Op::Add; break;
                case '-': op = Op::Sub; break;
                case '*': op = Op::Mul; break;
                case '/': op = Op::Div; break;
                case '&': op = Op::And; break;
                case '|': op = Op::Or; break;
                case '@': op = Op::Xor; break;
                case '\0': return acc;
                default: throw Failure{"unexpected text after an expression: " + std::string(t.substr(i))};
            }
            ++i;
            acc = Expr::Binary(op, std::move(acc), Term());
        }
    }
};

Expr Parse(std::string_view text)
{
    ExpressionParser p{text};
    return p.Whole();
}

/// Splits operands at commas outside quotes and parentheses; blanks around each removed
std::vector<std::string> SplitOperands(std::string_view text)
{
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    bool quote = false;
    for (const char c : text)
    {
        if (c == '"')
            quote = !quote;
        else if (!quote && c == '(')
            ++depth;
        else if (!quote && c == ')' && depth > 0)
            --depth;
        else if (!quote && c == ',' && depth == 0)
        {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty() || !out.empty())
        out.push_back(cur);
    for (std::string& s : out)
    {
        while (!s.empty() && s.front() == ' ')
            s.erase(s.begin());
        while (!s.empty() && s.back() == ' ')
            s.pop_back();
    }
    return out;
}

// Register and condition tokens as MASM spells them (capitals only: a lower-case word is text, a label)
const std::set<std::string> kRegisters = {"A", "B", "C", "D", "E", "H", "L", "I", "R", "XH", "XL", "YH", "YL", "IX", "IY", "AF'", "AF", "HL", "DE", "BC", "SP"};
const std::set<std::string> kConditions = {"M", "NC", "NV", "NZ", "P", "PE", "PO", "V", "Z", "C"};

Operand MakeOperand(const std::string& text, bool conditionAllowed)
{
    Operand o;
    if (conditionAllowed && kConditions.count(text))
    {
        o.kind = Operand::Kind::Condition;
        o.text = text == "NV" ? "po" : text == "V" ? "pe" : z80::Lower(text);   // NV / V: no overflow / overflow
        return o;
    }
    if (kRegisters.count(text))
    {
        o.kind = Operand::Kind::Register;
        o.text = z80::NormalizeRegister(z80::Lower(text));
        return o;
    }
    if (text.size() >= 3 && text.front() == '(' && text.back() == ')')
    {
        const std::string inner = text.substr(1, text.size() - 2);
        if (inner == "HL" || inner == "BC" || inner == "DE" || inner == "SP" || inner == "C" || inner == "IX" || inner == "IY")
        {
            o.kind = Operand::Kind::Indirect;
            o.text = z80::Lower(inner);
            return o;
        }
        if ((inner.rfind("IX", 0) == 0 || inner.rfind("IY", 0) == 0) && inner.size() > 2 && (inner[2] == '+' || inner[2] == '-'))
        {
            o.kind = Operand::Kind::Indexed;
            o.text = z80::Lower(inner.substr(0, 2));
            // (IX-e): MASM computes the whole e, then negates it ((IX-2+1) is -3)
            o.expr = Parse(inner.substr(3));
            if (inner[2] == '-')
            {
                Expr group = Expr::Make(Expr::Kind::Group);
                group.args.push_back(std::move(o.expr));
                o.expr = Expr::Binary(Op::Sub, Expr::Number(0), std::move(group));
            }
            return o;
        }
        o.kind = Operand::Kind::Memory;
        o.expr = Parse(inner);
        return o;
    }
    o.kind = Operand::Kind::Immediate;
    o.expr = Parse(text);
    return o;
}

/// DEFB items: a string in quotes ("" is a quote; a 1-character string is a byte either way), or a byte expression
/// ("A"+1 included)
std::vector<Operand> DataOperands(const std::vector<std::string>& ops)
{
    std::vector<Operand> out;
    for (const std::string& op : ops)
    {
        if (op.size() >= 2 && op.front() == '"' && op.back() == '"')
        {
            std::string chars;
            bool whole = true;
            for (size_t k = 1; k + 1 < op.size(); ++k)
            {
                if (op[k] == '"')
                {
                    if (k + 2 < op.size() && op[k + 1] == '"')
                    {
                        chars.push_back('"');
                        ++k;
                        continue;
                    }
                    whole = false;
                    break;
                }
                chars.push_back(op[k]);
            }
            if (whole && chars.size() != 1)
            {
                Operand o;
                o.kind = Operand::Kind::String;
                o.text = chars;
                out.push_back(std::move(o));
                continue;
            }
        }
        out.push_back(MakeOperand(op, false));
    }
    return out;
}

std::string FileName(const std::string& operand)
{
    std::string name = operand;
    if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
        name = name.substr(1, name.size() - 2);
    if (name.size() > 8)
        name.resize(8);   // MASM copies at most 8 characters into the TR-DOS name
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    return name;
}

Statement Directive(ir::DirectiveKind kind)
{
    Statement s;
    s.kind = Statement::Kind::Directive;
    s.directive = kind;
    return s;
}

Statement Instruction(const std::string& mnemonic, std::vector<Operand> operands = {})
{
    Statement s;
    s.kind = Statement::Kind::Instruction;
    s.mnemonic = mnemonic;
    s.operands = std::move(operands);
    return s;
}

Operand Reg(const std::string& name)
{
    Operand o;
    o.kind = Operand::Kind::Register;
    o.text = name;
    return o;
}

Operand Value(Expr e)
{
    Operand o;
    o.kind = Operand::Kind::Immediate;
    o.expr = std::move(e);
    return o;
}

Operand Cond(const std::string& name)
{
    Operand o;
    o.kind = Operand::Kind::Condition;
    o.text = name;
    return o;
}

Operand Port(int64_t port)
{
    Operand o;
    o.kind = Operand::Kind::Memory;
    o.expr = Expr::Number(port, ir::NumberSpelling::Hex, 2);
    return o;
}

Expr Here(int64_t offset)
{
    return offset >= 0 ? Expr::Binary(Op::Add, Expr::Make(Expr::Kind::Current), Expr::Number(offset))
                       : Expr::Binary(Op::Sub, Expr::Make(Expr::Kind::Current), Expr::Number(-offset));
}

Expr Hex(int64_t value)
{
    return Expr::Number(value, ir::NumberSpelling::Hex, 2);
}

/// DOWN rr / UP rr: the next / previous screen line, as MASM 1.1 writes them (its TAB / TAB2 tables)
std::vector<Statement> ScreenLine(const std::string& pair, bool down)
{
    const std::string hi = pair == "BC" ? "b" : pair == "DE" ? "d" : "h";
    const std::string lo = pair == "BC" ? "c" : pair == "DE" ? "e" : "l";
    std::vector<Statement> out;
    if (down)
    {
        out.push_back(Instruction("inc", {Reg(hi)}));
        out.push_back(Instruction("ld", {Reg("a"), Reg(hi)}));
    }
    else
    {
        out.push_back(Instruction("ld", {Reg("a"), Reg(hi)}));
        out.push_back(Instruction("dec", {Reg(hi)}));
    }
    out.push_back(Instruction("and", {Value(Expr::Number(7))}));
    out.push_back(Instruction("jr", {Cond("nz"), Value(Here(12))}));
    out.push_back(Instruction("ld", {Reg("a"), Reg(lo)}));
    out.push_back(Instruction(down ? "add" : "sub", down ? std::vector<Operand>{Reg("a"), Value(Expr::Number(32))} : std::vector<Operand>{Value(Expr::Number(32))}));
    out.push_back(Instruction("ld", {Reg(lo), Reg("a")}));
    out.push_back(Instruction("jr", {Cond("c"), Value(Here(6))}));
    out.push_back(Instruction("ld", {Reg("a"), Reg(hi)}));
    out.push_back(Instruction(down ? "sub" : "add", down ? std::vector<Operand>{Value(Expr::Number(8))} : std::vector<Operand>{Reg("a"), Value(Expr::Number(8))}));
    out.push_back(Instruction("ld", {Reg(hi), Reg("a")}));
    return out;
}

/// MASM 3.0's BANK n: page n at #C000 (its handler writes LD A,n : LD BC,#7FFD : OUT (C),A)
std::vector<Statement> Bank(const std::string& operand)
{
    Operand c;
    c.kind = Operand::Kind::Indirect;
    c.text = "c";
    return {Instruction("ld", {Reg("a"), Value(Parse(operand))}), Instruction("ld", {Reg("bc"), Value(Expr::Number(0x7FFD, ir::NumberSpelling::Hex, 4))}),
            Instruction("out", {c, Reg("a")})};
}

/// MASM 3.0's BORDER n: LD A,n : OUT (#FE),A, or XOR A : OUT (#FE),A when n is 0 (decided while assembling: a
/// non-constant n becomes an IF of the target)
std::vector<Statement> Border(const std::string& operand)
{
    const Expr value = Parse(operand);
    std::vector<Statement> out;
    if (value.kind == Expr::Kind::Number)
        out.push_back(value.value == 0 ? Instruction("xor", {Reg("a")}) : Instruction("ld", {Reg("a"), Value(value)}));
    else
    {
        Statement test = Directive(ir::DirectiveKind::If);
        test.args.push_back(Expr::Binary(Op::Equal, value, Expr::Number(0)));
        out.push_back(std::move(test));
        out.push_back(Instruction("xor", {Reg("a")}));
        out.push_back(Directive(ir::DirectiveKind::Else));
        out.push_back(Instruction("ld", {Reg("a"), Value(value)}));
        out.push_back(Directive(ir::DirectiveKind::EndIf));
    }
    out.push_back(Instruction("out", {Port(0xFE), Reg("a")}));
    return out;
}

/// MASM 3.0's CLS [attribute]: the screen cleared with LDIR; without an operand pixels and attributes become 0, with
/// one the attributes take it (its CLS handler and the two code blocks it copies)
std::vector<Statement> Cls(const std::string& operand)
{
    auto word = [](int64_t v) { return Value(Expr::Number(v, ir::NumberSpelling::Hex, 4)); };
    Operand hl;
    hl.kind = Operand::Kind::Indirect;
    hl.text = "hl";
    std::vector<Statement> out = {Instruction("ld", {Reg("hl"), word(0x4000)}), Instruction("ld", {Reg("de"), word(0x4001)}),
                                  Instruction("ld", {Reg("bc"), word(operand.empty() ? 0x1AFF : 0x1800)}), Instruction("ld", {hl, Reg("l")}),
                                  Instruction("ldir")};
    if (!operand.empty())
    {
        out.push_back(Instruction("ld", {Reg("bc"), word(0x02FF)}));
        out.push_back(Instruction("ld", {hl, Value(Parse(operand))}));
        out.push_back(Instruction("ldir"));
    }
    return out;
}

/// SYSTEM / SYSTEM+: the 48K system state for BASIC (the TAB_SYS table), + returns with interrupts on
std::vector<Statement> System(bool plus)
{
    std::vector<Statement> out;
    out.push_back(Instruction("di"));
    out.push_back(Instruction("ld", {Reg("iy"), Value(Expr::Number(0x5C3A, ir::NumberSpelling::Hex, 4))}));
    out.push_back(Instruction("ld", {Reg("a"), Value(Hex(0x3F))}));
    out.push_back(Instruction("ld", {Reg("i"), Reg("a")}));
    out.push_back(Instruction("im", {Value(Expr::Number(1))}));
    if (plus)
    {
        out.push_back(Instruction("ei"));
        out.push_back(Instruction("ret"));
    }
    return out;
}

/// STOPKEY [address]: wait while no key is pressed (the STOP_TB table): JP Z,address, or JR Z to itself
std::vector<Statement> StopKey(const std::string& operand)
{
    std::vector<Statement> out;
    out.push_back(Instruction("xor", {Reg("a")}));
    out.push_back(Instruction("in", {Reg("a"), Port(0xFE)}));
    out.push_back(Instruction("cpl"));
    out.push_back(Instruction("and", {Value(Hex(0x1F))}));
    if (operand.empty())
        out.push_back(Instruction("jr", {Cond("z"), Value(Here(-6))}));
    else
        out.push_back(Instruction("jp", {Cond("z"), Value(Parse(operand))}));
    return out;
}

// Directive words of every version (the keyword tables, research-masm.md §4)
const std::set<std::string> kDirectiveWords = {"ORG", "PHASE", "UNPHASE", "EQU", "BEGIN", "END", "INCBIN", "INCLUDE", "DB", "DEFB", "DEFS",
                                               "DEFW", "DS", "DW"};
// The macro commands of 1.1 on (the 1.0 demo's table ends before them)
const std::set<std::string> kMacroCommands = {"DOWN", "UP", "SYSTEM", "STOPKEY"};
// 2.0 and 3.0 additions, 3.0 only (no documentation, no real source)
const std::set<std::string> kWords20 = {"MAC", "ENDM", "IF", "ELSE", "ENDIF"};
const std::set<std::string> kWords30 = {"BANK", "BORDER", "CLS"};
// Instruction words MASM has (its keyword table): INF = IN F,(C), EXA = EX AF,AF'
const std::set<std::string> kInstructionWords = {"AND", "ADC", "SBC", "ADD", "SUB", "XOR", "OR", "CP", "LD", "IM", "RST", "EI", "DI", "EXX", "EXA",
                                                 "INF", "LDIR", "LDDR", "OTIR", "OTDR", "OUTI", "OUTD", "RETI", "RETN", "INIR", "INDR", "CPIR", "CPDR",
                                                 "NEG", "CPD", "CPI", "IND", "INI", "LDD", "LDI", "CCF", "CPL", "DAA", "HALT", "NOP", "RLA", "RLCA",
                                                 "RRA", "RRCA", "SCF", "RLD", "RRD", "EX", "RET", "CALL", "JP", "PUSH", "POP", "INC", "DEC", "OUT",
                                                 "IN", "DJNZ", "JR", "BIT", "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLI", "SRL", "RES", "SET"};

/// Which words are keywords: the version's table ("" = a text file: every version's)
bool UsesCurrent(const Expr& e)
{
    if (e.kind == Expr::Kind::Current)
        return true;
    for (const Expr& a : e.args)
        if (UsesCurrent(a))
            return true;
    return false;
}

/// The name of the value an ORG / PHASE computes before an active PHASE ends (frontend-made: no label of the source)
constexpr const char* kPhaseValue = "__UNREALASM_PHASE";

struct Keywords
{
    std::string version;

    bool MacroCommand(const std::string& word) const { return version != "1.0" && kMacroCommands.count(word); }
    bool Later(const std::string& word) const
    {
        return (kWords20.count(word) && (version.empty() || version == "2.0" || version == "3.0")) ||
               (kWords30.count(word) && (version.empty() || version == "3.0"));
    }
    bool operator()(const std::string& word) const
    {
        return kDirectiveWords.count(word) || kInstructionWords.count(word) || MacroCommand(word) || Later(word);
    }
};

struct LineParser
{
    FrontendResult& result;
    Keywords IsKeyword;
    // PHASE state: MASM's ORG sets both addresses, PHASE the logical one, UNPHASE puts it back; where a file starts
    // it is not known (an INCLUDE inside PHASE): the end is then conditional
    enum class Phase { Unknown, Active, Off } phase = Phase::Unknown;
    bool inMacro = false;   // inside a MASM 3.0 MAC block

    void EndPhase(ir::Line& line)
    {
        if (phase == Phase::Off)
            return;
        Statement ent = Directive(ir::DirectiveKind::Ent);
        if (phase == Phase::Unknown)
            ent.text = "if-displaced";
        line.statements.push_back(std::move(ent));
        phase = Phase::Off;
    }

    Statement Command(const std::string& word, const std::string& rest, uint32_t number)
    {
        const std::vector<std::string> ops = SplitOperands(rest);
        Statement s;
        if (word == "ORG" || word == "PHASE")
        {
            s = Directive(word == "ORG" ? ir::DirectiveKind::Org : ir::DirectiveKind::Disp);
            s.args.push_back(Parse(rest));
            return s;
        }
        if (word == "UNPHASE")
            return Directive(ir::DirectiveKind::Ent);
        if (word == "EQU")
        {
            s = Directive(ir::DirectiveKind::Equ);
            s.args.push_back(Parse(rest));
            return s;
        }
        if (word == "DB" || word == "DEFB")
        {
            s = Directive(ir::DirectiveKind::Db);
            s.operands = DataOperands(ops);
            return s;
        }
        if (word == "DW" || word == "DEFW")
        {
            s = Directive(ir::DirectiveKind::Dw);
            for (const std::string& op : ops)
                s.operands.push_back(MakeOperand(op, false));
            return s;
        }
        if (word == "DS" || word == "DEFS")
        {
            // DEFS count[,bytes...]: the byte list repeated count times; without one count zeros (help, section 6)
            if (ops.empty())
                throw Failure{"DEFS without a count"};
            s = Directive(ir::DirectiveKind::Ds);
            s.args.push_back(Parse(ops[0]));
            if (ops.size() > 1)
                s.operands = DataOperands(std::vector<std::string>(ops.begin() + 1, ops.end()));
            return s;
        }
        if (word == "INCLUDE" || word == "INCBIN")
        {
            s = Directive(word == "INCLUDE" ? ir::DirectiveKind::Include : ir::DirectiveKind::Incbin);
            s.text = FileName(rest);
            if (word == "INCBIN")
                s.params = {"sector-slack"};   // TR-DOS loads whole sectors; the address moves by the file's length
            return s;
        }
        if (word == "BEGIN")
        {
            s = Directive(ir::DirectiveKind::Repeat);
            s.args.push_back(Parse(rest));
            return s;
        }
        if (word == "END")
            return Directive(ir::DirectiveKind::EndRepeat);
        if (IsKeyword.Later(word))
        {
            s = Directive(ir::DirectiveKind::Other);
            s.text = word + (rest.empty() ? "" : " " + rest);
            const std::string why = IsKeyword.version == "3.0" ? " (MASM 3.0 has no handler for it: IF stops with \"!?Unknown error?!\", ELSE jumps into its menu)"
                                                                : " (MASM 2.0's syntax not established: the copy found does not assemble)";
            result.diagnostics.push_back({Severity::Warning, number, 0, "MASM directive " + word + " kept as text" + why});
            return s;
        }
        // Instructions
        if (word == "EXA")
            return Instruction("ex", {Reg("af"), Reg("af'")});
        if (word == "INF")
        {
            Operand c;
            c.kind = Operand::Kind::Indirect;
            c.text = "c";
            return Instruction("in", {Reg("f"), c});
        }
        const std::string mnemonic = z80::Lower(word);
        s = Instruction(mnemonic);
        for (size_t k = 0; k < ops.size(); ++k)
        {
            const bool condition = z80::TakesCondition(mnemonic) && k == 0 && (ops.size() >= 2 || mnemonic == "ret");
            s.operands.push_back(MakeOperand(ops[k], condition));
        }
        return s;
    }

    void ParseLine(const std::string& text, uint32_t number)
    {
        ir::Line line;
        line.sourceLine = number;
        if (inMacro)
        {
            // A MASM 3.0 MAC body up to ENDM: kept as a comment
            std::string t = text;
            const size_t semicolon = t.find(';');
            const std::string code = semicolon == std::string::npos ? t : t.substr(0, semicolon);
            if (code.find("ENDM") != std::string::npos)
                inMacro = false;
            line.comment = " " + text;
            line.hasComment = true;
            result.program.lines.push_back(std::move(line));
            return;
        }
        std::string t = text;
        bool quote = false;
        for (size_t k = 0; k < t.size(); ++k)
        {
            if (t[k] == '"')
                quote = !quote;
            else if (t[k] == ';' && !quote)
            {
                line.comment = t.substr(k + 1);
                line.hasComment = true;
                t.erase(k);
                break;
            }
        }
        while (!t.empty() && t.back() == ' ')
            t.pop_back();
        try
        {
            size_t i = 0;
            size_t wordEnd = 0;
            while (wordEnd < t.size() && t[wordEnd] != ' ' && t[wordEnd] != ':')
                ++wordEnd;
            // The label field; a keyword there is a command (MASM stores it as a token). A colon ends the label and
            // is ignored ("INCLUD1:CALL INCLUST")
            if (!t.empty() && t[0] != ' ' && !IsKeyword(t.substr(0, wordEnd)))
            {
                line.label = t.substr(0, wordEnd);
                i = wordEnd < t.size() && t[wordEnd] == ':' ? wordEnd + 1 : wordEnd;
            }
            while (i < t.size() && t[i] == ' ')
                ++i;
            if (i < t.size())
            {
                size_t j = i;
                while (j < t.size() && t[j] != ' ')
                    ++j;
                std::string word = t.substr(i, j - i);
                while (j < t.size() && t[j] == ' ')
                    ++j;
                std::string rest = t.substr(j);
                // SYSTEM+ is SYSTEM followed by a "+"
                if (word.rfind("SYSTEM", 0) == 0 && IsKeyword.MacroCommand("SYSTEM") && (word == "SYSTEM" || word == "SYSTEM+"))
                {
                    if (word == "SYSTEM" && !rest.empty() && rest[0] == '+')
                        word = "SYSTEM+", rest = rest.substr(1);
                    for (Statement& s : System(word == "SYSTEM+"))
                        line.statements.push_back(std::move(s));
                }
                else if ((word == "DOWN" || word == "UP") && IsKeyword.MacroCommand(word))
                {
                    if (rest != "HL" && rest != "DE" && rest != "BC")
                        throw Failure{word + " takes HL, DE or BC"};
                    for (Statement& s : ScreenLine(rest, word == "DOWN"))
                        line.statements.push_back(std::move(s));
                }
                else if (word == "STOPKEY" && IsKeyword.MacroCommand(word))
                    for (Statement& s : StopKey(rest))
                        line.statements.push_back(std::move(s));
                else if ((word == "BANK" || word == "BORDER" || word == "CLS") && IsKeyword.version == "3.0")
                {
                    if (word != "CLS" && rest.empty())
                        throw Failure{word + " needs an operand"};
                    for (Statement& s : word == "BANK" ? Bank(rest) : word == "BORDER" ? Border(rest) : Cls(rest))
                        line.statements.push_back(std::move(s));
                }
                else if (word == "MAC" && IsKeyword.version == "3.0")
                {
                    // NAME MAC ... ENDM: MASM 3.0 skips the block (its first pass); nothing calls it (a NAME in the
                    // command field defines a label: error 3, twice). NAME's value is a pointer into MASM's text
                    inMacro = true;
                    line.label.clear();
                    line.comment = " MASM 3.0 MAC block (assembled as nothing): " + text;
                    line.hasComment = true;
                    result.diagnostics.push_back({Severity::Warning, number, 0, "MASM 3.0 MAC block skipped as MASM skips it (no call exists)"});
                }
                else if (word == "ENDIF" && IsKeyword.version == "3.0")
                    ;   // MASM 3.0 has no handler for ENDIF: its dispatch lands on code that does nothing
                else if (!IsKeyword(word))
                    throw Failure{"no command " + word + " (MASM keywords are capitals)"};
                else
                {
                    Statement s = Command(word, rest, number);
                    if (s.kind == Statement::Kind::Directive && (s.directive == ir::DirectiveKind::Org || s.directive == ir::DirectiveKind::Disp))
                    {
                        // MASM's $ is the logical address: ORG / PHASE $-#4000 inside a PHASE takes it before the
                        // PHASE ends (sjasmplus has no DISP inside DISP), so the value is kept first
                        if (phase != Phase::Off && !s.args.empty() && UsesCurrent(s.args[0]))
                        {
                            ir::Line value;
                            value.sourceLine = number;
                            value.label = kPhaseValue;
                            Statement defl = Directive(ir::DirectiveKind::Defl);
                            defl.args.push_back(std::move(s.args[0]));
                            value.statements.push_back(std::move(defl));
                            result.program.lines.push_back(std::move(value));
                            s.args[0] = Expr::Symbol(kPhaseValue);
                        }
                        EndPhase(line);
                        phase = s.directive == ir::DirectiveKind::Disp ? Phase::Active : Phase::Off;
                    }
                    if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Ent)
                        EndPhase(line);
                    else
                        line.statements.push_back(std::move(s));
                }
            }
        }
        catch (const Failure& f)
        {
            Statement raw;
            raw.kind = Statement::Kind::Raw;
            raw.text = text;
            line.statements.assign(1, raw);
            line.label.clear();
            line.hasComment = false;
            result.diagnostics.push_back({Severity::Warning, number, 0, "not parsed (" + f.reason + "): kept as text"});
        }
        catch (const std::exception&)
        {
            Statement raw;
            raw.kind = Statement::Kind::Raw;
            raw.text = text;
            line.statements.assign(1, raw);
            line.label.clear();
            line.hasComment = false;
            result.diagnostics.push_back({Severity::Warning, number, 0, "not parsed (a number out of range): kept as text"});
        }
        result.program.lines.push_back(std::move(line));
    }
};
}  // namespace

FrontendResult MasmFrontend::Parse(const SourceDocument& source) const
{
    FrontendResult result;
    result.program.dialect = "masm";
    result.program.expressionBits = 16;          // MASM computes in 16-bit words (HL)
    result.program.unsignedArithmetic = true;    // its division is unsigned
    result.program.displacementAcrossFiles = true;
    LineParser parser{result, Keywords{source.subversion}};
    uint32_t number = 0;
    for (const SourceLine& line : source.lines)
        parser.ParseLine(line.text, ++number);
    return result;
}
}  // namespace unrealasm::dialects
