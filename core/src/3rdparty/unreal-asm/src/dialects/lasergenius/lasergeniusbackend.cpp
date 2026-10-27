#include "dialects/lasergenius/lasergeniusbackend.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
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

// Laser Genius' pseudo-ops, registers and conditions beside the mnemonics (its token table, research-laser-genius.md §3)
const std::set<std::string> kWords = {
    "ORG", "PUT", "EQU", "DB", "DEFB", "DEFM", "DW", "DEFW", "DL", "DEFL", "DS", "DEFS", "COND", "ELSE", "ENDC", "MACRO",
    "ENDM", "A", "B", "C", "D", "E", "H", "L", "I", "R", "F", "AF", "BC", "DE", "HL", "SP", "IX", "IY", "NZ", "Z", "NC", "PO",
    "PE", "P", "M", "ON", "OFF",
};

/// A comment for Laser Genius: ASCII only
std::string CommentText(const std::string& utf8)
{
    std::string out;
    for (size_t p = 0; p < utf8.size();)
    {
        const unsigned char b = static_cast<unsigned char>(utf8[p]);
        const size_t length = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
        if (b == '^')
            out += "\u2191";   // ^ and ` are the Spectrum's arrow and pound: their bytes
        else if (b == '`')
            out += "\u00A3";
        else
            out.push_back(b < 0x20 || b == 0x7F ? ' ' : b < 0x80 ? static_cast<char>(b) : '?');
        p += b < 0x80 ? 1 : length;
    }
    return out;
}

class LaserGeniusWriter : public RetroWriter
{
public:
    LaserGeniusWriter(const ir::Program& program, const BackendOptions& options, BackendResult& result) : RetroWriter(program, options, result) {}

private:
    std::string Title() const override { return "Laser Genius"; }
    size_t Significant() const override { return 16; }
    std::string Key(const std::string& name) const override { return name.substr(0, Significant()); }
    bool Reserved(const std::string& upper) const override { return kWords.count(upper) || z80::IsMnemonic(z80::Lower(upper)); }
    std::string Db() const override { return "DB"; }
    std::string Dw() const override { return "DW"; }

    std::string NumberText(const Expr& e) const
    {
        const int64_t v = e.value & 0xFFFF;
        switch (e.spelling)
        {
            // as its editor shows them: a byte in 2 hex digits, a word in 4
            case ir::NumberSpelling::Hex: return "#" + z80::HexDigits(static_cast<uint64_t>(v), v > 0xFF || e.digits > 2 ? 4 : 2);
            case ir::NumberSpelling::Binary: return "#" + z80::HexDigits(static_cast<uint64_t>(v), v > 0xFF ? 4 : 2);
            case ir::NumberSpelling::Character:
                if (e.text.size() == 1 && e.text[0] > 0x20 && e.text[0] < 0x7F && e.text[0] != '"' && e.text[0] != '\\' && e.text[0] != '^' && e.text[0] != '`')
                    return "\"" + e.text + "\"";
                return Number(v);
            default: return std::to_string(v);
        }
    }

    // An expression's text and how it binds: an atom 9, a prefix operator 8, a binary operator its level
    struct Printed
    {
        std::string text;
        int priority = 9;
    };

    static Printed Grouped(const Printed& p) { return {"[" + p.text + "]", 9}; }
    /// a prefix operator; "--" and "++" are Phoenix tokens (decrement, increment): a second sign goes in [ ]
    static Printed Prefix(const char* symbol, const Printed& x)
    {
        const bool doubled = !x.text.empty() && (x.text[0] == '-' || x.text[0] == '+') && (symbol[0] == '-' || symbol[0] == '+');
        return {symbol + (x.priority < 8 || doubled ? Grouped(x) : x).text, 8};
    }
    /// a binary operator; a right operand with a sign after - or + goes in [ ] the same way (3-[-5], not 3--5)
    static Printed Infix(const Printed& l, const char* symbol, int p, const Printed& r)
    {
        const char last = symbol[std::strlen(symbol) - 1];
        const bool doubled = !r.text.empty() && (r.text[0] == '-' || r.text[0] == '+') && (last == '-' || last == '+');
        return {(l.priority < p ? Grouped(l) : l).text + symbol + (r.priority <= p || doubled ? Grouped(r) : r).text, p};
    }
    /// 1 for true as the source's (sjasmplus -1: negated), unless the operation is written as Laser Genius' own truth
    Printed Truth(const Printed& lg)
    {
        if (rawTruth || True() == 1)
        {
            rawTruth = false;
            return lg;
        }
        return Prefix("-", lg);
    }
    bool rawTruth = false;

    static bool IsTruth(const Expr& e)
    {
        const Expr& u = Unwrapped(e);
        return (u.kind == Expr::Kind::Unary && u.op == Op::LogicalNot) ||
               (u.kind == Expr::Kind::Binary && u.op >= Op::Equal && u.op <= Op::LogicalOr);
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
            case Expr::Kind::CurrentPhysical: return Printed{"."};   // where the bytes go
            case Expr::Kind::Unary:
            {
                if (e.op == Op::Negate && True() == -1 && IsTruth(e.args[0]))
                {
                    // -(a<b): Laser Genius' 1 for true is the source's -1 negated
                    rawTruth = true;
                    const auto t = Print(e.args[0]);
                    rawTruth = false;
                    return t;
                }
                const bool raw = rawTruth;
                rawTruth = false;
                const auto x = Print(e.args[0]);
                rawTruth = raw && e.op == Op::LogicalNot;
                if (!x)
                    return std::nullopt;
                switch (e.op)
                {
                    case Op::Plus: return x;
                    case Op::Negate: return Prefix("-", *x);
                    case Op::Not: return Prefix("^", *x);
                    case Op::LogicalNot: return Truth(Prefix("!", *x));
                    case Op::High: return Infix(*x, ">>", 5, Printed{"8"});
                    case Op::Low: return Infix(*x, "&", 2, Printed{"255"});
                    default: break;
                }
                break;
            }
            case Expr::Kind::Binary:
            {
                struct Level
                {
                    Op op;
                    const char* symbol;
                    int priority;
                    bool truth;
                };
                static const Level kLevels[] = {
                    {Op::LogicalAnd, "&&", 1, true}, {Op::LogicalOr, "||", 1, true}, {Op::And, "&", 2, false}, {Op::Or, "|", 2, false},
                    {Op::Xor, "^", 2, false}, {Op::Equal, "?=", 3, true}, {Op::NotEqual, "!=", 3, true}, {Op::Less, "<", 4, true},
                    {Op::Greater, ">", 4, true}, {Op::LessEqual, "<=", 4, true}, {Op::GreaterEqual, ">=", 4, true},
                    {Op::Shl, "<<", 5, false}, {Op::Shr, ">>", 5, false}, {Op::ShrUnsigned, ">>", 5, false},
                    {Op::CyclicLeft16, "@<", 5, false}, {Op::CyclicRight16, "@>", 5, false}, {Op::Add, "+", 6, false},
                    {Op::Sub, "-", 6, false}, {Op::Mul, "*", 7, false}, {Op::Div, "/", 7, false}, {Op::Mod, "%", 7, false},
                };
                const Expr& a = e.args[0];
                const Expr& b = e.args[1];
                for (const Level& level : kLevels)
                {
                    if (level.op != e.op)
                        continue;
                    // 16-bit unsigned words; its division by #8000 and more is wrong (research-laser-genius.md §4)
                    if (e.op == Op::Div || e.op == Op::Mod)
                    {
                        const auto d = Value(b);
                        if (!InWord(a) || !d || *d <= 0 || *d >= 0x8000)
                            Problem("a division Laser Genius makes otherwise");
                    }
                    // its + - * stop the assembly with "** overflow **" outside 0..#FFFF (2-3, #FFFF+1, 256*256; measured)
                    const auto outside = [this](const Expr& x) { const auto v = Value(x); return v && (*v < 0 || *v > 0xFFFF); };
                    if ((e.op == Op::Add || e.op == Op::Sub || e.op == Op::Mul) && (outside(a) || outside(b) || outside(e)))
                        Problem("an operation that overflows Laser Genius' 16-bit unsigned words");
                    // its shift by 0 gives 0 (4<<0, 4>>0; by 1..17 right, measured)
                    if (e.op == Op::Shl || e.op == Op::Shr || e.op == Op::ShrUnsigned)
                        if (const auto count = Value(b); count && *count == 0)
                            Problem("a shift by 0, which Laser Genius makes 0");
                    if ((level.truth && level.priority >= 3) || e.op == Op::Shr || e.op == Op::ShrUnsigned)
                        if (!InWord(a) || !InWord(b))
                            Problem("an operation Laser Genius makes in 16-bit unsigned words on a value outside 0..#FFFF");
                    const bool raw = rawTruth;
                    rawTruth = false;
                    const auto l = Print(a), r = Print(b);
                    rawTruth = raw;
                    if (!l || !r)
                        return std::nullopt;
                    const Printed p = Infix(*l, level.symbol, level.priority, *r);
                    if (level.truth)
                        return Truth(p);
                    rawTruth = false;
                    return p;
                }
                break;
            }
            default: break;
        }
        if (const auto v = Value(e))
            return Printed{Number(*v)};
        Problem("an operation Laser Genius has no operator for");
        return std::nullopt;
    }

    std::optional<std::string> Text(const Expr& e) override
    {
        const auto p = Print(e);
        if (!p)
            return std::nullopt;
        return p->text;
    }

    std::string RegisterText(const std::string& r) const override { return r == "af'" ? "AF'" : RetroWriter::RegisterText(r); }

    /// A string of DB: printable runs quoted; the quote, "\", control characters and bytes above #7F as numbers
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
            if (u < 0x20 || u >= 0x7F || c == '"' || c == '\\' || c == '^' || c == '`')   // ^ `: no ASCII byte in the Spectrum's set
            {
                end();
                items.push_back(std::to_string(u));
            }
            else
                run.push_back(c);
        }
        end();
    }

    /// (IX+-d) for a displacement below 0: Laser Genius 1.04 builds its (IX- / (IY- operand tokens with a plus
    /// (RLC (IY-4) takes +4)
    std::string OperandText(const Operand& o) override
    {
        if (o.kind == Operand::Kind::Indexed)
            if (const auto d = Value(o.expr); d && *d < 0 && *d >= -128)
                return "(" + z80::Upper(o.text) + "+-" + std::to_string(-*d) + ")";
        return RetroWriter::OperandText(o);
    }

    std::optional<std::vector<std::string>> OwnInstruction(const Statement& s) override
    {
        const std::string& m = s.mnemonic;
        const auto& ops = s.operands;
        auto reg = [&](size_t k, const char* name) { return k < ops.size() && ops[k].kind == Operand::Kind::Register && ops[k].text == name; };
        if (m == "inf" || (m == "in" && (reg(0, "f") || (ops.size() == 1 && ops[0].kind == Operand::Kind::Indirect))))
            return std::vector<std::string>{Bytes({"#ED", "#70"}, "IN F,(C)")};
        if (m == "out" && ops.size() == 2 && ops[0].kind == Operand::Kind::Indirect && ops[1].kind == Operand::Kind::Immediate)
            return std::vector<std::string>{Bytes({"#ED", "#71"}, "OUT (C),0")};
        if ((m == "sll" || m == "sli") && ops.size() == 1)
        {
            static const std::map<std::string, int> kRegisters = {{"b", 0}, {"c", 1}, {"d", 2}, {"e", 3}, {"h", 4}, {"l", 5}, {"a", 7}};
            const Operand& o = ops[0];
            if (o.kind == Operand::Kind::Register && kRegisters.count(o.text))
                return std::vector<std::string>{Bytes({"#CB", Number(0x30 | kRegisters.at(o.text))}, "SLL " + Operands(ops))};
            if (o.kind == Operand::Kind::Indirect && o.text == "hl")
                return std::vector<std::string>{Bytes({"#CB", "#36"}, "SLL (HL)")};
            if (o.kind == Operand::Kind::Indexed)
            {
                const auto d = Value(o.expr);
                return std::vector<std::string>{Bytes({o.text == "ix" ? "#DD" : "#FD", "#CB", d ? Number(*d & 0xFF) : Expression(o.expr), "#36"}, "SLL " + Operands(ops))};
            }
        }
        return HalfRegisterBytes(s);
    }

    // ---- COND (true when not 0) / ELSE / ENDC ----

    std::optional<std::vector<std::string>> OpenNative(const Expr& condition, Condition& c) override
    {
        c.test = Expression(condition);
        return std::vector<std::string>{"COND " + c.test};
    }
    std::vector<std::string> ElseNative(Condition&) override { return {"ELSE"}; }
    std::vector<std::string> CloseNative(const Condition&) override { return {"ENDC"}; }

    RepeatForm Repeat(const std::vector<ir::Line>&, size_t, size_t) const override { return RepeatForm::Unrolled; }

    std::string dispBack{};   // the ORG that ends the displacement in force

    std::vector<std::string> DirectiveText(const Statement& s, const std::string&) override
    {
        switch (s.directive)
        {
            case ir::DirectiveKind::Org:
            {
                // ORG sets the address, PUT where the bytes go (without a PUT nothing is stored)
                displaced = false;
                const std::string at = Known(s.args[0]);
                return {"ORG " + at, "PUT " + at};
            }
            case ir::DirectiveKind::Disp:
            {
                // ORG sets the address only: the bytes go on where PUT has them
                strict = true;
                const auto run = Value(s.args[0]);
                strict = false;
                displaced = true;
                if (run && here)
                {
                    dispBack = "ORG $-" + Number(*run) + "+" + Number(*here);
                    return {"ORG " + Number(*run)};
                }
                // known only when Laser Genius assembles: the place and the run address under names of their own
                const std::string place = Give("ZPLACE"), at = Give("ZRUN");
                dispBack = "ORG $-" + at + "+" + place;
                return {place + "\tEQU $", at + "\tEQU " + Known(s.args[0]), "ORG " + at};
            }
            case ir::DirectiveKind::Ent:
                if (s.text == "if-displaced" && !displaced)
                    return {};
                displaced = false;
                // the address back where the bytes are: the place plus what the displaced code took
                return {dispBack};
            case ir::DirectiveKind::Ds:
            {
                const auto fill = s.args.size() > 1 ? Value(s.args[1]) : (s.operands.empty() ? std::optional<int64_t>(0) : std::nullopt);
                if (fill && *fill == 0 && s.operands.empty())
                    return {"DS " + Known(s.args[0])};
                const auto n = Value(s.args[0]);
                std::vector<int64_t> pattern;
                if (s.operands.empty() && fill)
                    pattern.push_back(*fill & 0xFF);
                for (const Operand& o : s.operands)
                    if (const auto v = Value(o.expr))
                        pattern.push_back(*v & 0xFF);
                if (!n || pattern.empty() || pattern.size() != std::max<size_t>(1, s.operands.size()))
                    return {NotConverted("DS with a fill or count known only when assembling")};
                std::vector<std::string> items;
                for (int64_t k = 0; k < *n; ++k)
                    items.push_back(Number(pattern[static_cast<size_t>(k) % pattern.size()]));
                return DataLines(Db(), items);
            }
            case ir::DirectiveKind::Include: return {"*INCLUDE \"" + DiskName(options, s.text) + "\""};
            case ir::DirectiveKind::Incbin: return IncbinAsData(s, "INCBIN (Laser Genius has none): " + s.text);
            case ir::DirectiveKind::End:
                diagnostics.push_back({Severity::Info, line, 0, "END dropped (Laser Genius assembles the whole text)"});
                return {";END"};
            case ir::DirectiveKind::SaveBinary:
                diagnostics.push_back({Severity::Info, line, 0, "SAVEBIN dropped: save the bytes from Laser Genius"});
                return {";SAVEBIN \"" + s.text + "\""};
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }

    /// "label: instruction", one statement a line
    std::vector<std::string> Layout(const std::string& label, const std::vector<std::string>& texts, bool bare, bool hasComment,
                                    const std::string& comment) override
    {
        std::vector<std::string> out;
        bool first = true;
        for (const std::string& t : texts)
        {
            const size_t tab = t.find('\t');
            if (tab != std::string::npos && t.find(' ') > tab)
            {
                // "NAME\tEQU ...": a line with a name of the writer's own
                if (first && !label.empty())
                    out.push_back(label + ":");
                out.push_back(t.substr(0, tab) + ": " + t.substr(tab + 1));
                first = false;
                continue;
            }
            const std::string head = first && !label.empty() ? label + ": " : std::string("        ");
            first = false;
            if (!t.empty() && t[0] == ';')
            {
                if (head.find(':') != std::string::npos)
                    out.push_back(label + ":");
                out.push_back(t);
                continue;
            }
            out.push_back(head + t);
        }
        if (first && !label.empty())
            out.push_back(label + ":");
        else if (first && (hasComment || bare))
            out.push_back(std::string());
        if (out.empty())
            return out;
        if (hasComment)
        {
            if (out.back().empty())
                out.back() = ";" + CommentText(comment);
            else
                out.back() += " ;" + CommentText(comment);
        }
        return out;
    }
};
}  // namespace

BackendResult LaserGeniusBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    BackendResult result;
    result.document.dialect = "lasergenius";
    LaserGeniusWriter w{program, options, result};
    w.Run();
    return result;
}
}  // namespace unrealasm::dialects
