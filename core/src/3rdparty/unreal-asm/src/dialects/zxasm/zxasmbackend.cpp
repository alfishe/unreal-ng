#include "dialects/zxasm/zxasmbackend.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>

#include "dialects/common/diskname.h"
#include "dialects/common/retrowriter.h"
#include "dialects/common/z80.h"

namespace unrealasm::dialects
{
namespace
{
using ir::Expr;
using ir::Op;
using ir::Operand;
using ir::Statement;

// ZX-ASM's keywords (its token table, research-zxasm.md §3) beside the mnemonics: a word that is one is stored as a
// token, so a label must not be one
const std::set<std::string> kWords = {
    "ORG", "EQU", "DB", "DW", "DS", "DEFB", "DEFW", "DEFS", "DC", "DBW", "INSERT", "INCLUDE", "IF", "IFDEF", "IFNDEF",
    "IFUSED", "IFNUSED", "ELSE", "ENDIF", "MAKE", "PHASE", "UNPHASE", "ENT", "REPT", "ENDR", "REPL", "LOADTAB", "MACRO",
    "ENDM", "EXITM", "IFP", "CREATE", "MAKELAB", "SAVEOBJ", "PROJECT", "PUBLIC", "ENDA", "LOADOBJ", "CHD", "EXA", "INF",
    "SLI", "A", "B", "C", "D", "E", "H", "L", "I", "R", "XH", "XL", "YH", "YL", "AF", "BC", "DE", "HL", "IX", "IY", "SP",
    "NZ", "Z", "NC", "PO", "PE", "P", "M",
};

/// A comment for ZX-ASM: ASCII and Russian letters (CP866: ZX-ASM takes the first character outside ASCII for the start
/// of a comment, so the ";" stays in front); others as "?"
std::string CommentText(const std::string& utf8)
{
    std::string out;
    for (size_t p = 0; p < utf8.size();)
    {
        const unsigned char b = static_cast<unsigned char>(utf8[p]);
        if (b < 0x80)
        {
            out.push_back(b < 0x20 || b == 0x7F ? ' ' : static_cast<char>(b));
            ++p;
            continue;
        }
        const size_t length = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
        char32_t c = length == 1 ? b : b & (0x7F >> length);
        for (size_t k = 1; k < length && p + k < utf8.size(); ++k)
            c = (c << 6) | (static_cast<unsigned char>(utf8[p + k]) & 0x3F);
        if ((c >= U'А' && c <= U'я') || c == U'Ё' || c == U'ё')
            out.append(utf8, p, length);
        else
            out.push_back('?');
        p += length;
    }
    return out;
}

class ZxasmWriter : public RetroWriter
{
public:
    ZxasmWriter(const ir::Program& program, const BackendOptions& options, BackendResult& result)
        : RetroWriter(program, options, result), v300(options.targetVersion == "3.00" || options.targetVersion == "2"),
          v2(options.targetVersion == "2")
    {
    }

private:
    /// ZX ASM 3.0 itself (1996), seen in unreal-ng: no & | ! and no . functions (an error), no REPT, EXA, OUT (C),0 or
    /// DS fill; 3.10 has them all. ZX-ASM 2.x the same (2.6 seen)
    bool v300 = false;
    /// ZX-ASM 2.x: no parentheses in an expression either (a syntax error, 2.6 seen): such a part as its value
    bool v2 = false;

    std::string Title() const override { return "ZX-ASM"; }
    size_t Significant() const override { return 24; }
    bool Reserved(const std::string& upper) const override { return kWords.count(upper) || z80::IsMnemonic(z80::Lower(upper)); }

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
                if (e.text.size() == 1 && e.text[0] > 0x20 && e.text[0] < 0x7F && e.text[0] != '"')
                    return "\"" + e.text + "\"";
                return Number(v);
            default: return std::to_string(v);
        }
    }

    /// ZAsm 3.15 multiplies a*b by a's bits from the top and stops at the first carry: 7*#4000 is #8000, #8001*4 is 0
    void Multiplied(const Expr& a, std::optional<int64_t> b)
    {
        const auto x = Value(a);
        if (!x || !b)
            return;
        const uint32_t multiplier = static_cast<uint32_t>(*x & 0xFFFF), multiplicand = static_cast<uint32_t>(*b & 0xFFFF);
        if (z80::StopAtCarryProduct(multiplicand, multiplier) != ((multiplier * multiplicand) & 0xFFFF))
            Problem("a multiplication ZX-ASM makes otherwise");
    }

    static bool IsAtom(const Expr& e)
    {
        const Expr& u = Unwrapped(e);
        return u.kind == Expr::Kind::Number || u.kind == Expr::Kind::Symbol || u.kind == Expr::Kind::Current || u.kind == Expr::Kind::Raw;
    }

    /// One operand of an operator or a function: an atom, or a part in parentheses
    std::optional<std::string> Term(const Expr& e)
    {
        const Expr& u = Unwrapped(e);
        if (u.kind == Expr::Kind::Unary && (u.op == Op::High || u.op == Op::Low || u.op == Op::Negate || u.op == Op::Not || u.op == Op::SwapBytes))
            return Print(u);   // a term with its function
        if (v2 && !IsAtom(e))
        {
            if (const auto v = Value(e))
                return Number(*v);
            Problem("a part in parentheses (ZX-ASM 2.x has none) of a value known only when assembling");
            return std::nullopt;
        }
        const auto t = Print(e);
        if (!t)
            return std::nullopt;
        return IsAtom(e) ? *t : "(" + *t + ")";
    }

    /// A chain left to right whose operators each take a term; the functions on the term before them
    std::optional<std::string> Print(const Expr& e)
    {
        switch (e.kind)
        {
            case Expr::Kind::Number: return NumberText(e);
            case Expr::Kind::Symbol: case Expr::Kind::Current: case Expr::Kind::Raw: return SymbolText(e);
            case Expr::Kind::Group: return Print(e.args[0]);
            case Expr::Kind::Unary:
            {
                const char* function = e.op == Op::High ? ".h" : e.op == Op::Low ? ".b" : e.op == Op::Negate ? ".n" : e.op == Op::Not ? ".c"
                                       : e.op == Op::SwapBytes                     ? ".e"
                                                                                   : nullptr;
                if (e.op == Op::Plus)
                    return Print(e.args[0]);
                if (!function)
                    break;
                if (v300)
                {
                    // high as /256, -x as 0-x, ~x as #FFFF-x; the rest as its value
                    const auto t = Term(e.args[0]);
                    if (!t)
                        return std::nullopt;
                    if (e.op == Op::High)
                        return *t + "/256";
                    if (e.op == Op::Negate)
                        return "0-" + *t;
                    if (e.op == Op::Not)
                        return "#FFFF-" + *t;
                    break;
                }
                // a function on a term with its own function in parentheses: ZAsm 3.15 refuses (x).c.b
                const Expr& inner = Unwrapped(e.args[0]);
                const bool nested = inner.kind == Expr::Kind::Unary && inner.op != Op::Plus && inner.op != Op::LogicalNot;
                auto t = Term(e.args[0]);
                if (t && nested)
                    t = "(" + *t + ")";
                return t ? std::optional<std::string>(*t + function) : std::nullopt;
            }
            case Expr::Kind::Binary:
            {
                const Expr& a = e.args[0];
                const Expr& b = e.args[1];
                std::string symbol;
                switch (e.op)
                {
                    case Op::Add: symbol = "+"; break;
                    case Op::Sub: symbol = "-"; break;
                    case Op::Mul: Multiplied(a, Value(b)); symbol = "*"; break;
                    case Op::And: symbol = v300 ? "" : "&"; break;
                    case Op::Or:
                    {
                        // ZAsm 3.15's | ORs the low bytes only, the high byte is the left one's (1|#8000 is 1, measured)
                        const auto x = Value(a), y = Value(b);
                        if (x && y && (*y & 0xFF00 & ~*x) != 0)
                            Problem("an OR of high bytes ZX-ASM drops");
                        symbol = v300 ? "" : "|";
                        break;
                    }
                    case Op::Xor: symbol = v300 ? "" : "!"; break;
                    case Op::Div: case Op::Mod:
                        if (!InWord(a) || !InWord(b))
                            Problem("a division ZX-ASM makes in 16-bit unsigned words");
                        symbol = e.op == Op::Div ? "/" : "\\";
                        break;
                    case Op::Shl: case Op::Shr: case Op::ShrUnsigned:
                    {
                        const auto shift = Value(b);
                        if (!shift || *shift < 0 || *shift > 15)
                            break;
                        if (e.op != Op::Shl && !InWord(a))
                            Problem("a right shift of a value outside 0..#FFFF");
                        if (e.op == Op::Shl)
                            Multiplied(a, int64_t{1} << *shift);
                        const auto l = Print(a);
                        if (!l)
                            return std::nullopt;
                        return *l + (e.op == Op::Shl ? "*" : "/") + Number(int64_t{1} << *shift);
                    }
                    default: break;
                }
                if (symbol.empty())
                    break;
                const auto l = Print(a);
                const auto r = Term(b);
                if (!l || !r)
                    return std::nullopt;
                return *l + symbol + *r;
            }
            default: break;
        }
        // An operation ZX-ASM lacks (a comparison, a 16-bit rotation of a computed value ...): its value, where known
        if (const auto v = Value(e))
            return Number(*v);
        Problem("an operation ZX-ASM has no operator for");
        return std::nullopt;
    }

    std::optional<std::string> Text(const Expr& e) override
    {
        auto t = Print(e);
        // a whole operand in parentheses reads as memory in an instruction: 0+ keeps it a value
        if (t && !t->empty() && (*t)[0] == '(')
            *t = "0+" + *t;
        return t;
    }

    std::string RegisterText(const std::string& r) const override
    {
        if (r == "ixh") return "XH";
        if (r == "ixl") return "XL";
        if (r == "iyh") return "YH";
        if (r == "iyl") return "YL";
        return r == "af'" ? "AF'" : RetroWriter::RegisterText(r);
    }

    /// A string of DB: printable runs of three characters or more quoted; the quote, control characters and bytes above
    /// #7F (a comment's start in ZX-ASM) as numbers
    void PutString(const std::string& text, std::vector<std::string>& items, std::vector<std::string>&, const std::function<void()>&) override
    {
        std::string run;
        auto end = [&]() {
            if (run.size() >= 3)
                items.push_back("\"" + run + "\"");
            else
                for (const char c : run)
                    items.push_back(std::to_string(static_cast<unsigned char>(c)));
            run.clear();
        };
        for (const char c : text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u >= 0x7F || c == '"' || c == '~' || c == '\'')
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
        if (m == "inf" || (m == "in" && (reg(0, "f") || (ops.size() == 1 && ops[0].kind == Operand::Kind::Indirect))))
            return std::vector<std::string>{"INF"};
        if ((m == "sll" || m == "sli") && ops.size() == 1)
            return std::vector<std::string>{"SLI " + Operands(ops)};
        if (m == "ex" && reg(0, "af") && reg(1, "af'"))
            return std::vector<std::string>{v300 ? "EX AF,AF'" : "EXA"};
        if (v300 && m == "out" && ops.size() == 2 && ops[0].kind == Operand::Kind::Indirect && ops[1].kind == Operand::Kind::Immediate)
            return std::vector<std::string>{Bytes({"#ED", "#71"}, "OUT (C),0")};
        return std::nullopt;
    }

    // ---- IF (true when not 0) / ELSE / ENDIF ----

    std::optional<std::vector<std::string>> OpenNative(const Expr& condition, Condition& c) override
    {
        const Expr& e = Unwrapped(condition);
        if (e.kind == Expr::Kind::Binary && (e.op == Op::Equal || e.op == Op::NotEqual))
        {
            c.test = Expression(Expr::Binary(Op::Sub, e.args[0], e.args[1]));
            c.whenZero = e.op == Op::Equal;
        }
        else if (e.kind == Expr::Kind::Unary && e.op == Op::LogicalNot)
        {
            c.test = Expression(e.args[0]);
            c.whenZero = true;
        }
        else if (e.kind == Expr::Kind::Binary && e.op >= Op::Less && e.op <= Op::LogicalOr)
            return std::nullopt;   // no comparison operators
        else
        {
            c.test = Expression(e);
            c.whenZero = false;
        }
        if (!c.whenZero)
            return std::vector<std::string>{"IF " + c.test};
        return std::vector<std::string>{"IF " + c.test, "ELSE"};
    }

    std::vector<std::string> ElseNative(Condition&) override { return {"ELSE"}; }
    std::vector<std::string> CloseNative(const Condition&) override { return {"ENDIF"}; }

    /// REPT n ... ENDR holds a body without labels (ZX-ASM makes them local to each pass, sjasmplus does not),
    /// assignments or blocks of its own
    RepeatForm Repeat(const std::vector<ir::Line>& lines, size_t begin, size_t end) const override
    {
        if (v300)
            return RepeatForm::Unrolled;
        for (size_t k = begin + 1; k < end; ++k)
        {
            if (!lines[k].label.empty())
                return RepeatForm::Unrolled;
            for (const Statement& s : lines[k].statements)
                if (IsDirective(s, ir::DirectiveKind::Defl) || IsDirective(s, ir::DirectiveKind::Repeat) || IsDirective(s, ir::DirectiveKind::While) ||
                    IsDirective(s, ir::DirectiveKind::If) || s.kind == Statement::Kind::MacroCall)
                    return RepeatForm::Unrolled;
        }
        return RepeatForm::Block;
    }

    /// sjasmplus' SAVEBIN "file",start,length: ZX-ASM's SAVEOBJ (the operands as written)
    std::optional<std::vector<std::string>> OtherText(const std::string& word, const std::string& rest) override
    {
        if (word != "SAVEBIN")
            return std::nullopt;
        // the names in the target's spelling (ZX-ASM writes them in capitals; the file name in quotes as it is)
        std::string out;
        for (size_t k = 0; k < rest.size();)
        {
            const char c = rest[k];
            if (c == '"' || c == '\'')
            {
                const size_t end = rest.find(c, k + 1);
                const size_t stop = end == std::string::npos ? rest.size() : end + 1;
                out += rest.substr(k, stop - k);
                k = stop;
            }
            else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.')
            {
                size_t j = k;
                while (j < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[j])) || std::string("_.?@!").find(rest[j]) != std::string::npos))
                    ++j;
                const bool number = k > 0 && (rest[k - 1] == '#' || std::isdigit(static_cast<unsigned char>(rest[k - 1])));
                out += number ? rest.substr(k, j - k) : Name(rest.substr(k, j - k));
                k = j;
            }
            else
                out += rest[k++];
        }
        return std::vector<std::string>{"SAVEOBJ " + out};
    }

    std::vector<std::string> DirectiveText(const Statement& s, const std::string&) override
    {
        switch (s.directive)
        {
            case ir::DirectiveKind::Org:
                displaced = false;
                return {"ORG " + Known(s.args[0])};
            case ir::DirectiveKind::Ds:
            {
                // DS n,fill...: the fill sequence repeated n times; a pattern cut at n bytes as DB
                const std::string count = Known(s.args[0]);
                const bool fill = s.args.size() > 1 || !s.operands.empty();
                if (s.operands.empty() && (!fill || !v300))
                    return {"DS " + count + (s.args.size() > 1 ? "," + Known(s.args[1]) : std::string())};
                if (s.operands.size() == 1 && !v300)
                    return {"DS " + count + "," + Known(s.operands[0].expr)};
                const auto n = Value(s.args[0]);
                if (!n)
                    return {NotConverted("a fill pattern of a count known only when assembling")};
                std::vector<std::string> items;
                for (int64_t k = 0; k < *n; ++k)
                {
                    const auto v = s.operands.empty() ? Value(s.args[1]) : Value(s.operands[static_cast<size_t>(k) % s.operands.size()].expr);
                    if (!v)
                        return {NotConverted("DS with a fill known only when assembling")};
                    items.push_back(Number(*v & 0xFF));
                }
                return DataLines(Db(), items);
            }
            case ir::DirectiveKind::Repeat:
                nativeBlock = true;
                repeats.push_back(true);
                return {"REPT " + Known(s.args[0])};
            case ir::DirectiveKind::EndRepeat:
                if (repeats.empty())
                    return {NotConverted("EDUP without DUP")};
                repeats.pop_back();
                nativeBlock = false;
                return {"ENDR"};
            case ir::DirectiveKind::Disp: displaced = true; return {"PHASE " + Known(s.args[0])};
            case ir::DirectiveKind::Ent:
                if (s.text == "if-displaced" && !displaced)
                    return {};
                displaced = false;
                return {"UNPHASE"};
            case ir::DirectiveKind::Include: return {"INCLUDE \"" + DiskName(options, s.text) + "\""};
            case ir::DirectiveKind::Incbin:
                if (s.args.size() > 1 || (!s.args.empty() && !(s.args[0].kind == Expr::Kind::Number && s.args[0].value == 0)))
                    return {NotConverted("INCBIN of a part of a file (INSERT takes it all): " + s.text)};
                return {"INSERT \"" + DiskName(options, s.text) + "\""};
            case ir::DirectiveKind::SaveBinary:
                if (s.args.size() >= 2)
                    return {"SAVEOBJ \"" + s.text + "\"," + Known(s.args[0]) + "," + Known(s.args[1])};
                return {"SAVEOBJ \"" + s.text + "\""};
            case ir::DirectiveKind::End:
                diagnostics.push_back({Severity::Info, line, 0, "END dropped (ZX-ASM assembles the whole text)"});
                return {";END"};
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }

    /// The label in column 0, the command from column 8, the operands after one blank (ZAsm's editor shows them so)
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
                out.push_back(text.empty() ? t : text + " " + t);
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
            out.back() += (out.back().empty() ? ";" : " ;") + CommentText(comment);
        return out;
    }
};
}  // namespace

BackendResult ZxasmBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    BackendResult result;
    result.document.dialect = "zxasm";
    ZxasmWriter w{program, options, result};
    w.Run();
    return result;
}
}  // namespace unrealasm::dialects
