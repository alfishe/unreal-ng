#include "codecs/alasm/alasmcodec.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "encoding/utf8.h"

namespace unrealasm::codecs
{
namespace
{
constexpr uint8_t kSignature[8] = {0xF3, 0x76, 0xC7, 0xDD, 0xFD, 0xED, 0xB0, 0xD9};
constexpr char32_t kRawBase = 0xF700;   // a byte with no character: U+F700 + byte (as the encodings do)
constexpr uint8_t kNoText = 0xFF;
constexpr uint8_t kLiteralRest = 0x10;
constexpr uint8_t kMaxRun = 0x0F;
constexpr size_t kKeywordColumn = 8;

bool HasSignature(std::span<const uint8_t> bytes)
{
    return bytes.size() >= AlasmCodec::kHeaderSize && std::memcmp(bytes.data() + AlasmCodec::kSignatureOffset, kSignature, 8) == 0;
}

void AppendLiteral(std::string& text, uint8_t b)
{
    utf8::Append(text, encoding::ByteToCodePoint(b, encoding::CodePage::Cp866));
}

void AppendRaw(std::string& text, uint8_t b)
{
    utf8::Append(text, kRawBase + b);
}

/// Text -> the bytes of ALASM's line buffer: ASCII as is, CP866 for other letters, U+F700 + byte as that byte
bool ToBuffer(const std::string& text, std::vector<uint8_t>& out, std::string& error)
{
    out.clear();
    const auto* data = reinterpret_cast<const uint8_t*>(text.data());
    size_t i = 0;
    while (i < text.size())
    {
        char32_t cp = 0;
        const size_t length = utf8::DecodeOne(std::span<const uint8_t>(data + i, text.size() - i), cp);
        uint8_t b = 0;
        if (length == 0)
        {
            error = "invalid UTF-8 at column " + std::to_string(out.size() + 1);
            return false;
        }
        if (cp >= kRawBase && cp <= kRawBase + 0xFF)
            b = static_cast<uint8_t>(cp - kRawBase);
        else if (cp < 0x80)
            b = static_cast<uint8_t>(cp);
        else if (!encoding::CodePointToByte(cp, encoding::CodePage::Cp866, b))
        {
            error = "a character ALASM cannot hold (not in CP866) at column " + std::to_string(out.size() + 1);
            return false;
        }
        if (b == 0)
        {
            error = "a zero byte at column " + std::to_string(out.size() + 1);
            return false;
        }
        out.push_back(b);
        i += length;
    }
    return true;
}

/// The indexes of a table's names by their first character (the low 7 bits, as sea_tkn compares), table order kept
template <typename Table>
std::array<std::vector<uint8_t>, 128> FirstIndex(const Table& names)
{
    std::array<std::vector<uint8_t>, 128> out;
    for (size_t c = 0; c < names.size(); ++c)
        if (!names[c].empty())
            out[static_cast<uint8_t>(names[c][0]) & 0x7F].push_back(static_cast<uint8_t>(c));
    return out;
}

const std::array<std::vector<uint8_t>, 128>& MnemonicIndex(const alasm::Version& version)
{
    static const std::vector<std::array<std::vector<uint8_t>, 128>> indexes = [] {
        std::vector<std::array<std::vector<uint8_t>, 128>> out;
        for (const alasm::Version& v : alasm::Versions())
            out.push_back(FirstIndex(v.mnemonics));
        return out;
    }();
    return indexes[static_cast<size_t>(&version - alasm::Versions().data())];
}

const std::array<std::vector<uint8_t>, 128>& RegisterIndex()
{
    static const std::array<std::vector<uint8_t>, 128> index = FirstIndex(alasm::Registers());
    return index;
}

/// sea_tkn of alTOKENS.H: the keyword starting at `h`; 0 when none. `end` = where scanning goes on (a mnemonic takes
/// one blank after it)
uint8_t FindKeyword(const std::vector<uint8_t>& t, size_t h, bool operands, const alasm::Version& version, size_t& end)
{
    auto at = [&](size_t i) -> uint8_t { return i < t.size() ? t[i] : 0; };
    auto matches = [&](std::string_view name) {
        for (size_t i = 0; i < name.size(); ++i)
            if (((at(h + i) ^ static_cast<uint8_t>(name[i])) & 0x7F) != 0 || at(h + i) == 0)
                return false;
        return true;
    };
    if (h >= t.size())
        return 0;
    const uint8_t first = t[h] & 0x7F;
    if (!operands)
    {
        for (const uint8_t c : MnemonicIndex(version)[first])
        {
            const std::string_view name = version.mnemonics[c];
            if (name.empty() || !matches(name))
                continue;
            const size_t e = h + name.size();
            const uint8_t next = at(e);
            if (next == 0 || next == ';')
                end = e;
            else if (next == ' ')
                end = e + 1;
            else
                continue;
            return static_cast<uint8_t>(alasm::kFirstMnemonic + c);
        }
        return 0;
    }
    const alasm::RegisterTable& registers = alasm::Registers();
    for (const uint8_t c : RegisterIndex()[first])
    {
        const std::string_view name = registers[c];
        if (name.empty() || !matches(name))
            continue;
        const size_t e = h + name.size();
        const uint8_t next = at(e);
        if (next != ';' && next >= '0')
            continue;
        end = e;
        return static_cast<uint8_t>(alasm::kFirstRegister + c);
    }
    return 0;
}
/// The keywords a record holds, as `version` spells them (the first from the mnemonic table, the rest operands)
std::vector<std::string_view> KeywordNames(const std::vector<uint8_t>& record, const alasm::Version& version)
{
    std::vector<std::string_view> names;
    bool literal = false;
    for (size_t i = 1; i < record.size(); ++i)
    {
        const uint8_t b = record[i];
        if (literal)
        {
            if (b == '"')
                literal = false;
            continue;
        }
        if (b == ';' || b == kLiteralRest)
            break;
        if (b == '"')
            literal = true;
        else if (b >= 0x80 && b != kNoText)
        {
            if (names.empty())
                names.push_back(b <= alasm::kLastMnemonic ? version.mnemonics[b - alasm::kFirstMnemonic] : std::string_view());
            else
                names.push_back(b >= alasm::kFirstRegister ? alasm::Registers()[b - alasm::kFirstRegister] : std::string_view());
        }
    }
    return names;
}
}  // namespace

AlasmCodec::AlasmCodec() : _info{"alasm", "ALASM source (tokenized)", "alasm", CodecFamily::Tokenized, {}}
{
    for (const alasm::Version& v : alasm::Versions())
        _info.subversions.push_back({std::string(v.id), std::string(v.title)});
}

std::vector<std::span<const uint8_t>> AlasmCodec::Lines(std::span<const uint8_t> bytes, std::span<const uint8_t>& rest, bool& framingOk)
{
    std::vector<std::span<const uint8_t>> lines;
    framingOk = false;
    rest = {};
    if (bytes.size() < kHeaderSize)
        return lines;
    const size_t length = bytes[kLengthOffset] | (bytes[kLengthOffset + 1] << 8);
    const size_t end = std::min(bytes.size(), kHeaderSize + length);
    size_t p = kHeaderSize;
    while (p < end && bytes[p] != 0 && p + bytes[p] <= end)
    {
        lines.push_back(bytes.subspan(p, bytes[p]));
        p += bytes[p];
    }
    framingOk = (p == end || bytes[p] == 0) && kHeaderSize + length <= bytes.size();
    rest = bytes.subspan(p);
    return lines;
}

int AlasmCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    if (!HasSignature(bytes))
        return 0;
    std::span<const uint8_t> rest;
    bool framingOk = false;
    Lines(bytes, rest, framingOk);
    if (!framingOk)
        return 40;
    return hints.type == 0 || hints.type == 'H' ? 95 : 85;
}

// str2txt of alTOKENS.H
std::string AlasmCodec::DecodeLine(std::span<const uint8_t> line, const alasm::Version& version)
{
    std::string text;
    size_t columns = 0;
    bool keywordSeen = false;     // bit 0 of B: mnemonic table until the first keyword, the register table after it
    bool noPadding = false;       // bit 2 of B: a blank run or #FF just before
    size_t implicitBlank = std::string::npos;
    size_t i = 1;                 // after the length byte
    while (i < line.size())
    {
        const uint8_t b = line[i++];
        if (b == kNoText)
        {
            noPadding = true;
            continue;
        }
        if (b == '"' || b == ';' || b == kLiteralRest)
        {
            const bool toEnd = b != '"';
            if (b != kLiteralRest)
                text.push_back(static_cast<char>(b)), ++columns;
            while (i < line.size())
            {
                const uint8_t c = line[i++];
                if (c < 0x20 || c == kNoText)
                    continue;   // the editor shows nothing for these
                AppendLiteral(text, c);
                ++columns;
                if (!toEnd && c == '"')
                    break;
            }
            noPadding = false;
            continue;
        }
        if (b >= 0x01 && b <= kMaxRun)
        {
            text.append(b, ' ');
            columns += b;
            noPadding = true;
            continue;
        }
        if (b >= 0x80)
        {
            if (columns < kKeywordColumn && !noPadding)
            {
                text.append(kKeywordColumn - columns, ' ');
                columns = kKeywordColumn;
            }
            std::string_view name;
            if (!keywordSeen && b <= alasm::kLastMnemonic)
                name = version.mnemonics[b - alasm::kFirstMnemonic];
            else if (keywordSeen && b >= alasm::kFirstRegister && b <= alasm::kLastRegister)
                name = alasm::Registers()[b - alasm::kFirstRegister];
            if (name.empty())
            {
                AppendRaw(text, b);   // not a keyword of this version
                ++columns;
            }
            else
            {
                text.append(name);
                columns += name.size();
            }
            if (!keywordSeen)
            {
                keywordSeen = true;
                text.push_back(' ');
                ++columns;
                implicitBlank = text.size();
            }
            noPadding = false;
            continue;
        }
        if (b >= 0x20 && b < 0x80)
            text.push_back(static_cast<char>(b));
        else
            AppendRaw(text, b);
        ++columns;
        noPadding = false;
    }
    if (implicitBlank == text.size())
        text.pop_back();   // the blank the editor puts after a mnemonic, at the end of the line
    return text;
}

// cnv2str of alTOKENS.H
bool AlasmCodec::EncodeLine(const std::string& text, const alasm::Version& version, std::vector<uint8_t>& record, std::string& error)
{
    std::vector<uint8_t> t;
    if (!ToBuffer(text, t, error))
        return false;
    std::vector<uint8_t> out;
    size_t h = 0;
    bool keywordSeen = false;
    bool afterRun = false;
    auto at = [&](size_t i) -> uint8_t { return i < t.size() ? t[i] : 0; };
    // c2TOKEN: copy the rest of a number or a name
    auto copyWord = [&]() {
        while (true)
        {
            const uint8_t c = at(h);
            if (c == '#' || (c >= '0' && c <= '9') || (c >= '?' && c < 0x80))
                out.push_back(c), ++h;
            else
                break;
        }
    };
    while (h < t.size())
    {
        const uint8_t c = t[h++];
        if (c == '"')
        {
            out.push_back(c);
            while (h < t.size())
            {
                const uint8_t s = t[h++];
                out.push_back(s);
                if (s == '"')
                    break;
            }
            afterRun = false;
            continue;
        }
        if (c == ';')
        {
            out.insert(out.end(), t.begin() + static_cast<std::ptrdiff_t>(h - 1), t.end());
            break;
        }
        if (c == ' ')
        {
            uint8_t run = 1;
            while (run < kMaxRun && at(h) == ' ')
                ++run, ++h;
            out.push_back(run);
            afterRun = true;
            continue;
        }
        if (c == '#')
        {
            --h;
            copyWord();
            afterRun = false;
            continue;
        }
        if (c == ',' || (c != '(' && c < '?'))
        {
            out.push_back(c);
            if (c != ',')
                copyWord();
            afterRun = false;
            continue;
        }
        --h;
        if (t[h] >= 0x80)
        {
            out.push_back(kLiteralRest);   // Russian or pseudographics: the rest of the line is text
            out.insert(out.end(), t.begin() + static_cast<std::ptrdiff_t>(h), t.end());
            break;
        }
        size_t end = 0;
        const uint8_t code = FindKeyword(t, h, keywordSeen, version, end);
        if (code == 0)
        {
            out.push_back(t[h++]);
            copyWord();
            afterRun = false;
            continue;
        }
        if (h == kKeywordColumn)
        {
            if (afterRun)
                out.pop_back();   // the decoder pads to the keyword column by itself
        }
        else if (h < kKeywordColumn && !afterRun)
            out.push_back(kNoText);   // keep it left of the keyword column
        keywordSeen = true;
        out.push_back(code);
        h = end;
        afterRun = false;
        if (code == 0x96)
            copyWord();   // DD / DEFM: hex digits are not split into keywords
    }
    // cend: walking backwards from the end must find this line's length byte first; #FF bytes are added until it does
    while (true)
    {
        if (out.size() + 1 > 0xFF)
        {
            error = "the line is longer than an ALASM record (254 bytes)";
            return false;
        }
        record.assign(1, static_cast<uint8_t>(out.size() + 1));
        record.insert(record.end(), out.begin(), out.end());
        size_t k = 1;
        while (record[record.size() - k] != k)
            ++k;
        if (k == record.size())
            return true;
        out.push_back(kNoText);
    }
}

DecodeResult AlasmCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeResult result;
    SourceDocument& document = result.document;
    document.format = _info.id;
    document.dialect = _info.dialect;
    document.codePage = encoding::CodePage::Cp866;
    document.lineEnd = encoding::LineEnd::Lf;
    if (!HasSignature(bytes))
    {
        result.diagnostics.push_back({Severity::Error, 0, 0, "not an ALASM source (no signature at +#28)"});
        document.attrs = {_info.id, std::vector<uint8_t>(bytes.begin(), bytes.end())};
        return result;
    }
    std::span<const uint8_t> rest;
    bool framingOk = false;
    const auto lines = Lines(bytes, rest, framingOk);
    if (!framingOk)
        result.diagnostics.push_back({Severity::Warning, static_cast<uint32_t>(lines.size() + 1), static_cast<uint64_t>(rest.data() - bytes.data()),
                                      "line framing does not end at the header's source length"});
    document.name.assign(reinterpret_cast<const char*>(bytes.data()), 8);
    while (!document.name.empty() && document.name.back() == ' ')
        document.name.pop_back();

    // The version: the newest one that re-tokenizes the most lines exactly as they are stored
    const alasm::Version* version = alasm::FindVersion(options.subversion);
    if (version)
        result.subversions = {std::string(version->id)};
    if (!options.subversion.empty() && !version)
        result.diagnostics.push_back({Severity::Warning, 0, 0, "unknown ALASM version " + options.subversion + ": detected instead"});
    if (!version)
    {
        std::vector<uint8_t> record;
        std::string error;
        // Evidence per version: lines it re-tokenizes exactly as stored (a keyword it lacks, or a word it would have
        // tokenized, breaks that), plus the operand of #96: a string for DEFM (3.8-4.5), hex digits for DD (4.44, 5.x)
        std::vector<size_t> exact;
        for (const alasm::Version& v : alasm::Versions())
        {
            const std::string_view code96 = v.mnemonics[0x96 - alasm::kFirstMnemonic];
            size_t n = 0;
            for (const auto& line : lines)
            {
                if (EncodeLine(DecodeLine(line, v), v, record, error) && std::equal(record.begin(), record.end(), line.begin(), line.end()))
                    ++n;
                const auto first = std::find_if(line.begin() + 1, line.end(), [](uint8_t b) { return b >= 0x80 || b == '"' || b == ';' || b == kLiteralRest; });
                if (first != line.end() && *first == 0x96 && first + 1 != line.end())
                {
                    const uint8_t operand = *(first + 1);
                    const bool isString = operand == '"';
                    const bool isHex = operand == '#' || (operand >= '0' && operand <= '9') || (operand >= 'A' && operand <= 'F');
                    n += (isString && code96 == "DEFM") || (isHex && code96 == "DD");
                }
            }
            exact.push_back(n);
        }
        const size_t best = *std::max_element(exact.begin(), exact.end());
        std::vector<const alasm::Version*> tied;
        for (size_t i = 0; i < exact.size(); ++i)
            if (exact[i] == best)
                tied.push_back(&alasm::Versions()[i]);
        version = tied.back();   // the newest of the versions the evidence allows
        for (const alasm::Version* v : tied)
            result.subversions.push_back(std::string(v->id));
        // Versions the file cannot tell apart may spell some of its keywords differently: say which
        std::string others, example;
        for (const alasm::Version* v : tied)
        {
            if (v == version)
                continue;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                const std::string theirs = DecodeLine(lines[i], *v);
                if (theirs != DecodeLine(lines[i], *version))
                {
                    others += (others.empty() ? "" : ", ") + std::string(v->id);
                    if (example.empty())
                        example = "line " + std::to_string(i + 1) + " reads \"" + theirs + "\" in " + std::string(v->id);
                    break;
                }
            }
        }
        if (!others.empty())
            result.diagnostics.push_back({Severity::Info, 0, 0, "ALASM " + std::string(version->id) + " chosen; the file is equally valid for " +
                                                                    others + ", which spell some of its keywords differently (" + example + ")"});
        else if (tied.size() > 1)
            result.diagnostics.push_back({Severity::Info, 0, 0, "ALASM " + std::string(tied.front()->id) + " to " + std::string(version->id) +
                                                                    " read this file the same; " + std::string(version->id) + " chosen"});
    }
    document.subversion = std::string(version->id);
    for (const auto& line : lines)
    {
        SourceLine sourceLine;
        sourceLine.text = DecodeLine(line, *version);
        sourceLine.attrs = {_info.id, std::vector<uint8_t>(line.begin(), line.end())};
        document.lines.push_back(std::move(sourceLine));
    }
    // Kept for the exact round trip: the header, how many bytes after the last line the length field still counts,
    // and those bytes
    const size_t declared = bytes[kLengthOffset] | (bytes[kLengthOffset + 1] << 8);
    const size_t bodySize = static_cast<size_t>(rest.data() - bytes.data()) - kHeaderSize;
    const size_t counted = declared > bodySize ? std::min(declared - bodySize, rest.size()) : 0;
    std::vector<uint8_t> kept(bytes.begin(), bytes.begin() + kHeaderSize);
    kept.push_back(static_cast<uint8_t>(counted & 0xFF));
    kept.push_back(static_cast<uint8_t>(counted >> 8));
    kept.insert(kept.end(), rest.begin(), rest.end());
    document.attrs = {_info.id, std::move(kept)};
    result.ok = !HasErrors(result.diagnostics);
    return result;
}

EncodeResult AlasmCodec::Encode(const SourceDocument& document, const EncodeOptions& options) const
{
    EncodeResult result;
    const bool sameFormat = document.format == _info.id;
    const std::string wanted = !options.subversion.empty() ? options.subversion : sameFormat ? document.subversion : std::string();
    const alasm::Version* version = alasm::FindVersion(wanted);
    if (!version)
    {
        if (!wanted.empty())
            result.diagnostics.push_back({Severity::Warning, 0, 0, "unknown ALASM version " + wanted + ": writing the newest"});
        version = &alasm::Versions().back();
    }
    const bool keep = sameFormat && document.subversion == version->id && document.attrs.codec == _info.id &&
                      document.attrs.bytes.size() >= kHeaderSize + 2;
    const alasm::Version* source = sameFormat ? alasm::FindVersion(document.subversion) : nullptr;

    std::vector<uint8_t> body, record;
    for (size_t i = 0; i < document.lines.size(); ++i)
    {
        const SourceLine& line = document.lines[i];
        if (keep && line.attrs.codec == _info.id && !line.attrs.bytes.empty() && DecodeLine(line.attrs.bytes, *version) == line.text)
        {
            body.insert(body.end(), line.attrs.bytes.begin(), line.attrs.bytes.end());
            continue;
        }
        std::string error;
        if (!EncodeLine(line.text, *version, record, error))
        {
            result.diagnostics.push_back({Severity::Error, static_cast<uint32_t>(i + 1), kHeaderSize + body.size(), error});
            continue;
        }
        // Another version: a keyword of the source version that this one does not have stays text
        if (source && source != version)
        {
            std::vector<uint8_t> sourceRecord;
            if (EncodeLine(line.text, *source, sourceRecord, error))
            {
                std::vector<std::string_view> kept = KeywordNames(record, *version);
                for (const std::string_view word : KeywordNames(sourceRecord, *source))
                {
                    const auto it = std::find(kept.begin(), kept.end(), word);
                    if (it != kept.end())
                        kept.erase(it);
                    else
                        result.diagnostics.push_back({Severity::Warning, static_cast<uint32_t>(i + 1), kHeaderSize + body.size(),
                                                      "'" + std::string(word) + "' is not a keyword of ALASM " + std::string(version->id) +
                                                          ": written as text"});
                }
            }
        }
        body.insert(body.end(), record.begin(), record.end());
    }
    if (body.size() > 0xFFFF - kHeaderSize)
        result.diagnostics.push_back({Severity::Error, 0, 0, "the source is longer than an ALASM file can hold"});

    std::vector<uint8_t> header(kHeaderSize, 0);
    std::vector<uint8_t> tail;
    size_t counted = 0;
    if (keep)
    {
        const std::vector<uint8_t>& kept = document.attrs.bytes;
        header.assign(kept.begin(), kept.begin() + kHeaderSize);
        counted = kept[kHeaderSize] | (kept[kHeaderSize + 1] << 8);
        tail.assign(kept.begin() + kHeaderSize + 2, kept.end());
    }
    else
    {
        // A fresh header: name, type, the editor state ALASM writes for a file opened at its first line
        for (size_t i = 0; i < 8; ++i)
            header[i] = static_cast<uint8_t>(i < document.name.size() ? document.name[i] : ' ');
        header[8] = 'H';
        header[0x23] = 0x40;
        header[0x24] = 0xC0;
        std::memcpy(header.data() + kSignatureOffset, kSignature, 8);
    }
    const size_t length = body.size() + counted;
    header[kLengthOffset] = static_cast<uint8_t>(length & 0xFF);
    header[kLengthOffset + 1] = static_cast<uint8_t>(length >> 8);
    result.bytes = std::move(header);
    result.bytes.insert(result.bytes.end(), body.begin(), body.end());
    result.bytes.insert(result.bytes.end(), tail.begin(), tail.end());
    result.ok = !HasErrors(result.diagnostics);
    return result;
}
}  // namespace unrealasm::codecs
