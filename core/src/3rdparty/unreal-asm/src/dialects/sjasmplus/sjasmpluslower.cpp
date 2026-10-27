#include "dialects/sjasmplus/sjasmpluslower.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>

#include "dialects/common/z80.h"
#include "dialects/sjasmplus/sjasmplusfrontend.h"

namespace unrealasm::dialects
{
namespace
{
using ir::DirectiveKind;
using ir::Expr;
using ir::Op;
using ir::Operand;
using ir::Statement;

std::string Trim(const std::string& text)
{
    const size_t a = text.find_first_not_of(" \t");
    if (a == std::string::npos)
        return {};
    const size_t b = text.find_last_not_of(" \t");
    return text.substr(a, b - a + 1);
}

bool IsOther(const Statement& s)
{
    return s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::Other;
}

/// The directive word of a statement kept as text (upper case, without a leading dot) and the rest
std::pair<std::string, std::string> WordRest(const Statement& s)
{
    const std::string text = Trim(s.text);
    const size_t blank = text.find_first_of(" \t");
    std::string word = z80::Upper(text.substr(0, blank));
    if (word.size() > 1 && word[0] == '.')
        word.erase(0, 1);
    return {word, blank == std::string::npos ? std::string() : Trim(text.substr(blank))};
}

bool IsTemporary(const std::string& label)
{
    return !label.empty() && label.find_first_not_of("0123456789") == std::string::npos;
}

Expr Grouped(Expr e)
{
    Expr g = Expr::Make(Expr::Kind::Group);
    g.args.push_back(std::move(e));
    return g;
}

/// A directive of the frontend's (DB, DW ...) with these items
Statement Parsed(const std::string& keyword, const std::string& items)
{
    const SjasmplusFrontend frontend;
    const FrontendResult r = frontend.Parse(SourceDocument::FromText("        " + keyword + " " + items, "sjasmplus"));
    if (!r.program.lines.empty() && !r.program.lines[0].statements.empty())
        return r.program.lines[0].statements[0];
    Statement raw;
    raw.kind = Statement::Kind::Raw;
    raw.text = keyword + " " + items;
    return raw;
}

Statement Data(DirectiveKind kind, std::vector<Expr> items)
{
    Statement s;
    s.kind = Statement::Kind::Directive;
    s.directive = kind;
    for (Expr& e : items)
    {
        Operand o;
        o.kind = Operand::Kind::Immediate;
        o.expr = std::move(e);
        s.operands.push_back(std::move(o));
    }
    return s;
}

/// (e >> shift) & mask
Expr Part(const Expr& e, int shift, int64_t mask)
{
    // a number: its part (a target of 16-bit numbers reads no #445566)
    if (e.kind == Expr::Kind::Number)
        return Expr::Number((e.value >> shift) & mask, ir::NumberSpelling::Hex, mask > 0xFF ? 4 : 2);
    Expr value = Grouped(e);
    if (shift)
        value = Grouped(Expr::Binary(Op::Shr, value, Expr::Number(shift)));
    return Expr::Binary(Op::And, value, Expr::Number(mask, ir::NumberSpelling::Hex, mask > 0xFF ? 4 : 2));
}

std::vector<Expr> D24Bytes(const Expr& e)
{
    return {Part(e, 0, 0xFF), Part(e, 8, 0xFF), Part(e, 16, 0xFF)};
}

std::vector<Expr> DwordWords(const Expr& e)
{
    return {Part(e, 0, 0xFFFF), Part(e, 16, 0xFFFF)};
}

/// The data words with no IR kind as DB / DW (nullopt: not one of them)
std::optional<std::vector<Statement>> LowerData(const std::string& word, const std::string& rest)
{
    if (const auto bits = SjasmplusBitBytes(word, rest))
    {
        // DG / DH: their bytes
        std::vector<Expr> items;
        for (const uint8_t b : *bits)
            items.push_back(Expr::Number(b, ir::NumberSpelling::Hex, 2));
        return std::vector<Statement>{Data(DirectiveKind::Db, items)};
    }
    if (word == "DZ" || word == "DC")
    {
        Statement s = Parsed("DB", rest);
        if (word == "DZ")
            s.operands.push_back(Data(DirectiveKind::Db, {Expr::Number(0)}).operands[0]);
        else if (!s.operands.empty())
        {
            // the last character with bit 7 set
            Operand& last = s.operands.back();
            if (last.kind == Operand::Kind::String && !last.text.empty())
            {
                // the last character as a number of its own (with bit 7 it is no character of a text)
                const int code = static_cast<unsigned char>(last.text.back()) | 0x80;
                last.text.pop_back();
                if (last.text.empty())
                    s.operands.pop_back();
                s.operands.push_back(Data(DirectiveKind::Db, {Expr::Number(code, ir::NumberSpelling::Hex, 2)}).operands[0]);
            }
            else if (last.kind == Operand::Kind::Immediate)
                last.expr = Expr::Binary(Op::Or, Grouped(last.expr), Expr::Number(0x80, ir::NumberSpelling::Hex, 2));
        }
        return std::vector<Statement>{s};
    }
    if (word == "ABYTE" || word == "ABYTEC" || word == "ABYTEZ")
    {
        // ABYTE offset items: each byte plus the offset
        const auto [offsetText, itemsText] = SjasmplusAbyteParts(rest);
        const Statement offsetHolder = Parsed("DW", offsetText);
        if (offsetHolder.operands.empty())
            return std::nullopt;
        const Expr offset = offsetHolder.operands[0].expr;
        const Statement items = Parsed("DB", itemsText);
        std::vector<Expr> bytes;
        for (const Operand& o : items.operands)
        {
            if (o.kind == Operand::Kind::String)
                for (const char c : o.text)
                    bytes.push_back(Expr::Binary(Op::Add, Expr::Number(static_cast<unsigned char>(c)), Grouped(offset)));
            else
                bytes.push_back(Expr::Binary(Op::Add, Grouped(o.expr), Grouped(offset)));
        }
        if (word == "ABYTEC" && !bytes.empty())
            bytes.back() = Expr::Binary(Op::Or, Grouped(bytes.back()), Expr::Number(0x80, ir::NumberSpelling::Hex, 2));
        if (word == "ABYTEZ")
            bytes.push_back(Expr::Number(0));
        return std::vector<Statement>{Data(DirectiveKind::Db, std::move(bytes))};
    }
    if (word == "D24" || word == "DD" || word == "DWORD")
    {
        const Statement items = Parsed("DW", rest);
        std::vector<Expr> out;
        for (const Operand& o : items.operands)
            for (Expr& e : word == "D24" ? D24Bytes(o.expr) : DwordWords(o.expr))
                out.push_back(std::move(e));
        return std::vector<Statement>{Data(word == "D24" ? DirectiveKind::Db : DirectiveKind::Dw, std::move(out))};
    }
    return std::nullopt;
}

/// A constant (a field's count): numbers and the names already known
std::optional<int64_t> Constant(const Expr& e, const std::map<std::string, int64_t>& known)
{
    switch (e.kind)
    {
        case Expr::Kind::Number: return e.value;
        case Expr::Kind::Group: return Constant(e.args[0], known);
        case Expr::Kind::Symbol:
        {
            const auto found = known.find(e.text[0] == '@' ? e.text.substr(1) : e.text);
            return found == known.end() ? std::nullopt : std::optional<int64_t>(found->second);
        }
        case Expr::Kind::Unary:
        {
            const auto a = Constant(e.args[0], known);
            if (!a)
                return std::nullopt;
            return e.op == Op::Negate ? -*a : e.op == Op::Plus ? *a : std::optional<int64_t>();
        }
        case Expr::Kind::Binary:
        {
            const auto a = Constant(e.args[0], known), b = Constant(e.args[1], known);
            if (!a || !b)
                return std::nullopt;
            switch (e.op)
            {
                case Op::Add: return *a + *b;
                case Op::Sub: return *a - *b;
                case Op::Mul: return *a * *b;
                case Op::Div: return *b ? std::optional<int64_t>(*a / *b) : std::nullopt;
                default: return std::nullopt;
            }
        }
        default: return std::nullopt;
    }
}

/// One piece of a structure's data: a field's statement at its offset
struct Leaf
{
    int64_t offset = 0;
    int64_t size = 0;
    Statement data;
    int init = 0;   // the value an instance may give it: 0 none (BLOCK), 1 a byte, 2 a word, 3 D24, 4 DWORD, 5 TEXT
};

struct StructDef
{
    int64_t size = 0;
    std::vector<Leaf> leaves;
    std::vector<std::pair<std::string, int64_t>> names;   // fields and their fields ("p.x") with their offsets
};

class Lowering
{
public:
    Lowering(const ir::Program& program, Diagnostics& diagnostics) : in(program), diagnostics(diagnostics) {}

    ir::Program Run()
    {
        Collect();
        ir::Program out = in;
        out.lines.clear();
        Reset();
        for (const ir::Line& l : in.lines)
            Write(l, out.lines);
        return out;
    }

private:
    const ir::Program& in;
    Diagnostics& diagnostics;
    std::set<std::string> defined;          // every full name the source defines
    std::set<std::string> macroNames;
    std::map<std::string, StructDef> structs;
    std::map<std::string, int64_t> constants;   // the structures' names and fields: their values (counts of fields)
    std::set<std::string> referenced;           // every name an expression or a macro argument uses (IFUSED)
    std::map<std::string, std::pair<size_t, size_t>> macroLines;   // each macro: its MACRO and ENDM lines
    std::map<std::string, std::string> moduleMacros;                // "macro@module." -> its copy for that module

    // the walk
    std::vector<std::string> modules;
    std::string parent;
    int inMacro = 0;
    bool inLua = false;
    std::optional<std::pair<std::string, StructDef>> open;   // the structure being defined
    int64_t position = 0;
    bool collecting = false;   // the first walk: the names (its diagnostics come with the second)
    // The displacement as sjasmplus has it (a DISP inside a DISP block is ignored): followed through IF blocks, not
    // known after one whose branches leave it otherwise
    struct Branches
    {
        std::optional<bool> before;                // as the block opened
        std::vector<std::optional<bool>> ends;     // as each branch ended
        bool hasElse = false;
    };
    std::optional<bool> displaced = false;
    std::vector<Branches> branches;

    void Reset()
    {
        modules.clear();
        parent.clear();
        inMacro = 0;
        inLua = false;
        open.reset();
        position = 0;
        displaced = false;
        branches.clear();
    }

    /// The displacement followed through a statement outside macros; false: sjasmplus ignores it (a DISP inside a
    /// DISP block)
    bool Displacement(const Statement& s, uint32_t sourceLine)
    {
        const std::string word = IsOther(s) ? WordRest(s).first : std::string();
        if ((s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::If) || word == "IFDEF" || word == "IFNDEF" ||
            word == "IFUSED" || word == "IFNUSED")
            branches.push_back({displaced, {}, false});
        else if (!branches.empty() && ((s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::Else) || word == "ELSEIF"))
        {
            branches.back().ends.push_back(displaced);
            branches.back().hasElse = branches.back().hasElse || word.empty();
            displaced = branches.back().before;
        }
        else if (!branches.empty() && s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::EndIf)
        {
            Branches b = std::move(branches.back());
            branches.pop_back();
            b.ends.push_back(displaced);
            if (!b.hasElse)
                b.ends.push_back(b.before);
            displaced = std::all_of(b.ends.begin(), b.ends.end(), [&](const std::optional<bool>& e) { return e == b.ends[0]; })
                            ? b.ends[0]
                            : std::nullopt;
        }
        else if (s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::Disp)
        {
            if (displaced == true)
            {
                diagnostics.push_back({Severity::Info, sourceLine, 0, "DISP inside a DISP block left out (sjasmplus ignores it)"});
                return false;
            }
            displaced = true;
        }
        else if (s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::Ent)
            displaced = false;
        return true;
    }

    std::string Prefix() const
    {
        std::string out;
        for (const std::string& m : modules)
            out += m + ".";
        return out;
    }

    /// The full name a label defines
    std::string FullOf(const std::string& label) const
    {
        if (label[0] == '!')
            return FullOf(label.substr(1));   // !label: a label that leaves the parent of .labels as it is
        if (label[0] == '@')
            return label.substr(1);
        if (label[0] == '.')
            return parent + label;
        return Prefix() + label;
    }

    /// The full name a name used here means: the most specific module's that is defined
    std::optional<std::string> Resolved(const std::string& name) const
    {
        if (name.empty() || name[0] == '@' || name[0] == '.')
            return std::nullopt;
        for (size_t k = modules.size(); k > 0; --k)
        {
            std::string candidate;
            for (size_t m = 0; m < k; ++m)
                candidate += modules[m] + ".";
            candidate += name;
            if (defined.count(candidate))
                return candidate;
        }
        return std::nullopt;
    }

    const StructDef* StructNamed(const std::string& name) const
    {
        if (macroNames.count(name))
            return nullptr;
        if (const auto full = Resolved(name))
            if (const auto found = structs.find(*full); found != structs.end())
                return &found->second;
        const auto found = structs.find(name[0] == '@' ? name.substr(1) : name);
        return found == structs.end() ? nullptr : &found->second;
    }

    /// A field's statement as leaves of the structure being defined
    /// The bytes of a TEXT field's {DB items} (a string a byte per character)
    static std::vector<Expr> TextBytes(const std::string& braced)
    {
        std::string inner = Trim(braced);
        if (!inner.empty() && inner.front() == '{')
            inner.erase(0, 1);
        if (!inner.empty() && inner.back() == '}')
            inner.pop_back();
        std::vector<Expr> out;
        if (Trim(inner).empty())
            return out;
        for (const Operand& o : Parsed("DB", inner).operands)
        {
            if (o.kind == Operand::Kind::String)
                for (const char c : o.text)
                    out.push_back(Expr::Number(static_cast<unsigned char>(c)));
            else
                out.push_back(o.expr);
        }
        return out;
    }

    /// A leaf's data with the value an instance (or a nested structure's field) gives it ("" keeps its own)
    Statement Given(const Leaf& leaf, const std::string& value) const
    {
        Statement d = leaf.data;
        const Statement parsed = Parsed("DW", value);
        if (value.empty() || parsed.operands.empty())
            return d;
        if (leaf.init == 5)
        {
            // TEXT: the bytes given overwrite the default ones, the rest stays
            const std::vector<Expr> bytes = TextBytes(value);
            for (size_t k = 0; k < bytes.size() && k < d.operands.size(); ++k)
                d.operands[k].expr = bytes[k];
            return d;
        }
        const Expr& e = parsed.operands[0].expr;
        if (leaf.init == 1 || leaf.init == 2)
            d.operands[0].expr = e;
        else
            d = Data(leaf.init == 3 ? DirectiveKind::Db : DirectiveKind::Dw, leaf.init == 3 ? D24Bytes(e) : DwordWords(e));
        return d;
    }

    void Field(const ir::Line& l, const Statement& s, bool named)
    {
        StructDef& def = open->second;
        Diagnostics ignored;
        Diagnostics& report = collecting ? ignored : diagnostics;
        auto add = [&](Statement data, int64_t size, int init) {
            def.leaves.push_back({position, size, std::move(data), init});
            position += size;
        };
        if (s.kind == Statement::Kind::MacroCall)
        {
            if (const StructDef* nested = StructNamed(s.mnemonic))
            {
                const StructDef copy = *nested;
                for (const auto& [name, offset] : copy.names)
                    if (named)
                        def.names.push_back({l.label + "." + name, position + offset});
                // st1B st1 {high x, ...}: the field's own values for the nested structure's leaves
                const std::vector<std::string> values = Values(s.params, copy);
                size_t next = 0;
                for (Leaf leaf : copy.leaves)
                {
                    leaf.offset += position;
                    if (leaf.init && next < values.size())
                        leaf.data = Given(leaf, values[next++]);
                    def.leaves.push_back(std::move(leaf));
                }
                position += copy.size;
                return;
            }
            if (z80::Upper(s.mnemonic) == "TEXT" && !s.params.empty())
            {
                // TEXT length[,{bytes}]: the last byte given fills the rest (zeros without any)
                const Statement length = Parsed("DW", s.params[0]);
                const auto size = length.operands.size() == 1 ? Constant(length.operands[0].expr, constants) : std::nullopt;
                if (!size || *size < 0)
                {
                    report.push_back({Severity::Warning, l.sourceLine, 0, "a TEXT field of STRUCT " + open->first + " with a length known only when assembling"});
                    return;
                }
                std::string given;
                for (size_t k = 1; k < s.params.size(); ++k)
                    given += (k > 1 ? "," : "") + s.params[k];
                std::vector<Expr> bytes = TextBytes(given);
                const Expr fill = bytes.empty() ? Expr::Number(0) : bytes.back();
                bytes.resize(static_cast<size_t>(*size), fill);
                add(Data(DirectiveKind::Db, std::move(bytes)), *size, 5);
                return;
            }
            report.push_back({Severity::Warning, l.sourceLine, 0, "a field of STRUCT " + open->first + " not understood: " + s.mnemonic});
            return;
        }
        if (s.kind != Statement::Kind::Directive)
        {
            report.push_back({Severity::Warning, l.sourceLine, 0, "an instruction in STRUCT " + open->first});
            return;
        }
        switch (s.directive)
        {
            case DirectiveKind::Db:
            case DirectiveKind::Dw:
                if (s.operands.empty())
                {
                    // BYTE / WORD without a value: one of them, 0 unless an instance gives one
                    Statement zero = s;
                    Operand o;
                    o.kind = Operand::Kind::Immediate;
                    o.expr = Expr::Number(0);
                    zero.operands.push_back(std::move(o));
                    Field(l, zero, named);
                    return;
                }
                break;
            default: break;
        }
        switch (s.directive)
        {
            case DirectiveKind::Db:
            {
                int64_t size = 0;
                for (const Operand& o : s.operands)
                    size += o.kind == Operand::Kind::String ? static_cast<int64_t>(o.text.size()) : 1;
                add(s, size, s.operands.size() == 1 && s.operands[0].kind != Operand::Kind::String ? 1 : 0);
                return;
            }
            case DirectiveKind::Dw: add(s, 2 * static_cast<int64_t>(s.operands.size()), s.operands.size() == 1 ? 2 : 0); return;
            case DirectiveKind::Ds:
            {
                const auto count = s.args.empty() ? std::optional<int64_t>(0) : Constant(s.args[0], constants);
                if (!count)
                {
                    report.push_back({Severity::Warning, l.sourceLine, 0, "a field of STRUCT " + open->first + " with a size not known here"});
                    return;
                }
                add(s, *count, 0);
                return;
            }
            case DirectiveKind::Other:
            {
                const auto [word, rest] = WordRest(s);
                if (word == "ALIGN")
                {
                    const auto boundary = rest.empty() ? std::optional<int64_t>(4) : Constant(Parsed("DW", rest).operands.empty() ? Expr::Number(4) : Parsed("DW", rest).operands[0].expr, constants);
                    if (boundary && *boundary > 0)
                    {
                        const int64_t gap = (*boundary - position % *boundary) % *boundary;
                        Statement fill;
                        fill.kind = Statement::Kind::Directive;
                        fill.directive = DirectiveKind::Ds;
                        fill.args.push_back(Expr::Number(gap));
                        if (gap)
                            add(fill, gap, 0);
                    }
                    return;
                }
                if (const auto data = LowerData(word, rest))
                {
                    const Statement items = Parsed("DW", rest);
                    const bool one = items.operands.size() == 1;
                    for (const Statement& d : *data)
                    {
                        int64_t size = 0;
                        for (const Operand& o : d.operands)
                            size += d.directive == DirectiveKind::Dw ? 2 : o.kind == Operand::Kind::String ? static_cast<int64_t>(o.text.size()) : 1;
                        add(d, size, one && (word == "D24" || word == "DD" || word == "DWORD") ? (word == "D24" ? 3 : 4) : 0);
                    }
                    return;
                }
                break;
            }
            default: break;
        }
        report.push_back({Severity::Warning, l.sourceLine, 0, "a field of STRUCT " + open->first + " not understood: " + s.text});
    }

    void Reference(const Expr& e)
    {
        if (e.kind == Expr::Kind::Symbol)
            referenced.insert(e.text[0] == '@' ? e.text.substr(1) : e.text);
        for (const Expr& a : e.args)
            Reference(a);
    }

    /// The names a text uses (a macro's argument, a directive kept as text)
    void ReferenceText(const std::string& text)
    {
        for (size_t k = 0; k < text.size();)
        {
            if (std::isalpha(static_cast<unsigned char>(text[k])) || text[k] == '_' || text[k] == '@' || text[k] == '.')
            {
                size_t end = k;
                while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_' || text[end] == '.' ||
                                             text[end] == '@' || text[end] == '?' || text[end] == '!'))
                    ++end;
                const std::string name = text.substr(k, end - k);
                referenced.insert(name[0] == '@' ? name.substr(1) : name);
                k = end;
            }
            else
                ++k;
        }
    }

    /// IFUSED name: a name some expression of the source uses (sjasmplus asks every pass: decided here)
    bool Used(const std::string& name) const
    {
        const std::string plain = name[0] == '@' ? name.substr(1) : name;
        if (referenced.count(plain))
            return true;
        // a name used inside its module (helper) asked by its full name (main.helper), or the other way
        const auto endsWith = [](const std::string& longer, const std::string& shorter) {
            return longer.size() > shorter.size() && longer.compare(longer.size() - shorter.size(), shorter.size(), shorter) == 0 &&
                   longer[longer.size() - shorter.size() - 1] == '.';
        };
        return std::any_of(referenced.begin(), referenced.end(), [&](const std::string& r) { return endsWith(r, plain) || endsWith(plain, r); });
    }

    void Collect()
    {
        Reset();
        collecting = true;
        for (size_t k = 0; k < in.lines.size(); ++k)
            for (const Statement& s : in.lines[k].statements)
                if (s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::Macro)
                {
                    int depth = 0;
                    size_t end = k;
                    for (size_t m = k; m < in.lines.size(); ++m)
                    {
                        for (const Statement& t : in.lines[m].statements)
                            if (t.kind == Statement::Kind::Directive)
                                depth += t.directive == DirectiveKind::Macro ? 1 : t.directive == DirectiveKind::EndMacro ? -1 : 0;
                        if (depth == 0)
                        {
                            end = m;
                            break;
                        }
                    }
                    macroLines[s.text] = {k, end};
                }
        for (const ir::Line& l : in.lines)
            for (const Statement& s : l.statements)
            {
                for (const Operand& o : s.operands)
                    Reference(o.expr);
                for (const Expr& a : s.args)
                    Reference(a);
                for (const std::string& p : s.params)
                    ReferenceText(p);
                if (IsOther(s))
                {
                    const auto [word, rest] = WordRest(s);
                    if (word != "IFUSED" && word != "IFNUSED")
                        ReferenceText(rest);
                }
            }
        for (const ir::Line& l : in.lines)
        {
            if (Skipped(l, nullptr))
                continue;
            if (open)
            {
                if (!l.label.empty())
                    open->second.names.push_back({l.label, position});
                for (const Statement& s : l.statements)
                    Field(l, s, !l.label.empty());
                continue;
            }
            if (!l.label.empty() && !IsTemporary(l.label))
            {
                const std::string full = FullOf(l.label);
                defined.insert(full);
                if (l.label[0] != '.' && l.label[0] != '!')
                    parent = full;
                for (const Statement& s : l.statements)
                    if (s.kind == Statement::Kind::MacroCall)
                        if (const StructDef* def = StructNamed(s.mnemonic))
                            for (const auto& [name, offset] : def->names)
                                defined.insert(full + "." + name);
            }
        }
        collecting = false;
    }

    /// The words that open and close blocks (MODULE, STRUCT, LUA, MACRO): true when the line is one of them or inside
    /// a LUA block (out: what it writes instead)
    bool Skipped(const ir::Line& l, std::vector<ir::Line>* out)
    {
        auto note = [&](const std::string& text) {
            if (!out)
                return;
            ir::Line c;
            c.sourceLine = l.sourceLine;
            c.comment = text + (l.hasComment ? " ;" + l.comment : std::string());
            c.hasComment = true;
            out->push_back(std::move(c));
        };
        for (const Statement& s : l.statements)
        {
            if (inLua)
            {
                if (IsOther(s) && WordRest(s).first == "ENDLUA")
                {
                    inLua = false;
                    note("ENDLUA");
                }
                return true;
            }
            // (a macro may open and close on one line: NAME MACRO p : ... : ENDM)
            if (s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::Macro)
            {
                ++inMacro;
                macroNames.insert(s.text);
                continue;
            }
            if (s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::EndMacro)
            {
                inMacro = std::max(0, inMacro - 1);
                continue;
            }
            if (!IsOther(s) || inMacro)
                continue;
            const auto [word, rest] = WordRest(s);
            if (word == "LUA")
            {
                inLua = true;
                if (out)
                    diagnostics.push_back({Severity::Warning, l.sourceLine, 0, "a LUA block is left out (sjasmplus runs its script: what it makes is missing)"});
                note("LUA (left out)");
                return true;
            }
            if (word == "MODULE" || word == "ENDMODULE")
            {
                if (word == "MODULE")
                    modules.push_back(rest);
                else if (!modules.empty())
                    modules.pop_back();
                parent.clear();
                if (out)
                {
                    // a mark the writer keeps the module by: a macro expanded inside it finds the module's names
                    ir::Line mark;
                    mark.sourceLine = l.sourceLine;
                    Statement s2;
                    s2.kind = Statement::Kind::Directive;
                    s2.directive = DirectiveKind::Other;
                    s2.text = word == "MODULE" ? kModuleMark + " " + rest : kEndModuleMark;
                    mark.statements.push_back(std::move(s2));
                    mark.comment = (word == "MODULE" ? "MODULE " + rest : std::string("ENDMODULE")) + (l.hasComment ? " ;" + l.comment : std::string());
                    mark.hasComment = true;
                    out->push_back(std::move(mark));
                }
                return true;
            }
            if (word == "STRUCT")
            {
                const size_t comma = rest.find(',');
                const std::string name = Prefix() + Trim(rest.substr(0, comma));
                position = 0;
                if (comma != std::string::npos)
                {
                    const Statement offset = Parsed("DW", rest.substr(comma + 1));
                    position = offset.operands.empty() ? 0 : Constant(offset.operands[0].expr, constants).value_or(0);
                }
                open = std::make_pair(name, StructDef{});
                note("STRUCT " + rest);
                return true;
            }
            if (word == "ENDS" && open)
            {
                auto [name, def] = std::move(*open);
                open.reset();
                def.size = position;
                constants[name] = def.size;
                for (const auto& [field, offset] : def.names)
                    constants[name + "." + field] = offset;
                if (out)
                {
                    // the fields' offsets and the size under the structure's names
                    for (const auto& [field, offset] : def.names)
                        out->push_back(Equ(l.sourceLine, "@" + name + "." + field, Expr::Number(offset)));
                    out->push_back(Equ(l.sourceLine, "@" + name, Expr::Number(def.size)));
                }
                else
                {
                    defined.insert(name);
                    for (const auto& [field, offset] : def.names)
                        defined.insert(name + "." + field);
                }
                structs[name] = std::move(def);
                return true;
            }
        }
        return false;
    }

    /// `keepsParent`: sjasmplus' !label, the parent of the .labels after it stays (a structure's offsets and an
    /// instance's fields are no labels of the source)
    static ir::Line Equ(uint32_t sourceLine, const std::string& label, Expr value, bool keepsParent = true)
    {
        ir::Line line;
        line.sourceLine = sourceLine;
        line.label = keepsParent ? "!" + label : label;
        Statement s;
        s.kind = Statement::Kind::Directive;
        s.directive = DirectiveKind::Equ;
        s.args.push_back(std::move(value));
        line.statements.push_back(std::move(s));
        return line;
    }

    /// The .labels an EQU / DEFL / alias names, as sjasmplus reads them: from the parent before the line's own label
    void Localized(Expr& e, const std::string& at) const
    {
        if (e.kind == Expr::Kind::Symbol && e.text.size() > 1 && e.text[0] == '.' && !at.empty())
            e.text = "@" + at + e.text;
        for (Expr& a : e.args)
            Localized(a, at);
    }

    void Resolve(Expr& e) const
    {
        if (e.kind == Expr::Kind::Symbol)
            if (const auto full = Resolved(e.text))
                e.text = "@" + *full;
        for (Expr& a : e.args)
            Resolve(a);
    }

    /// A macro called inside a MODULE whose body uses the module's names: a copy for the module with those names full
    /// (sjasmplus expands a macro as text where it is called), written before its first call; its name
    std::string ModuleMacro(const std::string& name, std::vector<ir::Line>& out, uint32_t sourceLine)
    {
        const auto found = macroLines.find(name);
        if (found == macroLines.end())
            return name;
        const auto [begin, end] = found->second;
        bool uses = false;
        std::vector<ir::Line> body(in.lines.begin() + static_cast<std::ptrdiff_t>(begin + 1), in.lines.begin() + static_cast<std::ptrdiff_t>(end));
        for (ir::Line& b : body)
            for (Statement& s : b.statements)
            {
                const auto check = [&](Expr& e) {
                    const std::string before = Text(e);
                    Resolve(e);
                    uses = uses || Text(e) != before;
                };
                for (Operand& o : s.operands)
                    check(o.expr);
                for (Expr& a : s.args)
                    check(a);
                for (std::string& p : s.params)
                {
                    const std::string resolved = ResolveText(p);
                    uses = uses || resolved != p;
                    p = resolved;
                }
            }
        if (!uses)
            return name;
        const std::string key = name + "@" + Prefix();
        if (const auto made = moduleMacros.find(key); made != moduleMacros.end())
            return made->second;
        std::string copy = name + "_" + Prefix().substr(0, Prefix().size() - 1);
        std::replace(copy.begin(), copy.end(), '.', '_');
        while (macroNames.count(copy))
            copy += "_";
        macroNames.insert(copy);
        moduleMacros[key] = copy;
        ir::Line openLine = in.lines[begin];
        for (Statement& s : openLine.statements)
            if (s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::Macro)
                s.text = copy;
        openLine.label.clear();
        openLine.sourceLine = sourceLine;
        openLine.comment = name + " for MODULE " + Prefix().substr(0, Prefix().size() - 1);
        openLine.hasComment = true;
        out.push_back(std::move(openLine));
        for (ir::Line& b : body)
        {
            b.sourceLine = sourceLine;
            out.push_back(std::move(b));
        }
        ir::Line close = in.lines[end];
        close.sourceLine = sourceLine;
        out.push_back(std::move(close));
        return copy;
    }

    /// An expression's names, to tell whether resolving changed it
    static std::string Text(const Expr& e)
    {
        std::string out = e.text;
        for (const Expr& a : e.args)
            out += "|" + Text(a);
        return out;
    }

    /// A macro's argument as text: its names as the module's (sjasmplus expands it where it is called)
    std::string ResolveText(const std::string& text) const
    {
        std::string out;
        char quote = 0;
        for (size_t k = 0; k < text.size();)
        {
            const char c = text[k];
            if (quote)
            {
                out.push_back(c);
                quote = c == quote ? 0 : quote;
                ++k;
                continue;
            }
            if (c == '"' || c == '\'')
            {
                quote = c;
                out.push_back(c);
                ++k;
                continue;
            }
            const bool start = std::isalpha(static_cast<unsigned char>(c)) || c == '_';
            if (start && (k == 0 || !(std::isalnum(static_cast<unsigned char>(text[k - 1])) || text[k - 1] == '_' || text[k - 1] == '.' ||
                                      text[k - 1] == '@' || text[k - 1] == '$' || text[k - 1] == '#' || text[k - 1] == '%')))
            {
                size_t end = k;
                while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_' || text[end] == '.' ||
                                             text[end] == '?' || text[end] == '!'))
                    ++end;
                const std::string name = text.substr(k, end - k);
                const auto full = Resolved(name);
                out += full ? "@" + *full : name;
                k = end;
                continue;
            }
            out.push_back(c);
            ++k;
        }
        return out;
    }

    /// The values an instance gives, braces flattened ("{5,6},#5678": 5, 6, #5678; an empty one keeps the default)
    /// The values an instance gives the leaves of `def` (sjasmplusfrontend.h)
    static std::vector<std::string> Values(const std::vector<std::string>& params, const StructDef& def)
    {
        std::string text;
        for (size_t k = 0; k < params.size(); ++k)
            text += (k ? "," : "") + params[k];
        std::vector<bool> textLeaves;
        for (const Leaf& leaf : def.leaves)
            if (leaf.init)
                textLeaves.push_back(leaf.init == 5);
        return SjasmplusStructValues(text, textLeaves);
    }

    /// An instance: its data from the defaults and the values given, label.field as EQU
    void Instance(const ir::Line& l, const Statement& call, const StructDef& def, std::vector<ir::Line>& out)
    {
        if (!call.params.empty() && !call.params[0].empty() && call.params[0][0] == '=' && !l.label.empty())
        {
            // label STRUCT = address: an alias, its label and label.field as EQU from the address, no bytes
            std::string text = call.params[0].substr(1);
            for (size_t k = 1; k < call.params.size(); ++k)
                text += "," + call.params[k];
            const Statement parsed = Parsed("DW", text);
            Expr address = parsed.operands.empty() ? Expr::Number(0) : parsed.operands[0].expr;
            Localized(address, parent);
            const std::string full = FullOf(l.label);
            if (l.label[0] != '.' && l.label[0] != '!')
                parent = full;
            out.push_back(Equ(l.sourceLine, "@" + full, address, l.label[0] == '.' || l.label[0] == '!'));
            for (const auto& [field, offset] : def.names)
                out.push_back(Equ(l.sourceLine, "@" + full + "." + field, Expr::Binary(Op::Add, Expr::Symbol("@" + full), Expr::Number(offset))));
            return;
        }
        const std::vector<std::string> values = Values(call.params, def);
        size_t next = 0;
        std::vector<Statement> data;
        int64_t at = 0;
        auto gap = [&](int64_t to) {
            if (to <= at)
                return;
            Statement fill;
            fill.kind = Statement::Kind::Directive;
            fill.directive = DirectiveKind::Ds;
            fill.args.push_back(Expr::Number(to - at));
            data.push_back(std::move(fill));
            at = to;
        };
        for (const Leaf& leaf : def.leaves)
        {
            gap(leaf.offset);
            Statement d = leaf.data;
            if (leaf.init && next < values.size())
                d = Given(leaf, values[next++]);
            data.push_back(std::move(d));
            at = leaf.offset + leaf.size;
        }
        gap(def.size);
        if (next < values.size())
            diagnostics.push_back({Severity::Warning, l.sourceLine, 0, "more values than STRUCT " + call.mnemonic + " takes"});
        std::string full;
        std::string label = l.label;
        if (!label.empty())
        {
            full = FullOf(label);
            if (label[0] == '!')
                label = modules.empty() ? label : "!@" + full;
            else if (label[0] != '.')
            {
                parent = full;
                if (!modules.empty() && label[0] != '@')
                    label = "@" + full;
            }
        }
        for (size_t k = 0; k < std::max<size_t>(1, data.size()); ++k)
        {
            ir::Line line;
            line.sourceLine = l.sourceLine;
            if (k == 0)
            {
                line.label = label;
                line.comment = call.mnemonic + (l.hasComment ? " ;" + l.comment : std::string());
                line.hasComment = true;
            }
            if (k < data.size())
                line.statements.push_back(data[k]);
            out.push_back(std::move(line));
        }
        if (!full.empty())
            for (const auto& [field, offset] : def.names)
                out.push_back(Equ(l.sourceLine, "@" + full + "." + field, Expr::Binary(Op::Add, Expr::Symbol("@" + full), Expr::Number(offset))));
    }

    void Write(const ir::Line& l, std::vector<ir::Line>& out)
    {
        if (Skipped(l, &out))
            return;
        if (open)
        {
            // a field: its offset written at ENDS
            if (!l.label.empty())
                open->second.names.push_back({l.label, position});
            for (const Statement& s : l.statements)
                Field(l, s, !l.label.empty());
            return;
        }
        if (!inMacro)
            for (const Statement& s : l.statements)
                if (s.kind == Statement::Kind::MacroCall)
                    if (const StructDef* def = StructNamed(s.mnemonic); def && l.statements.size() == 1)
                    {
                        const StructDef copy = *def;
                        Instance(l, s, copy, out);
                        return;
                    }
        ir::Line line = l;
        std::vector<Statement> statements = l.statements;
        if (!line.label.empty() && line.label[0] != '.' && line.label[0] != '!' && !IsTemporary(line.label) && !inMacro)
            for (Statement& s : statements)
                if (s.kind == Statement::Kind::Directive && (s.directive == DirectiveKind::Equ || s.directive == DirectiveKind::Defl))
                    for (Expr& a : s.args)
                        Localized(a, parent);
        if (!line.label.empty() && !IsTemporary(line.label) && !inMacro)
        {
            const std::string full = FullOf(line.label);
            if (line.label[0] == '!')
                line.label = modules.empty() ? line.label : "!@" + full;
            else if (line.label[0] != '.')
            {
                parent = full;
                if (!modules.empty() && line.label[0] != '@')
                    line.label = "@" + full;
            }
        }
        line.statements.clear();
        for (const Statement& s : statements)
        {
            if (!inMacro && !Displacement(s, l.sourceLine))
            {
                line.comment = "DISP" + (line.hasComment ? " ;" + line.comment : std::string());
                line.hasComment = true;
                continue;
            }
            std::vector<Statement> replaced;
            if (IsOther(s))
            {
                const auto [word, rest] = WordRest(s);
                if (const auto data = LowerData(word, rest))
                    replaced = *data;
                else if (word == "ALIGN")
                {
                    // DS up to the boundary: (n - $ % n) % n
                    const size_t comma = rest.find(',');
                    const Statement boundary = Parsed("DW", rest.empty() ? std::string("4") : rest.substr(0, comma));
                    if (!boundary.operands.empty())
                    {
                        const Expr n = Grouped(boundary.operands[0].expr);
                        Statement fill;
                        fill.kind = Statement::Kind::Directive;
                        fill.directive = DirectiveKind::Ds;
                        fill.params = {"align"};
                        fill.args.push_back(Expr::Binary(Op::Mod, Grouped(Expr::Binary(Op::Sub, n, Grouped(Expr::Binary(Op::Mod, Expr::Make(Expr::Kind::Current), n)))), n));
                        if (comma != std::string::npos)
                            if (const Statement value = Parsed("DW", rest.substr(comma + 1)); !value.operands.empty())
                                fill.args.push_back(value.operands[0].expr);
                        replaced.push_back(std::move(fill));
                    }
                }
                else if (word == "IFUSED" || word == "IFNUSED")
                {
                    // decided here: a name the source uses (the label of the line when none is given)
                    const std::string name = rest.empty() ? l.label : Trim(rest.substr(0, rest.find_first_of(" \t;")));
                    Statement test;
                    test.kind = Statement::Kind::Directive;
                    test.directive = DirectiveKind::If;
                    test.args.push_back(Expr::Number((name.empty() ? false : Used(name)) == (word == "IFUSED") ? 1 : 0));
                    diagnostics.push_back({Severity::Info, l.sourceLine, 0, word + " " + name + " decided: " + (test.args[0].value ? "taken" : "left out")});
                    replaced.push_back(std::move(test));
                }
                else if (word == "SLOT" || word == "PAGE" || word == "MMU")
                {
                    diagnostics.push_back({Severity::Info, l.sourceLine, 0, word + " dropped (the target has one 64K memory)"});
                    line.comment = s.text + (line.hasComment ? " ;" + line.comment : std::string());
                    line.hasComment = true;
                    continue;
                }
                else if (word == "INCLUDELUA")
                {
                    diagnostics.push_back({Severity::Warning, l.sourceLine, 0, "INCLUDELUA left out (a Lua script)"});
                    continue;
                }
            }
            if (replaced.empty())
                replaced.push_back(s);
            for (Statement& r : replaced)
            {
                if (r.kind == Statement::Kind::MacroCall && !modules.empty() && !inMacro)
                    r.mnemonic = ModuleMacro(r.mnemonic, out, l.sourceLine);
                if (!modules.empty())
                {
                    for (Operand& o : r.operands)
                        Resolve(o.expr);
                    for (Expr& a : r.args)
                        Resolve(a);
                    if (r.kind == Statement::Kind::MacroCall)
                        for (std::string& p : r.params)
                            p = ResolveText(p);
                }
                line.statements.push_back(std::move(r));
            }
        }
        out.push_back(std::move(line));
    }
};
}  // namespace

namespace
{
/// A macro argument as an operand (a register, an indirection, an expression), as the frontend reads it after INC
ir::Operand Argument(const std::string& text)
{
    ir::Operand o;
    o.kind = ir::Operand::Kind::Immediate;
    o.expr = Expr::Make(Expr::Kind::Raw);
    o.expr.text = text;
    if (text.empty())
        return o;
    const FrontendResult parsed = SjasmplusFrontend().Parse(SourceDocument::FromText("        INC " + text, "sjasmplus"));
    if (parsed.program.lines.size() == 1 && parsed.program.lines[0].statements.size() == 1 &&
        parsed.program.lines[0].statements[0].kind == Statement::Kind::Instruction && parsed.program.lines[0].statements[0].operands.size() == 1)
        return parsed.program.lines[0].statements[0].operands[0];
    return o;
}

Expr Substituted(const Expr& e, const std::map<std::string, ir::Operand>& args)
{
    if (e.kind == Expr::Kind::Symbol)
        if (const auto a = args.find(e.text); a != args.end())
        {
            Expr g = Expr::Make(Expr::Kind::Group);
            g.args.push_back(a->second.expr);
            return g;
        }
    Expr out = e;
    for (Expr& a : out.args)
        a = Substituted(a, args);
    return out;
}

/// A word of a text with the arguments put in (a macro call inside the body passes them on)
std::string SubstitutedText(const std::string& text, const std::map<std::string, std::string>& args)
{
    std::string out;
    for (size_t i = 0; i < text.size();)
    {
        const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '?' || c == '.'; };
        if (word(text[i]) && (i == 0 || !word(text[i - 1])))
        {
            size_t j = i;
            while (j < text.size() && word(text[j]))
                ++j;
            const std::string w = text.substr(i, j - i);
            const auto a = args.find(w);
            out += a != args.end() ? a->second : w;
            i = j;
            continue;
        }
        out += text[i++];
    }
    return out;
}

/// ALASM's LOCAL block makes every label of a macro local, sjasmplus only its .labels (and temporary ones): a macro
/// with both kinds is put in at each of its calls instead, its .labels named for each expansion (`!` keeps the parent
/// of the .labels after it as it is), its global labels and assignments as they are. `all`: every macro so (a target
/// without macros: ALASM 3.8)
ir::Program ExpandMixedMacros(const ir::Program& program, Diagnostics& diagnostics, bool all)
{
    struct Def
    {
        size_t begin = 0, end = 0;
        std::vector<std::string> params;
    };
    std::map<std::string, Def> mixed;
    for (size_t k = 0; k < program.lines.size(); ++k)
        for (const Statement& s : program.lines[k].statements)
            if (s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::Macro)
            {
                size_t end = k + 1;
                for (int depth = 1; end < program.lines.size(); ++end)
                {
                    for (const Statement& b : program.lines[end].statements)
                        if (b.kind == Statement::Kind::Directive)
                            depth += (b.directive == DirectiveKind::Macro) - (b.directive == DirectiveKind::EndMacro);
                    if (depth == 0)
                        break;
                }
                bool local = false, global = false;
                for (size_t n = k + 1; n < end && n < program.lines.size(); ++n)
                {
                    const std::string& label = program.lines[n].label;
                    if (label.empty())
                        continue;
                    if (label[0] == '.' || IsTemporary(label))
                        local = true;
                    else
                        global = true;
                }
                if (((local && global) || all) && end < program.lines.size())
                    mixed[s.text] = {k, end, s.params};
            }
    if (mixed.empty())
        return program;
    ir::Program out = program;
    out.lines.clear();
    int expansions = 0;
    for (size_t k = 0; k < program.lines.size(); ++k)
    {
        const ir::Line& l = program.lines[k];
        // the definition itself: left out (every call is put in)
        bool definition = false;
        for (const Statement& s : l.statements)
            if (s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::Macro && mixed.count(s.text) && mixed.at(s.text).begin == k)
            {
                ir::Line note;
                note.sourceLine = l.sourceLine;
                note.comment = "MACRO " + s.text + (all ? " (put in at its calls)" : " (put in at its calls: local and global labels)");
                note.hasComment = true;
                out.lines.push_back(std::move(note));
                k = mixed.at(s.text).end;
                definition = true;
            }
        if (definition)
            continue;
        const Statement* call = l.statements.size() == 1 && l.statements[0].kind == Statement::Kind::MacroCall ? &l.statements[0] : nullptr;
        const auto def = call ? mixed.find(call->mnemonic) : mixed.end();
        if (def == mixed.end())
        {
            out.lines.push_back(l);
            continue;
        }
        if (!l.label.empty())
        {
            ir::Line alone = l;
            alone.statements.clear();
            out.lines.push_back(std::move(alone));
        }
        ++expansions;
        std::map<std::string, ir::Operand> args;
        std::map<std::string, ir::Operand> strings;   // the arguments that are strings: so in DB ("DB name" with "TEXT")
        std::map<std::string, std::string> texts;
        for (size_t p = 0; p < def->second.params.size(); ++p)
        {
            const std::string text = p < call->params.size() ? call->params[p] : std::string();
            args[def->second.params[p]] = Argument(text);
            texts[def->second.params[p]] = text;
            const FrontendResult parsed = SjasmplusFrontend().Parse(SourceDocument::FromText("        DB " + text, "sjasmplus"));
            if (!text.empty() && parsed.program.lines.size() == 1 && parsed.program.lines[0].statements.size() == 1 &&
                parsed.program.lines[0].statements[0].operands.size() == 1 && parsed.program.lines[0].statements[0].operands[0].kind == ir::Operand::Kind::String)
                strings[def->second.params[p]] = parsed.program.lines[0].statements[0].operands[0];
        }
        std::map<std::string, std::string> locals;   // .label -> its name in this expansion
        for (size_t n = def->second.begin + 1; n < def->second.end; ++n)
            if (const std::string& label = program.lines[n].label; !label.empty() && label[0] == '.')
                locals[label] = "__M" + std::to_string(expansions) + "_" + label.substr(1);
        const std::function<void(Expr&)> rename = [&](Expr& e) {
            if (e.kind == Expr::Kind::Symbol)
                if (const auto found = locals.find(e.text); found != locals.end())
                    e.text = found->second;
            for (Expr& a : e.args)
                rename(a);
        };
        for (size_t n = def->second.begin + 1; n < def->second.end; ++n)
        {
            ir::Line copy = program.lines[n];
            copy.sourceLine = l.sourceLine;
            if (const auto found = locals.find(copy.label); found != locals.end())
                copy.label = "!" + found->second;
            for (Statement& s : copy.statements)
            {
                const bool data = s.kind == Statement::Kind::Directive && (s.directive == DirectiveKind::Db || s.directive == DirectiveKind::Dw);
                for (ir::Operand& o : s.operands)
                {
                    if (data && o.kind == ir::Operand::Kind::Immediate && o.expr.kind == Expr::Kind::Symbol && strings.count(o.expr.text))
                        o = strings.at(o.expr.text);
                    else if (o.kind == ir::Operand::Kind::Immediate && o.expr.kind == Expr::Kind::Symbol && args.count(o.expr.text))
                        o = args.at(o.expr.text);   // a register, an indirection ... as the call gives it
                    else
                        o.expr = Substituted(o.expr, args);
                    rename(o.expr);
                }
                for (Expr& a : s.args)
                {
                    a = Substituted(a, args);
                    rename(a);
                }
                for (std::string& p : s.params)
                    p = SubstitutedText(p, texts);
                if (s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::Other)
                    s.text = SubstitutedText(s.text, texts);
            }
            out.lines.push_back(std::move(copy));
        }
    }
    diagnostics.push_back({Severity::Info, 0, 0, std::to_string(expansions) + (all ? " macro call(s)" : " call(s) of macros with local and global labels") +
                                                     " put in where they stand"});
    return out;
}
}  // namespace

ir::Program LowerSjasmplus(const ir::Program& program, Diagnostics& diagnostics, bool expandMixedMacros, bool expandAllMacros)
{
    if (expandMixedMacros || expandAllMacros)
    {
        const ir::Program expanded = ExpandMixedMacros(program, diagnostics, expandAllMacros);
        Lowering lowering(expanded, diagnostics);
        return lowering.Run();
    }
    Lowering lowering(program, diagnostics);
    return lowering.Run();
}
}  // namespace unrealasm::dialects
