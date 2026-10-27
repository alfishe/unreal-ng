#include "dialects/alasm/alasmbackend.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <set>

#include "codecs/alasm/alasmtokens.h"
#include "dialects/common/diskname.h"
#include "dialects/common/z80.h"
#include "dialects/sjasmplus/sjasmplusfrontend.h"
#include "dialects/sjasmplus/sjasmpluslower.h"

namespace unrealasm::dialects
{
namespace
{
using ir::Expr;
using ir::Op;
using ir::Operand;
using ir::Statement;

/// "5.07" -> 507, "4.5" -> 450, "3.8" -> 380; "" (the newest) -> 999
int VersionNumber(const std::string& version)
{
    if (version.empty())
        return 999;
    const size_t dot = version.find('.');
    std::string minor = dot == std::string::npos ? std::string() : version.substr(dot + 1);
    if (minor.size() == 1)
        minor += "0";
    return std::atoi(version.substr(0, dot).c_str()) * 100 + std::atoi(minor.substr(0, 2).c_str());
}

// ALASM's words (its help, 5.0x): directives, pseudo-instructions, registers with the index halves, conditions. They
// are capitals; a lower-case "hl" or "ld" is a label to ALASM
const std::set<std::string> kKeywords = {
    "ORG", "EQU", "DB", "DEFB", "DEFM", "DW", "DEFW", "DS", "DEFS", "DD", "INCLUDE", "INCBIN", "IF0", "IF", "IFN", "ELSE",
    "ENDIF", "MACRO", "ENDM", "DUP", "EDUP", "REPEAT", "UNTIL0", "UNTIL", "DISP", "ENT", "LOCAL", "ENDL", "DISPLAY", "MAIN",
    "RUN", "ERASE", "STOP", "EXA", "EXD", "JZ", "JNZ", "JC", "JNC", "INF", "SLI",
    "A", "B", "C", "D", "E", "H", "L", "I", "R", "F", "AF", "BC", "DE", "HL", "SP", "IX", "IY", "HX", "LX", "HY", "LY",
    "NZ", "Z", "NC", "PO", "PE", "P", "M",
};

bool Reserved(const std::string& name)
{
    return name == z80::Upper(name) && (kKeywords.count(name) || z80::IsMnemonic(z80::Lower(name)));
}

/// A name ALASM reads as a label: a letter, "_" or "@" first, then letters, digits, "_" and "@"
bool IsAlasmLabel(const std::string& name)
{
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_' || name[0] == '@'))
        return false;
    for (const char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '@'))
            return false;
    return !Reserved(name);
}

/// The name ALASM gets for a source name it would not read as written (sjasmplus' main.loop, a?, a register name)
std::string Sanitized(const std::string& name)
{
    std::string out;
    for (const char c : name)
        out.push_back(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '@' ? c : '_');
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])) || Reserved(out))
        out = "L_" + out;
    return out;
}

/// The number of a temporary label's reference: 1 of 1B, 1F, 1_B, 1_F
std::string TemporaryNumber(const std::string& reference)
{
    if (reference.size() < 2)
        return {};
    std::string number = reference.substr(0, reference.size() - 1);
    if (number.size() > 1 && number.back() == '_')
        number.pop_back();
    return number;
}

bool IsTemporary(const std::string& label)
{
    return !label.empty() && label.find_first_not_of("0123456789") == std::string::npos;
}

bool IsDirective(const Statement& s, ir::DirectiveKind kind)
{
    return s.kind == Statement::Kind::Directive && s.directive == kind;
}

Expr Grouped(Expr e)
{
    Expr g = Expr::Make(Expr::Kind::Group);
    g.args.push_back(std::move(e));
    return g;
}

const Expr& Unwrapped(const Expr& e)
{
    return e.kind == Expr::Kind::Group ? Unwrapped(e.args[0]) : e;
}

// sjasmplus directives kept as text that make no bytes: dropped with a note
const std::set<std::string> kNoBytes = {
    "DEVICE", "SAVEBIN", "SAVESNA", "SAVETAP", "SAVEHOB", "SAVETRD", "SAVENEX", "SAVECPCSNA", "SAVECDT", "SAVEDEV",
    "EMPTYTAP", "EMPTYTRD", "OUTPUT", "OUTEND", "LABELSLIST", "CSPECTMAP", "BPLIST", "SETBP", "SETBREAKPOINT",
    "EXPORT", "TAPOUT", "TAPEND", "SHELLEXEC", "ASSERT", "SIZE", "DEFDEVICE",
};

/// The condition of an IF as ALASM tests it: IF0 (the block when the text is 0) or IFN (when it is not)
struct Test
{
    bool whenZero = false;
    std::string text;
    bool zeroOrOne = false;   ///< the text gives only 0 or 1
};

struct Writer
{
    const ir::Program& program;
    const BackendOptions& options;
    Diagnostics& diagnostics;
    int version = 999;
    bool sameDialect = false;
    uint32_t line = 0;
    size_t lineIndex = 0;
    std::string parent{};                             // sjasmplus: the last label without a dot (".x" hangs on it)
    std::map<std::string, std::string> names{};        // the source's full names -> ALASM names
    std::vector<std::map<std::string, std::string>> scopes{};   // LOCAL blocks written for macros and repeats
    std::vector<std::string> macroParams{};            // the macro being written: its named parameters (\0..\9)
    bool inMacro = false;
    std::set<std::string> redefinable{};               // names assigned with DEFL / "=" somewhere
    bool displaced = false;
    std::optional<int64_t> here{};                     // $ on the line being written, when its label gives it
    std::string problem{};                             // why ALASM cannot compute the expression being written
    bool strict = false;                               // every label must be known when the line is assembled (ORG, IF ...)
    std::map<std::string, std::vector<std::pair<size_t, std::string>>> temporaries{};   // sjasmplus "1": its definitions

    // ---- names ----

    std::string FullName(const std::string& text) const
    {
        if (sameDialect)
            return text;
        if (!text.empty() && text[0] == '@')
            return text.substr(1);
        if (!text.empty() && text[0] == '.')
            return parent + text;
        return text;
    }

    std::optional<std::string> Temporary(const std::string& reference) const
    {
        const std::string number = TemporaryNumber(reference);
        if (!IsTemporary(number))
            return std::nullopt;
        const char way = static_cast<char>(std::tolower(static_cast<unsigned char>(reference.back())));
        const auto found = temporaries.find(number);
        if (found == temporaries.end() || (way != 'b' && way != 'f'))
            return std::nullopt;
        std::optional<std::string> pick;
        for (const auto& [at, name] : found->second)
        {
            if (way == 'b' && at <= lineIndex)
                pick = name;
            if (way == 'f' && at > lineIndex)
                return name;
        }
        return pick;
    }

    std::string Name(const std::string& text) const
    {
        if (inMacro)
        {
            if (sameDialect && text.size() == 2 && (text[0] == '\\' || text[0] == ':'))
                return std::string(1, ParamMark()) + text[1];
            for (size_t k = 0; k < macroParams.size(); ++k)
                if (macroParams[k] == text)
                    return ParamMark() + std::to_string(k);
        }
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
        {
            const auto found = it->find(text);
            if (found != it->end())
                return found->second;
        }
        const auto found = names.find(FullName(text));
        if (found != names.end())
            return found->second;
        return IsAlasmLabel(text) || sameDialect ? text : Sanitized(FullName(text));
    }

    // ---- values (the source's arithmetic) ----

    int64_t Wrap(int64_t v) const
    {
        if (program.expressionBits == 16)
        {
            v &= 0xFFFF;
            return !program.unsignedArithmetic && v >= 0x8000 ? v - 0x10000 : v;
        }
        return static_cast<int32_t>(static_cast<uint32_t>(v & 0xFFFFFFFF));
    }

    int64_t True() const { return program.trueValue ? program.trueValue : -1; }

    const SourceLabel* Lookup(const std::string& text) const
    {
        if (inMacro || text.empty())
            return nullptr;
        const auto found = options.labelValues.find(FullName(text));
        return found == options.labelValues.end() ? nullptr : &found->second;
    }

    std::optional<int64_t> Value(const Expr& e) const
    {
        switch (e.kind)
        {
            case Expr::Kind::Number: return e.value;
            case Expr::Kind::Symbol:
            {
                const SourceLabel* l = Lookup(e.text);
                if (!l || l->redefinable || redefinable.count(FullName(e.text)))
                    return std::nullopt;
                return l->value;
            }
            case Expr::Kind::Current: return displaced ? std::nullopt : here;
            case Expr::Kind::Group: return Value(e.args[0]);
            case Expr::Kind::Unary:
            {
                if (e.op == Op::Exists)
                {
                    if (options.labelValues.empty() || e.args[0].kind != Expr::Kind::Symbol || inMacro)
                        return std::nullopt;
                    return options.labelValues.count(FullName(e.args[0].text)) ? True() : 0;
                }
                const auto a = Value(e.args[0]);
                if (!a)
                    return std::nullopt;
                switch (e.op)
                {
                    case Op::Negate: return Wrap(-*a);
                    case Op::Plus: return *a;
                    case Op::Not: return Wrap(~*a);
                    case Op::High: return (*a >> 8) & 0xFF;
                    case Op::Low: return *a & 0xFF;
                    case Op::LogicalNot: return *a == 0 ? True() : 0;
                    case Op::SwapBytes: return ((*a & 0xFF) << 8) | ((*a >> 8) & 0xFF);
                    default: return std::nullopt;
                }
            }

            case Expr::Kind::Binary:
            {
                const auto a = Value(e.args[0]);
                const auto b = Value(e.args[1]);
                if (!a || !b)
                    return std::nullopt;
                const bool unsignedWords = program.expressionBits == 16 && program.unsignedArithmetic;
                if (program.dialect == "sjasmplus" && (e.op == Op::Shl || e.op == Op::Shr || e.op == Op::ShrUnsigned))
                {
                    // its 32-bit value shifted with C++'s << / >>: the count modulo 32 as the machine takes it (1<<33 = 2)
                    const uint32_t u = static_cast<uint32_t>(*a & 0xFFFFFFFF);
                    const int n = static_cast<int>(*b & 31);
                    return Wrap(e.op == Op::Shl ? static_cast<int64_t>(u << n) : e.op == Op::Shr ? static_cast<int32_t>(u) >> n : static_cast<int64_t>(u >> n));
                }
                switch (e.op)
                {
                    case Op::Add: return Wrap(*a + *b);
                    case Op::Sub: return Wrap(*a - *b);
                    case Op::Mul: return Wrap(*a * *b);
                    case Op::Div: return *b == 0 ? std::nullopt : std::optional<int64_t>(Wrap(*a / *b));
                    case Op::Mod: return *b == 0 ? std::nullopt : std::optional<int64_t>(Wrap(*a % *b));
                    case Op::And: return Wrap(*a & *b);
                    case Op::Or: return Wrap(*a | *b);
                    case Op::Xor: return Wrap(*a ^ *b);
                    case Op::Shl: return *b < 0 || *b > 63 ? 0 : Wrap(*a << *b);
                    case Op::Shr: return *b < 0 || *b > 63 ? (*a < 0 && !unsignedWords ? -1 : 0) : Wrap(*a >> *b);
                    case Op::ShrUnsigned:
                        return *b < 0 || *b > 31 ? 0 : Wrap(static_cast<int64_t>((static_cast<uint64_t>(*a) & 0xFFFFFFFF) >> *b));
                    case Op::CyclicLeft16: case Op::CyclicRight16:
                    {
                        const uint32_t v = static_cast<uint32_t>(*a & 0xFFFF);
                        const int n = static_cast<int>(*b & 15);
                        const uint32_t r = e.op == Op::CyclicLeft16 ? (v << n) | (v >> (16 - n)) : (v >> n) | (v << (16 - n));
                        return r & 0xFFFF;
                    }
                    case Op::Equal: return *a == *b ? True() : 0;
                    case Op::NotEqual: return *a != *b ? True() : 0;
                    case Op::Less: return *a < *b ? True() : 0;
                    case Op::Greater: return *a > *b ? True() : 0;
                    case Op::LessEqual: return *a <= *b ? True() : 0;
                    case Op::GreaterEqual: return *a >= *b ? True() : 0;
                    case Op::LogicalAnd: return *a != 0 && *b != 0 ? True() : 0;
                    case Op::LogicalOr: return *a != 0 || *b != 0 ? True() : 0;
                    default: return std::nullopt;
                }
            }
            default: return std::nullopt;
        }
    }

    /// The label LabelValues gives a line without one (its address, for $), by its file and line
    std::string LineLabel(bool withFile) const
    {
        std::string file;
        if (withFile)
            for (const char c : options.fileName)
                file.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
        return "__UNREALASM_L" + file + "_" + std::to_string(line);
    }

    /// How often the WHILE on this line ran, as the layout gives it (LabelValues: __UNREALASM_W<file>_<line>)
    std::optional<int64_t> WhileCount() const
    {
        for (const bool withFile : {true, false})
        {
            const std::string key = "__UNREALASM_W" + LineLabel(withFile).substr(std::string("__UNREALASM_L").size());
            const auto found = options.labelValues.find(key);
            if (found != options.labelValues.end())
                return found->second.value;
        }
        return std::nullopt;
    }

    /// A label defined further down (or in another file): ALASM knows it only for + and -, after the pass
    bool Later(const std::string& text) const
    {
        const SourceLabel* l = Lookup(text);
        if (!l || l->redefinable)
            return false;
        if (!l->file.empty() && !options.fileName.empty() && l->file != options.fileName)
            return true;
        return l->line > line;
    }

    /// The value lies in 0..#FFFF, where ALASM's 16-bit unsigned division agrees with the source's
    bool InWord(const Expr& e) const
    {
        if (program.expressionBits == 16 && program.unsignedArithmetic)
            return true;
        const auto v = Value(e);
        return v && *v >= 0 && *v <= 0xFFFF;
    }

    // ---- printing ----

    static std::string Number(int64_t value)
    {
        const int64_t v = value & 0xFFFF;
        return v < 256 ? std::to_string(v) : "#" + z80::HexDigits(static_cast<uint64_t>(v), 4);
    }

    std::string NumberText(const Expr& e) const
    {
        const int64_t v = e.value & 0xFFFF;
        switch (e.spelling)
        {
            case ir::NumberSpelling::Hex: return "#" + z80::HexDigits(static_cast<uint64_t>(v), std::clamp(e.digits, 1, 4));
            case ir::NumberSpelling::Binary:
            {
                std::string bits;
                for (int k = std::clamp(e.digits, 1, 16) - 1; k >= 0; --k)
                    bits.push_back(((v >> k) & 1) ? '1' : '0');
                return "%" + bits;
            }
            case ir::NumberSpelling::Character:
                if (e.text.size() == 1 && e.text[0] >= 0x20 && e.text[0] < 0x7F && e.text[0] != '"')
                    return "\"" + e.text + "\"";
                return std::to_string(v);
            default: return std::to_string(v);
        }
    }

    static bool Compound(const Expr& e)
    {
        return e.kind == Expr::Kind::Binary ||
               (e.kind == Expr::Kind::Unary && (e.op == Op::Not || e.op == Op::Negate || e.op == Op::Plus || e.op == Op::SwapBytes ||
                                                e.op == Op::Exists || e.op == Op::LogicalNot));
    }

    /// An operand of a prefix operator or the right side of a binary one: compound expressions in parentheses
    std::string Term(const Expr& e, bool linear)
    {
        const std::string s = Print(e, linear);
        return Compound(e) ? Parenthesized(s) : s;
    }

    /// Text in parentheses; ALASM 3.8 has none in expressions (an operand starting with "(" is memory, live)
    std::string Parenthesized(const std::string& s)
    {
        if (version < 400)
            Problem("parentheses (ALASM 3.8 has none)");
        return "(" + s + ")";
    }

    void Problem(const std::string& why)
    {
        if (problem.empty())
            problem = why;
    }

    /// ALASM text computing the source's value modulo #10000, left to right; `linear`: only + and - lead here from
    /// the top (a later label is allowed there). Sets `problem` where ALASM cannot compute it so
    std::string Print(const Expr& e, bool linear)
    {
        switch (e.kind)
        {
            case Expr::Kind::Number: return NumberText(e);
            case Expr::Kind::Symbol:
                if (program.dialect == "sjasmplus" && (e.text == "relocate_count" || e.text == "relocate_size"))
                    if (const auto v = Value(e))
                        return Number(*v);   // sjasmplus' built-in names: ALASM has none
                if ((strict || !linear) && Later(e.text))
                    Problem("the label " + e.text + " is defined further down");
                return Name(e.text);
            case Expr::Kind::Current: return "$";
            case Expr::Kind::CurrentPage: return "$$";
            case Expr::Kind::CurrentPhysical:
                if (displaced)
                    Problem("the address where the code is put ($$$) during DISP");
                return "$";
            case Expr::Kind::Group: return Parenthesized(Print(e.args[0], linear));
            case Expr::Kind::Memory: return "{" + Print(e.args[0], false) + "}";
            case Expr::Kind::Defined: return "?" + Term(e.args[0], false);
            case Expr::Kind::Raw:
            {
                if (const auto temporary = Temporary(e.text))
                    return *temporary;
                if (e.text.size() == 2 && e.text[0] == '/')
                    return e.text;   // a DISPLAY format key
                Problem("unparsed text " + e.text);
                return e.text;
            }
            case Expr::Kind::Unary:
                switch (e.op)
                {
                    case Op::Negate: return "-" + Term(e.args[0], linear);
                    case Op::Plus: return Print(e.args[0], linear);
                    case Op::Not:
                        if (version < 400)
                            Problem("the operator ~ (ALASM 3.8 has + - * / only)");
                        return Print(e.args[0], false) + "~";   // inverts the result so far
                    case Op::High: return "'" + Term(e.args[0], false);
                    case Op::Low: return "." + Term(e.args[0], false);
                    case Op::SwapBytes:
                        return Print(Expr::Binary(Op::Add, Expr::Unary(Op::High, e.args[0]),
                                                  Grouped(Expr::Binary(Op::Mul, Expr::Unary(Op::Low, e.args[0]), Expr::Number(256)))),
                                     false);
                    case Op::LogicalNot: Problem("a logical not"); return "?";
                    case Op::Exists: Problem("exist (ALASM's ?label tells defined from referenced)"); return "?" + Term(e.args[0], false);
                    default: Problem("an operator ALASM lacks"); return Term(e.args[0], false);
                }
            case Expr::Kind::Binary: return Binary(e, linear);
        }
        return "?";
    }

    std::string Binary(const Expr& e, bool linear)
    {
        const Expr& a = e.args[0];
        const Expr& b = e.args[1];
        auto joined = [&](const char* symbol, bool keepsLinear) {
            if (version < 400 && std::string("+-*/").find(symbol[0]) == std::string::npos)
                Problem(std::string("the operator ") + symbol + " (ALASM 3.8 has + - * / only)");
            const std::string left = Print(a, linear && keepsLinear);
            return left + symbol + Term(b, linear && keepsLinear);
        };
        const auto shift = Value(b);
        switch (e.op)
        {
            case Op::Add: return joined("+", true);
            case Op::Sub: return joined("-", true);
            case Op::Mul: return joined("*", false);
            case Op::And: return joined("&", false);
            case Op::Or: return joined("|", false);
            case Op::Xor: return joined("!", false);
            case Op::CyclicLeft16: return joined("<", false);
            case Op::CyclicRight16: return joined(">", false);
            case Op::Div:
                if (!InWord(a) || !InWord(b) || Value(b) == 0)
                    Problem("a division ALASM makes in 16-bit unsigned words");
                return joined("/", false);
            case Op::Shl:
                if (b.kind != Expr::Kind::Number || shift < 0 || shift > 15)
                {
                    Problem("a shift by a count that is no number");
                    return joined("*", false);
                }
                return Print(a, false) + "*" + Number(int64_t{1} << *shift);
            case Op::Shr: case Op::ShrUnsigned:
                if (b.kind != Expr::Kind::Number || shift < 0 || shift > 15)
                {
                    Problem("a shift by a count that is no number");
                    return joined("/", false);
                }
                if (!InWord(a))
                    Problem("a right shift of a value outside 0..#FFFF");
                return Print(a, false) + "/" + Number(int64_t{1} << *shift);
            case Op::Mod:
            {
                if (!InWord(a) || !InWord(b) || Value(b) == 0)
                    Problem("a remainder ALASM makes in 16-bit unsigned words");
                const std::string left = Print(a, false);
                const std::string right = Term(b, false);
                return left + "-" + Parenthesized(left + "/" + right + "*" + right);
            }
            default:
                Problem("a comparison or logical operator (ALASM has none)");
                return joined("-", false);
        }
    }

    /// An expression as ALASM computes it; written as its value where ALASM cannot compute it
    std::string Expression(const Expr& e)
    {
        problem.clear();
        const std::string text = Print(e, true);
        if (problem.empty())
            return text;
        if (const auto value = Value(e))
        {
            diagnostics.push_back({Severity::Info, line, 0, "written as its value " + Number(*value) + ": " + problem});
            return Number(*value);
        }
        diagnostics.push_back({Severity::Warning, line, 0, "ALASM may compute this differently: " + problem});
        return text;
    }

    /// An expression that must be known when ALASM reaches the line (ORG, EQU, DS, DUP, IF ...)
    std::string Known(const Expr& e)
    {
        strict = true;
        const std::string text = Expression(e);
        strict = false;
        return text;
    }

    Test Condition(const Expr& condition)
    {
        const Expr& e = Unwrapped(condition);
        strict = true;
        Test t;
        if (e.kind == Expr::Kind::Unary && e.op == Op::LogicalNot)
        {
            t = Condition(e.args[0]);
            t.whenZero = !t.whenZero;
        }
        else if (e.kind == Expr::Kind::Unary && e.op == Op::Exists && e.args[0].kind == Expr::Kind::Symbol)
            t = {true, "?" + Name(e.args[0].text), false};   // ?label: 0 when it is defined
        else if (e.kind == Expr::Kind::Binary && (e.op == Op::Equal || e.op == Op::NotEqual))
        {
            const Expr& a = e.args[0];
            const Expr& b = e.args[1];
            const auto av = Value(a), bv = Value(b);
            if (av && bv && *av != *bv && ((*av - *bv) & 0xFFFF) == 0)
                t = {false, std::string((*av == *bv) == (e.op == Op::Equal) ? "1" : "0"), true};   // equal only in 16 bits
            else if (b.kind == Expr::Kind::Number && b.value == 0)
                t = {e.op == Op::Equal, Expression(a), false};
            else if (a.kind == Expr::Kind::Number && a.value == 0)
                t = {e.op == Op::Equal, Expression(b), false};
            else
                t = {e.op == Op::Equal, Expression(Expr::Binary(Op::Sub, a, b)), false};
        }
        else if (e.kind == Expr::Kind::Binary && (e.op == Op::Less || e.op == Op::Greater || e.op == Op::LessEqual || e.op == Op::GreaterEqual))
        {
            // a<b: (a-b+#8000)/#8000 is 0 when a-b lies in -#8000..-1 and 1 in 0..#7FFF, ALASM's unsigned division
            // agreeing with the source's while a-b+#8000 lies in 0..#FFFF (checked with the values where known)
            const bool swap = e.op == Op::Greater || e.op == Op::LessEqual;
            const Expr& a = swap ? e.args[1] : e.args[0];
            const Expr& b = swap ? e.args[0] : e.args[1];
            const Expr test = Expr::Binary(Op::Div, Expr::Binary(Op::Add, Expr::Binary(Op::Sub, a, b), Expr::Number(0x8000, ir::NumberSpelling::Hex, 4)),
                                           Expr::Number(0x8000, ir::NumberSpelling::Hex, 4));
            t = {e.op == Op::Less || e.op == Op::Greater, Expression(test), true};
        }
        else
        {
            problem.clear();
            const std::string text = Print(e, true);
            if (problem.empty())
                t = {false, text, false};
            else if (const auto value = Value(e))
            {
                diagnostics.push_back({Severity::Info, line, 0, "condition written as its value: " + problem});
                t = {false, *value != 0 ? "1" : "0", true};
            }
            else
            {
                diagnostics.push_back({Severity::Warning, line, 0, "ALASM cannot test this condition: " + problem});
                t = {false, text, false};
            }
        }
        strict = false;
        return t;
    }

    std::string If0() const { return version < 503 ? "IF" : "IF0"; }   // IF before 5.03 is 5.03's IF0 (the same code)
    std::string Until0() const { return version < 507 ? "UNTIL" : "UNTIL0"; }
    std::string Db() const { return version < 400 ? "DEFB" : "DB"; }
    std::string Dw() const { return version < 400 ? "DEFW" : "DW"; }
    std::string Ds() const { return version < 400 ? "DEFS" : "DS"; }   // 3.8 has no DS (live)
    char ParamMark() const { return version < 500 ? ':' : '\\'; }   // macro parameters: :0..:9 before 5.0 (4.2-4.5, live)
    /// Whether the target version's table has this keyword (4.5 lacks EXA that 4.44 has: the numbers are no order)
    bool HasKeyword(std::string_view word) const
    {
        const codecs::alasm::Version* v = codecs::alasm::FindVersion(options.targetVersion);
        return !v || std::find(v->mnemonics.begin(), v->mnemonics.end(), word) != v->mnemonics.end();
    }

    std::string IfText(const Test& t) const { return (t.whenZero ? If0() : std::string("IFN")) + " " + t.text; }

    /// A string as ALASM writes it: a quote inside doubled; control characters as numbers
    static std::string StringItems(const std::string& text)
    {
        std::string out, run;
        auto flush = [&]() {
            if (!run.empty())
                out += (out.empty() ? "" : ",") + ("\"" + run + "\"");
            run.clear();
        };
        for (const char c : text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u == 0x7F)
            {
                flush();
                out += (out.empty() ? "" : ",") + std::to_string(u);
            }
            else if (c == '"')
                run += "\"\"";
            else
                run.push_back(c);
        }
        flush();
        return out.empty() ? std::string("\"\"") : out;
    }

    std::string OperandText(const Operand& o, bool instruction)
    {
        switch (o.kind)
        {
            case Operand::Kind::Register:
            {
                const std::string r = o.text;
                if (r == "ixh") return "HX";
                if (r == "ixl") return "LX";
                if (r == "iyh") return "HY";
                if (r == "iyl") return "LY";
                return r == "af'" ? "AF'" : z80::Upper(r);
            }
            case Operand::Kind::Condition: return z80::Upper(o.text);
            case Operand::Kind::Indirect: return "(" + z80::Upper(o.text) + ")";
            case Operand::Kind::Indexed:
            {
                const std::string d = Expression(o.expr);
                return "(" + z80::Upper(o.text) + (d.empty() || d[0] == '+' || d[0] == '-' ? d : "+" + d) + ")";
            }
            case Operand::Kind::Memory: return "(" + Expression(o.expr) + ")";
            case Operand::Kind::Immediate:
            {
                // ALASM reads an operand starting with "(" as memory: a value written so gets "0+"
                const std::string v = Expression(o.expr);
                return instruction && !v.empty() && v[0] == '(' ? "0+" + v : v;
            }
            case Operand::Kind::String: return StringItems(o.text);
        }
        return {};
    }

    std::string Operands(const std::vector<Operand>& operands, bool instruction = false)
    {
        std::string out;
        for (const Operand& o : operands)
            out += (out.empty() ? "" : ",") + OperandText(o, instruction);
        return out;
    }

    std::string NotConverted(const std::string& what)
    {
        diagnostics.push_back({Severity::Warning, line, 0, "not converted: " + what});
        return ";unreal-asm: not converted: " + what;
    }

    /// Data items split over lines ALASM's records hold (254 bytes a line)
    std::vector<std::string> DataLines(const std::string& keyword, const std::vector<Operand>& operands)
    {
        std::vector<std::string> out;
        std::string current;
        for (const Operand& o : operands)
        {
            const std::string item = OperandText(o, false);
            if (!current.empty() && current.size() + item.size() > 100)
            {
                out.push_back(keyword + " " + current);
                current.clear();
            }
            current += (current.empty() ? "" : ",") + item;
        }
        out.push_back(keyword + " " + current);
        return out;
    }

    /// An argument of a macro call of another dialect: read as an operand by the source's frontend, written as ALASM
    std::string Argument(const std::string& text)
    {
        const IFrontend* frontend = DialectRegistry::Builtin().Frontend(program.dialect);
        if (!frontend || text.empty())
            return text;
        // a string stays a string (a macro's DB takes it so: "DB \0" with "TEXT")
        {
            const FrontendResult data = frontend->Parse(SourceDocument::FromText("        DB " + text, program.dialect));
            if (!data.program.lines.empty() && data.program.lines[0].statements.size() == 1)
            {
                const Statement& d = data.program.lines[0].statements[0];
                if (d.operands.size() == 1 && d.operands[0].kind == Operand::Kind::String && d.operands[0].text.size() >= 2 &&
                    d.operands[0].text.find('"') == std::string::npos)
                    return "\"" + d.operands[0].text + "\"";
            }
        }
        FrontendResult parsed = frontend->Parse(SourceDocument::FromText("        INC " + text, program.dialect));
        if (parsed.program.lines.empty() || parsed.program.lines[0].statements.size() != 1)
            return text;
        const Statement& s = parsed.program.lines[0].statements[0];
        if (s.kind != Statement::Kind::Instruction || s.operands.size() != 1)
            return text;
        return OperandText(s.operands[0], true);
    }

    std::vector<std::string> InstructionText(const Statement& s)
    {
        const std::string& m = s.mnemonic;
        const auto& ops = s.operands;
        auto reg = [&](size_t k, const char* name) { return k < ops.size() && ops[k].kind == Operand::Kind::Register && ops[k].text == name; };
        if (m == "ex" && reg(0, "af") && reg(1, "af'"))
            return {HasKeyword("EXA") ? "EXA" : "EX AF,AF'"};   // EXA is no keyword before 4.44 / in 4.5 (4.2 skips the line)
        if (m == "in" && (reg(0, "f") || (ops.size() == 1 && ops[0].kind == Operand::Kind::Indirect)))
            return {"INF"};
        // A Z80N instruction (a source for the ZX Spectrum Next): ALASM has none, so its bytes
        if (program.z80n)
            if (const auto next = z80::Z80nEncoding(s))
            {
                std::string items = "#ED," + Number(next->code);
                const auto byte = [&](const Expr& e) {
                    const auto v = Value(e);
                    return v ? Number(*v & 0xFF) : Expression(e);
                };
                if (next->layout == 'b' || next->layout == 'B')
                {
                    items += "," + byte(ops[0].expr);
                    if (next->layout == 'B')
                        items += "," + byte(ops[1].expr);
                }
                else if (next->layout == 'w' || next->layout == 'W')
                {
                    const auto v = Value(ops.back().expr);
                    if (!v)
                        return {NotConverted("a Z80N word known only when assembling: " + z80::Upper(m))};
                    items += "," + Number(next->layout == 'w' ? *v & 0xFF : (*v >> 8) & 0xFF) + "," + Number(next->layout == 'w' ? (*v >> 8) & 0xFF : *v & 0xFF);
                }
                return {Db() + " " + items + "   ;" + z80::Upper(m) + (ops.empty() ? std::string() : " " + Operands(ops, true))};
            }
        // The undocumented copies (RLC (IX+d),B; SET 3,(IY+d),A): ALASM reads the register as a second instruction,
        // so their bytes go as data
        {
            static const std::map<std::string, int> kShifts = {{"rlc", 0x00}, {"rrc", 0x08}, {"rl", 0x10}, {"rr", 0x18},
                                                                {"sla", 0x20}, {"sra", 0x28}, {"sll", 0x30}, {"sli", 0x30}, {"srl", 0x38}};
            static const std::map<std::string, int> kRegisters = {{"b", 0}, {"c", 1}, {"d", 2}, {"e", 3}, {"h", 4}, {"l", 5}, {"a", 7}};
            const auto shift = kShifts.find(m);
            const bool bitOp = (m == "res" || m == "set") && ops.size() == 3;
            const size_t at = bitOp ? 1 : 0;
            if ((shift != kShifts.end() && ops.size() == 2) || bitOp)
                if (ops[at].kind == Operand::Kind::Indexed && ops[at + 1].kind == Operand::Kind::Register && kRegisters.count(ops[at + 1].text))
                {
                    int code = kRegisters.at(ops[at + 1].text);
                    if (bitOp)
                    {
                        const auto bit = Value(ops[0].expr);
                        if (!bit)
                            return {NotConverted("an undocumented copy with a bit known only when assembling")};
                        code |= (m == "res" ? 0x80 : 0xC0) | static_cast<int>((*bit & 7) << 3);
                    }
                    else
                        code |= shift->second;
                    return {Db() + " " + std::string(ops[at].text == "ix" ? "#DD" : "#FD") + ",#CB," + Expression(ops[at].expr) + "," + Number(code) +
                            "   ;" + z80::Upper(m) + " " + Operands(ops, true)};
                }
        }
        if (m == "sll" || m == "sli")
            return {"SLI " + Operands(ops, true)};
        if (program.dialect == "sjasmplus")
        {
            // sjasmplus repeats an instruction for its operands (PUSH AF,BC; SUB A,B is SUB A : SUB B; LD A,1,B,2)
            if (const std::vector<Statement> parts = SjasmplusRepeated(s); parts.size() > 1)
            {
                std::vector<std::string> out;
                for (const Statement& one : parts)
                    for (const std::string& t : InstructionText(one))
                        out.push_back(t);
                return out;
            }
            // and its fake instructions, written out as it assembles them
            if (const std::vector<Statement> parts = SjasmplusFake(s); parts.size() > 1)
            {
                std::vector<std::string> out;
                for (const Statement& one : parts)
                    for (const std::string& t : InstructionText(one))
                        out.push_back(t);
                return out;
            }
        }
        // AND A,n of another dialect means AND n; ALASM reads A,n as two operand groups (AND A, then AND n)
        if (program.dialect != "sjasmplus" && ops.size() == 2 && reg(0, "a") && (m == "and" || m == "or" || m == "xor" || m == "cp" || m == "sub"))
            return {z80::Upper(m) + " " + OperandText(ops[1], true)};
        // ADD / ADC / SBC with one operand mean A,x elsewhere; ALASM 5.09 puts nothing down for ADD B (seen in unreal-ng)
        if (ops.size() == 1 && (m == "add" || m == "adc" || m == "sbc") && ops[0].kind != Operand::Kind::Condition)
            return {z80::Upper(m) + " A," + OperandText(ops[0], true)};
        const std::string text = Operands(ops, true);
        return {z80::Upper(m) + (text.empty() ? "" : " " + text)};
    }

    std::vector<std::string> StatementText(const Statement& s, const std::string& label)
    {
        switch (s.kind)
        {
            case Statement::Kind::Raw: return {NotConverted("unparsed line: " + s.text)};
            case Statement::Kind::Instruction: return InstructionText(s);
            case Statement::Kind::MacroCall:
            {
                std::string args;
                for (size_t k = 0; k < s.params.size(); ++k)
                {
                    std::string arg;
                    if (sameDialect && k < s.operands.size() && s.operands[k].expr.kind != Expr::Kind::Raw)
                        arg = OperandText(s.operands[k], false);
                    else if (sameDialect)
                        arg = s.params[k];
                    else
                        arg = Argument(s.params[k]);
                    args += (k ? "," : "") + arg;
                }
                return {Name(s.mnemonic) + (args.empty() ? "" : " " + args)};
            }
            case Statement::Kind::Directive: break;
        }
        auto args = [&](size_t from) {
            std::string out;
            for (size_t k = from; k < s.args.size(); ++k)
                out += (out.empty() ? "" : ",") + Known(s.args[k]);
            return out;
        };
        switch (s.directive)
        {
            case ir::DirectiveKind::Org: return {"ORG " + args(0)};
            case ir::DirectiveKind::Equ:
                if (redefinable.count(FullName(label)))
                    return {"=" + Known(s.args[0])};
                return {"EQU " + Known(s.args[0])};
            case ir::DirectiveKind::Defl: return {"=" + Known(s.args[0])};
            case ir::DirectiveKind::Db: return DataLines(Db(), s.operands);
            case ir::DirectiveKind::Dw:
            {
                // A string of one or two characters in DW is a word to the other dialects (sjasmplus 'AB' = #4142);
                // ALASM 5.09 puts DW "AB" down as the characters (41 42, seen in unreal-ng): written as the number
                std::vector<Operand> words = s.operands;
                for (Operand& o : words)
                    if (o.kind == Operand::Kind::String && o.text.size() <= 2)
                    {
                        int64_t value = 0;
                        for (const char c : o.text)
                            value = (value << 8) | static_cast<unsigned char>(c);
                        o.kind = Operand::Kind::Immediate;
                        o.expr = Expr::Number(value);
                    }
                    else if (o.kind == Operand::Kind::Immediate && o.expr.kind == Expr::Kind::Number && o.expr.spelling == ir::NumberSpelling::Character)
                        o.expr.spelling = ir::NumberSpelling::Decimal;
                return DataLines(Dw(), words);
            }
            case ir::DirectiveKind::Ds:
            {
                if (std::find(s.params.begin(), s.params.end(), "align") != s.params.end() && here && displaced && !s.args.empty())
                {
                    // sjasmplus' ALIGN in a displacement: its count from the displaced address the line has there
                    displaced = false;
                    const auto count = Value(s.args[0]);
                    displaced = true;
                    if (count)
                    {
                        Statement counted = s;
                        counted.args[0] = Expr::Number(*count);
                        counted.params.clear();
                        return StatementText(counted, label);
                    }
                }
                // ALASM's DS n,a,b repeats the pattern a,b n times; STORM's cyclic fill cuts it at n bytes
                if (s.operands.size() > 1 && !s.params.empty() && s.params[0] == "cyclic")
                {
                    const auto count = Value(s.args[0]);
                    if (!count)
                        return {NotConverted("a cyclic fill of a count known only when assembling")};
                    const int64_t k = static_cast<int64_t>(s.operands.size());
                    std::vector<std::string> out;
                    if (*count / k > 0)
                        out.push_back(Ds() + " " + std::to_string(*count / k) + "," + Operands(s.operands));
                    if (*count % k > 0)
                        out.push_back(Db() + " " + Operands(std::vector<Operand>(s.operands.begin(), s.operands.begin() + *count % k)));
                    return out;
                }
                if (!s.operands.empty())
                    return {Ds() + " " + Known(s.args[0]) + "," + Operands(s.operands)};
                return {Ds() + " " + args(0)};
            }
            case ir::DirectiveKind::Include:
            {
                if (const auto as = options.includeAs.find(s.text); as != options.includeAs.end())
                    return {"INCLUDE " + as->second};
                std::string name = DiskName(options, s.text);
                const size_t colon = name.find(':');
                const std::string drive = colon == std::string::npos ? std::string() : name.substr(0, colon + 1);
                if (colon != std::string::npos)
                    name = name.substr(colon + 1);
                if (name.size() > 8 && name.find_first_of("*?") == std::string::npos)
                {
                    diagnostics.push_back({Severity::Warning, line, 0, "INCLUDE \"" + name + "\": a TR-DOS name has 8 characters"});
                    name.resize(8);
                }
                return {"INCLUDE \"" + drive + name + "\"" + (s.args.empty() ? "" : "," + args(0))};
            }
            case ir::DirectiveKind::Incbin:
            {
                if (!s.args.empty() && !(s.args[0].kind == Expr::Kind::Number && s.args[0].value == 0))
                    return {NotConverted("INCBIN from an offset (ALASM takes a length only): " + s.text)};
                return {"INCBIN \"" + DiskName(options, s.text) + "\"" + (s.args.size() > 1 ? "," + Known(s.args[1]) : std::string())};
            }
            case ir::DirectiveKind::If: return {IfText(Condition(s.args[0]))};
            case ir::DirectiveKind::Else: return {"ELSE"};
            case ir::DirectiveKind::EndIf: return {"ENDIF"};
            case ir::DirectiveKind::Repeat: return {"DUP " + Known(s.args[0])};
            case ir::DirectiveKind::EndRepeat: return {"EDUP"};
            case ir::DirectiveKind::While:
            {
                if (!HasKeyword("REPEAT"))
                {
                    // no REPEAT before 4.43 (3.8, 4.2, 4.42): DUP of the count the layout ran it (none: not converted)
                    const auto count = WhileCount();
                    whileTests.push_back({false, count ? std::to_string(*count) : std::string(), false});
                    if (!count)
                        return {NotConverted("a WHILE (ALASM before 4.43 has no REPEAT) whose runs the layout does not give")};
                    return {*count ? "DUP " + std::to_string(*count) : "IFN 0"};
                }
                // WHILE c tests first; ALASM's REPEAT ... UNTIL0 x runs the body until x is 0: IF around it
                const Test t = Condition(s.args[0]);
                whileTests.push_back(t);
                return {IfText(t), "REPEAT"};
            }
            case ir::DirectiveKind::EndWhile:
            {
                if (whileTests.empty())
                    return {NotConverted("ENDW without WHILE")};
                const Test t = whileTests.back();
                whileTests.pop_back();
                if (!HasKeyword("REPEAT") && t.text.empty())
                    return {};
                if (!HasKeyword("REPEAT"))
                    return {t.text == "0" ? "ENDIF" : "EDUP"};
                if (!t.whenZero)
                    return {Until0() + " " + t.text, "ENDIF"};
                if (t.zeroOrOne)
                    return {Until0() + " " + t.text + "-1", "ENDIF"};   // 0 when the test is true: -1 turns it nonzero
                return {NotConverted("a WHILE whose test ALASM gives as 0 for true"), "ENDIF"};
            }
            case ir::DirectiveKind::RepeatUntil: return {"REPEAT"};
            case ir::DirectiveKind::UntilZero: return {Until0() + " " + Known(s.args[0])};
            case ir::DirectiveKind::Disp: displaced = true; return {"DISP " + Known(s.args[0])};
            case ir::DirectiveKind::Ent:
                if (s.text == "if-displaced" && !displaced)
                    return {};
                displaced = false;
                return {"ENT"};
            case ir::DirectiveKind::Display: return {"DISPLAY " + Operands(s.operands)};
            case ir::DirectiveKind::Main: return {"MAIN \"" + s.text + "\"" + (s.args.empty() ? "" : "," + args(0))};
            case ir::DirectiveKind::Run: return {"RUN " + args(0)};
            case ir::DirectiveKind::End:
                diagnostics.push_back({Severity::Info, line, 0, "END dropped (ALASM assembles the whole text)"});
                return {";END"};
            case ir::DirectiveKind::SaveBinary:
                diagnostics.push_back({Severity::Info, line, 0, "SAVEBIN dropped: save the bytes from ALASM"});
                return {";SAVEBIN \"" + s.text + "\""};
            case ir::DirectiveKind::LocalBlock: return {"LOCAL"};
            case ir::DirectiveKind::EndLocalBlock: return {"ENDL"};
            case ir::DirectiveKind::Other:
            {
                if (sameDialect)
                    return {s.text};
                const std::string word = z80::Upper(s.text.substr(0, s.text.find_first_of(" \t")));
                const size_t at = s.text.find_first_not_of(" \t", word.size());
                const std::string rest = at == std::string::npos ? std::string() : s.text.substr(at);
                const std::string flag = rest.substr(0, rest.find_first_of(" \t"));
                if (word == kModuleMark || word == kEndModuleMark)
                    return {};   // the names are full already (sjasmpluslower.h)
                // sjasmplus' DEFINE flags, tested with IFDEF in the order of the text: decided here
                if (word == "DEFINE" || word == "DEFINE+" || word == "UNDEFINE")
                {
                    if (word == "UNDEFINE")
                        defines.erase(flag);
                    else
                        defines.insert(flag);
                    if (word != "UNDEFINE" && flag.size() < rest.size())
                        diagnostics.push_back({Severity::Info, line, 0, "a DEFINE with a value: put in where its name stands (by the frontend)"});
                    return {";" + s.text};
                }
                if (word == "IFDEF" || word == "IFNDEF")
                {
                    const bool taken = defines.count(flag) == (word == "IFDEF" ? 1u : 0u);
                    return {std::string("IFN ") + (taken ? "1" : "0") + "   ;" + s.text};
                }
                if (word == "RELOCATE_START" || word == "RELOCATE_END")
                    return {";" + s.text};   // the table comes from the layout
                if (word == "RELOCATE_TABLE")
                {
                    // sjasmplus' relocation data: the addresses the layout found, each minus the offset given
                    int64_t subtract = 0;
                    if (!rest.empty())
                    {
                        const FrontendResult parsed = SjasmplusFrontend().Parse(SourceDocument::FromText("        DW " + rest, "sjasmplus"));
                        if (!parsed.program.lines.empty() && !parsed.program.lines[0].statements.empty() &&
                            !parsed.program.lines[0].statements[0].operands.empty())
                        {
                            const auto v = Value(parsed.program.lines[0].statements[0].operands[0].expr);
                            if (!v)
                                return {NotConverted("RELOCATE_TABLE with an offset known only when assembling")};
                            subtract = *v;
                        }
                    }
                    std::vector<Operand> items;
                    for (size_t k = 0;; ++k)
                    {
                        const auto found = options.labelValues.find("__UNREALASM_RELOC_" + std::to_string(k));
                        if (found == options.labelValues.end())
                            break;
                        Operand o;
                        o.kind = Operand::Kind::Immediate;
                        o.expr = Expr::Number((found->second.value - subtract) & 0xFFFF, ir::NumberSpelling::Hex, 4);
                        items.push_back(std::move(o));
                    }
                    if (items.empty())
                        return {";" + s.text + " (no relocation data)"};
                    return DataLines(Dw(), items);
                }
                if (kNoBytes.count(word))
                {
                    diagnostics.push_back({Severity::Info, line, 0, word + " dropped (it makes no bytes)"});
                    return {";" + s.text};
                }
                return {NotConverted(s.text)};
            }
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }

    std::vector<Test> whileTests{};
    std::set<std::string> defines{};
};

/// Whether the block opened at `first` (MACRO or DUP) defines labels before its end: written inside LOCAL ... ENDL.
/// `addresses`: only labels of addresses count (a name assigned with = / EQU in a DUP is the one global name)
bool DefinesLabels(const ir::Program& program, size_t first, ir::DirectiveKind open, ir::DirectiveKind close, bool addresses = false)
{
    int depth = 0;
    for (size_t k = first; k < program.lines.size(); ++k)
    {
        const ir::Line& l = program.lines[k];
        const bool assigns = std::any_of(l.statements.begin(), l.statements.end(), [](const Statement& s) {
            return IsDirective(s, ir::DirectiveKind::Equ) || IsDirective(s, ir::DirectiveKind::Defl);
        });
        if (k > first && !l.label.empty() && !(addresses && assigns))
            return true;
        for (const Statement& s : l.statements)
        {
            if (IsDirective(s, open))
                ++depth;
            if (IsDirective(s, close) && --depth == 0)
                return false;
        }
    }
    return false;
}
}  // namespace

namespace
{
/// A comment in what ALASM's CP866 holds: other characters as "?"
std::string InCp866(const std::string& utf8)
{
    std::string out;
    for (size_t p = 0; p < utf8.size();)
    {
        const unsigned char b = static_cast<unsigned char>(utf8[p]);
        const size_t length = std::min(utf8.size() - p, static_cast<size_t>(b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1));
        const std::string one = utf8.substr(p, length);
        std::vector<uint8_t> bytes;
        std::string error;
        out += encoding::FromUtf8(one, encoding::CodePage::Cp866, bytes, error) ? one : std::string("?");
        p += length;
    }
    return out;
}
}  // namespace

BackendResult AlasmBackend::Write(const ir::Program& given, const BackendOptions& options) const
{
    BackendResult result;
    // sjasmplus' MODULE, STRUCT ... in words ALASM has (sjasmpluslower.h)
    const ir::Program lowered = given.dialect == "sjasmplus" ? LowerSjasmplus(given, result.diagnostics, true, VersionNumber(options.targetVersion) < 400) : ir::Program{};
    const ir::Program& program = given.dialect == "sjasmplus" ? lowered : given;
    result.document.dialect = "alasm";
    result.document.codePage = encoding::CodePage::Cp866;
    Writer w{program, options, result.diagnostics};
    w.version = VersionNumber(options.targetVersion);
    w.sameDialect = program.dialect == "alasm";

    // Names: every label under the ALASM name its source name gets (sjasmplus' main.loop -> main_loop, a register name
    // renamed), unique; temporary labels (sjasmplus 1, 1B / 1F) numbered
    {
        std::set<std::string> taken;
        for (const ir::Line& l : program.lines)
            if (!l.label.empty())
                taken.insert(l.label);
        std::string parent;
        std::map<std::string, int> temporaryCount;
        bool inMacro = false;
        for (size_t k = 0; k < program.lines.size(); ++k)
        {
            const ir::Line& l = program.lines[k];
            for (const Statement& s : l.statements)
                if (IsDirective(s, ir::DirectiveKind::Macro))
                    inMacro = true;
            if (!l.label.empty() && !w.sameDialect)
            {
                if (IsTemporary(l.label))
                {
                    std::string name = "_T" + l.label + "_" + std::to_string(++temporaryCount[l.label]);
                    while (taken.count(name))
                        name += "_";
                    taken.insert(name);
                    w.temporaries[l.label].push_back({k, name});
                }
                else if (!(inMacro && l.label[0] == '.'))
                {
                    const bool keeps = l.label[0] == '!';   // sjasmplus' !label: not the parent of the .labels after it
                    const std::string own = keeps ? l.label.substr(1) : l.label;
                    std::string full = own[0] == '@' ? own.substr(1) : own[0] == '.' ? parent + own : own;
                    if (own[0] != '.' && !keeps)
                        parent = full;
                    if (!w.names.count(full))
                    {
                        std::string name = IsAlasmLabel(full) ? full : Sanitized(full);
                        if (name != full)
                        {
                            while (taken.count(name))
                                name += "_";
                            taken.insert(name);
                            result.diagnostics.push_back({Severity::Info, 0, 0, "label " + full + " written as " + name});
                        }
                        w.names[full] = name;
                    }
                }
            }
            for (const Statement& s : l.statements)
            {
                if (IsDirective(s, ir::DirectiveKind::EndMacro))
                    inMacro = false;
                if (IsDirective(s, ir::DirectiveKind::Defl) && !l.label.empty())
                    w.redefinable.insert(l.label[0] == '.' ? parent + l.label : l.label);
            }
        }
    }

    std::vector<int> localForBlock;   // per open MACRO / DUP: 1 when it was given a LOCAL block
    // ALASM 3.8 has no IF (its lines are errors and both branches assemble, live): an IF whose value the layout gives
    // is decided here, the branch not taken left out. Per open IF: 1 taken, 0 not taken, -1 written as it is; and
    // whether it opened inside a branch left out
    struct Decided
    {
        int state = -1;
        bool inSkipped = false;
    };
    std::vector<Decided> decided;
    const auto skipping = [&decided] { return std::any_of(decided.begin(), decided.end(), [](const Decided& d) { return d.state == 0; }); };
    for (size_t k = 0; k < program.lines.size(); ++k)
    {
        const ir::Line& l = program.lines[k];
        w.line = l.sourceLine;
        w.lineIndex = k;
        if (w.version < 400 && l.statements.size() == 1 && l.statements[0].kind == Statement::Kind::Directive)
        {
            const Statement& s = l.statements[0];
            const auto note = [&](const std::string& text) { result.document.lines.push_back({";" + text + " (ALASM 3.8 has no IF)", {}}); };
            if (s.directive == ir::DirectiveKind::If || s.directive == ir::DirectiveKind::IfUsed)
            {
                const auto value = s.directive == ir::DirectiveKind::If && !s.args.empty() ? w.Value(s.args[0]) : std::nullopt;
                if (skipping())
                {
                    decided.push_back({0, true});
                    continue;
                }
                if (value)
                {
                    decided.push_back({*value != 0 ? 1 : 0, false});
                    note(*value != 0 ? "IF: taken" : "IF: left out");
                    continue;
                }
                decided.push_back({-1, false});
                result.diagnostics.push_back({Severity::Warning, l.sourceLine, 0, "an IF whose value is not known: ALASM 3.8 has no IF"});
            }
            else if ((s.directive == ir::DirectiveKind::Else || s.directive == ir::DirectiveKind::EndIf) && !decided.empty() &&
                     decided.back().state != -1)
            {
                const Decided top = decided.back();
                if (s.directive == ir::DirectiveKind::EndIf)
                    decided.pop_back();
                else if (!top.inSkipped)
                    decided.back().state = 1 - top.state;
                if (!top.inSkipped)
                    note(s.directive == ir::DirectiveKind::Else ? "ELSE" : "ENDIF");
                continue;
            }
            else if (s.directive == ir::DirectiveKind::EndIf && !decided.empty())
                decided.pop_back();
        }
        if (skipping())
            continue;
        std::vector<std::string> texts;
        std::vector<std::string> before;   // lines written ahead of this one (name=$ of a redefinable label)

        // The label and the parent of sjasmplus' local labels (its !label leaves the parent as it is)
        const bool keepsParent = !w.sameDialect && !l.label.empty() && l.label[0] == '!';
        const std::string own = keepsParent ? l.label.substr(1) : l.label;
        std::string label;
        bool defines = false;
        for (const Statement& s : l.statements)
            defines = defines || IsDirective(s, ir::DirectiveKind::Equ) || IsDirective(s, ir::DirectiveKind::Defl);
        if (!own.empty())
        {
            if (!w.sameDialect && IsTemporary(own))
            {
                for (const auto& [at, name] : w.temporaries[own])
                    if (at == k)
                        label = name;
            }
            else if (!w.sameDialect && w.inMacro && own[0] == '.')
                label = w.Name(own);
            else
            {
                if (!w.sameDialect && !keepsParent && own[0] != '.' && own[0] != '@')
                    w.parent = own;
                else if (!w.sameDialect && !keepsParent && own[0] == '@')
                    w.parent = own.substr(1);
                label = w.Name(own);
            }
            result.labels.push_back({l.sourceLine, l.label, label, !w.scopes.empty(), w.inMacro});
        }
        w.here.reset();
        if (!own.empty() && !defines && !w.inMacro)
        {
            if (const SourceLabel* known = w.Lookup(own))
                w.here = known->value;
        }
        else if (own.empty() && !w.inMacro && !w.sameDialect)
        {
            // the project's labels name the file; a single source's do not
            auto found = options.labelValues.find(w.LineLabel(true));
            if (found == options.labelValues.end())
                found = options.labelValues.find(w.LineLabel(false));
            if (found != options.labelValues.end())
                w.here = found->second.value;
        }
        if (!label.empty() && !defines && w.redefinable.count(w.FullName(own)))
        {
            before.push_back(label + "=$");   // an address label of a name assigned with "=" elsewhere
            label.clear();
        }

        for (const Statement& s : l.statements)
        {
            if (IsDirective(s, ir::DirectiveKind::Macro))
            {
                if (s.params.size() > 10)
                    result.diagnostics.push_back({Severity::Warning, l.sourceLine, 0, "macro " + s.text + ": ALASM passes 10 parameters"});
                texts.push_back("MACRO " + w.Name(s.text));
                w.inMacro = true;
                w.macroParams = s.params;
                const bool local = !w.sameDialect && DefinesLabels(program, k, ir::DirectiveKind::Macro, ir::DirectiveKind::EndMacro);
                localForBlock.push_back(local ? 1 : 0);
                if (local)
                {
                    // sjasmplus' .labels of a macro belong to each expansion: ALASM's LOCAL block gives the same
                    std::map<std::string, std::string> scope;
                    for (size_t n = k + 1; n < program.lines.size(); ++n)
                    {
                        bool end = false;
                        for (const Statement& b : program.lines[n].statements)
                            end = end || IsDirective(b, ir::DirectiveKind::EndMacro);
                        if (end)
                            break;
                        const std::string& bodyLabel = program.lines[n].label;
                        if (!bodyLabel.empty() && bodyLabel[0] == '.')
                            scope[bodyLabel] = Sanitized(bodyLabel.substr(1));
                    }
                    w.scopes.push_back(std::move(scope));
                    texts.push_back("LOCAL");
                }
                continue;
            }
            if (IsDirective(s, ir::DirectiveKind::EndMacro))
            {
                if (!localForBlock.empty() && localForBlock.back())
                {
                    texts.push_back("ENDL");
                    if (!w.scopes.empty())
                        w.scopes.pop_back();
                }
                if (!localForBlock.empty())
                    localForBlock.pop_back();
                texts.push_back("ENDM");
                w.inMacro = false;
                w.macroParams.clear();
                continue;
            }
            if (IsDirective(s, ir::DirectiveKind::Repeat))
            {
                for (std::string& t : w.StatementText(s, own))
                    texts.push_back(t);
                // Labels in a repeated block (sjasmplus' temporary ones) are defined on every pass: LOCAL keeps them apart
                const bool local = !w.sameDialect && DefinesLabels(program, k, ir::DirectiveKind::Repeat, ir::DirectiveKind::EndRepeat, true);
                localForBlock.push_back(local ? 1 : 0);
                if (local)
                {
                    w.scopes.emplace_back();
                    texts.push_back("LOCAL");
                }
                continue;
            }
            if (IsDirective(s, ir::DirectiveKind::EndRepeat))
            {
                if (!localForBlock.empty() && localForBlock.back())
                {
                    texts.push_back("ENDL");
                    if (!w.scopes.empty())
                        w.scopes.pop_back();
                }
                if (!localForBlock.empty())
                    localForBlock.pop_back();
                texts.push_back("EDUP");
                continue;
            }
            for (std::string& t : w.StatementText(s, own))
                texts.push_back(t);
            // $ moves past what the statement puts down (sjasmplus: several statements a line)
            if (s.kind != Statement::Kind::Directive || IsDirective(s, ir::DirectiveKind::Db) || IsDirective(s, ir::DirectiveKind::Dw) ||
                IsDirective(s, ir::DirectiveKind::Ds) || IsDirective(s, ir::DirectiveKind::Incbin))
                w.here.reset();
        }

        // Lay out: the label in column 0, commands from column 8, one command a line; "=expr" takes the label
        for (const std::string& b : before)
            result.document.lines.push_back({b, {}});
        std::vector<std::string> out;
        std::string current = label;
        bool first = true;
        for (const std::string& t : texts)
        {
            if (!t.empty() && t[0] == '=')
            {
                current = label + t;
                first = false;
                continue;
            }
            if (!t.empty() && t[0] == ';')
            {
                if (!current.empty() && !first)
                    out.push_back(current);
                out.push_back((first && !label.empty() ? label + " " : std::string()) + t);
                current.clear();
                first = false;
                continue;
            }
            if (!first && !current.empty())
                out.push_back(current);
            std::string text = first ? label : std::string();
            text.append(text.size() < 8 ? 8 - text.size() : 1, ' ');
            text += t;
            current = text;
            first = false;
        }
        if (!current.empty() || out.empty())
            out.push_back(current);
        if (l.hasComment)
        {
            std::string& last = out.back();
            if (!last.empty())
                last += last.size() < 32 ? std::string(32 - last.size(), ' ') : std::string(" ");
            last += ";" + InCp866(l.comment);
        }
        for (const std::string& text : out)
        {
            result.document.lines.push_back({text, {}});
            result.document.lines.back().origin = l.sourceLine;
        }
    }
    return result;
}
}  // namespace unrealasm::dialects
