#include "dialects/prometheus/prometheusbackend.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>

#include "codecs/prometheus/prometheuscodec.h"
#include "dialects/common/chainwriter.h"
#include "dialects/common/z80.h"
#include "dialects/prometheus/prometheuscurrent.h"

namespace unrealasm::dialects
{
namespace
{
using codecs::PrometheusCodec;
using ir::Expr;
using ir::Op;
using ir::Operand;
using ir::Statement;

// PROMETHEUS' pseudo-instructions, registers and conditions beside the mnemonics (its instruction table)
const std::set<std::string> kWords = {
    "ORG", "PUT", "EQU", "ENT", "DEFB", "DEFM", "DEFS", "DEFW", "SLIA", "A", "B", "C", "D", "E", "H", "L", "I", "R", "HX", "LX",
    "HY", "LY", "AF", "BC", "DE", "HL", "SP", "IX", "IY", "NZ", "Z", "NC", "PO", "PE", "P", "M",
};

/// The longest DS written as DEFB zeros (PROMETHEUS' DEFS leaves the bytes as they were, sjasmplus writes zeros)
constexpr int64_t kZeroedHole = 4096;   // as ZEUS: FlexNavigator lays files over each other and clears with DS #400

/// A comment for PROMETHEUS: ASCII only (other characters as "?")
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

class PrometheusWriter : public ChainWriter
{
public:
    PrometheusWriter(const ir::Program& program, const BackendOptions& options, BackendResult& result) : ChainWriter(program, options, result) {}

private:
    std::string Title() const override { return "PROMETHEUS"; }
    /// up to 8 characters (a ninth is a syntax error), stored in capitals (book ch. 23)
    size_t Significant() const override { return 8; }
    bool Reserved(const std::string& upper) const override { return kWords.count(upper) || z80::IsMnemonic(z80::Lower(upper)); }
    /// a name starts with a letter (an atom starting with "_" is no name for it)
    std::string Sanitized(const std::string& name) const override
    {
        std::string out = RetroWriter::Sanitized(name);
        const size_t letter = out.find_first_not_of('_');
        out.erase(0, letter == std::string::npos ? out.size() : letter);
        return out.empty() || !std::isalpha(static_cast<unsigned char>(out[0])) ? "L" + out : out;
    }
    std::string Db() const override { return "DEFB"; }
    std::string Dw() const override { return "DEFW"; }
    /// a record holds at most 63 bytes after its first two (a name takes two): short data lines
    size_t DataWidth() const override { return 36; }
    size_t ExpressionWidth() const override { return 40; }
    /// no comment after an instruction: Layout() puts it on a line of its own before
    std::string CommentAfter() const override { return "\n;"; }

    const char* OperatorSymbol(Op op) const override
    {
        switch (op)
        {
            case Op::Add: return "+";
            case Op::Sub: return "-";
            case Op::Mul: return "*";
            case Op::Div: return "/";
            case Op::Mod: return "?";
            default: return nullptr;
        }
    }

    std::string NumberText(const Expr& e) const override
    {
        // hex digits in capitals, as it stores them
        if (e.spelling == ir::NumberSpelling::Hex)
            return "#" + z80::HexDigits(static_cast<uint64_t>(e.value & 0xFFFF), std::clamp(e.digits, 1, 4));
        return ChainWriter::NumberText(e);
    }

    std::optional<std::string> Text(const Expr& e) override
    {
        // the low byte as the remainder by 256 (no AND)
        if (e.kind == Expr::Kind::Unary && e.op == Op::Low)
            if (const auto inner = ChainWriter::Text(e.args[0]))
                return *inner + "?256";
        return ChainWriter::Text(e);
    }

    std::string RegisterText(const std::string& r) const override { return r == "af'" ? "AF'" : RetroWriter::RegisterText(r); }

    /// (IX) as (IX+0): its table has only the (ix+d) forms (JP (IX) aside)
    std::string OperandText(const Operand& o) override
    {
        if (o.kind == Operand::Kind::Indirect && (o.text == "ix" || o.text == "iy"))
            return "(" + z80::Upper(o.text) + "+0)";
        return ChainWriter::OperandText(o);
    }

    /// A string: DEFM "text" lines for runs of three characters or more; the quote, control characters and bytes above
    /// #7F as DEFB numbers
    void PutString(const std::string& text, std::vector<std::string>& items, std::vector<std::string>& out, const std::function<void()>& flush) override
    {
        std::string run;
        auto end = [&]() {
            if (run.size() >= 3)
            {
                flush();
                for (size_t at = 0; at < run.size(); at += 48)
                    out.push_back("DEFM \"" + run.substr(at, 48) + "\"");
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
                end();
                items.push_back(std::to_string(u));
            }
            else
                run.push_back(c);
        }
        end();
    }

    bool shifted = false;   // the instruction being written has its $ shifted already

    std::optional<std::vector<std::string>> OwnInstruction(const Statement& s) override
    {
        // its $ is where the operand's value goes (prometheuscurrent.h): the source's $ as $-field
        if (!shifted && std::any_of(s.operands.begin(), s.operands.end(), [](const Operand& o) { return UsesCurrent(o.expr); }))
        {
            Statement moved = s;
            for (size_t n = 0; n < moved.operands.size(); ++n)
                if (const int64_t field = prometheus::OperandField(s, n))
                    moved.operands[n].expr = ShiftCurrent(moved.operands[n].expr, field);
            shifted = true;
            std::vector<std::string> out = InstructionText(moved);
            shifted = false;
            return out;
        }
        const std::string& m = s.mnemonic;
        const auto& ops = s.operands;
        auto reg = [&](size_t k, const char* name) { return k < ops.size() && ops[k].kind == Operand::Kind::Register && ops[k].text == name; };
        // its table has the index halves and SLIA, but not these two
        if (m == "inf" || (m == "in" && (reg(0, "f") || (ops.size() == 1 && ops[0].kind == Operand::Kind::Indirect))))
            return std::vector<std::string>{Bytes({"#ED", "#70"}, "IN F,(C)")};
        if (m == "out" && ops.size() == 2 && ops[0].kind == Operand::Kind::Indirect && ops[1].kind == Operand::Kind::Immediate)
            return std::vector<std::string>{Bytes({"#ED", "#71"}, "OUT (C),0")};
        if (m == "jp" && ops.size() == 1 && ops[0].kind == Operand::Kind::Indirect)
            return std::vector<std::string>{"JP (" + z80::Upper(ops[0].text) + ")"};
        if ((m == "sll" || m == "sli") && ops.size() == 1)
            return std::vector<std::string>{"SLIA " + Operands(ops)};
        return std::nullopt;
    }

    RepeatForm Repeat(const std::vector<ir::Line>&, size_t, size_t) const override { return RepeatForm::Unrolled; }

    std::string dispBack{};   // the ORG that ends the displacement in force

    std::vector<std::string> DirectiveText(const Statement& s, const std::string&) override
    {
        switch (s.directive)
        {
            case ir::DirectiveKind::Org:
                displaced = false;
                return {"ORG " + Known(s.args[0])};
            case ir::DirectiveKind::Disp:
            {
                // ORG sets both addresses, PUT takes the bytes back where they were going
                strict = true;
                const auto run = Value(s.args[0]);
                strict = false;
                displaced = true;
                if (run && here)
                {
                    dispBack = "ORG $-" + Number(*run) + "+" + Number(*here);
                    return {"ORG " + Number(*run), "PUT " + Number(*here)};
                }
                // known only when PROMETHEUS assembles: the place and the run address under names of their own
                const std::string place = Give("ZPLACE"), at = Give("ZRUN");
                dispBack = "ORG $-" + at + "+" + place;
                return {place + "\tEQU $", at + "\tEQU " + Known(s.args[0]), "ORG " + at, "PUT " + place};
            }
            case ir::DirectiveKind::Ent:
                if (s.text == "if-displaced" && !displaced)
                    return {};
                displaced = false;
                // both addresses back where the bytes are: the place plus what the displaced code took
                return {dispBack};
            case ir::DirectiveKind::Ds:
            {
                const auto fill = s.args.size() > 1 ? Value(s.args[1]) : (s.operands.empty() ? std::optional<int64_t>(0) : std::nullopt);
                const auto n = Value(s.args[0]);
                if (fill && *fill == 0 && s.operands.empty() && (!n || *n > kZeroedHole))
                {
                    diagnostics.push_back({Severity::Warning, line, 0, "DS written as DEFS: PROMETHEUS leaves those bytes as they were (sjasmplus writes zeros)"});
                    return {"DEFS " + Known(s.args[0])};
                }
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
                return DataLines(Db(), items, DataWidth());
            }
            case ir::DirectiveKind::Include: return {NotConverted("INCLUDE (PROMETHEUS has none): " + s.text)};
            case ir::DirectiveKind::Incbin: return IncbinAsData(s, "INCBIN (PROMETHEUS has none): " + s.text);
            case ir::DirectiveKind::End:
                diagnostics.push_back({Severity::Info, line, 0, "END dropped (PROMETHEUS assembles the whole text)"});
                return {";END"};
            case ir::DirectiveKind::SaveBinary:
                diagnostics.push_back({Severity::Info, line, 0, "SAVEBIN dropped: save the bytes from PROMETHEUS"});
                return {";SAVEBIN \"" + s.text + "\""};
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }

    std::vector<PrometheusCodec::Symbol> symbols{};   // the names stored so far, in PROMETHEUS' order

    /// A line as PROMETHEUS shows it once stored (label 9 columns, mnemonic 5, lower case); as written, with a warning,
    /// when it cannot store it
    std::string Stored(const std::string& text)
    {
        std::vector<uint8_t> record;
        std::string error, shown;
        size_t length = 0;
        if (PrometheusCodec::EncodeLine(text, symbols, record, error) && PrometheusCodec::DecodeRecord(record, 0, symbols, shown, length))
            return shown;
        diagnostics.push_back({Severity::Warning, line, 0, "PROMETHEUS cannot store this line (" + (error.empty() ? std::string("its record") : error) + "): " + text});
        return text;
    }

    /// A comment on a line of its own (";" in column 0), its record's 63 bytes at most
    void Comment(const std::string& text, std::vector<std::string>& out)
    {
        out.push_back(Stored((";" + CommentText(text)).substr(0, 63)));
    }

    /// The label in column 0, one statement a line; comments on lines of their own before
    std::vector<std::string> Layout(const std::string& label, const std::vector<std::string>& texts, bool bare, bool hasComment,
                                    const std::string& comment) override
    {
        std::vector<std::string> out;
        if (hasComment)
            Comment(comment, out);
        bool first = true;
        for (const std::string& written : texts)
        {
            std::string t = written;
            if (const size_t note = t.find("\n;"); note != std::string::npos)
            {
                Comment(t.substr(note + 2), out);   // the instruction a DEFB stands for
                t.resize(note);
                while (!t.empty() && t.back() == ' ')
                    t.pop_back();
            }
            if (!t.empty() && t[0] == ';')
            {
                Comment(t.substr(1), out);
                continue;
            }
            const size_t tab = t.find('\t');
            if (tab != std::string::npos && t.find(' ') > tab)
            {
                // "NAME\tEQU ...": a line with a name of the writer's own
                if (first && !label.empty())
                    out.push_back(Stored(label));
                out.push_back(Stored(t.substr(0, tab) + " " + t.substr(tab + 1)));
                first = false;
                continue;
            }
            // a label before ORG / PUT on a line of its own: it takes the address before them
            if (first && !label.empty() && (t.rfind("ORG ", 0) == 0 || t.rfind("PUT ", 0) == 0))
            {
                out.push_back(Stored(label));
                first = false;
            }
            out.push_back(Stored((first ? label : std::string()) + " " + t));
            first = false;
        }
        if (first && !label.empty())
            out.push_back(Stored(label));
        else if (first && bare && !hasComment)
            out.push_back(std::string());
        return out;
    }
};
}  // namespace

BackendResult PrometheusBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    BackendResult result;
    result.document.dialect = "prometheus";
    PrometheusWriter w{program, options, result};
    w.Run();
    return result;
}
}  // namespace unrealasm::dialects
