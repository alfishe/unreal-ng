#include "dialects/gens/gensbackend.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>

#include "dialects/common/diskname.h"
#include "dialects/common/chainwriter.h"
#include "dialects/common/z80.h"

namespace unrealasm::dialects
{
namespace
{
using ir::Expr;
using ir::Op;
using ir::Operand;
using ir::Statement;

// GENS' directives, registers and conditions beside the mnemonics (Devpac manual, appendix 2)
const std::set<std::string> kWords = {
    "ORG", "EQU", "DEFB", "DEFW", "DEFM", "DEFS", "ENT", "IF", "ELSE", "END", "MAC", "ENDM", "A", "B", "C", "D", "E", "H", "L",
    "I", "R", "AF", "BC", "DE", "HL", "SP", "IX", "IY", "NZ", "Z", "NC", "PO", "PE", "P", "M",
};

/// A comment for GENS: ASCII only (other characters as "?"); ^ and ` are the Spectrum's arrow and pound (their bytes)
std::string CommentText(const std::string& utf8)
{
    std::string out;
    for (size_t p = 0; p < utf8.size();)
    {
        const unsigned char b = static_cast<unsigned char>(utf8[p]);
        const size_t length = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
        if (b == '^')
            out += "\u2191";
        else if (b == '`')
            out += "\u00A3";
        else
            out.push_back(b < 0x20 || b == 0x7F ? ' ' : b < 0x80 ? static_cast<char>(b) : '?');
        p += b < 0x80 ? 1 : length;
    }
    return out;
}

bool InSigned(std::optional<int64_t> v)
{
    return v && *v >= -32768 && *v <= 32767;
}

class GensWriter : public ChainWriter
{
public:
    GensWriter(const ir::Program& program, const BackendOptions& options, BackendResult& result) : ChainWriter(program, options, result) {}

private:
    std::string Title() const override { return "GENS"; }
    /// GENS compares names on their first 6 characters, with case (its manual 2.2, checked in GENS4)
    size_t Significant() const override { return 6; }
    std::string Key(const std::string& name) const override { return name.substr(0, Significant()); }
    bool Reserved(const std::string& upper) const override { return kWords.count(upper) || z80::IsMnemonic(z80::Lower(upper)); }
    std::string Db() const override { return "DEFB"; }
    std::string Dw() const override { return "DEFW"; }

    const char* OperatorSymbol(Op op) const override
    {
        switch (op)
        {
            case Op::Add: return "+";
            case Op::Sub: return "-";
            case Op::Mul: return "*";
            case Op::Div: return "/";
            case Op::Mod: return "?";
            case Op::And: return "&";
            case Op::Or: return "@";
            case Op::Xor: return "!";
            default: return nullptr;
        }
    }

    /// GENS divides signed words: the high byte as (x & #FF00) / 256 & 255
    std::string HighOf(const std::string& chain) const override { return chain + "&#FF00/256&255"; }
    bool DivisionAgrees(const Expr& a, const Expr& b) const override { return InSigned(Value(a)) && InSigned(Value(b)); }
    bool ShiftAgrees(const Expr& a) const override
    {
        const auto v = Value(a);
        return v && *v >= 0 && *v <= 0x7FFF;
    }
    /// A product above 32767 in absolute value, its words taken as signed, is *ERROR* 15 (its manual 2.5, checked in GENS4)
    bool MultiplicationAgrees(std::optional<int64_t> a, std::optional<int64_t> b) const override
    {
        if (!a || !b)
            return true;
        const int64_t product = int64_t{static_cast<int16_t>(*a & 0xFFFF)} * static_cast<int16_t>(*b & 0xFFFF);
        return product >= -32767 && product <= 32767;
    }

    /// A string: DEFM "text" lines for runs of three characters or more; the quote, control characters and bytes above
    /// #7F as DEFB numbers
    bool InQuotes(char c) const override { return ChainWriter::InQuotes(c) && c != '^' && c != '`'; }

    void PutString(const std::string& text, std::vector<std::string>& items, std::vector<std::string>& out, const std::function<void()>& flush) override
    {
        std::string run;
        auto end = [&]() {
            if (run.size() >= 3)
            {
                flush();
                out.push_back("DEFM \"" + run + "\"");
            }
            else
                for (const char c : run)
                    items.push_back(std::to_string(static_cast<unsigned char>(c)));
            run.clear();
        };
        for (const char c : text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u >= 0x7F || c == '"' || c == ';' || !InQuotes(c))
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
        // GENS has none of the undocumented instructions: their bytes
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

    std::string RegisterText(const std::string& r) const override { return r == "af'" ? "AF'" : RetroWriter::RegisterText(r); }

    // ---- IF (true when not 0) / ELSE / END, one level ----

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
            return std::nullopt;
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
    std::vector<std::string> CloseNative(const Condition&) override { return {"END"}; }

    RepeatForm Repeat(const std::vector<ir::Line>&, size_t, size_t) const override { return RepeatForm::Unrolled; }

    std::vector<std::string> DirectiveText(const Statement& s, const std::string&) override
    {
        switch (s.directive)
        {
            case ir::DirectiveKind::Org: return {"ORG " + Known(s.args[0])};
            case ir::DirectiveKind::Ds:
            {
                // DEFS n: zeros (GENS4); another fill as DEFB
                const auto fill = s.args.size() > 1 ? Value(s.args[1]) : (s.operands.empty() ? std::optional<int64_t>(0) : std::nullopt);
                if (fill && *fill == 0 && s.operands.empty())
                    return {"DEFS " + Known(s.args[0])};
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
            case ir::DirectiveKind::Disp:
                return {NotConverted("DISP (GENS has no displacement: the code that follows is assembled where it lies)")};
            case ir::DirectiveKind::Ent: return {};
            case ir::DirectiveKind::Include:
            {
                std::string name = DiskName(options, s.text);
                if (const size_t dot = name.rfind('.'); dot != std::string::npos && name.size() - dot == 2)
                    name = name.substr(0, dot);
                return {"*F " + name};
            }
            case ir::DirectiveKind::Incbin: return IncbinAsData(s, "INCBIN (GENS has none): " + s.text);
            case ir::DirectiveKind::End:
                diagnostics.push_back({Severity::Info, line, 0, "END dropped (GENS' END closes an IF)"});
                return {";END"};
            case ir::DirectiveKind::SaveBinary:
                diagnostics.push_back({Severity::Info, line, 0, "SAVEBIN dropped: save the bytes from GENS"});
                return {";SAVEBIN \"" + s.text + "\""};
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }

    /// The label in column 0, the command from column 8, a blank before the operands
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
            if (!t.empty() && t[0] == '*')
            {
                // an assembler command (*F name): GENS reads it only from the line's first column
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
            out.back() += (out.back().empty() ? ";" : " ;") + CommentText(comment);
        return out;
    }
};
}  // namespace

BackendResult GensBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    BackendResult result;
    result.document.dialect = "gens";
    GensWriter w{program, options, result};
    w.Run();
    return result;
}
}  // namespace unrealasm::dialects
