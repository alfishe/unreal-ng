#include "dialects/xas/xasbackend.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
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

// The words XAS reads as tokens (research-xas.md §4) or commands: a label must not be one of them
const std::set<std::string> kWords = {
    "ORG", "ENT", "EQU", "WORK", "DB", "DW", "DM", "DS", "DEFB", "DEFW", "DEFM", "DEFS", "!ASSM", "!CONT", ".ASM", ".END",
    "LTEXT", "LCODE", "LOADTEXT", "LOADCODE", "LTXT", "LCOD", "USEL", "IFNZ", "IFZ", "MAKE", "SLI", "BC", "DE", "HL", "IX",
    "IY", "SP", "AF", "B", "C", "D", "E", "H", "L", "A", "I", "R", "HX", "LX", "HY", "LY", "NZ", "Z", "NC", "PO", "PE",
    "P", "M",
};

/// Characters XAS's font draws as others (^ ↑, ` £, { | } ~ as Russian letters, research-xas.md §6)
bool NotInFont(unsigned char c)
{
    return c == '^' || c == '`' || (c >= '{' && c <= '~');
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

class XasWriter : public RetroWriter
{
public:
    XasWriter(const ir::Program& program, const BackendOptions& options, BackendResult& result, int v)
        : RetroWriter(program, options, result), version(v)
    {
    }

private:
    int version = 999;

    // ---- version ----

    bool Old() const { return version < 700; }           // 4.x / 5.x: DEFB DEFW DEFM DEFS, LOADTEXT, no strings in DEFB
    bool DotWords() const { return version >= 907 && version < 910; }   // 9.07m: .ASM .END LTXT LCOD
    bool HasIfz() const { return version >= 743 && version < 900; }      // IFNZ / IFZ: 7.43 and 7.447 only
    std::string Dm() const { return Old() ? "DEFM" : "DM"; }
    std::string Ds() const { return Old() ? "DEFS" : "DS"; }
    std::string Assm() const { return DotWords() ? ".ASM" : "!ASSM"; }
    std::string Cont() const { return DotWords() ? ".END" : "!CONT"; }
    std::string Ltext() const { return Old() ? "LOADTEXT" : DotWords() ? "LTXT" : "LTEXT"; }
    std::string Lcode() const { return Old() ? "LOADCODE" : DotWords() ? "LCOD" : "LCODE"; }

    // ---- the dialect ----

    std::string Title() const override { return "XAS"; }
    /// XAS compares labels on their first 7 characters without case (research-xas-to-sjasmplus.md §2)
    size_t Significant() const override { return 7; }
    bool Reserved(const std::string& upper) const override
    {
        // and the frontend's own name for WORK's offset (__xas_work: its first 7 characters)
        return kWords.count(upper) || z80::IsMnemonic(z80::Lower(upper)) || upper.rfind("__XAS_W", 0) == 0;
    }
    /// shown in lower case, as XAS's editor shows its text
    std::string Shown(const std::string& name) const override { return z80::Lower(name); }
    /// letters, digits, "_", "?", "@" (a letter first), in upper case
    std::string Sanitized(const std::string& name) const override
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
    std::string Number(int64_t value) const override { return z80::Lower(RetroWriter::Number(value)); }
    std::string HexByte(int value) const override { return z80::Lower(RetroWriter::HexByte(value)); }
    std::string Db() const override { return Old() ? "DEFB" : "DB"; }
    std::string Dw() const override { return Old() ? "DEFW" : "DW"; }

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
                return v < 256 ? std::to_string(v) : "#" + z80::Lower(z80::HexDigits(static_cast<uint64_t>(v), 4));
            default: return std::to_string(v);
        }
    }

    /// An operand of a chain: a number, a name or $ (a group of one), else its value when known
    std::optional<std::string> Atom(const Expr& e)
    {
        const Expr& u = Unwrapped(e);
        if (u.kind == Expr::Kind::Number)
            return NumberText(u);
        if (u.kind == Expr::Kind::Symbol || u.kind == Expr::Kind::Current || u.kind == Expr::Kind::Raw)
            return SymbolText(u);
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
    std::optional<std::string> Text(const Expr& e) override
    {
        switch (e.kind)
        {
            case Expr::Kind::Number: case Expr::Kind::Symbol: case Expr::Kind::Current: case Expr::Kind::Raw: return Atom(e);
            case Expr::Kind::Group: return Text(e.args[0]);
            case Expr::Kind::Unary:
            {
                const auto inner = Text(e.args[0]);
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
            const auto l = Text(left);
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
                    return Text(Expr::Binary(second, Expr::Binary(first, a, b.args[0]), b.args[1]));
                }
                if (b.kind == Expr::Kind::Binary && b.op == e.op && (e.op == Op::Mul || e.op == Op::Xor))
                    return Text(Expr::Binary(e.op, Expr::Binary(e.op, a, b.args[0]), b.args[1]));
                // + * ! commute: the atom goes to the right
                if (e.op != Op::Sub && IsAtom(a))
                    return join(symbol, b, a);
                if (e.op == Op::Sub && IsAtom(a))
                {
                    // a-b = (b*#FFFF)+a
                    const auto r = Text(b);
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
                const auto l = Text(a);
                return l ? std::optional<std::string>(*l + (e.op == Op::Shl ? "*" : "/") + Number(int64_t{1} << *shift)) : std::nullopt;
            }
            case Op::CyclicLeft16: case Op::CyclicRight16:
            {
                // 'L / 'R rotate by one bit (7.x)
                const auto count = Value(b);
                if (version < 700 || version >= 900 || !count)
                    return std::nullopt;
                auto l = Text(a);
                if (!l)
                    return std::nullopt;
                for (int64_t k = 0; k < (*count & 15); ++k)
                    *l += e.op == Op::CyclicLeft16 ? "'L" : "'R";
                return l;
            }
            default: return std::nullopt;
        }
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

    void PutString(const std::string& text, std::vector<std::string>& items, std::vector<std::string>& out, const std::function<void()>& flush) override
    {
        for (const std::string& item : StringItems(text))
            if (Old() && item[0] == '"')
            {
                // 4.x / 5.x: DEFB takes no strings; DEFM does
                flush();
                out.push_back(Dm() + " " + item);
            }
            else
                items.push_back(item);
    }

    std::optional<std::vector<std::string>> OwnInstruction(const Statement& s) override
    {
        const std::string& m = s.mnemonic;
        const auto& ops = s.operands;
        auto reg = [&](size_t k, const char* name) { return k < ops.size() && ops[k].kind == Operand::Kind::Register && ops[k].text == name; };
        // IN F,(C) / IN (C) and OUT (C),0 (no XAS form): their bytes
        if (m == "inf" || (m == "in" && (reg(0, "f") || (ops.size() == 1 && ops[0].kind == Operand::Kind::Indirect))))
            return std::vector<std::string>{Db() + " #ed,#70   ;IN F,(C)"};
        if (m == "out" && ops.size() == 2 && ops[0].kind == Operand::Kind::Indirect && ops[1].kind == Operand::Kind::Immediate)
            return std::vector<std::string>{Db() + " #ed,#71   ;OUT (C),0"};
        if ((m == "sll" || m == "sli") && ops.size() == 1)
            return std::vector<std::string>{"SLI " + Operands(ops)};
        // XAS 4.18 and 5.05 assemble LD IY,(nn) as LD IX,(nn), and 4.18 LD LY,n as LD HY,n (seen in unreal-ng): bytes
        if (Old() && m == "ld" && reg(0, "iy") && ops.size() == 2 && ops[1].kind == Operand::Kind::Memory)
            return std::vector<std::string>{Db() + " #fd,#2a   ;LD IY,(" + Expression(ops[1].expr) + ")", Dw() + " " + Expression(ops[1].expr)};
        if (version < 500 && m == "ld" && reg(0, "iyl") && ops.size() == 2 && ops[1].kind == Operand::Kind::Immediate)
            return std::vector<std::string>{Db() + " #fd,#2e," + Expression(ops[1].expr) + "   ;LD LY," + Expression(ops[1].expr)};
        return std::nullopt;
    }

    // ---- conditions left to XAS: IFNZ / IFZ ... !CONT (7.43 / 7.447) ----

    std::optional<std::vector<std::string>> OpenNative(const Expr& condition, Condition& c) override
    {
        if (!HasIfz())
            return std::nullopt;
        // x==0 / x!=0 and a plain value: IFZ x / IFNZ x; other comparisons as their difference
        const Expr& e = Unwrapped(condition);
        if (e.kind == Expr::Kind::Binary && (e.op == Op::Equal || e.op == Op::NotEqual))
        {
            c.whenZero = e.op == Op::Equal;
            c.test = Expression(Expr::Binary(Op::Sub, e.args[0], e.args[1]));
        }
        else
            c.test = Expression(e);
        return std::vector<std::string>{std::string(c.whenZero ? "IFZ " : "IFNZ ") + c.test};
    }

    std::vector<std::string> ElseNative(Condition& c) override
    {
        c.whenZero = !c.whenZero;
        return {Cont(), std::string(c.whenZero ? "IFZ " : "IFNZ ") + c.test};
    }

    std::vector<std::string> CloseNative(const Condition&) override { return {Cont()}; }

    /// A DUP XAS's !ASSM cannot hold: a body that assigns names, defines labels or opens blocks of its own (or inside
    /// another XAS block: the walk unrolls that); and every DUP in 4.18, whose !ASSM n assembles the block once (seen in
    /// unreal-ng)
    RepeatForm Repeat(const std::vector<ir::Line>& lines, size_t begin, size_t end) const override
    {
        if (version < 500)
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

    std::vector<std::string> DirectiveText(const Statement& s, const std::string&) override
    {
        switch (s.directive)
        {
            case ir::DirectiveKind::Org:
                if (displaced)
                {
                    displaced = false;
                    return {"WORK", "ORG " + Known(s.args[0])};
                }
                return {"ORG " + Known(s.args[0])};
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
                std::string name = DiskName(options, s.text);
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
                return {Lcode() + " \"" + DiskName(options, s.text) + "\""};
            case ir::DirectiveKind::Repeat:
                if (nativeBlock)
                    return {NotConverted("a repeat inside an XAS block (XAS keeps one)")};
                nativeBlock = true;
                repeats.push_back(true);
                return {Assm() + " " + Known(s.args[0])};
            case ir::DirectiveKind::EndRepeat:
                if (repeats.empty())
                    return {NotConverted("EDUP without DUP")};
                repeats.pop_back();
                nativeBlock = false;
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
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }

    /// Laid out as XAS shows it: the label in column 0, the command from column 8, the operands from column 14
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
                // a text written as a comment (a directive XAS has not): in characters XAS's font has
                const std::string asComment = ";" + CommentText(t.substr(1));
                out.push_back(text.empty() ? asComment : text + " " + asComment);
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

BackendResult XasBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    BackendResult result;
    result.document.dialect = "xas";
    XasWriter w{program, options, result, VersionNumber(options.targetVersion)};
    w.Run();
    return result;
}
}  // namespace unrealasm::dialects
