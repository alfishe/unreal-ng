#include "dialects/zeus/zeusbackend.h"

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

// ZEUS' reserved words beside the mnemonics (manual 5.1; the versions' tables, research-zeus.md §3)
const std::set<std::string> kWords = {
    "ORG", "EQU", "DEFB", "DEFW", "DEFM", "DEFS", "DB", "DW", "DM", "DS", "DISP", "ENT", "INCBIN", "INCLUDE", "PLACE", "OPEN",
    "A", "B", "C", "D", "E", "H", "L", "I", "R", "AF", "BC", "DE", "HL", "SP", "IX", "IY", "NZ", "Z", "NC", "PO", "PE", "P",
    "M", "V", "NV",
};

/// A comment for ZEUS: ASCII only (other characters as "?"); ^ and ` are the Spectrum's arrow and pound (their bytes)
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

class ZeusWriter : public ChainWriter
{
public:
    ZeusWriter(const ir::Program& program, const BackendOptions& options, BackendResult& result, std::string v)
        : ChainWriter(program, options, result), version(std::move(v))
    {
    }

private:
    std::string version;   // "1983", "3.1" (ZEUS+ 3.1), "gg", "pht" (v7.E), "1.1" (PHT's table, no * / %), "primus" (Primus 2.9)

    bool Pht() const { return version == "pht" || version == "1.1"; }
    bool V7e() const { return version == "pht"; }   // * and %binary (1.1 refuses them: error 0)
    bool Primus() const { return version == "primus"; }
    /// Primus and v7.E multiply a*b by b's bits from the top and stop at the first carry: 3*#7FFF is #7FFA, 2420*#FFFF
    /// #1B98 (Primus), 35*#2000 #1800 (v7.E)
    bool MultiplicationAgrees(std::optional<int64_t> a, std::optional<int64_t> b) const override
    {
        if (!(Primus() || V7e()) || !a || !b)
            return true;
        const uint32_t x = static_cast<uint32_t>(*a & 0xFFFF), y = static_cast<uint32_t>(*b & 0xFFFF);
        return z80::StopAtCarryProduct(x, y) == ((x * y) & 0xFFFF);
    }
    int orgs = 0;          // Primus takes one ORG

    std::string Title() const override { return Primus() ? "Primus" : "ZEUS"; }
    /// labels of up to 14 characters, compared whole with case (manual 5.1.1); Primus 16 (its PRI.DOC); ZEUS+ 3.1 6 (a
    /// seventh is its error 1, "label too long", seen in unreal-ng)
    size_t Significant() const override { return Primus() ? 16 : version == "3.1" ? 6 : 14; }
    std::string Key(const std::string& name) const override { return name; }
    bool Reserved(const std::string& upper) const override { return kWords.count(upper) || z80::IsMnemonic(z80::Lower(upper)); }
    /// letters and digits, a letter first (no "_" in ZEUS)
    std::string Sanitized(const std::string& name) const override
    {
        std::string out;
        for (const char c : name)
            if (std::isalnum(static_cast<unsigned char>(c)))
                out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])))
            out = "L" + out;
        return out;
    }
    /// the data words: DEFB / DEFW / DEFM / DEFS in 1983 and Primus, renamed DB / DW / DM / DS by GG and PHT (their
    /// tokens #96-#99: the old spellings are no words there)
    bool ShortData() const { return version == "gg" || Pht(); }
    std::string Db() const override { return ShortData() ? "DB" : "DEFB"; }
    std::string Dw() const override { return ShortData() ? "DW" : "DEFW"; }
    std::string Dm() const { return ShortData() ? "DM" : "DEFM"; }
    std::string Dss() const { return ShortData() ? "DS" : "DEFS"; }

    const char* OperatorSymbol(Op op) const override
    {
        switch (op)
        {
            case Op::Add: return "+";
            case Op::Sub: return "-";
            case Op::And: return "&";
            case Op::Or: return "!";
            case Op::Mul: return V7e() || Primus() ? "*" : nullptr;   // v7.E (its / rounds: not used), Primus
            case Op::Div: return Primus() ? "/" : nullptr;            // Primus: unsigned, truncating
            case Op::Mod: return Primus() ? "?" : nullptr;
            default: return nullptr;
        }
    }
    /// Primus reads "?" after a name as part of it (COUNT?7: an undefined label, seen in unreal-ng): +0 ends the name
    std::string Guard(const std::string& left, const char* symbol) const override
    {
        if (!Primus() || symbol[0] != '?')
            return {};
        size_t k = left.size();
        while (k > 0 && (std::isalnum(static_cast<unsigned char>(left[k - 1])) || std::string("_?@.$").find(left[k - 1]) != std::string::npos))
            --k;
        return k < left.size() && !std::isdigit(static_cast<unsigned char>(left[k])) && left[k] != '#' && left[k] != '%' ? "+0" : "";
    }

    /// no division in ZEUS 1983 (v7.E's rounds): the high byte only as a value; Primus /256
    std::string HighOf(const std::string& chain) const override { return Primus() ? chain + "/256" : std::string(); }

    std::optional<std::string> Text(const Expr& e) override
    {
        if (e.kind == Expr::Kind::Unary && e.op == Op::High && !Primus())
        {
            if (const auto v = Value(e))
                return Number(*v);
            Problem("a high byte ZEUS has no operator for");
            return std::nullopt;
        }
        return ChainWriter::Text(e);
    }

    /// "c is a character in ZEUS (no closing quote); hex in #, binary as hex before v7.E
    std::string NumberText(const Expr& e) const override
    {
        const int64_t v = e.value & 0xFFFF;
        if (e.spelling == ir::NumberSpelling::Character && e.text.size() == 1 && e.text[0] > 0x20 && e.text[0] < 0x7F && e.text[0] != '^' &&
            e.text[0] != '`' && !Primus())
            return "\"" + e.text;   // Primus refuses "A": the number
        if (e.spelling == ir::NumberSpelling::Binary && !V7e())
            return "#" + z80::HexDigits(static_cast<uint64_t>(v), v > 0xFF ? 4 : 2);
        if (e.spelling == ir::NumberSpelling::Character)
            return Number(v);
        return ChainWriter::NumberText(e);
    }

    /// DEFM with a delimiter the text does not hold; control characters and bytes above #7F as DEFB numbers
    void PutString(const std::string& text, std::vector<std::string>& items, std::vector<std::string>& out, const std::function<void()>& flush) override
    {
        std::string run;
        auto end = [&]() {
            if (run.size() >= 3)
            {
                char delimiter = 0;
                for (const char c : std::string("/\"'|#"))
                    if (run.find(c) == std::string::npos)
                    {
                        delimiter = c;
                        break;
                    }
                if (delimiter)
                {
                    flush();
                    out.push_back(Dm() + " " + delimiter + run + delimiter);
                    run.clear();
                    return;
                }
            }
            for (const char c : run)
                items.push_back(std::to_string(static_cast<unsigned char>(c)));
            run.clear();
        };
        for (const char c : text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u >= 0x7F || c == '^' || c == '`')   // ^ ` : no ASCII byte in the Spectrum's set
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
        // ZEUS has none of the undocumented instructions (INF, SLL are labels to it): their bytes
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

    RepeatForm Repeat(const std::vector<ir::Line>&, size_t, size_t) const override { return RepeatForm::Unrolled; }

    std::string dispOffset{};   // the offset of the displacement in force (a number, or the names' difference)
    bool orgRefused = false;    // Primus: a second ORG was left out (its addresses part from the source's from there)

    std::vector<std::string> DirectiveText(const Statement& s, const std::string&) override
    {
        switch (s.directive)
        {
            case ir::DirectiveKind::Org:
                if (Primus() && orgs++ > 0)
                {
                    // a second ORG ahead of $: the gap as a hole (Primus refuses another ORG: Duplicate ORG)
                    const auto to = s.args.empty() ? std::nullopt : Value(s.args[0]);
                    // (after one refused, the text's addresses are no longer the source's: no more holes)
                    if (to && here && *to >= *here && !displaced && !orgRefused)
                    {
                        if (*to == *here)
                            return {};
                        diagnostics.push_back({Severity::Info, line, 0, "ORG forward written as DEFS (Primus takes one ORG)"});
                        return {Dss() + " " + std::to_string(*to - *here)};
                    }
                    orgRefused = true;
                    return {NotConverted("a second ORG (Primus takes one: Duplicate ORG)")};
                }
                if (displaced)
                {
                    displaced = false;
                    return {"DISP 0", "ORG " + Known(s.args[0])};
                }
                return {"ORG " + Known(s.args[0])};
            case ir::DirectiveKind::Ds:
            {
                // DEFS leaves memory as it was in ZEUS 1983 / GG: a fill (zeros too) as DEFB where the count is known
                const auto n = Value(s.args[0]);
                std::vector<int64_t> pattern;
                if (s.operands.empty())
                    pattern.push_back(s.args.size() > 1 ? Value(s.args[1]).value_or(-1) : 0);
                for (const Operand& o : s.operands)
                    pattern.push_back(Value(o.expr).value_or(-1));
                if (Pht() && pattern.size() == 1 && pattern[0] == 0)
                    return {Dss() + " " + Known(s.args[0])};
                if (!n || std::any_of(pattern.begin(), pattern.end(), [](int64_t v) { return v < 0; }))
                    return {NotConverted("DS of a count or fill known only when assembling (ZEUS's DEFS leaves memory as it was)")};
                if (*n > 4096)
                    return {NotConverted("DS of " + std::to_string(*n) + " filled bytes: more DEFB than ZEUS's text holds (its DEFS leaves memory as it was)")};
                std::vector<std::string> items;
                for (int64_t k = 0; k < *n; ++k)
                    items.push_back(Number(pattern[static_cast<size_t>(k) % pattern.size()] & 0xFF));
                return DataLines(Db(), items);
            }
            case ir::DirectiveKind::Disp:
            {
                // DISP d puts the code at ORG + d: the run address as ORG, the offset to the place where it lies
                if (Primus())
                    return {NotConverted("DISP (Primus has none: its DISK includes a file)")};
                if (displaced)
                    return {NotConverted("a DISP inside another")};
                strict = true;
                const auto run = Value(s.args[0]);
                strict = false;
                displaced = true;
                if (run && here)
                {
                    dispOffset = Number((*here - *run) & 0xFFFF);
                    return {"ORG " + Number(*run), "DISP " + dispOffset};
                }
                // known only when ZEUS assembles: the place and the run address under names of their own
                const std::string place = Give("ZPLACE"), at = Give("ZRUN");
                dispOffset = place + "-" + at;
                return {place + "\tEQU $", at + "\tEQU " + Known(s.args[0]), "ORG " + at, "DISP " + dispOffset};
            }
            case ir::DirectiveKind::Ent:
                if (!displaced)
                    return {};
                displaced = false;
                // back to the place: DISP 0 puts the code at $ again, ORG moves on to where the displaced code ends
                return {"DISP 0", "ORG $+" + dispOffset};
            case ir::DirectiveKind::Include:
                if (Primus())
                {
                    std::string name = DiskName(options, s.text);
                    if (const size_t dot = name.rfind('.'); dot != std::string::npos)
                        name = name.substr(0, dot);
                    return {"DISK " + name};
                }
                if (Pht())
                {
                    std::string name = DiskName(options, s.text);
                    if (const size_t dot = name.rfind('.'); dot != std::string::npos && name.size() - dot == 2)
                        name = name.substr(0, dot);
                    return {"INCLUDE " + name};
                }
                return {NotConverted("INCLUDE (ZEUS 1983 and GG have none): " + s.text)};
            case ir::DirectiveKind::Incbin:
                if (s.args.size() > 1 || (!s.args.empty() && !(s.args[0].kind == Expr::Kind::Number && s.args[0].value == 0)))
                    return IncbinAsData(s, "INCBIN of a part of a file: " + s.text);
                if (version == "gg")
                    return {"INCBIN \"" + DiskName(options, s.text) + "\""};
                if (Pht())
                    return {"PLACE " + DiskName(options, s.text)};
                return IncbinAsData(s, "INCBIN (ZEUS 1983 has none): " + s.text);
            case ir::DirectiveKind::End:
                diagnostics.push_back({Severity::Info, line, 0, "END dropped (ZEUS assembles the whole text)"});
                return {";END"};
            case ir::DirectiveKind::SaveBinary:
                diagnostics.push_back({Severity::Info, line, 0, "SAVEBIN dropped: save the bytes from ZEUS"});
                return {";SAVEBIN \"" + s.text + "\""};
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }

    /// A label, a blank, the instruction (a line without a label starts with a blank: ZEUS' layout)
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
                    out.push_back(label);
                out.push_back(t.substr(0, tab) + " " + t.substr(tab + 1));
                first = false;
                continue;
            }
            const std::string head = first ? label : std::string();
            first = false;
            if (!t.empty() && t[0] == ';')
            {
                out.push_back(head.empty() ? " " + t : head + " " + t);
                continue;
            }
            out.push_back(head + " " + t);
        }
        // a label alone is an error in ZEUS 1983 (error 0, seen in unreal-ng): EQU $
        if (first && !label.empty())
            out.push_back(label + " EQU $");
        else if (first && (hasComment || bare))
            out.push_back(std::string());
        for (std::string& t : out)
            if (t.find(' ') == std::string::npos && !t.empty())
                t += " EQU $";
        if (out.empty())
            return out;
        if (hasComment)
            out.back() += " ;" + CommentText(comment);
        return out;
    }
};
}  // namespace

BackendResult ZeusBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    BackendResult result;
    result.document.dialect = "zeus";
    ZeusWriter w{program, options, result, options.targetVersion.empty() ? "1983" : options.targetVersion};
    w.Run();
    return result;
}
}  // namespace unrealasm::dialects
