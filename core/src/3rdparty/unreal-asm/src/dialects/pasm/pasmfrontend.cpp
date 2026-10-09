#include "dialects/pasm/pasmfrontend.h"

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

constexpr int64_t kDefaultOrigin = 24576;   // without ORG PASM puts the code there and runs it from there (its help)

std::string Trim(std::string_view text)
{
    size_t a = 0, b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\t'))
        ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\r'))
        --b;
    return std::string(text.substr(a, b - a));
}

bool IsNameChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '?' || c == '@';
}

bool IsNameStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

const std::set<std::string> kRegisters = {"A", "B", "C", "D", "E", "H", "L", "I", "R", "AF", "AF'", "BC", "DE", "HL", "IX", "IY", "SP"};
const std::set<std::string> kConditions = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};

Expr Grouped(Expr e)
{
    Expr g = Expr::Make(Expr::Kind::Group);
    g.args.push_back(std::move(e));
    return g;
}

/// Atoms joined by + - * / strictly left to right ("the rule of privileges is not kept": 1+2*255 = 765)
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
                const char d = static_cast<char>(std::toupper(static_cast<unsigned char>(t[i])));
                const int digit = std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : (d >= 'A' && d <= 'F') ? d - 'A' + 10 : 99;
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
        if (c == '\'')
        {
            // 'c': the character's code ('AB': two, the first the high byte)
            const size_t close = t.find('\'', i + 1);
            if (close == std::string_view::npos || close == i + 1 || close > i + 3)
                throw Failure{"a character constant holds one or two characters"};
            int64_t value = 0;
            for (size_t k = i + 1; k < close; ++k)
                value = (value << 8) | static_cast<unsigned char>(t[k]);
            Expr e = Expr::Number(value, ir::NumberSpelling::Character, static_cast<int>(close - i - 1));
            e.text = std::string(t.substr(i + 1, close - i - 1));
            i = close + 1;
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
            return Expr::Symbol(std::string(t.substr(from, i - from)));
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
                default: throw Failure{"unexpected text after an expression: " + std::string(t.substr(i))};
            }
            ++i;
            left = Grouped(Expr::Binary(op, std::move(left), Atom()));
        }
    }
};

Expr Parse(std::string_view text)
{
    ExpressionParser p{text};
    return p.Whole();
}

/// Operands split at commas outside apostrophes and parentheses
std::vector<std::string> SplitOperands(std::string_view text)
{
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    bool quoted = false;
    for (size_t k = 0; k < text.size(); ++k)
    {
        const char c = text[k];
        if (c == '\'')
        {
            // AF' is a register, not a quote
            const bool prime = !quoted && k >= 2 && std::toupper(static_cast<unsigned char>(text[k - 1])) == 'F' &&
                               std::toupper(static_cast<unsigned char>(text[k - 2])) == 'A';
            if (!prime)
                quoted = !quoted;
        }
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

/// The position of the comment's ';' (outside apostrophes), or npos
size_t CommentStart(const std::string& text)
{
    bool quoted = false;
    for (size_t k = 0; k < text.size(); ++k)
    {
        if (text[k] == '\'')
        {
            const bool prime = !quoted && k >= 2 && std::toupper(static_cast<unsigned char>(text[k - 1])) == 'F' &&
                               std::toupper(static_cast<unsigned char>(text[k - 2])) == 'A';
            if (!prime)
                quoted = !quoted;
        }
        else if (!quoted && text[k] == ';')
            return k;
    }
    return std::string::npos;
}

/// $ in an item of DB / DW is that item's address (checked in PASM 3.0: DW #1234,LAB,$ gave the third word's own
/// address)
void ShiftCurrent(Expr& e, int64_t by)
{
    if (e.kind == Expr::Kind::Current && by != 0)
    {
        e = Grouped(Expr::Binary(Op::Add, Expr::Make(Expr::Kind::Current), Expr::Number(by)));
        return;
    }
    for (Expr& a : e.args)
        ShiftCurrent(a, by);
}

bool HasCurrent(const Expr& e)
{
    if (e.kind == Expr::Kind::Current)
        return true;
    for (const Expr& a : e.args)
        if (HasCurrent(a))
            return true;
    return false;
}

/// What $ is in an instruction's operand: the bytes PASM has put when it reads that operand (checked in PASM 3.0).
/// The first of two operands is read before the opcode (LD ($),A: $ is the instruction's address); the others after
/// the prefix and opcode (JP $, LD HL,$, JR $: +1; LD BC,($) with ED, LD IX,$ with DD: +2)
int64_t CurrentShift(const Statement& s, size_t operand)
{
    if (s.operands.size() == 2 && operand == 0)
        return 0;
    int64_t bytes = 1;
    bool ed = false;
    for (const Operand& o : s.operands)
    {
        if ((o.kind == Operand::Kind::Register && (o.text == "ix" || o.text == "iy")) || o.kind == Operand::Kind::Indexed ||
            (o.kind == Operand::Kind::Indirect && (o.text == "ix" || o.text == "iy")))
            bytes = 2;
    }
    // LD rr,(nn) / LD (nn),rr with BC, DE or SP: ED 4B / ED 43 ...
    if (s.mnemonic == "ld" && s.operands.size() == 2)
    {
        const Operand& a = s.operands[0];
        const Operand& b = s.operands[1];
        auto pair = [](const Operand& o) { return o.kind == Operand::Kind::Register && (o.text == "bc" || o.text == "de" || o.text == "sp"); };
        ed = (pair(a) && b.kind == Operand::Kind::Memory) || (a.kind == Operand::Kind::Memory && pair(b));
    }
    return ed ? 2 : bytes;
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
        o.text = z80::Lower(upper);
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
                o.expr = displacement[0] == '+' ? Parse(std::string_view(displacement).substr(1)) : Expr::Unary(Op::Negate, Grouped(Parse(std::string_view(displacement).substr(1))));
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
    Diagnostics& diagnostics;
    uint32_t number = 0;

    /// "value DUP count": the position of DUP (outside apostrophes), or npos
    static size_t DupAt(const std::string& item)
    {
        const std::string upper = z80::Upper(item);
        bool quoted = false;
        for (size_t k = 0; k + 3 <= upper.size(); ++k)
        {
            if (upper[k] == '\'')
                quoted = !quoted;
            if (!quoted && upper.compare(k, 3, "DUP") == 0 && k > 0 && (upper[k - 1] == ' ' || upper[k - 1] == '\t') &&
                (k + 3 == upper.size() || upper[k + 3] == ' ' || upper[k + 3] == '\t'))
                return k;
        }
        return std::string::npos;
    }

    /// DB / DW: runs of items as DB / DW, "value DUP count" as a fill
    void Data(const std::vector<std::string>& items, bool words, ir::Line& line) const
    {
        const int size = words ? 2 : 1;
        int64_t offset = 0;
        bool known = true;   // the offset of the next item is known ($ can be shifted)
        Statement run = Directive(words ? DirectiveKind::Dw : DirectiveKind::Db);
        auto flush = [&]() {
            if (!run.operands.empty())
                line.statements.push_back(run);
            run.operands.clear();
        };
        for (const std::string& item : items)
        {
            if (item.empty())
                throw Failure{"an empty item"};
            const size_t dup = DupAt(item);
            if (dup != std::string::npos)
            {
                flush();
                Expr value = Parse(Trim(std::string_view(item).substr(0, dup)));
                Expr count = Parse(Trim(std::string_view(item).substr(dup + 3)));
                if (known)
                    ShiftCurrent(value, offset);
                Statement fill = Directive(DirectiveKind::Ds);
                fill.args.push_back(count);
                Operand low;
                low.kind = Operand::Kind::Immediate;
                if (words)
                {
                    low.expr = Expr::Binary(Op::And, Grouped(value), Expr::Number(0xFF, ir::NumberSpelling::Hex, 2));
                    Operand high;
                    high.kind = Operand::Kind::Immediate;
                    high.expr = Expr::Binary(Op::Shr, Grouped(value), Expr::Number(8));
                    fill.operands.push_back(std::move(low));
                    fill.operands.push_back(std::move(high));
                }
                else
                {
                    low.expr = std::move(value);
                    fill.operands.push_back(std::move(low));
                }
                line.statements.push_back(std::move(fill));
                if (count.kind == Expr::Kind::Number)
                    offset += count.value * size;
                else
                    known = false;
                continue;
            }
            Operand o;
            if (item.front() == '\'' && !words)
            {
                // A string: 'text', or an open one running to the line's end ('YEAH)
                const size_t close = item.find('\'', 1);
                const std::string text = item.substr(1, close == std::string::npos ? std::string::npos : close - 1);
                if (close != std::string::npos && close + 1 != item.size())
                    throw Failure{"text after a string: " + item};
                if (text.size() != 1 || close == std::string::npos)
                {
                    o.kind = Operand::Kind::String;
                    o.text = text;
                    run.operands.push_back(std::move(o));
                    offset += static_cast<int64_t>(text.size());
                    continue;
                }
            }
            o.kind = Operand::Kind::Immediate;
            o.expr = Parse(item);
            if (known)
                ShiftCurrent(o.expr, offset);
            run.operands.push_back(std::move(o));
            offset += size;
        }
        flush();
    }

    void ParseLine(std::string text, ir::Line& line)
    {
        const size_t semicolon = CommentStart(text);
        if (semicolon != std::string::npos)
        {
            line.comment = text.substr(semicolon + 1);
            line.hasComment = true;
            text = text.substr(0, semicolon);
        }
        if (Trim(text).empty())
            return;
        size_t k = 0;
        if (text[0] != ' ' && text[0] != '\t')
        {
            while (k < text.size() && text[k] != ' ' && text[k] != '\t')
                ++k;
            std::string label = text.substr(0, k);
            if (!label.empty() && label.back() == ':')
                label.pop_back();
            if (label.empty() || !IsNameStart(label[0]))
                throw Failure{"a label starts with a letter: " + label};
            for (const char c : label)
                if (!IsNameChar(c))
                    throw Failure{"a label holds letters, digits and _ . ? @: " + label};
            line.label = label;
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
        if (upper == "ORG")
        {
            Statement s = Directive(DirectiveKind::Org);
            s.args.push_back(Parse(one()));
            line.statements.push_back(std::move(s));
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
        if (upper == "ENT")
        {
            line.comment = "ENT (PASM's run address)" + std::string(line.hasComment ? " ;" + line.comment : "");
            line.hasComment = true;
            return;
        }
        if (upper == "DB" || upper == "DW")
        {
            Data(ops, upper == "DW", line);
            return;
        }
        if (upper == "ITXT" || upper == "IBIN")
        {
            // The first 8 characters after the blank are the file's name (its help)
            const std::string name = Trim(std::string_view(operands).substr(0, std::min<size_t>(8, operands.size())));
            if (name.empty())
                throw Failure{word + " without a file name"};
            Statement s = Directive(upper == "ITXT" ? DirectiveKind::Include : DirectiveKind::Incbin);
            s.text = name;
            line.statements.push_back(std::move(s));
            return;
        }
        if (!z80::IsMnemonic(z80::Lower(upper)))
            throw Failure{"unknown word " + word};
        Statement s;
        s.mnemonic = z80::Lower(upper);
        for (size_t n = 0; n < ops.size(); ++n)
        {
            const bool condition = n == 0 && (upper == "JP" || upper == "JR" || upper == "CALL" || upper == "RET") && (ops.size() > 1 || upper == "RET");
            Operand o = InstructionOperand(ops[n], condition);
            if (upper == "EX" && n == 1 && o.kind == Operand::Kind::Register && o.text == "af")
                o.text = "af'";
            s.operands.push_back(std::move(o));
        }
        for (size_t n = 0; n < s.operands.size(); ++n)
            if (HasCurrent(s.operands[n].expr))
                ShiftCurrent(s.operands[n].expr, CurrentShift(s, n));
        line.statements.push_back(std::move(s));
    }
};
}  // namespace

FrontendResult PasmFrontend::Parse(const SourceDocument& source) const
{
    FrontendResult result;
    result.program.dialect = "pasm";
    result.program.expressionBits = 16;          // 16-bit words
    result.program.unsignedArithmetic = true;    // the division truncates (100/7 = 14)
    LineParser p{result.diagnostics};
    bool origin = false;
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
        for (const Statement& s : line.statements)
            origin = origin || (s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::Org);
        result.program.lines.push_back(std::move(line));
    }
    // Without ORG the code goes to 24576: in the main text, where nothing has set the address yet ($ = 0); a text read
    // with ITXT goes on from its includer's address
    if (!origin && !result.program.lines.empty())
    {
        std::vector<ir::Line> head(3);
        Statement unset = Directive(DirectiveKind::If);
        unset.args.push_back(Expr::Binary(Op::Equal, Expr::Make(Expr::Kind::Current), Expr::Number(0)));
        head[0].statements.push_back(std::move(unset));
        head[0].comment = " PASM puts the code at 24576 without ORG";
        head[0].hasComment = true;
        Statement org = Directive(DirectiveKind::Org);
        org.args.push_back(Expr::Number(kDefaultOrigin));
        head[1].statements.push_back(std::move(org));
        head[2].statements.push_back(Directive(DirectiveKind::EndIf));
        for (ir::Line& l : head)
            l.sourceLine = 1;
        result.program.lines.insert(result.program.lines.begin(), head.begin(), head.end());
    }
    return result;
}
}  // namespace unrealasm::dialects
