#include "codecs/zxasm/zxasmcodec.h"

#include <algorithm>
#include <array>
#include <string_view>

#include "encoding/utf8.h"

namespace unrealasm::codecs
{
namespace
{
constexpr char32_t kRawBase = 0xF700;   // a byte with no character: U+F700 + byte
constexpr uint8_t kLineEnd = 0x0D;
constexpr uint8_t kBlankRun = 0x06;
constexpr size_t kMaxRun = 127;

// The keyword index of ZX-ASM 3.10 (#20 + index), the za_3.10 binary's table; 3.15 and later add the last seven,
// Lite 1.07 the first four of them (research-zxasm.md §3)
constexpr std::array<std::string_view, 173> kKeywords = {
    "ld", "ex", "im", "rst", "ret", "add", "adc", "sub", "sbc", "and", "xor", "or", "cp", "push", "pop", "inc",
    "dec", "in", "out", "jp", "call", "jr", "djnz", "rlc", "rrc", "rl", "rr", "sla", "sra", "sli", "srl", "bit",
    "res", "set", "nop", "halt", "di", "ei", "rlca", "rla", "rrca", "rra", "exx", "daa", "cpl", "ccf", "scf", "ldi",
    "ldir", "ldd", "lddr", "cpi", "cpir", "cpd", "cpdr", "neg", "inf", "ini", "inir", "ind", "indr", "outi", "otir", "outd",
    "otdr", "reti", "retn", "rld", "rrd", "org", "equ", "db", "dw", "ds", "defb", "defw", "defs", "insert", "include", "if",
    "ifdef", "ifndef", "ifused", "ifnused", "else", "endif", "make", "b", "c", "d", "e", "h", "l", "(hl)", "a", "xh",
    "xl", "yh", "yl", "(ix", "(iy", "(bc)", "(de)", "i", "r", "af", "bc", "de", "hl", "ix", "iy", "sp",
    "(sp)", "af'", "(c)", "nz", "z", "nc", "c", "po", "pe", "p", "m", "phase", "unphase", "dc", "ent", "rept",
    "endr", "loadtab", "macro", "endm", "create", "makelab", "saveobj", "exitm", "ifp", "exa", "retz", "retnz", "retc", "retnc", "retm", "retp",
    "retpo", "retpe", "jpz", "jpnz", "jpc", "jpnc", "jpm", "jpp", "jppo", "jppe", "callz", "callc", "callm", "callpe", "callnz", "callnc",
    "callp", "callpo", "jrz", "jrnz", "jrc", "jrnc", "project", "public", "enda", "dbw", "repl", "loadobj", "chd",
};

struct Version
{
    std::string_view id;
    std::string_view title;
    size_t keywords;   ///< 0: no tokens (2.x)
};

constexpr std::array<Version, 4> kVersions = {{
    {"2", "ZX-ASM 2.4-2.6 (Hohlov): text, blank runs", 0},
    {"3.0", "ZX-ASM 3.0, 3.01, 3.10 (Afendikov, Rubtsov)", 166},
    {"lite", "ZX ASM Lite 1.07 (Rubts0FF)", 170},
    {"3.15", "ZAsm 3.15-4.20 (Rubts0FF)", 173},
}};

const Version* FindVersion(std::string_view id)
{
    for (const Version& v : kVersions)
        if (v.id == id)
            return &v;
    return nullptr;
}

bool IsAsciiAlnum(char32_t c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/// A character of a label, number or name (Cyrillic letters included)
bool IsIdent(char32_t c)
{
    return IsAsciiAlnum(c) || c == '_' || c == '.' || c == '?' || c == '@' || c == '$' || (c >= 0x0400 && c <= 0x04FF);
}

char32_t Lower(char32_t c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

std::vector<char32_t> ToCodePoints(const std::string& text, std::string& error)
{
    std::vector<char32_t> out;
    const auto* data = reinterpret_cast<const uint8_t*>(text.data());
    for (size_t i = 0; i < text.size();)
    {
        char32_t cp = 0;
        const size_t length = utf8::DecodeOne(std::span<const uint8_t>(data + i, text.size() - i), cp);
        if (length == 0)
        {
            error = "invalid UTF-8 at column " + std::to_string(out.size() + 1);
            return {};
        }
        out.push_back(cp);
        i += length;
    }
    return out;
}

bool PutChar(std::vector<uint8_t>& out, char32_t cp, size_t column, std::string& error)
{
    uint8_t b = 0;
    if (cp >= kRawBase && cp <= kRawBase + 0xFF)
        b = static_cast<uint8_t>(cp - kRawBase);
    else if (cp < 0x80)
        b = static_cast<uint8_t>(cp);
    else if (!encoding::CodePointToByte(cp, encoding::CodePage::Cp866, b))
    {
        error = "a character ZX-ASM cannot hold (not in CP866) at column " + std::to_string(column + 1);
        return false;
    }
    out.push_back(b);
    return true;
}

void PutBlanks(std::vector<uint8_t>& out, size_t count)
{
    while (count >= 2)
    {
        const size_t chunk = std::min(count, kMaxRun);
        out.push_back(kBlankRun);
        out.push_back(static_cast<uint8_t>(0x80 + chunk));
        count -= chunk;
    }
    if (count == 1)
        out.push_back(' ');
}

/// The keyword starting at t[i], first in table order; -1 when none
int MatchKeyword(const std::vector<char32_t>& t, size_t i, size_t keywords)
{
    for (size_t k = 0; k < keywords; ++k)
    {
        const std::string_view name = kKeywords[k];
        if (i + name.size() > t.size())
            continue;
        bool same = true;
        for (size_t j = 0; j < name.size() && same; ++j)
            same = Lower(t[i + j]) == static_cast<char32_t>(name[j]);
        if (!same)
            continue;
        const size_t end = i + name.size();
        const bool nameEndsInLetter = IsAsciiAlnum(static_cast<char32_t>(name.back()));
        if (nameEndsInLetter && end < t.size() && (IsAsciiAlnum(t[end]) || t[end] == '_'))
            continue;
        return static_cast<int>(k);
    }
    return -1;
}
}  // namespace

ZxasmCodec::ZxasmCodec() : _info{"zxasm", "ZX-ASM / ZAsm source (tokenized)", "zxasm", CodecFamily::Tokenized, {}}
{
    for (const Version& v : kVersions)
        _info.subversions.push_back({std::string(v.id), std::string(v.title)});
}

std::string ZxasmCodec::DecodeLine(std::span<const uint8_t> line, const std::string& versionId)
{
    const Version* version = FindVersion(versionId);
    const size_t keywords = version ? version->keywords : kKeywords.size();
    std::string text;
    for (size_t i = 0; i < line.size();)
    {
        const uint8_t b = line[i];
        if (b == kBlankRun && i + 1 < line.size())
        {
            text.append(line[i + 1] & 0x7F, ' ');
            i += 2;
            continue;
        }
        if (b >= 0x02 && b <= 0x05 && i + 1 < line.size() && line[i + 1] >= 0x20 && static_cast<size_t>(line[i + 1] - 0x20) < keywords)
        {
            const std::string_view name = kKeywords[line[i + 1] - 0x20];
            const unsigned flags = b - 2;
            for (const char c : name)
                text.push_back((flags & 1) && c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c);
            if (flags & 2)
                text.push_back(' ');
            i += 2;
            continue;
        }
        if (b < 0x20)
            utf8::Append(text, kRawBase + b);   // a control code (colours, arrows in strings) or a keyword this version lacks
        else if (b < 0x80)
            text.push_back(static_cast<char>(b));
        else
            utf8::Append(text, encoding::ByteToCodePoint(b, encoding::CodePage::Cp866));
        ++i;
    }
    return text;
}

bool ZxasmCodec::EncodeLine(const std::string& text, const std::string& versionId, std::vector<uint8_t>& out, std::string& error)
{
    out.clear();
    const Version* version = FindVersion(versionId);
    const size_t keywords = version ? version->keywords : kKeywords.size();
    const bool tokens = keywords > 0;
    const std::vector<char32_t> t = ToCodePoints(text, error);
    if (!error.empty())
        return false;
    const size_t n = t.size();
    size_t i = 0;
    // The label field: a name from column 0, never a keyword
    for (; i < n && IsIdent(t[i]); ++i)
        if (!PutChar(out, t[i], i, error))
            return false;
    bool inComment = false, afterKeyword = false;
    while (i < n)
    {
        const char32_t c = t[i];
        const bool wasKeyword = afterKeyword;
        afterKeyword = false;
        if (c == ' ')
        {
            size_t j = i;
            while (j < n && t[j] == ' ')
                ++j;
            PutBlanks(out, j - i);
            i = j;
            continue;
        }
        // A string ("..." or '...'; an apostrophe right after a keyword belongs to it: AF', BC'): literal, blank runs
        // compressed, except the blank right after the opening quote (3.0 and later)
        if ((c == '"' || c == '\'') && !(c == '\'' && wasKeyword))
        {
            const char32_t quote = c;
            out.push_back(static_cast<uint8_t>(quote));
            ++i;
            if (tokens && i < n && t[i] == ' ')
                out.push_back(' '), ++i;
            while (i < n && t[i] != quote)
            {
                if (t[i] == ' ')
                {
                    size_t j = i;
                    while (j < n && t[j] == ' ')
                        ++j;
                    PutBlanks(out, j - i);
                    i = j;
                }
                else if (!PutChar(out, t[i], i, error))
                    return false;
                else
                    ++i;
            }
            if (i < n)
                out.push_back(static_cast<uint8_t>(quote)), ++i;
            continue;
        }
        if (c == ';' && !inComment)
        {
            out.push_back(';');
            inComment = true;   // comments are tokenized like the rest
            ++i;
            continue;
        }
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (tokens && (letter || c == '('))
        {
            const char32_t prev = i > 0 ? t[i - 1] : ' ';
            const int k = (c == '(' || !(IsIdent(prev) || prev == '(' || prev == '~')) ? MatchKeyword(t, i, keywords) : -1;
            if (k >= 0)
            {
                const size_t end = i + kKeywords[k].size();
                const char32_t first = c == '(' && i + 1 < n ? t[i + 1] : c;
                const bool capitals = first >= 'A' && first <= 'Z';
                const bool blank = end < n && t[end] == ' ';
                out.push_back(static_cast<uint8_t>(2 + (capitals ? 1 : 0) + (blank ? 2 : 0)));
                out.push_back(static_cast<uint8_t>(0x20 + k));
                i = end + (blank ? 1 : 0);
                afterKeyword = !blank;
                continue;
            }
            if (c == '(')
            {
                out.push_back('(');
                ++i;
                continue;
            }
        }
        if (IsIdent(c) || c == '#')
        {
            size_t j = i + 1;
            while (j < n && IsIdent(t[j]))
                ++j;
            for (; i < j; ++i)
                if (!PutChar(out, t[i], i, error))
                    return false;
            continue;
        }
        if (!PutChar(out, c, i, error))
            return false;
        ++i;
    }
    return true;
}

std::string ZxasmCodec::DetectVersion(std::span<const uint8_t> bytes, const CatalogHints& hints, std::vector<std::string>* consistent)
{
    // The TR-DOS catalog: 2.x saves type C at #A135-#A1DF (2.4, 2.5) or #2020 (2.6); 3.0 type C at 35151; 3.01 type z
    // with extension "as"; 3.10 and later type a with extension "sm"
    std::vector<std::string> allowed;
    if (hints.type == 'C' && ((hints.start >= 0xA135 && hints.start <= 0xA1DF) || hints.start == 0x2020))
        allowed = {"2"};
    else if ((hints.type == 'C' && hints.start == 35151) || (hints.type == 'z' && hints.start == 0x7361))
        allowed = {"3.0"};
    else if (hints.type == 'a' && hints.start == 0x6D73)
        allowed = {"3.0", "lite", "3.15"};
    else
        allowed = {"2", "3.0", "lite", "3.15"};
    // The evidence: lines a version's editor writes back unchanged with no byte it gives no meaning
    std::vector<std::span<const uint8_t>> lines;
    for (size_t start = 0, i = 0; i <= bytes.size(); ++i)
        if (i == bytes.size() || bytes[i] == kLineEnd)
        {
            lines.push_back(bytes.subspan(start, i - start));
            start = i + 1;
        }
    std::vector<size_t> exact;
    std::vector<uint8_t> again;
    std::string error;
    for (const std::string& id : allowed)
    {
        size_t count = 0;
        for (const auto& line : lines)
        {
            const std::string text = DecodeLine(line, id);
            const bool raw = text.find("\xEF\x9C") != std::string::npos;   // U+F700-U+F73F: a control code or unknown keyword
            count += !raw && EncodeLine(text, id, again, error) && std::equal(again.begin(), again.end(), line.begin(), line.end());
        }
        exact.push_back(count);
    }
    const size_t best = *std::max_element(exact.begin(), exact.end());
    std::string chosen;
    for (size_t i = 0; i < allowed.size(); ++i)
        if (exact[i] == best)
        {
            chosen = allowed[i];
            if (consistent)
                consistent->push_back(chosen);
        }
    return chosen;
}

int ZxasmCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    if (bytes.empty())
        return 0;
    const bool catalog = (hints.type == 'C' && ((hints.start >= 0xA135 && hints.start <= 0xA1DF) || hints.start == 0x2020 || hints.start == 35151)) ||
                         (hints.type == 'z' && hints.start == 0x7361) || (hints.type == 'a' && hints.start == 0x6D73);
    // The bytes: CR lines, blank runs #06 #8x and keyword pairs, nothing else below #20 except a few control codes
    size_t runs = 0, keywords = 0, crs = 0, odd = 0;
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        const uint8_t b = bytes[i];
        if (b == kLineEnd)
            ++crs;
        else if (b == kBlankRun && i + 1 < bytes.size() && bytes[i + 1] >= 0x80)
            ++runs, ++i;
        else if (b >= 0x02 && b <= 0x05 && i + 1 < bytes.size() && bytes[i + 1] >= 0x20 && bytes[i + 1] < 0x20 + kKeywords.size())
            ++keywords, ++i;
        else if (b < 0x20)
            ++odd;   // control codes occur inside strings (colours, arrows), but rarely
    }
    if (odd * 100 > bytes.size() || crs == 0)
        return catalog ? 40 : 0;
    if (catalog)
        return 95;
    // ZAsm 3.10+ keeps the last two characters of a file's extension in the start field ("A315.lbl": type l, "bl"), so
    // its texts come with any type; another start (a magazine text at #C000, a screen) is no ZX-ASM source
    auto extensionChar = [](uint8_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_'; };
    if (hints.type != 0 && !(extensionChar(static_cast<uint8_t>(hints.start & 0xFF)) && extensionChar(static_cast<uint8_t>(hints.start >> 8))))
        return 20;
    if (keywords > 0)
        return 80;
    return runs > 0 ? 62 : 20;
}

DecodeResult ZxasmCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeResult result;
    SourceDocument& document = result.document;
    document.format = _info.id;
    document.dialect = _info.dialect;
    document.codePage = encoding::CodePage::Cp866;
    document.lineEnd = encoding::LineEnd::Cr;
    if (!options.subversion.empty() && FindVersion(options.subversion))
    {
        document.subversion = options.subversion;
        result.subversions = {options.subversion};
    }
    else
        document.subversion = DetectVersion(bytes, options.catalog, &result.subversions);
    size_t start = 0;
    for (size_t i = 0; i < bytes.size(); ++i)
        if (bytes[i] == kLineEnd)
        {
            const auto line = bytes.subspan(start, i - start);
            document.lines.push_back({DecodeLine(line, document.subversion), {_info.id, std::vector<uint8_t>(line.begin(), line.end())}});
            start = i + 1;
        }
    const bool unterminated = start < bytes.size();
    if (unterminated)
    {
        const auto line = bytes.subspan(start);
        document.lines.push_back({DecodeLine(line, document.subversion), {_info.id, std::vector<uint8_t>(line.begin(), line.end())}});
    }
    document.attrs = {_info.id, {static_cast<uint8_t>(unterminated ? 1 : 0)}};
    result.ok = true;
    return result;
}

EncodeResult ZxasmCodec::Encode(const SourceDocument& document, const EncodeOptions& options) const
{
    EncodeResult result;
    const bool sameFormat = document.format == _info.id;
    std::string version = !options.subversion.empty() ? options.subversion : sameFormat ? document.subversion : std::string();
    if (!FindVersion(version))
    {
        if (!version.empty())
            result.diagnostics.push_back({Severity::Warning, 0, 0, "unknown ZX-ASM version " + version + ": writing the newest"});
        version = std::string(kVersions.back().id);
    }
    const bool keep = sameFormat && document.subversion == version;
    const bool unterminated = keep && document.attrs.codec == _info.id && document.attrs.bytes == std::vector<uint8_t>{1};
    const std::string source = sameFormat && FindVersion(document.subversion) ? document.subversion : std::string();
    std::vector<uint8_t> line, sourceLine;
    for (size_t i = 0; i < document.lines.size(); ++i)
    {
        const SourceLine& l = document.lines[i];
        std::string error;
        if (keep && l.attrs.codec == _info.id && DecodeLine(l.attrs.bytes, version) == l.text)
            line = l.attrs.bytes;
        else if (!EncodeLine(l.text, version, line, error))
        {
            result.diagnostics.push_back({Severity::Error, static_cast<uint32_t>(i + 1), result.bytes.size(), error});
            continue;
        }
        else if (!source.empty() && source != version && EncodeLine(l.text, source, sourceLine, error))
        {
            // Another version: keywords of the source version that this one does not have stay text
            auto count = [](const std::vector<uint8_t>& bytes) {
                size_t n = 0;
                for (size_t j = 0; j + 1 < bytes.size(); ++j)
                    if (bytes[j] >= 0x02 && bytes[j] <= 0x05 && bytes[j + 1] >= 0x20)
                        ++n, ++j;
                return n;
            };
            if (count(sourceLine) > count(line))
                result.diagnostics.push_back({Severity::Warning, static_cast<uint32_t>(i + 1), result.bytes.size(),
                                              "a keyword of ZX-ASM " + source + " is not one in ZX-ASM " + version + ": written as text"});
        }
        result.bytes.insert(result.bytes.end(), line.begin(), line.end());
        if (!(unterminated && i + 1 == document.lines.size()))
            result.bytes.push_back(kLineEnd);
    }
    result.ok = !HasErrors(result.diagnostics);
    return result;
}
}  // namespace unrealasm::codecs
