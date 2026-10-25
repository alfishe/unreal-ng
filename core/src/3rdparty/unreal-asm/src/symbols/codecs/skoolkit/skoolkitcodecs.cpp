#include "symbols/codecs/skoolkit/skoolkitcodecs.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <optional>

#include "symbols/codecs/rules.h"
#include "symbols/codecs/text/textlines.h"

namespace unrealasm::symbols::codecs
{
namespace
{
using text::Hex;
using text::Trim;

constexpr std::string_view kBlockTypes = "bcgistuw";
constexpr std::string_view kTraitPrefix = "skoolkit:";
constexpr std::string_view kTraitAuto = "skoolkit:auto";
constexpr std::string_view kTraitPoint = "skoolkit:point";
constexpr std::string_view kTraitEqu = "skoolkit:equ";

/// SkoolKit labels end up in an assembler source (skool2asm.py): a C identifier, no Z80 mnemonic or register
NameRules SkoolkitRules()
{
    NameRules r;
    r.charset = Charset::Identifier;
    r.extra = "_";
    r.firstExtra = "_";
    r.reserveZ80 = true;
    return r;
}

bool IsBlockType(char c)
{
    return kBlockTypes.find(c) != std::string_view::npos;
}

bool Digits(std::string_view s, uint32_t base, uint32_t& out)
{
    if (s.empty() || s.size() > 32)
        return false;
    uint64_t value = 0;
    for (const char c : s)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        uint32_t digit = 0;
        if (std::isdigit(u))
            digit = static_cast<uint32_t>(u - '0');
        else if (std::isxdigit(u))
            digit = static_cast<uint32_t>(std::toupper(u) - 'A' + 10);
        else
            return false;
        if (digit >= base)
            return false;
        value = value * base + digit;
        if (value > 0xFFFFFFFFull)
            return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

/// A SkoolKit number: $HHHH (or 0xHHHH) hex, %bbbb binary, else decimal
bool ParseNumber(std::string_view s, uint32_t& out)
{
    if (!s.empty() && s[0] == '$')
        return Digits(s.substr(1), 16, out);
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return Digits(s.substr(2), 16, out);
    if (!s.empty() && s[0] == '%')
        return Digits(s.substr(1), 2, out);
    return Digits(s, 10, out);
}

bool ParseAddress(std::string_view s, uint32_t& out)
{
    return ParseNumber(s, out) && out <= 0xFFFF;
}

std::string Address(uint32_t a)
{
    return "$" + Hex(a, 4);
}

std::string Value(uint32_t v)
{
    return v <= 0xFFFF ? Address(v) : std::to_string(v);
}

/// The titles sna2skool.py writes when nobody named the entry (zx-reaper's DEFAULT_TITLE)
bool IsDefaultTitle(std::string_view title)
{
    if (title == "Unused" || title == "Ignored")
        return true;
    for (const std::string_view head : {"Routine at ", "Data block at ", "Message at ", "Game status buffer entry at "})
        if (title.substr(0, head.size()) == head)
        {
            std::string_view rest = title.substr(head.size());
            if (!rest.empty() && rest[0] == '$')
                rest.remove_prefix(1);
            uint32_t value = 0;
            return Digits(rest, 16, value);
        }
    return false;
}

/// One line of text: line breaks inside a comment become blanks
std::string OneLine(std::string_view s)
{
    std::string out(s);
    std::replace(out.begin(), out.end(), '\n', ' ');
    std::replace(out.begin(), out.end(), '\r', ' ');
    return out;
}

bool HasTrait(const Symbol& s, std::string_view trait)
{
    return std::find(s.traits.begin(), s.traits.end(), trait) != s.traits.end();
}

/// The block letter a symbol was read with ("skoolkit:t" -> 't'); 0 when none
char TraitBlockType(const Symbol& s)
{
    for (const std::string& t : s.traits)
        if (t.size() == kTraitPrefix.size() + 1 && t.compare(0, kTraitPrefix.size(), kTraitPrefix) == 0 && IsBlockType(t.back()))
            return t.back();
    return 0;
}

struct Block
{
    char type = 'c';
    uint32_t address = 0;
    std::string title;
    uint32_t line = 0;
    std::string raw;
};

struct Named
{
    uint32_t address = 0;
    std::string name;
    std::string comment;
    uint32_t line = 0;
    std::string raw;
};

struct Equ
{
    std::string name;
    uint32_t value = 0;
    uint32_t line = 0;
    std::string raw;
};

/// What both formats hold, read: the blocks (entries), the labels, the comments by address, the constants
struct Parsed
{
    std::vector<Block> blocks;
    std::vector<Named> labels;
    std::map<uint32_t, std::string> comments;
    std::vector<Equ> equs;
};

/// "NAME=VALUE" of an @equ directive; false when the value is no number (an expression)
bool ParseEqu(std::string_view text, Equ& out, std::string& message)
{
    const size_t equals = text.find('=');
    if (equals == std::string_view::npos)
    {
        message = "@equ without '=': " + std::string(text);
        return false;
    }
    const std::string_view name = Trim(text.substr(0, equals));
    const std::string_view value = Trim(text.substr(equals + 1));
    if (name.empty() || !ParseNumber(value, out.value))
    {
        message = "@equ " + std::string(text) + ": the value is no number (skipped)";
        return false;
    }
    out.name = std::string(name);
    return true;
}

/// "NAME" of a label directive: SkoolKit's "*" (an automatic entry point label) has no name of its own
std::string LabelName(std::string_view value)
{
    value = Trim(value);
    if (!value.empty() && value.front() == '*')
        value.remove_prefix(1);
    return std::string(Trim(value));
}

void Stamp(Symbol& s, const std::string& importer, const std::string& raw, uint32_t line)
{
    s.provenance.importer = importer;
    s.provenance.raw = raw;
    s.provenance.line = line;
}

/// The symbols of a parsed file (see the header)
SymbolSet Build(Parsed parsed, const std::string& importer, Diagnostics& diagnostics)
{
    SymbolSet set;
    std::stable_sort(parsed.blocks.begin(), parsed.blocks.end(), [](const Block& a, const Block& b) { return a.address < b.address; });
    std::vector<Block> blocks;
    for (Block& b : parsed.blocks)
    {
        if (!blocks.empty() && blocks.back().address == b.address)
        {
            diagnostics.push_back({Severity::Warning, b.line, 0, "a second block at " + Address(b.address) + " (ignored)"});
            continue;
        }
        blocks.push_back(std::move(b));
    }
    std::map<uint32_t, Named> labels;
    for (Named& n : parsed.labels)
    {
        if (labels.count(n.address))
        {
            diagnostics.push_back({Severity::Warning, n.line, 0, "a second label at " + Address(n.address) + ": " + n.name + " (ignored)"});
            continue;
        }
        const uint32_t address = n.address;
        labels.emplace(address, std::move(n));
    }
    const auto kindOf = [](char type) { return type == 'c' ? SymbolKind::Code : type == 'i' ? SymbolKind::Unknown : SymbolKind::Data; };
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const Block& b = blocks[i];
        if (b.type == 'i')
            continue;
        Symbol s;
        s.location.offset = b.address;
        s.kind = kindOf(b.type);
        if (i + 1 < blocks.size())
            s.size = blocks[i + 1].address - b.address;
        s.traits.push_back(std::string(kTraitPrefix) + b.type);
        const auto label = labels.find(b.address);
        if (label != labels.end())
        {
            s.name = label->second.name;
            labels.erase(label);
        }
        else
        {
            s.name = "L" + Hex(b.address, 4);
            s.traits.push_back(std::string(kTraitAuto));
        }
        if (!b.title.empty() && !IsDefaultTitle(b.title))
            s.comment = b.title;
        Stamp(s, importer, b.raw, b.line);
        set.symbols.push_back(std::move(s));
    }
    for (auto& [address, n] : labels)
    {
        Symbol s;
        s.name = n.name;
        s.location.offset = address;
        // The entry the label is in gives its kind
        const auto after = std::upper_bound(blocks.begin(), blocks.end(), address, [](uint32_t a, const Block& b) { return a < b.address; });
        s.kind = after == blocks.begin() ? SymbolKind::Unknown : kindOf(std::prev(after)->type);
        s.traits.push_back(std::string(kTraitPoint));
        s.comment = n.comment;
        if (s.comment.empty())
            if (const auto c = parsed.comments.find(address); c != parsed.comments.end())
                s.comment = c->second;
        Stamp(s, importer, n.raw, n.line);
        set.symbols.push_back(std::move(s));
    }
    std::stable_sort(set.symbols.begin(), set.symbols.end(), [](const Symbol& a, const Symbol& b) { return a.location.offset < b.location.offset; });
    for (const Equ& e : parsed.equs)
    {
        Symbol s;
        s.name = e.name;
        s.location.offset = e.value;
        if (e.value > 0xFFFF)
            s.location.space.kind = SpaceKind::Constant;
        s.traits.push_back(std::string(kTraitEqu));
        Stamp(s, importer, e.raw, e.line);
        set.symbols.push_back(std::move(s));
    }
    return set;
}

/// The value an @equ writes: a constant's own, else the CPU address
std::optional<uint32_t> EquValue(const Symbol& s)
{
    if (s.location.space.kind == SpaceKind::Constant && s.location.space.cpu == "main")
        return s.location.offset;
    const std::optional<uint16_t> a = CpuAddress(s);
    if (!a)
        return std::nullopt;
    return *a;
}

SymbolEncodeResult Finish(SymbolEncodeResult result, const std::string& out)
{
    result.bytes.assign(out.begin(), out.end());
    result.ok = true;
    return result;
}

// skoolkit-ctl: one line -------------------------------------------------------------------------------------------

enum class CtlLine : uint8_t
{
    Skip,
    Block,
    Label,
    Equ,
    Comment,     ///< a sub-block or N comment at an address
    Dropped,     ///< a directive the symbols have no place for (D R E M L > < and the other ASM directives)
    Bad,
};

struct CtlParse
{
    CtlLine kind = CtlLine::Skip;
    char directive = 0;
    Block block;
    Named label;
    Equ equ;
    uint32_t address = 0;
    std::string text;
    std::string message;
};

/// "ADDR[,len...] rest": the address and the rest of the line
bool AddressAndRest(std::string_view s, uint32_t& address, std::string_view& rest)
{
    s = Trim(s);
    size_t end = 0;
    while (end < s.size() && s[end] != ' ' && s[end] != '\t' && s[end] != ',')
        ++end;
    if (!ParseAddress(s.substr(0, end), address))
        return false;
    size_t blank = end;
    while (blank < s.size() && s[blank] != ' ' && s[blank] != '\t')
        ++blank;   // the lengths after the address
    rest = Trim(s.substr(blank));
    return true;
}

CtlParse ParseCtlLine(std::string_view raw)
{
    CtlParse p;
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t'))
        raw.remove_suffix(1);
    if (Trim(raw).empty() || raw[0] == '#' || raw[0] == '%' || raw[0] == ';')
        return p;
    const char d = raw[0];
    p.directive = d;
    const bool spaced = raw.size() > 1 && (raw[1] == ' ' || raw[1] == '\t');
    std::string_view rest;
    if (IsBlockType(d) && spaced)
    {
        if (!AddressAndRest(raw.substr(2), p.block.address, rest))
        {
            p.kind = CtlLine::Bad;
            p.message = "a block directive without an address";
            return p;
        }
        p.kind = CtlLine::Block;
        p.block.type = d;
        p.block.title = std::string(rest);
        return p;
    }
    if (d == '@' && spaced)
    {
        if (!AddressAndRest(raw.substr(2), p.address, rest))
        {
            p.kind = CtlLine::Bad;
            p.message = "an ASM directive without an address";
            return p;
        }
        if (rest.substr(0, 6) == "label=")
        {
            p.label.address = p.address;
            p.label.name = LabelName(rest.substr(6));
            p.kind = p.label.name.empty() ? CtlLine::Dropped : CtlLine::Label;
            return p;
        }
        if (rest.substr(0, 4) == "equ=")
        {
            p.kind = ParseEqu(rest.substr(4), p.equ, p.message) ? CtlLine::Equ : CtlLine::Bad;
            return p;
        }
        p.kind = CtlLine::Dropped;
        return p;
    }
    // Sub-blocks (B C S T W, or a blank letter: the block's type) and the comment directives
    const bool subBlock = std::string_view("BCSTW").find(d) != std::string_view::npos;
    if ((subBlock || d == ' ' || d == '\t' || std::string_view("NDERML><").find(d) != std::string_view::npos) && (spaced || d == ' ' || d == '\t'))
    {
        if (!AddressAndRest(raw.substr(1), p.address, rest))
        {
            p.kind = CtlLine::Bad;
            p.message = std::string("a '") + d + "' directive without an address";
            return p;
        }
        if (subBlock || d == ' ' || d == '\t' || d == 'N')
        {
            p.kind = CtlLine::Comment;
            p.text = std::string(rest);
            return p;
        }
        p.kind = CtlLine::Dropped;
        return p;
    }
    p.kind = CtlLine::Bad;
    p.message = "not a control file line";
    return p;
}

// skoolkit-skool: one instruction line -----------------------------------------------------------------------------

/// "c$6C00 DI ; comment": the control character, the address and the comment (outside quotes); false when the line is
/// no instruction line
bool SkoolInstruction(std::string_view raw, char& control, uint32_t& address, std::string_view& comment)
{
    if (raw.size() < 2 || std::string_view("bcgistuw* !").find(raw[0]) == std::string_view::npos)
        return false;
    size_t end = 1;
    while (end < raw.size() && raw[end] != ' ' && raw[end] != '\t')
        ++end;
    if (!ParseAddress(raw.substr(1, end - 1), address))
        return false;
    control = raw[0];
    comment = {};
    bool quoted = false;
    for (size_t i = end; i < raw.size(); ++i)
    {
        if (raw[i] == '\\' && quoted)
            ++i;
        else if (raw[i] == '"')
            quoted = !quoted;
        else if (raw[i] == ';' && !quoted)
        {
            comment = Trim(raw.substr(i + 1));
            break;
        }
    }
    return true;
}

std::string_view RightTrim(std::string_view s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> ProbeLines(const Probe& probe)
{
    std::vector<std::string_view> lines = text::Lines(probe.bytes);
    if (probe.bytes.size() >= SymbolCodecRegistry::kProbeBytes && lines.size() > 1)
        lines.pop_back();   // the probe may cut the last line
    return lines;
}

int WithExtension(int score, const CodecInfo& info, const Probe& probe)
{
    if (score <= 0)
        return 0;
    const bool extension = std::find(info.extensions.begin(), info.extensions.end(), probe.extension) != info.extensions.end();
    return std::min(100, score + (extension ? 20 : 0));
}
}  // namespace

// skoolkit-ctl ------------------------------------------------------------------------------------------------------

SkoolkitCtlCodec::SkoolkitCtlCodec()
    : _info(MakeInfo("skoolkit-ctl", "SkoolKit control file (c $HHHH Title, @ $HHHH label=NAME)", Family::Text, {"ctl"},
                     SkoolkitRules(), false, "#"))
{
}

int SkoolkitCtlCodec::Detect(const Probe& probe) const
{
    text::LineScore score;
    size_t strong = 0;   // blocks and ASM directives: what makes it a control file
    for (const std::string_view raw : ProbeLines(probe))
    {
        const CtlParse p = ParseCtlLine(raw);
        if (p.kind == CtlLine::Skip)
            continue;
        ++score.data;
        if (p.kind != CtlLine::Bad)
            ++score.matched;
        strong += p.kind == CtlLine::Block || (p.directive == '@' && p.kind != CtlLine::Bad);
    }
    return strong ? WithExtension(score.Score(90), _info, probe) : 0;
}

SymbolDecodeResult SkoolkitCtlCodec::Decode(std::span<const uint8_t> bytes) const
{
    SymbolDecodeResult result;
    Parsed parsed;
    std::map<char, size_t> dropped;
    const std::vector<std::string_view> lines = text::Lines(bytes);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const uint32_t number = static_cast<uint32_t>(i + 1);
        CtlParse p = ParseCtlLine(lines[i]);
        const std::string raw(RightTrim(lines[i]));
        switch (p.kind)
        {
            case CtlLine::Skip:
                break;
            case CtlLine::Block:
                p.block.line = number;
                p.block.raw = raw;
                parsed.blocks.push_back(std::move(p.block));
                break;
            case CtlLine::Label:
                p.label.line = number;
                p.label.raw = raw;
                parsed.labels.push_back(std::move(p.label));
                break;
            case CtlLine::Equ:
                p.equ.line = number;
                p.equ.raw = raw;
                parsed.equs.push_back(std::move(p.equ));
                break;
            case CtlLine::Comment:
                if (!p.text.empty())
                    parsed.comments.emplace(p.address, p.text);
                break;
            case CtlLine::Dropped:
                ++dropped[p.directive];
                break;
            case CtlLine::Bad:
                result.diagnostics.push_back({Severity::Warning, number, 0, p.message});
                break;
        }
    }
    if (!dropped.empty())
    {
        std::string what;
        for (const auto& [directive, count] : dropped)
            what += std::string(what.empty() ? "" : ", ") + directive + " x" + std::to_string(count);
        result.diagnostics.push_back({Severity::Info, 0, 0, "directives the symbols do not hold (dropped): " + what});
    }
    result.file.sets.push_back(Build(std::move(parsed), _info.id, result.diagnostics));
    result.ok = true;
    return result;
}

SymbolEncodeResult SkoolkitCtlCodec::Encode(const SymbolFile& source, const SymbolEncodeOptions& options) const
{
    SymbolEncodeResult result;
    const PreparedFile prepared = Prepare(source, _info, options, result.diagnostics);
    const std::string& nl = options.lineEnd;

    struct OutBlock
    {
        char type = 'c';
        std::string label;
        std::string title;
        uint32_t end = 0;   // where a SkoolKit block read with a size ended (0 = unknown)
    };
    std::map<uint32_t, OutBlock> blocks;
    std::map<uint32_t, std::pair<std::string, std::string>> points;
    std::vector<std::pair<std::string, uint32_t>> equs;
    std::vector<const Symbol*> pointSymbols;
    size_t folded = 0;
    for (const SymbolSet& set : prepared.file.sets)
        for (const Symbol& s : set.symbols)
        {
            const SpaceKind space = s.location.space.kind;
            if (HasTrait(s, kTraitEqu) || s.kind == SymbolKind::Const || space == SpaceKind::Constant || space == SpaceKind::Rom)
            {
                // A constant, or a ROM routine the disassembly refers to: an EQU (sna2skool puts it into the skool)
                if (const std::optional<uint32_t> v = EquValue(s))
                    equs.emplace_back(s.name, *v);
                else
                    result.diagnostics.push_back({Severity::Warning, 0, 0, s.name + ": " + s.location.space.Format() + " has no value (skipped)"});
                continue;
            }
            const std::optional<uint16_t> address = CpuAddress(s);
            if (!address || s.location.space.cpu != "main")
            {
                result.diagnostics.push_back({Severity::Warning, 0, 0, s.name + ": " + s.location.space.Format() + " has no CPU address (skipped)"});
                continue;
            }
            folded += space == SpaceKind::Ram || space == SpaceKind::Cache;
            if (HasTrait(s, kTraitPoint) || s.kind == SymbolKind::Local)
            {
                pointSymbols.push_back(&s);
                continue;
            }
            const bool automatic = HasTrait(s, kTraitAuto);
            const auto existing = blocks.find(*address);
            if (existing != blocks.end())
            {
                if (!automatic && existing->second.label.empty())
                    existing->second.label = s.name;
                else if (!automatic)
                    result.diagnostics.push_back({Severity::Warning, 0, 0, s.name + ": " + Address(*address) + " already has the label " +
                                                                               existing->second.label + " (a control file holds one per address; skipped)"});
                continue;
            }
            OutBlock b;
            const char type = TraitBlockType(s);
            b.type = type ? type : (s.kind == SymbolKind::Data ? 'b' : 'c');
            b.label = automatic ? std::string() : s.name;
            b.title = OneLine(s.comment);
            if (type && s.size > 0)
                b.end = *address + s.size;
            blocks.emplace(*address, std::move(b));
        }
    for (const Symbol* s : pointSymbols)
    {
        const uint32_t address = *CpuAddress(*s);
        const auto block = blocks.find(address);
        if (block != blocks.end() && block->second.label.empty())
        {
            block->second.label = s->name;
            continue;
        }
        if (block != blocks.end() || points.count(address))
        {
            result.diagnostics.push_back({Severity::Warning, 0, 0, s->name + ": " + Address(address) +
                                                                       " already has a label (a control file holds one per address; skipped)"});
            continue;
        }
        points.emplace(address, std::make_pair(s->name, OneLine(s->comment)));
    }

    // The lines in address order: a block, its label, the constants after the first block; the labels inside
    struct OutLine
    {
        uint32_t address;
        int order;
        std::string text;
    };
    std::vector<OutLine> lines;
    for (auto it = blocks.begin(); it != blocks.end(); ++it)
    {
        const uint32_t a = it->first;
        const OutBlock& b = it->second;
        lines.push_back({a, 0, std::string(1, b.type) + " " + Address(a) + (b.title.empty() ? "" : " " + b.title)});
        ++result.written;
        if (!b.label.empty())
            lines.push_back({a, 1, "@ " + Address(a) + " label=" + b.label});
        // A SkoolKit block that ended before the next one: the bytes after it were ignored there
        const auto next = std::next(it);
        if (b.end > a && b.end <= 0xFFFF && (next == blocks.end() || next->first > b.end))
            lines.push_back({b.end, -1, "i " + Address(b.end)});
    }
    for (const auto& [a, point] : points)
    {
        lines.push_back({a, 1, "@ " + Address(a) + " label=" + point.first});
        if (!point.second.empty())
            lines.push_back({a, 2, "N " + Address(a) + " " + point.second});
        ++result.written;
    }
    if (!equs.empty() && blocks.empty())
        result.diagnostics.push_back({Severity::Warning, 0, 0, std::to_string(equs.size()) +
                                                                   " constant(s) skipped: an ASM directive needs a block to sit in"});
    else
        for (const auto& [name, value] : equs)
        {
            lines.push_back({blocks.begin()->first, 3, "@ " + Address(blocks.begin()->first) + " equ=" + name + "=" + Value(value)});
            ++result.written;
        }
    std::stable_sort(lines.begin(), lines.end(), [](const OutLine& x, const OutLine& y) {
        return x.address != y.address ? x.address < y.address : x.order < y.order;
    });
    std::string out = "# SkoolKit control file written by unreal-ng: sna2skool.py -c FILE.ctl SNAPSHOT" + nl;
    for (const std::string& line : prepared.header)
        out += line + nl;
    for (const OutLine& line : lines)
        out += line.text + nl;
    if (folded)
        result.diagnostics.push_back({Severity::Info, 0, 0, std::to_string(folded) + " page symbol(s) written at CPU addresses (the format has no pages)"});
    return Finish(std::move(result), out);
}

// skoolkit-skool ----------------------------------------------------------------------------------------------------

SkoolkitSkoolCodec::SkoolkitSkoolCodec()
    : _info(MakeInfo("skoolkit-skool", "SkoolKit skool file (@label=NAME over c$HHHH lines; written as @equ lines)", Family::Text,
                     {"skool"}, SkoolkitRules(), false, ""))
{
}

int SkoolkitSkoolCodec::Detect(const Probe& probe) const
{
    text::LineScore score;
    size_t strong = 0;   // an entry start or a label directive: what makes it a skool file
    for (const std::string_view line : ProbeLines(probe))
    {
        const std::string_view trimmed = Trim(line);
        if (trimmed.empty() || trimmed.front() == ';')
            continue;
        ++score.data;
        char control = 0;
        uint32_t address = 0;
        std::string_view comment;
        if (SkoolInstruction(line, control, address, comment))
        {
            ++score.matched;
            strong += IsBlockType(control);
        }
        else if (line.size() > 1 && line[0] == '@' && std::isalpha(static_cast<unsigned char>(line[1])))
        {
            ++score.matched;
            strong += line.substr(0, 7) == "@label=";
        }
    }
    return strong ? WithExtension(score.Score(90), _info, probe) : 0;
}

SymbolDecodeResult SkoolkitSkoolCodec::Decode(std::span<const uint8_t> bytes) const
{
    SymbolDecodeResult result;
    Parsed parsed;
    // One entry: its title (the first comment paragraph), whether an instruction came, the label for the next one
    std::string title;
    bool titleDone = false;
    bool inEntry = false;
    char entryType = 0;
    std::optional<Named> pending;
    const std::vector<std::string_view> lines = text::Lines(bytes);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const uint32_t number = static_cast<uint32_t>(i + 1);
        const std::string_view line = RightTrim(lines[i]);
        if (Trim(line).empty())
        {
            if (pending)
                result.diagnostics.push_back({Severity::Warning, pending->line, 0, "@label=" + pending->name + " over no instruction (skipped)"});
            pending.reset();
            title.clear();
            titleDone = false;
            inEntry = false;
            entryType = 0;
            continue;
        }
        if (line[0] == '@')
        {
            const std::string_view directive = line.substr(1);
            if (directive.substr(0, 6) == "label=")
            {
                const std::string name = LabelName(directive.substr(6));
                if (!name.empty())
                {
                    Named n;
                    n.name = name;
                    n.line = number;
                    n.raw = std::string(line);
                    pending = std::move(n);
                }
            }
            else if (directive.substr(0, 4) == "equ=")
            {
                Equ e;
                std::string message;
                if (ParseEqu(directive.substr(4), e, message))
                {
                    e.line = number;
                    e.raw = std::string(line);
                    parsed.equs.push_back(std::move(e));
                }
                else
                    result.diagnostics.push_back({Severity::Warning, number, 0, message});
            }
            continue;
        }
        if (line[0] == ';')
        {
            // The first paragraph of the comment block before the first instruction is the entry's title
            if (!inEntry && !titleDone)
            {
                const std::string_view textPart = Trim(line.substr(1));
                if (textPart.empty() || textPart == ".")
                    titleDone = !title.empty();
                else
                    title += (title.empty() ? "" : " ") + std::string(textPart);
            }
            continue;
        }
        char control = 0;
        uint32_t address = 0;
        std::string_view comment;
        if (!SkoolInstruction(line, control, address, comment))
        {
            // A continued instruction comment ("     ; more"), or the free text of an ignored entry
            if (Trim(line).front() != ';' && entryType != 'i')
                result.diagnostics.push_back({Severity::Warning, number, 0, "not a skool line"});
            continue;
        }
        if (!inEntry)
        {
            inEntry = true;
            if (IsBlockType(control))
            {
                entryType = control;
                Block b;
                b.type = control;
                b.address = address;
                b.title = title;
                b.line = number;
                b.raw = std::string(line);
                parsed.blocks.push_back(std::move(b));
            }
        }
        if (pending)
        {
            pending->address = address;
            pending->comment = std::string(comment);
            parsed.labels.push_back(std::move(*pending));
            pending.reset();
        }
    }
    result.file.sets.push_back(Build(std::move(parsed), _info.id, result.diagnostics));
    result.ok = true;
    return result;
}

SymbolEncodeResult SkoolkitSkoolCodec::Encode(const SymbolFile& source, const SymbolEncodeOptions& options) const
{
    // A skool file is a disassembly: without the instructions only the names can go, as the @equ lines of a head.skool
    // (zx-reaper's layout; "zxq.py equs --consolidate" turns the ones inside the disassembly into @label lines)
    SymbolEncodeResult result;
    const PreparedFile prepared = Prepare(source, _info, options, result.diagnostics);
    std::string out = "@start" + options.lineEnd;
    size_t comments = 0;
    for (const SymbolSet& set : prepared.file.sets)
        for (const Symbol& s : set.symbols)
        {
            if (HasTrait(s, kTraitAuto))
                continue;
            const std::optional<uint32_t> v = EquValue(s);
            if (!v || s.location.space.cpu != "main")
            {
                result.diagnostics.push_back({Severity::Warning, 0, 0, s.name + ": " + s.location.space.Format() + " has no value (skipped)"});
                continue;
            }
            out += "@equ=" + s.name + "=" + Value(*v) + options.lineEnd;
            ++result.written;
            comments += !s.comment.empty();
        }
    if (comments)
        result.diagnostics.push_back({Severity::Info, 0, 0, std::to_string(comments) + " comment(s) not written (an @equ line has none; use skoolkit-ctl)"});
    return Finish(std::move(result), out);
}
}  // namespace unrealasm::symbols::codecs
