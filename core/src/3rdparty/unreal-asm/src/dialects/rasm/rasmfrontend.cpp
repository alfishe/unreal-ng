#include "dialects/rasm/rasmfrontend.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>

#include "dialects/common/textexpr.h"
#include "dialects/common/textfrontend.h"
#include "dialects/common/z80.h"
#include "dialects/rasm/engine/rasmassembly.h"

namespace unrealasm::dialects
{
namespace
{
using ir::DirectiveKind;
using ir::Expr;
using ir::Line;
using ir::Op;
using ir::Operand;
using ir::Statement;
using namespace text;

// ---------------------------------------------------------------------------------------------------------------------
// line by line (a source the engine cannot assemble alone)

const TextDialect& Rasm()
{
    static const TextDialect dialect = [] {
        TextDialect d;
        d.id = "rasm";
        ExprRules& r = d.rules;
        r.binary = {
            {"||", Op::LogicalOr, 1}, {"&&", Op::LogicalAnd, 2}, {"|", Op::Or, 3},       {"or", Op::Or, 3},         {"^", Op::Xor, 4},
            {"xor", Op::Xor, 4},      {"&", Op::And, 5},         {"and", Op::And, 5},    {"==", Op::Equal, 6},      {"!=", Op::NotEqual, 6},
            {"<>", Op::NotEqual, 6},  {"<", Op::Less, 7},        {"<=", Op::LessEqual, 7}, {">", Op::Greater, 7},   {">=", Op::GreaterEqual, 7},
            {"<<", Op::Shl, 8},       {">>", Op::Shr, 8},        {"+", Op::Add, 9},      {"-", Op::Sub, 9},         {"*", Op::Mul, 10},
            {"/", Op::Div, 10},       {"%%", Op::Mod, 10},       {"mod", Op::Mod, 10},
        };
        r.unary = {{"-", Op::Negate, 11}, {"+", Op::Plus, 11}, {"~", Op::Not, 11}, {"!", Op::LogicalNot, 11}};
        r.dollarHex = r.hashHex = r.percentBinary = r.zeroB = true;
        r.atOctal = true;
        r.suffixOctalDecimal = false;
        r.dotInNames = r.atInNames = r.questionInNames = true;
        r.functions = {{"hi", Op::High}, {"lo", Op::Low}};
        d.labelStyle = LabelStyle::ColonOrColumnZero;
        d.statementSeparator = ':';
        d.macroParenParams = true;
        d.macroLocalLabels = true;
        d.expressionBits = 32;
        d.unsignedArithmetic = false;
        d.trueValue = 1;
        const auto add = [&](const char* name, DirectiveKind kind, Special special = Special::None) { d.commands[name] = Command{kind, special}; };
        add("org", DirectiveKind::Org);
        add("equ", DirectiveKind::Equ);
        add("=", DirectiveKind::Defl);
        for (const char* n : {"defb", "db", "defm", "dm", "byte", "text"})
            add(n, DirectiveKind::Db);
        for (const char* n : {"defw", "dw", "word"})
            add(n, DirectiveKind::Dw);
        for (const char* n : {"defs", "ds"})
            add(n, DirectiveKind::Ds);
        add("end", DirectiveKind::End);
        add("include", DirectiveKind::Include);
        add("incbin", DirectiveKind::Incbin);
        add("if", DirectiveKind::If);
        add("ifnot", DirectiveKind::If, Special::IfNot);
        add("ifdef", DirectiveKind::If, Special::IfDef);
        add("ifndef", DirectiveKind::If, Special::IfNdef);
        add("else", DirectiveKind::Else);
        add("endif", DirectiveKind::EndIf);
        add("macro", DirectiveKind::Macro, Special::Macro);
        for (const char* n : {"mend", "endm"})
            add(n, DirectiveKind::EndMacro, Special::EndBlock);
        add("repeat", DirectiveKind::Repeat, Special::Repeat);
        for (const char* n : {"rend", "endr"})
            add(n, DirectiveKind::EndMacro, Special::EndBlock);
        for (const char* n : {"list", "nolist", "print", "break"})
            add(n, DirectiveKind::Other, Special::Ignore);
        for (const char* n : {"save", "bank", "bankset", "buildsna", "buildcpr", "snaset", "limit", "module", "write", "charset", "defr", "defi", "defb", "run",
                              "export", "section", "while", "wend", "until", "switch", "case", "default", "endswitch", "struct", "endstruct", "let", "enhance", "nocode",
                              "code", "lz4", "lz48", "lz49", "lzsa1", "lzsa2", "lzx7", "lzx0", "lzapu", "lzexo", "lzclose", "align", "fail", "stop", "assert", "undef",
                              "timer", "tickerstart", "tickerstop", "pushsection", "popsection"})
            if (!d.commands.count(n))
                add(n, DirectiveKind::Other, Special::Text);
        return d;
    }();
    return dialect;
}

// ---------------------------------------------------------------------------------------------------------------------
// the engine's files: the project's documents, then its data files

std::string Normalized(std::string name)
{
    for (char& c : name)
    {
        if (c == '\\')
            c = '/';
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    while (name.rfind("./", 0) == 0)
        name.erase(0, 2);
    return name;
}

std::string BaseName(const std::string& name)
{
    const size_t slash = name.find_last_of('/');
    return slash == std::string::npos ? name : name.substr(slash + 1);
}

std::vector<uint8_t> Bytes(const SourceDocument& d)
{
    const std::string t = d.Text();
    return std::vector<uint8_t>(t.begin(), t.end());
}

rasm::FileSystem Files(const SourceDocument& main, const std::vector<const SourceDocument*>& project, const DataFileReader& data)
{
    rasm::FileSystem fs;
    fs.read = [&main, project, data](const std::string& wanted) -> std::optional<std::vector<uint8_t>> {
        const std::string n = Normalized(wanted);
        std::vector<const SourceDocument*> docs = project;
        docs.push_back(&main);
        for (const SourceDocument* d : docs)
            if (d && Normalized(d->name) == n)
                return Bytes(*d);
        for (const SourceDocument* d : docs)
            if (d && BaseName(Normalized(d->name)) == BaseName(n))
                return Bytes(*d);
        if (data)
        {
            std::vector<uint8_t> bytes = data(wanted);
            if (!bytes.empty())
                return bytes;
        }
        return std::nullopt;
    };
    return fs;
}

// ---------------------------------------------------------------------------------------------------------------------
// IR from the assembly

/// rasm's words keep `<<` / `>>` as `[` / `]` (its preprocessor): back to the operators, outside strings
std::string Operators(const std::string& w)
{
    std::string out;
    char quote = 0;
    for (const char c : w)
    {
        if (quote)
        {
            out += c;
            if (c == quote)
                quote = 0;
            continue;
        }
        if (c == '"' || c == '\'')
            quote = c;
        if (c == '[')
            out += "<<";
        else if (c == ']')
            out += ">>";
        else
            out += c;
    }
    return out;
}

/// The length of the unprefixed Z80 instruction starting `op` (CB included)
int MainLength(uint8_t op)
{
    switch (op)
    {
        case 0x01: case 0x11: case 0x21: case 0x31: case 0x22: case 0x2A: case 0x32: case 0x3A:
        case 0xC2: case 0xC3: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA:
        case 0xC4: case 0xCC: case 0xCD: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC:
            return 3;
        case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E:
        case 0x10: case 0x18: case 0x20: case 0x28: case 0x30: case 0x38:
        case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        case 0xD3: case 0xDB: case 0xCB:
            return 2;
        default:
            return 1;
    }
}

/// The length of the Z80 instruction at the start of `b` (0: a prefix not followed by an instruction)
int InstructionLength(const std::vector<uint8_t>& b)
{
    if (b.empty())
        return 0;
    const uint8_t op = b[0];
    if (op == 0xED)
    {
        if (b.size() < 2)
            return 0;
        const uint8_t x = b[1];
        return (x & 0xC7) == 0x43 ? 4 : 2;   // LD (nn),rr / LD rr,(nn)
    }
    if (op != 0xDD && op != 0xFD)
        return MainLength(op);
    if (b.size() < 2)
        return 0;
    const uint8_t x = b[1];
    if (x == 0xDD || x == 0xFD || x == 0xED)
        return 0;
    if (x == 0xCB)
        return 4;
    if (x == 0x36)
        return 4;
    if (x == 0x34 || x == 0x35 || (x >= 0x70 && x <= 0x77 && x != 0x76) || ((x & 0xC7) == 0x46 && x != 0x76) || ((x & 0xC7) == 0x86))
        return 3;   // (IX+d)
    return 1 + MainLength(x);
}

Expr Hex(int64_t v, int digits)
{
    return Expr::Number(v, v < 10 && v >= 0 ? ir::NumberSpelling::Decimal : ir::NumberSpelling::Hex, digits);
}

class Writer
{
public:
    Writer(const rasm::RasmAssembly& assembly, const SourceDocument& source, FrontendResult& result,
           std::function<std::optional<std::vector<uint8_t>>(const std::string&)> read)
        : a(assembly), src(source), out(result), readFile(std::move(read))
    {
    }

    void Write()
    {
        out.program.dialect = "rasm";
        out.program.trueValue = 1;
        NameLabels();
        for (const auto& d : a.diagnostics)
        {
            if (d.severity == Severity::Info)
                continue;   // PRINT output
            if (d.message.find("not converted: ") != std::string::npos)
                continue;   // reported below, once
            out.diagnostics.push_back(d);
        }

        if (a.zxSnapshot)
            WriteZx();
        else if (a.hasBinary)
            WriteRegion(a.binaryBank, a.binaryStart, a.binaryStart + static_cast<int>(a.binary.size()), false, 0);
        else if (!a.outputs.empty())
            WriteSaves();
        else
            WriteBanks();

        if (a.amsdosHeader && a.hasBinary)
            Report("AMSDOS header of the binary (128 bytes)");
        for (const auto& n : a.notConverted)
            Report(n);

        // labels not placed on a line: EQU at the top, with their final values
        std::vector<Line> equs;
        for (size_t i = 0; i < a.labels.size(); i++)
        {
            if (placed.count(i) || names[i].empty())
                continue;
            Line l;
            l.label = names[i];
            l.sourceLine = a.labels[i].file == 0 && a.labels[i].line > 0 ? static_cast<uint32_t>(a.labels[i].line) : 0;
            Statement s;
            s.kind = Statement::Kind::Directive;
            s.directive = DirectiveKind::Equ;
            s.args.push_back(Hex(a.labels[i].value, 4));
            l.statements.push_back(std::move(s));
            equs.push_back(std::move(l));
        }
        out.program.lines.insert(out.program.lines.begin(), equs.begin(), equs.end());
    }

private:
    const rasm::RasmAssembly& a;
    const SourceDocument& src;
    FrontendResult& out;
    std::vector<std::string> names;                    // per label: the name written ("" = none)
    std::map<std::string, size_t> byRasmName;          // rasm's name -> label
    std::set<size_t> placed;                           // labels written on a line
    std::function<std::optional<std::vector<uint8_t>>(const std::string&)> readFile;
    std::map<int, std::vector<std::string>> fileLines;
    int lastCommentFile = -1;
    int lastCommentLine = -1;
    std::map<std::tuple<int, int, int>, std::vector<size_t>> labelsAt; // (bank, zone, $) -> labels

    void Report(const std::string& what)
    {
        out.diagnostics.push_back({Severity::Warning, 0, 0, "rasm: not converted: " + what});
    }

    /// The spelling the sources use for each word (rasm keeps names upper case): the first one seen
    std::map<std::string, std::string> Spellings()
    {
        std::map<std::string, std::string> spelling;
        auto scan = [&spelling](const std::string& text) {
            size_t k = 0;
            while (k < text.size())
            {
                if (!std::isalnum(static_cast<unsigned char>(text[k])) && text[k] != '_')
                {
                    k++;
                    continue;
                }
                size_t e = k;
                while (e < text.size() && (std::isalnum(static_cast<unsigned char>(text[e])) || text[e] == '_'))
                    e++;
                const std::string word = text.substr(k, e - k);
                std::string upper = word;
                for (char& c : upper)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                spelling.emplace(upper, word);
                k = e;
            }
        };
        scan(src.Text());
        for (size_t f = 1; f < a.files.size() && readFile; f++)
            if (const auto bytes = readFile(a.files[f]))
                scan(std::string(bytes->begin(), bytes->end()));
        return spelling;
    }

    void NameLabels()
    {
        std::set<std::string> used;
        names.resize(a.labels.size());
        const std::map<std::string, std::string> spelling = Spellings();
        for (size_t i = 0; i < a.labels.size(); i++)
        {
            const rasm::RasmLabel& l = a.labels[i];
            byRasmName[l.name] = i;
            // each word of the name as the source spells it, the other characters (. @ of local labels) as '_'
            std::string n;
            for (size_t k = 0; k < l.name.size();)
            {
                const char c = l.name[k];
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                {
                    n += '_';
                    k++;
                    continue;
                }
                size_t e = k;
                while (e < l.name.size() && (std::isalnum(static_cast<unsigned char>(l.name[e])) || l.name[e] == '_'))
                    e++;
                const std::string word = l.name.substr(k, e - k);
                const auto sp = spelling.find(word);
                n += sp != spelling.end() ? sp->second : word;
                k = e;
            }
            if (n.empty() || std::isdigit(static_cast<unsigned char>(n[0])))
                n = "L" + n;
            std::string unique = n;
            for (int k = 2; used.count(unique); k++)
                unique = n + "_" + std::to_string(k);
            used.insert(unique);
            names[i] = unique;
            if (!l.crunched)
                labelsAt[{l.bank, l.orgzone, static_cast<int>(l.value)}].push_back(i);
        }
    }

    std::optional<int64_t> LabelValue(const std::string& rasmName) const
    {
        const auto it = byRasmName.find(rasmName);
        if (it == byRasmName.end())
            return std::nullopt;
        return a.labels[it->second].value;
    }

    /// The expression with rasm's names replaced by the written ones; none when a name is not a label
    std::optional<Expr> Renamed(Expr e) const
    {
        if (e.kind == Expr::Kind::Symbol)
        {
            const auto it = byRasmName.find(e.text);
            if (it == byRasmName.end())
                return std::nullopt;
            e.text = names[it->second];
            return e;
        }
        if (e.kind == Expr::Kind::Raw || e.kind == Expr::Kind::Current || e.kind == Expr::Kind::Memory)
            return std::nullopt;
        for (Expr& x : e.args)
        {
            auto r = Renamed(x);
            if (!r)
                return std::nullopt;
            x = std::move(*r);
        }
        return e;
    }

    static bool HasSymbol(const Expr& e)
    {
        if (e.kind == Expr::Kind::Symbol)
            return true;
        for (const Expr& x : e.args)
            if (HasSymbol(x))
                return true;
        return false;
    }

    /// The operand's expression when it gives `value` (masked to `mask`), else the number
    Expr Choose(const std::string& rasmText, int64_t value, int64_t mask, int digits) const
    {
        Diagnostics ignored;
        const Expr parsed = ParseExpression(Operators(rasmText), Rasm().rules, 0, ignored);
        const auto v = z80::ConstantValue(parsed, [this](const std::string& n) { return LabelValue(n); });
        // the value must fit as it is: a target assembler refuses or warns on a byte of 300
        const bool fits = v && *v >= -((mask + 1) / 2) && *v <= mask;
        if (v && fits && ((*v ^ value) & mask) == 0 && HasSymbol(parsed))
        {
            if (auto r = Renamed(parsed))
                return *r;
        }
        return Hex(value & mask, digits);
    }

    uint8_t Byte(const std::vector<uint8_t>& m, int at) const
    {
        return at >= 0 && at < static_cast<int>(m.size()) ? m[static_cast<size_t>(at)] : 0;
    }

    /// An instruction line from a trace run, when its bytes are what the instruction gives
    std::optional<Statement> InstructionOf(const rasm::TraceItem& t, const std::vector<uint8_t>& m) const
    {
        // rasm's aliases: the standard names
        std::string mnemonic = z80::Lower(t.mnemonic);
        std::vector<std::string> words = t.operands;
        static const std::map<std::string, std::string> aliases = {
            {"sl1", "sll"}, {"shl", "sla"}, {"shr", "srl"}, {"otd", "outd"}, {"oti", "outi"}};
        if (const auto al = aliases.find(mnemonic); al != aliases.end())
            mnemonic = al->second;
        if (mnemonic == "exa" && words.empty())
        {
            mnemonic = "ex";
            words = {"AF", "AF'"};
        }
        if (mnemonic == "ex" && words.size() == 2 && z80::Lower(words[0]) == "af" && z80::Lower(words[1]) == "af")
            words[1] = "AF'";   // rasm's words drop the quote
        if (!z80::IsMnemonic(mnemonic))
            return std::nullopt;
        const int size = t.outputEnd - t.outputStart;
        // one Z80 instruction (rasm's enhanced forms, LD HL,SP or RLC BC, are several: data)
        if (size != static_cast<int>(t.encoded.size()) || InstructionLength(t.encoded) != size)
            return std::nullopt;
        // the bytes no value is written to must be the encoder's
        std::vector<bool> valueByte(static_cast<size_t>(size), false);
        for (const auto& fx : t.fixups)
        {
            const int n = fx.kind == rasm::FixupKind::V16 || fx.kind == rasm::FixupKind::J16 || fx.kind == rasm::FixupKind::IV16 ||
                                  fx.kind == rasm::FixupKind::V16At0
                              ? 2
                              : 1;
            for (int k = 0; k < n && fx.offset + static_cast<uint32_t>(k) < valueByte.size(); k++)
                valueByte[fx.offset + static_cast<uint32_t>(k)] = true;
        }
        for (int k = 0; k < size; k++)
            if (!valueByte[static_cast<size_t>(k)] && Byte(m, t.outputStart + k) != t.encoded[static_cast<size_t>(k)])
                return std::nullopt;

        Statement s;
        s.kind = Statement::Kind::Instruction;
        s.mnemonic = mnemonic;
        Diagnostics ignored;
        size_t f = 0;
        for (size_t i = 0; i < words.size(); i++)
        {
            Operand o = InstructionOperand(Operators(words[i]), i == 0 && z80::TakesCondition(mnemonic),
                                           mnemonic == "in" || mnemonic == "out", Rasm().rules, 0, ignored);
            const bool takesValue = o.kind == Operand::Kind::Immediate || o.kind == Operand::Kind::Memory ||
                                    o.kind == Operand::Kind::Indexed ||
                                    (o.kind == Operand::Kind::Indirect && (o.text == "ix" || o.text == "iy"));
            if (o.kind == Operand::Kind::String)
                return std::nullopt;
            if (!takesValue)
            {
                s.operands.push_back(std::move(o));
                continue;
            }
            if (o.kind == Operand::Kind::Indirect)
            {
                // (IX): rasm writes a displacement of 0 the same way
                if (f >= t.fixups.size() || t.fixups[f].kind != rasm::FixupKind::IV8 || Byte(m, t.outputStart + static_cast<int>(t.fixups[f].offset)) != 0)
                    return std::nullopt;
                f++;
                s.operands.push_back(std::move(o));
                continue;
            }
            if (f >= t.fixups.size())
                return std::nullopt;
            const rasm::Fixup& fx = t.fixups[f++];
            const int at = t.outputStart + static_cast<int>(fx.offset);
            const int b0 = Byte(m, at);
            const int w = b0 | (Byte(m, at + 1) << 8);
            switch (fx.kind)
            {
                case rasm::FixupKind::V8:
                case rasm::FixupKind::V8At3:
                    o.expr = Choose(fx.expression, b0, 0xFF, 2);
                    break;
                case rasm::FixupKind::V16:
                case rasm::FixupKind::J16:
                case rasm::FixupKind::IV16:
                    o.expr = Choose(fx.expression, w, 0xFFFF, 4);
                    break;
                case rasm::FixupKind::J8:
                {
                    const int target = (t.codeStart + static_cast<int>(fx.instructionOffset) + 2 + static_cast<int8_t>(b0)) & 0xFFFF;
                    o.expr = Choose(fx.expression, target, 0xFFFF, 4);
                    break;
                }
                case rasm::FixupKind::IV8:
                {
                    if (o.kind != Operand::Kind::Indexed)
                        return std::nullopt;
                    const int d = static_cast<int8_t>(b0);
                    // rasm evaluates "(0+d)": the expression is the displacement
                    std::string e = fx.expression;
                    if (e.size() >= 2 && e.front() == '(' && e.back() == ')')
                        e = e.substr(1, e.size() - 2);
                    Expr chosen = Choose(e, d, 0xFF, 0);
                    if (chosen.kind == Expr::Kind::Number)
                        chosen = Expr::Number(d);
                    o.expr = std::move(chosen);
                    break;
                }
                case rasm::FixupKind::Rst:
                    o.expr = Hex(b0 & 0x38, 2);
                    break;
                case rasm::FixupKind::Im:
                    if (b0 == 0x46)
                        o.expr = Expr::Number(0);
                    else if (b0 == 0x56)
                        o.expr = Expr::Number(1);
                    else if (b0 == 0x5E)
                        o.expr = Expr::Number(2);
                    else
                        return std::nullopt;
                    break;
                case rasm::FixupKind::BitResSet:
                    o.expr = Expr::Number(((b0 - fx.extra) >> 3) & 7);
                    break;
                default:
                    return std::nullopt;
            }
            s.operands.push_back(std::move(o));
        }
        if (f != t.fixups.size())
            return std::nullopt;
        return s;
    }

    void Push(Line l) { out.program.lines.push_back(std::move(l)); }

    Line Directive(DirectiveKind k, std::vector<Expr> args, std::string text = {})
    {
        Line l;
        Statement s;
        s.kind = Statement::Kind::Directive;
        s.directive = k;
        s.args = std::move(args);
        s.text = std::move(text);
        l.statements.push_back(std::move(s));
        return l;
    }

    Line Sjasmplus(const std::string& text)
    {
        Line l = Directive(DirectiveKind::Other, {}, text);
        l.statements[0].params.push_back("sjasmplus");
        return l;
    }

    void Comment(const std::string& text)
    {
        Line l;
        l.hasComment = true;
        l.comment = " " + text;
        Push(std::move(l));
    }

    /// DB lines of memory bytes (DS for a run of zeros)
    void Data(const std::vector<uint8_t>& m, int from, int to, std::string label, uint32_t sourceLine, std::string comment)
    {
        int p = from;
        while (p < to)
        {
            Line l;
            l.label = std::move(label);
            label.clear();
            l.sourceLine = sourceLine;
            if (comment.size() > 100)
                comment = comment.substr(0, 97) + "...";   // one-line sources joined by ':' (assemblers cap the line)
            if (!comment.empty())
            {
                l.hasComment = true;
                l.comment = " " + comment;
                comment.clear();
            }
            int zeros = 0;
            while (p + zeros < to && Byte(m, p + zeros) == 0)
                zeros++;
            Statement s;
            s.kind = Statement::Kind::Directive;
            if (zeros >= 8)
            {
                s.directive = DirectiveKind::Ds;
                s.args.push_back(Expr::Number(zeros));
                p += zeros;
            }
            else
            {
                s.directive = DirectiveKind::Db;
                for (int k = 0; k < 16 && p < to; k++, p++)
                {
                    Operand o;
                    o.kind = Operand::Kind::Immediate;
                    o.expr = Hex(Byte(m, p), 2);
                    s.operands.push_back(std::move(o));
                }
            }
            l.statements.push_back(std::move(s));
            Push(std::move(l));
        }
    }

    /// The source line a run came from (the main source, or an included file read again), once per line
    /// A DEFB / DEFW run whose values came from expressions: DB / DW with the label where it gives the value
    void DataWithValues(const std::vector<uint8_t>& m, const rasm::TraceItem& t, std::string label, uint32_t sourceLine,
                        std::string comment)
    {
        std::map<int, const rasm::TraceItem::Value*> byOffset;
        for (const auto& v : t.values)
            byOffset[v.offset] = &v;
        const int size = t.outputEnd - t.outputStart;
        int k = 0;
        while (k < size)
        {
            const auto first = byOffset.find(k);
            const bool word = first != byOffset.end() && first->second->size == 2;
            Statement s;
            s.kind = Statement::Kind::Directive;
            s.directive = word ? DirectiveKind::Dw : DirectiveKind::Db;
            while (k < size && s.operands.size() < 8)
            {
                const auto it = byOffset.find(k);
                const bool isWord = it != byOffset.end() && it->second->size == 2;
                if (isWord != word || (isWord && k + 2 > size))
                    break;
                Operand o;
                o.kind = Operand::Kind::Immediate;
                const int at = t.outputStart + k;
                if (isWord)
                {
                    o.expr = Choose(it->second->expression, Byte(m, at) | (Byte(m, at + 1) << 8), 0xFFFF, 4);
                    k += 2;
                }
                else
                {
                    o.expr = it != byOffset.end() ? Choose(it->second->expression, Byte(m, at), 0xFF, 2) : Hex(Byte(m, at), 2);
                    k += 1;
                }
                s.operands.push_back(std::move(o));
            }
            Line l;
            l.label = std::move(label);
            label.clear();
            l.sourceLine = sourceLine;
            if (comment.size() > 100)
                comment = comment.substr(0, 97) + "...";
            if (!comment.empty())
            {
                l.hasComment = true;
                l.comment = " " + comment;
                comment.clear();
            }
            l.statements.push_back(std::move(s));
            Push(std::move(l));
        }
    }

    std::string SourceText(int file, int line)
    {
        if (line < 1 || (file == lastCommentFile && line == lastCommentLine))
            return {};
        lastCommentFile = file;
        lastCommentLine = line;
        if (file == 0)
            return line <= static_cast<int>(src.lines.size()) ? Trim(src.lines[static_cast<size_t>(line - 1)].text) : std::string();
        auto it = fileLines.find(file);
        if (it == fileLines.end())
        {
            std::vector<std::string> lines;
            if (file > 0 && file < static_cast<int>(a.files.size()) && readFile)
            {
                if (const auto bytes = readFile(a.files[static_cast<size_t>(file)]))
                {
                    std::string cur;
                    for (const uint8_t b : *bytes)
                    {
                        if (b == '\n')
                        {
                            lines.push_back(cur);
                            cur.clear();
                        }
                        else if (b != '\r')
                            cur += static_cast<char>(b);
                    }
                    lines.push_back(cur);
                }
            }
            it = fileLines.emplace(file, std::move(lines)).first;
        }
        return line <= static_cast<int>(it->second.size()) ? Trim(it->second[static_cast<size_t>(line - 1)]) : std::string();
    }

    /// Writes physical [from, to) of a memory space. device: ORG takes the physical address mapped by `base` (DISP for
    /// the code's own address); otherwise the output is sequential and ORG gives `$` only (rasm's .bin)
    /// `dollarAt`: sequential output whose bytes are at a known address (physical + base), e.g. a snapshot: the bytes
    /// between the runs keep `$` there
    void WriteRegion(int bank, int from, int to, bool device, int base, const std::vector<uint8_t>* memory = nullptr,
                     int64_t* dollarAt = nullptr)
    {
        const auto mit = a.memory.find(bank);
        static const std::vector<uint8_t> empty;
        const std::vector<uint8_t>& m = memory ? *memory : mit != a.memory.end() ? mit->second : empty;
        // the run that put down each byte last
        std::vector<int> owner(static_cast<size_t>(std::max(0, to - from)), -1);
        for (size_t i = 0; i < a.trace.size(); i++)
        {
            const rasm::TraceItem& t = a.trace[i];
            if (t.bank != bank)
                continue;
            for (int p = std::max(t.outputStart, from); p < std::min(t.outputEnd, to); p++)
                owner[static_cast<size_t>(p - from)] = static_cast<int>(i);
        }
        int p = from;
        int64_t dollar = dollarAt ? *dollarAt : -1;     // the `$` the target is at (-1: unknown)
        bool displaced = false;
        auto origin = [&](int64_t code, int physical) {
            if (!device)
            {
                if (code != dollar)
                    Push(Directive(DirectiveKind::Org, {Hex(code, 4)}));
                return;
            }
            if (dollar == -1)
                Push(Directive(DirectiveKind::Org, {Hex(base + physical, 4)}));
            const bool needDisp = code != base + physical;
            if (needDisp && code != dollar)
            {
                if (displaced)
                    Push(Directive(DirectiveKind::Ent, {}));
                Push(Directive(DirectiveKind::Disp, {Hex(code, 4)}));
                displaced = true;
            }
            else if (!needDisp && displaced)
            {
                Push(Directive(DirectiveKind::Ent, {}));
                displaced = false;
            }
        };
        while (p < to)
        {
            const int o = owner[static_cast<size_t>(p - from)];
            int q = p + 1;
            while (q < to && owner[static_cast<size_t>(q - from)] == o)
                q++;
            if (o < 0)
            {
                // bytes no statement put down (memory between zones): as they are
                if (device && displaced)
                {
                    Push(Directive(DirectiveKind::Ent, {}));
                    displaced = false;
                }
                if (dollar == -1 && device)
                    Push(Directive(DirectiveKind::Org, {Hex(base + p, 4)}));
                if (dollarAt && dollar != base + p)
                    Push(Directive(DirectiveKind::Org, {Hex(base + p, 4)}));
                Data(m, p, q, {}, 0, {});
                dollar = -2;   // unknown: the next run sets it
                if (device || dollarAt)
                    dollar = base + q;
                p = q;
                continue;
            }
            const rasm::TraceItem& t = a.trace[static_cast<size_t>(o)];
            const int64_t code = t.codeStart + (p - t.outputStart);
            origin(code, p);
            std::string label;
            if (p == t.outputStart)
            {
                const auto lit = labelsAt.find({t.bank, t.orgzone, t.codeStart});
                if (lit != labelsAt.end())
                {
                    // a label on the line of the statement goes on it; one on a line of its own (or a second one) on a
                    // line of its own, numbered as the source defines it
                    for (size_t li : lit->second)
                    {
                        if (placed.count(li))
                            continue;
                        placed.insert(li);
                        const rasm::RasmLabel& rl = a.labels[li];
                        if (label.empty() && rl.file == t.file && rl.line == t.line)
                        {
                            label = names[li];
                            continue;
                        }
                        Line own;
                        own.label = names[li];
                        own.sourceLine = rl.file == 0 && rl.line > 0 ? static_cast<uint32_t>(rl.line) : 0;
                        Push(std::move(own));
                    }
                }
            }
            const uint32_t sourceLine = t.file == 0 && t.line > 0 ? static_cast<uint32_t>(t.line) : 0;
            std::optional<Statement> ins;
            if (t.instruction && p == t.outputStart && q == t.outputEnd)
                ins = InstructionOf(t, m);
            if (ins)
            {
                Line l;
                l.label = label;
                l.sourceLine = sourceLine;
                l.statements.push_back(std::move(*ins));
                Push(std::move(l));
            }
            else if (!t.values.empty() && !t.instruction && p == t.outputStart && q == t.outputEnd)
            {
                DataWithValues(m, t, label, sourceLine, SourceText(t.file, t.line));
            }
            else
            {
                Data(m, p, q, label, sourceLine, SourceText(t.file, t.line));
            }
            dollar = code + (q - p);
            p = q;
        }
        if (displaced)
            Push(Directive(DirectiveKind::Ent, {}));
        if (dollarAt)
            *dollarAt = dollar;
    }

    /// The extent of the bytes put down in [from, to) of a memory space
    std::pair<int, int> Extent(int bank, int from, int to) const
    {
        int lo = to, hi = from;
        for (const auto& t : a.trace)
        {
            if (t.bank != bank)
                continue;
            const int s = std::max(t.outputStart, from), e = std::min(t.outputEnd, to);
            if (s < e)
            {
                lo = std::min(lo, s);
                hi = std::max(hi, e);
            }
        }
        return {lo, hi};
    }

    /// BUILDZX: rasm's .sna as it is (sjasmplus' SAVESNA would write its own system variables and stack): the 27-byte
    /// header, then #4000-#FFFF from banks 5 / 2 / 0 (or the upper 48K of bank 0 with BANKSET 0), the code where rasm put it
    void WriteZx()
    {
        const rasm::RasmAssembly::File* sna = nullptr;
        for (const auto& f : a.outputs)
            if (f.name.empty())
                sna = &f;
        if (!sna || sna->bytes.size() != 27 + 3 * 0x4000)
        {
            Report("BUILDZX snapshot");
            WriteBanks();
            return;
        }
        Comment("BUILDZX: rasm's 48K snapshot (27-byte header, then #4000-#FFFF)");
        Push(Sjasmplus("OUTPUT \"rasmoutput.sna\""));
        Push(Directive(DirectiveKind::Org, {Hex(0x4000 - 27, 4)}));
        int64_t dollar = 0x4000;
        const std::vector<uint8_t> header(sna->bytes.begin(), sna->bytes.begin() + 27);
        Data(header, 0, 27, {}, 0, "header: SP, IM 1, the program counter pushed at SP");
        // the pages as rasm wrote them (RUN's program counter included), at their place in their bank
        auto page = [&](int bank, int from, int k) {
            const int at = 0x4000 * (k + 1);
            std::vector<uint8_t> m(0x10000, 0);
            const auto mit = a.memory.find(bank);
            if (mit != a.memory.end())
                std::copy_n(mit->second.begin(), std::min<size_t>(mit->second.size(), m.size()), m.begin());
            std::copy_n(sna->bytes.begin() + 27 + k * 0x4000, 0x4000, m.begin() + from);
            WriteRegion(bank, from, from + 0x4000, false, at - from, &m, &dollar);
        };
        if (a.zxBankset0)
        {
            for (int k = 0; k < 3; k++)
                page(0, 0x4000 * (k + 1), k);
        }
        else
        {
            page(5, 0, 0);
            page(2, 0, 1);
            page(0, 0, 2);
        }
        Push(Sjasmplus("OUTEND"));
    }

    void WriteSaves()
    {
        std::set<int> banks;
        for (const auto& f : a.outputs)
            if (f.bank >= 0)
                banks.insert(f.bank);
        if (banks.size() > 1)
            Report("SAVE from several memory spaces (each is written, the target sees one memory)");
        for (const int bank : banks)
        {
            int lo = 0x10000, hi = 0;
            for (const auto& f : a.outputs)
            {
                if (f.bank != bank || f.size <= 0)
                    continue;
                lo = std::min(lo, f.offset);
                hi = std::max(hi, f.offset + f.size);
            }
            if (lo < hi)
                WriteRegion(bank, lo, hi, true, 0);
        }
        for (const auto& f : a.outputs)
        {
            if (f.bank < 0)
                continue;
            if (f.format == rasm::SaveFormat::Amsdos)
                Report("AMSDOS header of SAVE \"" + f.name + "\"");
            else if (f.format == rasm::SaveFormat::Hobeta)
                Report("HOBETA header of SAVE \"" + f.name + "\"");
            Line l = Directive(DirectiveKind::SaveBinary, {Hex(f.offset, 4), Expr::Number(f.size)}, f.name);
            Push(std::move(l));
        }
    }

    void WriteBanks()
    {
        std::set<int> banks;
        for (const auto& t : a.trace)
            banks.insert(t.bank);
        for (const int bank : banks)
        {
            const auto [lo, hi] = Extent(bank, 0, 0x10000);
            if (lo >= hi)
                continue;
            Comment("rasm memory space " + std::to_string(bank));
            WriteRegion(bank, lo, hi, false, 0);
        }
    }
};
}  // namespace

FrontendResult RasmFrontend::Parse(const SourceDocument& source) const
{
    return ParseWithData(source, {}, {});
}

FrontendResult RasmFrontend::ParseInProject(const SourceDocument& source, const std::vector<const SourceDocument*>& project) const
{
    return ParseWithData(source, project, {});
}

FrontendResult RasmFrontend::ParseWithData(const SourceDocument& source, const std::vector<const SourceDocument*>& project,
                                           const DataFileReader& dataFiles) const
{
    const rasm::FileSystem fs = Files(source, project, dataFiles);
    rasm::EngineOptions options;
    options.timestamp = 0;
    const std::string mainName = source.name.empty() ? "main.asm" : source.name;
    const rasm::RasmAssembly assembly = rasm::AssembleRasm(mainName, Bytes(source), fs, options);
    if (!assembly.ok)
    {
        FrontendResult result = ParseText(source, Rasm());
        std::string first;
        for (const auto& d : assembly.diagnostics)
        {
            if (d.severity == Severity::Error)
            {
                first = d.message;
                break;
            }
        }
        result.diagnostics.push_back({Severity::Info, 0, 0,
                                      "rasm: the source does not assemble alone (" + first + "): converted line by line"});
        return result;
    }
    FrontendResult result;
    Writer(assembly, source, result, fs.read).Write();
    return result;
}
}  // namespace unrealasm::dialects
