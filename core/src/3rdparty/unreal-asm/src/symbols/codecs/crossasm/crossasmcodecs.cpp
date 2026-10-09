#include "symbols/codecs/crossasm/crossasmcodecs.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <set>

#include "symbols/codecs/rules.h"
#include "symbols/codecs/text/textlines.h"
#include "symbols/codecs/text/tokenizer.h"

namespace unrealasm::symbols::codecs
{
namespace
{
using text::Hex;
using text::Trim;

/// The number a format without pages writes for a symbol: a constant's value, else its CPU address
std::optional<uint32_t> Value(const Symbol& s)
{
    if (s.location.space.kind == SpaceKind::Constant && s.location.space.cpu == "main")
        return s.location.offset;
    return CpuAddress(s);
}

/// A value read from a format without pages: a 16-bit address in the CPU view, a larger number a constant
void PlaceValue(Symbol& s, uint32_t value)
{
    s.location.offset = value;
    if (value > 0xFFFF)
        s.location.space.kind = SpaceKind::Constant;
}

bool IsPaged(const Symbol& s)
{
    const SpaceKind k = s.location.space.kind;
    return k == SpaceKind::Rom || k == SpaceKind::Ram || k == SpaceKind::Cache;
}

/// Every symbol of the file with its value, sorted by name (the order sjasmplus and pasmo write); what has no value
/// is reported
std::vector<std::pair<const Symbol*, uint32_t>> SortedValues(const SymbolFile& file, uint32_t max, SymbolEncodeResult& result)
{
    std::vector<std::pair<const Symbol*, uint32_t>> out;
    size_t folded = 0;
    for (const SymbolSet& set : file.sets)
        for (const Symbol& s : set.symbols)
        {
            const std::optional<uint32_t> v = Value(s);
            if (!v || *v > max)
            {
                result.diagnostics.push_back({Severity::Warning, 0, 0, s.name + ": " + s.location.space.Format() + " has no value this format can write (skipped)"});
                continue;
            }
            folded += IsPaged(s);
            out.emplace_back(&s, *v);
        }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first->name < b.first->name; });
    if (folded)
        result.diagnostics.push_back({Severity::Info, 0, 0, std::to_string(folded) + " page symbol(s) written at CPU addresses (the format has no pages)"});
    return out;
}

SymbolEncodeResult Finish(SymbolEncodeResult result, const std::string& text)
{
    result.bytes.assign(text.begin(), text.end());
    result.ok = true;
    return result;
}

/// The comment lines Prepare made (renames, commented symbols)
std::string Header(const PreparedFile& prepared, const SymbolEncodeOptions& options)
{
    std::string out;
    for (const std::string& line : prepared.header)
        out += line + options.lineEnd;
    return out;
}

void Bad(SymbolDecodeResult& result, size_t line, std::string message)
{
    result.diagnostics.push_back({Severity::Warning, static_cast<uint32_t>(line), 0, std::move(message)});
}

void Stamp(Symbol& s, const std::string& importer, std::string_view raw, size_t line)
{
    s.provenance.importer = importer;
    s.provenance.raw = std::string(raw);
    s.provenance.line = static_cast<uint32_t>(line);
}

int ScoreProbe(const Probe& probe, int base, bool (*accepts)(std::string_view))
{
    std::vector<std::string_view> lines = text::Lines(probe.bytes);
    if (probe.bytes.size() >= SymbolCodecRegistry::kProbeBytes && lines.size() > 1)
        lines.pop_back();
    text::LineScore score;
    for (const std::string_view raw : lines)
    {
        const std::string_view line = Trim(raw);
        if (line.empty())
            continue;
        ++score.data;
        score.matched += accepts(line);
    }
    return score.Score(base);
}

std::vector<std::string_view> Split(std::string_view s, char by, size_t maxFields)
{
    std::vector<std::string_view> out;
    while (out.size() + 1 < maxFields)
    {
        const size_t p = s.find(by);
        if (p == std::string_view::npos)
            break;
        out.push_back(s.substr(0, p));
        s.remove_prefix(p + 1);
    }
    out.push_back(s);
    return out;
}

bool ToInt(std::string_view s, int64_t& out)
{
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && end == s.data() + s.size();
}

// sjasmplus-sym -----------------------------------------------------------------------------------------------------

bool SymLine(std::string_view line, std::string_view& name, int64_t& value)
{
    const size_t equ = line.find(": EQU ");
    if (equ == std::string_view::npos || equ == 0)
        return false;
    name = line.substr(0, equ);
    return text::ParseNumber(Trim(line.substr(equ + 6)), value) && value >= 0;
}

bool SymAccepts(std::string_view line)
{
    std::string_view name;
    int64_t value = 0;
    return SymLine(line, name, value) && line.find(" EQU 0x") != std::string_view::npos;
}

// pasmo -------------------------------------------------------------------------------------------------------------

bool PasmoLine(std::string_view line, std::string_view& name, int64_t& value)
{
    const size_t gap = line.find_first_of(" \t");
    if (gap == std::string_view::npos || gap == 0)
        return false;
    name = line.substr(0, gap);
    const std::string_view rest = Trim(line.substr(gap));
    return rest.substr(0, 4) == "EQU " && text::ParseNumber(Trim(rest.substr(4)), value);
}

bool PasmoAccepts(std::string_view line)
{
    std::string_view name;
    int64_t value = 0;
    return PasmoLine(line, name, value) && line.find('\t') != std::string_view::npos && line.back() == 'H';
}
}  // namespace

// sjasmplus-sym -----------------------------------------------------------------------------------------------------

SjasmplusSymCodec::SjasmplusSymCodec()
    : _info(MakeInfo("sjasmplus-sym", "sjasmplus symbol file (--sym, --exp)", Family::Text, {"sym", "exp"}, SjasmplusRules(), false, ";"))
{
}

int SjasmplusSymCodec::Detect(const Probe& probe) const
{
    return ScoreProbe(probe, 95, SymAccepts);
}

SymbolDecodeResult SjasmplusSymCodec::Decode(std::span<const uint8_t> bytes) const
{
    SymbolDecodeResult result;
    SymbolSet set;
    const auto lines = text::Lines(bytes);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::string_view line = Trim(lines[i]);
        if (text::Skipped(line))
            continue;
        std::string_view name;
        int64_t value = 0;
        if (!SymLine(line, name, value))
        {
            Bad(result, i + 1, "not \"NAME: EQU value\"");
            continue;
        }
        Symbol s;
        s.name = std::string(name);
        PlaceValue(s, static_cast<uint32_t>(value));
        Stamp(s, _info.id, line, i + 1);
        set.symbols.push_back(std::move(s));
    }
    result.file.sets.push_back(std::move(set));
    result.ok = true;
    return result;
}

SymbolEncodeResult SjasmplusSymCodec::Encode(const SymbolFile& source, const SymbolEncodeOptions& options) const
{
    SymbolEncodeResult result;
    const PreparedFile prepared = Prepare(source, _info, options, result.diagnostics);
    const SymbolFile& file = prepared.file;
    std::string out = Header(prepared, options);
    for (const auto& [s, v] : SortedValues(file, 0xFFFFFFFF, result))
    {
        out += s->name + ": EQU 0x" + Hex(v, 8) + options.lineEnd;
        ++result.written;
    }
    return Finish(std::move(result), out);
}

// sjasmplus-sld -----------------------------------------------------------------------------------------------------

SjasmplusSldCodec::SjasmplusSldCodec()
    : _info(MakeInfo("sjasmplus-sld", "sjasmplus source-level debugging data (--sld)", Family::Text, {"sld"}, SjasmplusRules(), true, "||"))
{
}

int SjasmplusSldCodec::Detect(const Probe& probe) const
{
    const std::string_view text(reinterpret_cast<const char*>(probe.bytes.data()), probe.bytes.size());
    return text.substr(0, 18) == "|SLD.data.version|" ? 100 : 0;
}

SymbolDecodeResult SjasmplusSldCodec::Decode(std::span<const uint8_t> bytes) const
{
    SymbolDecodeResult result;
    const auto lines = text::Lines(bytes);
    if (lines.empty() || lines[0].substr(0, 18) != "|SLD.data.version|")
    {
        result.diagnostics.push_back({Severity::Error, 1, 0, "no |SLD.data.version| line"});
        return result;
    }
    if (lines[0].substr(18) != "1")
        result.diagnostics.push_back({Severity::Warning, 1, 0, "SLD version " + std::string(lines[0].substr(18)) + " (read as version 1)"});
    // Instructions mark code: a label at an address an instruction was traced at is code, else data
    std::set<std::pair<int64_t, int64_t>> traced;
    uint32_t pageSize = 0x4000;
    struct Labeled
    {
        size_t line;
        std::vector<std::string_view> fields;
    };
    std::vector<Labeled> labels;
    for (size_t i = 1; i < lines.size(); ++i)
    {
        const std::string_view line = lines[i];
        if (line.empty() || line[0] == '|')
            continue;   // a control or comment line
        const std::vector<std::string_view> f = Split(line, '|', 8);
        int64_t page = 0;
        int64_t value = 0;
        if (f.size() < 8 || f[6].size() != 1 || !ToInt(f[4], page) || !ToInt(f[5], value))
        {
            Bad(result, i + 1, "not an SLD line of eight fields");
            continue;
        }
        switch (f[6][0])
        {
            case 'T': traced.insert({page, value}); break;
            case 'L': labels.push_back({i + 1, f}); break;
            case 'Z':
            {
                const size_t at = f[7].find("pages.size:");
                int64_t size = 0;
                if (at != std::string_view::npos && ToInt(Split(f[7].substr(at + 11), ',', 2)[0], size) && size > 0 && (size & (size - 1)) == 0)
                    pageSize = static_cast<uint32_t>(size);
                break;
            }
            default: break;   // F / D repeat the L lines (deprecated), K are comments
        }
    }
    SymbolSet set;
    for (const Labeled& l : labels)
    {
        const std::vector<std::string_view> parts = Split(l.fields[7], ',', 64);
        if (parts.size() < 3)
        {
            Bad(result, l.line, "an L line without module, main and local name");
            continue;
        }
        const std::vector<std::string_view> traits(parts.begin() + 3, parts.end());
        auto has = [&traits](std::string_view t) { return std::find(traits.begin(), traits.end(), t) != traits.end(); };
        if (has("+module") || has("+endmod") || has("+struct_def") || has("+macro") || (parts[1].empty() && parts[2].empty()))
            continue;   // not a symbol: module brackets, structure and macro definitions
        Symbol s;
        std::string name;
        for (const std::string_view p : {parts[0], parts[1], parts[2]})
            if (!p.empty())
                name += (name.empty() ? "" : ".") + std::string(p);
        s.name = name;
        s.module = std::string(parts[0]);
        for (const std::string_view t : traits)
            if (t != "+equ" && t != "+local")
                s.traits.push_back(std::string(t));
        int64_t page = 0;
        int64_t value = 0;
        ToInt(l.fields[4], page);
        ToInt(l.fields[5], value);
        const bool equ = has("+equ");
        if (equ)
            s.kind = SymbolKind::Const;
        else if (!parts[2].empty())
            s.kind = SymbolKind::Local;
        else
            s.kind = traced.count({page, value}) ? SymbolKind::Code : SymbolKind::Data;
        if (!parts[2].empty())
            s.parent = parts[0].empty() ? std::string(parts[1]) : std::string(parts[0]) + "." + std::string(parts[1]);
        if (!equ && page >= 0 && value >= 0 && value <= 0xFFFF)
        {
            const uint64_t physical = static_cast<uint64_t>(page) * pageSize + (static_cast<uint64_t>(value) & (pageSize - 1));
            s.location.space.kind = SpaceKind::Ram;
            s.location.space.page = static_cast<uint16_t>(physical >> 14);
            s.location.offset = static_cast<uint32_t>(physical & 0x3FFF);
            s.window = static_cast<int>(value >> 14);
        }
        else
            PlaceValue(s, static_cast<uint32_t>(value));
        const std::vector<std::string_view> position = Split(l.fields[1], ':', 3);
        int64_t number = 0;
        s.source.file = std::string(l.fields[0]);
        if (ToInt(position[0], number) && number > 0)
            s.source.line = static_cast<uint32_t>(number);
        if (position.size() > 1 && ToInt(position[1], number) && number > 0)
            s.source.column = static_cast<uint32_t>(number);
        Stamp(s, _info.id, lines[l.line - 1], l.line);
        set.symbols.push_back(std::move(s));
    }
    result.file.sets.push_back(std::move(set));
    result.ok = true;
    return result;
}

SymbolEncodeResult SjasmplusSldCodec::Encode(const SymbolFile& source, const SymbolEncodeOptions& options) const
{
    SymbolEncodeResult result;
    const PreparedFile prepared = Prepare(source, _info, options, result.diagnostics);
    const SymbolFile& file = prepared.file;
    const std::string& nl = options.lineEnd;
    std::string body;
    int lastPage = -1;
    size_t folded = 0;
    for (const SymbolSet& set : file.sets)
        for (const Symbol& s : set.symbols)
        {
            const std::optional<uint32_t> v = Value(s);
            if (!v)
            {
                result.diagnostics.push_back({Severity::Warning, 0, 0, s.name + ": " + s.location.space.Format() + " cannot be written (skipped)"});
                continue;
            }
            int page = -1;
            if (s.location.space.kind == SpaceKind::Ram && s.location.space.cpu == "main")
                page = s.location.space.page;
            else
                folded += IsPaged(s);
            lastPage = std::max(lastPage, page);
            // The name back in its three parts: module, main label, local label
            std::string rest = s.name;
            if (!s.module.empty() && rest.rfind(s.module + ".", 0) == 0)
                rest.erase(0, s.module.size() + 1);
            std::string main = rest;
            std::string local;
            if (s.kind == SymbolKind::Local && !s.parent.empty())
            {
                std::string parent = s.parent;
                if (!s.module.empty() && parent.rfind(s.module + ".", 0) == 0)
                    parent.erase(0, s.module.size() + 1);
                if (rest.rfind(parent + ".", 0) == 0)
                {
                    main = parent;
                    local = rest.substr(parent.size() + 1);
                }
            }
            const std::string where = (s.source.file.empty() ? std::string("symbols") : s.source.file) + "|" + std::to_string(s.source.line) +
                                      (s.source.column ? ":" + std::to_string(s.source.column) : std::string()) + "||0|" +
                                      std::to_string(s.kind == SymbolKind::Const ? -1 : page) + "|" + std::to_string(*v) + "|";
            std::string data = s.module + "," + main + "," + local;
            if (s.kind == SymbolKind::Const)
                data += ",+equ";
            if (s.kind == SymbolKind::Local)
                data += ",+local";
            for (const std::string& t : s.traits)
                if (!t.empty() && t[0] == '+')
                    data += "," + t;
            body += where + "L|" + data + nl;
            if (s.kind == SymbolKind::Code)
                body += where + "T|" + nl;   // an instruction at the label: code
            ++result.written;
        }
    std::string out = "|SLD.data.version|1" + nl + Header(prepared, options);
    if (lastPage >= 0)
        out += "symbols|1||0|-1|-1|Z|pages.size:16384,pages.count:" + std::to_string(std::max(lastPage + 1, 8)) +
               ",slots.count:4,slots.adr:0,16384,32768,49152" + nl;
    if (folded)
        result.diagnostics.push_back({Severity::Info, 0, 0, std::to_string(folded) + " ROM / cache page symbol(s) written at CPU addresses"});
    return Finish(std::move(result), out + body);
}

// sjasmplus-lst -----------------------------------------------------------------------------------------------------

SjasmplusLstCodec::SjasmplusLstCodec()
    : _info(MakeInfo("sjasmplus-lst", "sjasmplus listing (--lst): labels", Family::Text, {"lst"}, SjasmplusRules(), false, ""))
{
}

namespace
{
/// A listing line: its address and source text ("" when the line has none)
bool ListingLine(std::string_view line, uint32_t& address, std::string_view& source)
{
    size_t p = 0;
    while (p < line.size() && line[p] == ' ')
        ++p;
    const size_t digits = p;
    while (p < line.size() && line[p] >= '0' && line[p] <= '9')
        ++p;
    if (p == digits)
        return false;
    while (p < line.size() && (line[p] == '+' || line[p] == '~'))
        ++p;   // macro / reptition markers
    while (p < line.size() && line[p] == ' ')
        ++p;
    if (p + 4 > line.size() || (p + 4 < line.size() && line[p + 4] != ' '))
        return false;
    int64_t value = 0;
    if (!text::ParseNumber("#" + std::string(line.substr(p, 4)), value))
        return false;
    address = static_cast<uint32_t>(value);
    // The address, then the bytes (up to four, "21 00 40 "), then the source: 18 characters after the address
    const size_t at = p + 18;
    source = at < line.size() ? line.substr(at) : std::string_view();
    return true;
}

bool LstAccepts(std::string_view line)
{
    uint32_t address = 0;
    std::string_view source;
    return line.rfind("# file ", 0) == 0 || ListingLine(line, address, source);
}

bool IsDataDirective(const std::string& upper)
{
    static const std::set<std::string> kData = {"DB", "DW", "DS", "DD", "DZ", "DC", "DM", "DEFB", "DEFW", "DEFS", "DEFM", "DEFD", "DEFG",
                                                "DEFH", "DWORD", "BLOCK", "BYTE", "WORD", "ABYTE", "ABYTEC", "ABYTEZ", "INCBIN"};
    return kData.count(upper) != 0;
}
}  // namespace

int SjasmplusLstCodec::Detect(const Probe& probe) const
{
    const std::string_view text(reinterpret_cast<const char*>(probe.bytes.data()), probe.bytes.size());
    const int score = ScoreProbe(probe, 70, LstAccepts);
    return text.rfind("# file opened: ", 0) == 0 ? std::max(score, 90) : score;
}

SymbolDecodeResult SjasmplusLstCodec::Decode(std::span<const uint8_t> bytes) const
{
    SymbolDecodeResult result;
    SymbolSet set;
    std::string file;
    std::string module;
    std::string lastGlobal;
    text::TokenizerOptions words;
    words.identExtra = "_.?!@#";
    const auto lines = text::Lines(bytes);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::string_view line = lines[i];
        if (line.rfind("# file opened: ", 0) == 0)
        {
            file = std::string(Trim(line.substr(15)));
            continue;
        }
        uint32_t address = 0;
        std::string_view source;
        if (!ListingLine(line, address, source) || source.empty())
            continue;
        std::string label;
        std::string_view rest = source;
        if (source[0] != ' ' && source[0] != '\t' && source[0] != ';')
        {
            const size_t end = source.find_first_of(" \t:;");
            label = std::string(source.substr(0, end));
            rest = end == std::string_view::npos ? std::string_view() : source.substr(end + (source[end] == ':' ? 1 : 0));
        }
        const std::vector<text::Token> tokens = text::Tokenize(rest, words);
        std::string instruction;
        size_t operand = 0;
        if (!tokens.empty() && (tokens[0].kind == text::TokenKind::Ident || tokens[0].text == "="))
        {
            instruction = text::Upper(tokens[0].text);
            operand = 1;
        }
        if (instruction == "MODULE")
        {
            module = tokens.size() > 1 ? std::string(tokens[1].text) : std::string();
            continue;
        }
        if (instruction == "ENDMODULE")
        {
            module.clear();
            continue;
        }
        if (label.empty() || std::all_of(label.begin(), label.end(), [](char c) { return c >= '0' && c <= '9'; }))
            continue;   // no label, or a temporary label (1:)
        Symbol s;
        if (label[0] == '.')
        {
            s.name = lastGlobal + label;
            s.parent = lastGlobal;
            s.kind = SymbolKind::Local;
        }
        else
        {
            const bool global = label[0] == '@';
            s.name = global ? label.substr(1) : module.empty() ? label : module + "." + label;
            lastGlobal = s.name;
        }
        s.module = label[0] == '@' ? std::string() : module;
        if (instruction == "EQU" || instruction == "DEFL" || instruction == "=")
        {
            if (operand >= tokens.size() || tokens[operand].kind != text::TokenKind::Number ||
                (operand + 1 < tokens.size() && tokens[operand + 1].kind != text::TokenKind::Comment))
            {
                Bad(result, i + 1, s.name + ": the value is an expression the listing does not show (skipped)");
                continue;
            }
            s.kind = SymbolKind::Const;
            PlaceValue(s, static_cast<uint32_t>(tokens[operand].value));
        }
        else
        {
            s.location.offset = address;
            if (s.kind != SymbolKind::Local && !instruction.empty())
                s.kind = IsDataDirective(instruction) ? SymbolKind::Data : SymbolKind::Code;
        }
        if (!tokens.empty() && tokens.back().kind == text::TokenKind::Comment)
            s.comment = std::string(Trim(tokens.back().text.substr(1)));
        s.source.file = file;
        Stamp(s, _info.id, line, i + 1);
        set.symbols.push_back(std::move(s));
    }
    result.file.sets.push_back(std::move(set));
    result.ok = true;
    return result;
}

SymbolEncodeResult SjasmplusLstCodec::Encode(const SymbolFile& source, const SymbolEncodeOptions& options) const
{
    SymbolEncodeResult result;
    const PreparedFile prepared = Prepare(source, _info, options, result.diagnostics);
    const SymbolFile& file = prepared.file;
    const std::string& nl = options.lineEnd;
    size_t count = 0;
    for (const SymbolSet& set : file.sets)
        count += set.symbols.size();
    const size_t width = std::to_string(std::max<size_t>(count, 1)).size();
    std::string out = "# file opened: symbols.asm" + nl;
    size_t number = 0;
    size_t folded = 0;
    for (const SymbolSet& set : file.sets)
        for (const Symbol& s : set.symbols)
        {
            const std::optional<uint32_t> v = Value(s);
            if (!v || (s.kind != SymbolKind::Const && *v > 0xFFFF))
            {
                result.diagnostics.push_back({Severity::Warning, 0, 0, s.name + ": " + s.location.space.Format() + " cannot be written (skipped)"});
                continue;
            }
            folded += IsPaged(s);
            std::string n = std::to_string(++number);
            n.insert(0, width - n.size(), ' ');
            std::string source = s.kind == SymbolKind::Const ? s.name + " EQU #" + Hex(*v, *v > 0xFFFF ? 8 : 4) : s.name + ":";
            if (!s.comment.empty())
                source += " ; " + s.comment;
            out += n + "    " + (s.kind == SymbolKind::Const ? std::string("0000") : Hex(*v, 4)) + std::string(14, ' ') + source + nl;
            ++result.written;
        }
    out += "# file closed: symbols.asm" + nl;
    if (folded)
        result.diagnostics.push_back({Severity::Info, 0, 0, std::to_string(folded) + " page symbol(s) written at CPU addresses (a listing has no pages)"});
    return Finish(std::move(result), out);
}

// pasmo -------------------------------------------------------------------------------------------------------------

PasmoCodec::PasmoCodec() : _info(MakeInfo("pasmo", "pasmo symbol file (NAME EQU 0HHHHH)", Family::Text, {"symbol", "pub"}, PasmoRules(), false, ";")) {}

int PasmoCodec::Detect(const Probe& probe) const
{
    return ScoreProbe(probe, 90, PasmoAccepts);
}

SymbolDecodeResult PasmoCodec::Decode(std::span<const uint8_t> bytes) const
{
    SymbolDecodeResult result;
    SymbolSet set;
    const auto lines = text::Lines(bytes);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::string_view line = Trim(lines[i]);
        if (text::Skipped(line))
            continue;
        std::string_view name;
        int64_t value = 0;
        if (!PasmoLine(line, name, value) || value < 0)
        {
            Bad(result, i + 1, "not \"NAME EQU value\"");
            continue;
        }
        Symbol s;
        s.name = std::string(name);
        PlaceValue(s, static_cast<uint32_t>(value));
        Stamp(s, _info.id, line, i + 1);
        set.symbols.push_back(std::move(s));
    }
    result.file.sets.push_back(std::move(set));
    result.ok = true;
    return result;
}

SymbolEncodeResult PasmoCodec::Encode(const SymbolFile& source, const SymbolEncodeOptions& options) const
{
    SymbolEncodeResult result;
    const PreparedFile prepared = Prepare(source, _info, options, result.diagnostics);
    const SymbolFile& file = prepared.file;
    std::string out = Header(prepared, options);
    for (const auto& [s, v] : SortedValues(file, 0xFFFF, result))
    {
        out += s->name + (s->name.size() < 8 ? "\t\t" : "\t") + "EQU 0" + Hex(v, 4) + "H" + options.lineEnd;
        ++result.written;
    }
    return Finish(std::move(result), out);
}

// z88dk-map --------------------------------------------------------------------------------------------------------

namespace
{
constexpr size_t kZ88Column = 31;   // z80asm's "%-*s" with COLUMN_WIDTH - 1 (src/z80asm/src/c/symtab1.c)

/// "name = $HHHH [; type, scope, def, module, section, file:line]"
bool MapLine(std::string_view line, std::string_view& name, int64_t& value, std::string_view& info)
{
    const size_t equals = line.find(" = $");
    if (equals == std::string_view::npos || equals == 0)
        return false;
    name = Trim(line.substr(0, equals));
    std::string_view rest = line.substr(equals + 3);
    const size_t semicolon = rest.find(';');
    info = semicolon == std::string_view::npos ? std::string_view() : Trim(rest.substr(semicolon + 1));
    return !name.empty() && text::ParseNumber(Trim(rest.substr(0, semicolon)), value) && value >= 0;
}

bool MapAccepts(std::string_view line)
{
    std::string_view name;
    std::string_view info;
    int64_t value = 0;
    return MapLine(line, name, value, info) && std::count(info.begin(), info.end(), ',') == 5;
}
}  // namespace

Z88dkMapCodec::Z88dkMapCodec()
    : _info(MakeInfo("z88dk-map", "z88dk z80asm map file (-m; also -s)", Family::Text, {"map", "sym"}, Z88dkRules(), false, ""))
{
}

int Z88dkMapCodec::Detect(const Probe& probe) const
{
    return ScoreProbe(probe, 95, MapAccepts);
}

SymbolDecodeResult Z88dkMapCodec::Decode(std::span<const uint8_t> bytes) const
{
    SymbolDecodeResult result;
    SymbolSet set;
    const auto lines = text::Lines(bytes);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::string_view line = Trim(lines[i]);
        if (line.empty())
            continue;
        std::string_view name;
        std::string_view info;
        int64_t value = 0;
        if (!MapLine(line, name, value, info))
        {
            Bad(result, i + 1, "not \"name = $value ; ...\"");
            continue;
        }
        Symbol s;
        s.name = std::string(name);
        PlaceValue(s, static_cast<uint32_t>(value));
        const std::vector<std::string_view> f = Split(info, ',', 6);
        if (f.size() == 6)
        {
            const std::string_view type = Trim(f[0]);
            const std::string_view scope = Trim(f[1]);
            if (type == "const")
                s.kind = SymbolKind::Const;
            else if (type != "addr" && !type.empty())
                s.provenance.type = std::string(type);
            s.exported = scope == "public" || scope == "global";
            if (scope != "local" && scope != "public" && !scope.empty())
                s.traits.push_back(std::string(scope));   // global, extern
            if (Trim(f[2]) == "def")
                s.traits.push_back("def");                 // defined by the linker (__head, __code_size ...)
            s.module = std::string(Trim(f[3]));
            s.section = std::string(Trim(f[4]));
            const std::string_view where = Trim(f[5]);
            const size_t colon = where.rfind(':');
            int64_t number = 0;
            if (colon != std::string_view::npos && ToInt(where.substr(colon + 1), number) && number > 0)
            {
                s.source.file = std::string(where.substr(0, colon));
                s.source.line = static_cast<uint32_t>(number);
            }
            else
                s.source.file = std::string(where);
        }
        Stamp(s, _info.id, line, i + 1);
        set.symbols.push_back(std::move(s));
    }
    result.file.sets.push_back(std::move(set));
    result.ok = true;
    return result;
}

SymbolEncodeResult Z88dkMapCodec::Encode(const SymbolFile& source, const SymbolEncodeOptions& options) const
{
    SymbolEncodeResult result;
    const PreparedFile prepared = Prepare(source, _info, options, result.diagnostics);
    const SymbolFile& file = prepared.file;
    std::string out;
    size_t folded = 0;
    // z80asm writes the symbols in source order: the file's order is kept
    for (const SymbolSet& set : file.sets)
        for (const Symbol& s : set.symbols)
        {
            const std::optional<uint32_t> v = Value(s);
            if (!v)
            {
                result.diagnostics.push_back({Severity::Warning, 0, 0, s.name + ": " + s.location.space.Format() + " cannot be written (skipped)"});
                continue;
            }
            folded += IsPaged(s);
            auto has = [&s](const char* t) { return std::find(s.traits.begin(), s.traits.end(), t) != s.traits.end(); };
            const char* scope = has("global") ? "global" : has("extern") ? "extern" : s.exported ? "public" : "local";
            const std::string type = s.kind == SymbolKind::Const ? "const" : !s.provenance.type.empty() ? s.provenance.type : "addr";
            std::string name = s.name;
            if (name.size() < kZ88Column)
                name.append(kZ88Column - name.size(), ' ');
            std::string line = name + " = $" + Hex(*v, *v > 0xFFFF ? 8 : 4) + " ; " + type + ", " + scope + ", " + (has("def") ? "def" : "") + ", " +
                               s.module + ", " + s.section + ", " +
                               (s.source.line ? s.source.file + ":" + std::to_string(s.source.line) : s.source.file);
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
                line.pop_back();   // z80asm strips the line
            out += line + options.lineEnd;
            ++result.written;
        }
    if (folded)
        result.diagnostics.push_back({Severity::Info, 0, 0, std::to_string(folded) + " page symbol(s) written at CPU addresses (the format has no pages)"});
    return Finish(std::move(result), out);
}

// cspect-map --------------------------------------------------------------------------------------------------------

namespace
{
/// "HHHHHHHH LLLLLLLL TT NAME": the 16-bit address, the physical address (page * page size + offset), the type
bool CspectLine(std::string_view line, uint32_t& address, uint32_t& physical, uint32_t& type, std::string_view& name)
{
    if (line.size() < 22 || line[8] != ' ' || line[17] != ' ' || line[20] != ' ')
        return false;
    return text::ParseHex(line.substr(0, 8), address) && text::ParseHex(line.substr(9, 8), physical) && text::ParseHex(line.substr(18, 2), type) &&
           !(name = Trim(line.substr(21))).empty();
}

bool CspectAccepts(std::string_view line)
{
    uint32_t a = 0, p = 0, t = 0;
    std::string_view name;
    return CspectLine(line, a, p, t, name) && t <= 4;
}
}  // namespace

CspectMapCodec::CspectMapCodec()
    : _info(MakeInfo("cspect-map", "#CSpect map (sjasmplus CSPECTMAP)", Family::Text, {"map"}, [] {
          NameRules r = SjasmplusRules();
          r.upper = true;   // CSpect's names are in capitals
          return r;
      }(), true, ""))
{
}

int CspectMapCodec::Detect(const Probe& probe) const
{
    return ScoreProbe(probe, 95, CspectAccepts);
}

SymbolDecodeResult CspectMapCodec::Decode(std::span<const uint8_t> bytes) const
{
    SymbolDecodeResult result;
    SymbolSet set;
    const auto lines = text::Lines(bytes);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::string_view line = Trim(lines[i]);
        if (line.empty())
            continue;
        uint32_t address = 0, physical = 0, type = 0;
        std::string_view name;
        if (!CspectLine(line, address, physical, type, name))
        {
            Bad(result, i + 1, "not \"address physical type NAME\"");
            continue;
        }
        Symbol s;
        // CSpect writes a local label PARENT@LOCAL
        std::string full(name);
        const size_t at = full.rfind('@');
        if (at != std::string::npos && at > 0)
        {
            s.parent = full.substr(0, at);
            full[at] = '.';
            s.kind = SymbolKind::Local;
        }
        s.name = full;
        if (type == 1 || type == 2)
        {
            s.kind = SymbolKind::Const;   // EQU, DEFL
            PlaceValue(s, address);
        }
        else if (type == 0 && address <= 0xFFFF)
        {
            // A label: the physical address is the RAM page and the offset in it
            s.location.space.kind = SpaceKind::Ram;
            s.location.space.page = static_cast<uint16_t>(physical >> 14);
            s.location.offset = physical & 0x3FFF;
            s.window = static_cast<int>(address >> 14);
        }
        else
        {
            PlaceValue(s, address);   // a ROM page or no device (3), a structure (4)
            s.provenance.type = type == 4 ? "struct" : "";
        }
        Stamp(s, _info.id, line, i + 1);
        set.symbols.push_back(std::move(s));
    }
    result.file.sets.push_back(std::move(set));
    result.ok = true;
    return result;
}

SymbolEncodeResult CspectMapCodec::Encode(const SymbolFile& source, const SymbolEncodeOptions& options) const
{
    SymbolEncodeResult result;
    const PreparedFile prepared = Prepare(source, _info, options, result.diagnostics);
    std::string out;
    for (const SymbolSet& set : prepared.file.sets)
        for (const Symbol& s : set.symbols)
        {
            const std::optional<uint32_t> v = Value(s);
            if (!v)
            {
                result.diagnostics.push_back({Severity::Warning, 0, 0, s.name + ": " + s.location.space.Format() + " cannot be written (skipped)"});
                continue;
            }
            uint32_t physical = *v;
            uint32_t type = 3;   // a ROM page or no device
            if (s.kind == SymbolKind::Const)
                type = 1;
            else if (s.location.space.kind == SpaceKind::Ram && s.location.space.cpu == "main")
            {
                type = 0;
                physical = static_cast<uint32_t>(s.location.space.page) * 0x4000 + (s.location.offset & 0x3FFF);
            }
            else if (s.location.space.kind == SpaceKind::CpuView)
                type = s.provenance.type == "struct" ? 4 : 0;
            // A constant's second field is what sjasmplus made of it (SCREEN EQU #4000 gets 0): kept from the line read
            uint32_t a = 0, p = 0, t = 0;
            std::string_view n;
            if (s.kind == SymbolKind::Const && s.provenance.importer == _info.id && CspectLine(s.provenance.raw, a, p, t, n))
                physical = p;
            std::string name = s.name;
            if (s.kind == SymbolKind::Local && !s.parent.empty() && name.rfind(s.parent + ".", 0) == 0)
                name[s.parent.size()] = '@';
            out += Hex(*v, 8) + " " + Hex(physical, 8) + " " + Hex(type, 2) + " " + name + options.lineEnd;
            ++result.written;
        }
    return Finish(std::move(result), out);
}
}  // namespace unrealasm::symbols::codecs
