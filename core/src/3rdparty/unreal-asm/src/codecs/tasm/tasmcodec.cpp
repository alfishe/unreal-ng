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

bool IsWordChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '@' || c == '$' || c == '#';
}
}  // namespace

TasmCodec::TasmCodec(int version)
    : _version(version), _runByte(version == 4 ? 0x01 : 0x0A), _tokens(version == 4 ? tasm::Tasm4Tokens() : tasm::Tasm3Tokens()),
      _info{version == 4 ? "tasm4" : "tasm3", version == 4 ? "TASM 4.x source (tokenized)" : "TASM 3.x source (tokenized)", "tasm",
            CodecFamily::Tokenized}
{
}

size_t TasmCodec::WalkFraming(std::span<const uint8_t> bytes, size_t* runs01, size_t* runs0A)
{
    size_t p = 0, lines = 0;
    while (p < bytes.size())
    {
        const uint8_t length = bytes[p];
        if (length == kEndMarker)
            return lines;
        if (p + 1 + length >= bytes.size() || bytes[p + 1 + length] != length)
            return 0;
        for (size_t i = p + 1; i + 1 < p + 1 + length; ++i)
        {
            if (bytes[i] == 0x01 && runs01)
                ++*runs01;
            if (bytes[i] == 0x0A && runs0A)
                ++*runs0A;
        }
        p += static_cast<size_t>(length) + 2;
        ++lines;
    }
    return 0;   // no end marker
}

int TasmCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    size_t runs01 = 0, runs0A = 0;
    const size_t lines = WalkFraming(bytes, &runs01, &runs0A);
    if (lines == 0)
        return 0;
    // TR-DOS catalog: type 'A'; TASM 3 saves with start 39221 / 40872, TASM 4 with start <= 4096 (ZX-M8XXX hints)
    if (hints.type == 'A')
    {
        const bool tasm3Start = hints.start == 39221 || hints.start == 40872;
        const bool tasm4Start = hints.start != 0 && hints.start <= 4096;
        if ((_version == 3 && tasm3Start) || (_version == 4 && tasm4Start))
            return 95;
        if ((_version == 3 && tasm4Start) || (_version == 4 && tasm3Start))
            return 20;
    }
    // No catalog: the framing holds; the space-run byte tells the version
    const bool mine = _version == 3 ? runs0A >= runs01 : runs01 > runs0A;
    const int base = lines >= 3 ? 80 : 60;
    return mine ? base : base - 40;
}

std::string TasmCodec::DecodeBody(std::span<const uint8_t> body) const
{
    std::string text;
    for (size_t i = 0; i < body.size(); ++i)
    {
        const uint8_t b = body[i];
        if (b == _runByte && i + 1 < body.size())
        {
            text.append(body[i + 1], ' ');
            ++i;
        }
        else if (b >= 0x20 && b < 0x7F)
            text.push_back(static_cast<char>(b));
        else if (b >= tasm::kFirstToken && b <= tasm::kLastToken)
            text.append(_tokens[b - tasm::kFirstToken]);
        else
            utf8::Append(text, kRawBase + b);
    }
    return text;
}

bool TasmCodec::EncodeBody(const std::string& text, std::vector<uint8_t>& body, std::string& error) const
{
    body.clear();
    const size_t n = text.size();
    auto spaces = [&](size_t count) {
        while (count >= 2)
        {
            const size_t chunk = std::min<size_t>(count, 255);
            body.push_back(_runByte);
            body.push_back(static_cast<uint8_t>(chunk));
            count -= chunk;
        }
        if (count == 1)
            body.push_back(' ');
    };
    size_t i = 0;
    bool labelField = n > 0 && text[0] != ' ';
    char quote = 0;
    bool comment = false;
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
        if (c == ' ')
        {
            size_t j = i;
            while (j < n && text[j] == ' ')
                ++j;
            spaces(j - i);
            i = j;
            labelField = false;
            continue;
        }
        if (quote || labelField || comment)
        {
            body.push_back(static_cast<uint8_t>(c));
            if (quote && c == quote)
                quote = 0;
            ++i;
            continue;
        }
        if (c == ';')
            comment = true;   // the rest of the line is literal (blanks still run-encoded)
        if (std::islower(static_cast<unsigned char>(c)) && (i == 0 || !IsWordChar(text[i - 1])))
        {
            size_t j = i;
            while (j < n && std::islower(static_cast<unsigned char>(text[j])))
                ++j;
            const std::string_view word(text.data() + i, j - i);
            const bool wordEnds = j >= n || !IsWordChar(text[j]);
            int token = -1;
            size_t consumed = 0;
            for (size_t t = 0; t < _tokens.size() && token < 0; ++t)
            {
                const std::string_view name = _tokens[t];
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
        if (c == '"' || c == '\'')
            quote = c;
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

DecodeResult TasmCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions&) const
{
    DecodeResult result;
    SourceDocument& document = result.document;
    document.format = _info.id;
    document.dialect = _info.dialect;
    document.subversion = _version == 4 ? "4.x" : "3.x";
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
        sourceLine.text = DecodeBody(body);
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

EncodeResult TasmCodec::Encode(const SourceDocument& document, const EncodeOptions&) const
{
    EncodeResult result;
    std::vector<uint8_t> body;
    for (size_t i = 0; i < document.lines.size(); ++i)
    {
        const SourceLine& line = document.lines[i];
        const bool own = line.attrs.codec == _info.id && DecodeBody(line.attrs.bytes) == line.text;
        if (own)
            body = line.attrs.bytes;
        else
        {
            std::string error;
            if (!EncodeBody(line.text, body, error))
            {
                result.diagnostics.push_back({Severity::Error, static_cast<uint32_t>(i + 1), result.bytes.size(), error});
                continue;
            }
        }
        result.bytes.push_back(static_cast<uint8_t>(body.size()));
        result.bytes.insert(result.bytes.end(), body.begin(), body.end());
        result.bytes.push_back(static_cast<uint8_t>(body.size()));
    }
    if (document.attrs.codec == _info.id)
        result.bytes.insert(result.bytes.end(), document.attrs.bytes.begin(), document.attrs.bytes.end());
    else
        result.bytes.insert(result.bytes.end(), {kEndMarker, kEndMarker});
    result.ok = !HasErrors(result.diagnostics);
    return result;
}
}  // namespace unrealasm::codecs
