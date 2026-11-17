#include "dialects/rasm/rasmbackend.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>

#include "dialects/common/retrowriter.h"
#include "dialects/common/z80.h"
#include "dialects/sjasmplus/sjasmplusbackend.h"
#include "dialects/sjasmplus/sjasmplusfrontend.h"

namespace unrealasm::dialects
{
namespace
{
using ir::Expr;
using ir::Op;
using ir::Operand;
using ir::Statement;

// Words rasm reads as something else wherever they stand (rasm.c 3.3: its instruction[] table of mnemonics and
// directives, the preprocessor's words, the math functions, the registers and conditions): a label must not be one.
// rasm compares them in any case
const std::set<std::string> kWords = {
    // directives and the mnemonics z80::IsMnemonic does not know
    "API_SEND", "DF24", "DEFF", "DEFR", "DEFB", "DEFM", "DF", "DR", "DM", "DB", "DEFW", "DW", "DEFS", "DS", "STR", "SL1",
    "SRL8", "EXA", "REPEAT", "STARTINGINDEX", "REND", "UNTIL", "ORG", "PROTECT", "WHILE", "WEND", "INCLUDE", "HEXBIN",
    "ALIGN", "CONFINE", "ELSEIFNOT", "ELSEIF", "ELSE", "IF", "ENDIF", "IFNOT", "IFDEF", "IFNDEF", "IFUSED", "IFNUSED",
    "UNDEF", "CASE", "BREAK", "DEFAULT", "SWITCH", "ENDSWITCH", "CODE", "NOCODE", "MACRO", "ENUM", "TICKER", "LET",
    "ASSERT", "CHARSET", "UTF8REMAP", "RUN", "SAVE", "REDEFINE_BRK", "BRK", "NOLIST", "LIST", "STOP", "PRINT", "COMZ",
    "DELAYED_PRINT", "FAIL", "BREAKPOINT", "ROMBANK", "BANK", "BANKSET", "NAMEBANK", "NAMEROM", "LIMIT", "LZEXO", "LZX7",
    "LZX0", "LZX0B", "LZAPU", "LZSA1", "LZSA2", "LZ4", "LZ48", "LZ49", "LZCLOSE", "SNASET", "SNAPINIT", "CPRINIT",
    "BUILDZX", "BUILDOBJ", "BUILDCPR", "BUILDSNA", "BUILDROM", "BUILDTAPE", "SETCPC", "SETCRTC", "AMSDOS", "OTD", "OTI",
    "SHL", "SHR", "STRUCT", "ENDSTRUCT", "ENDS", "LABEL", "NOEXPORT", "ENOEXPORT", "MODULE", "ENDMODULE", "TIMESTAMP",
    "SUMMEM", "SUM16", "XORMEM", "CIPHERMEM", "EXTERNAL", "PROCEDURE", "REBANK", "LOCALISATION", "RELOCATE",
    "ENDRELOCATE", "RELOCATE_START", "RELOCATE_END", "RELOCATE_TABLE", "EDSK", "HFE", "ERRRD", "DEFI", "END", "EQU",
    "DEFL",
    // the preprocessor's
    "INCBIN", "INCL48", "INCL49", "INCLZ4", "INCZX7", "INCEXO", "INCAPU", "INCZX0", "READ", "ENDM", "MEND",
    // the math functions
    "SIN", "COS", "INT", "ABS", "LN", "LOG10", "SQRT", "FLOOR", "ASIN", "ACOS", "ATAN", "EXP", "LO", "HI", "PSGVALUE",
    "RND", "FRAC", "CEIL", "GETR", "GETV", "GETG", "GETB", "SETR", "SETV", "SETG", "SETB", "SOFT2HARD_INK", "S2H_INK",
    "HARD2SOFT_INK", "H2S_INK", "PEEK", "POW2", "POW", "FMOD", "ATAN2", "HYPOT", "LDEXP", "FDIM", "FSTEP", "FMAX", "FMIN",
    "CLAMP", "LERP", "ISGREATER", "ISLESS", "FREMAIN", "GETNOP", "GETTICK", "DURATION", "FILESIZE", "GETSIZE",
    "IS_REGISTER", "FILEBYTE", "COUNTNOPS", "MOD", "AND", "OR", "XOR", "NOT",
    // the variables rasm defines
    "PI", "ASSEMBLER_RASM",
    // registers and conditions
    "A", "B", "C", "D", "E", "F", "H", "L", "I", "R", "AF", "BC", "DE", "HL", "IX", "IY", "SP", "LX", "HX", "XL", "XH",
    "LY", "HY", "YL", "YH", "IXL", "IYL", "IXH", "IYH", "NZ", "Z", "NC", "PO", "PE", "P", "M",
};

int Priority(Op op)
{
    // rasm.c's priorities (lower binds tighter): every comparison on one level, unlike C
    switch (op)
    {
        case Op::Mul: case Op::Div: case Op::Mod: return 2;
        case Op::Add: case Op::Sub: return 3;
        case Op::Shl: case Op::Shr: return 4;
        case Op::Less: case Op::Greater: case Op::LessEqual: case Op::GreaterEqual: case Op::Equal: case Op::NotEqual: return 5;
        case Op::And: return 6;
        case Op::Xor: return 7;
        case Op::Or: return 8;
        case Op::LogicalAnd: return 9;
        case Op::LogicalOr: return 10;
        default: return 0;
    }
}

const char* Symbol(Op op)
{
    switch (op)
    {
        case Op::Add: return "+";
        case Op::Sub: return "-";
        case Op::Mul: return "*";
        case Op::Div: return "\\";   // rasm's "/" divides in floating point (7/2 is 4 once rounded); "\" is C's
        case Op::Mod: return "%";
        case Op::And: return "&";
        case Op::Or: return "|";
        case Op::Xor: return "^";
        case Op::Shl: return "<<";
        case Op::Shr: return ">>";
        case Op::Equal: return "==";
        case Op::NotEqual: return "!=";
        case Op::Less: return "<";
        case Op::Greater: return ">";
        case Op::LessEqual: return "<=";
        case Op::GreaterEqual: return ">=";
        case Op::LogicalAnd: return "&&";
        case Op::LogicalOr: return "||";
        default: return nullptr;
    }
}

bool IsBoolean(Op op)
{
    return op == Op::Equal || op == Op::NotEqual || op == Op::Less || op == Op::Greater || op == Op::LessEqual || op == Op::GreaterEqual ||
           op == Op::LogicalAnd || op == Op::LogicalOr;
}

bool WhollyParenthesized(const std::string& v)
{
    if (v.size() < 2 || v.front() != '(' || v.back() != ')')
        return false;
    int depth = 0;
    for (size_t k = 0; k < v.size(); ++k)
    {
        if (v[k] == '(')
            ++depth;
        else if (v[k] == ')' && --depth == 0 && k + 1 != v.size())
            return false;
    }
    return true;
}

class RasmWriter : public RetroWriter
{
public:
    using RetroWriter::RetroWriter;

private:
    bool truthTop = false;   // the expression being written is a condition: only zero / not zero counts

    std::string Title() const override { return "rasm"; }
    size_t Significant() const override { return 120; }
    bool Reserved(const std::string& upper) const override
    {
        return kWords.count(upper) || z80::IsMnemonic(z80::Lower(upper)) || IntelHex(upper);
    }

    /// rasm reads a name of hex digits with an H after them as a number ("FCH" is #FC)
    static bool IntelHex(const std::string& upper)
    {
        return upper.size() >= 2 && upper.back() == 'H' &&
               std::all_of(upper.begin(), upper.end() - 1, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
    }

    /// Letters, digits and "_" in the source's case (rasm compares names in any case: Key() keeps them apart so)
    std::string Sanitized(const std::string& name) const override
    {
        std::string out;
        for (const char c : name)
            out.push_back(std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? c : '_');
        if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])))
            out = "L" + out;
        return out;
    }

    std::string Number(int64_t value) const override
    {
        if (value >= 0 && value < 256)
            return std::to_string(value);
        if (value >= 0 && value <= 0xFFFFFFFF)
            return "#" + z80::HexDigits(static_cast<uint64_t>(value), value <= 0xFFFF ? 4 : 8);
        return std::to_string(value);
    }

    std::string NumberText(const Expr& e) const
    {
        const int64_t v = e.value;
        switch (e.spelling)
        {
            case ir::NumberSpelling::Hex:
                if (v < 0)
                    break;
                return "#" + z80::HexDigits(static_cast<uint64_t>(v), std::max(e.digits, 1));
            case ir::NumberSpelling::Binary:
            {
                if (v < 0)
                    break;
                // 0b...: a "%" first could be read as rasm's modulo after an operand
                std::string bits;
                for (int k = std::max(e.digits, 1) - 1; k >= 0; --k)
                    bits.push_back(((v >> k) & 1) ? '1' : '0');
                return "0b" + bits;
            }
            case ir::NumberSpelling::Character:
                // a single printable character in quotes; rasm reads a quote or a backslash in one as an escape and
                // gives a constant of several characters 0, so those are written as their value
                if (e.text.size() == 1 && e.text[0] > 0x20 && e.text[0] < 0x7F && e.text[0] != '\'' && e.text[0] != '\\' && e.text[0] != '"')
                    return "'" + e.text + "'";
                break;
            default: break;
        }
        return std::to_string(v);
    }

    static Expr Grouped(Expr e)
    {
        Expr g = Expr::Make(Expr::Kind::Group);
        g.args.push_back(std::move(e));
        return g;
    }

    /// The priority the printed text of `e` has for an operator around it (0: an atom or a part in parentheses)
    int PrintedPriority(const Expr& e, bool truth) const
    {
        if (e.kind != Expr::Kind::Binary)
            return 0;
        if (IsBoolean(e.op) && !truth && True() != 1)
            return 0;   // written as (-(...))
        return Symbol(e.op) ? Priority(e.op) : 0;
    }

    /// An operand of a binary operator, in parentheses where rasm would bind it otherwise (a sign in front too: a--1)
    std::optional<std::string> Side(const Expr& e, int p, bool right, bool truth)
    {
        const auto t = Print(e, truth);
        if (!t)
            return std::nullopt;
        const int q = PrintedPriority(e, truth);
        if (q > p || (right && q == p && q != 0) || (right && !t->empty() && ((*t)[0] == '-' || (*t)[0] == '+')))
            return "(" + *t + ")";
        return t;
    }

    /// An operand of a unary operator or a function: an atom, else in parentheses
    std::optional<std::string> Atom(const Expr& e, bool truth = false)
    {
        const auto t = Print(e, truth);
        if (!t)
            return std::nullopt;
        const Expr& u = e;
        const bool atom = u.kind == Expr::Kind::Number || u.kind == Expr::Kind::Symbol || u.kind == Expr::Kind::Current ||
                          u.kind == Expr::Kind::Group || u.kind == Expr::Kind::Raw;
        if (atom && !t->empty() && (*t)[0] != '-' && (*t)[0] != '+')
            return t;
        return WhollyParenthesized(*t) ? *t : "(" + *t + ")";
    }

    std::optional<std::string> Joined(Op op, const Expr& a, const Expr& b, bool truth = false)
    {
        const int p = Priority(op);
        const auto l = Side(a, p, false, truth);
        const auto r = Side(b, p, true, truth);
        if (!l || !r)
            return std::nullopt;
        return *l + Symbol(op) + *r;
    }

    /// The source's arithmetic on a value rasm keeps whole (a double): 16-bit words where the source has them
    std::optional<std::string> Print(const Expr& e, bool truth)
    {
        switch (e.kind)
        {
            case Expr::Kind::Number: return NumberText(e);
            case Expr::Kind::Symbol: case Expr::Kind::Current: case Expr::Kind::Raw: return SymbolText(e);
            case Expr::Kind::CurrentPhysical: return std::string("$$");   // sjasmplus' $$$: rasm's $$ in ORG logical,physical
            case Expr::Kind::Group:
            {
                const auto t = Print(e.args[0], truth);
                if (!t)
                    return std::nullopt;
                return WhollyParenthesized(*t) ? *t : "(" + *t + ")";
            }
            case Expr::Kind::Unary:
                switch (e.op)
                {
                    case Op::Plus: return Print(e.args[0], truth);
                    case Op::Negate:
                    {
                        const auto a = Atom(e.args[0]);
                        return a ? std::optional<std::string>("-" + *a) : std::nullopt;
                    }
                    case Op::Not:
                    {
                        // rasm has no ~: -1-x
                        const auto a = Atom(e.args[0]);
                        return a ? std::optional<std::string>("(-1-" + *a + ")") : std::nullopt;
                    }
                    case Op::LogicalNot:
                    {
                        const auto a = Atom(e.args[0], true);
                        if (!a)
                            return std::nullopt;
                        // rasm's true is 1; the source's -1 (sjasmplus) where the value counts
                        return truth || True() == 1 ? "!" + *a : "(-(!" + *a + "))";
                    }
                    case Op::High: case Op::Low:
                    {
                        const auto t = Print(e.args[0], false);
                        return t ? std::optional<std::string>((e.op == Op::High ? "hi(" : "lo(") + *t + ")") : std::nullopt;
                    }
                    case Op::SwapBytes:
                    {
                        const Expr low = Expr::Binary(Op::And, e.args[0], Expr::Number(0xFF, ir::NumberSpelling::Hex, 2));
                        const Expr high = Expr::Binary(Op::And, e.args[0], Expr::Number(0xFFFF, ir::NumberSpelling::Hex, 4));
                        return Print(Grouped(Expr::Binary(Op::Or, Grouped(Expr::Binary(Op::Shl, Grouped(low), Expr::Number(8))),
                                                          Grouped(Expr::Binary(Op::Shr, Grouped(high), Expr::Number(8))))),
                                     false);
                    }
                    default: break;
                }
                break;
            case Expr::Kind::Binary:
            {
                const Expr& a = e.args[0];
                const Expr& b = e.args[1];
                if (e.op == Op::ShrUnsigned)
                {
                    // sjasmplus' >>>: the 32-bit value shifted in; rasm's >> keeps the sign, the mask takes it out
                    const auto n = Value(b);
                    if (!n || *n < 0 || *n > 31)
                        break;
                    if (*n == 0)
                        return Print(a, false);
                    const auto l = Side(a, Priority(Op::Shr), false, false);
                    if (!l)
                        return std::nullopt;
                    return "(" + *l + ">>" + std::to_string(*n) + "&#" + z80::HexDigits(0xFFFFFFFFull >> *n, 1) + ")";
                }
                if (!Symbol(e.op))
                    break;
                if (IsBoolean(e.op))
                {
                    const bool logical = e.op == Op::LogicalAnd || e.op == Op::LogicalOr;
                    const std::optional<std::string> t = Joined(e.op, a, b, logical);
                    if (!t)
                        return std::nullopt;
                    return truth || True() == 1 ? *t : "(-(" + *t + "))";
                }
                return Joined(e.op, a, b);
            }
            default: break;
        }
        // what rasm has no operator for (a memory read, sjasmplus' page $$, exist): its value, where known
        if (const auto v = Value(e))
            return Number(*v);
        Problem("an operation rasm has no operator for");
        return std::nullopt;
    }

    std::optional<std::string> Text(const Expr& e) override
    {
        const bool truth = truthTop;
        truthTop = false;
        // rasm computes in doubles and takes a value past 32 bits as #7FFFFFFF / #80000000 (its >>, & on it are no
        // longer the source's): such an expression as its value
        if (Overflows(e))
            if (const auto v = Value(e))
                return Number(*v);
        return Print(e, truth);
    }

    /// A part of the expression, or a product, sum or shift of two known parts, outside the 32-bit signed range
    bool Overflows(const Expr& e)
    {
        constexpr int64_t kLow = -(int64_t(1) << 31);
        constexpr int64_t kHigh = (int64_t(1) << 31) - 1;
        const auto outside = [&](int64_t v) { return v < kLow || v > kHigh; };
        if (e.kind == Expr::Kind::Number)
            return outside(e.value);
        if (e.kind == Expr::Kind::Binary)
        {
            const auto a = Value(e.args[0]);
            const auto b = Value(e.args[1]);
            if (a && b && !outside(*a) && !outside(*b))
            {
                const bool wide = (e.op == Op::Mul && outside(*a * *b)) || (e.op == Op::Add && outside(*a + *b)) ||
                                  (e.op == Op::Sub && outside(*a - *b)) ||
                                  (e.op == Op::Shl && *b >= 0 && (*b >= 32 || outside(*a * (int64_t(1) << *b))));
                if (wide)
                    return true;
            }
        }
        return std::any_of(e.args.begin(), e.args.end(), [&](const Expr& a) { return Overflows(a); });
    }

    /// An instruction's operand: a value wholly in parentheses would read as memory ("0+" keeps it a value); an index
    /// offset as its signed byte where known
    std::string OperandText(const Operand& o) override
    {
        if (o.kind == Operand::Kind::Immediate)
        {
            const std::string t = Expression(o.expr);
            return WhollyParenthesized(t) ? "0+" + t : t;
        }
        if (o.kind == Operand::Kind::Indexed)
        {
            if (const auto d = Value(o.expr))
            {
                const int64_t b = ((*d + 128) & 0xFF) - 128;
                return "(" + z80::Upper(o.text) + (b < 0 ? "-" : "+") + std::to_string(b < 0 ? -b : b) + ")";
            }
            return "(" + z80::Upper(o.text) + "+" + Expression(o.expr) + ")";
        }
        return RetroWriter::OperandText(o);
    }

    /// An item of DB / DW: the expression as it is (no instruction around it)
    std::string DataItem(const Operand& item, bool) override
    {
        return item.kind == Operand::Kind::Immediate ? Expression(item.expr) : RetroWriter::OperandText(item);
    }

    std::string RegisterText(const std::string& r) const override
    {
        return r == "af'" ? "AF'" : z80::Upper(r);
    }

    /// A string of DB: printable runs in double quotes; a quote, a backslash (rasm's escape), control characters and
    /// bytes above #7E (refused in quotes without -fq) as numbers
    void PutString(const std::string& text, std::vector<std::string>& items, std::vector<std::string>&, const std::function<void()>&) override
    {
        std::string run;
        auto end = [&]() {
            if (run.size() >= 2)
                items.push_back("\"" + run + "\"");
            else
                for (const char c : run)
                    items.push_back(std::to_string(static_cast<unsigned char>(c)));
            run.clear();
        };
        for (const char c : text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u >= 0x7F || c == '"' || c == '\\')
            {
                end();
                items.push_back(std::to_string(u));
            }
            else
                run.push_back(c);
        }
        end();
    }

    std::optional<std::vector<std::string>> OwnInstruction(const Statement& s) override
    {
        const std::string& m = s.mnemonic;
        const auto& ops = s.operands;
        auto reg = [&](size_t k, const char* name) { return k < ops.size() && ops[k].kind == Operand::Kind::Register && ops[k].text == name; };
        // IN (C) / IN F,(C): rasm takes the second form only
        if (m == "inf" || (m == "in" && (reg(0, "f") || (ops.size() == 1 && ops[0].kind == Operand::Kind::Indirect))))
            return std::vector<std::string>{"IN F,(C)"};
        if (m == "sli" && ops.size() == 1)
            return std::vector<std::string>{"SLL " + Operands(ops)};
        if (m == "exa")
            return std::vector<std::string>{"EX AF,AF'"};
        return std::nullopt;
    }

    // ---- IF / ELSE / ENDIF: a condition the layout does not decide (later labels as their values) ----

    std::optional<std::vector<std::string>> OpenNative(const Expr& condition, Condition&) override
    {
        truthTop = true;
        const std::string test = Known(condition);
        truthTop = false;
        return std::vector<std::string>{"IF " + test};
    }

    std::vector<std::string> ElseNative(Condition&) override { return {"ELSE"}; }
    std::vector<std::string> CloseNative(const Condition&) override { return {"ENDIF"}; }

    /// REPEAT n ... REND holds a body without labels, assignments or blocks of its own (those are unrolled, their
    /// labels one per pass); a count not above 0 is unrolled too (rasm's REPEAT without a count loops until UNTIL)
    RepeatForm Repeat(const std::vector<ir::Line>& lines, size_t begin, size_t end) const override
    {
        const Statement& open = lines[begin].statements.front();
        if (open.args.empty())
            return RepeatForm::Unrolled;
        if (const auto count = Value(open.args[0]); count && *count <= 0)
            return RepeatForm::Unrolled;
        for (size_t k = begin + 1; k < end; ++k)
        {
            if (!lines[k].label.empty())
                return RepeatForm::Unrolled;
            for (const Statement& s : lines[k].statements)
                if (IsDirective(s, ir::DirectiveKind::Defl) || IsDirective(s, ir::DirectiveKind::Equ) || IsDirective(s, ir::DirectiveKind::Repeat) ||
                    IsDirective(s, ir::DirectiveKind::While) || IsDirective(s, ir::DirectiveKind::If) || IsDirective(s, ir::DirectiveKind::Disp) ||
                    IsDirective(s, ir::DirectiveKind::Ent) || IsDirective(s, ir::DirectiveKind::Org) || IsDirective(s, ir::DirectiveKind::Other) ||
                    s.kind == Statement::Kind::MacroCall || (s.kind == Statement::Kind::Raw))
                    return RepeatForm::Unrolled;
        }
        return RepeatForm::Block;
    }

    /// sjasmplus' SAVEBIN "file",start,length: rasm's SAVE (written when the assembly ends)
    std::optional<std::vector<std::string>> OtherText(const std::string& word, const std::string& rest) override
    {
        if (word != "SAVEBIN")
            return std::nullopt;
        const FrontendResult parsed = SjasmplusFrontend().Parse(SourceDocument::FromText("        DB " + rest, "sjasmplus"));
        if (parsed.program.lines.empty() || parsed.program.lines[0].statements.empty())
            return std::vector<std::string>{NotConverted("SAVEBIN " + rest)};
        const Statement& items = parsed.program.lines[0].statements[0];
        if (items.operands.empty() || items.operands[0].kind != Operand::Kind::String)
            return std::vector<std::string>{NotConverted("SAVEBIN " + rest)};
        // the name as written (sjasmplus reads no escapes in a file name: "bin\fn.bin" is a DOS path)
        std::string name = items.operands[0].text;
        if (const size_t open = rest.find('"'); open != std::string::npos)
            if (const size_t close = rest.find('"', open + 1); close != std::string::npos)
                name = rest.substr(open + 1, close - open - 1);
        std::replace(name.begin(), name.end(), '\\', '/');
        std::string out = "SAVE \"" + name + "\"";
        for (size_t k = 1; k < items.operands.size(); ++k)
            out += "," + Known(items.operands[k].expr);
        if (items.operands.size() == 2)
            out += ",#10000-(" + Known(items.operands[1].expr) + ")";   // sjasmplus: to the end of memory
        return std::vector<std::string>{out};
    }

    std::string Count(const Expr& e) { return Known(e); }

    std::vector<std::string> DirectiveText(const Statement& s, const std::string&) override
    {
        auto expressions = [&](size_t from) {
            std::string out;
            for (size_t k = from; k < s.args.size(); ++k)
                out += (out.empty() ? "" : ",") + Known(s.args[k]);
            return out;
        };
        switch (s.directive)
        {
            case ir::DirectiveKind::Org:
            {
                displaced = false;
                std::vector<std::string> out = {"ORG " + Known(s.args[0])};
                if (s.args.size() > 1)
                    out.push_back(NotConverted("the page of ORG (rasm's BANK picks memory its own way)"));
                return out;
            }
            case ir::DirectiveKind::Ds:
            {
                const std::string count = Count(s.args[0]);
                if (s.operands.size() > 1 && !s.params.empty() && s.params[0] == "cyclic")
                {
                    // STORM's DS count,pattern: count bytes, the pattern repeated and cut
                    const auto n = Value(s.args[0]);
                    if (!n)
                        return {NotConverted("a fill pattern of a count known only when assembling")};
                    const int64_t k = static_cast<int64_t>(s.operands.size());
                    std::vector<std::string> out;
                    if (*n / k > 0)
                        out = {"REPEAT " + std::to_string(*n / k), "DB " + Operands(s.operands), "REND"};
                    if (*n % k > 0)
                        out.push_back("DB " + Operands(std::vector<Operand>(s.operands.begin(), s.operands.begin() + static_cast<std::ptrdiff_t>(*n % k))));
                    return out.empty() ? std::vector<std::string>{";DS 0"} : out;
                }
                if (s.operands.size() == 1 && s.operands[0].kind == Operand::Kind::Immediate)
                    return {"DEFS " + count + "," + Known(s.operands[0].expr)};
                if (!s.operands.empty())
                    return {"REPEAT " + count, "DB " + Operands(s.operands), "REND"};
                if (s.args.size() == 2)
                    return {"DEFS " + count + "," + Known(s.args[1])};
                if (s.args.size() > 2)
                    return {"REPEAT " + count, "DB " + expressions(1), "REND"};
                return {"DEFS " + count};
            }
            case ir::DirectiveKind::Repeat:
                nativeBlock = true;
                repeats.push_back(true);
                return {"REPEAT " + Count(s.args[0])};
            case ir::DirectiveKind::EndRepeat:
                if (repeats.empty())
                    return {NotConverted("the end of a repeat without its start")};
                repeats.pop_back();
                nativeBlock = false;
                return {"REND"};
            case ir::DirectiveKind::Disp:
                // ORG logical,physical: the code goes on where it is ($$), its labels count from the address given
                displaced = true;
                return {"ORG " + Known(s.args[0]) + ",$$"};
            case ir::DirectiveKind::Ent:
                if (s.text == "if-displaced" && !displaced)
                    return {};
                displaced = false;
                return {"ORG $$"};
            case ir::DirectiveKind::Include:
            {
                // a project's file: its name in the project, written with ".asm" (zxasm convert --project)
                if (const auto found = options.fileNames.find(s.text); found != options.fileNames.end())
                    return {"INCLUDE \"" + found->second + ".asm\""};
                if (!s.params.empty() && s.params[0] == "verbatim")
                    return {"INCLUDE \"" + s.text + "\""};
                return {"INCLUDE \"" + s.text + ".asm\""};
            }
            case ir::DirectiveKind::Incbin:
            {
                std::string name = s.text;
                if (const auto found = options.fileNames.find(name); found != options.fileNames.end())
                    name = found->second;
                std::vector<std::string> out = {"INCBIN \"" + name + "\"" + (s.args.empty() ? std::string() : "," + expressions(0))};
                if (!s.params.empty() && s.params[0] == "sector-slack")
                    out.push_back(NotConverted("the rest of the file's last sector after INCBIN (rasm reads the host file)"));
                return out;
            }
            case ir::DirectiveKind::SaveBinary:
                if (s.args.size() >= 2)
                    return {"SAVE \"" + s.text + "\"," + Known(s.args[0]) + "," + Known(s.args[1])};
                return {NotConverted("a SAVE without its start and length: " + s.text)};
            case ir::DirectiveKind::End:
                // rasm's END does not stop the assembly: the source's END is the last line anyway
                diagnostics.push_back({Severity::Info, line, 0, "END written as a comment (rasm assembles the whole text)"});
                return {";END"};
            case ir::DirectiveKind::Main: return {";MAIN \"" + s.text + "\" (the project's main source)"};
            case ir::DirectiveKind::Run: return {NotConverted("RUN (code called while assembling)")};
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }

    /// The label in column 0, the statement from column 8
    std::vector<std::string> Layout(const std::string& label, const std::vector<std::string>& texts, bool bare, bool hasComment,
                                    const std::string& comment) override
    {
        std::vector<std::string> out;
        bool first = true;
        for (const std::string& t : texts)
        {
            std::string text = first ? label : std::string();
            first = false;
            if (!t.empty() && t[0] == ';')
            {
                if (!text.empty())
                    out.push_back(text);
                out.push_back(t);
                continue;
            }
            text.append(text.size() < 8 ? 8 - text.size() : 1, ' ');
            out.push_back(text + t);
        }
        if (first && (!label.empty() || hasComment || bare))
            out.push_back(label);
        if (out.empty())
            return out;
        if (hasComment)
        {
            std::string& last = out.back();
            if (!last.empty())
                last.append(last.size() < 32 ? 32 - last.size() : 1, ' ');
            last += ";" + Blanked(comment);
        }
        return out;
    }
};
}  // namespace

BackendResult RasmBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    BackendResult result;
    if (program.dialect != "sjasmplus")
    {
        // Another dialect goes through its sjasmplus form (the sjasmplus backend knows every frontend's arithmetic,
        // LOCAL blocks and macro parameters), read again; its lines keep the source's line numbers and its renamed
        // labels the source's values
        const BackendResult first = SjasmplusBackend().Write(program, options);
        SourceDocument text = first.document;
        text.dialect = "sjasmplus";
        text.z80n = program.z80n;
        FrontendResult parsed = SjasmplusFrontend().Parse(text);
        for (ir::Line& l : parsed.program.lines)
            l.sourceLine = l.sourceLine >= 1 && l.sourceLine <= text.lines.size() ? text.lines[l.sourceLine - 1].origin : 0;
        parsed.program.z80n = program.z80n;
        parsed.program.displacementAcrossFiles = program.displacementAcrossFiles;
        BackendOptions renamed = options;
        for (const LabelName& n : first.labels)
            if (n.written != n.source)
                if (const auto found = options.labelValues.find(n.source); found != options.labelValues.end())
                    renamed.labelValues.emplace(n.written, found->second);
        result.diagnostics = first.diagnostics;
        result.macroParams = first.macroParams;
        result.ifUsedNames = first.ifUsedNames;
        result.definitions = first.definitions;
        for (const Diagnostic& d : parsed.diagnostics)
            if (d.severity != Severity::Info)
                result.diagnostics.push_back(d);
        RasmWriter w{parsed.program, renamed, result};
        w.Run();
        // the labels by the source's names
        std::map<std::string, const LabelName*> bySjasmplus;
        for (const LabelName& n : first.labels)
            bySjasmplus.emplace(n.written, &n);
        for (LabelName& n : result.labels)
            if (const auto found = bySjasmplus.find(n.source); found != bySjasmplus.end())
            {
                n.source = found->second->source;
                n.local = found->second->local;
                n.inMacro = n.inMacro || found->second->inMacro;
            }
        result.document.dialect = "rasm";
        return result;
    }
    RasmWriter w{program, options, result};
    w.Run();
    result.document.dialect = "rasm";
    // UTF-8: the strings' bytes above #7E are written as numbers, only comments keep the source's letters
    return result;
}
}  // namespace unrealasm::dialects
