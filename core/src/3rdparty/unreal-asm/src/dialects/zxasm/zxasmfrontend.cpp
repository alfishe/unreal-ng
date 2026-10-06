#include "dialects/zxasm/zxasmfrontend.h"

#include <cctype>
#include <map>
#include <optional>
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

bool IsLabelChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '?' || c == '@' || c == '$';
}

bool IsQuote(const std::string& t, size_t k)
{
    // ' after a letter or digit belongs to the word (AF')
    if (t[k] == '\'')
        return k == 0 || !std::isalnum(static_cast<unsigned char>(t[k - 1]));
    return t[k] == '"' || t[k] == '~';
}

std::string Trim(std::string s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    return s;
}

Expr Grouped(Expr e)
{
    Expr g = Expr::Make(Expr::Kind::Group);
    g.args.push_back(std::move(e));
    return g;
}

Expr Hex(int64_t v, int digits)
{
    return Expr::Number(v, ir::NumberSpelling::Hex, digits);
}

/// Expressions: left to right without priorities ("10+20*2 is 60"), parentheses first; postfix functions act on the
/// operand right before them (checked in ZAsm 3.15: #1200+#34.b = #1234)
struct ExpressionParser
{
    std::string_view t;
    size_t i = 0;

    void Blanks()
    {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t'))
            ++i;
    }
    char Peek() const { return i < t.size() ? t[i] : '\0'; }

    static bool HexDigit(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

    Expr Number()
    {
        // 123, 12h / 0ffh, 101b
        size_t j = i;
        while (j < t.size() && std::isalnum(static_cast<unsigned char>(t[j])))
            ++j;
        const std::string word(t.substr(i, j - i));
        i = j;
        const char last = static_cast<char>(std::tolower(static_cast<unsigned char>(word.back())));
        const std::string body = word.substr(0, word.size() - 1);
        auto all = [&](const std::string& s, auto pred) {
            if (s.empty())
                return false;
            for (const char c : s)
                if (!pred(c))
                    return false;
            return true;
        };
        if (last == 'h' && all(body, HexDigit))
            return Hex(std::stoll(body, nullptr, 16) & 0xFFFF, static_cast<int>(body.size()));
        if (last == 'b' && all(body, [](char c) { return c == '0' || c == '1'; }))
            return Expr::Number(std::stoll(body, nullptr, 2), ir::NumberSpelling::Binary, static_cast<int>(body.size()));
        if (!all(word, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
            throw Failure{"not a number: " + word};
        return Expr::Number(std::stoll(word) & 0xFFFF, ir::NumberSpelling::Decimal, static_cast<int>(word.size()));
    }

    Expr Term()
    {
        Blanks();
        const char c = Peek();
        Expr e;
        if (c == '-' || c == '+')
        {
            ++i;
            Expr inner = Term();
            return c == '-' ? Expr::Unary(Op::Negate, std::move(inner)) : inner;
        }
        if (c == '(')
        {
            ++i;
            Expr inner = Sequence();
            Blanks();
            if (Peek() != ')')
                throw Failure{"( without )"};
            ++i;
            e = Grouped(std::move(inner));
        }
        else if (c == '#' || c == '%')
        {
            const bool hex = c == '#';
            size_t j = ++i;
            while (j < t.size() && (hex ? HexDigit(t[j]) : (t[j] == '0' || t[j] == '1')))
                ++j;
            if (j == i)
                throw Failure{std::string(1, c) + " without digits"};
            e = Expr::Number(std::stoll(std::string(t.substr(i, j - i)), nullptr, hex ? 16 : 2) & 0xFFFF,
                             hex ? ir::NumberSpelling::Hex : ir::NumberSpelling::Binary, static_cast<int>(j - i));
            i = j;
        }
        else if (std::isdigit(static_cast<unsigned char>(c)))
            e = Number();
        else if (c == '"' || c == '\'')
        {
            const size_t close = t.find(c, i + 1);
            if (close == std::string_view::npos)
                throw Failure{"quote without its end"};
            const std::string chars(t.substr(i + 1, close - i - 1));
            i = close + 1;
            int64_t value = 0;
            for (const char ch : chars)
                value = ((value << 8) | static_cast<unsigned char>(ch)) & 0xFFFF;
            e = Expr::Number(value, ir::NumberSpelling::Character, static_cast<int>(chars.size()));
            e.text = chars;
        }
        else if (c == '$' && !(i + 1 < t.size() && IsLabelChar(t[i + 1])))
        {
            ++i;
            e = Expr::Make(Expr::Kind::Current);
        }
        else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '?' || c == '@')
        {
            size_t j = i;
            while (j < t.size() && IsLabelChar(t[j]))
                ++j;
            e = Expr::Symbol(std::string(t.substr(i, j - i)));
            i = j;
        }
        else
            throw Failure{std::string("unexpected '") + c + "' in an expression"};
        // Postfix functions: .b .h .e .l .r .L .R .c .n .s .m
        while (i + 1 < t.size() && t[i] == '.' && std::string_view("bherlLRcnsm").find(t[i + 1]) != std::string_view::npos &&
               !(i + 2 < t.size() && IsLabelChar(t[i + 2])))
        {
            const char f = t[i + 1];
            i += 2;
            switch (f)
            {
                case 'b': e = Grouped(Expr::Binary(Op::And, std::move(e), Hex(0xFF, 2))); break;
                case 'h': e = Expr::Unary(Op::High, std::move(e)); break;
                case 'e': e = Expr::Unary(Op::SwapBytes, std::move(e)); break;
                case 'l':
                case 'r':
                {
                    // 8-bit rotation of the low byte
                    const Expr low = Grouped(Expr::Binary(Op::And, e, Hex(0xFF, 2)));
                    const bool left = f == 'l';
                    Expr rotated = Expr::Binary(Op::Or, Expr::Binary(left ? Op::Shl : Op::Shr, low, Expr::Number(1)),
                                                Expr::Binary(left ? Op::Shr : Op::Shl, low, Expr::Number(7)));
                    e = Grouped(Expr::Binary(Op::And, Grouped(std::move(rotated)), Hex(0xFF, 2)));
                    break;
                }
                case 'L': e = Grouped(Expr::Binary(Op::RotateLeft16, std::move(e), Expr::Number(1))); break;
                case 'R': e = Grouped(Expr::Binary(Op::RotateRight16, std::move(e), Expr::Number(1))); break;
                case 'c': e = Grouped(Expr::Binary(Op::Xor, std::move(e), Hex(0xFFFF, 4))); break;
                case 'n': e = Grouped(Expr::Binary(Op::And, Expr::Unary(Op::Negate, std::move(e)), Hex(0xFFFF, 4))); break;
                case 's': e = Grouped(Expr::Binary(Op::Or, std::move(e), Hex(0x80, 2))); break;
                case 'm':
                {
                    Expr m = Expr::Make(Expr::Kind::Memory);
                    m.args.push_back(std::move(e));
                    e = std::move(m);
                    break;
                }
            }
        }
        return e;
    }

    Expr Sequence()
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
                case '\\': op = Op::Mod; break;
                case '&': op = Op::And; break;
                case '|': op = Op::Or; break;
                case '!': op = Op::Xor; break;
                default: return acc;
            }
            ++i;
            acc = Expr::Binary(op, std::move(acc), Term());
        }
    }

    Expr Whole()
    {
        Expr e = Sequence();
        Blanks();
        if (i != t.size())
            throw Failure{"unexpected text after an expression: " + std::string(t.substr(i))};
        return e;
    }
};

Expr ParseExpression(std::string_view text)
{
    ExpressionParser p{text};
    return p.Whole();
}

/// Splits at a separator outside quotes and parentheses
std::vector<std::string> Split(const std::string& text, char separator)
{
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    char quote = 0;
    for (size_t k = 0; k < text.size(); ++k)
    {
        const char c = text[k];
        if (quote)
        {
            if (c == quote)
                quote = 0;
        }
        else if (IsQuote(text, k))
            quote = c;
        else if (c == '(')
            ++depth;
        else if (c == ')' && depth > 0)
            --depth;
        else if (depth == 0 && c == separator)
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

/// One line taken apart: label, statements, comment
struct SourceStatementLine
{
    std::string label;
    std::vector<std::string> statements;
    std::string comment;
    bool hasComment = false;
    uint32_t number = 0;
};

SourceStatementLine SplitLine(const std::string& text, uint32_t number)
{
    SourceStatementLine out;
    out.number = number;
    std::string t = text;
    // A comment: ";" outside quotes, or the first character outside ASCII (Russian text needs no ";")
    char quote = 0;
    for (size_t k = 0; k < t.size(); ++k)
    {
        const unsigned char c = static_cast<unsigned char>(t[k]);
        if (quote)
        {
            if (t[k] == quote)
                quote = 0;
            continue;
        }
        if (IsQuote(t, k))
        {
            quote = t[k];
            continue;
        }
        if (t[k] == ';' || c >= 0x80)
        {
            out.comment = t.substr(t[k] == ';' ? k + 1 : k);
            out.hasComment = true;
            t.erase(k);
            break;
        }
    }
    size_t i = 0;
    if (!t.empty() && t[0] != ' ' && t[0] != '\t')
    {
        while (i < t.size() && t[i] != ' ' && t[i] != '\t' && t[i] != ':')
            ++i;
        out.label = t.substr(0, i);
        if (i < t.size() && t[i] == ':')
            ++i;   // Label: is marked for MAKELAB; the label is Label
    }
    for (std::string& s : Split(t.substr(i), ':'))
        if (!s.empty())
            out.statements.push_back(s);
    return out;
}

std::pair<std::string, std::string> Command(const std::string& statement)
{
    size_t j = 0;
    while (j < statement.size() && statement[j] != ' ' && statement[j] != '\t')
        ++j;
    return {statement.substr(0, j), Trim(statement.substr(j))};
}

bool WhollyParenthesized(const std::string& text)
{
    if (text.size() < 2 || text.front() != '(' || text.back() != ')')
        return false;
    int depth = 0;
    for (size_t k = 0; k < text.size(); ++k)
    {
        if (text[k] == '(')
            ++depth;
        else if (text[k] == ')' && --depth == 0)
            return k + 1 == text.size();
    }
    return false;
}

Operand ParseOperand(const std::string& text, bool conditionAllowed, bool port)
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
    if (z80::IsRegister(reg) && reg != "f")
    {
        o.kind = Operand::Kind::Register;
        o.text = reg;
        return o;
    }
    if (WhollyParenthesized(text))
    {
        const std::string inner = Trim(text.substr(1, text.size() - 2));
        const std::string innerLower = z80::Lower(inner);
        if (port && innerLower == "bc")   // IN A,(BC) = IN A,(C)
        {
            o.kind = Operand::Kind::Indirect;
            o.text = "c";
            return o;
        }
        if (innerLower == "hl" || innerLower == "bc" || innerLower == "de" || innerLower == "sp" || innerLower == "c" || innerLower == "ix" ||
            innerLower == "iy")
        {
            o.kind = Operand::Kind::Indirect;
            o.text = innerLower;
            return o;
        }
        if ((innerLower.rfind("ix", 0) == 0 || innerLower.rfind("iy", 0) == 0) && inner.size() > 2 && (inner[2] == '+' || inner[2] == '-'))
        {
            o.kind = Operand::Kind::Indexed;
            o.text = innerLower.substr(0, 2);
            o.expr = ParseExpression(std::string_view(inner).substr(2));
            // The offset is a byte: (IY+#FE) is (IY-2)
            if (o.expr.kind == Expr::Kind::Number && o.expr.value >= 0x80 && o.expr.value <= 0xFF)
                o.expr = Expr::Unary(Op::Negate, Expr::Number(0x100 - o.expr.value, o.expr.spelling, o.expr.digits));
            return o;
        }
        o.kind = Operand::Kind::Memory;
        o.expr = ParseExpression(inner);
        return o;
    }
    o.kind = Operand::Kind::Immediate;
    o.expr = ParseExpression(text);
    return o;
}

Statement Directive(ir::DirectiveKind kind)
{
    Statement s;
    s.kind = Statement::Kind::Directive;
    s.directive = kind;
    return s;
}

/// A quoted operand standing alone: its characters and its quote
bool QuotedText(const std::string& op, std::string& chars, char& quote)
{
    if (op.size() < 2 || (op.front() != '"' && op.front() != '\'' && op.front() != '~') || op.back() != op.front() ||
        op.find(op.front(), 1) != op.size() - 1)
        return false;
    quote = op.front();
    chars = op.substr(1, op.size() - 2);
    return true;
}

/// A file name of INCLUDE / INSERT / SAVEOBJ. ZAsm shows a TR-DOS name with an extension made of the type letter and
/// the two bytes of the catalog's start field ("ovlib.asm" is type a, "A315.lbl" type l): INCLUDE names the project
/// file without it unless a project file has the whole name ("a2.5_1"); INSERT / SAVEOBJ keep the name as written
/// (zxasm convert finds "FONT.fn1" as FONT of type f, or a file of that whole name); the drive goes
std::string FileName(const std::string& op, bool source, const std::set<std::string>* projectNames = nullptr)
{
    std::string chars;
    char quote = 0;
    std::string name = QuotedText(op, chars, quote) ? chars : op;
    if (name.size() > 2 && name[1] == ':')
        name = name.substr(2);
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos && dot > 0)
    {
        const std::string ext = name.substr(dot + 1);
        const bool typed = ext.size() == 3 || (ext.size() == 1 && std::isalpha(static_cast<unsigned char>(ext[0])));
        // A TR-DOS name may hold a dot itself ("a2.5_1"): a project file of the whole name wins
        const bool whole = projectNames && projectNames->count(name) && !projectNames->count(name.substr(0, dot));
        if (source && typed && !whole)
            name = name.substr(0, dot);
    }
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    return name;
}

/// The value of a constant expression with the EQU values known so far (a REPT count)
std::optional<int64_t> Evaluate(const Expr& e, const std::map<std::string, int64_t>& values)
{
    switch (e.kind)
    {
        case Expr::Kind::Number: return e.value;
        case Expr::Kind::Symbol:
        {
            const auto found = values.find(e.text);
            return found == values.end() ? std::nullopt : std::optional<int64_t>(found->second);
        }
        case Expr::Kind::Group: return Evaluate(e.args[0], values);
        case Expr::Kind::Unary:
        {
            const auto a = Evaluate(e.args[0], values);
            if (!a)
                return std::nullopt;
            switch (e.op)
            {
                case Op::Negate: return (-*a) & 0xFFFF;
                case Op::High: return (*a >> 8) & 0xFF;
                case Op::Low: return *a & 0xFF;
                default: return std::nullopt;
            }
        }
        case Expr::Kind::Binary:
        {
            const auto a = Evaluate(e.args[0], values), b = Evaluate(e.args[1], values);
            if (!a || !b)
                return std::nullopt;
            const int64_t x = *a & 0xFFFF, y = *b & 0xFFFF;
            switch (e.op)
            {
                case Op::Add: return (x + y) & 0xFFFF;
                case Op::Sub: return (x - y) & 0xFFFF;
                case Op::Mul: return (x * y) & 0xFFFF;
                case Op::Div: return y ? std::optional<int64_t>(x / y) : std::nullopt;
                case Op::Mod: return y ? std::optional<int64_t>(x % y) : std::nullopt;
                case Op::And: return x & y;
                case Op::Or: return x | y;
                case Op::Xor: return x ^ y;
                case Op::Shl: return (x << y) & 0xFFFF;
                case Op::Shr: return x >> y;
                default: return std::nullopt;
            }
        }
        default: return std::nullopt;
    }
}

const std::set<std::string> kDirectives = {
    "org", "make", "phase", "unphase", "ent", "equ", "defb", "db", "defw", "dw", "defs", "ds", "dc", "dbw", "include", "insert",
    "loadtab", "saveobj", "makelab", "if", "ifp", "ifdef", "ifndef", "ifused", "ifnused", "else", "endif", "macro", "exitm", "endm",
    "rept", "endr", "repl", "create", "chd", "loadobj", "project", "public", "enda",
};

/// jrz / jpnc / callnz / retc: an instruction written together with its condition
bool CombinedCondition(const std::string& lower, std::string& mnemonic, std::string& condition)
{
    for (const char* m : {"call", "ret", "jr", "jp"})
    {
        const std::string base = m;
        if (lower.size() > base.size() && lower.compare(0, base.size(), base) == 0 && z80::IsCondition(lower.substr(base.size())))
        {
            mnemonic = base;
            condition = lower.substr(base.size());
            return true;
        }
    }
    return false;
}

/// Macros and REPT passes are expanded before the lines are parsed: macro parameters keep the previous call's values
/// (help: FILL ,,6912,0 keeps the first two) and labels inside a macro or a REPT pass are local to it (checked)
struct Expander
{
    struct Macro
    {
        std::vector<SourceStatementLine> body;
        std::vector<std::string> last;   // the parameters of the previous call
    };
    std::map<std::string, Macro> macros;
    std::vector<SourceStatementLine> out;
    std::map<std::string, int64_t> values;   // EQU values known so far, for REPT counts
    Diagnostics& diagnostics;
    int expansions = 0;

    static std::string Lower(const std::string& s) { return z80::Lower(s); }

    /// Renames the labels the lines define (whole names, outside quotes) with `suffix`
    static void RenameLabels(std::vector<SourceStatementLine>& lines, const std::string& suffix)
    {
        std::set<std::string> defined;
        for (const SourceStatementLine& l : lines)
            if (!l.label.empty())
                defined.insert(l.label);
        if (defined.empty())
            return;
        auto rename = [&](const std::string& text) {
            std::string r;
            char quote = 0;
            for (size_t k = 0; k < text.size();)
            {
                if (quote)
                {
                    if (text[k] == quote)
                        quote = 0;
                    r.push_back(text[k++]);
                    continue;
                }
                if (IsQuote(text, k))
                {
                    quote = text[k];
                    r.push_back(text[k++]);
                    continue;
                }
                if (IsLabelChar(text[k]) && (k == 0 || !IsLabelChar(text[k - 1])))
                {
                    size_t e = k;
                    while (e < text.size() && IsLabelChar(text[e]))
                        ++e;
                    const std::string word = text.substr(k, e - k);
                    r += defined.count(word) ? word + suffix : word;
                    k = e;
                    continue;
                }
                r.push_back(text[k++]);
            }
            return r;
        };
        for (SourceStatementLine& l : lines)
        {
            if (!l.label.empty())
                l.label += suffix;
            for (std::string& s : l.statements)
                s = rename(s);
        }
    }

    /// =1 .. =n put in; a parameter glued to a command word (rept=1) gets a blank
    static std::string Substitute(const std::string& text, const std::vector<std::string>& params)
    {
        std::string r;
        for (size_t k = 0; k < text.size(); ++k)
        {
            if (text[k] == '=' && k + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[k + 1])))
            {
                size_t e = k + 1;
                while (e < text.size() && std::isdigit(static_cast<unsigned char>(text[e])))
                    ++e;
                const size_t n = static_cast<size_t>(std::stoul(text.substr(k + 1, e - k - 1)));
                if (!r.empty() && std::isalpha(static_cast<unsigned char>(r.back())))
                {
                    size_t w = r.size();
                    while (w > 0 && std::isalpha(static_cast<unsigned char>(r[w - 1])))
                        --w;
                    const std::string word = z80::Lower(r.substr(w));
                    if (w == 0 || r[w - 1] == ' ' || r[w - 1] == '\t' || r[w - 1] == ':')
                        if (kDirectives.count(word) || z80::IsMnemonic(word))
                            r.push_back(' ');
                }
                r += n >= 1 && n <= params.size() ? params[n - 1] : std::string();
                k = e - 1;
                continue;
            }
            r.push_back(text[k]);
        }
        return r;
    }

    void Emit(SourceStatementLine line, int depth)
    {
        // Statements one at a time: a macro call splits the line
        SourceStatementLine pending;
        pending.label = line.label;
        pending.number = line.number;
        auto flush = [&] {
            if (!pending.label.empty() || !pending.statements.empty())
                out.push_back(pending);
            pending = SourceStatementLine{};
            pending.number = line.number;
        };
        for (const std::string& statement : line.statements)
        {
            const auto [word, rest] = Command(statement);
            const auto macro = macros.find(word);
            if (macro == macros.end())
            {
                const std::string lower = Lower(word);
                if (lower == "equ" && !pending.label.empty())
                {
                    ExpressionParser p{rest};
                    try
                    {
                        if (const auto v = Evaluate(p.Whole(), values))
                            values[pending.label] = *v;
                    }
                    catch (const Failure&)
                    {
                    }
                }
                pending.statements.push_back(statement);
                continue;
            }
            flush();
            if (depth > 16)
            {
                diagnostics.push_back({Severity::Warning, line.number, 0, "macro " + word + " nested too deep"});
                continue;
            }
            std::vector<std::string> params = rest.empty() ? std::vector<std::string>{} : Split(rest, ',');
            const bool passed = !params.empty();
            Macro& m = macro->second;
            if (m.last.size() < params.size())
                m.last.resize(params.size());
            for (size_t k = 0; k < params.size(); ++k)
                if (!params[k].empty())
                    m.last[k] = params[k];
            std::vector<SourceStatementLine> body;
            for (const SourceStatementLine& b : m.body)
            {
                SourceStatementLine copy = b;
                copy.number = line.number;
                for (std::string& s : copy.statements)
                {
                    s = Substitute(s, m.last);
                    if (Lower(Command(s).first) == "ifp")   // IFP: a parameter was passed to this call
                        s = passed ? "if 1" : "if 0";
                }
                body.push_back(std::move(copy));
            }
            // EXITM ends the expansion there
            for (size_t k = 0; k < body.size(); ++k)
                for (size_t n = 0; n < body[k].statements.size(); ++n)
                    if (Lower(Command(body[k].statements[n]).first) == "exitm")
                    {
                        body[k].statements.resize(n);
                        body.resize(k + 1);
                        break;
                    }
            RenameLabels(body, "__M" + std::to_string(++expansions));
            out.push_back([&] {
                SourceStatementLine head;
                head.number = line.number;
                head.comment = " macro " + word + (rest.empty() ? "" : " " + rest);
                head.hasComment = true;
                return head;
            }());
            for (SourceStatementLine& b : body)
                Emit(std::move(b), depth + 1);
        }
        pending.comment = line.comment;
        pending.hasComment = line.hasComment;
        if (!pending.label.empty() || !pending.statements.empty() || pending.hasComment || line.statements.empty())
            out.push_back(pending);
    }

    /// The source with macros expanded and REPT passes with labels written out
    void Run(const std::vector<SourceStatementLine>& lines)
    {
        for (size_t k = 0; k < lines.size(); ++k)
        {
            const SourceStatementLine& l = lines[k];
            // A macro definition: NAME MACRO[:body...][:ENDM]
            if (!l.label.empty() && !l.statements.empty() && Lower(Command(l.statements[0]).first) == "macro")
            {
                Macro m;
                SourceStatementLine first;
                first.number = l.number;
                bool closed = false;
                for (size_t n = 1; n < l.statements.size() && !closed; ++n)
                {
                    if (Lower(Command(l.statements[n]).first) == "endm")
                        closed = true;
                    else
                        first.statements.push_back(l.statements[n]);
                }
                if (!first.statements.empty())
                    m.body.push_back(first);
                SourceStatementLine def;
                def.number = l.number;
                def.comment = " (macro " + l.label + ", expanded at its calls)";
                def.hasComment = true;
                out.push_back(def);
                while (!closed && ++k < lines.size())
                {
                    SourceStatementLine b = lines[k];
                    SourceStatementLine part = b;
                    part.statements.clear();
                    for (const std::string& s : b.statements)
                    {
                        if (Lower(Command(s).first) == "endm")
                        {
                            closed = true;
                            break;
                        }
                        part.statements.push_back(s);
                    }
                    if (!part.label.empty() || !part.statements.empty())
                        m.body.push_back(part);
                    SourceStatementLine shown;
                    shown.number = b.number;
                    shown.comment = " (macro) " + b.label + (b.statements.empty() ? "" : " ") + [&] {
                        std::string joined;
                        for (const std::string& s : b.statements)
                            joined += (joined.empty() ? "" : ":") + s;
                        return joined;
                    }();
                    shown.hasComment = true;
                    out.push_back(shown);
                }
                if (!closed)
                    diagnostics.push_back({Severity::Warning, l.number, 0, "macro " + l.label + " without ENDM"});
                macros[l.label] = std::move(m);
                continue;
            }
            // REPT n ... ENDR whose body defines labels: each pass gets its own (checked); the count must be known
            if (!l.statements.empty() && Lower(Command(l.statements[0]).first) == "rept")
            {
                std::vector<SourceStatementLine> body;
                SourceStatementLine first;
                first.number = l.number;
                bool closed = false;
                std::vector<std::string> after;
                for (size_t n = 1; n < l.statements.size(); ++n)
                {
                    if (closed)
                        after.push_back(l.statements[n]);
                    else if (Lower(Command(l.statements[n]).first) == "endr")
                        closed = true;
                    else
                        first.statements.push_back(l.statements[n]);
                }
                if (!first.statements.empty())
                    body.push_back(first);
                size_t end = k;
                while (!closed && ++end < lines.size())
                {
                    SourceStatementLine part = lines[end];
                    part.statements.clear();
                    for (const std::string& s : lines[end].statements)
                    {
                        if (closed)
                            after.push_back(s);
                        else if (Lower(Command(s).first) == "endr")
                            closed = true;
                        else
                            part.statements.push_back(s);
                    }
                    body.push_back(part);
                }
                bool labels = false;
                for (const SourceStatementLine& b : body)
                    labels = labels || !b.label.empty();
                std::optional<int64_t> count;
                try
                {
                    count = Evaluate(ParseExpression(Command(l.statements[0]).second), values);
                }
                catch (const Failure&)
                {
                }
                if (closed && labels && count)
                {
                    SourceStatementLine head;
                    head.number = l.number;
                    head.label = l.label;
                    head.comment = " " + l.statements[0] + " (written out: labels in each pass)";
                    head.hasComment = true;
                    Emit(head, 0);
                    for (int64_t pass = 0; pass < *count; ++pass)
                    {
                        std::vector<SourceStatementLine> copy = body;
                        RenameLabels(copy, "__R" + std::to_string(++expansions));
                        for (SourceStatementLine& c : copy)
                            Emit(std::move(c), 0);
                    }
                    if (!after.empty())
                    {
                        SourceStatementLine rest;
                        rest.number = lines[end].number;
                        rest.statements = after;
                        Emit(rest, 0);
                    }
                    k = end;
                    continue;
                }
                if (labels)
                    diagnostics.push_back({Severity::Warning, l.number, 0, "REPT with labels and a count not known when converting: labels clash"});
            }
            Emit(l, 0);
        }
    }
};

struct LineParser
{
    FrontendResult& result;
    std::set<std::string> projectNames;   // the other files of the project, as INCLUDE names them
    // PHASE nests (5 deep in ZX-ASM): UNPHASE returns to the outer PHASE where it would be now. The stack holds the
    // number of the labels that keep the outer address and the physical address at the inner PHASE (0 = the
    // outermost). Unknown where a file starts (an INCLUDE inside PHASE)
    std::vector<int> phases;
    bool phaseUnknown = true;
    int phaseLabels = 0;
    std::optional<Expr> lastOrg;

    void EndPhases(std::vector<Statement>& out)
    {
        if (phases.empty() && !phaseUnknown)
            return;
        Statement ent = Directive(ir::DirectiveKind::Ent);
        if (phases.empty())
            ent.text = "if-displaced";
        out.push_back(std::move(ent));
        phases.clear();
        phaseUnknown = false;
    }

    static ir::Line Assignment(const std::string& label, Expr value, uint32_t number)
    {
        ir::Line l;
        l.sourceLine = number;
        l.label = label;
        Statement s = Directive(ir::DirectiveKind::Defl);
        s.args.push_back(std::move(value));
        l.statements.push_back(std::move(s));
        return l;
    }

    std::vector<Operand> DataOperands(const std::vector<std::string>& ops, uint32_t number)
    {
        std::vector<Operand> out;
        for (const std::string& op : ops)
        {
            std::string chars;
            char quote = 0;
            if (QuotedText(op, chars, quote) && (chars.size() != 1 || quote == '~'))
            {
                if (quote == '~')
                    result.diagnostics.push_back({Severity::Warning, number, 0, "~text~ is translated by the LOADTAB table: kept as written"});
                Operand o;
                o.kind = Operand::Kind::String;
                o.text = chars;
                out.push_back(std::move(o));
                continue;
            }
            Operand o;
            o.kind = Operand::Kind::Immediate;
            o.expr = ParseExpression(op);
            out.push_back(std::move(o));
        }
        return out;
    }

    void ParseStatement(const std::string& statement, uint32_t number, const std::string& label, std::vector<ir::Line>& before,
                        std::vector<Statement>& out)
    {
        const auto [word, rest] = Command(statement);
        const std::string lower = z80::Lower(word);
        const std::vector<std::string> ops = rest.empty() ? std::vector<std::string>{} : Split(rest, ',');
        auto args = [&] {
            std::vector<Expr> a;
            for (const std::string& op : ops)
                a.push_back(ParseExpression(op));
            return a;
        };
        if (lower == "org")
        {
            std::vector<Expr> a = args();
            if (a.empty())
                throw Failure{"ORG without an address"};
            EndPhases(out);
            Statement s = Directive(ir::DirectiveKind::Org);
            s.args = a;
            lastOrg = a[0];
            out.push_back(std::move(s));
            return;
        }
        if (lower == "phase")
        {
            Statement disp = Directive(ir::DirectiveKind::Disp);
            disp.args = args();
            if (!phases.empty())
            {
                // Nested: keep the outer address, end the displacement (sjasmplus does not nest DISP), keep the address
                // where the code is put ($ once no displacement is active) for the UNPHASE back to the outer one
                const int n = ++phaseLabels;
                before.push_back(Assignment("__UNREALASM_PH" + std::to_string(n), Expr::Make(Expr::Kind::Current), number));
                ir::Line ent;
                ent.sourceLine = number;
                ent.statements.push_back(Directive(ir::DirectiveKind::Ent));
                before.push_back(std::move(ent));
                before.push_back(Assignment("__UNREALASM_PP" + std::to_string(n), Expr::Make(Expr::Kind::Current), number));
                phases.push_back(n);
            }
            else
                phases.push_back(0);
            phaseUnknown = false;
            out.push_back(std::move(disp));
            return;
        }
        if (lower == "unphase")
        {
            if (phases.size() > 1)
            {
                const std::string n = std::to_string(phases.back());
                phases.pop_back();
                // After ENT, $ is where the code is put: the outer address goes on by what was put since
                out.push_back(Directive(ir::DirectiveKind::Ent));
                Statement disp = Directive(ir::DirectiveKind::Disp);
                disp.args.push_back(Expr::Binary(Op::Add, Expr::Symbol("__UNREALASM_PH" + n),
                                                 Grouped(Expr::Binary(Op::Sub, Expr::Make(Expr::Kind::Current), Expr::Symbol("__UNREALASM_PP" + n)))));
                out.push_back(std::move(disp));
                return;
            }
            EndPhases(out);
            return;
        }
        if (lower == "equ")
        {
            Statement s = Directive(ir::DirectiveKind::Equ);
            s.args.push_back(ParseExpression(rest));
            out.push_back(std::move(s));
            return;
        }
        if (lower == "db" || lower == "defb")
        {
            Statement s = Directive(ir::DirectiveKind::Db);
            s.operands = DataOperands(ops, number);
            out.push_back(std::move(s));
            return;
        }
        if (lower == "dw" || lower == "defw")
        {
            Statement s = Directive(ir::DirectiveKind::Dw);
            for (const std::string& op : ops)
            {
                Operand o;
                o.kind = Operand::Kind::Immediate;
                o.expr = ParseExpression(op);
                s.operands.push_back(std::move(o));
            }
            out.push_back(std::move(s));
            return;
        }
        if (lower == "dbw")
        {
            // DBW n,nn = DEFB n : DEFW nn (3.3)
            if (ops.size() != 2)
                throw Failure{"DBW takes a byte and a word"};
            Statement b = Directive(ir::DirectiveKind::Db), w = Directive(ir::DirectiveKind::Dw);
            b.operands = DataOperands({ops[0]}, number);
            w.operands = DataOperands({ops[1]}, number);
            out.push_back(std::move(b));
            out.push_back(std::move(w));
            return;
        }
        if (lower == "dc")
        {
            // Texts with bit 7 set on the last character of each (help: DC "RND" = DB "RN","D"+#80; checked)
            Statement s = Directive(ir::DirectiveKind::Db);
            for (const std::string& op : ops)
            {
                std::string chars;
                char quote = 0;
                if (!QuotedText(op, chars, quote) || chars.empty())
                    throw Failure{"DC takes texts"};
                if (chars.size() > 1)
                {
                    Operand o;
                    o.kind = Operand::Kind::String;
                    o.text = chars.substr(0, chars.size() - 1);
                    s.operands.push_back(std::move(o));
                }
                Operand last;
                last.kind = Operand::Kind::Immediate;
                last.expr = Hex(static_cast<unsigned char>(chars.back()) | 0x80, 2);
                s.operands.push_back(std::move(last));
            }
            out.push_back(std::move(s));
            return;
        }
        if (lower == "ds" || lower == "defs")
        {
            // DS n[,fill...]: the fill sequence n times (DS 3,1,2 is six bytes, checked)
            if (ops.empty())
                throw Failure{"DS without a count"};
            Statement s = Directive(ir::DirectiveKind::Ds);
            s.args.push_back(ParseExpression(ops[0]));
            if (ops.size() > 1)
                s.operands = DataOperands(std::vector<std::string>(ops.begin() + 1, ops.end()), number);
            out.push_back(std::move(s));
            return;
        }
        if (lower == "include" || lower == "insert")
        {
            // Several names; INSERT copies the file's bytes (no sector tail, checked)
            for (const std::string& op : ops)
            {
                Statement s = Directive(lower == "include" ? ir::DirectiveKind::Include : ir::DirectiveKind::Incbin);
                s.text = FileName(op, lower == "include", &projectNames);
                out.push_back(std::move(s));
            }
            return;
        }
        if (lower == "saveobj")
        {
            // SAVEOBJ "file"[,start[,length]]: start defaults to the last ORG, length to $ - start
            if (ops.empty())
                throw Failure{"SAVEOBJ without a file"};
            Statement s = Directive(ir::DirectiveKind::SaveBinary);
            s.text = FileName(ops[0], false);
            Expr start = ops.size() > 1 && !ops[1].empty() ? ParseExpression(ops[1]) : lastOrg ? *lastOrg : Expr::Number(0);
            Expr length = ops.size() > 2 && !ops[2].empty() ? ParseExpression(ops[2])
                                                            : Expr::Binary(Op::Sub, Expr::Make(Expr::Kind::Current), Grouped(start));
            s.args = {start, length};
            out.push_back(std::move(s));
            return;
        }
        if (lower == "if")
        {
            Statement s = Directive(ir::DirectiveKind::If);
            s.args.push_back(ParseExpression(rest));
            out.push_back(std::move(s));
            return;
        }
        if (lower == "ifdef" || lower == "ifndef")
        {
            Statement s = Directive(ir::DirectiveKind::If);
            Expr exists = Expr::Unary(Op::Exists, Expr::Symbol(rest));
            s.args.push_back(lower == "ifdef" ? exists : Expr::Unary(Op::LogicalNot, exists));
            out.push_back(std::move(s));
            return;
        }
        if (lower == "ifused" || lower == "ifnused")
        {
            Statement s = Directive(ir::DirectiveKind::IfUsed);
            s.text = rest;
            if (lower == "ifnused")
                s.params = {"not"};
            out.push_back(std::move(s));
            return;
        }
        if (lower == "else" || lower == "endif")
        {
            out.push_back(Directive(lower == "else" ? ir::DirectiveKind::Else : ir::DirectiveKind::EndIf));
            return;
        }
        if (lower == "rept")
        {
            Statement s = Directive(ir::DirectiveKind::Repeat);
            s.args.push_back(ParseExpression(rest));
            out.push_back(std::move(s));
            return;
        }
        if (lower == "endr")
        {
            out.push_back(Directive(ir::DirectiveKind::EndRepeat));
            return;
        }
        if (lower == "ent" || lower == "create")
        {
            // ENT: the run address after assembling; CREATE: room for $labels. Neither makes code
            Statement s = Directive(ir::DirectiveKind::Other);
            s.text = "@comment " + word + " " + rest;
            out.push_back(std::move(s));
            return;
        }
        if (kDirectives.count(lower))
        {
            Statement s = Directive(ir::DirectiveKind::Other);
            s.text = word + (rest.empty() ? "" : " " + rest);
            result.diagnostics.push_back({Severity::Warning, number, 0, "ZX-ASM directive " + word + " kept as text"});
            out.push_back(std::move(s));
            return;
        }
        (void)label;

        std::string mnemonic = lower, condition;
        std::vector<std::string> parts = ops;
        if (mnemonic == "exa")
            mnemonic = "ex", parts = {"af", "af'"};
        else if (!z80::IsMnemonic(mnemonic) && CombinedCondition(lower, mnemonic, condition))
            parts.insert(parts.begin(), condition);
        if (!z80::IsMnemonic(mnemonic))
            throw Failure{"unknown command " + word};
        const bool port = mnemonic == "in" || mnemonic == "out";
        auto make = [&](std::vector<std::string> texts) {
            Statement s;
            s.kind = Statement::Kind::Instruction;
            s.mnemonic = mnemonic;
            for (size_t k = 0; k < texts.size(); ++k)
            {
                const bool cond = z80::TakesCondition(mnemonic) && k == 0 && (texts.size() >= 2 || mnemonic == "ret");
                s.operands.push_back(ParseOperand(texts[k], cond, port));
            }
            return s;
        };
        if ((mnemonic == "push" || mnemonic == "pop" || mnemonic == "inc" || mnemonic == "dec") && parts.size() > 1)
        {
            for (const std::string& p : parts)   // PUSH AF,BC / INC A,B,BC: one instruction each
                out.push_back(make({p}));
            return;
        }
        out.push_back(make(parts));
    }

    void ParseLine(const SourceStatementLine& source, const std::string& original)
    {
        ir::Line line;
        line.sourceLine = source.number;
        line.label = source.label;
        line.comment = source.comment;
        line.hasComment = source.hasComment;
        std::vector<ir::Line> before;
        try
        {
            // A label starts with a letter, "_" or "$" (an incremental label); anything else in column 0 is no source
            // (program documentation typed in the editor: "-  item")
            if (!line.label.empty())
            {
                const char c = line.label[0];
                if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '?' || c == '@'))
                    throw Failure{"not a label: " + line.label};
                for (const char ch : line.label)
                    if (!IsLabelChar(ch) && ch != '.')
                        throw Failure{"not a label: " + line.label};
            }
            std::vector<Statement> out;
            for (size_t k = 0; k < source.statements.size(); ++k)
            {
                const std::string& statement = source.statements[k];
                const auto [word, rest] = Command(statement);
                if (z80::Lower(word) == "repl")
                {
                    // REPL n: the rest of the line n times
                    Statement rept = Directive(ir::DirectiveKind::Repeat);
                    rept.args.push_back(ParseExpression(rest));
                    out.push_back(std::move(rept));
                    for (size_t n = k + 1; n < source.statements.size(); ++n)
                        ParseStatement(source.statements[n], source.number, line.label, before, out);
                    out.push_back(Directive(ir::DirectiveKind::EndRepeat));
                    break;
                }
                ParseStatement(statement, source.number, line.label, before, out);
            }
            line.statements = std::move(out);
        }
        catch (const Failure& f)
        {
            before.clear();
            Statement raw;
            raw.kind = Statement::Kind::Raw;
            raw.text = original;
            line.statements.assign(1, raw);
            line.label.clear();
            line.hasComment = false;
            result.diagnostics.push_back({Severity::Warning, source.number, 0, "not parsed (" + f.reason + "): kept as text"});
        }
        catch (const std::exception&)
        {
            before.clear();
            Statement raw;
            raw.kind = Statement::Kind::Raw;
            raw.text = original;
            line.statements.assign(1, raw);
            line.label.clear();
            line.hasComment = false;
            result.diagnostics.push_back({Severity::Warning, source.number, 0, "not parsed (a number out of range): kept as text"});
        }
        for (ir::Line& b : before)
            result.program.lines.push_back(std::move(b));
        // ENT / CREATE: comments only
        for (auto it = line.statements.begin(); it != line.statements.end();)
        {
            if (it->kind == Statement::Kind::Directive && it->directive == ir::DirectiveKind::Other && it->text.rfind("@comment ", 0) == 0)
            {
                line.comment = " " + it->text.substr(9) + (line.hasComment ? " ;" + line.comment : std::string());
                line.hasComment = true;
                it = line.statements.erase(it);
            }
            else
                ++it;
        }
        result.program.lines.push_back(std::move(line));
    }
};
}  // namespace

FrontendResult ZxasmFrontend::Parse(const SourceDocument& source) const
{
    return ParseInProject(source, {});
}

FrontendResult ZxasmFrontend::ParseInProject(const SourceDocument& source, const std::vector<const SourceDocument*>& project) const
{
    FrontendResult result;
    result.program.dialect = "zxasm";
    result.program.expressionBits = 16;          // 16-bit words: (0-1)/2 = #7FFF (checked)
    result.program.unsignedArithmetic = true;
    result.program.displacementAcrossFiles = true;

    auto split = [](const SourceDocument& document) {
        std::vector<SourceStatementLine> lines;
        uint32_t number = 0;
        for (const SourceLine& l : document.lines)
            lines.push_back(SplitLine(l.text, ++number));
        return lines;
    };
    Expander expander{{}, {}, {}, result.diagnostics};
    // The macros the other files of the project define (a definitions file the sources INCLUDE); a definition in this
    // file replaces one of the same name where it stands
    for (const SourceDocument* other : project)
    {
        if (other->dialect != source.dialect)
            continue;
        Diagnostics ignored;
        Expander scan{{}, {}, {}, ignored};
        scan.Run(split(*other));
        for (auto& [name, macro] : scan.macros)
            expander.macros.emplace(name, std::move(macro));
    }
    expander.Run(split(source));

    LineParser parser{result, {}, {}, true, 0, std::nullopt};
    for (const SourceDocument* other : project)
        parser.projectNames.insert(other->name);
    for (const SourceStatementLine& l : expander.out)
    {
        std::string original = l.label;
        for (size_t k = 0; k < l.statements.size(); ++k)
            original += (k ? ":" : (original.empty() ? "        " : " ")) + l.statements[k];
        parser.ParseLine(l, original);
    }
    return result;
}
}  // namespace unrealasm::dialects
