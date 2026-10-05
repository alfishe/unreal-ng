#include "dialects/sjasmplus/sjasmplusfrontend.h"

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
            switch (e)
            {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '0': out.push_back('\0'); break;
                case 'a': out.push_back('\a'); break;
                case 'b': out.push_back('\b'); break;
                case 'e': out.push_back('\x1B'); break;
                case 'f': out.push_back('\f'); break;
                case 'v': out.push_back('\v'); break;
                default: out.push_back(e); break;   // \\ \" \' and any other character as itself
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
            Expr inner = Parse(0);
            Blanks();
            if (Peek() != '}')
                throw Failure{"{ without }"};
            ++i;
            return Wrap(Expr::Kind::Memory, std::move(inner));
        }
        if (c == '$')
        {
            if (Peek(1) == '$')
            {
                if (Peek(2) == '$' || IsLabelChar(Peek(2)))
                    throw Failure{"$$$ / $$label have no counterpart"};
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
        if (w == "abs" || w == "norel" || w == "sizeof")
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

    Expr Parse(std::string_view text) const { return ParseExpression(text, line, diagnostics); }

    Operand InstructionOperand(const std::string& text, bool conditionAllowed, bool flagAllowed) const
    {
        Operand o;
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
            const std::string innerLower = z80::Lower(inner);
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
    {"INCLUDE", ir::DirectiveKind::Include},   {"INCBIN", ir::DirectiveKind::Incbin},    {"IF", ir::DirectiveKind::If},
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
    "RELOCATE_END", "RELOCATE_TABLE", "INCHOB", "INCTRD", "INSERT", "BINARY",   "DG",       "DEFG",     "DH",         "DEFH",
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

    const std::string mnemonic = z80::Lower(word);
    if (z80::IsMnemonic(mnemonic))
    {
        s.kind = Statement::Kind::Instruction;
        s.mnemonic = mnemonic;
        const std::vector<std::string> ops = Split(rest, ',');
        for (size_t k = 0; k < ops.size(); ++k)
        {
            const bool condition = k == 0 && z80::TakesCondition(mnemonic) && (ops.size() > 1 || mnemonic == "ret");
            s.operands.push_back(c.InstructionOperand(ops[k], condition, mnemonic == "in" || mnemonic == "out"));
        }
        return s;
    }

    // Anything else in the command place is a macro call (the macro may come from another file of the project)
    s.kind = Statement::Kind::MacroCall;
    s.mnemonic = word;
    s.params = Split(rest, ',');
    return s;
}
}  // namespace

FrontendResult SjasmplusFrontend::Parse(const SourceDocument& source) const
{
    FrontendResult result;
    result.program.dialect = "sjasmplus";
    std::set<std::string> macros;
    uint32_t number = 0;
    for (const SourceLine& sourceLine : source.lines)
    {
        ++number;
        ir::Line line;
        line.sourceLine = number;
        std::string text = sourceLine.text;

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
        const std::string rest = Trim(std::string_view(text).substr(k));
        Context c{number, result.diagnostics, macros};
        if (!rest.empty())
        {
            // "label=expr" and "label = expr": one statement, the ':' inside belongs to nothing else
            const std::vector<std::string> parts = rest[0] == '=' ? std::vector<std::string>{rest} : Split(rest, ':');
            for (const std::string& part : parts)
                if (!part.empty())
                    line.statements.push_back(ParseStatement(part, line.label, c));
        }
        result.program.lines.push_back(std::move(line));
    }
    return result;
}
}  // namespace unrealasm::dialects
