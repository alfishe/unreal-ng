#include "dialects/xas/xasbackend.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>

#include "dialects/common/z80.h"
#include "unrealasm/registry.h"

namespace unrealasm::dialects
{
namespace
{
using ir::Expr;
using ir::Op;
using ir::Operand;
using ir::Statement;

/// The codec version names: "4.18" -> 418, "7.43c" -> 743, "9.07m" -> 907; "" (the newest) -> 999
int VersionNumber(const std::string& version)
{
    if (version.empty())
        return 999;
    const size_t dot = version.find('.');
    std::string minor = dot == std::string::npos ? std::string() : version.substr(dot + 1, 2);
    while (minor.size() < 2)
        minor += "0";
    return std::atoi(version.substr(0, dot).c_str()) * 100 + std::atoi(minor.c_str());
}

/// XAS compares labels on their first 7 characters without case (research-xas-to-sjasmplus.md §2)
constexpr size_t kSignificant = 7;

// The words XAS reads as tokens (research-xas.md §4) or commands: a label must not be one of them
const std::set<std::string> kWords = {
    "ORG", "ENT", "EQU", "WORK", "DB", "DW", "DM", "DS", "DEFB", "DEFW", "DEFM", "DEFS", "!ASSM", "!CONT", ".ASM", ".END",
    "LTEXT", "LCODE", "LOADTEXT", "LOADCODE", "LTXT", "LCOD", "USEL", "IFNZ", "IFZ", "MAKE", "SLI", "BC", "DE", "HL", "IX",
    "IY", "SP", "AF", "B", "C", "D", "E", "H", "L", "A", "I", "R", "HX", "LX", "HY", "LY", "NZ", "Z", "NC", "PO", "PE",
    "P", "M",
};

bool Reserved(const std::string& upper)
{
    // and the frontend's own name for WORK's offset (__xas_work: its first 7 characters)
    return kWords.count(upper) || z80::IsMnemonic(z80::Lower(upper)) || upper.rfind("__XAS_W", 0) == 0;
}

/// Characters XAS's font draws as others (^ ↑, ` £, { | } ~ as Russian letters, research-xas.md §6)
bool NotInFont(unsigned char c)
{
    return c == '^' || c == '`' || (c >= '{' && c <= '~');
}

std::string Key(const std::string& name)
{
    return z80::Upper(name.substr(0, kSignificant));
}

/// The name XAS gets for a source name: letters, digits, "_", "?", "@" (a letter first), in upper case
std::string Sanitized(const std::string& name)
{
    std::string out;
    for (const char c : name)
        out.push_back(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '?' || c == '@'
                          ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                          : '_');
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])))
        out = "L" + out;
    return out;
}

bool IsTemporary(const std::string& label)
{
    return !label.empty() && label.find_first_not_of("0123456789") == std::string::npos;
}

bool IsDirective(const Statement& s, ir::DirectiveKind kind)
{
    return s.kind == Statement::Kind::Directive && s.directive == kind;
}

const Expr& Unwrapped(const Expr& e)
{
    return e.kind == Expr::Kind::Group ? Unwrapped(e.args[0]) : e;
}

bool UsesCurrent(const Expr& e)
{
    if (e.kind == Expr::Kind::Current)
        return true;
    return std::any_of(e.args.begin(), e.args.end(), [](const Expr& a) { return UsesCurrent(a); });
}

/// `e` with $ replaced by $-offset (an item of a DB / DW list: XAS's $ is the item's address, the source's the line's)
Expr ShiftCurrent(const Expr& e, int64_t offset)
{
    if (e.kind == Expr::Kind::Current)
    {
        Expr g = Expr::Make(Expr::Kind::Group);
        g.args.push_back(Expr::Binary(Op::Sub, e, Expr::Number(offset)));
        return g;
    }
    Expr out = e;
    for (Expr& a : out.args)
        a = ShiftCurrent(a, offset);
    return out;
}

/// A comment in characters XAS's font has: Russian letters in capitals, those it draws as Latin ones (А В Е К М Н О Р
/// С Т Х З) as those, any other character outside ASCII as "?" (research-xas.md §6)
std::string CommentText(const std::string& utf8)
{
    static const std::string kLookalikes = "ABEKMHOPCTX3";   // А В Е К М Н О Р С Т Х З
    static const std::u32string kRussian = U"АВЕКМНОРСТХЗ";
    static const std::u32string kOwn = U"ДЖИЙЛПУФЦЧЫЬЭЮЯЪШЩБГ";
    std::string out;
    for (size_t p = 0; p < utf8.size();)
    {
        const unsigned char b = static_cast<unsigned char>(utf8[p]);
        if (b < 0x80)
        {
            out.push_back(b < 0x20 ? ' ' : NotInFont(b) ? '?' : static_cast<char>(b));
            ++p;
            continue;
        }
        const size_t length = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
        char32_t c = length == 1 ? b : b & (0x7F >> length);
        for (size_t k = 1; k < length && p + k < utf8.size(); ++k)
            c = (c << 6) | (static_cast<unsigned char>(utf8[p + k]) & 0x3F);
        p += length;
        if (c >= U'а' && c <= U'я')
            c -= 0x20;
        if (c == U'Ё' || c == U'ё')
            c = U'Е';
        if (const size_t k = kRussian.find(c); k != std::u32string::npos)
            out.push_back(kLookalikes[k]);
        else if (kOwn.find(c) != std::u32string::npos)
        {
            // UTF-8 of the capital as it is: the codec maps it to XAS's glyph
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
        else
            out.push_back('?');
    }
    return out;
}

// sjasmplus directives kept as text that make no bytes: dropped with a note
const std::set<std::string> kNoBytes = {
    "DEVICE", "SAVEBIN", "SAVESNA", "SAVETAP", "SAVEHOB", "SAVETRD", "SAVENEX", "SAVECPCSNA", "SAVECDT", "SAVEDEV",
    "EMPTYTAP", "EMPTYTRD", "OUTPUT", "OUTEND", "LABELSLIST", "CSPECTMAP", "BPLIST", "SETBP", "SETBREAKPOINT",
    "EXPORT", "TAPOUT", "TAPEND", "SHELLEXEC", "ASSERT", "SIZE", "DEFDEVICE", "DISPLAY", "OPT",
};

struct Writer
{
    const ir::Program& program;
    const BackendOptions& options;
    Diagnostics& diagnostics;
    int version = 999;
    uint32_t line = 0;
    size_t lineIndex = 0;
    std::string parent{};                                // sjasmplus: the last label without a dot (".x" hangs on it)
    std::map<std::string, std::string> names{};          // the source's full names -> XAS names
    std::set<std::string> redefinable{};                 // names assigned with DEFL / "=": their values are written
    std::map<std::string, int64_t> deflValues{};         // ... as they stand at the line being written
    std::map<std::string, std::string> deflNames{};      // ... or, where the value is not known, the XAS name of the definition in force
    std::string assignedName{};                          // the XAS name the statement being written defines that way
    std::vector<std::map<std::string, std::string>> scopes{};   // the labels of each macro expansion being written
    std::map<std::string, std::vector<std::pair<size_t, std::string>>> temporaries{};   // sjasmplus "1": its definitions
    bool displaced = false;
    std::optional<int64_t> here{};                       // $ on the line being written, when its label gives it
    std::string problem{};                               // why XAS cannot compute the expression being written
    bool strict = false;                                 // every label must be known when the line is assembled (ORG, DS ...)

    // ---- version ----

    bool Old() const { return version < 700; }           // 4.x / 5.x: DEFB DEFW DEFM DEFS, LOADTEXT, no strings in DEFB
    bool DotWords() const { return version >= 907 && version < 910; }   // 9.07m: .ASM .END LTXT LCOD
    bool HasIfz() const { return version >= 743 && version < 900; }      // IFNZ / IFZ: 7.43 and 7.447 only
    std::string Db() const { return Old() ? "DEFB" : "DB"; }
    std::string Dw() const { return Old() ? "DEFW" : "DW"; }
    std::string Dm() const { return Old() ? "DEFM" : "DM"; }
    std::string Ds() const { return Old() ? "DEFS" : "DS"; }
    std::string Assm() const { return DotWords() ? ".ASM" : "!ASSM"; }
    std::string Cont() const { return DotWords() ? ".END" : "!CONT"; }
    std::string Ltext() const { return Old() ? "LOADTEXT" : DotWords() ? "LTXT" : "LTEXT"; }
    std::string Lcode() const { return Old() ? "LOADCODE" : DotWords() ? "LCOD" : "LCODE"; }

    // ---- names ----

    std::string FullName(const std::string& text) const
    {
        if (!text.empty() && text[0] == '@')
            return text.substr(1);
        if (!text.empty() && text[0] == '.')
            return parent + text;
        return text;
    }

    std::optional<std::string> Temporary(const std::string& reference) const
    {
        if (reference.size() < 2 || !IsTemporary(reference.substr(0, reference.size() - 1)))
            return std::nullopt;
        const char way = static_cast<char>(std::tolower(static_cast<unsigned char>(reference.back())));
        const auto found = temporaries.find(reference.substr(0, reference.size() - 1));
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

    /// The XAS spelling of a name (shown in lower case, as XAS's editor shows its text)
    std::string Name(const std::string& text) const
    {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            if (const auto local = it->find(text); local != it->end())
                return z80::Lower(local->second);
        const std::string full = FullName(text);
        const auto found = names.find(full);
        return z80::Lower(found != names.end() ? found->second : Sanitized(full).substr(0, kSignificant));
    }

    bool InScope(const std::string& text) const
    {
        return std::any_of(scopes.begin(), scopes.end(), [&](const auto& scope) { return scope.count(text) > 0; });
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
        if (text.empty() || InScope(text))
            return nullptr;   // a label of a macro expansion: no layout value
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
                if (redefinable.count(FullName(e.text)) && !InScope(e.text))
                {
                    const auto v = deflValues.find(FullName(e.text));
                    return v == deflValues.end() ? std::nullopt : std::optional<int64_t>(v->second);
                }
                const SourceLabel* l = Lookup(e.text);
                if (!l || l->redefinable)
                    return std::nullopt;
                return l->value;
            }
            case Expr::Kind::Current: return displaced ? std::nullopt : here;
            case Expr::Kind::Group: return Value(e.args[0]);
            case Expr::Kind::Unary:
            {
                if (e.op == Op::Exists)
                {
                    if (options.labelValues.empty() || e.args[0].kind != Expr::Kind::Symbol)
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
                    case Op::RotateLeft16: case Op::RotateRight16:
                    {
                        const uint32_t v = static_cast<uint32_t>(*a & 0xFFFF);
                        const int n = static_cast<int>(*b & 15);
                        const uint32_t r = e.op == Op::RotateLeft16 ? (v << n) | (v >> (16 - n)) : (v >> n) | (v << (16 - n));
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

    /// The label LabelValues gives a line without one (its address, for $): a local label of the line's parent
    std::string LineLabel(bool withFile) const
    {
        std::string file;
        if (withFile)
            for (const char c : options.fileName)
                file.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
        return parent + ".__UNREALASM_L" + file + "_" + std::to_string(line);
    }

    /// A label defined further down (or in another file): unknown while XAS's first pass reaches this line
    bool Later(const std::string& text) const
    {
        const SourceLabel* l = Lookup(text);
        if (!l || l->redefinable)
            return false;
        if (!l->file.empty() && !options.fileName.empty() && l->file != options.fileName)
            return true;
        return l->line > line;
    }

    /// The value lies in 0..#FFFF, where XAS's 16-bit unsigned division agrees with the source's
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
        return v < 256 ? std::to_string(v) : "#" + z80::Lower(z80::HexDigits(static_cast<uint64_t>(v), 4));
    }

    static std::string NumberText(const Expr& e)
    {
        const int64_t v = e.value & 0xFFFF;
        switch (e.spelling)
        {
            case ir::NumberSpelling::Hex: return "#" + z80::Lower(z80::HexDigits(static_cast<uint64_t>(v), std::clamp(e.digits, 1, 4)));
            case ir::NumberSpelling::Binary:
            {
                std::string bits;
                for (int k = std::clamp(e.digits, 1, 16) - 1; k >= 0; --k)
                    bits.push_back(((v >> k) & 1) ? '1' : '0');
                return "%" + bits;
            }
            case ir::NumberSpelling::Character:
                // one character only: XAS 9.10 puts the others of a longer string into the code
                if (e.text.size() == 1 && e.text[0] > 0x20 && e.text[0] < 0x7F && e.text[0] != '"' && e.text[0] != ';' &&
                    !NotInFont(static_cast<unsigned char>(e.text[0])))
                    return "\"" + e.text + "\"";
                return Number(v);
            default: return std::to_string(v);
        }
    }

    void Problem(const std::string& why)
    {
        if (problem.empty())
            problem = why;
    }

    /// An operand of a chain: a number, a name or $ (a group of one), else its value when known
    std::optional<std::string> Atom(const Expr& e)
    {
        const Expr& u = Unwrapped(e);
        switch (u.kind)
        {
            case Expr::Kind::Number: return NumberText(u);
            case Expr::Kind::Symbol:
                if (redefinable.count(FullName(u.text)) && !InScope(u.text))
                {
                    // XAS defines a name once: a name assigned again and again stands for its value here, or for the
                    // name its definition in force got
                    if (const auto v = Value(u))
                        return Number(*v);
                    if (const auto n = deflNames.find(FullName(u.text)); n != deflNames.end())
                        return z80::Lower(n->second);
                    Problem("the value of " + u.text + " (assigned with = / DEFL) is not known here");
                    return Name(u.text);
                }
                if (strict && Later(u.text))
                {
                    if (const auto v = Value(u))
                        return Number(*v);
                    Problem("the label " + u.text + " is defined further down");
                }
                return Name(u.text);
            case Expr::Kind::Current: return std::string("$");
            case Expr::Kind::Raw:
                if (const auto temporary = Temporary(u.text))
                    return z80::Lower(*temporary);
                return std::nullopt;
            default: break;
        }
        if (const auto v = Value(u))
            return Number(*v);
        return std::nullopt;
    }

    static bool IsAtom(const Expr& e)
    {
        const Expr& u = Unwrapped(e);
        return u.kind == Expr::Kind::Number || u.kind == Expr::Kind::Symbol || u.kind == Expr::Kind::Current ||
               (u.kind == Expr::Kind::Raw && u.text.size() >= 2);
    }

    /// XAS text computing the source's value modulo #10000 left to right, each operator taking an atom; nullopt when
    /// the expression cannot be put so (an operator XAS lacks, a grouping it cannot regroup)
    std::optional<std::string> Chain(const Expr& e)
    {
        switch (e.kind)
        {
            case Expr::Kind::Number: case Expr::Kind::Symbol: case Expr::Kind::Current: case Expr::Kind::Raw: return Atom(e);
            case Expr::Kind::Group: return Chain(e.args[0]);
            case Expr::Kind::Unary:
            {
                const auto inner = Chain(e.args[0]);
                switch (e.op)
                {
                    case Op::Plus: return inner;
                    case Op::Negate:
                        if (IsAtom(e.args[0]))
                            if (const auto a = Atom(e.args[0]))
                                return "0-" + *a;
                        return inner ? std::optional<std::string>(*inner + "*#ffff") : std::nullopt;   // -x = x*#FFFF in 16 bits
                    case Op::Not: return inner ? std::optional<std::string>(*inner + "!#ffff") : std::nullopt;
                    case Op::High: return inner ? std::optional<std::string>(*inner + "/256") : std::nullopt;
                    case Op::Low: return inner ? std::optional<std::string>(*inner + "*256/256") : std::nullopt;
                    default: return std::nullopt;
                }
            }
            case Expr::Kind::Binary: return Binary(e);
            default: return std::nullopt;
        }
    }

    std::optional<std::string> Binary(const Expr& e)
    {
        const Expr& a = e.args[0];
        const Expr& b = Unwrapped(e.args[1]);
        const auto join = [&](const char* symbol, const Expr& left, const Expr& right) -> std::optional<std::string> {
            const auto l = Chain(left);
            const auto r = Atom(right);
            if (!l || !r)
                return std::nullopt;
            return *l + symbol + *r;
        };
        switch (e.op)
        {
            case Op::Add: case Op::Sub: case Op::Mul: case Op::Xor:
            {
                const char* symbol = e.op == Op::Add ? "+" : e.op == Op::Sub ? "-" : e.op == Op::Mul ? "*" : "!";
                if (IsAtom(b))
                    return join(symbol, a, b);
                // a+(c+d) = a+c+d, a+(c-d) = a+c-d, a-(c+d) = a-c-d, a-(c-d) = a-c+d; a*(c*d), a!(c!d) alike
                if (b.kind == Expr::Kind::Binary && ((e.op == Op::Add || e.op == Op::Sub) && (b.op == Op::Add || b.op == Op::Sub)))
                {
                    const Op first = e.op;
                    const Op second = e.op == Op::Add ? b.op : (b.op == Op::Add ? Op::Sub : Op::Add);
                    return Chain(Expr::Binary(second, Expr::Binary(first, a, b.args[0]), b.args[1]));
                }
                if (b.kind == Expr::Kind::Binary && b.op == e.op && (e.op == Op::Mul || e.op == Op::Xor))
                    return Chain(Expr::Binary(e.op, Expr::Binary(e.op, a, b.args[0]), b.args[1]));
                // + * ! commute: the atom goes to the right
                if (e.op != Op::Sub && IsAtom(a))
                    return join(symbol, b, a);
                if (e.op == Op::Sub && IsAtom(a))
                {
                    // a-b = (b*#FFFF)+a
                    const auto r = Chain(b);
                    const auto l = Atom(a);
                    if (r && l)
                        return *r + "*#ffff+" + *l;
                }
                return join(symbol, a, b);   // the right side folded to its value, if known
            }
            case Op::Div:
                if (!InWord(a) || !InWord(b))
                    Problem("a division XAS makes in 16-bit unsigned words");
                return join("/", a, b);
            case Op::Shl: case Op::Shr: case Op::ShrUnsigned:
            {
                const auto shift = Value(b);
                if (!shift || *shift < 0 || *shift > 15)
                    return std::nullopt;
                if (e.op != Op::Shl && !InWord(a))
                    Problem("a right shift of a value outside 0..#FFFF");
                const auto l = Chain(a);
                return l ? std::optional<std::string>(*l + (e.op == Op::Shl ? "*" : "/") + Number(int64_t{1} << *shift)) : std::nullopt;
            }
            case Op::RotateLeft16: case Op::RotateRight16:
            {
                // 'L / 'R rotate by one bit (7.x)
                const auto count = Value(b);
                if (version < 700 || version >= 900 || !count)
                    return std::nullopt;
                auto l = Chain(a);
                if (!l)
                    return std::nullopt;
                for (int64_t k = 0; k < (*count & 15); ++k)
                    *l += e.op == Op::RotateLeft16 ? "'L" : "'R";
                return l;
            }
            default: return std::nullopt;
        }
    }

    /// An expression as XAS computes it; written as its value where XAS cannot compute it so
    std::string Expression(const Expr& e)
    {
        problem.clear();
        const auto text = Chain(e);
        if (text && problem.empty())
            return *text;
        if (const auto value = Value(e))
        {
            diagnostics.push_back({Severity::Info, line, 0, "written as its value " + Number(*value) + (problem.empty() ? std::string() : ": " + problem)});
            return Number(*value);
        }
        diagnostics.push_back({Severity::Warning, line, 0, "XAS cannot compute this expression" + (problem.empty() ? std::string() : " (" + problem + ")") + ": written as 0"});
        return text ? *text : "0";
    }

    /// An expression that must be known when XAS's first pass reaches the line (ORG, EQU, DS, WORK, !ASSM)
    std::string Known(const Expr& e)
    {
        strict = true;
        const std::string text = Expression(e);
        strict = false;
        return text;
    }

    /// A string as DB items: printable runs quoted; the quote, ";", characters outside the font and control
    /// characters as numbers; a run of one character a number too
    static std::vector<std::string> StringItems(const std::string& text)
    {
        std::vector<std::string> out;
        std::string run;
        auto flush = [&]() {
            if (run.size() == 1)
                out.push_back(std::to_string(static_cast<unsigned char>(run[0])));
            else if (!run.empty())
                out.push_back("\"" + run + "\"");
            run.clear();
        };
        for (const char c : text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u >= 0x7F || c == '"' || c == ';' || NotInFont(u))
            {
                flush();
                out.push_back(std::to_string(u));
            }
            else
                run.push_back(c);
        }
        flush();
        return out;
    }

    std::string OperandText(const Operand& o)
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
                return r == "af'" ? "AF" : z80::Upper(r);   // EX AF,AF is EX AF,AF'
            }
            case Operand::Kind::Condition: return z80::Upper(o.text);
            case Operand::Kind::Indirect: return "(" + z80::Upper(o.text) + ")";
            case Operand::Kind::Indexed:
            {
                const auto d = Value(o.expr);
                if (d && *d < 0 && *d >= -128)
                    return "(" + z80::Upper(o.text) + "-" + std::to_string(-*d) + ")";
                return "(" + z80::Upper(o.text) + "+" + Expression(o.expr) + ")";
            }
            case Operand::Kind::Memory: return "(" + Expression(o.expr) + ")";
            case Operand::Kind::Immediate: return Expression(o.expr);
            case Operand::Kind::String:
            {
                std::string out;
                for (const std::string& item : StringItems(o.text))
                    out += (out.empty() ? "" : ",") + item;
                return out;
            }
        }
        return {};
    }

    std::string Operands(const std::vector<Operand>& operands)
    {
        std::string out;
        for (const Operand& o : operands)
            out += (out.empty() ? "" : ",") + OperandText(o);
        return out;
    }

    std::string NotConverted(const std::string& what)
    {
        diagnostics.push_back({Severity::Warning, line, 0, "not converted: " + what});
        return ";unreal-asm: not converted: " + what;
    }

    /// Data items over lines of a size XAS's editor shows (about 40 characters of operands)
    static std::vector<std::string> DataLines(const std::string& keyword, const std::vector<std::string>& items)
    {
        std::vector<std::string> out;
        std::string current;
        for (const std::string& item : items)
        {
            if (!current.empty() && current.size() + item.size() > 40)
            {
                out.push_back(keyword + " " + current);
                current.clear();
            }
            current += (current.empty() ? "" : ",") + item;
        }
        if (!current.empty())
            out.push_back(keyword + " " + current);
        return out;
    }

    /// DB / DW items; a list using $ gets $ per item (XAS's $ is the address of the item itself)
    std::vector<std::string> Data(const Statement& s, bool words)
    {
        const bool current = s.operands.size() > 1 && std::any_of(s.operands.begin(), s.operands.end(), [](const Operand& o) { return UsesCurrent(o.expr); });
        std::vector<std::string> items;
        std::vector<std::string> out;
        const auto flush = [&]() {
            for (const std::string& l : DataLines(words ? Dw() : Db(), items))
                out.push_back(l);
            items.clear();
        };
        int64_t offset = 0;
        for (const Operand& o : s.operands)
        {
            if (o.kind == Operand::Kind::String)
            {
                if (words)
                {
                    // a string of one or two characters in DW is a word (sjasmplus 'AB' = #4142)
                    if (o.text.size() > 2)
                    {
                        items.push_back(NotConverted("a string of more than two characters in DW"));
                        continue;
                    }
                    int64_t value = 0;
                    for (const char c : o.text)
                        value = ((value << 8) | static_cast<unsigned char>(c)) & 0xFFFF;
                    items.push_back(Number(value));
                    offset += 2;
                    continue;
                }
                if (o.text.size() <= 2)
                {
                    for (const char c : o.text)
                        items.push_back(std::to_string(static_cast<unsigned char>(c)));
                }
                else if (Old())
                {
                    // 4.x / 5.x: DEFB takes no strings; DEFM does
                    for (const std::string& item : StringItems(o.text))
                        if (item[0] == '"')
                        {
                            flush();
                            out.push_back(Dm() + " " + item);
                        }
                        else
                            items.push_back(item);
                }
                else
                    for (const std::string& item : StringItems(o.text))
                        items.push_back(item);
                offset += static_cast<int64_t>(o.text.size());
                continue;
            }
            Operand item = o;
            if (current && offset > 0)
                item.expr = ShiftCurrent(o.expr, offset);
            if (words && item.kind == Operand::Kind::Immediate && item.expr.kind == Expr::Kind::Number && item.expr.spelling == ir::NumberSpelling::Character)
                item.expr.spelling = ir::NumberSpelling::Decimal;
            items.push_back(OperandText(item));
            offset += words ? 2 : 1;
            if (current)
                flush();   // one item a line: $ is then the item's address in both assemblers once shifted
        }
        flush();
        return out;
    }

    std::vector<std::string> InstructionText(const Statement& s)
    {
        const std::string& m = s.mnemonic;
        const auto& ops = s.operands;
        auto reg = [&](size_t k, const char* name) { return k < ops.size() && ops[k].kind == Operand::Kind::Register && ops[k].text == name; };
        // IN F,(C) / IN (C) and OUT (C),0 (no XAS form): their bytes
        if (m == "inf" || (m == "in" && (reg(0, "f") || (ops.size() == 1 && ops[0].kind == Operand::Kind::Indirect))))
            return {Db() + " #ed,#70   ;IN F,(C)"};
        if (m == "out" && ops.size() == 2 && ops[0].kind == Operand::Kind::Indirect && ops[1].kind == Operand::Kind::Immediate)
            return {Db() + " #ed,#71   ;OUT (C),0"};
        // The undocumented copies (RLC (IX+d),B; SET 3,(IY+d),A): their bytes
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
                    return {Db() + " " + std::string(ops[at].text == "ix" ? "#dd" : "#fd") + ",#cb," + Expression(ops[at].expr) + "," + Number(code) +
                            "   ;" + z80::Upper(m) + " " + Operands(ops)};
                }
        }
        if (m == "sll" || m == "sli")
            return {"SLI " + Operands(ops)};
        // XAS 4.18 and 5.05 assemble LD IY,(nn) as LD IX,(nn), and 4.18 LD LY,n as LD HY,n (seen in unreal-ng): bytes
        if (Old() && m == "ld" && reg(0, "iy") && ops.size() == 2 && ops[1].kind == Operand::Kind::Memory)
            return {Db() + " #fd,#2a   ;LD IY,(" + Expression(ops[1].expr) + ")", Dw() + " " + Expression(ops[1].expr)};
        if (version < 500 && m == "ld" && reg(0, "iyl") && ops.size() == 2 && ops[1].kind == Operand::Kind::Immediate)
            return {Db() + " #fd,#2e," + Expression(ops[1].expr) + "   ;LD LY," + Expression(ops[1].expr)};
        if (program.dialect == "sjasmplus")
        {
            // sjasmplus repeats these for each operand (PUSH AF,BC; SUB A,B is SUB A : SUB B)
            const bool pairFirst = !ops.empty() && ops[0].kind == Operand::Kind::Register && (ops[0].text == "hl" || ops[0].text == "ix" || ops[0].text == "iy");
            if (ops.size() > 1 && (m == "push" || m == "pop" || m == "inc" || m == "dec" ||
                                   ((m == "sub" || m == "and" || m == "xor" || m == "or" || m == "cp") && !pairFirst)))
            {
                std::vector<std::string> out;
                for (const Operand& o : ops)
                    out.push_back(z80::Upper(m) + " " + OperandText(o));
                return out;
            }
            // and its fake instructions, written out as it assembles them
            auto halves = [](const std::string& r) -> std::pair<std::string, std::string> {
                if (r == "bc") return {"B", "C"};
                if (r == "de") return {"D", "E"};
                if (r == "hl") return {"H", "L"};
                if (r == "ix") return {"HX", "LX"};
                if (r == "iy") return {"HY", "LY"};
                return {};
            };
            const bool pair0 = ops.size() == 2 && ops[0].kind == Operand::Kind::Register && !halves(ops[0].text).first.empty();
            const bool pair1 = ops.size() == 2 && ops[1].kind == Operand::Kind::Register && !halves(ops[1].text).first.empty();
            if (m == "sub" && pair0 && ops[0].text == "hl" && pair1)
                return {"OR A", "SBC HL," + z80::Upper(ops[1].text)};
            if (m == "ld" && pair0 && pair1)
            {
                const auto d = halves(ops[0].text), v = halves(ops[1].text);
                return {"LD " + d.first + "," + v.first, "LD " + d.second + "," + v.second};
            }
            if (m == "ld" && pair0 && ops[1].kind == Operand::Kind::Indexed)
            {
                Operand next = ops[1];
                next.expr = Expr::Binary(Op::Add, ops[1].expr, Expr::Number(1));
                const auto d = halves(ops[0].text);
                return {"LD " + d.second + "," + OperandText(ops[1]), "LD " + d.first + "," + OperandText(next)};
            }
            if (m == "ld" && pair1 && ops[0].kind == Operand::Kind::Indexed)
            {
                Operand next = ops[0];
                next.expr = Expr::Binary(Op::Add, ops[0].expr, Expr::Number(1));
                const auto v = halves(ops[1].text);
                return {"LD " + OperandText(ops[0]) + "," + v.second, "LD " + OperandText(next) + "," + v.first};
            }
        }
        if (program.dialect != "sjasmplus" && ops.size() == 2 && reg(0, "a") && (m == "and" || m == "or" || m == "xor" || m == "cp" || m == "sub"))
            return {z80::Upper(m) + " " + OperandText(ops[1])};
        if (ops.size() == 1 && (m == "add" || m == "adc" || m == "sbc") && ops[0].kind != Operand::Kind::Condition)
            return {z80::Upper(m) + " A," + OperandText(ops[0])};
        const std::string text = Operands(ops);
        return {z80::Upper(m) + (text.empty() ? "" : " " + text)};
    }

    // ---- conditions decided here or left to XAS ----

    struct Condition
    {
        bool parentEmits = true;
        bool decided = true;      ///< known while converting: only the taken branch is written
        bool taken = true;
        bool xas = false;         ///< left to XAS (IFNZ / IFZ ... !CONT)
        bool whenZero = false;
        std::string test;
        bool Emits() const { return parentEmits && (!decided || taken); }
    };
    std::vector<Condition> conditions{};
    bool xasBlock = false;        ///< an XAS block (a repeat or a condition) is open: XAS keeps one level

    bool Emitting() const { return conditions.empty() || conditions.back().Emits(); }

    /// IF: decided when its value is known, else IFNZ / IFZ in 7.x
    std::vector<std::string> OpenCondition(const Expr& condition)
    {
        Condition c;
        c.parentEmits = Emitting();
        if (!c.parentEmits)
        {
            conditions.push_back(c);
            return {};
        }
        strict = true;
        const auto value = Value(condition);
        strict = false;
        if (value)
        {
            c.taken = *value != 0;
            conditions.push_back(c);
            return {};
        }
        c.decided = false;
        if (!HasIfz() || xasBlock)
        {
            conditions.push_back(c);
            return {NotConverted(std::string("a condition known only when assembling") + (xasBlock ? " inside an XAS block" : " (this XAS has no IFNZ / IFZ)") +
                                 ": its block is written as taken")};
        }
        // x==0 / x!=0 and a plain value: IFZ x / IFNZ x; other comparisons as their difference
        const Expr& e = Unwrapped(condition);
        if (e.kind == Expr::Kind::Binary && (e.op == Op::Equal || e.op == Op::NotEqual))
        {
            c.whenZero = e.op == Op::Equal;
            c.test = Expression(Expr::Binary(Op::Sub, e.args[0], e.args[1]));
        }
        else
            c.test = Expression(e);
        c.xas = true;
        xasBlock = true;
        conditions.push_back(c);
        return {std::string(c.whenZero ? "IFZ " : "IFNZ ") + c.test};
    }

    std::vector<std::string> ElseCondition()
    {
        if (conditions.empty())
            return {NotConverted("ELSE without IF")};
        Condition& c = conditions.back();
        if (c.decided)
        {
            c.taken = !c.taken;
            return {};
        }
        if (!c.xas)
            return {};
        c.whenZero = !c.whenZero;
        return {Cont(), std::string(c.whenZero ? "IFZ " : "IFNZ ") + c.test};
    }

    std::vector<std::string> CloseCondition()
    {
        if (conditions.empty())
            return {NotConverted("ENDIF without IF")};
        const Condition c = conditions.back();
        conditions.pop_back();
        if (c.xas)
        {
            xasBlock = false;
            return {Cont()};
        }
        return {};
    }

    std::vector<std::string> StatementText(const Statement& s, const std::string& label)
    {
        // The conditions first: a block not taken writes nothing
        if (s.kind == Statement::Kind::Directive)
        {
            if (s.directive == ir::DirectiveKind::If)
                return OpenCondition(s.args[0]);
            if (s.directive == ir::DirectiveKind::Else)
                return ElseCondition();
            if (s.directive == ir::DirectiveKind::EndIf)
                return CloseCondition();
            if (s.directive == ir::DirectiveKind::Other)
            {
                const std::string word = z80::Upper(s.text.substr(0, s.text.find_first_of(" \t")));
                const size_t at = s.text.find_first_not_of(" \t", word.size());
                const std::string rest = at == std::string::npos ? std::string() : s.text.substr(at);
                const std::string flag = rest.substr(0, rest.find_first_of(" \t"));
                if (word == "IFDEF" || word == "IFNDEF")
                {
                    // sjasmplus' DEFINE flags, tested in the order of the text: decided here
                    Condition c;
                    c.parentEmits = Emitting();
                    c.taken = defines.count(flag) == (word == "IFDEF" ? 1u : 0u);
                    conditions.push_back(c);
                    return {};
                }
                if (!Emitting())
                    return {};
                if (word == "DEFINE" || word == "DEFINE+" || word == "UNDEFINE")
                {
                    if (word == "UNDEFINE")
                        defines.erase(flag);
                    else
                        defines.insert(flag);
                    if (word != "UNDEFINE" && flag.size() < rest.size())
                        diagnostics.push_back({Severity::Warning, line, 0, "a DEFINE with a value (text put in where its name stands): not converted"});
                    return {";" + s.text};
                }
                if (kNoBytes.count(word))
                {
                    diagnostics.push_back({Severity::Info, line, 0, word + " dropped (it makes no bytes)"});
                    return {";" + s.text};
                }
                return {NotConverted(s.text)};
            }
        }
        if (!Emitting())
            return {};
        switch (s.kind)
        {
            case Statement::Kind::Raw: return {NotConverted("unparsed line: " + s.text)};
            case Statement::Kind::Instruction: return InstructionText(s);
            case Statement::Kind::MacroCall: return {NotConverted("a call of an unknown macro: " + s.mnemonic)};
            case Statement::Kind::Directive: break;
        }
        switch (s.directive)
        {
            case ir::DirectiveKind::Org:
                if (displaced)
                {
                    displaced = false;
                    return {"WORK", "ORG " + Known(s.args[0])};
                }
                return {"ORG " + Known(s.args[0])};
            case ir::DirectiveKind::Equ:
            case ir::DirectiveKind::Defl:
                if (label.empty())
                    return {NotConverted("an assignment without a name")};
                if (redefinable.count(FullName(label)) && !InScope(label))
                {
                    // its uses get the value it has then
                    strict = true;
                    const auto v = Value(s.args[0]);
                    strict = false;
                    if (!v)
                    {
                        // a value XAS computes ($ in it): this definition gets an XAS name of its own
                        const std::string text = Known(s.args[0]);
                        deflValues.erase(FullName(label));
                        assignedName = Give(FullName(label));
                        deflNames[FullName(label)] = assignedName;
                        return {"EQU " + text};
                    }
                    deflValues[FullName(label)] = *v;
                    deflNames.erase(FullName(label));
                    return {";" + label + "=" + Number(*v)};
                }
                return {"EQU " + Known(s.args[0])};
            case ir::DirectiveKind::Db: return Data(s, false);
            case ir::DirectiveKind::Dw: return Data(s, true);
            case ir::DirectiveKind::Ds:
            {
                // XAS's DS n,w repeats the word w (low, high) and cuts it at n bytes
                const std::string count = Known(s.args[0]);
                if (s.operands.empty())
                {
                    const auto fill = s.args.size() > 1 ? Value(s.args[1]) : std::optional<int64_t>(0);
                    if (!fill)
                        return {NotConverted("DS with a fill known only when assembling")};
                    const int64_t byte = *fill & 0xFF;
                    return {Ds() + " " + count + (byte ? "," + Number(byte | (byte << 8)) : std::string())};
                }
                std::vector<int64_t> pattern;
                for (const Operand& o : s.operands)
                {
                    const auto v = Value(o.expr);
                    if (!v)
                        return {NotConverted("DS with a fill known only when assembling")};
                    pattern.push_back(*v & 0xFF);
                }
                if (pattern.size() == 1)
                    return {Ds() + " " + count + "," + Number(pattern[0] | (pattern[0] << 8))};
                if (pattern.size() == 2)
                    return {Ds() + " " + count + "," + Number(pattern[0] | (pattern[1] << 8))};
                const auto n = Value(s.args[0]);
                if (!n)
                    return {NotConverted("a fill pattern of a count known only when assembling")};
                std::vector<std::string> items;
                for (int64_t k = 0; k < *n; ++k)
                    items.push_back(std::to_string(pattern[static_cast<size_t>(k) % pattern.size()]));
                return DataLines(Db(), items);
            }
            case ir::DirectiveKind::Include:
            {
                if (const auto as = options.includeAs.find(s.text); as != options.includeAs.end())
                    return {Ltext() + " " + as->second};
                std::string name = s.text;
                if (name.size() > 8)
                {
                    diagnostics.push_back({Severity::Warning, line, 0, Ltext() + " \"" + name + "\": a TR-DOS name has 8 characters"});
                    name.resize(8);
                }
                return {Ltext() + " \"" + name + "\""};
            }
            case ir::DirectiveKind::Incbin:
                if (s.args.size() > 1 || (!s.args.empty() && !(s.args[0].kind == Expr::Kind::Number && s.args[0].value == 0)))
                    return {NotConverted("INCBIN of a part of a file (" + Lcode() + " loads it all): " + s.text)};
                return {Lcode() + " \"" + s.text + "\""};
            case ir::DirectiveKind::Repeat:
                if (xasBlock)
                    return {NotConverted("a repeat inside an XAS block (XAS keeps one)")};
                xasBlock = true;
                repeats.push_back(true);
                return {Assm() + " " + Known(s.args[0])};
            case ir::DirectiveKind::EndRepeat:
                if (repeats.empty())
                    return {NotConverted("EDUP without DUP")};
                repeats.pop_back();
                xasBlock = false;
                return {Cont()};
            case ir::DirectiveKind::Disp: displaced = true; return {"WORK " + Known(s.args[0])};
            case ir::DirectiveKind::Ent:
                if (s.text == "if-displaced" && !displaced)
                    return {};
                displaced = false;
                return {"WORK"};
            case ir::DirectiveKind::End:
                diagnostics.push_back({Severity::Info, line, 0, "END dropped (XAS assembles the whole text)"});
                return {";END"};
            case ir::DirectiveKind::SaveBinary:
                diagnostics.push_back({Severity::Info, line, 0, "SAVEBIN dropped: save the bytes from XAS"});
                return {";SAVEBIN \"" + s.text + "\""};
            case ir::DirectiveKind::Display: return {};
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }

    // ---- the walk: blocks unrolled, macros expanded ----

    BackendResult* result = nullptr;
    struct MacroDef
    {
        size_t begin = 0;   ///< the MACRO line
        size_t end = 0;     ///< the ENDM line
        std::vector<std::string> params;
    };
    std::map<std::string, MacroDef> macros{};
    int expansions = 0;     ///< macro expansions written (their labels' names count it)
    int nesting = 0;        ///< expansions open (a limit against a macro calling itself)
    std::set<std::string> taken{};   // the 7-character keys of the names given out

    /// A new XAS name for `wanted`, unique in its first 7 characters and no keyword
    std::string Give(const std::string& wanted)
    {
        const std::string base = Sanitized(wanted);
        std::string name = base.substr(0, kSignificant);
        for (int n = 1; taken.count(Key(name)) || Reserved(name); ++n)
        {
            const std::string suffix = std::to_string(n);
            name = base.substr(0, kSignificant - suffix.size()) + suffix;
        }
        taken.insert(Key(name));
        return name;
    }
    std::vector<bool> repeats{};
    std::set<std::string> defines{};

    static size_t Matching(const std::vector<ir::Line>& lines, size_t from, ir::DirectiveKind open, ir::DirectiveKind close)
    {
        int depth = 0;
        for (size_t k = from; k < lines.size(); ++k)
            for (const Statement& s : lines[k].statements)
            {
                if (IsDirective(s, open))
                    ++depth;
                if (IsDirective(s, close) && --depth == 0)
                    return k;
            }
        return lines.size();
    }

    /// A DUP XAS's !ASSM cannot hold: inside another XAS block, or a body that assigns names, defines labels or
    /// opens blocks of its own; and every DUP in 4.18, whose !ASSM n assembles the block once (seen in unreal-ng)
    bool Unrolled(const std::vector<ir::Line>& lines, size_t begin, size_t end) const
    {
        if (xasBlock || version < 500)
            return true;
        for (size_t k = begin + 1; k < end; ++k)
        {
            if (!lines[k].label.empty())
                return true;
            for (const Statement& s : lines[k].statements)
                if (IsDirective(s, ir::DirectiveKind::Defl) || IsDirective(s, ir::DirectiveKind::Repeat) || IsDirective(s, ir::DirectiveKind::While) ||
                    IsDirective(s, ir::DirectiveKind::If) || s.kind == Statement::Kind::MacroCall)
                    return true;
        }
        return false;
    }

    void Range(const std::vector<ir::Line>& lines, size_t begin, size_t end, size_t index)
    {
        for (size_t k = begin; k < end; ++k)
        {
            const ir::Line& l = lines[k];
            const size_t at = index == SIZE_MAX ? k : index;
            const Statement* first = l.statements.empty() ? nullptr : &l.statements.front();
            if (first && IsDirective(*first, ir::DirectiveKind::Macro))
            {
                k = Matching(lines, k, ir::DirectiveKind::Macro, ir::DirectiveKind::EndMacro);   // written where it is used
                continue;
            }
            if (Emitting() && first && IsDirective(*first, ir::DirectiveKind::While))
            {
                // WHILE: unrolled while its condition, with the names' values as they stand, holds
                const size_t e = Matching(lines, k, ir::DirectiveKind::While, ir::DirectiveKind::EndWhile);
                line = l.sourceLine;
                for (int pass = 0;; ++pass)
                {
                    strict = true;
                    const auto go = Value(first->args[0]);
                    strict = false;
                    if (!go || pass > 4096)
                    {
                        Emit({NotConverted(!go ? "a WHILE whose condition is known only when assembling" : "a WHILE of more than 4096 passes")}, l.sourceLine);
                        break;
                    }
                    if (*go == 0)
                        break;
                    Range(lines, k + 1, e, at);
                }
                k = e;
                continue;
            }
            if (Emitting() && first && IsDirective(*first, ir::DirectiveKind::Repeat) && l.label.empty())
            {
                const size_t e = Matching(lines, k, ir::DirectiveKind::Repeat, ir::DirectiveKind::EndRepeat);
                if (Unrolled(lines, k, e))
                {
                    line = l.sourceLine;
                    strict = true;
                    const auto count = Value(first->args[0]);
                    strict = false;
                    if (!count || *count < 0 || *count > 4096)
                        Emit({NotConverted("a repeat XAS cannot hold, of a count known only when assembling")}, l.sourceLine);
                    else
                        for (int64_t pass = 0; pass < *count; ++pass)
                            Range(lines, k + 1, e, at);
                    k = e;
                    continue;
                }
            }
            if (Emitting() && first && first->kind == Statement::Kind::MacroCall)
            {
                const auto def = macros.find(z80::Lower(first->mnemonic));
                if (def != macros.end() && nesting < 16)
                {
                    if (!l.label.empty())
                    {
                        ir::Line alone = l;
                        alone.statements.clear();
                        WriteLine(alone, at);
                    }
                    Expand(def->second, *first, at, l.sourceLine);
                    continue;
                }
            }
            WriteLine(l, at);
        }
    }

    /// An operand of a macro call, read by the source's frontend
    Operand Argument(const std::string& text) const
    {
        Operand o;
        o.kind = Operand::Kind::Immediate;
        o.expr = Expr::Make(Expr::Kind::Raw);
        o.expr.text = text;
        const IFrontend* frontend = DialectRegistry::Builtin().Frontend(program.dialect);
        if (!frontend || text.empty())
            return o;
        FrontendResult parsed = frontend->Parse(SourceDocument::FromText("        INC " + text, program.dialect));
        if (parsed.program.lines.empty() || parsed.program.lines[0].statements.size() != 1)
            return o;
        const Statement& s = parsed.program.lines[0].statements[0];
        if (s.kind != Statement::Kind::Instruction || s.operands.size() != 1)
            return o;
        return s.operands[0];
    }

    static Expr Substituted(const Expr& e, const std::map<std::string, Operand>& args)
    {
        if (e.kind == Expr::Kind::Symbol)
            if (const auto a = args.find(e.text); a != args.end())
            {
                Expr g = Expr::Make(Expr::Kind::Group);
                g.args.push_back(a->second.expr);
                return g;
            }
        Expr out = e;
        for (Expr& a : out.args)
            a = Substituted(a, args);
        return out;
    }

    static Operand Substituted(const Operand& o, const std::map<std::string, Operand>& args)
    {
        if (o.kind == Operand::Kind::Immediate && o.expr.kind == Expr::Kind::Symbol)
            if (const auto a = args.find(o.expr.text); a != args.end())
                return a->second;   // a register, an indirection ... as the call gives it
        Operand out = o;
        out.expr = Substituted(o.expr, args);
        return out;
    }

    void Expand(const MacroDef& def, const Statement& call, size_t index, uint32_t callLine)
    {
        std::map<std::string, Operand> args;
        for (size_t k = 0; k < def.params.size(); ++k)
            args[def.params[k]] = Argument(k < call.params.size() ? call.params[k] : std::string());
        // The body with the arguments in, its labels one name per expansion
        ++expansions;
        std::map<std::string, std::string> scope;
        std::vector<ir::Line> body;
        for (size_t n = def.begin + 1; n < def.end; ++n)
        {
            ir::Line copy = program.lines[n];
            if (!copy.label.empty() && !scope.count(copy.label))
            {
                const std::string base = Sanitized("M" + std::to_string(expansions) + (copy.label[0] == '.' ? copy.label.substr(1) : copy.label));
                std::string name = base.substr(0, kSignificant);
                for (int k = 1; taken.count(Key(name)) || Reserved(name); ++k)
                {
                    const std::string suffix = std::to_string(k);
                    name = base.substr(0, kSignificant - suffix.size()) + suffix;
                }
                taken.insert(Key(name));
                scope[copy.label] = name;
            }
            for (Statement& s : copy.statements)
            {
                for (Operand& o : s.operands)
                    o = Substituted(o, args);
                for (Expr& a : s.args)
                    a = Substituted(a, args);
            }
            copy.sourceLine = callLine;
            body.push_back(std::move(copy));
        }
        scopes.push_back(std::move(scope));
        ++nesting;
        Range(body, 0, body.size(), index);
        --nesting;
        scopes.pop_back();
    }

    /// Lines of the document for `texts` (the comment lines a walk adds)
    void Emit(const std::vector<std::string>& texts, uint32_t origin)
    {
        for (const std::string& t : texts)
        {
            result->document.lines.push_back({t, {}});
            result->document.lines.back().origin = origin;
        }
    }

    void WriteLine(const ir::Line& l, size_t k)
    {
        line = l.sourceLine;
        lineIndex = k;
        std::vector<std::string> texts;
        const bool emittingBefore = Emitting();

        // The label and the parent of sjasmplus' local labels
        std::string label;
        bool assigns = false;
        for (const Statement& s : l.statements)
            assigns = assigns || IsDirective(s, ir::DirectiveKind::Equ) || IsDirective(s, ir::DirectiveKind::Defl);
        if (!l.label.empty())
        {
            if (IsTemporary(l.label))
            {
                for (const auto& [at, name] : temporaries[l.label])
                    if (at == k)
                        label = z80::Lower(name);
            }
            else
            {
                if (scopes.empty() && l.label[0] != '.' && l.label[0] != '@')
                    parent = l.label;
                else if (scopes.empty() && l.label[0] == '@')
                    parent = l.label.substr(1);
                label = Name(l.label);
            }
            if (scopes.empty())
                result->labels.push_back({l.sourceLine, l.label, label, false, false});
        }
        here.reset();
        if (scopes.empty() && !l.label.empty() && !assigns)
        {
            if (const SourceLabel* known = Lookup(l.label))
                here = known->value;
        }
        else if (scopes.empty() && l.label.empty())
        {
            auto found = options.labelValues.find(LineLabel(true));
            if (found == options.labelValues.end())
                found = options.labelValues.find(LineLabel(false));
            if (found != options.labelValues.end())
                here = found->second.value;
        }

        assignedName.clear();
        for (const Statement& s : l.statements)
        {
            for (std::string& t : StatementText(s, l.label))
                texts.push_back(t);
            // $ moves past what the statement puts down (sjasmplus: several statements a line)
            if (s.kind != Statement::Kind::Directive || IsDirective(s, ir::DirectiveKind::Db) || IsDirective(s, ir::DirectiveKind::Dw) ||
                IsDirective(s, ir::DirectiveKind::Ds) || IsDirective(s, ir::DirectiveKind::Incbin))
                here.reset();
        }
        if (!emittingBefore && !Emitting())
            return;   // inside a block not taken: nothing of the line exists
        if (!emittingBefore || (!l.label.empty() && redefinable.count(FullName(l.label)) && !InScope(l.label)))
            label = z80::Lower(assignedName);   // a name assigned with = / DEFL is no XAS label, or one per definition

        // Lay out as XAS shows it: the label in column 0, the command from column 8, the operands from column 14
        std::vector<std::string> out;
        bool first = true;
        for (const std::string& t : texts)
        {
            std::string text = first ? label : std::string();
            first = false;
            if (!t.empty() && t[0] == ';')
            {
                out.push_back(text.empty() ? t : text + " " + t);
                continue;
            }
            text.append(text.size() < 8 ? 8 - text.size() : 1, ' ');
            const size_t blank = t.find(' ');
            text += blank == std::string::npos ? t : t.substr(0, blank);
            if (blank != std::string::npos)
            {
                text.append(text.size() < 14 ? 14 - text.size() : 1, ' ');
                text += t.substr(t.find_first_not_of(' ', blank));
            }
            out.push_back(text);
        }
        if (first && (!label.empty() || l.hasComment || l.statements.empty()))
            out.push_back(label);
        if (out.empty())
            return;
        if (l.hasComment)
            out.back() += (out.back().empty() ? ";" : " ;") + CommentText(l.comment);
        Emit(out, l.sourceLine);
    }
};
}  // namespace

BackendResult XasBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    BackendResult result;
    result.document.dialect = "xas";
    Writer w{program, options, result.diagnostics};
    w.version = VersionNumber(options.targetVersion);
    w.result = &result;

    // The macros, and the names assigned more than once (DEFL / "="): those are written as their values
    {
        std::string parent;
        for (size_t k = 0; k < program.lines.size(); ++k)
        {
            const ir::Line& l = program.lines[k];
            for (const Statement& s : l.statements)
                if (IsDirective(s, ir::DirectiveKind::Macro))
                {
                    Writer::MacroDef def;
                    def.begin = k;
                    def.end = Writer::Matching(program.lines, k, ir::DirectiveKind::Macro, ir::DirectiveKind::EndMacro);
                    def.params = s.params;
                    w.macros[z80::Lower(s.text)] = def;
                }
            if (l.label.empty() || IsTemporary(l.label))
                continue;
            const std::string full = l.label[0] == '@' ? l.label.substr(1) : l.label[0] == '.' ? parent + l.label : l.label;
            if (l.label[0] != '.')
                parent = full;
            for (const Statement& s : l.statements)
                if (IsDirective(s, ir::DirectiveKind::Defl))
                    w.redefinable.insert(full);
        }
    }

    // Names: every label under an XAS name unique in its first 7 characters (sjasmplus' main.loop -> MAIN_LO, a
    // keyword renamed); temporary labels numbered; the labels of a macro body get theirs per expansion
    {
        const auto give = [&](const std::string& wanted) { return w.Give(wanted); };
        std::string parent;
        std::map<std::string, int> temporaryCount;
        bool inMacro = false;
        for (size_t k = 0; k < program.lines.size(); ++k)
        {
            const ir::Line& l = program.lines[k];
            for (const Statement& s : l.statements)
                inMacro = inMacro || IsDirective(s, ir::DirectiveKind::Macro);
            if (!l.label.empty() && !inMacro)
            {
                if (IsTemporary(l.label))
                    w.temporaries[l.label].push_back({k, give("T" + l.label + "_" + std::to_string(++temporaryCount[l.label]))});
                else
                {
                    const std::string full = l.label[0] == '@' ? l.label.substr(1) : l.label[0] == '.' ? parent + l.label : l.label;
                    if (l.label[0] != '.')
                        parent = full;
                    if (!w.names.count(full) && !w.redefinable.count(full))
                    {
                        const std::string name = give(full);
                        if (z80::Upper(name) != z80::Upper(full))
                            result.diagnostics.push_back({Severity::Info, 0, 0, "label " + full + " written as " + z80::Lower(name)});
                        w.names[full] = name;
                    }
                }
            }
            for (const Statement& s : l.statements)
                if (IsDirective(s, ir::DirectiveKind::EndMacro))
                    inMacro = false;
        }
    }

    w.Range(program.lines, 0, program.lines.size(), SIZE_MAX);
    return result;
}
}  // namespace unrealasm::dialects
