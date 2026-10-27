#include "codecs/masm/masmcodec.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

#include "encoding/utf8.h"

namespace unrealasm::codecs
{
namespace
{
constexpr char32_t kRawBase = 0xF700;   // a byte the format gives no character: U+F700 + byte
constexpr uint8_t kEnd = 0xFF;

// The keyword table common to every version (#80..#F6), read from the binaries and from MASM's own source (TAB_OPR in
// M1+): research-masm.md §4. A trailing blank is part of the keyword. #9E differs per version
constexpr std::array<std::string_view, 0x77> kCommon = {
    "A", "B", "C", "D", "E", "H", "L", "I", "R", "XH", "XL", "YH", "YL", "IX", "IY", "AF'",                          // 80
    "AF", "HL", "DE", "BC", "M", "NC", "NV", "NZ", "P", "PE", "PO", "V", "Z", "SP", "", "ORG ",                       // 90
    "PHASE ", "UNPHASE", "AND ", "ADC ", "SBC ", "ADD ", "SUB ", "XOR ", "OR ", "CP ", "LD ", "IM ",                  // A0
    "RST ", "EI", "DI", "EXX", "EXA", "INF", "LDIR", "LDDR", "OTIR", "OTDR", "OUTI", "OUTD",                          // AC
    "RETI", "RETN", "INIR", "INDR", "CPIR", "CPDR", "NEG", "CPD", "CPI", "IND", "INI", "LDD",                         // B8
    "LDI", "CCF", "CPL", "DAA", "HALT", "NOP", "RLA", "RLCA", "RRA", "RRCA", "SCF", "RLD",                            // C4
    "RRD", "EX ", "RET", "CALL ", "JP ", "PUSH ", "POP ", "INC ", "DEC ", "OUT ", "IN ", "DJNZ ",                     // D0
    "JR ", "BIT ", "RLC ", "RRC ", "RL ", "RR ", "SLA ", "SRA ", "SLI ", "SRL ", "RES ", "SET ",                       // DC
    "EQU ", "BEGIN ", "END", "INCBIN ", "INCLUDE ", "DB ", "DEFB ", "DEFS ", "DEFW ", "DS ", "DW ", "DOWN ",          // E8
    "UP ", "SYSTEM", "STOPKEY",                                                                                        // F4
};

/// The table of a version: "1.0" (the demo), "1.1" (1.1 / 1.3), "2.0", "3.0"
std::vector<std::string_view> Table(const std::string& version)
{
    std::vector<std::string_view> t(kCommon.begin(), kCommon.end());
    if (version == "1.0")
    {
        t[0x1E] = "{$ ";
        t.resize(0xF3 - 0x80);   // the demo's table ends at #F2, BEGIN / END differ and #F3 is its signature
        t[0xE9 - 0x80] = "BEGIN";
        t[0xEA - 0x80] = "END ";
        t.push_back("*AIG*'95 ");
    }
    else if (version == "2.0")
    {
        t[0x1E] = "*";
        for (const char* w : {"MAC ", "ENDM", "IF ", "ELSE", "ENDIF"})
            t.push_back(w);
    }
    else if (version == "3.0")
    {
        t[0x1E] = "{ ";
        for (const char* w : {"MAC", "ENDM", "BANK ", "BORDER ", "CLS", "IF ", "ELSE", "ENDIF"})
            t.push_back(w);
    }
    else
        t[0x1E] = "{ ";
    return t;
}

bool Framed1(const std::string& version)
{
    return version == "1.0" || version == "1.1";
}

/// The catalog start of a file each version saves (research-masm.md §2); 1.1 / 1.3 files carry #97xx
uint16_t NewFileStart(const std::string& version)
{
    return version == "1.0" ? 37178 : version == "2.0" ? 38106 : version == "3.0" ? 37155 : 38667;
}

struct Stream
{
    std::vector<std::span<const uint8_t>> bodies;
    size_t end = 0;        // offset after the end marker
    bool ended = false;
    bool unmarked = false; // the stream stops after its last line without #FF (the 1.0 demo's catalog length leaves it out)
    int cursor = -1;       // 3.0: the cursor line
};

Stream Split(std::span<const uint8_t> b, const std::string& version)
{
    Stream s;
    size_t p = 0;
    if (Framed1(version))
    {
        while (p < b.size())
        {
            const uint8_t n = b[p];
            if (n == kEnd)
            {
                s.end = p + 1;
                s.ended = true;
                return s;
            }
            if (n == 0)   // an empty line is one #00 (TASM writes #00 #00)
            {
                s.bodies.push_back({});
                ++p;
                continue;
            }
            if (p + n + 1 >= b.size() || b[p + n + 1] != n)
                return s;
            s.bodies.push_back(b.subspan(p + 1, n));
            p += n + 2;
        }
        if (p == b.size() && !s.bodies.empty())
        {
            s.end = p;
            s.ended = true;
            s.unmarked = true;
        }
        return s;
    }
    if (version == "3.0")
    {
        if (b.size() < 4 || b[0] != kEnd || b[3] != kEnd)
            return s;
        s.cursor = b[1] | b[2] << 8;
        p = 4;
    }
    else
    {
        if (b.empty() || b[0] != kEnd)
            return s;
        p = 1;
    }
    while (p < b.size())
    {
        if (b[p] == kEnd)
        {
            s.end = p + 1;
            s.ended = true;
            return s;
        }
        size_t q = p;
        while (q < b.size() && b[q] != 0 && b[q] != kEnd)
            ++q;
        if (q >= b.size() || b[q] != 0)
            return s;
        s.bodies.push_back(b.subspan(p, q - p));
        p = q + 1;
    }
    return s;
}

const std::string_view kLeftStop = "_.?@#\"";
const std::string_view kRightStop = "_.?@";

bool InStops(char c, std::string_view stops)
{
    return std::isalnum(static_cast<unsigned char>(c)) || stops.find(c) != std::string_view::npos;
}
}  // namespace

MasmCodec::MasmCodec()
    : _info{"masm",
            "MASM (Master Assembler) source (tokenized, TR-DOS type a)",
            "masm",
            CodecFamily::Tokenized,
            {{"1.0", "MASM 1.0 demo"}, {"1.1", "MASM 1.1 / 1.3"}, {"2.0", "MASM 2.0 TURBO"}, {"3.0", "MASM 3.0 MACRO"}}}
{
}

std::string MasmCodec::DecodeBody(std::span<const uint8_t> body, const std::string& version)
{
    const std::vector<std::string_view> table = Table(version);
    std::string text;
    for (size_t i = 0; i < body.size(); ++i)
    {
        const uint8_t c = body[i];
        if (Framed1(version) && c == 0x0A && i + 1 < body.size())
        {
            text.append(body[++i], ' ');
            continue;
        }
        if (!Framed1(version) && c >= 0x01 && c <= 0x1F)
            text.append(static_cast<size_t>(c) + 1, ' ');
        else if (c >= 0x20 && c <= 0x7F)
            text.push_back(static_cast<char>(c));
        else if (c >= 0x80 && static_cast<size_t>(c - 0x80) < table.size() && !table[c - 0x80].empty())
            text.append(table[c - 0x80]);
        else
            utf8::Append(text, kRawBase + c);
    }
    return text;
}

bool MasmCodec::EncodeBody(const std::string& source, const std::string& version, std::vector<uint8_t>& body, std::string& error)
{
    body.clear();
    std::string text = source;
    while (!text.empty() && text.back() == ' ')
        text.pop_back();
    const std::vector<std::string_view> table = Table(version);
    std::vector<std::pair<std::string_view, uint8_t>> words;
    for (size_t k = 0; k < table.size(); ++k)
        if (!table[k].empty())
            words.push_back({table[k], static_cast<uint8_t>(0x80 + k)});
    std::stable_sort(words.begin(), words.end(), [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });

    // MASM 2.0 tokenizes greedily, inside words too, everywhere but in the label field (research-masm.md §5.2)
    const bool greedy = version == "2.0";
    const size_t labelEnd = !greedy || (!text.empty() && text[0] == ' ') ? 0 : std::min(text.find(' '), text.size());
    auto blanks = [&](size_t count) {
        if (count == 1)
        {
            body.push_back(' ');
            return;
        }
        while (count > 0)
        {
            if (Framed1(version))
            {
                const size_t k = std::min<size_t>(count, 255);
                if (k > 1)
                    body.insert(body.end(), {0x0A, static_cast<uint8_t>(k)});
                else
                    body.push_back(' ');
                count -= k;
            }
            else
            {
                const size_t k = std::min<size_t>(count, 32);
                body.push_back(k > 1 ? static_cast<uint8_t>(k - 1) : static_cast<uint8_t>(' '));
                count -= k;
            }
        }
    };
    bool quoted = false;   // the demo keeps "..." as typed
    for (size_t i = 0; i < text.size();)
    {
        const char ch = text[i];
        const unsigned char u = static_cast<unsigned char>(ch);
        if (u >= 0x80)
        {
            // a private-use character: the byte it stands for
            char32_t cp = 0;
            const size_t length = utf8::DecodeOne(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()) + i, text.size() - i), cp);
            if (!length || cp < kRawBase || cp > kRawBase + 0xFF)
            {
                error = "column " + std::to_string(i + 1) + ": a character MASM cannot store";
                return false;
            }
            body.push_back(static_cast<uint8_t>(cp - kRawBase));
            i += length;
            continue;
        }
        if (version == "1.0" && ch == '"')
            quoted = !quoted;
        if (quoted || (version == "1.0" && ch == '"') || (greedy && i < labelEnd))
        {
            body.push_back(u);
            ++i;
            continue;
        }
        if (ch == ' ')
        {
            size_t j = i;
            while (j < text.size() && text[j] == ' ')
                ++j;
            blanks(j - i);
            i = j;
            continue;
        }
        if (greedy || i == 0 || !InStops(text[i - 1], kLeftStop))
        {
            bool hit = false;
            for (const auto& [word, code] : words)
            {
                if (text.compare(i, word.size(), word) != 0)
                    continue;
                const char next = i + word.size() < text.size() ? text[i + word.size()] : '\0';
                if (greedy || word.back() == ' ' || next == '\0' || !InStops(next, kRightStop))
                {
                    body.push_back(code);
                    i += word.size();
                    hit = true;
                    break;
                }
            }
            if (hit)
                continue;
        }
        if (u < 0x20)
        {
            error = "column " + std::to_string(i + 1) + ": a control character";
            return false;
        }
        body.push_back(u);
        ++i;
    }
    return true;
}

std::vector<std::string> MasmCodec::DetectVersions(std::span<const uint8_t> bytes, const CatalogHints& hints)
{
    std::vector<std::string> found;
    auto whole = [&](const std::string& v) { return Split(bytes, v).ended; };
    if (whole("3.0"))
        found.push_back("3.0");
    else if (whole("2.0"))
        found.push_back("2.0");
    const Stream s = Split(bytes, "1.1");
    if (s.ended && std::any_of(s.bodies.begin(), s.bodies.end(), [](const auto& b) { return !b.empty(); }))
    {
        // The demo and 1.1 / 1.3 share the framing: the catalog start decides (the demo's 37178), else 1.1
        if (hints.start == NewFileStart("1.0"))
            found.insert(found.end(), {"1.0", "1.1"});
        else
            found.insert(found.end(), {"1.1", "1.0"});
    }
    // A token above a version's table rules the version out
    std::vector<std::string> fitting;
    for (const std::string& v : found)
    {
        const size_t top = 0x80 + Table(v).size();
        bool fits = true;
        for (const auto& body : Split(bytes, v).bodies)
            for (const uint8_t c : body)
                fits = fits && (c < 0x80 || c < top);
        if (fits)
            fitting.push_back(v);
    }
    return fitting.empty() ? found : fitting;
}

int MasmCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    const std::vector<std::string> versions = DetectVersions(bytes, hints);
    if (versions.empty())
        return 0;
    // MASM saves type a; a 1.x file of another type is TASM's framing (type A, an empty line #00 #00)
    if (hints.type != 0 && hints.type != 'a')
        return 15;
    const bool start = hints.start == NewFileStart(versions[0]) || (Framed1(versions[0]) && (hints.start >> 8) == 0x97);
    if (hints.type == 'a' && start)
        return 95;
    if (hints.type == 'a')
        return 70;
    return versions[0] == "2.0" || versions[0] == "3.0" ? 55 : 45;
}

DecodeResult MasmCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeResult result;
    SourceDocument& document = result.document;
    document.format = _info.id;
    document.dialect = _info.dialect;
    document.codePage = encoding::CodePage::Ascii;
    document.lineEnd = encoding::LineEnd::Lf;
    if (!options.subversion.empty())
        result.subversions = {options.subversion};
    else
        result.subversions = DetectVersions(bytes, options.catalog);
    document.subversion = result.subversions.empty() ? std::string("1.1") : result.subversions.front();
    std::sort(result.subversions.begin(), result.subversions.end());
    const Stream s = Split(bytes, document.subversion);
    if (!s.ended)
        result.diagnostics.push_back({Severity::Error, static_cast<uint32_t>(s.bodies.size() + 1), 0, "the line framing does not reach an end marker"});
    for (const auto& body : s.bodies)
        document.lines.push_back({DecodeBody(body, document.subversion), {_info.id, std::vector<uint8_t>(body.begin(), body.end())}});
    // File attributes: the 3.0 cursor line (2 bytes, little endian), a flag byte (1 = no end marker) and the bytes
    // after the end marker
    std::vector<uint8_t> attrs;
    const int cursor = s.cursor < 0 ? 0 : s.cursor;
    attrs.push_back(static_cast<uint8_t>(cursor & 0xFF));
    attrs.push_back(static_cast<uint8_t>(cursor >> 8));
    attrs.push_back(s.unmarked ? 1 : 0);
    if (s.ended)
        attrs.insert(attrs.end(), bytes.begin() + static_cast<std::ptrdiff_t>(s.end), bytes.end());
    document.attrs = {_info.id, attrs};
    result.ok = s.ended;
    return result;
}

EncodeResult MasmCodec::Encode(const SourceDocument& document, const EncodeOptions& options) const
{
    EncodeResult result;
    const bool sameFormat = document.format == _info.id;
    const std::string version = !options.subversion.empty() ? options.subversion
                                : sameFormat && !document.subversion.empty() ? document.subversion : std::string("1.1");
    const bool keep = sameFormat && document.subversion == version;
    const bool attrs = keep && document.attrs.codec == _info.id && document.attrs.bytes.size() >= 3;
    std::vector<uint8_t>& out = result.bytes;
    if (!Framed1(version))
    {
        out.push_back(kEnd);
        if (version == "3.0")
            out.insert(out.end(), {attrs ? document.attrs.bytes[0] : uint8_t(0), attrs ? document.attrs.bytes[1] : uint8_t(0), kEnd});
    }
    std::vector<uint8_t> body;
    for (size_t i = 0; i < document.lines.size(); ++i)
    {
        const SourceLine& line = document.lines[i];
        if (keep && line.attrs.codec == _info.id && DecodeBody(line.attrs.bytes, version) == line.text)
            body = line.attrs.bytes;
        else
        {
            std::string error;
            if (!EncodeBody(line.text, version, body, error))
            {
                result.diagnostics.push_back({Severity::Error, static_cast<uint32_t>(i + 1), out.size(), error});
                continue;
            }
        }
        if (Framed1(version))
        {
            if (body.size() > 0xFE)
            {
                result.diagnostics.push_back({Severity::Error, static_cast<uint32_t>(i + 1), out.size(), "line longer than MASM stores"});
                continue;
            }
            if (body.empty())
                out.push_back(0);
            else
            {
                out.push_back(static_cast<uint8_t>(body.size()));
                out.insert(out.end(), body.begin(), body.end());
                out.push_back(static_cast<uint8_t>(body.size()));
            }
        }
        else
        {
            out.insert(out.end(), body.begin(), body.end());
            out.push_back(0);
        }
    }
    if (!(attrs && document.attrs.bytes[2] == 1 && Framed1(version)))
        out.push_back(kEnd);
    if (attrs)
        out.insert(out.end(), document.attrs.bytes.begin() + 3, document.attrs.bytes.end());
    result.ok = !HasErrors(result.diagnostics);
    return result;
}
}  // namespace unrealasm::codecs
