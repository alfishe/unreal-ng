#include "dialects/zeus/zeusfrontend.h"

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

/// The words of a version's keyword table (research-zeus.md §3: 1983's 101 words; GG and PHT rename DEFB-DEFW to
/// DB-DW and add INCBIN, or INCLUDE and PLACE). A word of the table cannot be a label; anything else can (INF, SLL)
bool IsKeyword(const std::string& word, const std::string& version)
{
    static const std::set<std::string> table = {
        "A",    "ADC",  "ADD",  "AF'",  "AF",   "AND",  "B",    "BC",   "BIT",  "C",    "CALL", "CCF",  "CP",   "CPD",  "CPDR", "CPI",
        "CPIR", "CPL",  "D",    "DAA",  "DE",   "DEC",  "DI",   "DISP", "DJNZ", "E",    "EI",   "ENT",  "EQU",  "EX",   "EXX",  "H",
        "HALT", "HL",   "I",    "IM",   "IN",   "INC",  "IND",  "INDR", "INI",  "INIR", "IX",   "IY",   "JP",   "JR",   "L",    "LD",
        "LDD",  "LDDR", "LDI",  "LDIR", "M",    "NC",   "NEG",  "NOP",  "NV",   "NZ",   "OR",   "ORG",  "OTDR", "OTIR", "OUT",  "OUTD",
        "OUTI", "P",    "PE",   "PO",   "POP",  "PUSH", "R",    "RES",  "RET",  "RETI", "RETN", "RL",   "RLA",  "RLC",  "RLCA", "RLD",
        "RR",   "RRA",  "RRC",  "RRCA", "RRD",  "RST",  "SBC",  "SCF",  "SET",  "SLA",  "SP",   "SRA",  "SRL",  "SUB",  "V",    "XOR",
        "Z",
    };
    if (table.count(word))
        return true;
    if (version == "1983")
        return word == "DEFB" || word == "DEFM" || word == "DEFS" || word == "DEFW";
    if (word == "DB" || word == "DM" || word == "DS" || word == "DW")
        return true;
    return version == "gg" ? word == "INCBIN" : (word == "INCLUDE" || word == "PLACE");
}

bool IsLabelChar(char c, bool pht)
{
    const unsigned char u = static_cast<unsigned char>(c);
    // 1983 / GG: letters and digits; PHT / v7.E: also the symbols #3C-#7E (research-zeus.md §4: its word characters)
    return std::isalnum(u) || (pht && (c == '_' || c == '?' || c == '@'));
}

Expr Grouped(Expr e)
{
    Expr g = Expr::Make(Expr::Kind::Group);
    g.args.push_back(std::move(e));
    return g;
}

/// ZEUS expressions: operands joined by operators, no priorities, strictly left to right (manual 5.3)
struct ExpressionParser
{
    std::string_view t;
    bool pht = false;
    size_t i = 0;

    char Peek() const { return i < t.size() ? t[i] : '\0'; }
    void Blanks()
    {
        while (i < t.size() && t[i] == ' ')
            ++i;
    }

    Expr Term()
    {
        Blanks();
        const char c = Peek();
        if (c == '-' || c == '+')
        {
            // a leading sign: 0 - x (ZEUS's evaluator starts from 0)
            ++i;
            Expr inner = Term();
            return c == '-' ? Expr::Unary(Op::Negate, std::move(inner)) : inner;
        }
        if (c == '"')
        {
            // "c: the code of the one character after the quote
            if (i + 1 >= t.size())
                throw Failure{"\" without a character"};
            const unsigned char ch = static_cast<unsigned char>(t[i + 1]);
            if (ch >= 0x80)
                throw Failure{"a character literal outside ASCII"};
            i += 2;
            Expr e = Expr::Number(ch, ir::NumberSpelling::Character, 1);
            e.text = std::string(1, static_cast<char>(ch));
            return e;
        }
        if (c == '#' || (c == '%' && pht))
        {
            const bool hex = c == '#';
            size_t j = ++i;
            while (j < t.size() && (hex ? std::isxdigit(static_cast<unsigned char>(t[j])) != 0 : (t[j] == '0' || t[j] == '1')))
                ++j;
            if (j == i)
                throw Failure{std::string(1, c) + " without digits"};
            const Expr e = Expr::Number(std::stoll(std::string(t.substr(i, j - i)), nullptr, hex ? 16 : 2) & 0xFFFF,
                                        hex ? ir::NumberSpelling::Hex : ir::NumberSpelling::Binary, static_cast<int>(j - i));
            i = j;
            return e;
        }
        if (c == '$')
        {
            ++i;
            return Expr::Make(Expr::Kind::Current);
        }
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            size_t j = i;
            while (j < t.size() && std::isdigit(static_cast<unsigned char>(t[j])))
                ++j;
            const Expr e = Expr::Number(std::stoll(std::string(t.substr(i, j - i))) & 0xFFFF, ir::NumberSpelling::Decimal, static_cast<int>(j - i));
            i = j;
            if (i < t.size() && IsLabelChar(t[i], pht))
                throw Failure{"a number followed by letters"};
            return e;
        }
        if (IsLabelChar(c, pht))
        {
            size_t j = i;
            while (j < t.size() && IsLabelChar(t[j], pht))
                ++j;
            Expr e = Expr::Symbol(std::string(t.substr(i, j - i)));
            i = j;
            return e;
        }
        throw Failure{std::string("unexpected '") + c + "' in an expression"};
    }

    Expr Sequence()
    {
        Expr left = Term();
        while (true)
        {
            Blanks();
            const char c = Peek();
            Op op;
            if (c == '+')
                op = Op::Add;
            else if (c == '-')
                op = Op::Sub;
            else if (c == '&')
                op = Op::And;
            else if (c == '!')
                op = Op::Or;
            else if (pht && c == '*')
                op = Op::Mul;
            else if (pht && c == '/')
                op = Op::Div;
            else
                return left;
            ++i;
            // left to right: what is built so far is one operand of the next operator
            Expr right = Term();
            if (op == Op::Div)
            {
                // ZEUS v7.E rounds the quotient to the nearest, a remainder of exactly half down (seen in the
                // emulator: 7/2 = 3, 11/4 = 3, 3000/7 = 429, 65535/2 = 32767): a/b + (a%b > b/2)
                Expr quotient = Grouped(Expr::Binary(Op::Div, left, right));
                Expr remainder = Grouped(Expr::Binary(Op::Mod, left, right));
                Expr half = Grouped(Expr::Binary(Op::Div, right, Expr::Number(2)));
                left = Grouped(Expr::Binary(Op::Add, std::move(quotient), Grouped(Expr::Binary(Op::Greater, std::move(remainder), std::move(half)))));
                continue;
            }
            left = Grouped(Expr::Binary(op, std::move(left), std::move(right)));
        }
    }
};

Expr ParseExpression(std::string_view text, bool pht)
{
    ExpressionParser p{text, pht};
    Expr e = p.Sequence();
    p.Blanks();
    if (p.i != text.size())
        throw Failure{"unexpected text after an expression: " + std::string(text.substr(p.i))};
    // A whole expression in a group is the expression
    while (e.kind == Expr::Kind::Group && e.args.size() == 1 && e.args[0].kind != Expr::Kind::Binary)
        e = e.args[0];
    return e;
}

std::string Trim(std::string s)
{
    while (!s.empty() && s.front() == ' ')
        s.erase(s.begin());
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

/// Operands split at commas; a "c literal (", or "" included) is one character, not a separator
std::vector<std::string> Split(std::string_view text)
{
    std::vector<std::string> out;
    std::string cur;
    for (size_t k = 0; k < text.size(); ++k)
    {
        const char c = text[k];
        if (c == '"' && k + 1 < text.size())
        {
            cur += text.substr(k, 2);
            ++k;
            continue;
        }
        if (c == ',')
        {
            out.push_back(Trim(cur));
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    if (!Trim(cur).empty() || !out.empty())
        out.push_back(Trim(cur));
    return out;
}

bool WhollyParenthesized(const std::string& text)
{
    if (text.size() < 2 || text.front() != '(' || text.back() != ')')
        return false;
    int depth = 0;
    for (size_t k = 0; k < text.size(); ++k)
    {
        if (text[k] == '"')
        {
            ++k;
            continue;
        }
        if (text[k] == '(')
            ++depth;
        else if (text[k] == ')' && --depth == 0 && k + 1 != text.size())
            return false;
    }
    return true;
}

Operand ParseOperand(const std::string& text, bool conditionAllowed, bool pht)
{
    Operand o;
    const std::string lower = z80::Lower(text);
    if (text == z80::Upper(text) && conditionAllowed)
    {
        // ZEUS's V / NV are PE / PO (manual 5.1.2)
        const std::string condition = lower == "v" ? "pe" : lower == "nv" ? "po" : lower;
        if (z80::IsCondition(condition))
        {
            o.kind = Operand::Kind::Condition;
            o.text = condition;
            return o;
        }
    }
    // Registers are keywords: upper case, and only those of ZEUS's table (no IXH / IXL halves)
    if (text == z80::Upper(text) && z80::IsRegister(lower) && lower != "f" && lower.find('x') == std::string::npos && lower.find("iy") != 0)
    {
        o.kind = Operand::Kind::Register;
        o.text = lower;
        return o;
    }
    if (text == "IX" || text == "IY")
    {
        o.kind = Operand::Kind::Register;
        o.text = lower;
        return o;
    }
    if (WhollyParenthesized(text))
    {
        const std::string inner = Trim(text.substr(1, text.size() - 2));
        if (inner == "HL" || inner == "BC" || inner == "DE" || inner == "SP" || inner == "C" || inner == "IX" || inner == "IY")
        {
            o.kind = Operand::Kind::Indirect;
            o.text = z80::Lower(inner);
            return o;
        }
        if (inner.size() > 2 && (inner.rfind("IX", 0) == 0 || inner.rfind("IY", 0) == 0))
        {
            const std::string displacement = Trim(inner.substr(2));
            if (!displacement.empty() && (displacement[0] == '+' || displacement[0] == '-'))
            {
                o.kind = Operand::Kind::Indexed;
                o.text = z80::Lower(inner.substr(0, 2));
                o.expr = ParseExpression(displacement, pht);
                return o;
            }
        }
        o.kind = Operand::Kind::Memory;
        o.expr = ParseExpression(inner, pht);
        return o;
    }
    o.kind = Operand::Kind::Immediate;
    o.expr = ParseExpression(text, pht);
    return o;
}

Statement Directive(ir::DirectiveKind kind)
{
    Statement s;
    s.kind = Statement::Kind::Directive;
    s.directive = kind;
    return s;
}

/// One statement split from its line: the label (empty when none) and the rest
struct Part
{
    std::string label;
    std::string body;
};

/// The statements of a line and its comment: ":" separates statements and ";" starts the comment, except in a "c
/// literal and inside a DEFM / DM text (its delimiter is the character after the keyword's blank)
std::vector<std::string> SplitStatements(const std::string& text, std::string& comment, bool& hasComment)
{
    std::vector<std::string> out;
    std::string cur;
    for (size_t k = 0; k < text.size(); ++k)
    {
        const char c = text[k];
        if (c == '"' && k + 1 < text.size())
        {
            cur += text.substr(k, 2);
            ++k;
            continue;
        }
        // DEFM / DM: everything to the closing delimiter is text
        const std::string trimmed = Trim(cur);
        const bool atText = (trimmed == "DM" || trimmed == "DEFM" || (trimmed.size() > 3 && (trimmed.compare(trimmed.size() - 3, 3, " DM") == 0)) ||
                             (trimmed.size() > 5 && trimmed.compare(trimmed.size() - 5, 5, " DEFM") == 0)) &&
                            !cur.empty() && cur.back() == ' ' && c != ' ';
        if (atText)
        {
            const size_t close = text.find(c, k + 1);
            const size_t end = close == std::string::npos ? text.size() : close + 1;
            cur += text.substr(k, end - k);
            k = end - 1;
            continue;
        }
        if (c == ';')
        {
            comment = text.substr(k + 1);
            hasComment = true;
            break;
        }
        if (c == ':')
        {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

struct LineParser
{
    FrontendResult& result;
    std::string version;
    bool pht = false;
    // DISP: the offset from ORG where code is put (manual 5.5); none while it is 0
    bool displaced = false;
    Expr displacement;
    int temporaries = 0;

    std::string Temporary() { return "__UNREALASM_ZEUS" + std::to_string(temporaries++); }

    static ir::Line LineWith(uint32_t number, Statement s, std::string label = {})
    {
        ir::Line l;
        l.sourceLine = number;
        l.label = std::move(label);
        l.statements.push_back(std::move(s));
        return l;
    }

    /// ORG a: with a displacement active the code goes to a + DISP and runs at a
    void Org(Expr address, uint32_t number, std::vector<ir::Line>& out)
    {
        if (!displaced)
        {
            Statement org = Directive(ir::DirectiveKind::Org);
            org.args.push_back(std::move(address));
            out.push_back(LineWith(number, std::move(org)));
            return;
        }
        const std::string at = Temporary();
        Statement keep = Directive(ir::DirectiveKind::Defl);
        keep.args.push_back(std::move(address));
        out.push_back(LineWith(number, std::move(keep), at));
        out.push_back(LineWith(number, Directive(ir::DirectiveKind::Ent)));
        Statement org = Directive(ir::DirectiveKind::Org);
        org.args.push_back(Grouped(Expr::Binary(Op::Add, Expr::Symbol(at), displacement)));
        out.push_back(LineWith(number, std::move(org)));
        Statement disp = Directive(ir::DirectiveKind::Disp);
        disp.args.push_back(Expr::Symbol(at));
        out.push_back(LineWith(number, std::move(disp)));
    }

    /// DISP d: from here the code goes to $ + d (DISP 0 ends it)
    void Disp(Expr offset, uint32_t number, std::vector<ir::Line>& out)
    {
        const std::string at = Temporary();
        Statement keep = Directive(ir::DirectiveKind::Defl);
        keep.args.push_back(Expr::Make(Expr::Kind::Current));
        out.push_back(LineWith(number, std::move(keep), at));
        if (displaced)
            out.push_back(LineWith(number, Directive(ir::DirectiveKind::Ent)));
        const bool zero = offset.kind == Expr::Kind::Number && offset.value == 0;
        Statement org = Directive(ir::DirectiveKind::Org);
        org.args.push_back(zero ? Expr::Symbol(at) : Grouped(Expr::Binary(Op::Add, Expr::Symbol(at), offset)));
        out.push_back(LineWith(number, std::move(org)));
        displaced = !zero;
        if (displaced)
        {
            displacement = offset;
            Statement disp = Directive(ir::DirectiveKind::Disp);
            disp.args.push_back(Expr::Symbol(at));
            out.push_back(LineWith(number, std::move(disp)));
        }
    }

    std::vector<Operand> Data(const std::vector<std::string>& ops)
    {
        std::vector<Operand> out;
        for (const std::string& op : ops)
        {
            if (op.empty())
                throw Failure{"an empty item"};
            Operand o;
            o.kind = Operand::Kind::Immediate;
            o.expr = ParseExpression(op, pht);
            out.push_back(std::move(o));
        }
        return out;
    }

    static std::string FileName(const std::string& op)
    {
        std::string name = Trim(op);
        if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
            name = name.substr(1, name.size() - 2);
        return name;
    }

    /// One statement (label already taken off): its IR lines
    void ParseBody(const std::string& body, const std::string& label, uint32_t number, std::vector<ir::Line>& out)
    {
        size_t j = 0;
        while (j < body.size() && body[j] != ' ')
            ++j;
        const std::string word = body.substr(0, j);
        const std::string rest = Trim(body.substr(j));
        ir::Line line;
        line.sourceLine = number;
        line.label = label;
        auto push = [&](Statement s) {
            line.statements.push_back(std::move(s));
            out.push_back(std::move(line));
        };

        if (word.empty())
        {
            out.push_back(std::move(line));   // a label alone
            return;
        }
        if (word == "ORG")
        {
            if (!label.empty())
                out.push_back(std::move(line));
            Org(ParseExpression(rest, pht), number, out);
            return;
        }
        if (word == "DISP")
        {
            if (!label.empty())
                out.push_back(std::move(line));
            Disp(ParseExpression(rest, pht), number, out);
            return;
        }
        if (word == "ENT")
        {
            out.push_back(std::move(line));   // the entry point for ZEUS's X command: no code
            return;
        }
        if (word == "EQU")
        {
            if (label.empty())
                throw Failure{"EQU without a label"};
            Statement s = Directive(ir::DirectiveKind::Equ);
            s.args.push_back(ParseExpression(rest, pht));
            push(std::move(s));
            return;
        }
        if (word == "DEFB" || word == "DB")
        {
            Statement s = Directive(ir::DirectiveKind::Db);
            s.operands = Data(Split(rest));
            push(std::move(s));
            return;
        }
        if (word == "DEFW" || word == "DW")
        {
            Statement s = Directive(ir::DirectiveKind::Dw);
            s.operands = Data(Split(rest));
            push(std::move(s));
            return;
        }
        if (word == "DEFS" || word == "DS")
        {
            Statement s = Directive(ir::DirectiveKind::Ds);
            s.args.push_back(ParseExpression(rest, pht));
            push(std::move(s));
            return;
        }
        if (word == "DEFM" || word == "DM")
        {
            // DEFM /text/: the text between the first character and its next occurrence
            if (rest.empty())
                throw Failure{"DEFM without a text"};
            // (no closing delimiter: the text runs to the end of the line, as the Zeus Routines write it)
            const char delimiter = rest[0];
            const size_t close = rest.find(delimiter, 1);
            if (close != std::string::npos && close + 1 != rest.size())
                throw Failure{"text after DEFM's closing delimiter"};
            Statement s = Directive(ir::DirectiveKind::Db);
            Operand o;
            o.kind = Operand::Kind::String;
            o.text = rest.substr(1, close == std::string::npos ? std::string::npos : close - 1);
            if (o.text.size() == 1)
            {
                o.kind = Operand::Kind::Immediate;
                o.expr = Expr::Number(static_cast<unsigned char>(o.text[0]), ir::NumberSpelling::Character, 1);
                o.expr.text = o.text;
            }
            if (!o.text.empty())
                s.operands.push_back(std::move(o));
            if (s.operands.empty())
            {
                out.push_back(std::move(line));
                return;
            }
            push(std::move(s));
            return;
        }
        if (word == "INCLUDE" || word == "PLACE" || word == "INCBIN")
        {
            Statement s = Directive(word == "INCLUDE" ? ir::DirectiveKind::Include : ir::DirectiveKind::Incbin);
            s.text = FileName(rest);
            push(std::move(s));
            return;
        }
        const std::string mnemonic = z80::Lower(word);
        if (!IsKeyword(word, version) || !z80::IsMnemonic(mnemonic))
            throw Failure{"unknown command " + word};
        Statement s;
        s.kind = Statement::Kind::Instruction;
        s.mnemonic = mnemonic;
        const std::vector<std::string> ops = Split(rest);
        for (size_t k = 0; k < ops.size(); ++k)
        {
            const bool condition = k == 0 && z80::TakesCondition(mnemonic) && (ops.size() > 1 || mnemonic == "ret");
            s.operands.push_back(ParseOperand(ops[k], condition, pht));
        }
        push(std::move(s));
    }

    void ParseLine(const std::string& text, uint32_t number)
    {
        std::string comment;
        bool hasComment = false;
        std::vector<ir::Line> lines;
        try
        {
            const std::vector<std::string> statements = SplitStatements(text, comment, hasComment);
            for (const std::string& raw : statements)
            {
                const std::string statement = Trim(raw);
                if (statement.empty())
                    continue;
                // A first word that is no keyword is the statement's label
                size_t j = 0;
                while (j < statement.size() && statement[j] != ' ')
                    ++j;
                const std::string first = statement.substr(0, j);
                std::string label;
                std::string body = statement;
                if (!IsKeyword(first, version))
                {
                    for (const char c : first)
                        if (!IsLabelChar(c, pht))
                            throw Failure{"label " + first};
                    if (!std::isalpha(static_cast<unsigned char>(first[0])) && !(pht && first[0] == '_'))
                        throw Failure{"a label starts with a letter: " + first};
                    label = first;
                    body = Trim(statement.substr(j));
                }
                ParseBody(body, label, number, lines);
            }
        }
        catch (const Failure& f)
        {
            lines.clear();
            Statement raw;
            raw.kind = Statement::Kind::Raw;
            raw.text = text;
            lines.push_back(LineWith(number, raw));
            hasComment = false;
            result.diagnostics.push_back({Severity::Warning, number, 0, "not parsed (" + f.reason + "): kept as text"});
        }
        catch (const std::exception&)
        {
            lines.clear();
            Statement raw;
            raw.kind = Statement::Kind::Raw;
            raw.text = text;
            lines.push_back(LineWith(number, raw));
            hasComment = false;
            result.diagnostics.push_back({Severity::Warning, number, 0, "not parsed (a number out of range): kept as text"});
        }
        if (lines.empty())
        {
            ir::Line empty;
            empty.sourceLine = number;
            lines.push_back(std::move(empty));
        }
        lines.back().comment = comment;
        lines.back().hasComment = hasComment;
        for (ir::Line& l : lines)
            result.program.lines.push_back(std::move(l));
    }
};
}  // namespace

FrontendResult ZeusFrontend::Parse(const SourceDocument& source) const
{
    FrontendResult result;
    result.program.dialect = "zeus";
    result.program.expressionBits = 16;   // addresses and values are 16-bit words
    result.program.unsignedArithmetic = true;
    result.program.trueValue = 1;          // the comparison of the rounded division gives 1
    // * / and %binary are ZEUS v7.E's; the bytes of a v7.E source without INCLUDE / PLACE are valid 1983 bytes too, so
    // they are read in every version (1983 and ZEUS 1.1 refuse them: such a source was written for v7.E)
    LineParser parser{result, source.subversion.empty() ? std::string("1983") : source.subversion, true, false, {}, 0};
    uint32_t number = 0;
    for (const SourceLine& line : source.lines)
        parser.ParseLine(line.text, ++number);
    return result;
}
}  // namespace unrealasm::dialects
