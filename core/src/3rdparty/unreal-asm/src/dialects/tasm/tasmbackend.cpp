#include "dialects/tasm/tasmbackend.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>

#include "codecs/tasm/tasmtokens.h"
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

// TASM's keywords (its token tables, research-tasm.md §3) beside the mnemonics: a whole word that is one is stored as
// its token wherever it stands, so a label must not be one
const std::set<std::string> kWords = {
    "ORG", "EQU", "DEFB", "DEFW", "DEFS", "DEFM", "DB", "DW", "DS", "DM", "PHASE", "UNPHASE", "INCLUDE", "INCBIN", "DEFMAC",
    "ENDMAC", "DISPLAY", "INF", "SLI", "A", "B", "C", "D", "E", "H", "L", "I", "R", "AF", "BC", "DE", "HL", "SP", "IX",
    "IY", "HX", "LX", "HY", "LY", "NZ", "Z", "NC", "PO", "PE", "P", "M", "NV", "V",
};

/// A comment in characters TASM keeps: ASCII (its bytes #80-#F0 are its keywords' tokens, so a Russian letter is no
/// character of a TASM 3 / 4 line: the codec refuses it); others as "?", control characters as blanks
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

class TasmWriter : public RetroWriter
{
public:
    TasmWriter(const ir::Program& program, const BackendOptions& options, BackendResult& result, std::string v)
        : RetroWriter(program, options, result), version(std::move(v))
    {
    }

private:
    std::string version;   // "2.0", "3", "4.0", "4.12"

    bool V412() const { return version.empty() || version == "4.12"; }
    bool V40() const { return version == "4.0"; }
    bool V3() const { return version == "3" || version == "2.0"; }   // 2.0: 3.x's language as plain text
    std::string Dotted(const std::string& word) const { return V412() ? "." + word : word; }

    // ---- the dialect ----

    std::string Title() const override { return "TASM"; }
    /// TASM 4.12 keeps labels of any length (a hash table, its help; long ones only push the command to the right); 4.0
    /// refuses a label of more than 7 characters (error 1, its manual), as 3.x
    size_t Significant() const override { return V412() ? 24 : 7; }
    bool Reserved(const std::string& upper) const override { return kWords.count(upper) || z80::IsMnemonic(z80::Lower(upper)); }
    std::string Db() const override { return "DEFB"; }
    std::string Dw() const override { return "DEFW"; }

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

    static bool IsAtom(const Expr& e)
    {
        const Expr& u = Unwrapped(e);
        return u.kind == Expr::Kind::Number || u.kind == Expr::Kind::Symbol || u.kind == Expr::Kind::Current || u.kind == Expr::Kind::Raw;
    }

    /// One operand of an operator: an atom, or a part in parentheses
    std::optional<std::string> Term(const Expr& e)
    {
        const auto t = Print(e);
        if (!t)
            return std::nullopt;
        return IsAtom(e) ? *t : "(" + *t + ")";
    }

    /// The text TASM computes left to right: a chain whose operators each take a term
    std::optional<std::string> Print(const Expr& e)
    {
        switch (e.kind)
        {
            case Expr::Kind::Number: return NumberText(e);
            case Expr::Kind::Symbol: case Expr::Kind::Current: case Expr::Kind::Raw: return SymbolText(e);
            case Expr::Kind::Group: return Print(e.args[0]);
            case Expr::Kind::Unary:
            {
                const Expr& a = e.args[0];
                switch (e.op)
                {
                    case Op::Plus: return Print(a);
                    case Op::Negate: case Op::Not:
                    {
                        // -x as 0-x, ~x as #FFFF-x (TASM's examples write 0-(...); no unary operator is documented)
                        const auto t = Term(a);
                        return t ? std::optional<std::string>((e.op == Op::Negate ? "0-" : "#FFFF-") + *t) : std::nullopt;
                    }
                    case Op::High: case Op::Low:
                    {
                        // on the value so far: 4.12 and 3.x have no { } (error 8 in unreal-ng; 4.12's help lists
                        // + - * / | &), x/256 and x&255 in their 16-bit words; 4.0 the postfix { }
                        const auto t = Print(a);
                        if (!t)
                            return std::nullopt;
                        if (!V40())
                            return *t + (e.op == Op::High ? "/256" : "&255");
                        return *t + (e.op == Op::High ? "{" : "}");
                    }
                    case Op::SwapBytes:
                        if (V40())
                        {
                            const auto t = Print(a);
                            return t ? std::optional<std::string>(*t + "^") : std::nullopt;
                        }
                        break;
                    default: break;
                }
                break;
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
                    case Op::Mul: symbol = "*"; break;
                    case Op::And: symbol = "&"; break;
                    case Op::Or: symbol = "|"; break;
                    case Op::Xor:
                        if (V40())
                            symbol = "!";
                        break;
                    case Op::Div:
                        if (!InWord(a) || !InWord(b))
                            Problem("a division TASM makes in 16-bit unsigned words");
                        symbol = "/";
                        break;
                    case Op::Shl: case Op::Shr: case Op::ShrUnsigned:
                    {
                        // x*2^n, x/2^n
                        const auto shift = Value(b);
                        if (!shift || *shift < 0 || *shift > 15)
                            break;
                        if (e.op != Op::Shl && !InWord(a))
                            Problem("a right shift of a value outside 0..#FFFF");
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
        // An operation TASM lacks (a comparison, a remainder, XOR in 4.12 ...): its value, where known
        if (const auto v = Value(e))
            return Number(*v);
        Problem("an operation TASM has no operator for");
        return std::nullopt;
    }

    std::optional<std::string> Text(const Expr& e) override
    {
        auto t = Print(e);
        // an operand that starts with "(" is memory to TASM: 0+ makes it a value (TASM 4.12 article)
        if (t && !t->empty() && (*t)[0] == '(')
            *t = "0+" + *t;
        return t;
    }

    std::string RegisterText(const std::string& r) const override { return r == "af'" ? "AF'" : RetroWriter::RegisterText(r); }

    /// A string of DEFB: printable runs of three characters or more quoted; the quote, control characters and bytes
    /// above #7F as numbers; TASM 3 / 4.0 put texts in DEFM
    void PutString(const std::string& text, std::vector<std::string>& items, std::vector<std::string>& out, const std::function<void()>& flush) override
    {
        std::string run;
        auto end = [&]() {
            if (run.size() >= 3)
            {
                if (V412())
                    items.push_back("\"" + run + "\"");
                else
                {
                    flush();
                    out.push_back("DEFM \"" + run + "\"");
                }
            }
            else
                for (const char c : run)
                    items.push_back(std::to_string(static_cast<unsigned char>(c)));
            run.clear();
        };
        const std::vector<bool> keyword = KeywordLetters(text);
        for (size_t k = 0; k < text.size(); ++k)
        {
            const char c = text[k];
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u >= 0x7F || c == '"' || keyword[k])
            {
                end();
                items.push_back(std::to_string(u));
            }
            else
                run.push_back(c);
        }
        end();
    }

    /// The letters of the words in a string TASM's editor would store as its keywords' tokens (research-tasm.md §4:
    /// every whole keyword word, in strings too, shown and assembled in capitals): those not already in capitals
    std::vector<bool> KeywordLetters(const std::string& text) const
    {
        const codecs::tasm::TokenTable& table = V3() ? codecs::tasm::Tasm3Tokens() : V40() ? codecs::tasm::Tasm40Tokens() : codecs::tasm::Tasm412Tokens();
        std::vector<bool> out(text.size(), false);
        auto wordChar = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '@' || c == '$' || c == '#' || c == '\\';
        };
        for (size_t k = 0; k < text.size();)
        {
            if (!std::isalpha(static_cast<unsigned char>(text[k])) || (k > 0 && wordChar(text[k - 1])))
            {
                ++k;
                continue;
            }
            size_t end = k;
            while (end < text.size() && std::isalpha(static_cast<unsigned char>(text[end])))
                ++end;
            const std::string word = text.substr(k, end - k);
            if ((end == text.size() || !wordChar(text[end])) && word != z80::Upper(word))
            {
                const std::string upper = z80::Upper(word);
                const bool isKeyword = std::any_of(table.begin(), table.end(), [&](std::string_view t) {
                    while (!t.empty() && t.back() == ' ')
                        t.remove_suffix(1);
                    return t == upper;
                });
                if (isKeyword)
                    std::fill(out.begin() + static_cast<std::ptrdiff_t>(k), out.begin() + static_cast<std::ptrdiff_t>(end), true);
            }
            k = end;
        }
        return out;
    }

    std::optional<std::vector<std::string>> OwnInstruction(const Statement& s) override
    {
        const std::string& m = s.mnemonic;
        const auto& ops = s.operands;
        auto reg = [&](size_t k, const char* name) { return k < ops.size() && ops[k].kind == Operand::Kind::Register && ops[k].text == name; };
        // 3.x have no HX / LX / HY / LY (error 5 in unreal-ng; 4.0 added their tokens): bytes
        if (V3())
            if (auto bytes = HalfRegisterBytes(s))
                return bytes;
        if (m == "inf" || (m == "in" && (reg(0, "f") || (ops.size() == 1 && ops[0].kind == Operand::Kind::Indirect))))
            return std::vector<std::string>{V40() ? "INF" : Bytes({"#ED", "#70"}, "IN F,(C)")};
        if (m == "out" && ops.size() == 2 && ops[0].kind == Operand::Kind::Indirect && ops[1].kind == Operand::Kind::Immediate)
            return std::vector<std::string>{Bytes({"#ED", "#71"}, "OUT (C),0")};
        // TASM 4.12 assembles LD HX,LX / LD HX,HX with two prefixes (DD DD 65, seen in unreal-ng): bytes
        const auto half = [&](size_t k) -> int {
            if (k >= ops.size() || ops[k].kind != Operand::Kind::Register)
                return -1;
            const std::string& r = ops[k].text;
            return r == "ixh" || r == "iyh" ? 4 : r == "ixl" || r == "iyl" ? 5 : -1;
        };
        if (m == "ld" && ops.size() == 2 && half(0) >= 0 && half(1) >= 0)
            return std::vector<std::string>{Bytes({ops[0].text[1] == 'x' ? "#DD" : "#FD", Number(0x40 | (half(0) << 3) | half(1))}, "LD " + Operands(ops))};
        if ((m == "sll" || m == "sli") && ops.size() == 1)
        {
            if (V40())
                return std::vector<std::string>{"SLI " + Operands(ops)};
            // TASM 4.12 has no SLI ("they are not used that often, DEFB will do", its help)
            static const std::map<std::string, int> kRegisters = {{"b", 0}, {"c", 1}, {"d", 2}, {"e", 3}, {"h", 4}, {"l", 5}, {"a", 7}};
            const Operand& o = ops[0];
            if (o.kind == Operand::Kind::Register && kRegisters.count(o.text))
                return std::vector<std::string>{Bytes({"#CB", Number(0x30 | kRegisters.at(o.text))}, "SLI " + Operands(ops))};
            if (o.kind == Operand::Kind::Indirect && o.text == "hl")
                return std::vector<std::string>{Bytes({"#CB", "#36"}, "SLI (HL)")};
            if (o.kind == Operand::Kind::Indexed)
            {
                const auto d = Value(o.expr);
                return std::vector<std::string>{Bytes({o.text == "ix" ? "#DD" : "#FD", "#CB", d ? Number(*d & 0xFF) : Expression(o.expr), "#36"}, "SLI " + Operands(ops))};
            }
        }
        return std::nullopt;
    }

    // ---- 4.12's .IF: its part is assembled when the value is 0; one level ----

    std::optional<std::vector<std::string>> OpenNative(const Expr& condition, Condition& c) override
    {
        if (!V412())
            return std::nullopt;
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
        if (c.whenZero)
            return std::vector<std::string>{".IF " + c.test};
        return std::vector<std::string>{".IF " + c.test, ".ELSE"};
    }

    std::vector<std::string> ElseNative(Condition&) override { return {".ELSE"}; }
    std::vector<std::string> CloseNative(const Condition&) override { return {".ENDIF"}; }

    RepeatForm Repeat(const std::vector<ir::Line>&, size_t, size_t) const override { return RepeatForm::Unrolled; }

    /// A TR-DOS name for .INCLUDE / .INCBIN (no quotes; 8 characters)
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
                displaced = false;   // TASM's ORG ends a .PHASE
                return {"ORG " + Known(s.args[0])};
            case ir::DirectiveKind::Ds:
            {
                // DEFS n,fill: the fill repeated n times; a pattern of several bytes as DEFB
                const std::string count = Known(s.args[0]);
                const auto n = Value(s.args[0]);
                if (!V412() && (s.args.size() > 1 || !s.operands.empty()))
                {
                    // 3.x / 4.0 take DEFS n only (4.0 hangs in pass 1 on a second operand): the fill as DEFB
                    if (!n)
                        return {NotConverted("a DS fill of a count known only when assembling")};
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
                if (s.operands.empty())
                    return {"DEFS " + count + (s.args.size() > 1 ? "," + Known(s.args[1]) : std::string())};
                if (s.operands.size() == 1)
                    return {"DEFS " + count + "," + Known(s.operands[0].expr)};
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
            case ir::DirectiveKind::Disp: displaced = true; return {Dotted("PHASE") + " " + Known(s.args[0])};
            case ir::DirectiveKind::Ent:
                if (s.text == "if-displaced" && !displaced)
                    return {};
                displaced = false;
                return {Dotted("UNPHASE")};
            case ir::DirectiveKind::Include: return {Dotted("INCLUDE") + " " + FileName(s.text)};
            case ir::DirectiveKind::Incbin:
                if (s.args.size() > 1 || (!s.args.empty() && !(s.args[0].kind == Expr::Kind::Number && s.args[0].value == 0)))
                    return {NotConverted("INCBIN of a part of a file (TASM loads it all): " + s.text)};
                return {Dotted("INCBIN") + " " + FileName(s.text)};
            case ir::DirectiveKind::End:
                diagnostics.push_back({Severity::Info, line, 0, "END dropped (TASM assembles the whole text)"});
                return {";END"};
            case ir::DirectiveKind::SaveBinary:
                diagnostics.push_back({Severity::Info, line, 0, "SAVEBIN dropped: save the bytes from TASM"});
                return {";SAVEBIN \"" + s.text + "\""};
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }

    /// Laid out as TASM shows it: the label in column 0, the command from column 8, the operands from column 16
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
            const size_t blank = t.find(' ');
            text += blank == std::string::npos ? t : t.substr(0, blank);
            if (blank != std::string::npos)
            {
                text.append(text.size() < 16 ? 16 - text.size() : 1, ' ');
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

BackendResult TasmBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    BackendResult result;
    result.document.dialect = "tasm";
    // the version written, for its codec (without it the codec takes its newest, whose tokens differ)
    result.document.format = "tasm";
    result.document.subversion = options.targetVersion.empty() ? "4.12" : options.targetVersion;
    TasmWriter w{program, options, result, options.targetVersion};
    w.Run();
    return result;
}
}  // namespace unrealasm::dialects
