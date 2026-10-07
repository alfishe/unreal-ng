#include "symbols/codecs/script/scriptcodecs.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>

#include "symbols/codecs/rules.h"
#include "symbols/codecs/text/textlines.h"
#include "symbols/codecs/text/tokenizer.h"

namespace unrealasm::symbols::codecs
{
namespace
{
using text::Hex;
using text::Trim;

std::optional<uint32_t> Value(const Symbol& s)
{
    if (s.location.space.kind == SpaceKind::Constant && s.location.space.cpu == "main")
        return s.location.offset;
    return CpuAddress(s);
}

bool IsPaged(const Symbol& s)
{
    const SpaceKind k = s.location.space.kind;
    return k == SpaceKind::Rom || k == SpaceKind::Ram || k == SpaceKind::Cache;
}

void PlaceValue(Symbol& s, uint32_t value)
{
    s.location.offset = value;
    if (value > 0xFFFF)
        s.location.space.kind = SpaceKind::Constant;
}

void Stamp(Symbol& s, const std::string& importer, std::string_view raw, size_t line)
{
    s.provenance.importer = importer;
    s.provenance.raw = std::string(raw);
    s.provenance.line = static_cast<uint32_t>(line);
}

/// A symbol the format cannot place is reported; a folded page symbol counted
template <typename Write>
void ForEachWritable(const SymbolFile& file, SymbolEncodeResult& result, Write write)
{
    size_t folded = 0;
    for (const SymbolSet& set : file.sets)
        for (const Symbol& s : set.symbols)
        {
            const std::optional<uint32_t> v = Value(s);
            if (!v || *v > 0xFFFF)
            {
                result.diagnostics.push_back({Severity::Warning, 0, 0, s.name + ": " + s.location.space.Format() + " cannot be written (skipped)"});
                continue;
            }
            folded += IsPaged(s);
            write(s, *v);
            ++result.written;
        }
    if (folded)
        result.diagnostics.push_back({Severity::Info, 0, 0, std::to_string(folded) + " page symbol(s) written at CPU addresses (the format has no pages)"});
}

SymbolEncodeResult Finish(SymbolEncodeResult result, const std::string& text)
{
    result.bytes.assign(text.begin(), text.end());
    result.ok = true;
    return result;
}

int Ratio(size_t matched, size_t data, int base)
{
    if (!matched)
        return 0;
    return matched * 2 >= data ? base : base / 3;
}

// IDA --------------------------------------------------------------------------------------------------------------

std::string QuoteC(const std::string& s)
{
    std::string out = "\"";
    for (const char c : s)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        if (c == '\n')
        {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "\"";
}

/// A quoted string at p (either quote, C escapes); false when there is none
bool ReadString(std::string_view line, size_t& p, std::string& out)
{
    while (p < line.size() && line[p] == ' ')
        ++p;
    if (p >= line.size() || (line[p] != '"' && line[p] != '\''))
        return false;
    const char quote = line[p++];
    out.clear();
    while (p < line.size() && line[p] != quote)
    {
        char c = line[p++];
        if (c == '\\' && p < line.size())
        {
            c = line[p++];
            c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
        }
        out += c;
    }
    if (p >= line.size())
        return false;
    ++p;
    return true;
}

struct IdaCall
{
    bool name = false;   // set_name / MakeName, else a comment call
    uint32_t address = 0;
    std::string text;
};

/// set_name(ea, "name"...), MakeName(ea, "name"), MakeNameEx(...), set_cmt(ea, "text", 0), MakeComm(ea, "text"), with an
/// optional module prefix (idc., ida_name., ida_bytes.)
std::optional<IdaCall> ParseIda(std::string_view line)
{
    static const std::pair<std::string_view, bool> kCalls[] = {{"set_name(", true},  {"MakeNameEx(", true}, {"MakeName(", true},
                                                               {"set_cmt(", false},  {"MakeComm(", false},  {"MakeRptCmt(", false}};
    for (const auto& [call, isName] : kCalls)
    {
        const size_t at = line.find(call);
        if (at == std::string_view::npos || (at > 0 && (std::isalnum(static_cast<unsigned char>(line[at - 1])) || line[at - 1] == '_')))
            continue;
        size_t p = at + call.size();
        const size_t comma = line.find(',', p);
        if (comma == std::string_view::npos)
            return std::nullopt;
        int64_t value = 0;
        if (!text::ParseNumber(Trim(line.substr(p, comma - p)), value) || value < 0 || value > 0xFFFFFFFF)
            return std::nullopt;
        p = comma + 1;
        IdaCall c;
        c.name = isName;
        c.address = static_cast<uint32_t>(value);
        if (!ReadString(line, p, c.text))
            return std::nullopt;
        return c;
    }
    return std::nullopt;
}

// Ghidra -----------------------------------------------------------------------------------------------------------

/// What LinuxSystemMapImportScript reads: "address type name" or "address name"
bool GhidraLine(std::string_view line, uint32_t& address, char& type, std::string_view& name)
{
    const std::vector<std::string_view> w = text::Words(line);
    if (w.size() == 3 && w[1].size() == 1)
    {
        type = w[1][0];
        name = w[2];
    }
    else if (w.size() == 2)
    {
        type = 0;
        name = w[1];
    }
    else
        return false;
    return text::ParseHex(w[0], address) && w[0].find_first_of("$xX") == std::string_view::npos;
}

// MAME -------------------------------------------------------------------------------------------------------------

bool MameLine(std::string_view line, uint32_t& address, std::string_view& text)
{
    size_t p = 0;
    if (line.rfind("comadd ", 0) == 0)
        p = 7;
    else if (line.rfind("commit ", 0) == 0)
        p = 7;
    else if (line.rfind("// ", 0) == 0)
        p = 3;
    else
        return false;
    const size_t comma = line.find(',', p);
    if (comma == std::string_view::npos)
        return false;
    text = Trim(line.substr(comma + 1));
    return !text.empty() && text::ParseHex(Trim(line.substr(p, comma - p)), address);
}
}  // namespace

// IDA --------------------------------------------------------------------------------------------------------------

IdaCodec::IdaCodec(bool python)
    : _python(python),
      _info(MakeInfo(python ? "ida-python" : "ida-idc", python ? "IDAPython script (idc.set_name)" : "IDA IDC script (set_name)", Family::Script,
                     python ? std::vector<std::string>{"py"} : std::vector<std::string>{"idc"}, IdaRules(), false, python ? "#" : "//"))
{
}

int IdaCodec::Detect(const Probe& probe) const
{
    const std::string_view all(reinterpret_cast<const char*>(probe.bytes.data()), probe.bytes.size());
    size_t data = 0;
    size_t calls = 0;
    for (const std::string_view raw : text::Lines(probe.bytes))
    {
        const std::string_view line = Trim(raw);
        if (line.empty() || line == "{" || line == "}" || line[0] == '#' || line.rfind("//", 0) == 0 || line.rfind("static ", 0) == 0 ||
            line.rfind("import ", 0) == 0)
            continue;
        ++data;
        calls += ParseIda(line).has_value();
    }
    const bool pythonFlavor = all.find("import idc") != std::string_view::npos || all.find("idc.set_name") != std::string_view::npos;
    const bool idcFlavor = all.find("static main") != std::string_view::npos || all.find("<idc.idc>") != std::string_view::npos;
    const int score = Ratio(calls, data, 90);
    if (pythonFlavor == idcFlavor)
        return score * 2 / 3;
    return _python == pythonFlavor ? score : score / 3;
}

SymbolDecodeResult IdaCodec::Decode(std::span<const uint8_t> bytes) const
{
    SymbolDecodeResult result;
    SymbolSet set;
    std::map<uint32_t, size_t> byAddress;
    const auto lines = text::Lines(bytes);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::string_view line = Trim(lines[i]);
        const std::optional<IdaCall> c = ParseIda(line);
        if (!c)
            continue;
        if (!c->name)
        {
            if (const auto it = byAddress.find(c->address); it != byAddress.end())
                set.symbols[it->second].comment = c->text;
            else
                result.diagnostics.push_back({Severity::Warning, static_cast<uint32_t>(i + 1), 0, "a comment at an address without a name (skipped)"});
            continue;
        }
        Symbol s;
        s.name = c->text;
        PlaceValue(s, c->address);
        Stamp(s, _info.id, line, i + 1);
        byAddress[c->address] = set.symbols.size();
        set.symbols.push_back(std::move(s));
    }
    result.file.sets.push_back(std::move(set));
    result.ok = true;
    return result;
}

SymbolEncodeResult IdaCodec::Encode(const SymbolFile& source, const SymbolEncodeOptions& options) const
{
    SymbolEncodeResult result;
    const PreparedFile prepared = Prepare(source, _info, options, result.diagnostics);
    const std::string& nl = options.lineEnd;
    std::string out = _info.comment + " Symbols written by unreal-ng" + nl;
    for (const std::string& line : prepared.header)
        out += line + nl;
    out += _python ? "import idc" + nl + nl : "#include <idc.idc>" + nl + nl + "static main()" + nl + "{" + nl;
    const std::string indent = _python ? "" : "    ";
    const std::string prefix = _python ? "idc." : "";
    const std::string end = _python ? "" : ";";
    ForEachWritable(prepared.file, result, [&](const Symbol& s, uint32_t v) {
        const std::string ea = "0x" + Hex(v, 4);
        out += indent + prefix + "set_name(" + ea + ", " + QuoteC(s.name) + ", " + prefix + "SN_NOWARN)" + end + nl;
        if (!s.comment.empty())
            out += indent + prefix + "set_cmt(" + ea + ", " + QuoteC(s.comment) + ", 0)" + end + nl;
    });
    if (!_python)
        out += "}" + nl;
    return Finish(std::move(result), out);
}

// Ghidra -----------------------------------------------------------------------------------------------------------

GhidraCodec::GhidraCodec()
    : _info(MakeInfo("ghidra", "Ghidra System.map import (address type name; LinuxSystemMapImportScript)", Family::Script, {}, WordRules(), false, ""))
{
}

int GhidraCodec::Detect(const Probe& probe) const
{
    size_t data = 0;
    size_t matched = 0;
    for (const std::string_view raw : text::Lines(probe.bytes))
    {
        const std::string_view line = Trim(raw);
        if (line.empty())
            continue;
        ++data;
        uint32_t a = 0;
        char t = 0;
        std::string_view n;
        matched += GhidraLine(line, a, t, n) && t != 0;   // the typed form only: "HHHH NAME" alone is many formats
    }
    text::LineScore score{data, matched};
    return score.Score(80);
}

SymbolDecodeResult GhidraCodec::Decode(std::span<const uint8_t> bytes) const
{
    SymbolDecodeResult result;
    SymbolSet set;
    const auto lines = text::Lines(bytes);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::string_view line = Trim(lines[i]);
        if (line.empty())
            continue;
        uint32_t address = 0;
        char type = 0;
        std::string_view name;
        if (!GhidraLine(line, address, type, name))
        {
            result.diagnostics.push_back({Severity::Warning, static_cast<uint32_t>(i + 1), 0, "not \"address [type] name\""});
            continue;
        }
        Symbol s;
        s.name = std::string(name);
        PlaceValue(s, address);
        // nm's letters: T text (a function for Ghidra), D / B / R data, A absolute; lower case is not exported
        switch (std::toupper(static_cast<unsigned char>(type)))
        {
            case 'T': s.kind = SymbolKind::Code; break;
            case 'D':
            case 'B':
            case 'R': s.kind = SymbolKind::Data; break;
            case 'A': s.kind = SymbolKind::Const; break;
            case 'L': s.kind = SymbolKind::Local; break;   // ours: a local label (a plain label for Ghidra)
            case 0:
            case '?': break;
            default: s.provenance.type = std::string(1, type); break;
        }
        s.exported = type != 0 && type != 'l' && std::isupper(static_cast<unsigned char>(type));
        Stamp(s, _info.id, line, i + 1);
        set.symbols.push_back(std::move(s));
    }
    result.file.sets.push_back(std::move(set));
    result.ok = true;
    return result;
}

SymbolEncodeResult GhidraCodec::Encode(const SymbolFile& source, const SymbolEncodeOptions& options) const
{
    SymbolEncodeResult result;
    const PreparedFile prepared = Prepare(source, _info, options, result.diagnostics);
    std::string out;
    ForEachWritable(prepared.file, result, [&](const Symbol& s, uint32_t v) {
        char type = '?';
        switch (s.kind)
        {
            case SymbolKind::Code:
            case SymbolKind::Entry: type = 'T'; break;
            case SymbolKind::Local: type = 'l'; break;   // a label, not a function (only t / T make one)
            case SymbolKind::Data: type = 'D'; break;
            case SymbolKind::Const: type = 'A'; break;
            case SymbolKind::Port:
            case SymbolKind::Unknown: type = s.provenance.type.size() == 1 ? s.provenance.type[0] : '?'; break;
        }
        if (type != '?' && type != 'l' && !s.exported && s.provenance.importer == _info.id)
            type = static_cast<char>(std::tolower(static_cast<unsigned char>(type)));
        std::string address = Hex(v, 4);
        std::transform(address.begin(), address.end(), address.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        out += address + " " + type + " " + s.name + options.lineEnd;
    });
    return Finish(std::move(result), out);
}

// MAME -------------------------------------------------------------------------------------------------------------

MameCodec::MameCodec()
    : _info(MakeInfo("mame", "MAME debugger comments (comadd HHHH,name)", Family::Script, {"cmd"}, [] {
          NameRules r;
          r.charset = Charset::Any;
          r.forbidden = ",;";   // the parameter and command separators
          return r;
      }(), false, ""))
{
}

int MameCodec::Detect(const Probe& probe) const
{
    size_t data = 0;
    size_t matched = 0;
    for (const std::string_view raw : text::Lines(probe.bytes))
    {
        const std::string_view line = Trim(raw);
        if (line.empty())
            continue;
        ++data;
        uint32_t a = 0;
        std::string_view t;
        matched += MameLine(line, a, t);
    }
    text::LineScore score{data, matched};
    return score.Score(90);
}

SymbolDecodeResult MameCodec::Decode(std::span<const uint8_t> bytes) const
{
    SymbolDecodeResult result;
    SymbolSet set;
    const auto lines = text::Lines(bytes);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::string_view line = Trim(lines[i]);
        uint32_t address = 0;
        std::string_view comment;
        if (line.empty() || !MameLine(line, address, comment))
        {
            if (!line.empty())
                result.diagnostics.push_back({Severity::Warning, static_cast<uint32_t>(i + 1), 0, "not \"comadd address,text\""});
            continue;
        }
        Symbol s;
        // "name - comment" as written by the encoder; a plain MAME comment becomes the name
        const size_t dash = comment.find(" - ");
        s.name = std::string(Trim(comment.substr(0, dash)));
        if (dash != std::string_view::npos)
            s.comment = std::string(Trim(comment.substr(dash + 3)));
        PlaceValue(s, address);
        Stamp(s, _info.id, line, i + 1);
        set.symbols.push_back(std::move(s));
    }
    result.file.sets.push_back(std::move(set));
    result.ok = true;
    return result;
}

SymbolEncodeResult MameCodec::Encode(const SymbolFile& source, const SymbolEncodeOptions& options) const
{
    SymbolEncodeResult result;
    const PreparedFile prepared = Prepare(source, _info, options, result.diagnostics);
    std::string out;
    ForEachWritable(prepared.file, result, [&](const Symbol& s, uint32_t v) {
        std::string comment = s.comment;
        std::replace(comment.begin(), comment.end(), ',', ' ');
        std::replace(comment.begin(), comment.end(), ';', ' ');
        out += "comadd " + Hex(v, 4) + "," + s.name + (comment.empty() ? "" : " - " + comment) + options.lineEnd;
    });
    return Finish(std::move(result), out);
}
}  // namespace unrealasm::symbols::codecs
