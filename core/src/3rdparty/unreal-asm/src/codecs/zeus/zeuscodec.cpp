#include "codecs/zeus/zeuscodec.h"

#include <algorithm>
#include <array>
#include <string_view>

#include "encoding/utf8.h"

namespace unrealasm::codecs
{
namespace
{
constexpr uint8_t kBlanks = 0x0A;
constexpr int kEndNumber = 0xFFFF;

// The 1983 reserved word table (#80-#E4), from the ZEUS code at #EE57 and the manual's Appendix 3 (research-zeus.md §3)
constexpr std::array<std::string_view, 101> k1983 = {
    "A", "ADC ", "ADD ", "AF'", "AF", "AND ", "B", "BC", "BIT ", "C", "CALL ", "CCF", "CP ", "CPD", "CPDR", "CPI",   // 80
    "CPIR", "CPL", "D", "DAA", "DE", "DEC ", "DEFB ", "DEFM ", "DEFS ", "DEFW ", "DI", "DISP ", "DJNZ ", "E", "EI",  // 90
    "ENT", "EQU ", "EX ", "EXX", "H", "HALT", "HL", "I", "IM ", "IN ", "INC ", "IND", "INDR", "INI", "INIR", "IX",   // 9F
    "IY", "JP ", "JR ", "L", "LD ", "LDD", "LDDR", "LDI", "LDIR", "M", "NC", "NEG", "NOP", "NV", "NZ", "OR ",        // AF
    "ORG ", "OTDR", "OTIR", "OUT ", "OUTD", "OUTI", "P", "PE", "PO", "POP ", "PUSH ", "R", "RES ", "RET", "RETI",     // BF
    "RETN", "RL ", "RLA", "RLC ", "RLCA", "RLD", "RR ", "RRA", "RRC ", "RRCA", "RRD", "RST ", "SBC ", "SCF", "SET ",  // CE
    "SLA ", "SP", "SRA ", "SRL ", "SUB ", "V", "XOR ", "Z",                                                           // DD
};

std::vector<std::string_view> Table(const std::string& version)
{
    std::vector<std::string_view> t(k1983.begin(), k1983.end());
    if (version == "gg" || version == "pht")
    {
        t[0x16] = "DB ";   // #96-#99 renamed by the later versions
        t[0x17] = "DM ";
        t[0x18] = "DS ";
        t[0x19] = "DW ";
        if (version == "gg")
            t.push_back("INCBIN ");
        else
            t.insert(t.end(), {"INCLUDE ", "PLACE "});
    }
    return t;
}

/// Word characters: 1983 and GG letters and digits; PHT digits and #3C-#7E
bool IsWord(uint8_t c, const std::string& version)
{
    if (c >= '0' && c <= '9')
        return true;
    if (version == "pht")
        return c >= 0x3C && c <= 0x7E;
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

struct Record
{
    int number = 0;
    std::span<const uint8_t> body;
};

struct Walk
{
    std::vector<Record> lines;
    bool ended = false;   // #FF #FF found
    size_t end = 0;       // after #FF #FF
};

Walk WalkRecords(std::span<const uint8_t> b)
{
    Walk w;
    size_t p = 0;
    int previous = -1;
    while (p + 2 <= b.size())
    {
        const int n = b[p] | (b[p + 1] << 8);
        if (n == kEndNumber)
        {
            w.ended = true;
            w.end = p + 2;
            return w;
        }
        if (n <= previous)
            return w;
        size_t e = p + 2;
        while (e < b.size() && b[e] != 0)   // ZEUS finds a line's end by its #00
            ++e;
        if (e >= b.size())
            return w;
        w.lines.push_back({n, b.subspan(p + 2, e - p - 2)});
        previous = n;
        p = e + 1;
    }
    return w;
}
}  // namespace

ZeusCodec::ZeusCodec()
    : _info{"zeus",
            "ZEUS source (tokenized)",
            "zeus",
            CodecFamily::Tokenized,
            {{"1983", "ZEUS 1983 (Crystal) and ports with its table"}, {"gg", "ZEUS (GG)"}, {"pht", "ZEUS 1.1 beta (PHT) / ZEUS v7.E"}}}
{
}

std::string ZeusCodec::DecodeBody(std::span<const uint8_t> body, const std::string& version)
{
    const std::vector<std::string_view> table = Table(version);
    std::string text;
    for (size_t i = 0; i < body.size(); ++i)
    {
        const uint8_t c = body[i];
        if (c == kBlanks && i + 1 < body.size())
        {
            const size_t n = body[++i];
            text.append(n == 0 ? 256 : n, ' ');
            continue;
        }
        if (c >= 0x80 && static_cast<size_t>(c - 0x80) < table.size())
            text += table[c - 0x80];
        else
            utf8::Append(text, encoding::ByteToCodePoint(c, encoding::CodePage::ZxSpectrum));
    }
    return text;
}

bool ZeusCodec::EncodeBody(const std::string& text, const std::string& version, std::vector<uint8_t>& body, std::string& error)
{
    body.clear();
    std::vector<uint8_t> buf;
    if (!encoding::FromUtf8(text, encoding::CodePage::ZxSpectrum, buf, error))
        return false;
    for (const uint8_t c : buf)
        if (c == 0)
        {
            error = "a zero byte ends a ZEUS line";
            return false;
        }
    const std::vector<std::string_view> table = Table(version);
    const size_t end = buf.size();
    buf.resize(end + 40, ' ');   // the screen line is padded with blanks
    size_t i = 0;
    size_t blanks = 0;
    while (i < buf.size())
    {
        const uint8_t c = buf[i];
        if (i >= end && c == ' ')
            break;
        if (c == ' ')
        {
            ++blanks;
            ++i;
            continue;
        }
        if (blanks == 1)
            body.push_back(' ');
        else if (blanks > 1)
        {
            // ZEUS counts blanks in one byte; a longer run is written in pieces
            while (blanks > 255)
            {
                body.insert(body.end(), {kBlanks, 255});
                blanks -= 255;
            }
            if (blanks == 1)
                body.push_back(' ');
            else
                body.insert(body.end(), {kBlanks, static_cast<uint8_t>(blanks)});
        }
        blanks = 0;
        // The first keyword of the table that matches wins
        size_t hit = table.size();
        for (size_t k = 0; k < table.size(); ++k)
        {
            const std::string_view kw = table[k];
            if (i + kw.size() > buf.size() || !std::equal(kw.begin(), kw.end(), buf.begin() + static_cast<std::ptrdiff_t>(i)))
                continue;
            if (kw.back() == ' ' || i + kw.size() >= buf.size() || !IsWord(buf[i + kw.size()], version))
            {
                hit = k;
                break;
            }
        }
        if (hit < table.size())
        {
            body.push_back(static_cast<uint8_t>(0x80 + hit));
            i += table[hit].size();
            continue;
        }
        if (IsWord(c, version))
        {
            while (i < end && IsWord(buf[i], version))
                body.push_back(buf[i++]);
            continue;
        }
        body.push_back(c);
        ++i;
        if (c == '"' || c == '#')
            while (i < end && IsWord(buf[i], version))
                body.push_back(buf[i++]);
    }
    return true;
}

std::vector<std::string> ZeusCodec::DetectVersions(std::span<const uint8_t> bytes)
{
    const Walk w = WalkRecords(bytes);
    if (!w.ended || w.lines.empty())
        return {};
    // A token past a version's table rules it out; then the tokenizer that reproduces most lines, ties in the order
    // 1983, pht, gg (#E5 is INCLUDE in pht and INCBIN in gg: the bytes cannot tell)
    struct Score
    {
        std::string version;
        size_t exact;
    };
    std::vector<Score> scores;
    for (const char* v : {"1983", "pht", "gg"})
    {
        const size_t top = 0x80 + Table(v).size();
        bool covers = true;
        for (const Record& r : w.lines)
            for (size_t i = 0; i < r.body.size(); ++i)
            {
                if (r.body[i] == kBlanks && i + 1 < r.body.size())
                {
                    ++i;
                    continue;
                }
                covers = covers && (r.body[i] < 0x80 || r.body[i] < top);
            }
        if (!covers)
            continue;
        size_t exact = 0;
        std::vector<uint8_t> body;
        std::string error;
        for (const Record& r : w.lines)
            exact += EncodeBody(DecodeBody(r.body, v), v, body, error) && std::equal(body.begin(), body.end(), r.body.begin(), r.body.end());
        scores.push_back({v, exact});
    }
    if (scores.empty())
        scores.push_back({"pht", 0});   // the largest table: decodes the most
    std::stable_sort(scores.begin(), scores.end(), [](const Score& a, const Score& b) { return a.exact > b.exact; });
    std::vector<std::string> out;
    for (const Score& s : scores)
        out.push_back(s.version);
    return out;
}

int ZeusCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    const Walk w = WalkRecords(bytes);
    if (!w.ended || w.lines.empty())
        return 0;
    int score = w.lines.size() >= 3 ? 70 : 35;
    if (w.end == bytes.size())
        score += 20;
    // What ZEUS's line entry writes: no control byte but a blank count, no token past the largest table, a line that
    // fits the editor (the corpus: at most 25 bytes; music data whose #00 / #FF #FF happen to frame "lines" fails this)
    constexpr size_t kLongest = 64;
    for (const Record& r : w.lines)
    {
        bool typical = r.body.size() <= kLongest;
        for (size_t i = 0; i < r.body.size() && typical; ++i)
        {
            if (r.body[i] == kBlanks && i + 1 < r.body.size())
                ++i;
            else
                typical = r.body[i] >= 0x20 && r.body[i] < 0x80 + 103;
        }
        if (!typical)
            return std::min(score, 15);
    }
    // TR-DOS ports save type C (ZEUS v7.E: Z)
    if (hints.type != 0 && hints.type != 'C' && hints.type != 'Z')
        score = std::min(score, 30);
    return score;
}

DecodeResult ZeusCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeResult result;
    SourceDocument& document = result.document;
    document.format = _info.id;
    document.dialect = _info.dialect;
    document.codePage = encoding::CodePage::ZxSpectrum;
    document.lineEnd = encoding::LineEnd::Lf;
    const Walk w = WalkRecords(bytes);
    result.subversions = options.subversion.empty() ? DetectVersions(bytes) : std::vector<std::string>{options.subversion};
    document.subversion = result.subversions.empty() ? std::string("1983") : result.subversions.front();
    std::sort(result.subversions.begin(), result.subversions.end());
    if (!w.ended)
    {
        result.diagnostics.push_back({Severity::Error, static_cast<uint32_t>(w.lines.size() + 1), 0, "the line records do not reach the #FF #FF end"});
        return result;
    }
    for (const Record& r : w.lines)
    {
        SourceLine line;
        line.text = DecodeBody(r.body, document.subversion);
        line.number = r.number;
        std::vector<uint8_t> canonical;
        std::string error;
        if (!EncodeBody(line.text, document.subversion, canonical, error) || !std::equal(canonical.begin(), canonical.end(), r.body.begin(), r.body.end()))
            line.attrs = {_info.id, std::vector<uint8_t>(r.body.begin(), r.body.end())};
        document.lines.push_back(std::move(line));
    }
    document.attrs = {_info.id, std::vector<uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(w.end), bytes.end())};
    result.ok = true;
    return result;
}

EncodeResult ZeusCodec::Encode(const SourceDocument& document, const EncodeOptions& options) const
{
    EncodeResult result;
    const bool sameFormat = document.format == _info.id;
    const std::string version = !options.subversion.empty() ? options.subversion
                                : sameFormat && !document.subversion.empty() ? document.subversion : std::string("1983");
    const bool keep = sameFormat && document.subversion == version;

    // The stored numbers when every line has one and they increase, else 10, 20, ... (ZEUS's own default)
    std::vector<int> numbers;
    int previous = -1;
    for (const SourceLine& line : document.lines)
    {
        const int n = line.number;
        if (n <= previous || n >= kEndNumber)
            break;
        numbers.push_back(n);
        previous = n;
    }
    if (numbers.size() != document.lines.size())
    {
        if (document.lines.size() >= static_cast<size_t>(kEndNumber))
        {
            result.diagnostics.push_back({Severity::Error, 0, 0, "more lines than ZEUS numbers (65535)"});
            return result;
        }
        const int step = document.lines.size() * 10 < static_cast<size_t>(kEndNumber) ? 10 : 1;
        numbers.clear();
        for (size_t i = 0; i < document.lines.size(); ++i)
            numbers.push_back(static_cast<int>(i + 1) * step);
        if (!document.lines.empty())
            result.diagnostics.push_back({Severity::Info, 0, 0, "lines numbered " + std::to_string(step) + ", " + std::to_string(2 * step) + ", ..."});
    }

    std::vector<uint8_t>& out = result.bytes;
    std::vector<uint8_t> body;
    for (size_t i = 0; i < document.lines.size(); ++i)
    {
        const SourceLine& line = document.lines[i];
        if (keep && line.attrs.codec == _info.id && !line.attrs.bytes.empty() && DecodeBody(line.attrs.bytes, version) == line.text)
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
        out.push_back(static_cast<uint8_t>(numbers[i] & 0xFF));
        out.push_back(static_cast<uint8_t>(numbers[i] >> 8));
        out.insert(out.end(), body.begin(), body.end());
        out.push_back(0);
    }
    out.insert(out.end(), {uint8_t(0xFF), uint8_t(0xFF)});
    if (keep && document.attrs.codec == _info.id)
        out.insert(out.end(), document.attrs.bytes.begin(), document.attrs.bytes.end());
    result.ok = !HasErrors(result.diagnostics);
    return result;
}
}  // namespace unrealasm::codecs
