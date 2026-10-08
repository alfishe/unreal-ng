#include "dialects/prometheus/prometheusfrontend.h"

#include <cctype>
#include <set>
#include <string_view>

#include "dialects/common/z80.h"

namespace unrealasm::dialects
{
namespace
{
using ir::DirectiveKind;
using ir::Expr;
using ir::Op;
using ir::Operand;
using ir::Statement;

struct Failure
{
    std::string reason;
};

std::string Trim(std::string_view text)
{
    size_t a = 0, b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\t'))
        ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\r'))
        --b;
    return std::string(text.substr(a, b - a));
}

bool IsNameStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool IsNameChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

const std::set<std::string> kRegisters = {"A", "B", "C", "D", "E", "H", "L", "I", "R", "AF", "AF'", "BC", "DE", "HL", "IX", "IY", "SP",
                                          "HX", "LX", "HY", "LY"};
const std::set<std::string> kConditions = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};
const std::set<std::string> kMnemonics = {
    "ADC",  "ADD",  "AND",  "BIT",  "CALL", "CCF",  "CP",   "CPD",  "CPDR", "CPI",  "CPIR", "CPL",  "DAA",  "DEC",  "DI",   "DJNZ",
    "EI",   "EX",   "EXX",  "HALT", "IM",   "IN",   "INC",  "IND",  "INDR", "INI",  "INIR", "JP",   "JR",   "LD",   "LDD",  "LDDR",
    "LDI",  "LDIR", "NEG",  "NOP",  "OR",   "OTDR", "OTIR", "OUT",  "OUTD", "OUTI", "POP",  "PUSH", "RES",  "RET",  "RETI", "RETN",
    "RL",   "RLA",  "RLC",  "RLCA", "RLD",  "RR",   "RRA",  "RRC",  "RRCA", "RRD",  "RST",  "SBC",  "SCF",  "SET",  "SLA",  "SLIA",
    "SRA",  "SRL",  "SUB",  "XOR"};

/// The index halves as the IR names them
std::string RegisterName(const std::string& upper)
{
    if (upper == "HX")
        return "ixh";
    if (upper == "LX")
        return "ixl";
    if (upper == "HY")
        return "iyh";
    if (upper == "LY")
        return "iyl";
    return z80::Lower(upper);
}

/// Atoms joined by + - * / ? strictly left to right; a sign may stand before any atom
struct ExpressionParser
{
    std::string_view t;
    size_t i = 0;

    void Blanks()
    {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t'))
            ++i;
    }

    Expr Atom()
    {
        Blanks();
        if (i >= t.size())
            throw Failure{"an operator without its operand"};
        const char c = t[i];
        if (c == '-' || c == '+')
        {
            ++i;
            Expr inner = Atom();
            return c == '-' ? Expr::Unary(Op::Negate, std::move(inner)) : inner;
        }
        if (c == '#' || c == '%' || std::isdigit(static_cast<unsigned char>(c)))
        {
            const int base = c == '#' ? 16 : c == '%' ? 2 : 10;
            if (base != 10)
                ++i;
            const size_t from = i;
            uint64_t value = 0;
            while (i < t.size())
            {
                const char d = t[i];
                const int digit = std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : (d >= 'A' && d <= 'F') ? d - 'A' + 10 : (d >= 'a' && d <= 'f') ? d - 'a' + 10 : 99;
                if (digit >= base)
                    break;
                value = (value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit)) & 0xFFFF;
                ++i;
            }
            if (i == from)
                throw Failure{std::string(1, c) + " without digits"};
            return Expr::Number(static_cast<int64_t>(value), base == 16 ? ir::NumberSpelling::Hex : base == 2 ? ir::NumberSpelling::Binary : ir::NumberSpelling::Decimal,
                                static_cast<int>(i - from));
        }
        if (c == '"' || c == '\'')
        {
            // "A": one byte; "AB": two (the first the high byte); a doubled quote inside is the quote ("""" = 34)
            std::string chars;
            size_t k = i + 1;
            while (true)
            {
                if (k >= t.size())
                    throw Failure{"a quoted constant without its closing quote"};
                if (t[k] == c)
                {
                    if (k + 1 < t.size() && t[k + 1] == c)
                    {
                        chars.push_back(c);
                        k += 2;
                        continue;
                    }
                    break;
                }
                chars.push_back(t[k++]);
            }
            if (chars.empty() || chars.size() > 2)
                throw Failure{"a quoted constant holds one or two characters"};
            int64_t value = 0;
            for (const char ch : chars)
                value = (value << 8) | static_cast<unsigned char>(ch);
            Expr e = Expr::Number(value, ir::NumberSpelling::Character, static_cast<int>(chars.size()));
            e.text = chars;
            i = k + 1;
            return e;
        }
        if (c == '$')
        {
            ++i;
            return Expr::Make(Expr::Kind::Current);
        }
        if (IsNameStart(c))
        {
            const size_t from = i;
            while (i < t.size() && IsNameChar(t[i]))
                ++i;
            return Expr::Symbol(z80::Upper(std::string(t.substr(from, i - from))));
        }
        throw Failure{std::string("unexpected '") + c + "' in an expression"};
    }

    Expr Whole()
    {
        Expr left = Atom();
        while (true)
        {
            Blanks();
            if (i >= t.size())
                return left;
            Op op;
            switch (t[i])
            {
                case '+': op = Op::Add; break;
                case '-': op = Op::Sub; break;
                case '*': op = Op::Mul; break;
                case '/': op = Op::Div; break;
                case '?': op = Op::Mod; break;
                default: throw Failure{"unexpected text after an expression: " + std::string(t.substr(i))};
            }
            ++i;
            left = Expr::Binary(op, std::move(left), Atom());
        }
    }
};

Expr Parse(std::string_view text)
{
    ExpressionParser p{text};
    return p.Whole();
}

std::vector<std::string> SplitOperands(std::string_view text)
{
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    char quoted = 0;
    for (const char c : text)
    {
        if ((c == '"' || c == '\'') && (!quoted || quoted == c))
            quoted = quoted ? 0 : c;
        else if (!quoted && c == '(')
            ++depth;
        else if (!quoted && c == ')' && depth > 0)
            --depth;
        if (c == ',' && depth == 0 && !quoted)
        {
            out.push_back(Trim(current));
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    if (!Trim(current).empty() || !out.empty())
        out.push_back(Trim(current));
    return out;
}

/// $ in the n-th item of DEFB / DEFW is that item's address (checked in PROMETHEUS: DEFW START,$ puts the second
/// word's own address)
void ShiftCurrent(Expr& e, int64_t by)
{
    if (e.kind == Expr::Kind::Current && by != 0)
    {
        e = Expr::Binary(Op::Add, Expr::Make(Expr::Kind::Current), Expr::Number(by));
        return;
    }
    for (Expr& a : e.args)
        ShiftCurrent(a, by);
}

Statement Directive(DirectiveKind kind)
{
    Statement s;
    s.kind = Statement::Kind::Directive;
    s.directive = kind;
    return s;
}

Operand InstructionOperand(const std::string& text, bool conditionAllowed)
{
    Operand o;
    const std::string upper = z80::Upper(text);
    if (conditionAllowed && kConditions.count(upper))
    {
        o.kind = Operand::Kind::Condition;
        o.text = z80::Lower(upper);
        return o;
    }
    if (kRegisters.count(upper))
    {
        o.kind = Operand::Kind::Register;
        o.text = RegisterName(upper);
        return o;
    }
    if (text.size() >= 2 && text.front() == '(' && text.back() == ')')
    {
        const std::string inner = Trim(std::string_view(text).substr(1, text.size() - 2));
        const std::string upperInner = z80::Upper(inner);
        if (upperInner == "HL" || upperInner == "BC" || upperInner == "DE" || upperInner == "SP" || upperInner == "C" || upperInner == "IX" || upperInner == "IY")
        {
            o.kind = Operand::Kind::Indirect;
            o.text = z80::Lower(upperInner);
            return o;
        }
        if ((upperInner.rfind("IX", 0) == 0 || upperInner.rfind("IY", 0) == 0) && inner.size() > 2)
        {
            const std::string displacement = Trim(std::string_view(inner).substr(2));
            if (!displacement.empty() && (displacement[0] == '+' || displacement[0] == '-'))
            {
                o.kind = Operand::Kind::Indexed;
                o.text = z80::Lower(upperInner.substr(0, 2));
                o.expr = Parse(displacement[0] == '+' ? std::string_view(displacement).substr(1) : std::string_view(displacement));
                return o;
            }
        }
        o.kind = Operand::Kind::Memory;
        o.expr = Parse(inner);
        return o;
    }
    o.kind = Operand::Kind::Immediate;
    o.expr = Parse(text);
    return o;
}

struct LineParser
{
    ir::Program& program;
    Diagnostics& diagnostics;
    uint32_t number = 0;
    bool put = false;   ///< a PUT is in force: the bytes go elsewhere than the addresses say
    int puts = 0;

    void Add(ir::Line line) { program.lines.push_back(std::move(line)); }

    ir::Line Plain() const
    {
        ir::Line l;
        l.sourceLine = number;
        return l;
    }

    void ParseLine(const std::string& text, ir::Line& line)
    {
        if (Trim(text).empty())
            return;
        if (text[0] == ';')
        {
            line.comment = text.substr(1);
            line.hasComment = true;
            return;
        }
        size_t k = 0;
        if (text[0] != ' ' && text[0] != '\t')
        {
            if (!IsNameStart(text[0]))
                throw Failure{"a label starts with a letter"};
            while (k < text.size() && IsNameChar(text[k]))
                ++k;
            if (k < text.size() && text[k] != ' ' && text[k] != '\t')
                throw Failure{"a label holds letters, digits and _"};
            line.label = z80::Upper(text.substr(0, k));
        }
        const std::string rest = Trim(std::string_view(text).substr(k));
        if (rest.empty())
            return;
        const size_t blank = rest.find_first_of(" \t");
        const std::string word = rest.substr(0, blank);
        const std::string upper = z80::Upper(word);
        const std::string operands = blank == std::string::npos ? std::string() : Trim(std::string_view(rest).substr(blank));
        const std::vector<std::string> ops = SplitOperands(operands);
        auto one = [&]() -> const std::string& {
            if (ops.size() != 1 || ops[0].empty())
                throw Failure{word + " takes one operand"};
            return ops[0];
        };
        if (upper == "ENT")
        {
            line.comment = "ENT " + one() + " (PROMETHEUS' RUN address)";
            line.hasComment = true;
            return;
        }
        if (upper == "EQU")
        {
            if (line.label.empty())
                throw Failure{"EQU without a label"};
            Statement s = Directive(DirectiveKind::Equ);
            s.args.push_back(Parse(one()));
            line.statements.push_back(std::move(s));
            return;
        }
        if (upper == "ORG")
        {
            // Both the address and where the bytes go; ends a PUT
            if (put)
            {
                ir::Line end = Plain();
                end.statements.push_back(Directive(DirectiveKind::Ent));
                Add(std::move(end));
                put = false;
            }
            Statement s = Directive(DirectiveKind::Org);
            s.args.push_back(Parse(one()));
            line.statements.push_back(std::move(s));
            return;
        }
        if (upper == "PUT")
        {
            // The bytes go to the operand, the address goes on: ORG there, DISP back to the address it had
            const std::string keep = "__PROMETHEUS_PUT" + std::to_string(++puts);
            if (put)
            {
                ir::Line end = Plain();
                end.statements.push_back(Directive(DirectiveKind::Ent));
                Add(std::move(end));
            }
            ir::Line at = Plain();
            at.label = keep;
            Statement assign = Directive(DirectiveKind::Defl);
            assign.args.push_back(Expr::Make(Expr::Kind::Current));
            at.statements.push_back(std::move(assign));
            Add(std::move(at));
            if (!line.label.empty())
            {
                ir::Line labelled = Plain();
                labelled.label = line.label;
                Add(std::move(labelled));
                line.label.clear();
            }
            Statement org = Directive(DirectiveKind::Org);
            org.args.push_back(Parse(one()));
            line.statements.push_back(std::move(org));
            Statement disp = Directive(DirectiveKind::Disp);
            disp.args.push_back(Expr::Symbol(keep));
            line.statements.push_back(std::move(disp));
            put = true;
            return;
        }
        if (upper == "DEFB" || upper == "DEFW")
        {
            Statement s = Directive(upper == "DEFW" ? DirectiveKind::Dw : DirectiveKind::Db);
            for (size_t n = 0; n < ops.size(); ++n)
            {
                Operand o;
                o.kind = Operand::Kind::Immediate;
                o.expr = Parse(ops[n]);
                ShiftCurrent(o.expr, static_cast<int64_t>(n) * (upper == "DEFW" ? 2 : 1));
                s.operands.push_back(std::move(o));
            }
            line.statements.push_back(std::move(s));
            return;
        }
        if (upper == "DEFM")
        {
            const std::string body = Trim(operands);
            if (body.size() < 2 || (body[0] != '"' && body[0] != '\'') || body.back() != body[0])
                throw Failure{"DEFM takes a quoted text"};
            Statement s = Directive(DirectiveKind::Db);
            Operand o;
            o.kind = Operand::Kind::String;
            o.text = body.substr(1, body.size() - 2);
            s.operands.push_back(std::move(o));
            line.statements.push_back(std::move(s));
            return;
        }
        if (upper == "DEFS")
        {
            // A hole: both addresses move on, nothing is written (checked: the memory keeps its bytes). ORG $+n does
            // that; under a PUT (a DISP) ORG would end it, so zeros are written there
            if (put)
            {
                Statement s = Directive(DirectiveKind::Ds);
                s.args.push_back(Parse(one()));
                line.statements.push_back(std::move(s));
                diagnostics.push_back({Severity::Info, number, 0, "DEFS under PUT written as zeros (PROMETHEUS leaves the bytes as they are)"});
                return;
            }
            Statement s = Directive(DirectiveKind::Org);
            Expr size = Parse(one());
            if (size.kind == Expr::Kind::Binary || size.kind == Expr::Kind::Unary)
            {
                Expr g = Expr::Make(Expr::Kind::Group);
                g.args.push_back(std::move(size));
                size = std::move(g);
            }
            s.args.push_back(Expr::Binary(Op::Add, Expr::Make(Expr::Kind::Current), std::move(size)));
            line.statements.push_back(std::move(s));
            return;
        }
        if (!kMnemonics.count(upper))
            throw Failure{"unknown word " + word};
        Statement s;
        s.mnemonic = upper == "SLIA" ? "sli" : z80::Lower(upper);
        for (size_t n = 0; n < ops.size(); ++n)
        {
            const bool condition = n == 0 && (upper == "JP" || upper == "JR" || upper == "CALL" || upper == "RET") && (ops.size() > 1 || upper == "RET");
            Operand o = InstructionOperand(ops[n], condition);
            if (upper == "EX" && n == 1 && o.kind == Operand::Kind::Register && o.text == "af")
                o.text = "af'";
            s.operands.push_back(std::move(o));
        }
        line.statements.push_back(std::move(s));
    }
};
}  // namespace

FrontendResult PrometheusFrontend::Parse(const SourceDocument& source) const
{
    FrontendResult result;
    result.program.dialect = "prometheus";
    result.program.expressionBits = 16;          // 16-bit words
    result.program.unsignedArithmetic = true;    // / and ? unsigned
    LineParser p{result.program, result.diagnostics};
    uint32_t index = 0;
    for (const SourceLine& sourceLine : source.lines)
    {
        ++index;
        p.number = index;
        ir::Line line;
        line.sourceLine = index;
        try
        {
            p.ParseLine(sourceLine.text, line);
        }
        catch (const Failure& f)
        {
            line = ir::Line{};
            line.sourceLine = index;
            Statement raw;
            raw.kind = Statement::Kind::Raw;
            raw.text = sourceLine.text;
            line.statements.push_back(std::move(raw));
            result.diagnostics.push_back({Severity::Warning, index, 0, "not parsed (" + f.reason + "): " + sourceLine.text});
        }
        result.program.lines.push_back(std::move(line));
    }
    return result;
}
}  // namespace unrealasm::dialects
