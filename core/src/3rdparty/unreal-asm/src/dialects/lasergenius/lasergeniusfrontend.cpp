#include "dialects/lasergenius/lasergeniusfrontend.h"

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

bool IsNameChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$';
}

bool IsNameStart(char c)
{
    return IsNameChar(c) && !std::isdigit(static_cast<unsigned char>(c));
}

const std::set<std::string> kRegisters = {"A", "B", "C", "D", "E", "H", "L", "I", "R", "F", "AF", "AF'", "BC", "DE", "HL", "IX", "IY", "SP"};
const std::set<std::string> kConditions = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};
const std::set<std::string> kMnemonics = {
    "ADC",  "ADD",  "AND",  "BIT",  "CALL", "CCF",  "CP",   "CPD",  "CPDR", "CPI",  "CPIR", "CPL",  "DAA",  "DEC",  "DI",   "DJNZ",
    "EI",   "EX",   "EXX",  "HALT", "IM",   "IN",   "INC",  "IND",  "INDR", "INI",  "INIR", "JP",   "JR",   "LD",   "LDD",  "LDDR",
    "LDI",  "LDIR", "NEG",  "NOP",  "OR",   "OTDR", "OTIR", "OUT",  "OUTD", "OUTI", "POP",  "PUSH", "RES",  "RET",  "RETI", "RETN",
    "RL",   "RLA",  "RLC",  "RLCA", "RLD",  "RR",   "RRA",  "RRC",  "RRCA", "RRD",  "RST",  "SBC",  "SCF",  "SET",  "SLA",  "SRA",
    "SRL",  "SUB",  "XOR"};
// Directives that only steer the listing, the printer and the prompts
const std::set<std::string> kListingDirectives = {"LIST", "LLIST", "COUNT", "SCREEN", "PRINTER", "MACLIST", "FORM", "REPORT", "TITLE", "PAUSE", "PROMPTS"};

Expr Grouped(Expr e)
{
    Expr g = Expr::Make(Expr::Kind::Group);
    g.args.push_back(std::move(e));
    return g;
}

/// Laser Genius expressions (manual appendix D, checked by assembling in Laser Genius 1.04): binary levels from the
/// lowest && || ; & | ^ ; ?= != ; < > <= >= ; << >> @< @> ; + - ; * / %, left to right; unary - ! ^ * right to left
struct ExpressionParser
{
    std::string_view t;
    size_t i = 0;
    bool put = false;   ///< a PUT in force: "." is where the bytes go ($$$)

    void Blanks()
    {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t'))
            ++i;
    }

    bool Take(std::string_view s)
    {
        if (t.compare(i, s.size(), s) == 0)
        {
            i += s.size();
            return true;
        }
        return false;
    }

    Expr Number(int base, ir::NumberSpelling spelling)
    {
        const size_t from = i;
        uint64_t value = 0;
        while (i < t.size())
        {
            const char d = static_cast<char>(std::toupper(static_cast<unsigned char>(t[i])));
            const int digit = std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : (d >= 'A' && d <= 'F') ? d - 'A' + 10 : 99;
            if (digit >= base)
                break;
            value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
            if (value > 0xFFFF)
                throw Failure{"a number above 65535"};
            ++i;
        }
        if (i == from)
            throw Failure{"digits expected"};
        return Expr::Number(static_cast<int64_t>(value), spelling, static_cast<int>(i - from));
    }

    Expr Primary()
    {
        Blanks();
        if (i >= t.size())
            throw Failure{"an operator without its operand"};
        const char c = t[i];
        if (c == '[')
        {
            ++i;
            Expr inner = Binary(0);
            Blanks();
            if (!Take("]"))
                throw Failure{"']' expected"};
            return Grouped(std::move(inner));
        }
        if (c == '#')
        {
            ++i;
            return Number(16, ir::NumberSpelling::Hex);
        }
        if (c == '%')
        {
            ++i;
            return Number(2, ir::NumberSpelling::Binary);
        }
        if (c == '@')
        {
            ++i;
            Expr e = Number(8, ir::NumberSpelling::Decimal);
            e.digits = 0;
            return e;
        }
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            size_t end = i;
            while (end < t.size() && std::isxdigit(static_cast<unsigned char>(t[end])))
                ++end;
            if (end < t.size() && t[end] == 'H')
            {
                Expr e = Number(16, ir::NumberSpelling::Hex);
                ++i;
                return e;
            }
            return Number(10, ir::NumberSpelling::Decimal);
        }
        if (c == '"')
        {
            // One character: "A", or an escape "\13" (a decimal code), "\"", "\\"
            ++i;
            int64_t value = 0;
            std::string text;
            if (i < t.size() && t[i] == '\\' && i + 1 < t.size() && std::isdigit(static_cast<unsigned char>(t[i + 1])))
            {
                ++i;
                const Expr code = Number(10, ir::NumberSpelling::Decimal);
                value = code.value & 0xFF;
                if (!Take("\""))
                    throw Failure{"a character constant holds one character"};
                return Expr::Number(value);
            }
            if (i < t.size() && t[i] == '\\')
                ++i;
            if (i >= t.size())
                throw Failure{"a character constant without its closing quote"};
            value = static_cast<unsigned char>(t[i]);
            text.push_back(t[i++]);
            if (!Take("\""))
                throw Failure{"a character constant holds one character"};
            Expr e = Expr::Number(value, ir::NumberSpelling::Character, 1);
            e.text = text;
            return e;
        }
        if (c == '\\')
        {
            // A macro parameter
            ++i;
            const size_t from = i;
            while (i < t.size() && IsNameChar(t[i]))
                ++i;
            if (i == from)
                throw Failure{"a parameter name expected after \\"};
            return Expr::Symbol(std::string(t.substr(from, i - from)));
        }
        if ((c == '$' || c == '.') && (i + 1 >= t.size() || !IsNameChar(t[i + 1])))
        {
            ++i;
            if (c == '$' || !put)
                return Expr::Make(Expr::Kind::Current);
            return Expr::Make(Expr::Kind::CurrentPhysical);
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

    Expr Unary()
    {
        Blanks();
        if (i < t.size())
        {
            const char c = t[i];
            if (c == '-' || c == '!' || c == '^' || c == '*')
            {
                ++i;
                Expr inner = Unary();
                if (c == '-')
                    return Expr::Unary(Op::Negate, std::move(inner));
                if (c == '!')
                    return Expr::Unary(Op::LogicalNot, std::move(inner));
                if (c == '^')
                    return Expr::Unary(Op::Not, std::move(inner));
                Expr m = Expr::Make(Expr::Kind::Memory);   // "contents of": the word at that address
                m.args.push_back(std::move(inner));
                return m;
            }
        }
        return Primary();
    }

    /// The binary operator at the position for a level (0 = the lowest), consumed; false when none
    bool Operator(int level, Op& op)
    {
        Blanks();
        struct Entry
        {
            const char* text;
            Op op;
            int level;
        };
        // Longest first, so "<=" is not read as "<"
        static const Entry kTable[] = {
            {"&&", Op::LogicalAnd, 0}, {"||", Op::LogicalOr, 0}, {"?=", Op::Equal, 2},    {"!=", Op::NotEqual, 2},
            {"<=", Op::LessEqual, 3},  {">=", Op::GreaterEqual, 3}, {"<<", Op::Shl, 4},   {">>", Op::Shr, 4},
            {"@<", Op::RotateLeft16, 4}, {"@>", Op::RotateRight16, 4}, {"&", Op::And, 1}, {"|", Op::Or, 1},
            {"^", Op::Xor, 1},         {"<", Op::Less, 3},       {">", Op::Greater, 3},   {"+", Op::Add, 5},
            {"-", Op::Sub, 5},         {"*", Op::Mul, 6},        {"/", Op::Div, 6},       {"%", Op::Mod, 6},
        };
        for (const Entry& e : kTable)
            if (t.compare(i, std::char_traits<char>::length(e.text), e.text) == 0)
            {
                if (e.level != level)
                    return false;
                op = e.op;
                i += std::char_traits<char>::length(e.text);
                return true;
            }
        return false;
    }

    Expr Binary(int level)
    {
        if (level > 6)
            return Unary();
        Expr left = Binary(level + 1);
        Op op;
        while (Operator(level, op))
            left = Expr::Binary(op, std::move(left), Binary(level + 1));
        return left;
    }

    Expr Whole()
    {
        Expr e = Binary(0);
        Blanks();
        if (i < t.size())
            throw Failure{"unexpected text after an expression: " + std::string(t.substr(i))};
        return e;
    }
};

std::vector<std::string> SplitOperands(std::string_view text)
{
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    bool quoted = false;
    for (size_t k = 0; k < text.size(); ++k)
    {
        const char c = text[k];
        if (quoted && c == '\\' && k + 1 < text.size())
        {
            current.push_back(c);
            current.push_back(text[++k]);
            continue;
        }
        if (c == '"')
            quoted = !quoted;
        else if (!quoted && (c == '(' || c == '['))
            ++depth;
        else if (!quoted && (c == ')' || c == ']') && depth > 0)
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

/// The position of the comment's ';' (outside quotes), or npos
size_t CommentStart(const std::string& text)
{
    bool quoted = false;
    for (size_t k = 0; k < text.size(); ++k)
    {
        if (quoted && text[k] == '\\')
        {
            ++k;
            continue;
        }
        if (text[k] == '"')
            quoted = !quoted;
        else if (!quoted && text[k] == ';')
            return k;
    }
    return std::string::npos;
}

Statement Directive(DirectiveKind kind)
{
    Statement s;
    s.kind = Statement::Kind::Directive;
    s.directive = kind;
    return s;
}

struct LineParser
{
    ir::Program& program;
    Diagnostics& diagnostics;
    uint32_t number = 0;
    bool put = false;   ///< a PUT in force: the bytes go elsewhere than the addresses say
    int puts = 0;

    Expr Parse(std::string_view text) const
    {
        ExpressionParser p{text, 0, put};
        return p.Whole();
    }

    void Add(ir::Line line) { program.lines.push_back(std::move(line)); }

    ir::Line Plain() const
    {
        ir::Line l;
        l.sourceLine = number;
        return l;
    }

    Operand InstructionOperand(const std::string& text, bool conditionAllowed) const
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
                    o.expr = displacement[0] == '+' ? Parse(std::string_view(displacement).substr(1))
                                                    : Expr::Unary(Op::Negate, Grouped(Parse(std::string_view(displacement).substr(1))));
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

    /// A string of DB / DEFM with its escapes put in ("\13" a code, "\"" a quote)
    static std::string Unescape(const std::string& body)
    {
        std::string out;
        for (size_t k = 0; k < body.size(); ++k)
        {
            if (body[k] == '\\' && k + 1 < body.size())
            {
                if (std::isdigit(static_cast<unsigned char>(body[k + 1])))
                {
                    unsigned v = 0;
                    while (k + 1 < body.size() && std::isdigit(static_cast<unsigned char>(body[k + 1])))
                        v = v * 10 + static_cast<unsigned>(body[++k] - '0');
                    out.push_back(static_cast<char>(v & 0xFF));
                    continue;
                }
                out.push_back(body[++k]);
                continue;
            }
            out.push_back(body[k]);
        }
        return out;
    }

    /// A DB string's text and escaped codes in order: text runs as strings, \n codes as numbers
    static std::vector<Operand> StringParts(const std::string& body)
    {
        std::vector<Operand> out;
        std::string text;
        auto flush = [&]() {
            if (text.empty())
                return;
            Operand o;
            o.kind = Operand::Kind::String;
            o.text = text;
            out.push_back(std::move(o));
            text.clear();
        };
        for (size_t k = 0; k < body.size(); ++k)
        {
            if (body[k] == '\\' && k + 1 < body.size())
            {
                if (std::isdigit(static_cast<unsigned char>(body[k + 1])))
                {
                    unsigned v = 0;
                    while (k + 1 < body.size() && std::isdigit(static_cast<unsigned char>(body[k + 1])))
                        v = v * 10 + static_cast<unsigned>(body[++k] - '0');
                    flush();
                    Operand o;
                    o.kind = Operand::Kind::Immediate;
                    o.expr = Expr::Number(v & 0xFF);
                    out.push_back(std::move(o));
                    continue;
                }
                text.push_back(body[++k]);
                continue;
            }
            text.push_back(body[k]);
        }
        flush();
        return out;
    }

    void EndPut()
    {
        if (!put)
            return;
        ir::Line end = Plain();
        end.statements.push_back(Directive(DirectiveKind::Ent));
        Add(std::move(end));
        put = false;
    }

    void ParseLine(std::string text, ir::Line& line)
    {
        // A paragraph number in front (a listing), then the comment
        {
            size_t k = 0;
            while (k < text.size() && (text[k] == ' ' || text[k] == '\t'))
                ++k;
            const size_t digits = k;
            while (k < text.size() && std::isdigit(static_cast<unsigned char>(text[k])))
                ++k;
            if (k > digits && (k == text.size() || text[k] == ' ' || text[k] == '\t'))
                text = text.substr(k);
        }
        const size_t semicolon = CommentStart(text);
        if (semicolon != std::string::npos)
        {
            line.comment = text.substr(semicolon + 1);
            line.hasComment = true;
            text = text.substr(0, semicolon);
        }
        std::string rest = Trim(text);
        if (rest.empty())
            return;
        if (rest.front() == '{' && rest.back() == '}')
            throw Failure{"a paragraph the codec kept as bytes (Phoenix, the hash extensions' language)"};
        // A label: a name and a colon (blanks allowed before the colon)
        {
            size_t k = 0;
            while (k < rest.size() && IsNameChar(rest[k]))
                ++k;
            size_t colon = k;
            while (colon < rest.size() && (rest[colon] == ' ' || rest[colon] == '\t'))
                ++colon;
            if (k > 0 && IsNameStart(rest[0]) && colon < rest.size() && rest[colon] == ':')
            {
                line.label = rest.substr(0, k);
                rest = Trim(std::string_view(rest).substr(colon + 1));
            }
        }
        if (rest.empty())
            return;
        size_t blank = rest.find_first_of(" \t");
        std::string word = rest.substr(0, blank);
        std::string operands = blank == std::string::npos ? std::string() : Trim(std::string_view(rest).substr(blank));
        const std::vector<std::string> ops = SplitOperands(operands);
        auto one = [&]() -> const std::string& {
            if (ops.size() != 1 || ops[0].empty())
                throw Failure{word + " takes one operand"};
            return ops[0];
        };
        if (word[0] == '\\')
        {
            Statement s;
            s.kind = Statement::Kind::MacroCall;
            s.mnemonic = word.substr(1);
            s.params = ops;
            line.statements.push_back(std::move(s));
            return;
        }
        if (word[0] == '*')
        {
            Directives(z80::Upper(word.substr(1)), ops, line);
            return;
        }
        const std::string upper = z80::Upper(word);
        if (upper == "EQU" || upper == "DL" || upper == "DEFL")
        {
            if (line.label.empty())
                throw Failure{upper + " without a label"};
            Statement s = Directive(upper == "EQU" ? DirectiveKind::Equ : DirectiveKind::Defl);
            s.args.push_back(Parse(one()));
            line.statements.push_back(std::move(s));
            return;
        }
        if (upper == "ORG")
        {
            // The address; under a PUT the bytes keep their place: a new displacement
            if (put)
            {
                ir::Line end = Plain();
                end.statements.push_back(Directive(DirectiveKind::Ent));
                Add(std::move(end));
                Statement disp = Directive(DirectiveKind::Disp);
                disp.args.push_back(Parse(one()));
                line.statements.push_back(std::move(disp));
                return;
            }
            Statement s = Directive(DirectiveKind::Org);
            s.args.push_back(Parse(one()));
            line.statements.push_back(std::move(s));
            return;
        }
        if (upper == "PUT")
        {
            // The bytes go to the operand, the address goes on: ORG there, DISP back to the address it had
            const std::string keep = "__LG_PUT" + std::to_string(++puts);
            EndPut();
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
        if (upper == "DB" || upper == "DEFB" || upper == "DEFM" || upper == "DW" || upper == "DEFW")
        {
            const bool words = upper == "DW" || upper == "DEFW";
            Statement s = Directive(words ? DirectiveKind::Dw : DirectiveKind::Db);
            for (const std::string& item : ops)
            {
                Operand o;
                // A string of other than one character (one is a character constant, a number); its escaped codes
                // (\13, \255) written as numbers beside the text, which is UTF-8 in the IR
                if (item.size() >= 2 && item.front() == '"' && item.back() == '"')
                {
                    const std::string body = item.substr(1, item.size() - 2);
                    if (Unescape(body).size() != 1)
                    {
                        if (words)
                            throw Failure{"strings cannot be given to " + upper};
                        for (Operand& part : StringParts(body))
                            s.operands.push_back(std::move(part));
                        continue;
                    }
                }
                o.kind = Operand::Kind::Immediate;
                o.expr = Parse(item);
                s.operands.push_back(std::move(o));
            }
            line.statements.push_back(std::move(s));
            return;
        }
        if (upper == "DS" || upper == "DEFS")
        {
            Statement s = Directive(DirectiveKind::Ds);   // zeros (checked: the memory there is cleared)
            s.args.push_back(Parse(one()));
            line.statements.push_back(std::move(s));
            return;
        }
        if (upper == "COND")
        {
            Statement s = Directive(DirectiveKind::If);
            s.args.push_back(Parse(one()));
            line.statements.push_back(std::move(s));
            return;
        }
        if (upper == "ELSE" || upper == "ENDC" || upper == "ENDM")
        {
            line.statements.push_back(Directive(upper == "ELSE" ? DirectiveKind::Else : upper == "ENDC" ? DirectiveKind::EndIf : DirectiveKind::EndMacro));
            return;
        }
        if (upper == "MACRO")
        {
            if (line.label.empty())
                throw Failure{"MACRO without a name (the label)"};
            Statement s = Directive(DirectiveKind::Macro);
            s.text = line.label;
            line.label.clear();
            for (const std::string& p : ops)
            {
                if (p.size() < 2 || p[0] != '\\')
                    throw Failure{"a macro parameter starts with \\"};
                s.params.push_back(p.substr(1));
            }
            line.statements.push_back(std::move(s));
            return;
        }
        if (!kMnemonics.count(upper))
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
        line.statements.push_back(std::move(s));
    }

    void Directives(const std::string& name, const std::vector<std::string>& ops, ir::Line& line)
    {
        auto one = [&]() -> const std::string& {
            if (ops.size() != 1 || ops[0].empty())
                throw Failure{"*" + name + " takes one operand"};
            return ops[0];
        };
        auto quoted = [&]() {
            const std::string& o = one();
            if (o.size() < 2 || o.front() != '"' || o.back() != '"')
                throw Failure{"*" + name + " takes a quoted name"};
            return Unescape(o.substr(1, o.size() - 2));
        };
        auto note = [&](const std::string& why) {
            std::string text = "*" + name;
            for (size_t k = 0; k < ops.size(); ++k)
                text += (k ? "," : " ") + ops[k];
            line.comment = text + (why.empty() ? "" : " (" + why + ")") + (line.hasComment ? " ;" + line.comment : "");
            line.hasComment = true;
        };
        if (kListingDirectives.count(name))
        {
            note("");
            return;
        }
        if (name == "WHILE")
        {
            Statement s = Directive(DirectiveKind::While);
            s.args.push_back(Parse(one()));
            line.statements.push_back(std::move(s));
            return;
        }
        if (name == "ENDW")
        {
            line.statements.push_back(Directive(DirectiveKind::EndWhile));
            return;
        }
        if (name == "REPEAT")
        {
            line.statements.push_back(Directive(DirectiveKind::RepeatUntil));
            return;
        }
        if (name == "UNTIL")
        {
            // Laser Genius repeats until the expression is true; the IR's block runs until its expression is 0
            Statement s = Directive(DirectiveKind::UntilZero);
            s.args.push_back(Expr::Unary(Op::LogicalNot, Grouped(Parse(one()))));
            line.statements.push_back(std::move(s));
            return;
        }
        if (name == "INCLUDE")
        {
            Statement s = Directive(DirectiveKind::Include);
            s.text = quoted();
            line.statements.push_back(std::move(s));
            return;
        }
        if (name == "PRINT")
        {
            // Strings and expressions (Laser Genius prints an expression in hex: #3D0)
            if (ops.empty())
                throw Failure{"*PRINT takes strings and expressions"};
            Statement s = Directive(DirectiveKind::Display);
            bool hex = false;
            for (const std::string& item : ops)
            {
                Operand o;
                if (item.size() >= 2 && item.front() == '"' && item.back() == '"')
                {
                    o.kind = Operand::Kind::String;
                    o.text = Unescape(item.substr(1, item.size() - 2));
                    s.operands.push_back(std::move(o));
                    continue;
                }
                if (!hex)
                {
                    Operand key;
                    key.kind = Operand::Kind::Immediate;
                    key.expr = Expr::Make(Expr::Kind::Raw);
                    key.expr.text = "/H";
                    s.operands.push_back(std::move(key));
                    hex = true;
                }
                o.kind = Operand::Kind::Immediate;
                o.expr = Parse(item);
                s.operands.push_back(std::move(o));
            }
            line.statements.push_back(std::move(s));
            return;
        }
        if (name == "OPENOUT" || name == "CLOSEOUT")
        {
            note("Laser Genius' object file");
            diagnostics.push_back({Severity::Info, number, 0, "*" + name + ": the object file is the target's output, kept as a comment"});
            return;
        }
        if (name == "CODE")
        {
            note("");
            if (!ops.empty() && z80::Upper(ops[0]) == "OFF")
                diagnostics.push_back({Severity::Warning, number, 0, "*CODE OFF stops the object code; the conversion writes it"});
            return;
        }
        throw Failure{"unknown directive *" + name};
    }
};
}  // namespace

FrontendResult LaserGeniusFrontend::Parse(const SourceDocument& source) const
{
    FrontendResult result;
    result.program.dialect = "lasergenius";
    result.program.expressionBits = 16;          // 16-bit words
    result.program.unsignedArithmetic = true;    // checked: -10/3 = 21842, -1>>1 = #7FFF, -1<1 = 0
    result.program.trueValue = 1;                // comparisons, && || ! give 1
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
