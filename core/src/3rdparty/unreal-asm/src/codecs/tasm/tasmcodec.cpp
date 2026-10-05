#include "codecs/tasm/tasmcodec.h"

#include <algorithm>
#include <cctype>

#include "encoding/utf8.h"

namespace unrealasm::codecs
{
namespace
{
constexpr uint8_t kEndMarker = 0xFF;
constexpr char32_t kRawBase = 0xF700;   // a byte the format gives no character: U+F700 + byte (as the encodings do)

enum class Spacing : uint8_t
{
    Run0A,     ///< 3.x, 4.0, 4.4: #0A n = n blanks
    Direct,    ///< 4.12: #02-#1F = that many blanks (#0A = ten), #01 n = n blanks
};

struct Version
{
    Spacing spacing;
    const tasm::TokenTable& tokens;
};

Version VersionOf(const std::string& version)
{
    if (version == "3")
        return {Spacing::Run0A, tasm::Tasm3Tokens()};
    if (version == "4.12")
        return {Spacing::Direct, tasm::Tasm412Tokens()};
    return {Spacing::Run0A, tasm::Tasm40Tokens()};
}

/// Blanks at body[i]: their count and the bytes they take; 0 when body[i] is not a blank run
size_t BlankRun(std::span<const uint8_t> body, size_t i, Spacing spacing, size_t& taken)
{
    const uint8_t b = body[i];
    if (spacing == Spacing::Run0A)
    {
        if (b == 0x0A && i + 1 < body.size())
            return taken = 2, body[i + 1];
        return 0;
    }
    if (b == 0x01 && i + 1 < body.size())
        return taken = 2, body[i + 1];
    if (b >= 0x02 && b <= 0x1F)
        return taken = 1, b;
    return 0;
}

bool IsWordChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '@' || c == '$' || c == '#';
}
}  // namespace

TasmCodec::TasmCodec()
    : _info{"tasm", "TASM source (tokenized)", "tasm", CodecFamily::Tokenized,
            {{"3", "TASM 3.0-3.5 (Rst7)"}, {"4.0", "TASM 4.0 (XL Design) / 4.4 (KVA)"}, {"4.12", "TASM 4.12 (Rst7)"}}}
{
}

std::vector<std::span<const uint8_t>> TasmCodec::Bodies(std::span<const uint8_t> bytes, bool& ended)
{
    std::vector<std::span<const uint8_t>> bodies;
    size_t p = 0;
    ended = false;
    while (p < bytes.size())
    {
        const uint8_t length = bytes[p];
        if (length == kEndMarker)
        {
            ended = true;
            break;
        }
        if (p + 1 + length >= bytes.size() || bytes[p + 1 + length] != length)
            break;
        bodies.push_back(bytes.subspan(p + 1, length));
        p += static_cast<size_t>(length) + 2;
    }
    return bodies;
}

std::string TasmCodec::DetectVersion(std::span<const uint8_t> bytes, const CatalogHints& hints, std::vector<std::string>* consistent)
{
    bool ended = false;
    const auto bodies = Bodies(bytes, ended);
    if (!ended)
        return {};
    // The TR-DOS catalog's start field: each version saves sources with its own value (the word is in its binary)
    std::string byCatalog;
    if (hints.type == 'A' && hints.start == 39221)
        byCatalog = "3";
    else if (hints.type == 'A' && hints.start == 40872)
        byCatalog = "4.0";
    else if (hints.type == 'A' && hints.start <= 4096)
        byCatalog = "4.12";   // 4.12 keeps a small number there (0, 17, 32, ...: the editor's line)
    if (!byCatalog.empty())
    {
        if (consistent)
            *consistent = {byCatalog};
        return byCatalog;
    }
    // No catalog: the versions whose tokenizer writes the most lines back exactly as stored (the blank encoding and the
    // keyword set decide); the newest of them
    static const char* const kVersions[] = {"3", "4.0", "4.12"};
    std::vector<size_t> exact;
    std::vector<uint8_t> again;
    std::string error;
    for (const char* version : kVersions)
    {
        size_t n = 0;
        for (const auto& body : bodies)
        {
            // A byte this version gives no meaning (shown as U+F700 + byte) is evidence against it, even though it
            // would be written back unchanged
            const std::string text = DecodeBody(body, version);
            const bool raw = text.find("\xEF\x9C") != std::string::npos || text.find("\xEF\x9D") != std::string::npos ||
                             text.find("\xEF\x9E") != std::string::npos || text.find("\xEF\x9F") != std::string::npos;
            n += !raw && EncodeBody(text, version, again, error) && std::equal(again.begin(), again.end(), body.begin(), body.end());
        }
        exact.push_back(n);
    }
    const size_t best = *std::max_element(exact.begin(), exact.end());
    std::string chosen;
    for (size_t i = 0; i < exact.size(); ++i)
        if (exact[i] == best)
        {
            chosen = kVersions[i];
            if (consistent)
                consistent->push_back(chosen);
        }
    return chosen;
}

int TasmCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    bool ended = false;
    const size_t lines = Bodies(bytes, ended).size();
    if (!ended || lines == 0)
        return 0;
    const bool tasmStart = hints.start == 39221 || hints.start == 40872 || hints.start <= 4096;
    if (hints.type == 'A' && tasmStart)
        return 95;
    return lines >= 3 ? 80 : 60;
}

std::string TasmCodec::DecodeBody(std::span<const uint8_t> body, const std::string& version)
{
    const auto [spacing, tokens] = VersionOf(version);
    std::string text;
    for (size_t i = 0; i < body.size();)
    {
        const uint8_t b = body[i];
        size_t taken = 1;
        if (const size_t blanks = BlankRun(body, i, spacing, taken))
            text.append(blanks, ' ');
        else if (b >= 0x20 && b < 0x7F)
            text.push_back(static_cast<char>(b));
        else if (b >= tasm::kFirstToken && b <= tasm::kLastToken && !tokens[b - tasm::kFirstToken].empty())
            text.append(tokens[b - tasm::kFirstToken]);
        else
            utf8::Append(text, kRawBase + b);   // not a character or keyword of this version
        i += taken;
    }
    return text;
}

bool TasmCodec::EncodeBody(const std::string& text, const std::string& version, std::vector<uint8_t>& body, std::string& error)
{
    const auto [spacing, tokens] = VersionOf(version);
    body.clear();
    const size_t n = text.size();
    auto spaces = [&](size_t count) {
        while (count >= 2)
        {
            if (spacing == Spacing::Run0A)
            {
                const size_t chunk = std::min<size_t>(count, 255);
                body.push_back(0x0A);
                body.push_back(static_cast<uint8_t>(chunk));
                count -= chunk;
            }
            else
            {
                const size_t chunk = std::min<size_t>(count, 0x1F);
                body.push_back(static_cast<uint8_t>(chunk));
                count -= chunk;
            }
        }
        if (count == 1)
            body.push_back(' ');
    };
    // TASM tokenizes every lower-case keyword word wherever it stands: labels, operands, strings and comments alike
    // (research-tasm.md §4)
    size_t i = 0;
    while (i < n)
    {
        const char c = text[i];
        // Raw bytes (U+F700 + byte) go back as the byte
        if (static_cast<unsigned char>(c) >= 0x80)
        {
            char32_t cp = 0;
            const auto* data = reinterpret_cast<const uint8_t*>(text.data());
            const size_t length = utf8::DecodeOne(std::span<const uint8_t>(data + i, n - i), cp);
            if (length && cp >= kRawBase && cp <= kRawBase + 0xFF)
            {
                body.push_back(static_cast<uint8_t>(cp - kRawBase));
                i += length;
                continue;
            }
            error = "a character TASM cannot hold at column " + std::to_string(i + 1);
            return false;
        }
        // A character constant "x" stays as typed (longer strings are tokenized like everything else)
        if (c == '"' && i + 2 < n && text[i + 2] == '"' && static_cast<unsigned char>(text[i + 1]) < 0x80)
        {
            body.insert(body.end(), text.begin() + static_cast<std::ptrdiff_t>(i), text.begin() + static_cast<std::ptrdiff_t>(i + 3));
            i += 3;
            continue;
        }
        if (c == ' ')
        {
            size_t j = i;
            while (j < n && text[j] == ' ')
                ++j;
            spaces(j - i);
            i = j;
            continue;
        }
        if (std::islower(static_cast<unsigned char>(c)) && (i == 0 || !IsWordChar(text[i - 1])))
        {
            size_t j = i;
            while (j < n && std::islower(static_cast<unsigned char>(text[j])))
                ++j;
            const std::string_view word(text.data() + i, j - i);
            const bool wordEnds = j >= n || !IsWordChar(text[j]);
            int token = -1;
            size_t consumed = 0;
            for (size_t t = 0; t < tokens.size() && token < 0; ++t)
            {
                const std::string_view name = tokens[t];
                if (name.empty())
                    continue;
                if (name == "af'" && word == "af" && j < n && text[j] == '\'')
                    token = static_cast<int>(t), consumed = 3;
                else if (wordEnds && name.size() == word.size() + 1 && name.back() == ' ' && name.substr(0, word.size()) == word &&
                         j < n && text[j] == ' ')
                    token = static_cast<int>(t), consumed = word.size() + 1;
                else if (wordEnds && name == word)
                    token = static_cast<int>(t), consumed = word.size();
            }
            if (token >= 0)
            {
                body.push_back(static_cast<uint8_t>(tasm::kFirstToken + token));
                i += consumed;
                continue;
            }
            body.insert(body.end(), text.begin() + static_cast<std::ptrdiff_t>(i), text.begin() + static_cast<std::ptrdiff_t>(j));
            i = j;
            continue;
        }
        body.push_back(static_cast<uint8_t>(c));
        ++i;
    }
    if (body.size() >= kEndMarker)
    {
        error = "the line is longer than a TASM record (254 bytes)";
        return false;
    }
    return true;
}

namespace
{
/// The keywords (token names without the trailing blank) a body holds
std::vector<std::string> Keywords(const std::vector<uint8_t>& body, const Version& version)
{
    std::vector<std::string> words;
    const auto& tokens = version.tokens;
    for (size_t i = 0; i < body.size(); ++i)
    {
        size_t taken = 1;
        if (BlankRun(body, i, version.spacing, taken))
            i += taken - 1;
        else if (body[i] >= tasm::kFirstToken && body[i] <= tasm::kLastToken && !tokens[body[i] - tasm::kFirstToken].empty())
        {
            std::string word(tokens[body[i] - tasm::kFirstToken]);
            if (!word.empty() && word.back() == ' ')
                word.pop_back();
            words.push_back(word);
        }
    }
    return words;
}
}  // namespace

DecodeResult TasmCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeResult result;
    SourceDocument& document = result.document;
    document.format = _info.id;
    document.dialect = _info.dialect;
    if (!options.subversion.empty())
    {
        document.subversion = options.subversion;
        result.subversions = {options.subversion};
    }
    else
        document.subversion = DetectVersion(bytes, options.catalog, &result.subversions);
    if (document.subversion.empty())
        document.subversion = _info.subversions.back().id;   // framing broken: the diagnostics below say where
    document.codePage = encoding::CodePage::Ascii;
    document.lineEnd = encoding::LineEnd::Lf;
    size_t p = 0;
    uint32_t line = 0;
    bool ended = false;
    while (p < bytes.size())
    {
        const uint8_t length = bytes[p];
        if (length == kEndMarker)
        {
            ended = true;
            break;
        }
        ++line;
        if (p + 1 + length >= bytes.size() || bytes[p + 1 + length] != length)
        {
            result.diagnostics.push_back({Severity::Error, line, p, "line framing broken (length byte not repeated)"});
            break;
        }
        const std::span<const uint8_t> body = bytes.subspan(p + 1, length);
        SourceLine sourceLine;
        sourceLine.text = DecodeBody(body, document.subversion);
        sourceLine.attrs = {_info.id, std::vector<uint8_t>(body.begin(), body.end())};
        document.lines.push_back(std::move(sourceLine));
        p += static_cast<size_t>(length) + 2;
    }
    if (!ended && result.diagnostics.empty())
        result.diagnostics.push_back({Severity::Warning, line, p, "no end marker (#FF)"});
    // Everything from the end marker on (or from where the framing broke) is kept for the exact round trip
    document.attrs = {_info.id, std::vector<uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(std::min(p, bytes.size())), bytes.end())};
    result.ok = !HasErrors(result.diagnostics);
    return result;
}

EncodeResult TasmCodec::Encode(const SourceDocument& document, const EncodeOptions& options) const
{
    EncodeResult result;
    const bool sameFormat = document.format == _info.id;
    const std::string version = !options.subversion.empty() ? options.subversion
                                : sameFormat && !document.subversion.empty() ? document.subversion : _info.subversions.back().id;
    // Kept bytes are reused only when they are this version's bytes of the same text
    const bool keep = sameFormat && document.subversion == version;
    std::vector<uint8_t> body;
    for (size_t i = 0; i < document.lines.size(); ++i)
    {
        const SourceLine& line = document.lines[i];
        const bool own = keep && line.attrs.codec == _info.id && DecodeBody(line.attrs.bytes, version) == line.text;
        if (own)
            body = line.attrs.bytes;
        else
        {
            std::string error;
            if (!EncodeBody(line.text, version, body, error))
            {
                result.diagnostics.push_back({Severity::Error, static_cast<uint32_t>(i + 1), result.bytes.size(), error});
                continue;
            }
            // Another version of the same format: a keyword of the source version that the target lacks stays text
            if (sameFormat && !document.subversion.empty() && document.subversion != version)
            {
                std::vector<uint8_t> sourceBody;
                std::string ignored;
                if (EncodeBody(line.text, document.subversion, sourceBody, ignored))
                {
                    const Version source = VersionOf(document.subversion), target = VersionOf(version);
                    std::vector<std::string> kept = Keywords(body, target);
                    for (const std::string& word : Keywords(sourceBody, source))
                    {
                        const auto it = std::find(kept.begin(), kept.end(), word);
                        if (it != kept.end())
                            kept.erase(it);
                        else
                            result.diagnostics.push_back({Severity::Warning, static_cast<uint32_t>(i + 1), result.bytes.size(),
                                                          "'" + word + "' is not a keyword of TASM " + version + ": written as text"});
                    }
                }
            }
        }
        result.bytes.push_back(static_cast<uint8_t>(body.size()));
        result.bytes.insert(result.bytes.end(), body.begin(), body.end());
        result.bytes.push_back(static_cast<uint8_t>(body.size()));
    }
    if (keep && document.attrs.codec == _info.id)
        result.bytes.insert(result.bytes.end(), document.attrs.bytes.begin(), document.attrs.bytes.end());
    else
        result.bytes.insert(result.bytes.end(), {kEndMarker, kEndMarker});
    result.ok = !HasErrors(result.diagnostics);
    return result;
}
}  // namespace unrealasm::codecs
