#include "dialects/sjasmplus/sjasmplusfrontend.h"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <map>
#include <optional>
#include <set>

#include "dialects/common/z80.h"
#include "layout/z80size.h"

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

bool IsLabelStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '@';
}

bool IsLabelChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '@' || c == '?';
}

std::string Trim(std::string_view text)
{
    size_t a = 0, b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\t'))
        ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\r'))
        --b;
    return std::string(text.substr(a, b - a));
}

/// The apostrophe of AF' (EX AF,AF') is not a quote
bool IsAfApostrophe(std::string_view t, size_t i)
{
    return i >= 2 && std::tolower(static_cast<unsigned char>(t[i - 2])) == 'a' && std::tolower(static_cast<unsigned char>(t[i - 1])) == 'f' &&
           (i < 3 || !IsLabelChar(t[i - 3]));
}

/// The text with the inside of every string replaced by 'x' (quotes kept), so that ';', ':' and ',' are found by
/// position without reading into strings
std::string Mask(std::string_view t)
{
    std::string m(t);
    for (size_t i = 0; i < t.size(); ++i)
    {
        const char q = t[i];
        if (q != '"' && (q != '\'' || IsAfApostrophe(t, i)))
            continue;
        size_t j = i + 1;
        while (j < t.size())
        {
            if (q == '"' && t[j] == '\\' && j + 1 < t.size())
            {
                m[j] = m[j + 1] = 'x';
                j += 2;
                continue;
            }
            if (t[j] == q)
            {
                if (q == '\'' && j + 1 < t.size() && t[j + 1] == '\'')   // '' inside '...' is one apostrophe
                {
                    m[j] = m[j + 1] = 'x';
                    j += 2;
                    continue;
                }
                break;
            }
            m[j++] = 'x';
        }
        i = j;
    }
    return m;
}

/// Splits at `separator` outside strings, parentheses and braces
std::vector<std::string> Split(std::string_view text, char separator)
{
    const std::string m = Mask(text);
    std::vector<std::string> out;
    int depth = 0;
    size_t start = 0;
    for (size_t k = 0; k < m.size(); ++k)
    {
        if (m[k] == '(' || m[k] == '{' || m[k] == '[')
            ++depth;
        else if ((m[k] == ')' || m[k] == '}' || m[k] == ']') && depth > 0)
            --depth;
        else if (m[k] == separator && depth == 0)
        {
            out.push_back(Trim(text.substr(start, k - start)));
            start = k + 1;
        }
    }
    const std::string last = Trim(text.substr(start));
    if (!last.empty() || !out.empty())
        out.push_back(last);
    return out;
}

/// The characters of a string literal starting at t[i] (a quote); i moves past it
std::string StringLiteral(std::string_view t, size_t& i)
{
    const char q = t[i++];
    std::string out;
    while (i < t.size())
    {
        const char c = t[i];
        if (c == q)
        {
            if (q == '\'' && i + 1 < t.size() && t[i + 1] == '\'')
            {
                out.push_back('\'');
                i += 2;
                continue;
            }
            ++i;
            return out;
        }
        if (q == '"' && c == '\\' && i + 1 < t.size())
        {
            const char e = t[i + 1];
            i += 2;
            // sjasmplus reads the letters in either case (\A and \a are both 7)
            switch (e >= 'A' && e <= 'Z' ? static_cast<char>(e + 32) : e)
            {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '0': out.push_back('\0'); break;
                case 'a': out.push_back('\a'); break;
                case 'b': out.push_back('\b'); break;
                case 'd': out.push_back('\x7F'); break;
                case 'e': out.push_back('\x1B'); break;
                case 'f': out.push_back('\f'); break;
                case 'v': out.push_back('\v'); break;
                case '\\': case '"': case '\'': case '?': out.push_back(e); break;
                default:   // an unknown escape stays a backslash followed by the character (sjasmplus warns)
                    out.push_back('\\');
                    --i;
                    break;
            }
            continue;
        }
        out.push_back(c);
        ++i;
    }
    throw Failure{"string without its closing quote"};
}

/// Expressions with sjasmplus' priorities (documentation, "Expressions"), lowest first:
/// || · && · | or · ^ xor · & and · = == != · < > <= >= · << >> >>> shl shr · + - · * / % mod · unary
struct ExpressionParser
{
    std::string_view t;
    size_t i = 0;

    void Blanks()
    {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t'))
            ++i;
    }
    char Peek(size_t ahead = 0) const { return i + ahead < t.size() ? t[i + ahead] : '\0'; }

    std::string Word() const
    {
        size_t j = i;
        while (j < t.size() && std::isalpha(static_cast<unsigned char>(t[j])))
            ++j;
        if (j < t.size() && IsLabelChar(t[j]))
            return {};
        return z80::Lower(t.substr(i, j - i));
    }

    static Expr Wrap(Expr::Kind kind, Expr inner)
    {
        Expr e = Expr::Make(kind);
        e.args.push_back(std::move(inner));
        return e;
    }

    Expr Number()
    {
        size_t j = i;
        while (j < t.size() && std::isalnum(static_cast<unsigned char>(t[j])))
            ++j;
        const std::string run(t.substr(i, j - i));
        i = j;
        auto all = [](std::string_view s, const char* set) { return !s.empty() && s.find_first_not_of(set) == std::string_view::npos; };
        if (j + 1 < t.size() && t[j] == '_' && std::strchr("bBfF", t[j + 1]) && (j + 2 == t.size() || !std::isalnum(static_cast<unsigned char>(t[j + 2]))) &&
            all(run, "0123456789"))
        {
            // 1_B / 1_F: a temporary label in any expression (sjasmplus 1.19); 1B / 1F only in a jump
            i = j + 2;
            Expr raw = Expr::Make(Expr::Kind::Raw);
            raw.text = run + "_" + std::string(1, t[j + 1]);
            return raw;
        }
        const char* hex = "0123456789abcdefABCDEF";
        const char last = static_cast<char>(std::tolower(static_cast<unsigned char>(run.back())));
        if (run.size() > 2 && run[0] == '0' && (run[1] == 'x' || run[1] == 'X') && all(std::string_view(run).substr(2), hex))
            return Expr::Number(std::stoll(run.substr(2), nullptr, 16), ir::NumberSpelling::Hex, static_cast<int>(run.size() - 2));
        if (run.size() > 2 && run[0] == '0' && (run[1] == 'b' || run[1] == 'B') && all(std::string_view(run).substr(2), "01"))
            return Expr::Number(std::stoll(run.substr(2), nullptr, 2), ir::NumberSpelling::Binary, static_cast<int>(run.size() - 2));
        if (last == 'h' && all(std::string_view(run).substr(0, run.size() - 1), hex))
            return Expr::Number(std::stoll(run.substr(0, run.size() - 1), nullptr, 16), ir::NumberSpelling::Hex, static_cast<int>(run.size() - 1));
        if (all(run, "0123456789"))
            return Expr::Number(std::stoll(run), ir::NumberSpelling::Decimal, static_cast<int>(run.size()));
        if ((last == 'b' || last == 'f') && all(std::string_view(run).substr(0, run.size() - 1), "0123456789"))
        {
            // 1B / 1F: a temporary label backwards / forwards (or 101b, binary): written back as it is, sjasmplus decides
            Expr raw = Expr::Make(Expr::Kind::Raw);
            raw.text = run;
            return raw;
        }
        if (last == 'b' && all(std::string_view(run).substr(0, run.size() - 1), "01"))
            return Expr::Number(std::stoll(run.substr(0, run.size() - 1), nullptr, 2), ir::NumberSpelling::Binary, static_cast<int>(run.size() - 1));
        throw Failure{"number " + run + " in a form the converter does not read"};
    }

    Expr Primary()
    {
        Blanks();
        const char c = Peek();
        if (c == '(')
        {
            ++i;
            Expr inner = Parse(0);
            Blanks();
            if (Peek() != ')')
                throw Failure{"( without )"};
            ++i;
            return Wrap(Expr::Kind::Group, std::move(inner));
        }
        if (c == '{')
        {
            ++i;
            Blanks();
            // {b address}: the byte there, the low byte of the word {address}
            const bool byte = (Peek() == 'b' || Peek() == 'B') && (Peek(1) == ' ' || Peek(1) == '\t');
            if (byte)
                ++i;
            Expr inner = Parse(0);
            Blanks();
            if (Peek() != '}')
                throw Failure{"{ without }"};
            ++i;
            Expr read = Wrap(Expr::Kind::Memory, std::move(inner));
            return byte ? Wrap(Expr::Kind::Group, Expr::Binary(Op::And, std::move(read), Expr::Number(255))) : read;
        }
        if (c == '$')
        {
            if (Peek(1) == '$')
            {
                if (Peek(2) == '$' && !IsLabelChar(Peek(3)))
                {
                    i += 3;   // $$$: where the code goes while DISP is active
                    return Expr::Make(Expr::Kind::CurrentPhysical);
                }
                if (Peek(2) == '$' || IsLabelChar(Peek(2)))
                    throw Failure{"$$label has no counterpart"};
                i += 2;
                return Expr::Make(Expr::Kind::CurrentPage);
            }
            if (std::isxdigit(static_cast<unsigned char>(Peek(1))))
            {
                size_t j = ++i;
                while (j < t.size() && std::isxdigit(static_cast<unsigned char>(t[j])))
                    ++j;
                const Expr e = Expr::Number(std::stoll(std::string(t.substr(i, j - i)), nullptr, 16), ir::NumberSpelling::Hex, static_cast<int>(j - i));
                i = j;
                return e;
            }
            ++i;
            return Expr::Make(Expr::Kind::Current);
        }
        if (c == '#' || c == '%')
        {
            const bool isHex = c == '#';
            size_t j = ++i;
            while (j < t.size() && (isHex ? std::isxdigit(static_cast<unsigned char>(t[j])) != 0 : (t[j] == '0' || t[j] == '1')))
                ++j;
            if (j == i)
                throw Failure{std::string(1, c) + " without digits"};
            const Expr e = Expr::Number(std::stoll(std::string(t.substr(i, j - i)), nullptr, isHex ? 16 : 2),
                                        isHex ? ir::NumberSpelling::Hex : ir::NumberSpelling::Binary, static_cast<int>(j - i));
            i = j;
            return e;
        }
        if (std::isdigit(static_cast<unsigned char>(c)))
            return Number();
        if (c == '\'' || c == '"')
        {
            const std::string chars = StringLiteral(t, i);
            int64_t value = 0;
            for (const char ch : chars)
                value = (value << 8) | static_cast<unsigned char>(ch);
            Expr e = Expr::Number(value, ir::NumberSpelling::Character, static_cast<int>(chars.size()));
            e.text = chars;
            return e;
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

    Expr Unary()
    {
        Blanks();
        const char c = Peek();
        if (c == '-' || c == '+' || c == '~' || (c == '!' && Peek(1) != '='))
        {
            ++i;
            const Op op = c == '-' ? Op::Negate : c == '+' ? Op::Plus : c == '~' ? Op::Not : Op::LogicalNot;
            return Expr::Unary(op, Unary());
        }
        const std::string w = Word();
        if (w == "high" || w == "low" || w == "not" || w == "exist")
        {
            i += w.size();
            const Op op = w == "high" ? Op::High : w == "low" ? Op::Low : w == "not" ? Op::LogicalNot : Op::Exists;
            return Expr::Unary(op, Unary());
        }
        if (w == "norel" && t.find_first_not_of(" \t", i + w.size()) != std::string_view::npos &&
            std::string(",)").find(t[t.find_first_not_of(" \t", i + w.size())]) == std::string::npos)
        {
            // the value as it is, not relocatable (RELOCATE_START): a group the layout knows by its text
            i += w.size();
            Expr e = Wrap(Expr::Kind::Group, Unary());
            e.text = "norel";
            return e;
        }
        if (w == "abs" || w == "sizeof")
            throw Failure{"operator " + w + " has no counterpart"};
        return Primary();
    }

    static int Priority(Op op)
    {
        switch (op)
        {
            case Op::Mul: case Op::Div: case Op::Mod: return 9;
            case Op::Add: case Op::Sub: return 8;
            case Op::Shl: case Op::Shr: case Op::ShrUnsigned: return 7;
            case Op::Less: case Op::Greater: case Op::LessEqual: case Op::GreaterEqual: return 6;
            case Op::Equal: case Op::NotEqual: return 5;
            case Op::And: return 4;
            case Op::Xor: return 3;
            case Op::Or: return 2;
            case Op::LogicalAnd: return 1;
            default: return 0;
        }
    }

    /// The binary operator at the position (not consumed): its length, 0 = none
    size_t BinaryOp(Op& op) const
    {
        struct Symbol
        {
            const char* text;
            Op op;
        };
        static const Symbol symbols[] = {
            {">>>", Op::ShrUnsigned}, {"<<", Op::Shl}, {">>", Op::Shr}, {"<=", Op::LessEqual}, {">=", Op::GreaterEqual},
            {"==", Op::Equal},        {"!=", Op::NotEqual}, {"<>", Op::NotEqual}, {"&&", Op::LogicalAnd}, {"||", Op::LogicalOr},
            {"*", Op::Mul},           {"/", Op::Div},   {"%", Op::Mod},   {"+", Op::Add},          {"-", Op::Sub},
            {"<", Op::Less},          {">", Op::Greater}, {"=", Op::Equal}, {"&", Op::And},          {"^", Op::Xor},
            {"|", Op::Or},
        };
        if ((Peek() == '<' || Peek() == '>') && Peek(1) == '?')
            throw Failure{"minimum / maximum operators have no counterpart"};
        for (const Symbol& s : symbols)
        {
            const std::string_view text(s.text);
            if (t.substr(i, text.size()) == text)
            {
                op = s.op;
                return text.size();
            }
        }
        const std::string w = Word();
        static const std::pair<const char*, Op> words[] = {{"mod", Op::Mod}, {"shl", Op::Shl}, {"shr", Op::Shr},
                                                           {"and", Op::And}, {"xor", Op::Xor}, {"or", Op::Or}};
        for (const auto& [name, wordOp] : words)
            if (w == name)
            {
                op = wordOp;
                return w.size();
            }
        return 0;
    }

    Expr Parse(int minPriority)
    {
        Expr left = Unary();
        while (true)
        {
            Blanks();
            Op op = Op::Add;
            const size_t length = BinaryOp(op);
            if (length == 0 || Priority(op) < minPriority)
                return left;
            i += length;
            Expr right = Parse(Priority(op) + 1);
            left = Expr::Binary(op, std::move(left), std::move(right));
        }
    }
};

/// An expression; what the converter cannot read stays as its text (the sjasmplus backend writes it back)
Expr ParseExpression(std::string_view text, uint32_t line, Diagnostics& diagnostics)
{
    ExpressionParser p{text};
    try
    {
        Expr e = p.Parse(0);
        p.Blanks();
        if (p.i != text.size())
            throw Failure{"unexpected text after an expression: " + std::string(text.substr(p.i))};
        return e;
    }
    catch (const Failure& f)
    {
        diagnostics.push_back({Severity::Info, line, 0, "expression kept as text (" + f.reason + "): " + std::string(text)});
    }
    catch (const std::exception&)
    {
        diagnostics.push_back({Severity::Info, line, 0, "expression kept as text (a number out of range): " + std::string(text)});
    }
    Expr raw = Expr::Make(Expr::Kind::Raw);
    raw.text = Trim(text);
    return raw;
}

bool WhollyParenthesized(std::string_view v)
{
    const std::string m = Mask(v);
    if (m.size() < 2 || m.front() != '(' || m.back() != ')')
        return false;
    int depth = 0;
    for (size_t k = 0; k < m.size(); ++k)
    {
        if (m[k] == '(')
            ++depth;
        else if (m[k] == ')' && --depth == 0 && k + 1 != m.size())
            return false;
    }
    return true;
}

struct Context
{
    uint32_t line = 0;
    Diagnostics& diagnostics;
    std::set<std::string>& macros;
    bool z80n = false;   ///< the Next mode is on from this line on
    const std::vector<std::string>* params = nullptr;   ///< the parameters of the macro being defined

    /// sjasmplus puts a macro's argument wherever its parameter's name stands as a word, case kept: `a` in
    /// `MACRO T a` ... `LD A,a` is the parameter, `A` the register
    bool IsParameter(const std::string& text) const
    {
        return params && std::find(params->begin(), params->end(), text) != params->end();
    }

    Expr Parse(std::string_view text) const { return ParseExpression(text, line, diagnostics); }

    Operand InstructionOperand(const std::string& text, bool conditionAllowed, bool flagAllowed) const
    {
        Operand o;
        if (IsParameter(text))
        {
            o.kind = Operand::Kind::Immediate;
            o.expr = Parse(text);
            return o;
        }
        const std::string lower = z80::Lower(text);
        if (conditionAllowed && z80::IsCondition(lower))
        {
            o.kind = Operand::Kind::Condition;
            o.text = lower;
            return o;
        }
        const std::string reg = z80::NormalizeRegister(lower);
        if (z80::IsRegister(reg) && (reg != "f" || flagAllowed))
        {
            o.kind = Operand::Kind::Register;
            o.text = reg;
            return o;
        }
        if (text.size() > 1 && text[0] == '+' && WhollyParenthesized(std::string_view(text).substr(1)))
        {
            // +(..): a value, not memory (the backend puts the plus back)
            o.kind = Operand::Kind::Immediate;
            o.expr = Parse(std::string_view(text).substr(1));
            return o;
        }
        if (WhollyParenthesized(text))
        {
            const std::string inner = Trim(std::string_view(text).substr(1, text.size() - 2));
            const std::string innerLower = IsParameter(inner) ? std::string() : z80::Lower(inner);
            if (innerLower == "hl" || innerLower == "bc" || innerLower == "de" || innerLower == "sp" || innerLower == "c" || innerLower == "ix" ||
                innerLower == "iy")
            {
                o.kind = Operand::Kind::Indirect;
                o.text = innerLower;
                return o;
            }
            if (innerLower.size() > 2 && (innerLower.rfind("ix", 0) == 0 || innerLower.rfind("iy", 0) == 0))
            {
                const std::string displacement = Trim(std::string_view(inner).substr(2));
                if (!displacement.empty() && (displacement[0] == '+' || displacement[0] == '-'))
                {
                    o.kind = Operand::Kind::Indexed;
                    o.text = innerLower.substr(0, 2);
                    o.expr = Parse(displacement);   // the sign is part of the displacement expression
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

    /// DB / DW / DISPLAY items: strings (more than one character) and expressions
    std::vector<Operand> DataOperands(const std::string& rest, bool display) const
    {
        std::vector<Operand> out;
        for (const std::string& item : Split(rest, ','))
        {
            Operand o;
            if (display && item.size() == 2 && item[0] == '/')
            {
                o.kind = Operand::Kind::Immediate;   // a DISPLAY format key (/D /H /A /L /T)
                o.expr = Expr::Make(Expr::Kind::Raw);
                o.expr.text = item;
                out.push_back(std::move(o));
                continue;
            }
            if (!item.empty() && (item[0] == '"' || item[0] == '\''))
            {
                try
                {
                    size_t k = 0;
                    const std::string chars = StringLiteral(item, k);
                    if (k == item.size() && chars.size() != 1)
                    {
                        o.kind = Operand::Kind::String;
                        o.text = chars;
                        out.push_back(std::move(o));
                        continue;
                    }
                }
                catch (const Failure&)
                {
                }
            }
            o.kind = Operand::Kind::Immediate;
            o.expr = Parse(item);
            out.push_back(std::move(o));
        }
        return out;
    }
};

struct Directive
{
    std::string_view name;
    ir::DirectiveKind kind;
};

constexpr Directive kDirectives[] = {
    {"ORG", ir::DirectiveKind::Org},           {"EQU", ir::DirectiveKind::Equ},          {"DEFL", ir::DirectiveKind::Defl},
    {"=", ir::DirectiveKind::Defl},            {"DB", ir::DirectiveKind::Db},            {"DEFB", ir::DirectiveKind::Db},
    {"BYTE", ir::DirectiveKind::Db},           {"DM", ir::DirectiveKind::Db},            {"DEFM", ir::DirectiveKind::Db},
    {"DW", ir::DirectiveKind::Dw},             {"DEFW", ir::DirectiveKind::Dw},          {"WORD", ir::DirectiveKind::Dw},
    {"DS", ir::DirectiveKind::Ds},             {"DEFS", ir::DirectiveKind::Ds},          {"BLOCK", ir::DirectiveKind::Ds},
    {"INCLUDE", ir::DirectiveKind::Include},   {"INCBIN", ir::DirectiveKind::Incbin},    {"INSERT", ir::DirectiveKind::Incbin},
    {"BINARY", ir::DirectiveKind::Incbin},     {"IF", ir::DirectiveKind::If},
    {"IFN", ir::DirectiveKind::If},            {"ELSE", ir::DirectiveKind::Else},        {"ENDIF", ir::DirectiveKind::EndIf},
    {"MACRO", ir::DirectiveKind::Macro},       {"ENDM", ir::DirectiveKind::EndMacro},    {"DUP", ir::DirectiveKind::Repeat},
    {"REPT", ir::DirectiveKind::Repeat},       {"EDUP", ir::DirectiveKind::EndRepeat},   {"ENDR", ir::DirectiveKind::EndRepeat},
    {"WHILE", ir::DirectiveKind::While},       {"ENDW", ir::DirectiveKind::EndWhile},    {"DISP", ir::DirectiveKind::Disp},
    {"PHASE", ir::DirectiveKind::Disp},        {"TEXTAREA", ir::DirectiveKind::Disp},    {"ENT", ir::DirectiveKind::Ent},
    {"UNPHASE", ir::DirectiveKind::Ent},       {"DEPHASE", ir::DirectiveKind::Ent},      {"ENDT", ir::DirectiveKind::Ent},
    {"DISPLAY", ir::DirectiveKind::Display},   {"END", ir::DirectiveKind::End},
};

// sjasmplus directives without an IR kind: kept as text
const std::set<std::string> kOtherDirectives = {
    "DEVICE",   "SLOT",      "PAGE",      "MMU",      "SAVEBIN",   "SAVESNA",   "SAVETAP",  "SAVEHOB",  "SAVETRD",    "SAVENEX",
    "SAVECPCSNA", "SAVECDT", "SAVEDEV",   "EMPTYTAP", "EMPTYTRD",  "MODULE",    "ENDMODULE", "DEFINE",  "DEFINE+",    "UNDEFINE",
    "IFDEF",    "IFNDEF",    "IFUSED",    "IFNUSED",  "ELSEIF",    "ALIGN",     "ASSERT",   "OUTPUT",   "OUTEND",     "LUA",
    "ENDLUA",   "STRUCT",    "ENDS",      "OPT",      "LABELSLIST", "CSPECTMAP", "SIZE",    "DEFARRAY", "DEFARRAY+",  "RELOCATE_START",
    "RELOCATE_END", "RELOCATE_TABLE", "INCHOB", "INCTRD", "DG",       "DEFG",     "DH",         "DEFH",
    "DZ",       "DC",        "D24",       "DWORD",    "DD",        "ABYTE",     "ABYTEC",   "ABYTEZ",   "FPOS",       "SHELLEXEC",
    "BPLIST",   "SETBP",     "SETBREAKPOINT", "EXPORT", "TAPOUT",  "TAPEND",    "ENCODING", "DEFDEVICE", "INCLUDELUA",
};

std::string FileName(const std::string& operand)
{
    std::string name = Trim(operand);
    if (name.size() >= 2 && ((name.front() == '"' && name.back() == '"') || (name.front() == '<' && name.back() == '>')))
        name = name.substr(1, name.size() - 2);
    return name;
}

Statement ParseStatement(const std::string& text, std::string& label, const Context& c)
{
    Statement s;
    size_t wordEnd = 0;
    while (wordEnd < text.size() && text[wordEnd] != ' ' && text[wordEnd] != '\t')
        ++wordEnd;
    std::string word = text.substr(0, wordEnd);
    if (word.size() > 1 && word[0] == '=')   // "=expr"
        wordEnd = 1, word = "=";
    const std::string rest = Trim(std::string_view(text).substr(wordEnd));
    std::string upper = z80::Upper(word);
    if (upper.size() > 1 && upper[0] == '.')
        upper.erase(0, 1);   // .db, .org: sjasmplus accepts a dot before a directive

    // ".3 INC HL": sjasmplus repeats the instruction; kept as written
    if (word.size() > 1 && word[0] == '.' && (std::isdigit(static_cast<unsigned char>(word[1])) || word[1] == '('))
    {
        s.kind = Statement::Kind::Directive;
        s.directive = ir::DirectiveKind::Other;
        s.text = text;
        return s;
    }
    // A macro may take a directive's or an instruction's name (sjasmplus looks for macros first)
    if (c.macros.count(word) && upper != "ENDM")
    {
        s.kind = Statement::Kind::MacroCall;
        s.mnemonic = word;
        s.params = Split(rest, ',');
        return s;
    }
    for (const Directive& d : kDirectives)
    {
        if (upper != d.name)
            continue;
        s.kind = Statement::Kind::Directive;
        s.directive = d.kind;
        switch (d.kind)
        {
            case ir::DirectiveKind::Db:
            case ir::DirectiveKind::Dw:
            case ir::DirectiveKind::Display:
                s.operands = c.DataOperands(rest, d.kind == ir::DirectiveKind::Display);
                break;
            case ir::DirectiveKind::Include:
            case ir::DirectiveKind::Incbin:
            {
                const std::vector<std::string> ops = Split(rest, ',');
                s.text = ops.empty() ? std::string() : FileName(ops[0]);
                if (d.kind == ir::DirectiveKind::Include && s.text.size() > 4 && z80::Lower(s.text.substr(s.text.size() - 4)) == ".asm")
                    s.text.resize(s.text.size() - 4);   // the IR names a source without its extension
                else if (d.kind == ir::DirectiveKind::Include)
                    s.params = {"verbatim"};            // another extension (.a80, .inc): the name stays as written
                for (size_t k = 1; k < ops.size(); ++k)
                    s.args.push_back(c.Parse(ops[k]));
                break;
            }
            case ir::DirectiveKind::Macro:
            {
                // MACRO name params, or name MACRO params (the label names the macro)
                std::vector<std::string> ops = Split(rest, ',');
                if (label.empty() && !ops.empty())
                {
                    std::string& first = ops[0];
                    size_t blank = first.find_first_of(" \t");
                    s.text = first.substr(0, blank);
                    first = blank == std::string::npos ? std::string() : Trim(std::string_view(first).substr(blank));
                    if (first.empty())
                        ops.erase(ops.begin());
                }
                else
                {
                    s.text = label;
                    label.clear();
                }
                s.params = ops;
                c.macros.insert(s.text);
                break;
            }
            case ir::DirectiveKind::If:
                s.args.push_back(c.Parse(rest));
                if (upper == "IFN")
                {
                    Expr g = Expr::Make(Expr::Kind::Group);
                    g.args.push_back(std::move(s.args[0]));
                    s.args[0] = Expr::Unary(Op::LogicalNot, std::move(g));
                }
                break;
            default:
                for (const std::string& op : Split(rest, ','))
                    if (!op.empty())
                        s.args.push_back(c.Parse(op));
                break;
        }
        return s;
    }

    if (kOtherDirectives.count(upper))
    {
        s.kind = Statement::Kind::Directive;
        s.directive = ir::DirectiveKind::Other;
        s.text = text;
        return s;
    }

    // sjasmplus' short forms: EXA = EX AF,AF', EXD = EX DE,HL
    if ((upper == "EXA" || upper == "EXD") && rest.empty())
    {
        s.kind = Statement::Kind::Instruction;
        s.mnemonic = "ex";
        Operand first, second;
        first.kind = second.kind = Operand::Kind::Register;
        first.text = upper == "EXA" ? "af" : "de";
        second.text = upper == "EXA" ? "af'" : "hl";
        s.operands = {first, second};
        return s;
    }
    const std::string mnemonic = z80::Lower(word);
    if (z80::IsMnemonic(mnemonic) || (c.z80n && z80::IsZ80nMnemonic(mnemonic)))
    {
        s.kind = Statement::Kind::Instruction;
        s.mnemonic = mnemonic;
        // --syntax=a writes ",," between the repeated operand groups (ld hl,1,,de,2): an empty item between them
        std::vector<std::string> ops = Split(rest, ',');
        ops.erase(std::remove_if(ops.begin(), ops.end(), [](const std::string& op) { return op.empty(); }), ops.end());
        for (size_t k = 0; k < ops.size(); ++k)
        {
            const bool condition = k == 0 && z80::TakesCondition(mnemonic) && (ops.size() > 1 || mnemonic == "ret");
            s.operands.push_back(c.InstructionOperand(ops[k], condition, mnemonic == "in" || mnemonic == "out"));
        }
        // what sjasmplus reads otherwise than it is written: EX HL,DE is EX DE,HL; an ALU operation's (n) is the number n
        // (the Z80 has no ADD A,(nn): sjasmplus assembles the value)
        auto reg = [&](size_t k, const char* name) { return k < s.operands.size() && s.operands[k].kind == Operand::Kind::Register && s.operands[k].text == name; };
        if (mnemonic == "ex" && s.operands.size() == 2 && reg(0, "hl") && reg(1, "de"))
            std::swap(s.operands[0], s.operands[1]);
        // LD r,(n) with r not A: there is no such load either (LD H,(TABLE/256) is LD H,TABLE/256)
        static const std::set<std::string> kByteRegisters = {"b", "c", "d", "e", "h", "l", "ixh", "ixl", "iyh", "iyl", "i", "r"};
        if (mnemonic == "ld" && s.operands.size() == 2 && s.operands[0].kind == Operand::Kind::Register && kByteRegisters.count(s.operands[0].text) &&
            s.operands[1].kind == Operand::Kind::Memory)
            s.operands[1].kind = Operand::Kind::Immediate;
        static const std::set<std::string> kAlu = {"add", "adc", "sub", "sbc", "and", "xor", "or", "cp"};
        if (kAlu.count(mnemonic))
            for (Operand& o : s.operands)
                if (o.kind == Operand::Kind::Memory)
                    o.kind = Operand::Kind::Immediate;
        return s;
    }

    // Anything else in the command place is a macro call (the macro may come from another file of the project)
    s.kind = Statement::Kind::MacroCall;
    s.mnemonic = word;
    s.params = Split(rest, ',');
    return s;
}
}  // namespace

namespace
{
/// --syntax=a: "ld hl,1,,de,2" is "ld hl,1" and "ld de,2" (",," between the repeated operand groups); other text as it is
std::vector<std::string> MultiArgGroups(const std::string& part)
{
    const size_t wordEnd = part.find_first_of(" \t");
    if (wordEnd == std::string::npos || !z80::IsMnemonic(z80::Lower(part.substr(0, wordEnd))))
        return {part};
    std::vector<std::string> groups;
    std::string current;
    char quote = 0;
    int depth = 0;
    const std::string rest = part.substr(wordEnd);
    for (size_t k = 0; k < rest.size(); ++k)
    {
        const char c = rest[k];
        if (quote)
            quote = c == quote ? 0 : quote;
        else if (c == '"' || (c == '\'' && !(k >= 2 && std::tolower(static_cast<unsigned char>(rest[k - 1])) == 'f' &&
                                               std::tolower(static_cast<unsigned char>(rest[k - 2])) == 'a')))
            quote = c;
        else if (c == '(' || c == '[')
            ++depth;
        else if (c == ')' || c == ']')
            --depth;
        else if (c == ',' && depth == 0 && k + 1 < rest.size() && rest[k + 1] == ',')
        {
            groups.push_back(part.substr(0, wordEnd) + " " + Trim(current));
            current.clear();
            ++k;
            continue;
        }
        current += c;
    }
    groups.push_back(part.substr(0, wordEnd) + " " + Trim(current));
    return groups;
}

/// 101b: sjasmplus reads a temporary label backwards only in a jump's operand (JR 1B); anywhere else digits 0 and 1
/// before b are a binary number
void BinaryRaw(Expr& e)
{
    for (Expr& a : e.args)
        BinaryRaw(a);
    if (e.kind != Expr::Kind::Raw || e.text.size() < 2)
        return;
    const char last = e.text.back();
    const std::string body = e.text.substr(0, e.text.size() - 1);
    if ((last == 'b' || last == 'B') && body.find_first_not_of("01") == std::string::npos)
        e = Expr::Number(std::stoll(body, nullptr, 2), ir::NumberSpelling::Binary, static_cast<int>(body.size()));
}

void BinaryOutsideJumps(Statement& s)
{
    if (s.kind == Statement::Kind::Instruction)
    {
        const std::string m = z80::Upper(s.mnemonic);
        if (m == "JR" || m == "JP" || m == "DJNZ" || m == "CALL")
        {
            // a jump's target named like a register is a label (DJNZ de: the label de)
            for (Operand& o : s.operands)
                if (o.kind == Operand::Kind::Register)
                {
                    o.kind = Operand::Kind::Immediate;
                    o.expr = Expr::Symbol(o.text);
                    o.text.clear();
                }
            return;
        }
    }
    if (s.kind == Statement::Kind::MacroCall)
        return;   // its arguments are text
    if (s.kind == Statement::Kind::Instruction && s.operands.size() == 2)
    {
        // OUT n,A and IN A,n: the port without parentheses, as OUT (n),A
        const std::string m = z80::Lower(s.mnemonic);
        Operand& port = m == "out" ? s.operands[0] : s.operands[1];
        const Operand& other = m == "out" ? s.operands[1] : s.operands[0];
        if ((m == "out" || m == "in") && port.kind == Operand::Kind::Immediate && other.kind == Operand::Kind::Register && other.text == "a")
            port.kind = Operand::Kind::Memory;
    }
    for (Operand& o : s.operands)
        BinaryRaw(o.expr);
    for (Expr& a : s.args)
        BinaryRaw(a);
}

/// Where an instruction's value field starts (label+*, sjasmplus' smart SMC): the immediate value or the address in
/// (nn), a word for (nn), JP, CALL and LD rr,nn, else a byte (JR's offset too); none for an index's displacement, a port
std::optional<int64_t> ValueField(const Statement& s)
{
    if (s.kind != Statement::Kind::Instruction)
        return std::nullopt;
    const std::string m = z80::Lower(s.mnemonic);
    if (m == "in" || m == "out" || m == "rst" || m == "im")
        return std::nullopt;
    bool immediate = false, memory = false;
    for (const Operand& o : s.operands)
    {
        immediate = immediate || o.kind == Operand::Kind::Immediate;
        memory = memory || o.kind == Operand::Kind::Memory;
    }
    if (!immediate && !memory)
        return std::nullopt;
    const bool pair = !s.operands.empty() && s.operands[0].kind == Operand::Kind::Register &&
                      (s.operands[0].text == "bc" || s.operands[0].text == "de" || s.operands[0].text == "hl" || s.operands[0].text == "sp" ||
                       s.operands[0].text == "ix" || s.operands[0].text == "iy");
    const int width = memory || m == "jp" || m == "call" || (m == "ld" && pair) ? 2 : 1;
    std::string error;
    const int size = layout::InstructionSize(s, error);
    if (size <= width)
        return std::nullopt;
    return size - width;
}

/// The DEFINEs' values in a line where their names stand as whole words (outside strings)
std::string Substitute(const std::string& text, const std::map<std::string, std::string>& defines)
{
    std::string out;
    char quote = 0;
    for (size_t k = 0; k < text.size();)
    {
        const char c = text[k];
        if (quote)
        {
            out.push_back(c);
            if (c == '\\' && quote == '"' && k + 1 < text.size())
                out.push_back(text[++k]);
            else if (c == quote)
                quote = 0;
            ++k;
            continue;
        }
        if (c == '"' || (c == '\'' && !(k >= 2 && std::toupper(static_cast<unsigned char>(text[k - 1])) == 'F' &&
                                         std::toupper(static_cast<unsigned char>(text[k - 2])) == 'A')))
        {
            quote = c;
            out.push_back(c);
            ++k;
            continue;
        }
        const bool wordStart = (std::isalpha(static_cast<unsigned char>(c)) || c == '_') &&
                               (k == 0 || !(std::isalnum(static_cast<unsigned char>(text[k - 1])) || text[k - 1] == '_' || text[k - 1] == '.' ||
                                            text[k - 1] == '@' || text[k - 1] == '$' || text[k - 1] == '#'));
        if (wordStart)
        {
            size_t end = k;
            while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_'))
                ++end;
            const std::string word = text.substr(k, end - k);
            const auto found = defines.find(word);
            const bool partOfName = end < text.size() && (text[end] == '.' || text[end] == '?' || text[end] == '!');
            out += found != defines.end() && !partOfName ? found->second : word;
            k = end;
            continue;
        }
        out.push_back(c);
        ++k;
    }
    return out;
}
}  // namespace

std::optional<std::vector<uint8_t>> SjasmplusBitBytes(const std::string& word, const std::string& operands)
{
    std::vector<uint8_t> out;
    if (word == "DG" || word == "DEFG")
    {
        int bits = 0, value = 0;
        for (const char c : operands)
        {
            if (c == ' ' || c == '\t')
                continue;
            value = (value << 1) | (c == '-' || c == '.' || c == '_' ? 0 : 1);
            if (++bits == 8)
            {
                out.push_back(static_cast<uint8_t>(value));
                bits = value = 0;
            }
        }
        if (bits != 0 || out.empty())
            return std::nullopt;
        return out;
    }
    if (word == "DH" || word == "DEFH")
    {
        bool quoted = false;
        std::string digits;
        for (const char c : operands)
        {
            if (c == '"')
            {
                quoted = !quoted;
                continue;
            }
            if (!quoted || c == ' ' || c == '\t')
                continue;
            if (!std::isxdigit(static_cast<unsigned char>(c)))
                return std::nullopt;
            digits += c;
            if (digits.size() == 2)
            {
                out.push_back(static_cast<uint8_t>(std::stoi(digits, nullptr, 16)));
                digits.clear();
            }
        }
        if (!digits.empty() || out.empty())
            return std::nullopt;
        return out;
    }
    return std::nullopt;
}

std::vector<std::string> SjasmplusStructValues(const std::string& text, const std::vector<bool>& textLeaves)
{
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    char quote = 0;
    for (size_t k = 0; k < text.size(); ++k)
    {
        const char c = text[k];
        if (quote)
        {
            current.push_back(c);
            if (c == quote)
                quote = 0;
            continue;
        }
        if (c == '"' || c == '\'')
            quote = c;
        depth += c == '(';
        depth -= c == ')';
        if (depth == 0 && c == '{' && out.size() < textLeaves.size() && textLeaves[out.size()] && Trim(current).empty())
        {
            // a TEXT leaf's bytes: the group up to its closing brace
            size_t end = k + 1;
            for (char q = 0; end < text.size() && (q || text[end] != '}'); ++end)
                q = q ? (text[end] == q ? 0 : q) : (text[end] == '"' || text[end] == '\'' ? text[end] : 0);
            current = text.substr(k, end - k + 1);
            k = end;
            continue;
        }
        if (depth == 0 && (c == '{' || c == '}'))
            continue;
        if (depth == 0 && c == ',')
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

std::pair<std::string, std::string> SjasmplusAbyteParts(const std::string& operands)
{
    const std::string text = Trim(operands);
    const SjasmplusFrontend frontend;
    std::pair<std::string, std::string> parts{text, std::string()};
    for (size_t at = text.find_first_of(" \t"); at != std::string::npos; at = text.find_first_of(" \t", at + 1))
    {
        const FrontendResult r = frontend.Parse(SourceDocument::FromText("        DW " + text.substr(0, at), "sjasmplus"));
        const bool one = r.program.lines.size() == 1 && r.program.lines[0].statements.size() == 1 && r.program.lines[0].statements[0].operands.size() == 1 &&
                         r.program.lines[0].statements[0].operands[0].kind == Operand::Kind::Immediate &&
                         r.program.lines[0].statements[0].operands[0].expr.kind != Expr::Kind::Raw;
        if (one)
            parts = {Trim(text.substr(0, at)), Trim(text.substr(at))};
    }
    return parts;
}

std::vector<ir::Statement> SjasmplusFake(const ir::Statement& s)
{
    const std::vector<Operand>& ops = s.operands;
    if (s.kind != Statement::Kind::Instruction || ops.empty() || ops.size() > 2)
        return {s};
    const std::string m = z80::Lower(s.mnemonic);
    const auto reg = [](const std::string& r) {
        Operand o;
        o.kind = Operand::Kind::Register;
        o.text = r;
        return o;
    };
    const auto one = [&](const char* mnemonic, std::vector<Operand> operands) {
        Statement t = s;
        t.mnemonic = mnemonic;
        t.operands = std::move(operands);
        return t;
    };
    const auto is = [](const Operand& o, std::initializer_list<const char*> names) {
        return o.kind == Operand::Kind::Register && std::any_of(names.begin(), names.end(), [&](const char* n) { return o.text == n; });
    };
    // the halves of a pair: high, low
    const auto halves = [](const std::string& r) -> std::pair<std::string, std::string> {
        if (r == "bc") return {"b", "c"};
        if (r == "de") return {"d", "e"};
        if (r == "hl") return {"h", "l"};
        if (r == "ix") return {"ixh", "ixl"};
        if (r == "iy") return {"iyh", "iyl"};
        return {};
    };
    // (IX+d) / (IX) / (IY+d) / (IY): the index register, else ""
    const auto index = [](const Operand& o) -> std::string {
        if (o.kind == Operand::Kind::Indexed || (o.kind == Operand::Kind::Indirect && (o.text == "ix" || o.text == "iy")))
            return o.text;
        return {};
    };
    const auto next = [](Operand o) {
        if (o.kind == Operand::Kind::Indirect)   // (IX) is (IX+0)
        {
            o.kind = Operand::Kind::Indexed;
            o.expr = Expr::Number(0);
        }
        o.expr = Expr::Binary(Op::Add, o.expr, Expr::Number(1));
        return o;
    };
    const bool hlMemory0 = ops[0].kind == Operand::Kind::Indirect && ops[0].text == "hl";
    const bool hlMemory1 = ops.size() == 2 && ops[1].kind == Operand::Kind::Indirect && ops[1].text == "hl";

    if (ops.size() == 1)
    {
        // RL BC ... SRL HL: the pair's halves shifted through the carry
        if (!is(ops[0], {"bc", "de", "hl"}))
            return {s};
        const auto [high, low] = halves(ops[0].text);
        if (m == "rl")
            return {one("rl", {reg(low)}), one("rl", {reg(high)})};
        if (m == "rr")
            return {one("rr", {reg(high)}), one("rr", {reg(low)})};
        if (m == "sla" && ops[0].text == "hl")
            return {one("add", {reg("hl"), reg("hl")})};
        if (m == "sla")
            return {one("sla", {reg(low)}), one("rl", {reg(high)})};
        if (m == "sll" || m == "sli")
            return {one("sli", {reg(low)}), one("rl", {reg(high)})};
        if (m == "sra" || m == "srl")
            return {one(m == "sra" ? "sra" : "srl", {reg(high)}), one("rr", {reg(low)})};
        return {s};
    }

    // ADD / ADC / SBC / SUB DE,rr: through HL (EX DE,HL around); SUB HL,rr: OR A : SBC HL,rr
    if ((m == "add" || m == "adc" || m == "sbc" || m == "sub") && is(ops[0], {"de"}) && is(ops[1], {"bc", "de", "hl", "sp"}))
    {
        const std::string other = ops[1].text == "de" ? "hl" : ops[1].text == "hl" ? "de" : ops[1].text;
        std::vector<Statement> out;
        if (m == "sub")
            out.push_back(one("or", {reg("a")}));
        out.push_back(one("ex", {reg("de"), reg("hl")}));
        out.push_back(one(m == "sub" ? "sbc" : m == "add" ? "add" : m == "adc" ? "adc" : "sbc", {reg("hl"), reg(other)}));
        out.push_back(one("ex", {reg("de"), reg("hl")}));
        return out;
    }
    if (m == "sub" && is(ops[0], {"hl"}) && is(ops[1], {"bc", "de", "hl", "sp"}))
        return {one("or", {reg("a")}), one("sbc", ops)};

    if (m == "ld")
    {
        const Operand& d = ops[0];
        const Operand& v = ops[1];
        // LD rr,rr': two 8-bit loads, PUSH / POP where an index register meets HL or the other one
        if (is(d, {"bc", "de", "hl", "ix", "iy"}) && is(v, {"bc", "de", "hl", "ix", "iy"}))
        {
            const bool d16 = d.text == "ix" || d.text == "iy", v16 = v.text == "ix" || v.text == "iy";
            if ((d16 && (v.text == "hl" || (v16 && v.text != d.text))) || (v16 && d.text == "hl"))
                return {one("push", {v}), one("pop", {d})};
            const auto dh = halves(d.text), vh = halves(v.text);
            return {one("ld", {reg(dh.first), reg(vh.first)}), one("ld", {reg(dh.second), reg(vh.second)})};
        }
        // LD rr,(HL) / LD (HL),rr: through HL and back
        if (is(d, {"bc", "de"}) && hlMemory1)
        {
            const auto [high, low] = halves(d.text);
            return {one("ld", {reg(low), v}), one("inc", {reg("hl")}), one("ld", {reg(high), v}), one("dec", {reg("hl")})};
        }
        if (hlMemory0 && is(v, {"bc", "de"}))
        {
            const auto [high, low] = halves(v.text);
            return {one("ld", {d, reg(low)}), one("inc", {reg("hl")}), one("ld", {d, reg(high)}), one("dec", {reg("hl")})};
        }
        // LD rr,(IX+d) / LD (IX+d),rr: the low byte at d
        if (is(d, {"bc", "de", "hl"}) && !index(v).empty())
        {
            const auto [high, low] = halves(d.text);
            return {one("ld", {reg(low), v}), one("ld", {reg(high), next(v)})};
        }
        if (!index(d).empty() && is(v, {"bc", "de", "hl"}))
        {
            const auto [high, low] = halves(v.text);
            return {one("ld", {d, reg(low)}), one("ld", {next(d), reg(high)})};
        }
        return {s};
    }

    if (m == "ldi" || m == "ldd")
    {
        // LDI / LDD with operands: the load, then the pointer moved (LDI A,(HL) is LD A,(HL) : INC HL)
        const Operand& d = ops[0];
        const Operand& v = ops[1];
        const char* step = m == "ldi" ? "inc" : "dec";
        const auto pointer = [&](const Operand& o) -> std::string {
            if (!index(o).empty())
                return index(o);
            if (o.kind == Operand::Kind::Indirect && (o.text == "hl" || o.text == "bc" || o.text == "de"))
                return o.text;
            return {};
        };
        // LDI rr,(HL) / (IX+d) and LDI (HL) / (IX+d),rr: a byte, the pointer, the other byte, the pointer
        if (m == "ldi" && is(d, {"bc", "de", "hl"}) && (pointer(v) == "hl" || !index(v).empty()) && !(d.text == "hl" && pointer(v) == "hl"))
        {
            const auto [high, low] = halves(d.text);
            const std::string p = pointer(v);
            return {one("ld", {reg(low), v}), one("inc", {reg(p)}), one("ld", {reg(high), v}), one("inc", {reg(p)})};
        }
        if (m == "ldi" && is(v, {"bc", "de", "hl"}) && (pointer(d) == "hl" || !index(d).empty()) && !(v.text == "hl" && pointer(d) == "hl"))
        {
            const auto [high, low] = halves(v.text);
            const std::string p = pointer(d);
            return {one("ld", {d, reg(low)}), one("inc", {reg(p)}), one("ld", {d, reg(high)}), one("inc", {reg(p)})};
        }
        // LDI r,(rr) / LDI (rr),r / LDI (rr),n: (BC) and (DE) with A only
        const bool byteReg0 = is(d, {"a", "b", "c", "d", "e", "h", "l"}), byteReg1 = is(v, {"a", "b", "c", "d", "e", "h", "l"});
        if (byteReg0 && !pointer(v).empty() && (d.text == "a" || pointer(v) == "hl" || !index(v).empty()))
            return {one("ld", {d, v}), one(step, {reg(pointer(v))})};
        if (!pointer(d).empty() && (byteReg1 || v.kind == Operand::Kind::Immediate) && (pointer(d) == "hl" || !index(d).empty() || is(v, {"a"})))
            return {one("ld", {d, v}), one(step, {reg(pointer(d))})};
        return {s};
    }
    return {s};
}

std::vector<ir::Statement> SjasmplusRepeated(const ir::Statement& s)
{
    const std::vector<Operand>& ops = s.operands;
    if (s.kind != Statement::Kind::Instruction || ops.size() < 2)
        return {s};
    const std::string m = z80::Lower(s.mnemonic);
    const auto reg = [&](size_t k, std::initializer_list<const char*> names) {
        return ops[k].kind == Operand::Kind::Register && std::any_of(names.begin(), names.end(), [&](const char* n) { return ops[k].text == n; });
    };
    const auto split = [&](size_t width, std::optional<Operand> first) {
        std::vector<Statement> out;
        for (size_t k = first ? 1 : 0; k + width <= ops.size(); k += width)
        {
            Statement one = s;
            one.operands.clear();
            if (first)
                one.operands.push_back(*first);
            one.operands.insert(one.operands.end(), ops.begin() + static_cast<std::ptrdiff_t>(k), ops.begin() + static_cast<std::ptrdiff_t>(k + width));
            out.push_back(std::move(one));
        }
        return out;
    };
    if (m == "jp" || m == "jr" || m == "call")
    {
        if (ops[0].kind != Operand::Kind::Condition)
            return split(1, std::nullopt);
        if (ops.size() <= 2 || ops.size() % 2 != 0)
            return {s};
        // the first operand of each pair a condition (the frontend takes only the first for one)
        std::vector<Statement> out = split(2, std::nullopt);
        for (Statement& one : out)
        {
            Operand& c = one.operands[0];
            if (c.kind == Operand::Kind::Immediate && c.expr.kind == Expr::Kind::Symbol && z80::IsCondition(z80::Lower(c.expr.text)))
            {
                c.kind = Operand::Kind::Condition;
                c.text = z80::Lower(c.expr.text);
                c.expr = Expr();
            }
        }
        return out;
    }
    if (m == "push" || m == "pop" || m == "inc" || m == "dec" || m == "djnz" || m == "rst")
        return split(1, std::nullopt);
    if (m == "sub" || m == "and" || m == "or" || m == "xor" || m == "cp")
        return reg(0, {"hl", "ix", "iy", "bc", "de"}) ? std::vector<Statement>{s} : split(1, std::nullopt);   // SUB HL,DE: a fake instruction
    if (m == "rl" || m == "rr" || m == "rlc" || m == "rrc" || m == "sla" || m == "sra" || m == "sli" || m == "sll" || m == "srl")
        return ops[0].kind == Operand::Kind::Indexed || reg(0, {"bc", "de", "hl"}) ? std::vector<Statement>{s} : split(1, std::nullopt);
    if ((m == "add" || m == "adc" || m == "sbc") && reg(0, {"a"}) && ops.size() > 2)
        return split(1, ops[0]);
    if ((m == "set" || m == "res") && ops.size() == 3 && ops[1].kind == Operand::Kind::Indexed)
        return {s};   // RES 0,(IX+1),A: the undocumented copy
    if ((m == "ld" || m == "ex" || m == "add" || m == "adc" || m == "sbc" || m == "in" || m == "out" || m == "bit" || m == "set" || m == "res") &&
        ops.size() > 2 && ops.size() % 2 == 0)
        return split(2, std::nullopt);
    return {s};
}

FrontendResult SjasmplusFrontend::Parse(const SourceDocument& source) const
{
    FrontendResult result;
    result.program.dialect = "sjasmplus";
    result.program.trueValue = -1;   // sjasmplus' comparisons give -1
    std::set<std::string> macros;
    std::vector<std::string> macroParams;   // the parameters of the macro being defined
    std::map<std::string, std::string> defines;   // DEFINE name value
    bool z80n = source.z80n;   // the Next mode: the project enables it, or DEVICE ZXSPECTRUMNEXT / OPT --zxnext below
    bool inBlock = false;   // inside /* ... */
    uint32_t number = 0;
    for (const SourceLine& sourceLine : source.lines)
    {
        ++number;
        ir::Line line;
        line.sourceLine = number;
        std::string text = sourceLine.text;

        // Block comments /* ... */, also over several lines: their text becomes line comments
        {
            std::string kept, note;
            size_t at = 0;
            while (at < text.size())
            {
                if (inBlock)
                {
                    const size_t end = text.find("*/", at);
                    note += text.substr(at, end == std::string::npos ? std::string::npos : end - at);
                    if (end == std::string::npos)
                        break;
                    inBlock = false;
                    at = end + 2;
                    continue;
                }
                const std::string m = Mask(text.substr(at));
                const size_t open = m.find("/*");
                const size_t semicolon = m.find(';');
                const size_t slashes = m.find("//");
                if (open == std::string::npos || (semicolon != std::string::npos && semicolon < open) || (slashes != std::string::npos && slashes < open))
                {
                    kept += text.substr(at);
                    break;
                }
                kept += text.substr(at, open);
                inBlock = true;
                at += open + 2;
            }
            if (!note.empty() || inBlock || kept.size() != text.size())
            {
                text = kept;
                while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
                    text.pop_back();
                if (!note.empty())
                {
                    line.comment = note;
                    line.hasComment = true;
                }
            }
        }

        // The comment: ';' or "//" outside strings
        const std::string m = Mask(text);
        size_t comment = m.find(';');
        const size_t slashes = m.find("//");
        if (slashes != std::string::npos && (comment == std::string::npos || slashes < comment))
        {
            line.comment = text.substr(slashes + 2);
            line.hasComment = true;
            text.resize(slashes);
        }
        else if (comment != std::string::npos)
        {
            line.comment = text.substr(comment + 1);
            line.hasComment = true;
            text.resize(comment);
        }

        // The label: whatever starts in column 0
        size_t k = 0;
        if (!text.empty() && text[0] != ' ' && text[0] != '\t')
        {
            while (k < text.size() && text[k] != ' ' && text[k] != '\t' && text[k] != ':' && text[k] != '=')
                ++k;
            line.label = text.substr(0, k);
            if (k < text.size() && text[k] == ':')
                ++k;
        }
        std::string rest = Trim(std::string_view(text).substr(k));
        {
            // DEFINE name value: the value put in where the name stands as a word in the lines below (the DEFINE
            // line stays: IFDEF asks for it)
            const size_t blank = rest.find_first_of(" \t");
            std::string word = z80::Upper(rest.substr(0, blank));
            if (word.size() > 1 && word[0] == '.')
                word.erase(0, 1);
            const std::string after = blank == std::string::npos ? std::string() : Trim(std::string_view(rest).substr(blank));
            if (word == "DEFINE" || word == "DEFINE+")
            {
                const size_t nameEnd = after.find_first_of(" \t");
                const std::string name = after.substr(0, nameEnd);
                const std::string value = nameEnd == std::string::npos ? std::string() : Trim(std::string_view(after).substr(nameEnd));
                if (!value.empty())
                    defines[name] = Substitute(value, defines);
                else
                    defines.erase(name);
            }
            else if (word == "UNDEFINE")
                defines.erase(after);
            else if (word != "IFDEF" && word != "IFNDEF" && !defines.empty())
                rest = Substitute(rest, defines);
        }
        Context c{number, result.diagnostics, macros, z80n, &macroParams};
        std::vector<std::string> partTexts;   // each statement's text
        if (!rest.empty())
        {
            // "label=expr" and "label = expr": one statement, the ':' inside belongs to nothing else
            const std::vector<std::string> parts = rest[0] == '=' ? std::vector<std::string>{rest} : Split(rest, ':');
            std::vector<std::string> split;
            for (const std::string& part : parts)
                for (const std::string& group : MultiArgGroups(part))
                    split.push_back(group);
            for (const std::string& part : split)
                if (!part.empty())
                {
                    line.statements.push_back(ParseStatement(part, line.label, c));
                    partTexts.push_back(part);
                    const Statement& parsed = line.statements.back();
                    if (parsed.kind == Statement::Kind::Directive && z80::EnablesZ80n(parsed.text))
                        z80n = true;
                    if (parsed.kind == Statement::Kind::Directive && parsed.directive == ir::DirectiveKind::Macro)
                    {
                        macroParams.clear();
                        for (const std::string& param : parsed.params)
                            macroParams.push_back(Trim(param));
                    }
                    else if (parsed.kind == Statement::Kind::Directive && parsed.directive == ir::DirectiveKind::EndMacro)
                        macroParams.clear();
                }
        }
        for (Statement& st : line.statements)
            BinaryOutsideJumps(st);
        // A macro on one line (NAME MACRO p : PUSH HL : ... : ENDM): its lines one by one, as written over several
        for (size_t macroAt = 0; macroAt + 1 < line.statements.size(); ++macroAt)
            if (line.statements[macroAt].kind == Statement::Kind::Directive && line.statements[macroAt].directive == ir::DirectiveKind::Macro)
            {
                ir::Line head = line;
                head.statements.assign(line.statements.begin(), line.statements.begin() + static_cast<std::ptrdiff_t>(macroAt + 1));
                head.hasComment = false;
                head.comment.clear();
                std::string headText = line.label;
                for (size_t i = 0; i <= macroAt; ++i)
                    headText += (i ? " : " : "\t") + partTexts[i];
                head.ownText = headText;
                result.program.lines.push_back(std::move(head));
                for (size_t i = macroAt + 1; i < line.statements.size(); ++i)
                {
                    ir::Line part;
                    part.sourceLine = number;
                    part.statements.push_back(line.statements[i]);
                    part.ownText = "\t" + partTexts[i];
                    if (i + 1 == line.statements.size())
                    {
                        part.comment = line.comment;
                        part.hasComment = line.hasComment;
                    }
                    result.program.lines.push_back(std::move(part));
                }
                line.statements.clear();
                line.label.clear();
                break;
            }
        if (line.statements.empty() && line.label.empty() && !result.program.lines.empty() && result.program.lines.back().sourceLine == number)
            continue;
        // label+N: the label is $+N (self-modifying code names the operand of its instruction); label+*: the
        // instruction's value field (sjasmplus' smart SMC). A line of its own first, the label assigned so
        if (const size_t plus = line.label.find('+'); plus != std::string::npos && plus > 0 && plus + 2 == line.label.size() && line.label.back() == '*')
        {
            const std::optional<int64_t> field = line.statements.empty() ? std::nullopt : ValueField(line.statements[0]);
            if (!field)
                result.diagnostics.push_back({Severity::Error, number, 0, line.label + ": the instruction has no value field (sjasmplus: no significant target)"});
            line.label = line.label.substr(0, plus) + "+" + std::to_string(field.value_or(0));
        }
        if (const size_t plus = line.label.find('+'); plus != std::string::npos && plus > 0 && plus + 1 < line.label.size() &&
                                                      line.label.find_first_not_of("0123456789", plus + 1) == std::string::npos)
        {
            ir::Line named;
            named.sourceLine = number;
            named.label = line.label.substr(0, plus);
            Statement assign;
            assign.kind = Statement::Kind::Directive;
            assign.directive = ir::DirectiveKind::Equ;
            assign.args.push_back(Expr::Binary(ir::Op::Add, Expr::Make(Expr::Kind::Current), Expr::Number(std::stoll(line.label.substr(plus + 1)))));
            named.statements.push_back(std::move(assign));
            named.ownText = named.label + "\tEQU $+" + line.label.substr(plus + 1);
            result.program.lines.push_back(std::move(named));
            line.label.clear();
            line.ownText = "\t" + rest;
        }
        result.program.lines.push_back(std::move(line));
    }
    result.program.z80n = z80n;
    return result;
}
}  // namespace unrealasm::dialects
