#include "dialects/masm/masmbackend.h"

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

// MASM's keywords (its token table, research-masm.md §4) beside the mnemonics
const std::set<std::string> kWords = {
    "ORG", "EQU", "PHASE", "UNPHASE", "BEGIN", "END", "INCBIN", "INCLUDE", "DB", "DEFB", "DEFS", "DEFW", "DS", "DW", "DOWN",
    "UP", "SYSTEM", "STOPKEY", "EXA", "INF", "SLI", "MAC", "ENDM", "IF", "ELSE", "ENDIF", "BANK", "BORDER", "CLS", "A", "B",
    "C", "D", "E", "H", "L", "I", "R", "XH", "XL", "YH", "YL", "IX", "IY", "AF", "HL", "DE", "BC", "SP", "M", "NC", "NV", "NZ",
    "P", "PE", "PO", "V", "Z",
};

/// MASM 1.1 labels: up to 10 characters (its help)
constexpr size_t kLabelLength = 10;
/// The longest line its editor holds (a 31-byte line buffer: longer lines break its screen, seen in unreal-ng)
constexpr size_t kWidth = 31;

/// The characters of a UTF-8 text
size_t Width(const std::string& utf8)
{
    return static_cast<size_t>(std::count_if(utf8.begin(), utf8.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

/// A comment line cut at the editor's width
std::string Cut(const std::string& t)
{
    size_t chars = 0, at = 0;
    while (at < t.size() && chars < kWidth)
    {
        ++chars;
        ++at;
        while (at < t.size() && (static_cast<unsigned char>(t[at]) & 0xC0) == 0x80)
            ++at;
    }
    return t.substr(0, at);
}

/// A comment for MASM: ASCII; others as "?" (MASM's bytes #80 and up are its keywords: a Russian letter is no character
/// of a MASM line, the codec refuses it)
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
        out.push_back('?');
        p += b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
    }
    return out;
}

class MasmWriter : public ChainWriter
{
public:
    MasmWriter(const ir::Program& program, const BackendOptions& options, BackendResult& result)
        : ChainWriter(program, options, result), version(options.targetVersion)
    {
    }

private:
    std::string version;   // "1.1" (also 1.3, the default), "2.0", "3.0"

    std::string Title() const override { return "MASM"; }
    size_t Significant() const override { return kLabelLength; }
    bool Reserved(const std::string& upper) const override { return kWords.count(upper) || z80::IsMnemonic(z80::Lower(upper)); }

    const char* OperatorSymbol(Op op) const override
    {
        switch (op)
        {
            case Op::Add: return "+";
            case Op::Sub: return "-";
            case Op::Mul: return "*";
            case Op::Div: return "/";
            case Op::And: return "&";
            case Op::Or: return "|";
            case Op::Xor: return version == "3.0" ? nullptr : "@";   // 3.0: error 0 on @ (seen in unreal-ng)
            default: return nullptr;
        }
    }

    /// The text ends with a name (its last token starts with a letter, "_" or "?"; "@" is not looked through: in 24@BETA
    /// it is the operator, and a +0 too many does no harm)
    static bool EndsWithName(const std::string& t)
    {
        size_t k = t.size();
        while (k > 0 && (std::isalnum(static_cast<unsigned char>(t[k - 1])) || t[k - 1] == '_' || t[k - 1] == '?' || t[k - 1] == '.'))
            --k;
        return k < t.size() && !std::isdigit(static_cast<unsigned char>(t[k])) && t[k] != '#' && t[k] != '%';
    }

    /// MASM reads "@" after a name as part of it (LAB@#FF: an error in MASM 1.1): +0 ends the name first
    std::string Guard(const std::string& left, const char* symbol) const override { return symbol[0] == '@' && EndsWithName(left) ? "+0" : ""; }

    size_t ExpressionWidth() const override { return kWidth - 11 - 5; }

    /// A string of DB: printable runs of three characters or more quoted; the quote, control characters and bytes above
    /// #7F as numbers
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
            if (u < 0x20 || u >= 0x7F || c == '"')
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
        if (m == "out" && ops.size() == 2 && ops[0].kind == Operand::Kind::Indirect && ops[1].kind == Operand::Kind::Immediate)
            return std::vector<std::string>{Bytes({"#ED", "#71"}, "OUT (C),0")};
        if ((m == "sll" || m == "sli") && ops.size() == 1)
            return std::vector<std::string>{"SLI " + Operands(ops)};
        if (m == "ex" && reg(0, "af") && reg(1, "af'"))
            return std::vector<std::string>{"EXA"};
        // MASM 1.1 refuses EX (SP),IY (error 0, seen in unreal-ng): its bytes
        if (m == "ex" && ops.size() == 2 && ops[0].kind == Operand::Kind::Indirect && ops[0].text == "sp" && reg(1, "iy"))
            return std::vector<std::string>{Bytes({"#FD", "#E3"}, "EX (SP),IY")};
        return std::nullopt;
    }

    std::string RegisterText(const std::string& r) const override
    {
        if (r == "ixh") return "XH";
        if (r == "ixl") return "XL";
        if (r == "iyh") return "YH";
        if (r == "iyl") return "YL";
        return r == "af'" ? "AF'" : RetroWriter::RegisterText(r);
    }

    /// BEGIN n ... END: a body without labels (MASM: "no labels inside"), assignments or blocks of its own
    RepeatForm Repeat(const std::vector<ir::Line>& lines, size_t begin, size_t end) const override
    {
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

    /// A TR-DOS name for INCLUDE / INCBIN (bare, 8 characters)
    std::string FileName(const std::string& given)
    {
        const std::string name = DiskName(options, given);
        std::string base = name;
        if (const size_t dot = name.rfind('.'); dot != std::string::npos && name.size() - dot == 2)
            base = name.substr(0, dot);
        if (base.size() > 8)
        {
            diagnostics.push_back({Severity::Warning, line, 0, "\"" + name + "\": a TR-DOS name has 8 characters"});
            base.resize(8);
        }
        return base;
    }

    std::vector<std::string> DirectiveText(const Statement& s, const std::string&) override
    {
        switch (s.directive)
        {
            case ir::DirectiveKind::Org:
                displaced = false;   // MASM's ORG sets both addresses
                return {"ORG " + Known(s.args[0])};
            case ir::DirectiveKind::Ds:
            {
                // DEFS n,list: the list repeated n times; a pattern cut at n bytes as DEFB
                const std::string count = Known(s.args[0]);
                if (s.operands.empty())
                    return {"DEFS " + count + (s.args.size() > 1 ? "," + Known(s.args[1]) : std::string())};
                if (s.operands.size() == 1)
                    return {"DEFS " + count + "," + Known(s.operands[0].expr)};
                const auto n = Value(s.args[0]);
                if (!n)
                    return {NotConverted("a fill pattern of a count known only when assembling")};
                std::vector<std::string> items;
                for (int64_t k = 0; k < *n; ++k)
                {
                    const auto v = Value(s.operands[static_cast<size_t>(k) % s.operands.size()].expr);
                    if (!v)
                        return {NotConverted("DS with a fill known only when assembling")};
                    items.push_back(Number(*v & 0xFF));
                }
                return DataLines(Db(), items);
            }
            case ir::DirectiveKind::Repeat:
                nativeBlock = true;
                repeats.push_back(true);
                return {"BEGIN " + Known(s.args[0])};
            case ir::DirectiveKind::EndRepeat:
                if (repeats.empty())
                    return {NotConverted("EDUP without DUP")};
                repeats.pop_back();
                nativeBlock = false;
                return {"END"};
            case ir::DirectiveKind::Disp: displaced = true; return {"PHASE " + Known(s.args[0])};
            case ir::DirectiveKind::Ent:
                if (s.text == "if-displaced" && !displaced)
                    return {};
                displaced = false;
                return {"UNPHASE"};
            case ir::DirectiveKind::Include: return {"INCLUDE " + FileName(s.text)};
            case ir::DirectiveKind::Incbin:
                if (s.args.size() > 1 || (!s.args.empty() && !(s.args[0].kind == Expr::Kind::Number && s.args[0].value == 0)))
                    return {NotConverted("INCBIN of a part of a file (MASM loads it all): " + s.text)};
                return {"INCBIN " + FileName(s.text)};
            case ir::DirectiveKind::End:
                diagnostics.push_back({Severity::Info, line, 0, "END dropped (MASM assembles the whole text; its END closes a BEGIN)"});
                return {";END"};
            case ir::DirectiveKind::SaveBinary:
                diagnostics.push_back({Severity::Info, line, 0, "SAVEBIN dropped: save the bytes from MASM"});
                return {";SAVEBIN \"" + s.text + "\""};
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }

    size_t DataWidth() const override { return kWidth - 11 - 5; }

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

    /// The label in column 0, the command from column 11 (labels have up to 10 characters), the operands after a blank;
    /// a comment that does not fit in the editor's 31 characters goes on a line of its own, cut there
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
            text.append(text.size() < 11 ? 11 - text.size() : 1, ' ');
            out.push_back(text + t);
        }
        if (first && (!label.empty() || hasComment || bare))
            out.push_back(label);
        if (out.empty())
            return out;
        if (hasComment)
        {
            const std::string note = ";" + CommentText(comment);
            if (out.back().empty())
                out.back() = note;
            else if (out.back()[0] != ';' && Width(out.back()) + 1 + Width(note) <= kWidth)
                out.back() += " " + note;
            else
                out.push_back(note);
        }
        // a comment after a statement (the bytes of an instruction MASM lacks) goes on a line of its own when too long
        for (size_t k = 0; k < out.size(); ++k)
        {
            const size_t semicolon = CommentAt(out[k]);
            if (Width(out[k]) > kWidth && semicolon != std::string::npos && semicolon > 0)
            {
                std::string note = out[k].substr(semicolon);
                out[k].resize(semicolon);
                while (!out[k].empty() && out[k].back() == ' ')
                    out[k].pop_back();
                out.insert(out.begin() + static_cast<std::ptrdiff_t>(k) + 1, note);
            }
        }
        for (std::string& t : out)
        {
            if (Width(t) <= kWidth)
                continue;
            if (t[0] == ';')
                t = Cut(t);
            else
                diagnostics.push_back({Severity::Info, line, 0, "a line of " + std::to_string(Width(t)) + " characters: MASM assembles it, its editor shows 31: " + t});
        }
        return out;
    }
};
}  // namespace

BackendResult MasmBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    BackendResult result;
    result.document.dialect = "masm";
    MasmWriter w{program, options, result};
    w.Run();
    return result;
}
}  // namespace unrealasm::dialects
