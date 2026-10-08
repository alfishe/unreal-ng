#include "codecs/odin/odincodec.h"

#include <algorithm>
#include <cctype>

#include "encoding/utf8.h"

namespace unrealasm::codecs
{
namespace
{
constexpr uint8_t kEof = 0xFF;

struct OdinToken
{
    uint8_t token;
    uint8_t sub;
    const char* text;
};

#include "codecs/odin/odintokens.inc"

template <size_t N>
const char* TextOf(const OdinToken (&table)[N], uint8_t token, uint8_t sub)
{
    for (const OdinToken& t : table)
        if (t.token == token && t.sub == sub)
            return t.text;
    return nullptr;
}

/// Odin's isIdentChar: a letter, a digit, '.' or '_'
bool IsIdent(char c)
{
    const unsigned char u = static_cast<unsigned char>(c);
    return u == '.' || u == '_' || std::isalnum(u);
}

char Up(char c)
{
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

/// Odin's tokenise: the first entry of the table whose text is at `at` and whose end condition holds; `length` is what the
/// token consumes (an instruction token swallows a single blank that is followed by more text)
template <size_t N>
const OdinToken* Match(const OdinToken (&table)[N], const std::string& text, size_t at, bool instruction, size_t& length)
{
    for (const OdinToken& t : table)
    {
        const size_t n = std::string_view(t.text).size();
        if (at + n > text.size())
            continue;
        bool same = true;
        for (size_t k = 0; k < n && same; ++k)
            same = Up(text[at + k]) == t.text[k];
        if (!same)
            continue;
        const size_t after = at + n;
        const char next = after < text.size() ? text[after] : '\0';
        if (instruction)
        {
            if (next == ' ')
            {
                const char then = after + 1 < text.size() ? text[after + 1] : '\0';
                length = n + (then == ' ' || then == '\0' || then == ';' ? 0 : 1);
                return &t;
            }
            if (next == '\0' || next == ';')
            {
                length = n;
                return &t;
            }
            continue;
        }
        if (!IsIdent(next) || !IsIdent(t.text[n - 1]))
        {
            length = n;
            return &t;
        }
    }
    return nullptr;
}

void PushSpaces(std::vector<uint8_t>& out, size_t count)
{
    while (count > 0)
    {
        if (count <= 22)
        {
            out.push_back(static_cast<uint8_t>(0x21 - count));
            return;
        }
        const size_t n = std::min<size_t>(count, 255);
        out.push_back(0x0A);
        out.push_back(static_cast<uint8_t>(n));
        count -= n;
    }
}
}  // namespace

OdinCodec::OdinCodec() : _info{"odin", "Odin document (.odn)", "odin", CodecFamily::Tokenized, {}} {}

std::vector<uint8_t> OdinCodec::Tokenize(const std::string& text)
{
    // Odin's tokeniseLine: a mode (0 look for tokens, 1 inside an identifier, 2 comment, or the quote character), blanks counted and
    // written as one code in front of the next character (blanks at the end of the line are dropped), instruction tokens first and
    // operand tokens after the first token found
    std::vector<uint8_t> out;
    int mode = 0;
    bool instruction = true;
    size_t spaces = 0;
    size_t i = 0;
    while (i < text.size())
    {
        const char c = text[i];
        if (mode == 1)
        {
            if (IsIdent(c))
            {
                out.push_back(static_cast<uint8_t>(c));
                ++i;
                continue;
            }
            mode = 0;   // the first character that is not part of the name ends it
        }
        if (c == ' ')
        {
            ++spaces;
            ++i;
            continue;
        }
        PushSpaces(out, spaces);
        spaces = 0;
        if (mode == 2)
        {
            out.push_back(static_cast<uint8_t>(c));
            ++i;
            continue;
        }
        if (mode == '"' || mode == '\'')
        {
            out.push_back(static_cast<uint8_t>(c));
            if (c == mode)
                mode = 0;
            ++i;
            continue;
        }
        mode = 0;
        if (c == '$')
        {
            out.push_back(static_cast<uint8_t>(c));
            mode = 1;
            ++i;
            continue;
        }
        if (c == ';')
        {
            out.push_back(static_cast<uint8_t>(c));
            mode = 2;
            ++i;
            continue;
        }
        if (c == '"' || c == '\'')
        {
            out.push_back(static_cast<uint8_t>(c));
            mode = c;
            ++i;
            continue;
        }
        size_t length = 0;
        const OdinToken* token = instruction ? Match(kOdinInstructionTokens, text, i, true, length) : Match(kOdinOperandTokens, text, i, false, length);
        if (token)
        {
            out.push_back(token->token);
            if (token->sub != 0)
                out.push_back(token->sub);
            instruction = false;
            i += length;
            continue;
        }
        out.push_back(static_cast<uint8_t>(c));
        mode = IsIdent(c) ? 1 : 0;
        ++i;
    }
    return out;
}

std::string OdinCodec::Expand(std::span<const uint8_t> line, bool& valid)
{
    // Odin's detokenise
    std::string out;
    bool instruction = true;
    valid = true;
    for (size_t k = 0; k < line.size(); ++k)
    {
        const uint8_t b = line[k];
        if (b >= 0x80 && b < kEof)
        {
            uint8_t sub = 0;
            if (k + 1 < line.size() && line[k + 1] >= 1 && line[k + 1] <= 2)
                sub = line[++k];
            const char* text = instruction ? TextOf(kOdinInstructionTokens, b, sub) : TextOf(kOdinOperandTokens, b, sub);
            if (!text)
            {
                valid = false;
                return out;
            }
            out += text;
            if (instruction)
            {
                // an implicit blank follows an instruction token when the next byte is neither a blank, a control code nor a ';'
                const uint8_t next = k + 1 < line.size() ? line[k + 1] : 0;
                if (next >= 0x21 && next != ';')
                    out.push_back(' ');
            }
            instruction = false;
        }
        else if (b >= 0x21 && b <= 0x7F)
            out.push_back(static_cast<char>(b));
        else if (b >= 0x0B && b <= 0x20)
            out.append(static_cast<size_t>(0x21 - b), ' ');
        else if (b == 0x0A && k + 1 < line.size())
            out.append(line[++k], ' ');
        else
        {
            valid = false;   // $01..$09 are ignored by Odin; none belongs in a line
            return out;
        }
    }
    return out;
}

int OdinCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    (void)hints;
    if (bytes.size() < 7 || bytes[0] != 'O' || bytes[1] != 'D' || bytes[2] != 'S' || bytes[3] != 0 || bytes[4] != kEof || bytes[5] != 0)
        return 0;
    return std::find(bytes.begin() + 6, bytes.end(), kEof) != bytes.end() ? 98 : 60;
}

DecodeResult OdinCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions&) const
{
    DecodeResult result;
    SourceDocument& document = result.document;
    document.format = _info.id;
    document.dialect = _info.dialect;
    document.codePage = encoding::CodePage::ZxSpectrum;
    document.lineEnd = encoding::LineEnd::Lf;
    if (Detect(bytes, {}) == 0)
    {
        result.diagnostics.push_back({Severity::Error, 0, 0, "not an Odin document (no ODS header)"});
        return result;
    }
    size_t p = 6;
    // The lines: each ends with $00; the document ends with $FF
    while (p < bytes.size() && bytes[p] != kEof)
    {
        size_t e = p;
        while (e < bytes.size() && bytes[e] != 0 && bytes[e] != kEof)
            ++e;
        const std::span<const uint8_t> stored = bytes.subspan(p, e - p);
        bool valid = true;
        SourceLine line;
        line.text = Expand(stored, valid);
        // the text may hold Spectrum characters above 7F only through tokens, so it is ASCII
        if (!valid || Tokenize(line.text) != std::vector<uint8_t>(stored.begin(), stored.end()))
            line.attrs = {_info.id, std::vector<uint8_t>(stored.begin(), stored.end())};
        if (!valid)
            result.diagnostics.push_back({Severity::Warning, static_cast<uint32_t>(document.lines.size() + 1), p, "a byte that is no character or token"});
        document.lines.push_back(std::move(line));
        if (e >= bytes.size() || bytes[e] == kEof)
        {
            p = e;
            break;
        }
        p = e + 1;
    }
    // A document ends with $FF; the bytes after it are kept
    std::vector<uint8_t> tail;
    if (p < bytes.size() && bytes[p] == kEof)
        tail.assign(bytes.begin() + static_cast<std::ptrdiff_t>(p) + 1, bytes.end());
    else
        result.diagnostics.push_back({Severity::Warning, 0, p, "the document does not end with $FF"});
    document.attrs = {_info.id, tail};
    result.ok = true;
    return result;
}

EncodeResult OdinCodec::Encode(const SourceDocument& document, const EncodeOptions&) const
{
    EncodeResult result;
    std::vector<uint8_t>& out = result.bytes;
    out = {'O', 'D', 'S', 0, kEof, 0};
    const bool same = document.format == _info.id;
    for (size_t i = 0; i < document.lines.size(); ++i)
    {
        const SourceLine& line = document.lines[i];
        std::string text;
        std::vector<uint8_t> body;
        if (same && line.attrs.codec == _info.id)
        {
            bool valid = true;
            if (!line.attrs.bytes.empty() && Expand(line.attrs.bytes, valid) == line.text)
                body = line.attrs.bytes;
            else
                body = Tokenize(line.text);
        }
        else
            body = Tokenize(line.text);
        for (const uint8_t b : body)
            if (b == 0 || b == kEof)
            {
                result.diagnostics.push_back({Severity::Error, static_cast<uint32_t>(i + 1), out.size(), "a character Odin cannot hold in a line"});
                break;
            }
        out.insert(out.end(), body.begin(), body.end());
        out.push_back(0);   // every line ends with $00
    }
    out.push_back(kEof);
    if (same && document.attrs.codec == _info.id)
        out.insert(out.end(), document.attrs.bytes.begin(), document.attrs.bytes.end());
    result.ok = !HasErrors(result.diagnostics);
    return result;
}
}  // namespace unrealasm::codecs
