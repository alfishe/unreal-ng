#include "dialects/storm/stormbackend.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <set>

#include "codecs/storm/stormcodec.h"
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

// STORM's keyword table TBTK (research-storm.md §3) beside the mnemonics: a label must not be one of them
const std::set<std::string> kWords = {
    "ORG", "EQU", "EXA", "INF", "SLI", "DB", "DW", "DS", "DEFB", "DEFW", "DEFS", "INCL", "INCB", "REPT", "ENDR", "IF", "IFU",
    "IFNU", "IFD", "IFND", "ELSE", "EIF", "ENDM", "A", "B", "C", "D", "E", "H", "L", "I", "R", "HX", "LX", "HY", "LY", "XH",
    "XL", "YH", "YL", "AF", "BC", "DE", "HL", "SP", "IX", "IY", "NZ", "Z", "NC", "PO", "PE", "P", "M",
};

/// The longest line STORM's editor shows and edits (research-storm.md §2)
constexpr size_t kWidth = 40;
/// The operands' part of it: the command from column 8, a blank after it
constexpr size_t kOperandWidth = kWidth - 8 - 5;

/// A comment in characters STORM keeps (CP866): ASCII, Russian letters; others as "?", control characters as blanks
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

/// The characters of a UTF-8 text (a Russian letter is one on STORM's screen)
size_t Width(const std::string& utf8)
{
    return static_cast<size_t>(std::count_if(utf8.begin(), utf8.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

class StormWriter : public RetroWriter
{
public:
    StormWriter(const ir::Program& program, const BackendOptions& options, BackendResult& result) : RetroWriter(program, options, result) {}

private:
    // ---- the dialect ----

    std::string Title() const override { return "STORM"; }
    /// STORM packs labels of up to 7 characters (research-storm.md §3)
    size_t Significant() const override { return 7; }
    bool Reserved(const std::string& upper) const override { return kWords.count(upper) || z80::IsMnemonic(z80::Lower(upper)); }
    /// letters, digits, "_" (a letter first: a line starting "_" reads as a command in column 0 where one follows)
    std::string Sanitized(const std::string& name) const override
    {
        std::string out = RetroWriter::Sanitized(name);
        out.erase(0, out.find_first_not_of('_'));
        if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])))
            out = "L" + out;
        return out;
    }

    /// The text of a number in the form the source gave it, where STORM has it
    std::string NumberText(const Expr& e) const
    {
        const int64_t v = e.value & 0xFFFF;
        switch (e.spelling)
        {
            // as STORM shows them: a hex byte in 2 digits, a word in 4; binary in 8 or 16
            case ir::NumberSpelling::Hex: return "#" + z80::HexDigits(static_cast<uint64_t>(v), v > 0xFF || e.digits > 2 ? 4 : 2);
            case ir::NumberSpelling::Binary:
            {
                std::string bits;
                for (int k = v > 0xFF ? 15 : 7; k >= 0; --k)
                    bits.push_back(((v >> k) & 1) ? '1' : '0');
                return "%" + bits;
            }
            case ir::NumberSpelling::Character:
                // a character constant of one printable character (two are a word with the first high in STORM)
                if (e.text.size() == 1 && e.text[0] > 0x20 && e.text[0] < 0x7F && e.text[0] != '"')
                    return "\"" + e.text + "\"";
                return Number(v);
            default: return std::to_string(v);
        }
    }

    // An expression's text and what it binds as: an atom (9), a binary operator's priority, a postfix operator's
    struct Printed
    {
        std::string text;
        int priority = 9;
        bool postfix = false;   ///< ends with a postfix operator
        bool lost = false;      ///< holds a group STORM 1.3 cannot compute
    };

    /// In parentheses STORM 1.3 computes only + - * / \ << >> and ~ [ ] (priorities 4 and up): & ! | lose their right
    /// operand (1+(6&3) = 7, 9-(+6&3) = 3), @ is dropped (1+(0@) = 1), a comparison is a syntax error, (6!1) and a
    /// following *2 stop STORM with "3 Subscript wrong" (STORM's own tokens of the lines typed in its editor, 2026-10-19).
    /// The same holds for an index's (IX+d), not for a memory operand's (5!2)
    static Printed Parenthesized(const Printed& p) { return {"(" + p.text + ")", 9, false, p.lost || p.priority <= 3}; }

    /// `x` with a postfix operator of priority `q`: it acts on what the operators of priority q and above built
    static Printed Postfix(const Printed& x, const char* symbol, int q)
    {
        const Printed operand = x.priority < q ? Parenthesized(x) : x;
        return {operand.text + symbol, q, true, operand.lost};
    }

    /// `l op r`, equal priorities left to right. A postfix operand on the left is parenthesized: STORM 1.3 computes
    /// 2*3~*2 as -6 (research-storm-to-sjasmplus.md §3)
    static Printed Infix(const Printed& l, const char* symbol, int p, const Printed& r)
    {
        const Printed left = l.priority < p || l.postfix ? Parenthesized(l) : l;
        const Printed right = r.priority <= p ? Parenthesized(r) : r;
        return {left.text + symbol + right.text, p, false, left.lost || right.lost};
    }

    /// STORM's 1 for true as the source's (sjasmplus -1: negated)
    Printed Truth(const Printed& storm) const { return True() == 1 ? storm : Postfix(storm, "~", 6); }

    static bool IsTruth(const Expr& e)
    {
        const Expr& u = Unwrapped(e);
        return (u.kind == Expr::Kind::Unary && u.op == Op::LogicalNot) ||
               (u.kind == Expr::Kind::Binary && (u.op == Op::Less || u.op == Op::Greater || u.op == Op::LessEqual || u.op == Op::GreaterEqual ||
                                                 u.op == Op::Equal || u.op == Op::NotEqual || u.op == Op::LogicalAnd || u.op == Op::LogicalOr));
    }

    static bool IsWordMask(const Expr& e) { return Unwrapped(e).kind == Expr::Kind::Number && (Unwrapped(e).value & 0xFFFFFFFF) == 0xFFFF; }

    /// A comparison or a logical operation as STORM gives it: 1 / 0
    std::optional<Printed> StormTruth(const Expr& e)
    {
        const bool saved = stormTruth;
        stormTruth = true;
        auto p = Print(e);
        stormTruth = saved;
        return p;
    }
    bool stormTruth = false;   ///< the operation being printed gives STORM's 1 / 0, not the source's truth

    /// A truth value as the context wants it
    Printed Truthful(const Printed& storm)
    {
        if (stormTruth)
        {
            stormTruth = false;
            return storm;
        }
        return Truth(storm);
    }

    /// STORM stores a parenthesized part after - / & | >> >= > beginning with an operator (its own source writes
    /// (+C2-KEYTXT), research-storm.md §4): a "+" goes in there
    static std::string SubExpressions(const std::string& text)
    {
        static const char* kOdd[] = {">>", ">=", "-", "/", "&", "|", ">"};
        static const char* kAll[] = {"<<", ">>", "<=", ">=", "+", "-", "*", "/", "\\", "&", "!", "|", "<", ">", "="};
        std::string out;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[i];
            out.push_back(c);
            if (c == '"')
            {
                const size_t close = text.find('"', i + 1);
                const size_t end = close == std::string::npos ? text.size() : close + 1;
                out.append(text, i + 1, end - i - 1);
                i = end - 1;
                continue;
            }
            if (c != '(' || out.size() < 2)
                continue;
            const std::string before = out.substr(0, out.size() - 1);
            bool odd = false;
            for (const char* op : kAll)
            {
                const std::string o = op;
                if (before.size() >= o.size() && before.compare(before.size() - o.size(), o.size(), o) == 0)
                {
                    odd = std::any_of(std::begin(kOdd), std::end(kOdd), [&](const char* k) { return o == k; });
                    break;
                }
            }
            if (odd && i + 1 < text.size() && text[i + 1] != '+' && text[i + 1] != '-')
                out.push_back('+');
        }
        return out;
    }

    std::optional<Printed> Print(const Expr& e)
    {
        switch (e.kind)
        {
            case Expr::Kind::Number: return Printed{NumberText(e)};
            case Expr::Kind::Symbol: case Expr::Kind::Current: case Expr::Kind::Raw:
                if (const auto t = SymbolText(e))
                    return Printed{*t};
                return std::nullopt;
            case Expr::Kind::Group: return Print(e.args[0]);
            case Expr::Kind::Unary:
            {
                if (e.op == Op::Not)   // ~x = x | #FFFF (STORM's | is XOR)
                    return Print(Expr::Binary(Op::Xor, e.args[0], Expr::Number(0xFFFF, ir::NumberSpelling::Hex, 4)));
                if (e.op != Op::Plus && e.op != Op::Negate && e.op != Op::High && e.op != Op::Low && e.op != Op::LogicalNot)
                    break;
                if (e.op == Op::Negate && True() == -1 && IsTruth(e.args[0]))
                    return StormTruth(e.args[0]);   // -(a<b): STORM's 1 for true is the source's -1 negated
                const bool truth = stormTruth;
                stormTruth = false;
                const auto x = Print(e.args[0]);
                stormTruth = truth;
                if (!x)
                    return std::nullopt;
                switch (e.op)
                {
                    case Op::Plus: return x;
                    case Op::Negate: return Postfix(*x, "~", 6);
                    case Op::High: return Postfix(*x, "[", 8);
                    case Op::Low: return Postfix(*x, "]", 8);
                    default: return Truthful(Postfix(*x, "@", 2));
                }
            }
            case Expr::Kind::Binary:
            {
                struct Infixed
                {
                    Op op;
                    const char* symbol;
                    int priority;
                    bool word;   ///< computed in 16-bit unsigned words: the operands must lie in 0..#FFFF
                };
                static const Infixed kInfix[] = {
                    {Op::Add, "+", 4, false}, {Op::Sub, "-", 4, false}, {Op::Mul, "*", 7, false}, {Op::Div, "/", 7, true},
                    {Op::Mod, "\\", 7, true}, {Op::And, "&", 3, false}, {Op::Or, "!", 3, false},  {Op::Xor, "|", 3, false},
                    {Op::Shl, "<<", 5, false}, {Op::Shr, ">>", 5, true}, {Op::ShrUnsigned, ">>", 5, true},
                    {Op::Less, "<", 2, true}, {Op::Greater, ">", 2, true}, {Op::LessEqual, "<=", 2, true},
                    {Op::GreaterEqual, ">=", 2, true}, {Op::Equal, "=", 2, true}, {Op::NotEqual, "=", 2, true},
                };
                const Expr& a = e.args[0];
                const Expr& b = e.args[1];
                const bool truth = stormTruth;
                stormTruth = false;
                if (e.op == Op::And && (IsWordMask(a) || IsWordMask(b)))
                {
                    // x & #FFFF: STORM's words are 16 bits already
                    stormTruth = truth;
                    return Print(IsWordMask(b) ? a : b);
                }
                if (e.op == Op::LogicalAnd || e.op == Op::LogicalOr)
                {
                    // each side as 1 / 0 (x@@), then AND / OR
                    const auto l = Print(a), r = Print(b);
                    if (!l || !r)
                        return std::nullopt;
                    const Printed truthL = Postfix(Postfix(*l, "@", 2), "@", 2), truthR = Postfix(Postfix(*r, "@", 2), "@", 2);
                    stormTruth = truth;
                    return Truthful(Infix(truthL, e.op == Op::LogicalAnd ? "&" : "!", 3, truthR));
                }
                for (const Infixed& i : kInfix)
                {
                    if (i.op != e.op)
                        continue;
                    if (i.word && (!InWord(a) || !InWord(b)))
                        Problem("an operation STORM makes in 16-bit unsigned words on a value outside 0..#FFFF");
                    if (e.op == Op::Shl || e.op == Op::Shr || e.op == Op::ShrUnsigned)
                    {
                        const auto shift = Value(b);
                        if (!shift || *shift < 0 || *shift > 15)
                            Problem("a shift by a count outside 0..15");
                    }
                    const auto l = Print(a), r = Print(b);
                    if (!l || !r)
                        return std::nullopt;
                    const Printed p = Infix(*l, i.symbol, i.priority, *r);
                    stormTruth = truth;
                    if (e.op == Op::NotEqual)
                        return Truthful(Postfix(p, "@", 2));
                    return i.priority == 2 ? Truthful(p) : p;
                }
                break;
            }
            default: break;
        }
        // An operation STORM lacks (a rotation, a byte swap ...): its value, where known
        if (const auto v = Value(e))
            return Printed{Number(*v)};
        Problem("an operation STORM has no operator for");
        return std::nullopt;
    }

    std::optional<std::string> Text(const Expr& e) override
    {
        stormTruth = false;
        const auto p = Print(e);
        if (!p)
            return std::nullopt;
        if (p->lost || (indexed && p->priority <= 3))
            Problem("STORM 1.3 cannot compute & ! | @ or a comparison in parentheses");   // written as its value
        const std::string text = SubExpressions(p->text);
        if (text.size() > kOperandWidth && Value(e))
            Problem("too long for STORM's line");   // written as its value
        return text;
    }

    bool indexed = false;   ///< the displacement of (IX+d) being written

    std::string OperandText(const Operand& o) override
    {
        indexed = o.kind == Operand::Kind::Indexed;
        const std::string text = RetroWriter::OperandText(o);
        indexed = false;
        return text;
    }

    std::string RegisterText(const std::string& r) const override { return r == "af'" ? "AF'" : RetroWriter::RegisterText(r); }

    /// A string of DB: printable runs of three characters or more quoted (one or two are a character constant, a value);
    /// the quote, control characters (blank runs in STORM's text), bytes above #7F (CP866 in STORM's text) as numbers;
    /// "@" before hex digit pairs too (STORM reads "@AEDF" as the bytes #AE,#DF)
    void PutString(const std::string& text, std::vector<std::string>& items, std::vector<std::string>&, const std::function<void()>&) override
    {
        std::string run;
        auto flush = [&]() {
            if (run.size() >= 3)
            {
                if (run[0] == '@' && run.size() % 2 == 1 && std::all_of(run.begin() + 1, run.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }))
                {
                    items.push_back(std::to_string('@'));
                    run.erase(0, 1);
                    if (run.size() < 3)
                    {
                        for (const char c : run)
                            items.push_back(std::to_string(static_cast<unsigned char>(c)));
                        run.clear();
                        return;
                    }
                }
                items.push_back("\"" + run + "\"");
            }
            else
                for (const char c : run)
                    items.push_back(std::to_string(static_cast<unsigned char>(c)));
            run.clear();
        };
        for (const char c : text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u >= 0x7F || c == '"')
            {
                flush();
                items.push_back(std::to_string(u));
            }
            else
            {
                if (run.size() + 2 >= DataWidth())
                    flush();   // a quoted run fits a line of its own
                run.push_back(c);
            }
        }
        flush();
    }

    std::optional<std::vector<std::string>> OwnInstruction(const Statement& s) override
    {
        const std::string& m = s.mnemonic;
        const auto& ops = s.operands;
        auto reg = [&](size_t k, const char* name) { return k < ops.size() && ops[k].kind == Operand::Kind::Register && ops[k].text == name; };
        if (m == "inf" || (m == "in" && (reg(0, "f") || (ops.size() == 1 && ops[0].kind == Operand::Kind::Indirect))))
            return std::vector<std::string>{"INF"};
        if (m == "out" && ops.size() == 2 && ops[0].kind == Operand::Kind::Indirect && ops[1].kind == Operand::Kind::Immediate)
            return std::vector<std::string>{Bytes({"#ED", "#71"}, "OUT (C),0")};
        if ((m == "sll" || m == "sli") && ops.size() == 1)
            return std::vector<std::string>{"SLI " + Operands(ops)};
        if (m == "ex" && reg(0, "af") && reg(1, "af'"))
            return std::vector<std::string>{"EXA"};
        return std::nullopt;
    }

    /// STORM repeats a line (".n", n up to 256): a DUP of one line of instructions or data without a label is that line
    RepeatForm Repeat(const std::vector<ir::Line>& lines, size_t begin, size_t end) const override
    {
        if (end != begin + 2 || end >= lines.size())
            return RepeatForm::Unrolled;
        const ir::Line& body = lines[begin + 1];
        if (!body.label.empty() || body.statements.empty())
            return RepeatForm::Unrolled;
        for (const Statement& s : body.statements)
            if (s.kind != Statement::Kind::Instruction && !IsDirective(s, ir::DirectiveKind::Db) && !IsDirective(s, ir::DirectiveKind::Dw) &&
                !IsDirective(s, ir::DirectiveKind::Ds))
                return RepeatForm::Unrolled;
        const auto count = Value(lines[begin].statements.front().args[0]);
        return count && *count >= 1 && *count <= 256 ? RepeatForm::Line : RepeatForm::Unrolled;
    }

    std::string RepeatedLabel(int64_t count) const override { return "." + std::to_string(count == 256 ? 0 : count); }

    /// A TR-DOS name for INCL / INCB: 8 characters, the type as the 9th ("NAME.B" -> "NAME    B")
    std::string FileName(const std::string& given)
    {
        const std::string name = DiskName(options, given);
        std::string base = name, type;
        if (const size_t dot = name.rfind('.'); dot != std::string::npos && name.size() - dot == 2)
        {
            base = name.substr(0, dot);
            type = name.substr(dot + 1);
        }
        if (base.size() > 8)
        {
            diagnostics.push_back({Severity::Warning, line, 0, "\"" + name + "\": a TR-DOS name has 8 characters"});
            base.resize(8);
        }
        if (!type.empty())
            base.append(8 - base.size(), ' ');
        return "\"" + base + type + "\"";
    }

    std::string dispRun{};                // the run address of the displacement in force (its value, or its text)
    std::optional<int64_t> dispPlace{};   // the address it is put at

    std::vector<std::string> DirectiveText(const Statement& s, const std::string&) override
    {
        switch (s.directive)
        {
            case ir::DirectiveKind::Org:
                // STORM's next ORG ends a displacement (ORG run,place)
                displaced = false;
                return {"ORG " + Known(s.args[0])};
            case ir::DirectiveKind::Disp:
            {
                if (displaced)
                    return {NotConverted("a DISP inside a DISP")};
                // ORG run,place: put where the address is so far
                dispPlace = here;
                const std::string run = Known(s.args[0]);
                strict = true;
                const auto value = Value(s.args[0]);
                strict = false;
                displaced = true;
                if (value || run.find_first_not_of("0123456789#ABCDEF") == std::string::npos)
                {
                    dispRun = value ? Number(*value) : run;
                    return {"ORG " + run + ",$"};
                }
                // a run address STORM computes ($ in it): under a name of its own for the end
                dispRun = Give("RUN");
                return {dispRun + "\tEQU " + run, "ORG " + dispRun + ",$"};
            }
            case ir::DirectiveKind::Ent:
                if (!displaced)
                    return {};
                displaced = false;
                // the place goes on where the displaced code ends
                if (!dispPlace)
                    return {NotConverted("the end of a DISP whose place is not known")};
                return {"ORG $-" + dispRun + "+" + Number(*dispPlace)};
            case ir::DirectiveKind::Ds:
            {
                // DS n[,pattern...]: n bytes of the pattern repeated and cut
                std::string text = "DS " + Known(s.args[0]);
                for (size_t k = 1; k < s.args.size(); ++k)
                    text += "," + Known(s.args[k]);
                for (const Operand& o : s.operands)
                    text += "," + Known(o.expr);
                return {text};
            }
            case ir::DirectiveKind::Include: return {"INCL " + FileName(s.text)};
            case ir::DirectiveKind::Incbin:
                if (s.args.size() > 1 || (!s.args.empty() && !(s.args[0].kind == Expr::Kind::Number && s.args[0].value == 0)))
                    return {NotConverted("INCBIN of a part of a file (INCB takes it all): " + s.text)};
                return {"INCB " + FileName(s.text)};
            case ir::DirectiveKind::End:
                diagnostics.push_back({Severity::Info, line, 0, "END dropped (STORM assembles the whole text)"});
                return {";END"};
            case ir::DirectiveKind::SaveBinary:
                diagnostics.push_back({Severity::Info, line, 0, "SAVEBIN dropped: save the bytes from STORM"});
                return {";SAVEBIN \"" + s.text + "\""};
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }

    /// Where a comment starts on a line (outside quotes), or npos
    static size_t CommentAt(const std::string& t)
    {
        bool quote = false;
        for (size_t k = 0; k < t.size(); ++k)
        {
            if (t[k] == '"')
                quote = !quote;
            else if (t[k] == ';' && !quote)
                return k;
        }
        return std::string::npos;
    }

    /// The names in a STORM expression; nothing for a local label
    std::optional<std::vector<std::string>> Names(const std::string& e) const
    {
        std::vector<std::string> names;
        for (size_t i = 0; i < e.size();)
        {
            const auto c = static_cast<unsigned char>(e[i]);
            if (std::isalpha(c) || c == '_')
            {
                size_t j = i;
                while (j < e.size() && (std::isalnum(static_cast<unsigned char>(e[j])) || e[j] == '_'))
                    ++j;
                names.push_back(Key(e.substr(i, j - i)));
                i = j;
            }
            else if (c == '#' || c == '%' || std::isdigit(c))
                for (++i; i < e.size() && std::isalnum(static_cast<unsigned char>(e[i]));)
                    ++i;
            else if (c == '.')
                return std::nullopt;
            else
                ++i;
        }
        return names;
    }

    struct Short
    {
        std::string equ, line, name, expr, value;   ///< no equ: an EQU of the value given before is taken
    };
    std::map<std::string, std::pair<std::string, size_t>> named{};   // an EQU's value: its name, the line it goes before

    /// An instruction line wider than STORM's 40 columns: its longest operand expression under a name of its own, an EQU
    /// of it (`LD H,(IY+(100~)+V+T+1)` -> `S1 EQU (100~)+V+T+1`, `LD H,(IY+S1)`); nothing when no operand gives a pair
    /// that fits
    std::optional<Short> Shortened(const std::string& t, size_t k)
    {
        if (t.empty() || t[0] == ';' || t[0] == '.' || t.find_first_of("\"'") != std::string::npos)
            return std::nullopt;
        const size_t labelEnd = t[0] == ' ' ? 0 : t.find(' ');
        const size_t command = t.find_first_not_of(' ', labelEnd);
        const size_t blank = command == std::string::npos ? std::string::npos : t.find(' ', command);
        if (blank == std::string::npos)
            return std::nullopt;
        const std::string head = t.substr(0, blank + 1), operands = t.substr(blank + 1);
        // the operands at the commas outside brackets
        std::vector<std::pair<size_t, size_t>> parts;
        int depth = 0;
        size_t from = 0;
        for (size_t i = 0; i <= operands.size(); ++i)
        {
            if (i == operands.size() || (operands[i] == ',' && depth == 0))
            {
                parts.push_back({from, i});
                from = i + 1;
            }
            else if (operands[i] == '(')
                ++depth;
            else if (operands[i] == ')')
                --depth;
        }
        // the expression of each: inside a bracket that holds the whole operand, after IX / IY
        size_t best = 0, bestAt = 0;
        std::string bestExpr;
        for (const auto& [b, e] : parts)
        {
            size_t at = b, end = e;
            if (operands[at] == '(' && operands[end - 1] == ')')
            {
                int d = 0;
                size_t close = at;
                for (; close < end; ++close)
                    if (operands[close] == '(')
                        ++d;
                    else if (operands[close] == ')' && --d == 0)
                        break;
                if (close == end - 1)
                {
                    ++at;
                    --end;
                    const std::string reg = z80::Upper(operands.substr(at, 2));
                    if ((reg == "IX" || reg == "IY") && end - at > 2 && (operands[at + 2] == '+' || operands[at + 2] == '-'))
                        at += operands[at + 2] == '+' ? 3 : 2;   // (IX-a+b): the offset 0-a+b under the name
                }
            }
            if (end - at > best)
            {
                best = end - at;
                bestAt = at;
                bestExpr = operands.substr(at, end - at);
            }
        }
        if (best < 8)
            return std::nullopt;
        const bool sign = bestExpr[0] == '-';
        const std::string value = std::string(sign ? "0" : "") + bestExpr;
        // the same value again: its name, where the EQU stands above or the line is an instruction (a directive's
        // value must be known on STORM's first pass); not with $, the address of the line it was written for
        const std::string word = z80::Upper(t.substr(command, blank - command));
        const auto given = named.find(value);
        const bool reuse = given != named.end() && value.find('$') == std::string::npos &&
                           (given->second.second <= k || (word != "EQU" && word != "ORG" && word != "DS"));
        const std::string name = reuse ? given->second.first : Give("S");
        std::string equ;
        if (!reuse)
        {
            equ = name;
            equ.append(equ.size() < 8 ? 8 - equ.size() : 1, ' ');
            equ += "EQU " + value;
        }
        std::string rest = operands.substr(0, bestAt) + (sign ? "+" : "") + name + operands.substr(bestAt + best);
        const std::string shorter = head + rest;
        if (Width(equ) > kWidth || Width(shorter) > kWidth)
        {
            if (!reuse)
                taken.erase(Key(name));
            return std::nullopt;
        }
        return Short{equ, shorter, name, bestExpr, value};
    }

    std::string CommentAfter() const override { return ";"; }
    /// STORM 1.3's first pass counts DB "A" as two bytes (the labels after it come out too high; its help lists DB "A"
    /// among the bugs): a character of DB is written as its number
    bool CharactersInData() const override { return false; }
    size_t DataWidth() const override { return kWidth - 8 - 3; }

    /// One line as STORM shows it: the label, the command from column 8, one blank before the operands, a comment right
    /// after them (STORM's own layout: "ADD HL,DE; (ETEXT)"); a repeated line (".n") holds all its statements; a
    /// comment that does not fit goes on a line of its own, cut at 40 characters
    std::vector<std::string> Layout(const std::string& label, const std::vector<std::string>& texts, bool bare, bool hasComment,
                                    const std::string& comment) override
    {
        auto command = [](const std::string& head, const std::string& t) {
            std::string text = head;
            text.append(text.size() < 8 ? 8 - text.size() : 1, ' ');
            return text + t;
        };
        std::vector<std::string> out;
        bool first = true;
        if (!label.empty() && label[0] == '.')
        {
            std::string joined;
            for (const std::string& t : texts)
                joined += (joined.empty() ? "" : ":") + t;
            out.push_back(command(label, joined));
            first = false;
        }
        else
            for (const std::string& t : texts)
            {
                const size_t tab = t.find('\t');
                if (!t.empty() && t[0] == ';')
                {
                    if (first && !label.empty())
                        out.push_back(label);
                    out.push_back(t);
                }
                else if (tab != std::string::npos && t.find(' ') > tab)
                {
                    // "NAME\tEQU ...": a line with a name of the writer's own
                    if (first && !label.empty())
                        out.push_back(label);
                    out.push_back(command(t.substr(0, tab), t.substr(tab + 1)));
                }
                else
                    out.push_back(command(first ? label : std::string(), t));
                first = false;
            }
        if (first && (!label.empty() || hasComment || bare))
            out.push_back(label);
        if (out.empty())
            return out;
        if (hasComment)
        {
            const std::string note = ";" + CommentText(comment);
            std::string& last = out.back();
            if (last.empty())
                last = note;
            else if (last[0] != ';' && Width(last) + Width(note) <= kWidth)
                last += note;
            else
                out.push_back(note);
        }
        // a comment after a statement that makes the line too long goes on a line of its own
        for (size_t k = 0; k < out.size(); ++k)
        {
            const size_t semicolon = CommentAt(out[k]);
            if (Width(out[k]) > kWidth && semicolon != std::string::npos && semicolon > 0)
            {
                const std::string note = out[k].substr(semicolon);
                out[k].resize(semicolon);
                out.insert(out.begin() + static_cast<std::ptrdiff_t>(k) + 1, note);
            }
        }
        for (std::string& t : out)
        {
            if (Width(t) > kWidth && !t.empty() && t[0] == ';')
            {
                // a comment cut at STORM's line
                size_t chars = 0, at = 0;
                while (at < t.size() && chars < kWidth)
                {
                    ++chars;
                    ++at;
                    while (at < t.size() && (static_cast<unsigned char>(t[at]) & 0xC0) == 0x80)
                        ++at;
                }
                t.resize(at);
            }
            std::vector<uint8_t> body;
            std::string error;
            if (!t.empty() && !codecs::StormCodec::EncodeLine(t, body, error))
                diagnostics.push_back({Severity::Warning, line, 0, "STORM cannot hold the line (" + error + "): " + t});
        }
        return out;
    }

public:
    /// The text written: a line wider than STORM's 40 columns gets an EQU of its longest expression. STORM's EQU takes
    /// no label defined further down ("Label not defined", STORM 1.3) and an instruction takes a later EQU, so the EQU
    /// goes before the line, or after the line of the last name it holds (then without $: the instruction's address
    /// is no longer the EQU's). A name of another file, a local label: the line stays as it is, reported
    void Finish()
    {
        std::vector<SourceLine>& lines = result.document.lines;
        const auto labelOf = [](const std::string& t) { return t.empty() || t[0] == ' ' || t[0] == ';' ? std::string() : t.substr(0, t.find(' ')); };
        std::map<std::string, size_t> where;
        for (size_t k = 0; k < lines.size(); ++k)
            if (const std::string label = labelOf(lines[k].text); !label.empty())
                where.emplace(Key(label), k);
        std::map<size_t, std::vector<SourceLine>> before;   // EQU lines to put before a line (lines.size(): at the end)
        for (size_t k = 0; k < lines.size(); ++k)
        {
            if (Width(lines[k].text) <= kWidth)
                continue;
            std::optional<size_t> at;
            const auto shorter = Shortened(lines[k].text, k);
            if (shorter && shorter->equ.empty())
            {
                lines[k].text = shorter->line;
                continue;
            }
            if (const auto names = shorter ? Names(shorter->expr) : std::nullopt)
            {
                size_t last = 0;
                bool known = true;
                for (const std::string& n : *names)
                {
                    const auto w = where.find(n);
                    known = known && w != where.end();
                    if (w != where.end())
                        last = std::max(last, w->second);
                }
                if (known && (names->empty() || last < k))
                    at = k;
                else if (known && shorter->expr.find('$') == std::string::npos)
                    at = last + 1;
            }
            if (!at)
            {
                if (shorter)
                    taken.erase(Key(shorter->name));
                diagnostics.push_back({Severity::Warning, lines[k].origin, 0,
                                       "a line of " + std::to_string(Width(lines[k].text)) + " characters (STORM edits 40): " + lines[k].text});
                continue;
            }
            SourceLine equ = lines[k];
            equ.text = shorter->equ;
            before[*at].push_back(equ);
            named.emplace(shorter->value, std::make_pair(shorter->name, *at));
            lines[k].text = shorter->line;
        }
        for (auto b = before.rbegin(); b != before.rend(); ++b)
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(b->first), b->second.begin(), b->second.end());
    }
};
}  // namespace

BackendResult StormBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    BackendResult result;
    result.document.dialect = "storm";
    StormWriter w{program, options, result};
    w.Run();
    w.Finish();
    return result;
}
}  // namespace unrealasm::dialects
