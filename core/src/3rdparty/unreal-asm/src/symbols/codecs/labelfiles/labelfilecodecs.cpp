#include "symbols/codecs/labelfiles/labelfilecodecs.h"

#include <algorithm>
#include <cctype>

#include "symbols/codecs/text/textlines.h"

namespace unrealasm::symbols::codecs
{
namespace
{
using text::Hex;
using text::ParseHex;
using text::Trim;

constexpr uint32_t kMaxRamPage = 255;   // the emulator's MAX_RAM_PAGES - 1
constexpr uint32_t kMaxRomPage = 127;   // MAX_ROM_PAGES - 1

/// Case-insensitive find of an ASCII word
size_t FindNoCase(std::string_view s, std::string_view word)
{
    for (size_t i = 0; i + word.size() <= s.size(); ++i)
    {
        bool same = true;
        for (size_t k = 0; k < word.size() && same; ++k)
            same = std::toupper(static_cast<unsigned char>(s[i + k])) == std::toupper(static_cast<unsigned char>(word[k]));
        if (same)
            return i;
    }
    return std::string_view::npos;
}

/// "; (TYPE) more" -> kind / type and the rest as the comment
void TypeAndComment(std::string_view comment, Symbol& s)
{
    if (!comment.empty() && comment.front() == '(')
    {
        const size_t close = comment.find(')');
        if (close != std::string_view::npos && text::ApplyTypeToken(comment.substr(0, close + 1), s))
            comment = Trim(comment.substr(close + 1));
    }
    s.comment = std::string(comment);
}

/// " ; (TYPE) comment" for the EQU-style formats ("" when there is neither)
std::string TrailingComment(const Symbol& s)
{
    const std::string type = text::TypeWord(s);
    if (type.empty() && s.comment.empty())
        return {};
    std::string out = " ;";
    if (!type.empty())
        out += " (" + text::Upper(type) + ")";
    if (!s.comment.empty())
        out += " " + s.comment;
    return out;
}

/// The 16-bit address a format without pages writes; `folded` when a page symbol lost its page
bool FoldedAddress(const Symbol& s, uint16_t& address, bool& folded, std::string& message)
{
    const std::optional<uint16_t> a = CpuAddress(s);
    if (!a)
    {
        message = s.name + ": " + s.location.space.Format() + " has no CPU address in this format (skipped)";
        return false;
    }
    address = *a;
    const SpaceKind kind = s.location.space.kind;
    folded = kind == SpaceKind::Rom || kind == SpaceKind::Ram || kind == SpaceKind::Cache;
    return true;
}

/// "HHHH NAME [(TYPE)]" after the comment is cut off (simple-sym, unreal-map without a page)
LineCodec::Line AddressNameType(std::string_view code, std::string_view comment, Symbol& out, std::string& message, bool pages)
{
    const std::vector<std::string_view> words = text::Words(code);
    if (words.size() < 2)
        return words.size() == 1 ? LineCodec::Line::Bad : LineCodec::Line::Skip;
    std::string_view address = words[0];
    const size_t colon = address.find(':');
    if (colon != std::string_view::npos)
    {
        if (!pages)
        {
            message = "an address with a page in a format without pages";
            return LineCodec::Line::Bad;
        }
        const std::string bank = text::Upper(address.substr(0, colon));
        address = address.substr(colon + 1);
        const bool ram = bank.rfind("RAM", 0) == 0;
        const bool rom = bank.rfind("ROM", 0) == 0;
        uint32_t page = 0;
        const size_t digit = bank.find_first_of("0123456789");
        bool number = digit != std::string::npos;
        for (size_t i = digit; number && i < bank.size(); ++i)
        {
            number = std::isdigit(static_cast<unsigned char>(bank[i])) != 0;
            page = page * 10 + static_cast<uint32_t>(bank[i] - '0');
            number = number && page <= 0xFFFF;
        }
        if (!(ram || rom) || !number)
        {
            message = "unknown bank \"" + bank + "\" (read as a CPU address)";
        }
        else if ((ram && page > kMaxRamPage) || (rom && page > kMaxRomPage))
        {
            message = "bank " + bank + " past the last page (read as page 0)";
            page = 0;
        }
        if ((ram || rom) && number)
        {
            out.location.space.kind = ram ? SpaceKind::Ram : SpaceKind::Rom;
            out.location.space.page = static_cast<uint16_t>(page);
        }
    }
    uint32_t value = 0;
    if (!ParseHex(address, value) || value > 0xFFFF)
    {
        message = "\"" + std::string(address) + "\" is no 16-bit hex address";
        return LineCodec::Line::Bad;
    }
    if (out.location.space.kind == SpaceKind::CpuView)
        out.location.offset = value;
    else
    {
        out.location.offset = value & 0x3FFF;
        out.window = static_cast<int>(value >> 14);
    }
    out.name = std::string(words[1]);
    if (words.size() >= 3)
        text::ApplyTypeToken(words[2], out);
    out.comment = std::string(comment);
    return LineCodec::Line::Symbol;
}
}  // namespace

int LineCodec::ScoreLines(const std::vector<std::string_view>& lines) const
{
    text::LineScore score;
    for (const std::string_view raw : lines)
    {
        const std::string_view line = Trim(raw);
        if (text::Skipped(line))
            continue;
        ++score.data;
        score.matched += Accepts(line);
    }
    return score.Score(50);
}

bool LineCodec::Accepts(std::string_view line) const
{
    Symbol s;
    std::string message;
    return ParseLine(line, s, message) == Line::Symbol;
}

int LineCodec::Detect(const Probe& probe) const
{
    std::vector<std::string_view> lines = text::Lines(probe.bytes);
    if (probe.bytes.size() >= SymbolCodecRegistry::kProbeBytes && lines.size() > 1)
        lines.pop_back();   // the probe may cut the last line
    const int score = ScoreLines(lines);
    if (score <= 0)
        return 0;
    const bool extension = std::find(_info.extensions.begin(), _info.extensions.end(), probe.extension) != _info.extensions.end();
    return std::min(100, score + (extension ? 20 : 0));
}

SymbolDecodeResult LineCodec::Decode(std::span<const uint8_t> bytes) const
{
    SymbolDecodeResult result;
    SymbolSet set;
    const std::vector<std::string_view> lines = text::Lines(bytes);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::string_view line = Trim(lines[i]);
        if (text::Skipped(line))
            continue;
        Symbol s;
        std::string message;
        const Line kind = ParseLine(line, s, message);
        const uint32_t number = static_cast<uint32_t>(i + 1);
        if (kind == Line::Bad)
        {
            result.diagnostics.push_back({Severity::Warning, number, 0, message.empty() ? "not a symbol line" : message});
            continue;
        }
        if (kind == Line::Skip)
            continue;
        if (!message.empty())
            result.diagnostics.push_back({Severity::Warning, number, 0, message});
        s.provenance.importer = _info.id;
        s.provenance.raw = std::string(line);
        s.provenance.line = number;
        set.symbols.push_back(std::move(s));
    }
    result.file.sets.push_back(std::move(set));
    result.ok = true;
    return result;
}

SymbolEncodeResult LineCodec::Encode(const SymbolFile& file, const SymbolEncodeOptions& options) const
{
    SymbolEncodeResult result;
    std::string out = Header(options.lineEnd);
    size_t folded = 0;
    for (const SymbolSet& set : file.sets)
        for (const Symbol& s : set.symbols)
        {
            bool pageLost = false;
            std::string message;
            const std::string line = WriteLine(s, pageLost, message);
            if (line.empty())
            {
                result.diagnostics.push_back({Severity::Warning, 0, 0, message});
                continue;
            }
            out += line + options.lineEnd;
            ++result.written;
            folded += pageLost;
        }
    if (folded)
        result.diagnostics.push_back({Severity::Info, 0, 0, std::to_string(folded) + " page symbol(s) written at CPU addresses (the format has no pages)"});
    result.bytes.assign(out.begin(), out.end());
    result.ok = true;
    return result;
}

// unreal-map --------------------------------------------------------------------------------------------------------

UnrealMapCodec::UnrealMapCodec() : LineCodec({"unreal-map", "unreal-ng map file ([ROMn:|RAMn:]HHHH NAME (TYPE))", Family::Text, {"map"}}) {}

LineCodec::Line UnrealMapCodec::ParseLine(std::string_view line, Symbol& out, std::string& message) const
{
    std::string_view code;
    std::string_view comment;
    text::SplitComment(line, code, comment);
    return AddressNameType(code, comment, out, message, true);
}

std::string UnrealMapCodec::WriteLine(const Symbol& s, bool& folded, std::string& message) const
{
    std::string address;
    const SpaceKind kind = s.location.space.kind;
    if (s.location.space.cpu == "main" && (kind == SpaceKind::Rom || kind == SpaceKind::Ram))
        address = (kind == SpaceKind::Rom ? "ROM" : "RAM") + std::to_string(s.location.space.page) + ":" + Hex(*CpuAddress(s), 4);
    else
    {
        uint16_t a = 0;
        if (!FoldedAddress(s, a, folded, message))
            return {};
        address = Hex(a, 4);
    }
    std::string out = address + "  " + s.name;
    const std::string type = text::TypeWord(s);
    if (!type.empty())
        out += "  (" + text::Upper(type) + ")";
    if (!s.comment.empty())
        out += " ; " + s.comment;
    return out;
}

int UnrealMapCodec::ScoreLines(const std::vector<std::string_view>& lines) const
{
    text::LineScore score;
    bool mapOnly = false;   // a bank prefix or a "(TYPE)": only a map file has them
    for (const std::string_view raw : lines)
    {
        const std::string_view line = Trim(raw);
        if (text::Skipped(line))
            continue;
        ++score.data;
        Symbol s;
        std::string message;
        if (ParseLine(line, s, message) == Line::Symbol)
        {
            ++score.matched;
            mapOnly = mapOnly || s.location.space.kind != SpaceKind::CpuView || s.kind != SymbolKind::Unknown || !s.provenance.type.empty();
        }
    }
    return score.Score(mapOnly ? 75 : 50);
}

// simple-sym --------------------------------------------------------------------------------------------------------

SimpleSymCodec::SimpleSymCodec() : LineCodec({"simple-sym", "simple symbol file (HHHH NAME)", Family::Text, {"sym"}}) {}

LineCodec::Line SimpleSymCodec::ParseLine(std::string_view line, Symbol& out, std::string& message) const
{
    std::string_view code;
    std::string_view comment;
    text::SplitComment(line, code, comment);
    return AddressNameType(code, comment, out, message, false);
}

std::string SimpleSymCodec::WriteLine(const Symbol& s, bool& folded, std::string& message) const
{
    uint16_t a = 0;
    if (!FoldedAddress(s, a, folded, message))
        return {};
    std::string out = Hex(a, 4) + " " + s.name;
    const std::string type = text::TypeWord(s);
    if (!type.empty())
        out += " (" + type + ")";
    if (!s.comment.empty())
        out += " ; " + s.comment;
    return out;
}

std::string SimpleSymCodec::Header(const std::string& nl) const
{
    return "; Labels exported by unreal-ng" + nl + "; Format: ADDR NAME [(TYPE)] [; COMMENT]" + nl + nl;
}

// unreal-l ----------------------------------------------------------------------------------------------------------

UnrealLCodec::UnrealLCodec() : LineCodec({"unreal-l", "Unreal user.l (HHHH name, PP:HHHH name; RAM pages)", Family::Text, {"l"}}) {}

LineCodec::Line UnrealLCodec::ParseLine(std::string_view line, Symbol& out, std::string& message) const
{
    // Unreal's MON_LABELS::load: "xxxx label" (the linear RAM address) or "bb:xxxx label"
    uint32_t value = 0;
    uint32_t page = 0;
    std::string_view name;
    if (line.size() >= 6 && line[4] == ' ' && ParseHex(line.substr(0, 4), value) && line.substr(0, 4).find_first_of("$xX") == std::string_view::npos)
    {
        page = value >> 14;
        name = line.substr(5);
    }
    else if (line.size() >= 9 && line[2] == ':' && line[7] == ' ' && ParseHex(line.substr(0, 2), page) && ParseHex(line.substr(3, 4), value) &&
             line.substr(0, 7).find_first_of("$xX") == std::string_view::npos)
    {
        out.window = static_cast<int>(value >> 14);
        name = line.substr(8);
    }
    else
    {
        message = "not \"HHHH name\" or \"PP:HHHH name\"";
        return Line::Bad;
    }
    name = Trim(name);
    if (name.empty())
    {
        message = "no name";
        return Line::Bad;
    }
    out.location.space.kind = SpaceKind::Ram;
    out.location.space.page = static_cast<uint16_t>(page);
    out.location.offset = value & 0x3FFF;
    out.name = std::string(name);
    return Line::Symbol;
}

std::string UnrealLCodec::WriteLine(const Symbol& s, bool& folded, std::string& message) const
{
    (void)folded;
    const AddressSpace& space = s.location.space;
    if (space.cpu != "main" || space.kind != SpaceKind::Ram || space.page > 0xFF)
    {
        message = s.name + ": user.l holds RAM pages 0-255 only (" + space.Format() + " skipped)";
        return {};
    }
    return Hex(space.page, 2) + ":" + Hex(*CpuAddress(s), 4) + " " + s.name;
}

int UnrealLCodec::ScoreLines(const std::vector<std::string_view>& lines) const
{
    text::LineScore score;
    bool paged = false;
    for (const std::string_view raw : lines)
    {
        const std::string_view line = Trim(raw);
        if (text::Skipped(line))
            continue;
        ++score.data;
        if (Accepts(line))
        {
            ++score.matched;
            paged = paged || (line.size() > 2 && line[2] == ':');
        }
    }
    return score.Score(paged ? 75 : 50);
}

// vice --------------------------------------------------------------------------------------------------------------

ViceCodec::ViceCodec() : LineCodec({"vice", "VICE label file (al C:HHHH .name)", Family::Text, {"vice", "lbl"}}) {}

LineCodec::Line ViceCodec::ParseLine(std::string_view line, Symbol& out, std::string& message) const
{
    if (line.substr(0, 3) != "al ")
        return Line::Skip;
    const std::vector<std::string_view> words = text::Words(line);
    if (words.size() < 3)
    {
        message = "\"al\" without an address and a name";
        return Line::Bad;
    }
    std::string_view address = words[1];
    if (address.size() > 2 && address[1] == ':')
        address.remove_prefix(2);   // the memory space letter ("C:")
    uint32_t value = 0;
    if (!ParseHex(address, value) || value > 0xFFFF)
    {
        message = "\"" + std::string(words[1]) + "\" is no 16-bit hex address";
        return Line::Bad;
    }
    std::string_view name = words[2];
    if (name.size() > 1 && name.front() == '.')
        name.remove_prefix(1);   // VICE writes a label as .name
    out.location.offset = value;
    out.name = std::string(name);
    if (words.size() >= 4)
        text::ApplyTypeToken(words[3], out);
    return Line::Symbol;
}

std::string ViceCodec::WriteLine(const Symbol& s, bool& folded, std::string& message) const
{
    uint16_t a = 0;
    if (!FoldedAddress(s, a, folded, message))
        return {};
    return "al C:" + Hex(a, 4) + " ." + s.name;
}

int ViceCodec::ScoreLines(const std::vector<std::string_view>& lines) const
{
    text::LineScore score;
    for (const std::string_view raw : lines)
    {
        const std::string_view line = Trim(raw);
        if (text::Skipped(line))
            continue;
        ++score.data;
        score.matched += Accepts(line);
    }
    return score.Score(90);
}

// sjasm-equ ---------------------------------------------------------------------------------------------------------

SjasmEquCodec::SjasmEquCodec() : LineCodec({"sjasm-equ", "sjasm symbol file (NAME EQU $HHHH)", Family::Text, {"s", "asm", "equ"}}) {}

LineCodec::Line SjasmEquCodec::ParseLine(std::string_view line, Symbol& out, std::string& message) const
{
    const size_t equ = FindNoCase(line, " EQU ");
    if (equ == std::string_view::npos)
        return Line::Skip;
    std::string_view name = Trim(line.substr(0, equ));
    if (!name.empty() && name.back() == ':')
        name.remove_suffix(1);
    std::string_view value;
    std::string_view comment;
    text::SplitComment(line.substr(equ + 5), value, comment);
    uint32_t number = 0;
    if (name.empty() || !ParseHex(value, number) || number > 0xFFFF)
    {
        message = "\"" + std::string(value) + "\" is no 16-bit hex value";
        return Line::Bad;
    }
    out.name = std::string(name);
    out.location.offset = number;
    TypeAndComment(comment, out);
    return Line::Symbol;
}

std::string SjasmEquCodec::WriteLine(const Symbol& s, bool& folded, std::string& message) const
{
    uint16_t a = 0;
    if (!FoldedAddress(s, a, folded, message))
        return {};
    return s.name + " EQU $" + Hex(a, 4) + TrailingComment(s);
}

int SjasmEquCodec::ScoreLines(const std::vector<std::string_view>& lines) const
{
    text::LineScore score;
    for (const std::string_view raw : lines)
    {
        const std::string_view line = Trim(raw);
        if (text::Skipped(line))
            continue;
        ++score.data;
        score.matched += Accepts(line);
    }
    return score.Score(85);
}

// z88dk-defc --------------------------------------------------------------------------------------------------------

Z88dkDefcCodec::Z88dkDefcCodec() : LineCodec({"z88dk-defc", "z88dk DEFC file (DEFC name = $HHHH)", Family::Text, {"z88", "def"}}) {}

LineCodec::Line Z88dkDefcCodec::ParseLine(std::string_view line, Symbol& out, std::string& message) const
{
    if (FindNoCase(line.substr(0, 5), "DEFC ") != 0)
        return Line::Skip;
    const size_t equals = line.find('=');
    if (equals == std::string_view::npos)
    {
        message = "DEFC without '='";
        return Line::Bad;
    }
    const std::string_view name = Trim(line.substr(5, equals - 5));
    std::string_view value;
    std::string_view comment;
    text::SplitComment(line.substr(equals + 1), value, comment);
    uint32_t number = 0;
    if (name.empty() || !ParseHex(value, number) || number > 0xFFFF)
    {
        message = "\"" + std::string(value) + "\" is no 16-bit hex value";
        return Line::Bad;
    }
    out.name = std::string(name);
    out.location.offset = number;
    TypeAndComment(comment, out);
    return Line::Symbol;
}

std::string Z88dkDefcCodec::WriteLine(const Symbol& s, bool& folded, std::string& message) const
{
    uint16_t a = 0;
    if (!FoldedAddress(s, a, folded, message))
        return {};
    return "DEFC " + s.name + " = $" + Hex(a, 4) + TrailingComment(s);
}

int Z88dkDefcCodec::ScoreLines(const std::vector<std::string_view>& lines) const
{
    text::LineScore score;
    for (const std::string_view raw : lines)
    {
        const std::string_view line = Trim(raw);
        if (text::Skipped(line))
            continue;
        ++score.data;
        score.matched += Accepts(line);
    }
    return score.Score(90);
}
}  // namespace unrealasm::symbols::codecs
