#include "dialects/common/textexpr.h"

#include <algorithm>
#include <cctype>

#include "dialects/common/z80.h"

namespace unrealasm::dialects::text
{
using ir::Expr;
using ir::Op;
using ir::Operand;

bool IsNameStart(char c, const ExprRules& r)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || (r.dotInNames && c == '.') || (r.atInNames && c == '@') ||
           (r.questionInNames && c == '?');
}

bool IsNameChar(char c, const ExprRules& r)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || (r.dotInNames && c == '.') || (r.atInNames && c == '@') ||
           (r.questionInNames && c == '?') || (r.dollarInNames && c == '$');
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

std::string Lower(std::string_view text)
{
    return z80::Lower(text);
}

std::string Upper(std::string_view text)
{
    return z80::Upper(text);
}

namespace
{
/// The apostrophe of AF' (EX AF,AF') is not a quote
bool IsAfApostrophe(std::string_view t, size_t i)
{
    return i >= 2 && std::tolower(static_cast<unsigned char>(t[i - 2])) == 'a' && std::tolower(static_cast<unsigned char>(t[i - 1])) == 'f' &&
           (i < 3 || !(std::isalnum(static_cast<unsigned char>(t[i - 3])) || t[i - 3] == '_'));
}
}  // namespace

std::string Mask(std::string_view t, bool escapesInSingleQuotes)
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
            if ((q == '"' || escapesInSingleQuotes) && t[j] == '\\' && j + 1 < t.size())
            {
                m[j] = m[j + 1] = 'x';
                j += 2;
                continue;
            }
            if (t[j] == q)
            {
                if (q == '\'' && !escapesInSingleQuotes && j + 1 < t.size() && t[j + 1] == '\'')
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

std::vector<std::string> Split(std::string_view text, char separator, bool escapesInSingleQuotes)
{
    const std::string m = Mask(text, escapesInSingleQuotes);
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

std::string StringLiteral(std::string_view t, size_t& i, const ExprRules& rules)
{
    const char q = t[i++];
    std::string out;
    while (i < t.size())
    {
        const char c = t[i];
        if (c == q)
        {
            if (q == '\'' && !rules.escapesInSingleQuotes && i + 1 < t.size() && t[i + 1] == '\'')
            {
                out.push_back('\'');
                i += 2;
                continue;
            }
            ++i;
            return out;
        }
        if ((q == '"' || rules.escapesInSingleQuotes) && rules.backslashEscapes && c == '\\' && i + 1 < t.size())
        {
            const char e = t[i + 1];
            i += 2;
            switch (e >= 'A' && e <= 'Z' ? static_cast<char>(e + 32) : e)
            {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'a': out.push_back('\a'); break;
                case 'b': out.push_back('\b'); break;
                case 'e': out.push_back('\x1B'); break;
                case 'f': out.push_back('\f'); break;
                case 'v': out.push_back('\v'); break;
                case 'x':
                {
                    int value = 0, digits = 0;
                    while (digits < 2 && i < t.size() && std::isxdigit(static_cast<unsigned char>(t[i])))
                    {
                        value = value * 16 + (std::isdigit(static_cast<unsigned char>(t[i])) ? t[i] - '0' : (std::tolower(static_cast<unsigned char>(t[i])) - 'a' + 10));
                        ++i;
                        ++digits;
                    }
                    out.push_back(static_cast<char>(value));
                    break;
                }
                default:
                    if (e >= '0' && e <= '7')
                    {
                        int value = e - '0', digits = 1;
                        while (digits < 3 && i < t.size() && t[i] >= '0' && t[i] <= '7')
                        {
                            value = value * 8 + (t[i] - '0');
                            ++i;
                            ++digits;
                        }
                        out.push_back(static_cast<char>(value));
                    }
                    else
                        out.push_back(e);   // \\ \" and any other character as itself
                    break;
            }
            continue;
        }
        out.push_back(c);
        ++i;
    }
    throw Failure{"string without its closing quote"};
}

namespace
{
struct Parser
{
    std::string_view t;
    const ExprRules& rules;
    size_t i = 0;

    void Blanks()
    {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t'))
            ++i;
    }
    char Peek(size_t ahead = 0) const { return i + ahead < t.size() ? t[i + ahead] : '\0'; }

    /// The whole word at the position in lower case ("" when a name continues or it is no word)
    std::string Word() const
    {
        size_t j = i;
        while (j < t.size() && std::isalpha(static_cast<unsigned char>(t[j])))
            ++j;
        if (j < t.size() && IsNameChar(t[j], rules))
            return {};
        return Lower(t.substr(i, j - i));
    }

    static Expr Wrap(Expr::Kind kind, Expr inner)
    {
        Expr e = Expr::Make(kind);
        e.args.push_back(std::move(inner));
        return e;
    }

    static bool All(std::string_view s, const char* set) { return !s.empty() && s.find_first_not_of(set) == std::string_view::npos; }

    Expr Digits(std::string_view digits, int base, ir::NumberSpelling spelling)
    {
        std::string clean;
        for (const char c : digits)
            if (c != '$')
                clean += c;
        return Expr::Number(std::stoll(clean, nullptr, base), spelling, static_cast<int>(clean.size()));
    }

    Expr Number()
    {
        size_t j = i;
        while (j < t.size() && (std::isalnum(static_cast<unsigned char>(t[j])) || (rules.dollarInDigits && t[j] == '$')))
            ++j;
        const std::string run(t.substr(i, j - i));
        i = j;
        const char* hex = "0123456789abcdefABCDEF$";
        const char last = static_cast<char>(std::tolower(static_cast<unsigned char>(run.back())));
        const std::string_view body = std::string_view(run).substr(0, run.size() - 1);
        if (rules.zeroX && run.size() > 2 && run[0] == '0' && (run[1] == 'x' || run[1] == 'X') && All(std::string_view(run).substr(2), hex))
            return Digits(std::string_view(run).substr(2), 16, ir::NumberSpelling::Hex);
        if (rules.zeroB && run.size() > 2 && run[0] == '0' && (run[1] == 'b' || run[1] == 'B') && All(std::string_view(run).substr(2), "01$"))
            return Digits(std::string_view(run).substr(2), 2, ir::NumberSpelling::Binary);
        if (rules.suffixes)
        {
            if (last == 'h' && All(body, hex))
                return Digits(body, 16, ir::NumberSpelling::Hex);
            if (last == 'b' && All(body, "01$"))
                return Digits(body, 2, ir::NumberSpelling::Binary);
            if (rules.suffixOctalDecimal && (last == 'o' || last == 'q') && All(body, "01234567$"))
                return Digits(body, 8, ir::NumberSpelling::Decimal);
            if (rules.suffixOctalDecimal && last == 'd' && All(body, "0123456789$"))
                return Digits(body, 10, ir::NumberSpelling::Decimal);
        }
        if (All(run, "0123456789$"))
            return Digits(run, 10, ir::NumberSpelling::Decimal);
        throw Failure{"number " + run + " in a form the converter does not read"};
    }

    Expr Primary()
    {
        Blanks();
        const char c = Peek();
        if (c == '(' || c == '[')
        {
            const char close = c == '(' ? ')' : ']';
            ++i;
            Expr inner = Parse(0);
            Blanks();
            if (Peek() != close)
                throw Failure{std::string(1, c) + " without " + close};
            ++i;
            return Wrap(Expr::Kind::Group, std::move(inner));
        }
        if (c == '$' && rules.currentAddress == "$" && !(rules.dollarHex && std::isxdigit(static_cast<unsigned char>(Peek(1)))))
        {
            ++i;
            return Expr::Make(Expr::Kind::Current);
        }
        if (c == '$' && rules.dollarHex && std::isxdigit(static_cast<unsigned char>(Peek(1))))
        {
            size_t j = ++i;
            while (j < t.size() && (std::isxdigit(static_cast<unsigned char>(t[j])) || (rules.dollarInDigits && t[j] == '$')))
                ++j;
            const Expr e = Digits(t.substr(i, j - i), 16, ir::NumberSpelling::Hex);
            i = j;
            return e;
        }
        if (c == '#' && rules.hashHex && std::isxdigit(static_cast<unsigned char>(Peek(1))))
        {
            size_t j = ++i;
            while (j < t.size() && std::isxdigit(static_cast<unsigned char>(t[j])))
                ++j;
            const Expr e = Digits(t.substr(i, j - i), 16, ir::NumberSpelling::Hex);
            i = j;
            return e;
        }
        if (c == '@' && rules.atOctal && std::isdigit(static_cast<unsigned char>(Peek(1))))
        {
            size_t j = ++i;
            while (j < t.size() && t[j] >= '0' && t[j] <= '7')
                ++j;
            const Expr e = Digits(t.substr(i, j - i), 8, ir::NumberSpelling::Decimal);
            i = j;
            return e;
        }
        if (((c == '%' && rules.percentBinary) || (c == '@' && rules.atBinary)) && (Peek(1) == '0' || Peek(1) == '1'))
        {
            size_t j = ++i;
            while (j < t.size() && (t[j] == '0' || t[j] == '1' || (rules.dollarInDigits && t[j] == '$')))
                ++j;
            const Expr e = Digits(t.substr(i, j - i), 2, ir::NumberSpelling::Binary);
            i = j;
            return e;
        }
        if (c == '&' && rules.ampersandBase)
        {
            const char k = static_cast<char>(std::tolower(static_cast<unsigned char>(Peek(1))));
            int base = 16;
            size_t skip = 1;
            if (k == 'o')
                base = 8, skip = 2;
            else if (k == 'h' || k == 'x')
                skip = 2;
            size_t j = i + skip;
            const size_t from = j;
            while (j < t.size() && (base == 8 ? (t[j] >= '0' && t[j] <= '7') : std::isxdigit(static_cast<unsigned char>(t[j]))))
                ++j;
            if (j == from)
                throw Failure{"& without digits"};
            const Expr e = Digits(t.substr(from, j - from), base, base == 16 ? ir::NumberSpelling::Hex : ir::NumberSpelling::Decimal);
            i = j;
            return e;
        }
        if (std::isdigit(static_cast<unsigned char>(c)))
            return Number();
        if (c == '\'' || (c == '"' && rules.doubleQuoteIsNumber))
        {
            const std::string chars = StringLiteral(t, i, rules);
            int64_t value = 0;
            if (rules.twoCharsLowFirst)
                for (size_t k = chars.size(); k > 0; --k)
                    value = (value << 8) | static_cast<unsigned char>(chars[k - 1]);
            else
                for (const char ch : chars)
                    value = (value << 8) | static_cast<unsigned char>(ch);
            Expr e = Expr::Number(value, ir::NumberSpelling::Character, static_cast<int>(chars.size()));
            e.text = chars;
            return e;
        }
        if (IsNameStart(c, rules))
        {
            size_t j = i;
            while (j < t.size() && IsNameChar(t[j], rules))
                ++j;
            std::string name(t.substr(i, j - i));
            if (!rules.currentWord.empty() && Lower(name) == rules.currentWord)
            {
                i = j;
                return Expr::Make(Expr::Kind::Current);
            }
            // name(expr): a function of one argument
            for (const auto& [function, op] : rules.functions)
                if (Lower(name) == function)
                {
                    size_t k = j;
                    while (k < t.size() && (t[k] == ' ' || t[k] == '\t'))
                        ++k;
                    if (k < t.size() && t[k] == '(')
                    {
                        i = k + 1;
                        Expr arg = Parse(0);
                        Blanks();
                        if (Peek() != ')')
                            throw Failure{"( without )"};
                        ++i;
                        return Expr::Unary(op, Wrap(Expr::Kind::Group, std::move(arg)));
                    }
                }
            if (rules.dollarInNames)
                name.erase(std::remove(name.begin(), name.end(), '$'), name.end());
            Expr e = Expr::Symbol(std::move(name));
            i = j;
            return e;
        }
        throw Failure{std::string("unexpected '") + c + "' in an expression"};
    }

    /// The prefix operator at the position: its length, 0 = none
    size_t PrefixOp(const UnaryOp*& found) const
    {
        for (const UnaryOp& u : rules.unary)
        {
            const std::string_view text(u.text);
            if (t.substr(i, text.size()) != text)
                continue;
            const bool word = std::isalpha(static_cast<unsigned char>(text[0])) != 0;
            if (word)
            {
                if (Word() != Lower(text))
                    continue;
            }
            else if (text == "!" && Peek(1) == '=')
                continue;
            found = &u;
            return text.size();
        }
        return 0;
    }

    Expr Unary()
    {
        Blanks();
        const UnaryOp* u = nullptr;
        if (const size_t length = PrefixOp(u))
        {
            i += length;
            return Expr::Unary(u->op, Parse(u->priority));
        }
        if (rules.definedOperator && Word() == "defined")
        {
            i += 7;
            Blanks();
            Expr name = Primary();
            if (name.kind != Expr::Kind::Symbol)
                throw Failure{"DEFINED needs a name"};
            return Expr::Unary(Op::Exists, std::move(name));
        }
        return Primary();
    }

    size_t BinaryAt(const BinaryOp*& found) const
    {
        size_t best = 0;
        for (const BinaryOp& b : rules.binary)
        {
            const std::string_view text(b.text);
            if (t.substr(i, text.size()) != text || text.size() <= best)
                continue;
            if (std::isalpha(static_cast<unsigned char>(text[0])) && Word() != Lower(text))
                continue;
            found = &b;
            best = text.size();
        }
        return best;
    }

    Expr Parse(int minPriority)
    {
        Expr left = Unary();
        while (true)
        {
            Blanks();
            const BinaryOp* b = nullptr;
            const size_t length = BinaryAt(b);
            if (length == 0 || b->priority < minPriority)
                return left;
            i += length;
            Expr right = Parse(b->priority + 1);
            left = Expr::Binary(b->op, std::move(left), std::move(right));
        }
    }
};
}  // namespace

Expr ParseExpression(std::string_view text, const ExprRules& rules, uint32_t line, Diagnostics& diagnostics)
{
    Parser p{text, rules};
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

namespace
{
bool WhollyParenthesized(std::string_view text)
{
    const std::string m = Mask(text);
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
}  // namespace

Operand InstructionOperand(const std::string& text, bool conditionAllowed, bool flagAllowed, const ExprRules& rules, uint32_t line,
                           Diagnostics& diagnostics)
{
    if (rules.bracketIndirect && text.size() >= 2 && text.front() == '[' && text.back() == ']')
    {
        // [..] wholly: the same as (..) when the brackets close at the end
        int depth = 0;
        bool whole = true;
        for (size_t k = 0; k < text.size() && whole; ++k)
        {
            if (text[k] == '[')
                ++depth;
            else if (text[k] == ']' && --depth == 0 && k + 1 != text.size())
                whole = false;
        }
        if (whole)
            return InstructionOperand("(" + text.substr(1, text.size() - 2) + ")", conditionAllowed, flagAllowed, rules, line, diagnostics);
    }
    if (rules.hashImmediate && text.size() > 1 && text[0] == '#')
        return InstructionOperand(Trim(std::string_view(text).substr(1)), false, false, rules, line, diagnostics);
    Operand o;
    const std::string lower = Lower(text);
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
        o.kind = Operand::Kind::Immediate;   // +(..): a value, not memory
        o.expr = ParseExpression(std::string_view(text).substr(1), rules, line, diagnostics);
        return o;
    }
    if (WhollyParenthesized(text))
    {
        const std::string inner = Trim(std::string_view(text).substr(1, text.size() - 2));
        const std::string innerLower = Lower(inner);
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
                o.expr = ParseExpression(displacement, rules, line, diagnostics);
                return o;
            }
        }
        o.kind = Operand::Kind::Memory;
        o.expr = ParseExpression(inner, rules, line, diagnostics);
        return o;
    }
    o.kind = Operand::Kind::Immediate;
    o.expr = ParseExpression(text, rules, line, diagnostics);
    return o;
}

std::vector<Operand> DataOperands(const std::string& rest, const ExprRules& rules, uint32_t line, Diagnostics& diagnostics)
{
    std::vector<Operand> out;
    for (const std::string& item : Split(rest, ',', rules.escapesInSingleQuotes))
    {
        Operand o;
        if (!item.empty() && (item[0] == '"' || item[0] == '\''))
        {
            try
            {
                size_t k = 0;
                const std::string chars = StringLiteral(item, k, rules);
                if (k == item.size() && chars.size() != 1)
                {
                    o.kind = Operand::Kind::String;
                    o.text = chars;
                    out.push_back(std::move(o));
                    continue;
                }
                const std::string after = Trim(std::string_view(item).substr(k));
                if (rules.stringLastCharOps && !chars.empty() && !after.empty() && std::string_view("+-&|^").find(after[0]) != std::string_view::npos)
                {
                    // "text" + n: the operator changes the last character
                    if (chars.size() > 1)
                    {
                        Operand head;
                        head.kind = Operand::Kind::String;
                        head.text = chars.substr(0, chars.size() - 1);
                        out.push_back(std::move(head));
                    }
                    Expr last = Expr::Number(static_cast<unsigned char>(chars.back()), ir::NumberSpelling::Character, 1);
                    last.text = std::string(1, chars.back());
                    const Op op = after[0] == '+' ? Op::Add : after[0] == '-' ? Op::Sub : after[0] == '&' ? Op::And : after[0] == '|' ? Op::Or : Op::Xor;
                    o.kind = Operand::Kind::Immediate;
                    o.expr = Expr::Binary(op, std::move(last), ParseExpression(Trim(std::string_view(after).substr(1)), rules, line, diagnostics));
                    out.push_back(std::move(o));
                    continue;
                }
            }
            catch (const Failure&)
            {
            }
        }
        o.kind = Operand::Kind::Immediate;
        o.expr = ParseExpression(item, rules, line, diagnostics);
        out.push_back(std::move(o));
    }
    return out;
}
}  // namespace unrealasm::dialects::text
