#include "dialects/tasm/tasmfrontend.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>

#include "dialects/common/macros.h"
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
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '?' || c == '@';
}

/// What the version changes in the syntax
struct Syntax
{
    bool v412 = true;   ///< [address] reads memory (4.0: [ ] are postfix rotations)
    std::string version;   ///< "3", "4.0", "4.12"; "" = any (a text file): every keyword counts
};

/// Whether a directive word exists in that TASM version (its keyword table, research-tasm.md §3): a word of another
/// version is a label there (TASM 3 sources use DM and INF as labels)
bool InVersion(std::string_view word, const std::string& version)
{
    if (version.empty())
        return true;
    // 5.0 / 5.5 beta: the TASM 4.0 syntax (their files use nothing else; 5.5's IF / MACRO / REPT stay text, below)
    const bool v3 = version == "2.0" || version == "3", v40 = version == "4.0" || version == "5.0" || version == "5.5", v412 = version == "4.12";
    if (word == "DB" || word == "DM" || word == "DS" || word == "DW" || word == "INF")
        return v40;
    if (word == "DEFM" || word == "PHASE" || word == "UNPHASE" || word == "INCLUDE" || word == "INCBIN")
        return v3 || v40;
    if (word == "DEFMAC" || word == "ENDMAC" || word == "DISPLAY" || (!word.empty() && word[0] == '.'))
        return v412;
    if (word == "IF" || word == "ELSE" || word == "ENDIF" || word == "IFDEF" || word == "MACRO" || word == "ENDM" || word == "REPT" ||
        word == "ENDR" || word == "INCSEC" || word == "PRINTF")
        return version == "5.5";
    return true;
}

/// Expressions: left to right without priorities ("all operations are done one after another", TASM 4.0 description)
struct ExpressionParser
{
    std::string_view t;
    Syntax syntax;
    size_t i = 0;
    int memoryDepth = 0;

    void Blanks()
    {
        while (i < t.size() && t[i] == ' ')
            ++i;
    }
    char Peek() const { return i < t.size() ? t[i] : '\0'; }

    static Expr Wrap(Expr::Kind kind, Expr inner)
    {
        Expr e = Expr::Make(kind);
        e.args.push_back(std::move(inner));
        return e;
    }

    Expr Term()
    {
        Blanks();
        const char c = Peek();
        if (c == '-' || c == '+')
        {
            ++i;
            return Expr::Unary(c == '-' ? Op::Negate : Op::Plus, Term());
        }
        if (c == '(')
        {
            ++i;
            Expr inner = Sequence();
            Blanks();
            if (Peek() != ')')
                throw Failure{"( without )"};
            ++i;
            return Wrap(Expr::Kind::Group, std::move(inner));
        }
        if (c == '[' && syntax.v412)
        {
            // TASM 4.12: the word at that address while assembling
            ++i;
            ++memoryDepth;
            Expr inner = Sequence();
            --memoryDepth;
            Blanks();
            if (Peek() != ']')
                throw Failure{"[ without ]"};
            ++i;
            return Wrap(Expr::Kind::Memory, std::move(inner));
        }
        if (c == '#' || c == '%')
        {
            const bool hex = c == '#';
            size_t j = ++i;
            while (j < t.size() && (hex ? std::isxdigit(static_cast<unsigned char>(t[j])) != 0 : (t[j] == '0' || t[j] == '1')))
                ++j;
            if (j == i)
                throw Failure{std::string(1, c) + " without digits"};
            const Expr e = Expr::Number(std::stoll(std::string(t.substr(i, j - i)), nullptr, hex ? 16 : 2), hex ? ir::NumberSpelling::Hex : ir::NumberSpelling::Binary,
                                        static_cast<int>(j - i));
            i = j;
            return e;
        }
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            // Decimal, or hex with an H suffix (40H)
            size_t j = i;
            while (j < t.size() && std::isxdigit(static_cast<unsigned char>(t[j])))
                ++j;
            if (j < t.size() && (t[j] == 'H' || t[j] == 'h'))
            {
                const Expr e = Expr::Number(std::stoll(std::string(t.substr(i, j - i)), nullptr, 16), ir::NumberSpelling::Hex, static_cast<int>(j - i));
                i = j + 1;
                return e;
            }
            j = i;
            while (j < t.size() && std::isdigit(static_cast<unsigned char>(t[j])))
                ++j;
            const Expr e = Expr::Number(std::stoll(std::string(t.substr(i, j - i))), ir::NumberSpelling::Decimal, static_cast<int>(j - i));
            i = j;
            return e;
        }
        if (c == '"')
        {
            // A character constant; "" inside is one quote (GENS rule, TASM 4.0 fixed """)
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
            // A text may run to the end of the line (TASM 4.0 reads """ as one quote, the GENS form)
            i = j < t.size() ? j + 1 : j;
            int64_t value = 0;
            for (const char ch : chars)
                value = ((value << 8) | static_cast<unsigned char>(ch)) & 0xFFFF;   // a 16-bit word: the last two characters
            Expr e = Expr::Number(value, ir::NumberSpelling::Character, static_cast<int>(chars.size()));
            e.text = chars;
            return e;
        }
        if (c == '$')
        {
            ++i;
            return Expr::Make(Expr::Kind::Current);
        }
        if ((c == '\\' || c == '/') && i + 1 < t.size() && std::isdigit(static_cast<unsigned char>(t[i + 1])))
        {
            // A macro parameter, \0 or /0 (TASM 4.12's EXAMPLES): the backend names it
            Expr e = Expr::Symbol(std::string("\\") + t[i + 1]);
            i += 2;
            return e;
        }
        if (IsLabelChar(c) && !std::isdigit(static_cast<unsigned char>(c)))
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

    Expr Sequence()
    {
        Expr acc = Term();
        while (true)
        {
            Blanks();
            const char c = Peek();
            // Postfix operators act on the value so far
            if (c == '^' || c == '{' || c == '}')
            {
                ++i;
                acc = Expr::Unary(c == '^' ? Op::SwapBytes : c == '{' ? Op::High : Op::Low, std::move(acc));
                continue;
            }
            if ((c == '[' || c == ']') && !(c == ']' && memoryDepth > 0))
            {
                ++i;
                acc = Expr::Binary(c == '[' ? Op::RotateLeft16 : Op::RotateRight16, std::move(acc), Expr::Number(1));
                continue;
            }
            Op op;
            switch (c)
            {
                case '+': op = Op::Add; break;
                case '-': op = Op::Sub; break;
                case '*': op = Op::Mul; break;
                case '/': op = Op::Div; break;
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

/// Splits operands at commas outside quotes, parentheses and brackets
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
        else if (!quote && (c == '(' || c == '['))
            ++depth;
        else if (!quote && (c == ')' || c == ']') && depth > 0)
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

struct Parser
{
    Syntax syntax;
    uint32_t line = 0;
    Diagnostics& diagnostics;
    const std::set<std::string>* labels = nullptr;   // labels the source defines (column 0)

    Expr Parse(std::string_view text) const
    {
        ExpressionParser p{text, syntax};
        return p.Whole();
    }

    Operand ParseOperand(const std::string& text, bool conditionAllowed, bool instruction) const
    {
        Operand o;
        const std::string lower = z80::Lower(text);
        if (conditionAllowed && (z80::IsCondition(lower) || lower == "nv" || lower == "v"))
        {
            o.kind = Operand::Kind::Condition;
            o.text = lower == "nv" ? "po" : lower == "v" ? "pe" : lower;   // TASM's NV / V: no overflow / overflow
            return o;
        }
        const std::string reg = z80::NormalizeRegister(lower);
        if (z80::IsRegister(reg) && reg != "f")
        {
            o.kind = Operand::Kind::Register;
            o.text = reg;
            return o;
        }
        if (instruction && text.size() >= 3 && text.front() == '(')
        {
            // An operand starting with "(" is memory (TASM 4.12 article: "0+" in front makes LD DE,(nn) a value); up
            // to the matching ")"
            int depth = 0;
            size_t close = std::string::npos;
            for (size_t c = 0; c < text.size() && close == std::string::npos; ++c)
            {
                if (text[c] == '(')
                    ++depth;
                else if (text[c] == ')' && --depth == 0)
                    close = c;
            }
            if (close == std::string::npos)
                throw Failure{"( without )"};
            if (close + 1 < text.size())
                diagnostics.push_back({Severity::Warning, line, 0, "TASM reads " + text.substr(0, close + 1) + " as memory; \"" + text.substr(close + 1) + "\" is not converted"});
            const std::string inner = text.substr(1, close - 1);
            const std::string innerLower = z80::Lower(inner);
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
                o.expr = Parse(std::string_view(inner).substr(2));
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

    /// DEFB / DEFM / DEFS fill items: strings of any length (a 1-character string is a byte either way) and values
    std::vector<Operand> DataOperands(const std::vector<std::string>& ops, bool words, bool anyDelimiter = false) const
    {
        std::vector<Operand> out;
        for (const std::string& op : ops)
        {
            // DEFM /text/: the first character delimits the text (the GENS form; seen as DM /"/)
            if (anyDelimiter && op.size() >= 2 && op.front() == op.back() && std::ispunct(static_cast<unsigned char>(op.front())) &&
                op.front() != '"' && op.front() != '(' && op.front() != '#' && op.front() != '%' && op.front() != '$' && op.front() != '-' &&
                op.front() != '+')
            {
                Operand o;
                o.kind = Operand::Kind::String;
                o.text = op.substr(1, op.size() - 2);
                out.push_back(std::move(o));
                continue;
            }
            if (!words && op.size() >= 2 && op.front() == '"' && (op.back() != '"' || op.size() == 2) &&
                std::count(op.begin(), op.end(), '"') == 1)
            {
                Operand o;   // a text running to the end of the line
                o.kind = Operand::Kind::String;
                o.text = op.substr(1);
                out.push_back(std::move(o));
                continue;
            }
            if (!words && op.size() >= 2 && op.front() == '"' && op.back() == '"')
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
                        whole = false;   // "a"+1: an expression
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
            out.push_back(ParseOperand(op, false, false));
        }
        return out;
    }
};

struct Directive
{
    std::string_view name;
    ir::DirectiveKind kind;
};

// The directives of TASM 3 / 4.0 (tokens) and 4.12 (tokens + dotted words); upper case: the tokenizer stores them so
constexpr Directive kDirectives[] = {
    {"ORG", ir::DirectiveKind::Org},           {"EQU", ir::DirectiveKind::Equ},          {"DEFB", ir::DirectiveKind::Db},
    {"DB", ir::DirectiveKind::Db},             {"DEFM", ir::DirectiveKind::Db},          {"DM", ir::DirectiveKind::Db},
    {"DEFW", ir::DirectiveKind::Dw},           {"DW", ir::DirectiveKind::Dw},            {"DEFS", ir::DirectiveKind::Ds},
    {"DS", ir::DirectiveKind::Ds},             {"PHASE", ir::DirectiveKind::Disp},       {".PHASE", ir::DirectiveKind::Disp},
    {"UNPHASE", ir::DirectiveKind::Ent},       {".UNPHASE", ir::DirectiveKind::Ent},     {"INCLUDE", ir::DirectiveKind::Include},
    {".INCLUDE", ir::DirectiveKind::Include},  {"INCBIN", ir::DirectiveKind::Incbin},    {".INCBIN", ir::DirectiveKind::Incbin},
    {".IF", ir::DirectiveKind::If},            {".ELSE", ir::DirectiveKind::Else},       {".ENDIF", ir::DirectiveKind::EndIf},
    {"DEFMAC", ir::DirectiveKind::Macro},      {"ENDMAC", ir::DirectiveKind::EndMacro},  {"DISPLAY", ir::DirectiveKind::Display},
    {".LOCAL", ir::DirectiveKind::Other},      {".PAGE", ir::DirectiveKind::Other},      {".RUN", ir::DirectiveKind::Other},
    // TASM 5.5 beta's keyword table has these; its assembler (a beta) refuses even DB inside them, so their meaning is
    // not known: kept as text
    {"IF", ir::DirectiveKind::Other},          {"ELSE", ir::DirectiveKind::Other},       {"ENDIF", ir::DirectiveKind::Other},
    {"IFDEF", ir::DirectiveKind::Other},       {"MACRO", ir::DirectiveKind::Other},      {"ENDM", ir::DirectiveKind::Other},
    {"REPT", ir::DirectiveKind::Other},        {"ENDR", ir::DirectiveKind::Other},       {"INCSEC", ir::DirectiveKind::Other},
    {"PRINTF", ir::DirectiveKind::Other},
};

std::string FileName(const std::string& operand)
{
    std::string name = operand;
    if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
        name = name.substr(1, name.size() - 2);
    while (!name.empty() && name.back() == ' ')   // TR-DOS names are blank padded
        name.pop_back();
    return name;
}

bool TwoOperands(const std::string& mnemonic)
{
    return mnemonic == "ld" || mnemonic == "add" || mnemonic == "adc" || mnemonic == "sbc" || mnemonic == "ex" || mnemonic == "in" ||
           mnemonic == "out" || mnemonic == "bit" || mnemonic == "res" || mnemonic == "set" || mnemonic == "jp" || mnemonic == "jr" ||
           mnemonic == "call";
}

/// TASM keeps operands as tokens: a register or condition token needs no comma after it, and some sources store none
/// ("LD HL#4000", "LD C(HL)", "LD (PTR)A", "JR NZLOOP"). The text shows them run together: the split goes after a
/// parenthesized first operand or after a register / condition name when the rest starts an operand (a number, a
/// parenthesis, a register, a label the source defines)
std::vector<std::string> SplitWithoutComma(const std::string& text, const std::string& mnemonic, const std::set<std::string>* labels)
{
    auto startsOperand = [&](const std::string& rest) {
        if (rest.empty())
            return false;
        const char c = rest[0];
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            // a number: digits (or hex digits and H) up to an operator, not the tail of a name (Z12_, Z9X)
            size_t e = 0;
            while (e < rest.size() && std::isxdigit(static_cast<unsigned char>(rest[e])))
                ++e;
            if (e < rest.size() && (rest[e] == 'H' || rest[e] == 'h'))
                ++e;
            return e == rest.size() || !IsLabelChar(rest[e]);
        }
        if (c == '#' || c == '%' || c == '(' || c == '"' || c == '$' || c == '[')
            return true;
        if (z80::IsRegister(z80::NormalizeRegister(z80::Lower(rest))))
            return true;
        size_t e = 0;
        while (e < rest.size() && IsLabelChar(rest[e]))
            ++e;
        return labels && e > 0 && labels->count(rest.substr(0, e));
    };
    if (labels && labels->count(text))
        return {text};
    if (!text.empty() && text[0] == '(')
    {
        int depth = 0;
        for (size_t c = 0; c < text.size(); ++c)
        {
            if (text[c] == '(')
                ++depth;
            else if (text[c] == ')' && --depth == 0)
            {
                const std::string rest = text.substr(c + 1);
                if (startsOperand(rest))
                    return {text.substr(0, c + 1), rest};
                break;
            }
        }
        return {text};
    }
    static const char* const kRegisters[] = {"AF'", "IXH", "IXL", "IYH", "IYL", "HL", "DE", "BC", "SP", "IX", "IY", "AF", "HX", "LX", "HY",
                                             "LY", "XH", "XL", "YH", "YL", "A", "B", "C", "D", "E", "H", "L", "I", "R"};
    static const char* const kConditions[] = {"NZ", "NC", "PO", "PE", "NV", "Z", "C", "P", "M", "V"};
    const bool conditional = mnemonic == "jp" || mnemonic == "jr" || mnemonic == "call";
    const std::string upper = z80::Upper(text);
    auto tryNames = [&](const char* const* names, size_t count) -> std::vector<std::string> {
        for (size_t k = 0; k < count; ++k)
        {
            const std::string name = names[k];
            if (upper.size() > name.size() && upper.compare(0, name.size(), name) == 0 && startsOperand(text.substr(name.size())))
                return {text.substr(0, name.size()), text.substr(name.size())};
        }
        return {};
    };
    std::vector<std::string> split = conditional ? tryNames(kConditions, std::size(kConditions)) : tryNames(kRegisters, std::size(kRegisters));
    if (split.empty() && (mnemonic == "bit" || mnemonic == "res" || mnemonic == "set"))
    {
        // BIT 3D: a bit number, then the register
        for (const char* name : kRegisters)
        {
            const std::string n = name;
            if (upper.size() > n.size() && upper.compare(upper.size() - n.size(), n.size(), n) == 0 &&
                std::isdigit(static_cast<unsigned char>(upper[upper.size() - n.size() - 1])))
                return {text.substr(0, text.size() - n.size()), text.substr(text.size() - n.size())};
        }
        if (upper.size() > 4 && upper.back() == ')' && upper.find('(') != std::string::npos)
        {
            const size_t open = text.find('(');
            if (open > 0 && std::isdigit(static_cast<unsigned char>(text[open - 1])))
                return {text.substr(0, open), text.substr(open)};
        }
    }
    return split.empty() ? std::vector<std::string>{text} : split;
}

/// The statement after the label: a directive, an instruction or a macro call
Statement ParseStatement(const std::string& word, const std::string& rest, const Parser& p, const std::set<std::string>& macros)
{
    Statement s;
    const std::string upper = z80::Upper(word);
    const std::vector<std::string> ops = SplitOperands(rest);
    if (!macros.count(word))
        for (const Directive& d : kDirectives)
        {
            if (upper != d.name || !InVersion(d.name, p.syntax.version))
                continue;
            s.kind = Statement::Kind::Directive;
            s.directive = d.kind;
            switch (d.kind)
            {
                case ir::DirectiveKind::Db:
                case ir::DirectiveKind::Dw:
                case ir::DirectiveKind::Display:
                    s.operands = p.DataOperands(ops, d.kind == ir::DirectiveKind::Dw, upper == "DEFM" || upper == "DM");
                    break;
                case ir::DirectiveKind::Ds:
                    // DS count[,fill...]: the fill sequence (strings too) repeats count times (TASM 4.0 description)
                    if (ops.empty())
                        throw Failure{"DS without a count"};
                    s.args.push_back(p.Parse(ops[0]));
                    if (ops.size() > 1)
                        s.operands = p.DataOperands(std::vector<std::string>(ops.begin() + 1, ops.end()), false);
                    break;
                case ir::DirectiveKind::Include:
                case ir::DirectiveKind::Incbin:
                    s.text = ops.empty() ? std::string() : FileName(ops[0]);
                    if (d.kind == ir::DirectiveKind::Incbin && ops.size() > 1)
                        s.args.push_back(Expr::Number(0));   // INCBIN name,length; the IR has [offset, length]
                    // TASM's INCBIN copies whole sectors: the bytes after the file in its last sector land in memory
                    // after it, the address moves by the file's length (seen in the GS 1.04 ROM)
                    if (d.kind == ir::DirectiveKind::Incbin && ops.size() == 1)
                        s.params = {"sector-slack"};
                    for (size_t k = 1; k < ops.size(); ++k)
                        s.args.push_back(p.Parse(ops[k]));
                    break;
                case ir::DirectiveKind::Macro:
                    s.text = ops.empty() ? std::string() : ops[0];
                    break;
                case ir::DirectiveKind::If:
                {
                    // .IF compiles its first part when the value is 0 (Scenergy #1: "USE_MULT8=0" selects the code)
                    Expr g = Expr::Make(Expr::Kind::Group);
                    g.args.push_back(p.Parse(rest));
                    s.args.push_back(Expr::Binary(Op::Equal, std::move(g), Expr::Number(0)));
                    break;
                }
                case ir::DirectiveKind::Other:
                    s.text = upper + (rest.empty() ? "" : " " + rest);
                    break;
                default:
                    for (const std::string& op : ops)
                        if (!op.empty())
                            s.args.push_back(p.Parse(op));
            }
            return s;
        }

    std::string mnemonic = z80::Lower(word);
    std::vector<std::string> parts = ops;
    if (upper == "INF" && InVersion("INF", p.syntax.version))
        mnemonic = "in", parts = {"F", "(C)"};
    else if (parts.size() == 1 && TwoOperands(mnemonic) && z80::IsMnemonic(mnemonic) && !macros.count(word))
        parts = SplitWithoutComma(parts[0], mnemonic, p.labels);
    if (macros.count(word) || !z80::IsMnemonic(mnemonic))
    {
        s.kind = Statement::Kind::MacroCall;
        s.mnemonic = word;
        s.params = ops;
        return s;
    }
    s.kind = Statement::Kind::Instruction;
    s.mnemonic = mnemonic;
    for (size_t k = 0; k < parts.size(); ++k)
    {
        if (mnemonic == "in" && z80::Upper(parts[k]) == "F")
        {
            Operand f;
            f.kind = Operand::Kind::Register;
            f.text = "f";
            s.operands.push_back(f);
            continue;
        }
        const bool condition = z80::TakesCondition(mnemonic) && k == 0 && (parts.size() >= 2 || mnemonic == "ret");
        s.operands.push_back(p.ParseOperand(parts[k], condition, true));
    }
    return s;
}

/// TASM 4.12 local labels: "...name" after a .LOCAL belongs to that region
/// TASM 4.12 local labels: "...name" belongs to the region after the last .LOCAL; inside a macro to each expansion
/// (region -1: a label of a LOCAL block the backend makes unique per expansion)
std::string LocalName(const std::string& name, int region)
{
    if (name.rfind("...", 0) != 0)
        return name;
    if (region < 0)
        return "local_" + name.substr(3);
    return "__local" + std::to_string(region) + "_" + name.substr(3);
}

void RenameLocals(Expr& e, int region)
{
    if (e.kind == Expr::Kind::Symbol)
        e.text = LocalName(e.text, region);
    for (Expr& a : e.args)
        RenameLocals(a, region);
}

bool IsKeyword(const std::string& word, const std::set<std::string>& macros, const std::string& version)
{
    const std::string upper = z80::Upper(word);
    for (const Directive& d : kDirectives)
        if (upper == d.name && InVersion(d.name, version))
            return true;
    if (upper == "INF")
        return InVersion("INF", version);
    return macros.count(word) || z80::IsMnemonic(z80::Lower(word));
}

struct LineParser
{
    Syntax syntax;
    std::set<std::string>& macros;
    FrontendResult& result;
    int localRegion = 0;
    // .PAGE n (4.05): the code of the next ORGs at #C000 and up goes to RAM page n (checked in TASM 4.12)
    std::optional<Expr> page{};
    bool inMacro = false;                      // inside DEFMAC: ...labels are local to each expansion
    const std::set<std::string>* referenced = nullptr;   // names the source uses as operands
    const std::set<std::string>* labels = nullptr;       // names defined in column 0
    int Region() const { return inMacro ? -1 : localRegion; }
    // PHASE state: an ORG or another PHASE ends an active PHASE first (sjasmplus' ORG would move only $). Where a file
    // starts it is not known (an INCLUDE inside PHASE): the end is then conditional
    enum class Phase { Unknown, Active, Off } phase = Phase::Unknown;

    void EndPhase(ir::Line& line)
    {
        if (phase == Phase::Off)
            return;
        Statement ent;
        ent.kind = Statement::Kind::Directive;
        ent.directive = ir::DirectiveKind::Ent;
        if (phase == Phase::Unknown)
            ent.text = "if-displaced";
        line.statements.push_back(std::move(ent));
        phase = Phase::Off;
    }

    void ParseLine(const std::string& text, uint32_t number)
    {
        ir::Line line;
        line.sourceLine = number;
        std::string t = text;
        // The comment: from a ; outside a string
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
        for (char& c : t)
            if (c == '\t')
                c = ' ';
        while (!t.empty() && t.back() == ' ')
            t.pop_back();
        const Parser p{syntax, number, result.diagnostics, labels};
        try
        {
            size_t i = 0;
            // The label: whatever starts in column 0 (TASM's label field)
            size_t wordEnd = 0;
            while (wordEnd < t.size() && t[wordEnd] != ' ' && t[wordEnd] != '=')
                ++wordEnd;
            // A keyword in column 0 is a command: TASM stores it as a token, never as a label
            if (!t.empty() && t[0] != ' ' && !IsKeyword(t.substr(0, wordEnd), macros, syntax.version))
            {
                i = wordEnd;
                line.label = t.substr(0, i);
                if (line.label.size() > 1 && line.label.back() == ':')
                    line.label.pop_back();   // LOOP: is LOOP
            }
            else if (referenced && !t.empty() && t[0] == ' ')
            {
                // An indented word that is no command but is used as a label is one (" ?ASKYN" in ADVENTURER's FORMAIN)
                const size_t w0 = t.find_first_not_of(' ');
                size_t w1 = w0;
                while (w1 < t.size() && t[w1] != ' ')
                    ++w1;
                const std::string word = w0 == std::string::npos ? std::string() : t.substr(w0, w1 - w0);
                const size_t n0 = t.find_first_not_of(' ', w1);
                const std::string next = n0 == std::string::npos ? std::string() : t.substr(n0, t.find(' ', n0) == std::string::npos ? std::string::npos : t.find(' ', n0) - n0);
                // and so is an indented word before EQU or = ("        fSIZE   EQU 4")
                const bool defines = z80::Upper(next) == "EQU" || next == "=" || (!next.empty() && next[0] == '=');
                if (!word.empty() && !IsKeyword(word, macros, syntax.version) &&
                    (defines || (referenced->count(word) && (next.empty() || IsKeyword(next, macros, syntax.version)))))
                {
                    line.label = word;
                    i = w1;
                }
            }
            if (!line.label.empty())
            {
                // Inside a macro a ...label is local to each expansion, every other label stays global
                line.labelGlobal = inMacro && line.label.rfind("...", 0) != 0;
                line.label = LocalName(line.label, Region());
            }
            while (i < t.size() && t[i] == ' ')
                ++i;
            if (i < t.size() && t[i] == '=')
            {
                // label = expression: redefinable (TASM 4.12)
                Statement s;
                s.kind = Statement::Kind::Directive;
                s.directive = ir::DirectiveKind::Defl;
                s.args.push_back(p.Parse(std::string_view(t).substr(i + 1)));
                RenameLocals(s.args.back(), Region());
                line.statements.push_back(std::move(s));
            }
            else if (i < t.size())
            {
                size_t j = i;
                while (j < t.size() && t[j] != ' ')
                    ++j;
                const std::string word = t.substr(i, j - i);
                while (j < t.size() && t[j] == ' ')
                    ++j;
                if (z80::Upper(word) == ".PAGE" || z80::Upper(word) == ".RUN")
                {
                    // .PAGE n: kept for the ORGs that follow (ORG address,page); .RUN address (4.11) only sets where
                    // TASM's Run command starts the code: no counterpart, kept as a comment
                    const std::string operand = t.substr(j);
                    if (z80::Upper(word) == ".PAGE")
                        page = p.Parse(operand);
                    else
                        result.diagnostics.push_back({Severity::Info, number, 0, ".RUN " + operand + " (TASM's Run address) kept as a comment"});
                    line.comment = " " + z80::Upper(word) + " " + operand + (line.hasComment ? ";" + line.comment : std::string());
                    line.hasComment = true;
                    result.program.lines.push_back(std::move(line));
                    return;
                }
                if (z80::Upper(word) == ".LOCAL")
                {
                    // A new region for ...labels; the labels are renamed, nothing is left to write
                    ++localRegion;
                    line.comment = " .LOCAL" + (line.hasComment ? ";" + line.comment : std::string());
                    line.hasComment = true;
                    result.program.lines.push_back(std::move(line));
                    return;
                }
                Statement s = ParseStatement(word, t.substr(j), p, macros);
                if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Macro)
                    macros.insert(s.text);
                if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Other)
                    result.diagnostics.push_back({Severity::Warning, number, 0, "TASM directive " + word + " kept as text"});
                if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Org && page && s.args.size() == 1)
                {
                    // TASM puts the code in the page only at #C000 and up ("ORG must be above #C000")
                    if (s.args[0].kind == Expr::Kind::Number && s.args[0].value >= 0xC000)
                        s.args.push_back(*page);
                    else if (s.args[0].kind != Expr::Kind::Number)
                        result.diagnostics.push_back({Severity::Warning, number, 0, "ORG after .PAGE with an address not known here: the page is not attached"});
                }
                if (s.kind == Statement::Kind::Directive && (s.directive == ir::DirectiveKind::Org || s.directive == ir::DirectiveKind::Disp))
                {
                    EndPhase(line);
                    phase = s.directive == ir::DirectiveKind::Disp ? Phase::Active : Phase::Off;
                }
                if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Ent)
                {
                    // UNPHASE: ends an active PHASE; without one TASM ignores it
                    if (phase == Phase::Off)
                    {
                        line.comment = " UNPHASE (no PHASE active)" + (line.hasComment ? ";" + line.comment : std::string());
                        line.hasComment = true;
                    }
                    EndPhase(line);
                    result.program.lines.push_back(std::move(line));
                    return;
                }
                for (Expr& a : s.args)
                    RenameLocals(a, Region());
                for (Operand& o : s.operands)
                    RenameLocals(o.expr, Region());
                if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Macro)
                {
                    // The body is a LOCAL block: its ...labels become unique per expansion
                    line.statements.push_back(std::move(s));
                    Statement block;
                    block.kind = Statement::Kind::Directive;
                    block.directive = ir::DirectiveKind::LocalBlock;
                    line.statements.push_back(std::move(block));
                    inMacro = true;
                    result.program.lines.push_back(std::move(line));
                    return;
                }
                if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::EndMacro)
                {
                    Statement block;
                    block.kind = Statement::Kind::Directive;
                    block.directive = ir::DirectiveKind::EndLocalBlock;
                    line.statements.push_back(std::move(block));
                    inMacro = false;
                }
                // PUSH AF,BC,DE: one instruction per register (TASM 4.0)
                if (s.kind == Statement::Kind::Instruction && (s.mnemonic == "push" || s.mnemonic == "pop") && s.operands.size() > 1)
                {
                    for (const Operand& o : s.operands)
                    {
                        Statement part = s;
                        part.operands.assign(1, o);
                        line.statements.push_back(std::move(part));
                    }
                }
                else
                    line.statements.push_back(std::move(s));
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

/// The first word of a line after its label (upper case) and the rest
std::pair<std::string, std::string> Command(const std::string& text)
{
    size_t i = 0;
    if (!text.empty() && text[0] != ' ' && text[0] != '\t')
        while (i < text.size() && text[i] != ' ' && text[i] != '\t')
            ++i;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
        ++i;
    size_t j = i;
    while (j < text.size() && text[j] != ' ' && text[j] != '\t' && text[j] != ';')
        ++j;
    size_t k = j;
    while (k < text.size() && (text[k] == ' ' || text[k] == '\t'))
        ++k;
    return {z80::Upper(text.substr(i, j - i)), text.substr(k)};
}

/// TASM 4.12's conditionals do not nest (checked in the emulator, research-tasm-to-sjasmplus.md): one state, which .IF
/// sets from its value even inside a skipped part, .ELSE inverts and .ENDIF clears. As sjasmplus blocks: a .IF while
/// one is open closes it first, a .ENDIF with none open is dropped, a .ELSE with none open starts skipping (IF 0), and
/// an open block ends before ENDMAC and at the end of the file. A macro body keeps its own state.
void FlattenConditionals(ir::Program& program, Diagnostics& diagnostics)
{
    const auto directive = [](ir::DirectiveKind kind) {
        Statement s;
        s.kind = Statement::Kind::Directive;
        s.directive = kind;
        return s;
    };
    bool open = false;
    bool outerOpen = false;
    for (ir::Line& line : program.lines)
    {
        std::vector<Statement> statements;
        for (Statement& s : line.statements)
        {
            if (s.kind != Statement::Kind::Directive)
            {
                statements.push_back(std::move(s));
                continue;
            }
            switch (s.directive)
            {
                case ir::DirectiveKind::If:
                    if (open)
                    {
                        statements.push_back(directive(ir::DirectiveKind::EndIf));
                        diagnostics.push_back({Severity::Info, line.sourceLine, 0, ".IF while a .IF is open: TASM's conditionals do not nest, the open one ends here"});
                    }
                    open = true;
                    break;
                case ir::DirectiveKind::Else:
                    if (!open)
                    {
                        // No block open: TASM's .ELSE inverts "not skipping" into skipping
                        s = directive(ir::DirectiveKind::If);
                        s.args.push_back(Expr::Number(0));
                        open = true;
                        diagnostics.push_back({Severity::Info, line.sourceLine, 0, ".ELSE with no .IF open: skips to the next .ENDIF / .IF"});
                    }
                    break;
                case ir::DirectiveKind::EndIf:
                    if (!open)
                    {
                        diagnostics.push_back({Severity::Info, line.sourceLine, 0, ".ENDIF with no .IF open: nothing to end"});
                        continue;
                    }
                    open = false;
                    break;
                case ir::DirectiveKind::Macro:
                    outerOpen = open;
                    open = false;
                    break;
                case ir::DirectiveKind::EndMacro:
                    if (open)
                        statements.push_back(directive(ir::DirectiveKind::EndIf));
                    open = outerOpen;
                    outerOpen = false;
                    break;
                default:
                    break;
            }
            statements.push_back(std::move(s));
        }
        line.statements = std::move(statements);
    }
    if (open && !program.lines.empty())
    {
        program.lines.back().statements.push_back(directive(ir::DirectiveKind::EndIf));
        diagnostics.push_back({Severity::Info, program.lines.back().sourceLine, 0, "a .IF still open at the end of the file ends there"});
    }
}
}  // namespace

FrontendResult TasmFrontend::Parse(const SourceDocument& source) const
{
    FrontendResult result;
    result.program.dialect = "tasm";
    result.program.expressionBits = 16;          // TASM computes in 16-bit words
    result.program.unsignedArithmetic = true;
    result.program.displacementAcrossFiles = true;
    Syntax syntax;
    syntax.v412 = source.subversion.empty() || source.subversion == "4.12";
    syntax.version = source.subversion;

    // Macro bodies, and the ones expanded at their calls (gluing parameters or walking the parameter text)
    std::map<std::string, std::vector<std::string>> bodies;
    {
        std::string current;
        for (const SourceLine& l : source.lines)
        {
            const auto [word, rest] = Command(l.text);
            if (word == "DEFMAC")
            {
                current = rest.substr(0, rest.find_first_of(" ;\t"));
                bodies[current].clear();
            }
            else if (word == "ENDMAC")
                current.clear();
            else if (!current.empty())
                bodies[current].push_back(l.text);
        }
    }
    std::set<std::string> expanded;
    for (const auto& [name, body] : bodies)
        if (NeedsExpansion(body))
        {
            expanded.insert(name);
            result.diagnostics.push_back({Severity::Info, 0, 0, "macro " + name + " glues or walks its parameters: expanded at its calls"});
        }

    // Names used as operands (words after the command), for labels written indented
    std::set<std::string> referenced;
    for (const SourceLine& l : source.lines)
    {
        std::string t = l.text.substr(0, l.text.find(';'));
        const auto [word, rest] = Command(t);
        (void)word;
        for (size_t k = 0; k < rest.size();)
        {
            while (k < rest.size() && !(IsLabelChar(rest[k]) && !std::isdigit(static_cast<unsigned char>(rest[k]))))
                ++k;
            size_t e = k;
            while (e < rest.size() && IsLabelChar(rest[e]))
                ++e;
            if (e > k)
                referenced.insert(rest.substr(k, e - k));
            k = e;
        }
    }
    std::set<std::string> labels;
    for (const SourceLine& l : source.lines)
        if (!l.text.empty() && l.text[0] != ' ' && l.text[0] != '\t' && l.text[0] != ';')
            labels.insert(l.text.substr(0, l.text.find_first_of(" \t=;")));
    std::set<std::string> macros;
    LineParser parser{syntax, macros, result};
    parser.referenced = &referenced;
    parser.labels = &labels;
    std::function<void(const std::string&, uint32_t, int)> emit = [&](const std::string& text, uint32_t number, int depth) {
        const size_t before = result.program.lines.size();
        parser.ParseLine(text, number);
        if (result.program.lines.size() != before + 1 || depth > 16)
            return;
        ir::Line& line = result.program.lines.back();
        if (line.statements.size() != 1 || line.statements[0].kind != Statement::Kind::MacroCall || !expanded.count(line.statements[0].mnemonic))
            return;
        MacroArguments args;
        for (size_t n = 0; n < line.statements[0].params.size(); ++n)
            args.text += (n ? "," : "") + line.statements[0].params[n];
        const std::string name = line.statements[0].mnemonic;
        ir::Line call = std::move(line);
        result.program.lines.pop_back();
        ir::Line head;
        head.sourceLine = number;
        head.label = call.label;
        head.comment = " macro " + name + " expanded" + (call.hasComment ? ";" + call.comment : std::string());
        head.hasComment = true;
        result.program.lines.push_back(std::move(head));
        for (const std::string& bodyLine : bodies[name])
            emit(Substitute(bodyLine, args), number, depth + 1);
    };

    uint32_t number = 0;
    std::string skipping;   // inside the definition of an expanded macro: written as comments
    for (const SourceLine& sourceLine : source.lines)
    {
        ++number;
        const auto [word, rest] = Command(sourceLine.text);
        if (skipping.empty() && word == "DEFMAC")
        {
            const std::string name = rest.substr(0, rest.find_first_of(" ;\t"));
            if (expanded.count(name))
                skipping = name;
        }
        if (!skipping.empty())
        {
            ir::Line comment;
            comment.sourceLine = number;
            comment.comment = " (expanded at its calls) " + sourceLine.text;
            comment.hasComment = true;
            result.program.lines.push_back(std::move(comment));
            if (word == "ENDMAC")
            {
                macros.insert(skipping);
                skipping.clear();
            }
            continue;
        }
        emit(sourceLine.text, number, 0);
    }
    // A name assigned with "=" somewhere may be defined with EQU too (TASM 4.12: SIN_ADR EQU ... then SIN_ADR=SIN_ADR+1):
    // every definition of it is redefinable
    std::set<std::string> redefined;
    for (const ir::Line& l : result.program.lines)
        for (const Statement& st : l.statements)
            if (st.kind == Statement::Kind::Directive && st.directive == ir::DirectiveKind::Defl && !l.label.empty())
                redefined.insert(l.label);
    for (ir::Line& l : result.program.lines)
        for (Statement& st : l.statements)
            if (st.kind == Statement::Kind::Directive && st.directive == ir::DirectiveKind::Equ && redefined.count(l.label))
                st.directive = ir::DirectiveKind::Defl;
    FlattenConditionals(result.program, result.diagnostics);
    return result;
}
}  // namespace unrealasm::dialects
