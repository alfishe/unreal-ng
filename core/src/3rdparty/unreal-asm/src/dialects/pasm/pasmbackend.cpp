#include "dialects/pasm/pasmbackend.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>

#include "dialects/common/diskname.h"
#include "dialects/common/chainwriter.h"
#include "dialects/common/z80.h"
#include "dialects/pasm/pasmcurrent.h"

namespace unrealasm::dialects
{
namespace
{
using ir::Expr;
using ir::Op;
using ir::Operand;
using ir::Statement;

// PASM's directives, registers and conditions beside the mnemonics (its help)
const std::set<std::string> kWords = {
    "ORG", "EQU", "ENT", "DB", "DW", "DUP", "ITXT", "IBIN", "SLI", "A", "B", "C", "D", "E", "H", "L", "I", "R",
    "AF", "BC", "DE", "HL", "SP", "IX", "IY", "NZ", "Z", "NC", "PO", "PE", "P", "M",
};

/// PASM reads 64 columns of a line (a longer one goes on as if it were the next line: checked in PASM 3.0)
constexpr size_t kLineWidth = 64;

/// A comment for PASM: ASCII only (other characters as "?")
std::string CommentText(const std::string& utf8)
{
    std::string out;
    for (size_t p = 0; p < utf8.size();)
    {
        const unsigned char b = static_cast<unsigned char>(utf8[p]);
        const size_t length = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
        out.push_back(b < 0x20 || b == 0x7F ? ' ' : b < 0x80 ? static_cast<char>(b) : '?');
        p += b < 0x80 ? 1 : length;
    }
    return out;
}

/// The columns a line takes (TAB to the next multiple of 8)
size_t Columns(const std::string& text)
{
    size_t column = 0;
    for (const char c : text)
        column = c == '\t' ? (column / 8 + 1) * 8 : column + 1;
    return column;
}

/// A character PASM takes between apostrophes (no apostrophe; no ";", which a reader may take for a comment)
bool Quotable(char c)
{
    return c >= 0x20 && c < 0x7F && c != '\'' && c != ';';
}

/// $ replaced by the address it stands for
Expr AtAddress(const Expr& e, int64_t address)
{
    if (e.kind == Expr::Kind::Current)
        return Expr::Number(address);
    Expr out = e;
    for (Expr& a : out.args)
        a = AtAddress(a, address);
    return out;
}

bool EightBit(const Operand& o)
{
    static const std::set<std::string> kRegisters = {"a", "b", "c", "d", "e", "h", "l", "ixh", "ixl", "iyh", "iyl"};
    return (o.kind == Operand::Kind::Register && kRegisters.count(o.text)) || (o.kind == Operand::Kind::Indirect && o.text == "hl") ||
           o.kind == Operand::Kind::Indexed;
}

/// Operand k is a byte PASM checks to be in 0..255 (a displacement aside: it takes (IX-3))
bool ByteOperand(const Statement& s, size_t k)
{
    static const std::set<std::string> kArithmetic = {"add", "adc", "sub", "sbc", "and", "xor", "or", "cp"};
    const Operand& o = s.operands[k];
    const std::string& m = s.mnemonic;
    if (o.kind == Operand::Kind::Memory)
        return m == "in" || m == "out";   // the port
    if (o.kind != Operand::Kind::Immediate)
        return false;
    if (kArithmetic.count(m))
        return true;
    if (m == "ld" && s.operands.size() == 2)
        return EightBit(s.operands[1 - k]);
    return false;
}

class PasmWriter : public ChainWriter
{
public:
    PasmWriter(const ir::Program& program, const BackendOptions& options, BackendResult& result) : ChainWriter(program, options, result) {}

private:
    std::string Title() const override { return "PASM"; }
    /// up to 14 characters (a fifteenth is "Bad label": checked in PASM 3.0)
    size_t Significant() const override { return 14; }
    bool Reserved(const std::string& upper) const override { return kWords.count(upper) || z80::IsMnemonic(z80::Lower(upper)); }
    /// a name starts with a letter ("_X" is refused)
    std::string Sanitized(const std::string& name) const override
    {
        std::string out = RetroWriter::Sanitized(name);
        const size_t letter = out.find_first_not_of('_');
        out.erase(0, letter == std::string::npos ? out.size() : letter);
        return out.empty() || !std::isalpha(static_cast<unsigned char>(out[0])) ? "L" + out : out;
    }
    size_t ExpressionWidth() const override { return 40; }
    std::string CommentAfter() const override { return " ;"; }
    bool CurrentInEqu() const override { return false; }

    const char* OperatorSymbol(Op op) const override
    {
        switch (op)
        {
            case Op::Add: return "+";
            case Op::Sub: return "-";
            case Op::Mul: return "*";
            case Op::Div: return "/";
            default: return nullptr;
        }
    }

    std::string NumberText(const Expr& e) const override
    {
        if (e.spelling == ir::NumberSpelling::Hex)
            return "#" + z80::HexDigits(static_cast<uint64_t>(e.value & 0xFFFF), std::clamp(e.digits, 1, 4));
        // one character between apostrophes ('AB' is a syntax error)
        if (e.spelling == ir::NumberSpelling::Character)
            return !inData && e.text.size() == 1 && Quotable(e.text[0]) ? "'" + e.text + "'" : Number(e.value);
        return ChainWriter::NumberText(e);
    }

    std::string RegisterText(const std::string& r) const override { return r == "af'" ? "AF'" : RetroWriter::RegisterText(r); }

    /// (IX) as (IX+0): PASM refuses (IX) without an offset
    std::string OperandText(const Operand& o) override
    {
        if (o.kind == Operand::Kind::Indirect && (o.text == "ix" || o.text == "iy"))
            return "(" + z80::Upper(o.text) + "+0)";
        // a displacement known here as its signed value ((IX-3): a word below 0 is out of its range)
        if (o.kind == Operand::Kind::Indexed && !IsAtom(o.expr))
            if (const auto d = Value(o.expr))
            {
                const int64_t signedByte = ((*d & 0xFF) ^ 0x80) - 0x80;
                return "(" + z80::Upper(o.text) + (signedByte < 0 ? "-" + std::to_string(-signedByte) : "+" + std::to_string(signedByte)) + ")";
            }
        return ChainWriter::OperandText(o);
    }

    /// A DB item below 0 as its byte (PASM: "Value out of range" for -1); one made from $ as its value (PASM refuses a
    /// byte computed from $ by subtraction)
    std::string DataItem(const Operand& item, bool words) override
    {
        if (displaced && statementAddress && UsesCurrent(item.expr))
        {
            // in a displaced block $ is the run address: its value
            Operand at = item;
            at.expr = AtAddress(item.expr, *statementAddress);
            return DataItem(at, words);
        }
        if (!words && item.kind == Operand::Kind::Immediate)
            if (const auto v = Value(item.expr); v && (*v < 0 || UsesCurrent(item.expr)))
                return Number(*v & 0xFF);
        // an item starting with an apostrophe is a string for PASM: characters in DB / DW as numbers
        inData = true;
        const std::string text = OperandText(item);
        inData = false;
        return text;
    }
    bool inData = false;

    /// A string: runs of two characters or more between apostrophes, the others as numbers
    void PutString(const std::string& text, std::vector<std::string>& items, std::vector<std::string>&, const std::function<void()>&) override
    {
        std::string run;
        auto end = [&]() {
            if (run.size() >= 2)
                for (size_t at = 0; at < run.size(); at += 32)
                    items.push_back("'" + run.substr(at, 32) + "'");
            else
                for (const char c : run)
                    items.push_back(std::to_string(static_cast<unsigned char>(c)));
            run.clear();
        };
        for (const char c : text)
        {
            if (Quotable(c))
                run.push_back(c);
            else
            {
                end();
                items.push_back(std::to_string(static_cast<unsigned char>(c)));
            }
        }
        end();
    }

    bool adjusted = false;   // the instruction being written has its operands adjusted already
    bool orgRefused = false; // a second ORG was left out (the text's addresses part from the source's from there)

    std::optional<std::vector<std::string>> OwnInstruction(const Statement& s) override
    {
        if (!adjusted)
        {
            // a byte below 0 or made from $ as its value; $ in a word as PASM's (pasmcurrent.h): the source's $ as $-k
            Statement moved = s;
            bool changed = false;
            if (displaced && statementAddress)
            {
                // a displaced block (no DISP in PASM): $ is the run address, a relative jump goes by its distance there
                for (Operand& o : moved.operands)
                    if (UsesCurrent(o.expr))
                    {
                        o.expr = AtAddress(o.expr, *statementAddress);
                        changed = true;
                    }
                if ((s.mnemonic == "jr" || s.mnemonic == "djnz") && !moved.operands.empty())
                    if (const auto target = Value(moved.operands.back().expr))
                    {
                        // PASM's $ there is the place + 1, the offset counts from the place + 2
                        const int64_t k = *target - *statementAddress - 1;
                        std::string text = z80::Upper(s.mnemonic) + " ";
                        if (moved.operands.size() == 2)
                            text += z80::Upper(moved.operands[0].text) + ",";
                        return std::vector<std::string>{text + "$" + (k < 0 ? "-" + std::to_string(-k) : "+" + std::to_string(k))};
                    }
            }
            for (size_t n = 0; n < moved.operands.size(); ++n)
            {
                Expr& e = moved.operands[n].expr;
                const bool current = UsesCurrent(e);
                if (ByteOperand(s, n) || (current && moved.operands[n].kind == Operand::Kind::Indexed))
                {
                    if (const auto v = Value(e); v && (*v < 0 || current))
                    {
                        e = Expr::Number(*v & 0xFF);
                        changed = true;
                    }
                }
                else if (current)
                {
                    if (const int64_t k = pasm::CurrentShift(s, n))
                    {
                        e = ShiftCurrent(e, k);
                        changed = true;
                    }
                }
            }
            if (changed)
            {
                adjusted = true;
                std::vector<std::string> out = InstructionText(moved);
                adjusted = false;
                return out;
            }
        }
        const std::string& m = s.mnemonic;
        const auto& ops = s.operands;
        auto reg = [&](size_t k, const char* name) { return k < ops.size() && ops[k].kind == Operand::Kind::Register && ops[k].text == name; };
        // PASM has neither of these
        if (m == "inf" || (m == "in" && (reg(0, "f") || (ops.size() == 1 && ops[0].kind == Operand::Kind::Indirect))))
            return std::vector<std::string>{Bytes({"#ED", "#70"}, "IN F,(C)")};
        if (m == "out" && ops.size() == 2 && ops[0].kind == Operand::Kind::Indirect && ops[1].kind == Operand::Kind::Immediate)
            return std::vector<std::string>{Bytes({"#ED", "#71"}, "OUT (C),0")};
        if (m == "jp" && ops.size() == 1 && ops[0].kind == Operand::Kind::Indirect)
            return std::vector<std::string>{"JP (" + z80::Upper(ops[0].text) + ")"};
        if ((m == "sll" || m == "sli") && ops.size() == 1)
            return std::vector<std::string>{"SLI " + Operands(ops)};
        // no index halves
        return HalfRegisterBytes(s);
    }

    RepeatForm Repeat(const std::vector<ir::Line>&, size_t, size_t) const override { return RepeatForm::Unrolled; }

    bool origin = false;   // the ORG is written (PASM takes one, before the code: "Bad ORG")

    std::vector<std::string> DirectiveText(const Statement& s, const std::string&) override
    {
        switch (s.directive)
        {
            case ir::DirectiveKind::Org:
            {
                if (!origin)
                {
                    origin = true;
                    return {"ORG " + Known(s.args[0])};
                }
                // another ORG: a gap forward as zeros, else not for PASM
                strict = true;
                const auto to = Value(s.args[0]);
                strict = false;
                // (after one refused, the text's addresses are no longer the source's: no more zeros)
                if (to && here && *to >= *here && !orgRefused)
                {
                    if (*to == *here)
                        return {};
                    diagnostics.push_back({Severity::Info, line, 0, "ORG forward written as zeros (PASM takes one ORG)"});
                    return {"DB 0 DUP " + std::to_string(*to - *here)};
                }
                orgRefused = true;
                return {NotConverted("a second ORG (PASM takes one, at the start)")};
            }
            case ir::DirectiveKind::Disp:
                // no DISP in PASM: the block's labels are written as EQU of their run addresses, its $ as values and
                // its relative jumps by their distance (OwnInstruction, Layout)
                displaced = true;
                diagnostics.push_back({Severity::Info, line, 0, "DISP: the block's labels written as EQU (PASM has no displacement)"});
                return {};
            case ir::DirectiveKind::Ent:
                displaced = false;
                return {};
            case ir::DirectiveKind::Ds:
            {
                const auto fill = s.args.size() > 1 ? Value(s.args[1]) : (s.operands.empty() ? std::optional<int64_t>(0) : std::nullopt);
                const auto n = Value(s.args[0]);
                std::vector<int64_t> pattern;
                if (s.operands.empty() && fill)
                    pattern.push_back(*fill & 0xFF);
                for (const Operand& o : s.operands)
                    if (const auto v = Value(o.expr))
                        pattern.push_back(*v & 0xFF);
                if (!n || pattern.empty() || pattern.size() != std::max<size_t>(1, s.operands.size()))
                    return {NotConverted("DS with a fill or count known only when assembling")};
                if (*n <= 0)
                    return {};
                // "value DUP count" is the only item of its line
                if (pattern.size() == 1)
                    return {"DB " + Number(pattern[0]) + " DUP " + std::to_string(*n)};
                std::vector<std::string> items;
                for (int64_t k = 0; k < *n; ++k)
                    items.push_back(Number(pattern[static_cast<size_t>(k) % pattern.size()]));
                return DataLines(Db(), items, DataWidth());
            }
            case ir::DirectiveKind::Include:
            case ir::DirectiveKind::Incbin:
            {
                // the file's name on the disk: 8 characters, without the folder and the extension
                std::string name = DiskName(options, s.text);
                if (const size_t dot = name.rfind('.'); dot != std::string::npos && dot > 0)
                    name.resize(dot);
                return {(s.directive == ir::DirectiveKind::Include ? "ITXT " : "IBIN ") + name.substr(0, 8)};
            }
            case ir::DirectiveKind::End:
                diagnostics.push_back({Severity::Info, line, 0, "END dropped (PASM compiles the whole text)"});
                return {";END"};
            case ir::DirectiveKind::SaveBinary:
                diagnostics.push_back({Severity::Info, line, 0, "SAVEBIN dropped: save the code from PASM (PUT OBJ)"});
                return {";SAVEBIN \"" + s.text + "\""};
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }

    /// A line of at most 64 columns, else a warning
    std::string Fitted(const std::string& text)
    {
        if (Columns(text) > kLineWidth)
            diagnostics.push_back({Severity::Warning, line, 0, "PASM reads 64 columns of a line: " + text});
        return text;
    }

    /// The label in column 0, a TAB, the statement; the comment after it as far as 64 columns take it
    std::vector<std::string> Layout(const std::string& label, const std::vector<std::string>& texts, bool bare, bool hasComment,
                                    const std::string& comment) override
    {
        std::vector<std::string> out;
        std::string name = label;
        // a label of a displaced block: its run address
        if (displaced && lineAddress && !name.empty() && (texts.empty() || texts[0].rfind("EQU ", 0) != 0))
        {
            out.push_back(Fitted(name + "\tEQU " + Number(*lineAddress)));
            name.clear();
        }
        bool first = true;
        for (const std::string& t : texts)
        {
            if (!t.empty() && t[0] == ';')
            {
                out.push_back(Fitted(CommentText(t).substr(0, kLineWidth - 1)));
                continue;
            }
            out.push_back(Fitted((first ? name : std::string()) + "\t" + t));
            first = false;
        }
        if (first && (!name.empty() || (bare && out.empty())))
            out.push_back(name);
        if (hasComment)
        {
            const std::string note = CommentText(comment);
            const size_t used = out.empty() ? 0 : Columns(out.back());
            if (!out.empty() && !out.back().empty() && out.back()[0] != ';' && used + 2 < kLineWidth)
                out.back() += " ;" + note.substr(0, kLineWidth - 2 - used);
            else if (!out.empty() && out.back().empty())
                out.back() = ";" + note.substr(0, kLineWidth - 1);
            else
                out.insert(out.empty() ? out.end() : out.end() - 1, ";" + note.substr(0, kLineWidth - 1));
        }
        return out;
    }
};
}  // namespace

BackendResult PasmBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    BackendResult result;
    result.document.dialect = "pasm";
    result.document.lineEnd = encoding::LineEnd::CrLf;   // its text file's line ends
    PasmWriter w{program, options, result};
    w.Run();
    return result;
}
}  // namespace unrealasm::dialects
