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
    Run0A,        ///< 3.x, 4.0, 4.4: #0A n = n blanks
    Direct,       ///< 4.12: #02-#1F = that many blanks (#0A = ten), #01 n = n blanks
    Structural,   ///< 5.x: no blanks or separating commas stored; the editor lays the line out (see DecodeStructural)
};

struct Version
{
    Spacing spacing;
    const tasm::TokenTable& tokens;
    bool terminators = false;   ///< 5.5: a name in a line ends with a blank
};

Version VersionOf(const std::string& version)
{
    if (version == "3")
        return {Spacing::Run0A, tasm::Tasm3Tokens()};
    if (version == "4.12")
        return {Spacing::Direct, tasm::Tasm412Tokens()};
    if (version == "5.0")
        return {Spacing::Structural, tasm::Tasm40Tokens()};
    if (version == "5.5")
        return {Spacing::Structural, tasm::Tasm55Tokens(), true};
    return {Spacing::Run0A, tasm::Tasm40Tokens()};
}

// --- TASM 5.x: structural lines ------------------------------------------------------------------------------------
// A 5.x line is stored as its parts: [label][command token][operands][;comment], no blanks between them and no comma
// where the boundary between two operands is plain without one: after an operand token (register, condition), a ')',
// a closing quote or (5.5) a name, and before a quote, '#', '%', '(' or a token. A comma stays only between a letter or
// digit and a letter or digit (1,2 / #AB,LAB / LAB,LAB in 5.0). 5.5 ends every name (label definition and reference)
// with a blank. The editor shows the label in column 0, the command at 8, the operands at 16, a comment at 32. Seen on
// files typed into TASM 5.0 beta and 5.5 beta and saved (research-tasm-to-sjasmplus.md section 5).

std::string_view TokenName(const tasm::TokenTable& tokens, uint8_t b)
{
    if (b < tasm::kFirstToken || b > tasm::kLastToken)
        return {};
    std::string_view name = tokens[b - tasm::kFirstToken];
    if (!name.empty() && name.back() == ' ')
        name.remove_suffix(1);
    return name;
}

bool IsNameChar(uint8_t c)
{
    return std::isalnum(c) || c == '_' || c == '.' || c == '@' || c == '?' || c == '!' || c == '\'';
}

void Pad(std::string& line, size_t column)
{
    if (line.size() < column)
        line.append(column - line.size(), ' ');
    else
        line.push_back(' ');
}

void AppendRaw(std::string& text, uint8_t b, const tasm::TokenTable& tokens)
{
    const std::string_view name = b >= tasm::kFirstToken ? std::string_view(tokens[b - tasm::kFirstToken]) : std::string_view();
    if (b >= 0x20 && b < 0x7F)
        text.push_back(static_cast<char>(b));
    else if (!name.empty())
        text.append(name);
    else
        utf8::Append(text, kRawBase + b);
}

std::string DecodeStructural(std::span<const uint8_t> body, const Version& version)
{
    const auto& tokens = version.tokens;
    const size_t n = body.size();
    if (n == 0)
        return {};
    auto comment = [&](size_t from) {
        std::string c;
        for (size_t k = from; k < n; ++k)
        {
            if (body[k] == 0x0A && k + 1 < n)   // a blank run (lines the editor turned into comments)
            {
                c.append(body[k + 1], ' ');
                ++k;
            }
            else
                AppendRaw(c, body[k], tokens);
        }
        return c;
    };
    if (body[0] == ';')
        return comment(0);
    size_t i = 0;
    std::string label;
    if (body[0] < tasm::kFirstToken)
    {
        while (i < n && body[i] < tasm::kFirstToken && body[i] != ';' && !(version.terminators && body[i] == ' '))
            AppendRaw(label, body[i++], tokens);
        if (version.terminators && i < n && body[i] == ' ')
            ++i;
    }
    std::string command;
    if (i < n && !TokenName(tokens, body[i]).empty() && !tasm::IsOperandToken(TokenName(tokens, body[i])))
        command = std::string(TokenName(tokens, body[i++]));
    // Operands, with the commas the editor shows
    enum class End { None, Name, Plain, Operator };   // Name: a letter or digit; Plain: token, ')', quote, name terminator
    End end = End::None;
    std::string operands;
    auto separate = [&](bool alnumStart) {
        if (end == End::Plain || (end == End::Name && !alnumStart))
            operands.push_back(',');
    };
    while (i < n && body[i] != ';')
    {
        const uint8_t b = body[i];
        const std::string_view name = TokenName(tokens, b);
        if (!name.empty())
        {
            separate(false);
            operands.append(name);
            end = End::Plain;
            ++i;
        }
        else if (b == '"')
        {
            separate(false);
            size_t k = i + 1;
            while (k < n && body[k] != '"')
                ++k;
            for (size_t c = i; c <= std::min(k, n - 1); ++c)
                AppendRaw(operands, body[c], tokens);
            i = std::min(k + 1, n);
            end = End::Plain;
        }
        else if (b == ' ')
        {
            if (end == End::Name)
                end = End::Plain;   // the blank that ends a name (5.5)
            ++i;
        }
        else if (b == ',')
        {
            operands.push_back(',');
            end = End::None;
            ++i;
        }
        else if (b == '#' || b == '%' || b == '$')
        {
            separate(false);
            operands.push_back(static_cast<char>(b));
            ++i;
            while (i < n && std::isalnum(body[i]))
                operands.push_back(static_cast<char>(body[i++]));
            end = End::Name;
        }
        else if (IsNameChar(b))
        {
            separate(true);
            while (i < n && IsNameChar(body[i]) && TokenName(tokens, body[i]).empty())
                operands.push_back(static_cast<char>(body[i++]));
            end = End::Name;
        }
        else if (b == '(')
        {
            separate(false);
            operands.push_back('(');
            end = End::None;
            ++i;
        }
        else if (b == ')')
        {
            operands.push_back(')');
            end = End::Plain;
            ++i;
        }
        else
        {
            AppendRaw(operands, b, tokens);
            end = End::Operator;
            ++i;
        }
    }
    std::string line = label;
    if (!command.empty() || !operands.empty())
    {
        if (!line.empty() || !command.empty() || !operands.empty())
            Pad(line, 8);
        line += command;
        if (!operands.empty())
        {
            Pad(line, 16);
            line += operands;
        }
    }
    if (i < n)
    {
        if (!line.empty())
            Pad(line, 32);
        line += comment(i);
    }
    return line;
}

bool EncodeStructural(const std::string& text, const Version& version, std::vector<uint8_t>& body, std::string& error)
{
    const auto& tokens = version.tokens;
    body.clear();
    auto findToken = [&](const std::string& word, bool operand) -> int {
        for (size_t t = 0; t < tokens.size(); ++t)
        {
            std::string_view name = tokens[t];
            if (!name.empty() && name.back() == ' ')
                name.remove_suffix(1);
            if (name.empty() || name != word || tasm::IsOperandToken(name) != operand)
                continue;
            return static_cast<int>(t);
        }
        return -1;
    };
    auto upper = [](std::string w) {
        for (char& c : w)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return w;
    };
    // Raw bytes (U+F700 + byte) and plain characters of a text part; in a comment three or more blanks are a run
    // (#0A n), as the editor stores them
    auto plain = [&](const std::string& part, bool comment = false) {
        for (size_t k = 0; k < part.size();)
        {
            const unsigned char c = static_cast<unsigned char>(part[k]);
            if (comment && c == ' ')
            {
                size_t m = k;
                while (m < part.size() && part[m] == ' ' && m - k < 255)
                    ++m;
                if (m - k >= 3)
                {
                    body.push_back(0x0A);
                    body.push_back(static_cast<uint8_t>(m - k));
                    k = m;
                    continue;
                }
            }
            if (c >= 0x80)
            {
                char32_t cp = 0;
                const size_t length = utf8::DecodeOne(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(part.data()) + k, part.size() - k), cp);
                if (!length || cp < kRawBase || cp > kRawBase + 0xFF)
                    return false;
                body.push_back(static_cast<uint8_t>(cp - kRawBase));
                k += length;
                continue;
            }
            body.push_back(c);
            ++k;
        }
        return true;
    };
    size_t i = text.find_first_not_of(' ');
    if (i == std::string::npos)
        return true;
    if (text[i] == ';')
    {
        if (!plain(text.substr(i), true))
            return error = "a character TASM cannot hold", false;
        return true;
    }
    // Label
    if (i == 0)
    {
        size_t j = 0;
        while (j < text.size() && text[j] != ' ' && text[j] != ';')
            ++j;
        if (!plain(text.substr(0, j)))
            return error = "a character TASM cannot hold", false;
        if (version.terminators)
            body.push_back(' ');
        i = j;
    }
    while (i < text.size() && text[i] == ' ')
        ++i;
    // Command
    size_t j = i;
    while (j < text.size() && std::isalpha(static_cast<unsigned char>(text[j])))
        ++j;
    if (j > i && (j == text.size() || text[j] == ' ' || text[j] == ';'))
    {
        const int token = findToken(upper(text.substr(i, j - i)), false);
        if (token >= 0)
        {
            body.push_back(static_cast<uint8_t>(tasm::kFirstToken + token));
            i = j;
        }
    }
    // Operands up to a comment, split at commas outside quotes and parentheses
    std::vector<std::vector<uint8_t>> items;
    std::vector<uint8_t> item;
    size_t k = i;
    int depth = 0;
    bool quote = false;
    std::string part;
    auto flushPart = [&]() {
        // a part of an item without quotes: names, numbers, operators; blanks dropped
        for (size_t p = 0; p < part.size();)
        {
            const unsigned char c = static_cast<unsigned char>(part[p]);
            if (c == ' ')
            {
                ++p;
                continue;
            }
            if (c == '#' || c == '%' || c == '$' || std::isdigit(c))
            {
                item.push_back(c);
                ++p;
                while (p < part.size() && std::isalnum(static_cast<unsigned char>(part[p])))
                    item.push_back(static_cast<uint8_t>(part[p++]));
                continue;
            }
            if (std::isalpha(c) || c == '_' || c == '.' || c == '@' || c == '?' || c == '!')
            {
                size_t q = p;
                while (q < part.size() && IsNameChar(static_cast<uint8_t>(part[q])) && part[q] != '\'')
                    ++q;
                std::string word = part.substr(p, q - p);
                if (upper(word) == "AF" && q < part.size() && part[q] == '\'')
                    word += '\'', ++q;
                const int token = findToken(upper(word), true);
                if (token >= 0)
                    item.push_back(static_cast<uint8_t>(tasm::kFirstToken + token));
                else
                {
                    item.insert(item.end(), word.begin(), word.end());
                    if (version.terminators)
                        item.push_back(' ');
                }
                p = q;
                continue;
            }
            item.push_back(c);
            ++p;
        }
        part.clear();
    };
    for (; k < text.size(); ++k)
    {
        const char c = text[k];
        if (quote)
        {
            item.push_back(static_cast<uint8_t>(c));
            quote = c != '"';
            continue;
        }
        if (c == ';')
            break;
        if (c == '"')
        {
            flushPart();
            item.push_back('"');
            quote = true;
            continue;
        }
        if (c == '(')
            ++depth;
        else if (c == ')' && depth > 0)
            --depth;
        if (c == ',' && depth == 0)
        {
            flushPart();
            items.push_back(item);
            item.clear();
            continue;
        }
        part.push_back(c);
    }
    flushPart();
    if (!item.empty() || !items.empty())
        items.push_back(item);
    auto alnumByte = [&](uint8_t b) { return b < tasm::kFirstToken && std::isalnum(b); };
    for (size_t n = 0; n < items.size(); ++n)
    {
        if (n > 0 && !items[n - 1].empty() && !items[n].empty() && alnumByte(items[n - 1].back()) && alnumByte(items[n].front()))
            body.push_back(',');
        body.insert(body.end(), items[n].begin(), items[n].end());
    }
    if (k < text.size() && !plain(text.substr(k), true))
        return error = "a character TASM cannot hold", false;
    if (body.size() >= kEndMarker)
        return error = "the line is longer than a TASM record (254 bytes)", false;
    return true;
}

/// A 5.x file: lines without a label start right with a command token (4.x stores blanks before the command)
bool LooksStructural(const std::vector<std::span<const uint8_t>>& bodies)
{
    size_t token = 0, blank = 0;
    for (const auto& body : bodies)
    {
        if (body.empty() || body[0] == ';')
            continue;
        if (body[0] >= tasm::kFirstToken)
            ++token;
        else if (body[0] == 0x0A || body[0] == ' ' || (body[0] >= 0x01 && body[0] <= 0x1F))
            ++blank;
    }
    return token > 0 && token > blank * 4;
}

/// How well a table reads a 5.x file: a command token where the command stands, operand tokens after it
size_t StructuralScore(const std::vector<std::span<const uint8_t>>& bodies, const tasm::TokenTable& tokens)
{
    size_t score = 0;
    for (const auto& body : bodies)
    {
        size_t i = 0;
        while (i < body.size() && body[i] < tasm::kFirstToken && body[i] != ';')
            ++i;
        if (i >= body.size() || body[i] == ';')
            continue;
        const std::string_view first = TokenName(tokens, body[i]);
        if (first.empty() || tasm::IsOperandToken(first))
            continue;
        ++score;
        for (size_t k = i + 1; k < body.size() && body[k] != ';'; ++k)
            if (body[k] >= tasm::kFirstToken)
            {
                const std::string_view name = TokenName(tokens, body[k]);
                if (!name.empty() && tasm::IsOperandToken(name))
                    ++score;
                else
                    score = score > 0 ? score - 1 : 0;
            }
    }
    return score;
}

/// Blanks at body[i]: their count and the bytes they take; 0 when body[i] is not a blank run
size_t BlankRun(std::span<const uint8_t> body, size_t i, Spacing spacing, size_t& taken)
{
    const uint8_t b = body[i];
    if (spacing == Spacing::Structural)
        return 0;
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
    // A keyword never starts or ends next to these (a backslash starts a TASM 4.12 macro parameter: \r, \0)
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '@' || c == '$' || c == '#' || c == '\\';
}
constexpr size_t kTabStop = 8;
constexpr uint16_t kTasm2Start = 38750;   // TASM 2.0 saves sources as type C with this start

/// TASM 2.0 keeps its source as text: printable bytes, TAB, CR LF
bool LooksLikeTasm2Text(std::span<const uint8_t> bytes)
{
    if (bytes.empty())
        return false;
    size_t crlf = 0;
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        const uint8_t b = bytes[i];
        if (b == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n')
            ++crlf, ++i;
        else if (b < 0x20 && b != '\t')
            return false;
    }
    return crlf > 0;
}

/// One TASM 2.0 line -> text: TABs to the next multiple of 8 (as the editor shows them), CP866 above #7F
std::string DecodeTasm2Line(std::span<const uint8_t> line)
{
    std::string text;
    size_t column = 0;
    for (const uint8_t b : line)
    {
        if (b == '\t')
        {
            const size_t next = (column / kTabStop + 1) * kTabStop;
            text.append(next - column, ' ');
            column = next;
            continue;
        }
        if (b >= 0x20 && b < 0x80)
            text.push_back(static_cast<char>(b));
        else if (b >= 0x80)
            utf8::Append(text, encoding::ByteToCodePoint(b, encoding::CodePage::Cp866));
        else
            utf8::Append(text, kRawBase + b);
        ++column;
    }
    return text;
}

/// Text -> one TASM 2.0 line the way its editor stores it: a blank run that reaches a tab stop becomes one TAB per
/// stop, the blanks after the last stop stay (strings and comments included)
bool EncodeTasm2Line(const std::string& text, std::vector<uint8_t>& out, std::string& error)
{
    out.clear();
    const auto* data = reinterpret_cast<const uint8_t*>(text.data());
    size_t column = 0, i = 0;
    while (i < text.size())
    {
        if (text[i] == ' ')
        {
            size_t end = i;
            while (end < text.size() && text[end] == ' ')
                ++end;
            const size_t from = column, to = column + (end - i);
            size_t at = from;
            for (size_t stop = (from / kTabStop + 1) * kTabStop; stop <= to; stop += kTabStop)
            {
                out.push_back('\t');
                at = stop;
            }
            out.insert(out.end(), to - at, ' ');
            column = to;
            i = end;
            continue;
        }
        char32_t cp = 0;
        const size_t length = utf8::DecodeOne(std::span<const uint8_t>(data + i, text.size() - i), cp);
        uint8_t b = 0;
        if (length == 0)
        {
            error = "invalid UTF-8 at column " + std::to_string(column + 1);
            return false;
        }
        if (cp >= kRawBase && cp <= kRawBase + 0xFF)
            b = static_cast<uint8_t>(cp - kRawBase);
        else if (cp < 0x80)
            b = static_cast<uint8_t>(cp);
        else if (!encoding::CodePointToByte(cp, encoding::CodePage::Cp866, b))
        {
            error = "a character TASM cannot hold at column " + std::to_string(column + 1);
            return false;
        }
        out.push_back(b);
        ++column;
        i += length;
    }
    return true;
}
}  // namespace

TasmCodec::TasmCodec()
    : _info{"tasm", "TASM source (tokenized)", "tasm", CodecFamily::Tokenized,
            {{"2.0", "TASM 2.0 (Rst7): plain text"},
             {"3", "TASM 3.0-3.5 (Rst7)"},
             {"4.0", "TASM 4.0 (XL Design) / 4.4 (KVA)"},
             {"4.12", "TASM 4.12 (Rst7)"},
             {"5.0", "TASM 5.0 beta (XL Design): structural lines, the TASM 4.0 table"},
             {"5.5", "TASM 5.5 beta (XL Design): structural lines, its own table"}}}
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
    // TASM 2.0: a type-C file with its start, or plain text where the others have records
    bool ended = false;
    const auto bodies = Bodies(bytes, ended);
    if ((hints.type == 'C' && hints.start == kTasm2Start && LooksLikeTasm2Text(bytes)) || (!ended && LooksLikeTasm2Text(bytes)))
    {
        if (consistent)
            *consistent = {"2.0"};
        return "2.0";
    }
    if (!ended)
        return {};
    // TASM 5.x: structural lines whatever the catalog says (its start field holds the editor's state); the table that
    // reads the commands best
    if (LooksStructural(bodies))
    {
        const size_t v50 = StructuralScore(bodies, tasm::Tasm40Tokens()), v55 = StructuralScore(bodies, tasm::Tasm55Tokens());
        const std::string chosen = v55 > v50 ? "5.5" : "5.0";
        if (consistent)
            *consistent = {chosen};
        return chosen;
    }
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
    if (hints.type == 'C' && hints.start == kTasm2Start && LooksLikeTasm2Text(bytes))
        return 95;   // TASM 2.0 text; without the catalog any text codec reads it as well
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
    const Version v = VersionOf(version);
    if (v.spacing == Spacing::Structural)
        return DecodeStructural(body, v);
    const auto& spacing = v.spacing;
    const auto& tokens = v.tokens;
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
    const Version v = VersionOf(version);
    if (v.spacing == Spacing::Structural)
        return EncodeStructural(text, v, body, error);
    const auto& spacing = v.spacing;
    const auto& tokens = v.tokens;
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
    // TASM tokenizes every keyword word, in any case, wherever it stands: labels, operands, strings and comments alike
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
        if (std::isalpha(static_cast<unsigned char>(c)) && (i == 0 || !IsWordChar(text[i - 1])))
        {
            // A whole word of letters, compared with the keywords without regard to case (TASM shows them in capitals)
            size_t j = i;
            while (j < n && std::isalpha(static_cast<unsigned char>(text[j])))
                ++j;
            std::string word(text.data() + i, j - i);
            for (char& ch : word)
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            const bool wordEnds = j >= n || !IsWordChar(text[j]);
            int token = -1;
            size_t consumed = 0;
            for (size_t t = 0; t < tokens.size() && token < 0; ++t)
            {
                const std::string_view name = tokens[t];
                if (name.empty())
                    continue;
                if (name == "AF'" && word == "AF" && j < n && text[j] == '\'')
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
    if (document.subversion == "2.0")
    {
        // Lines end with CR LF; a last line without one is marked in the file attributes
        document.codePage = encoding::CodePage::Cp866;
        document.lineEnd = encoding::LineEnd::CrLf;
        size_t start = 0;
        for (size_t i = 0; i + 1 < bytes.size(); ++i)
            if (bytes[i] == '\r' && bytes[i + 1] == '\n')
            {
                const auto line = bytes.subspan(start, i - start);
                document.lines.push_back({DecodeTasm2Line(line), {_info.id, std::vector<uint8_t>(line.begin(), line.end())}});
                start = i + 2;
                ++i;
            }
        const bool unterminated = start < bytes.size();
        if (unterminated)
        {
            const auto line = bytes.subspan(start);
            document.lines.push_back({DecodeTasm2Line(line), {_info.id, std::vector<uint8_t>(line.begin(), line.end())}});
        }
        document.attrs = {_info.id, {static_cast<uint8_t>(unterminated ? 1 : 0)}};
        result.ok = true;
        return result;
    }
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
    if (version == "2.0")
    {
        std::vector<uint8_t> line;
        const bool unterminated = keep && document.attrs.codec == _info.id && document.attrs.bytes == std::vector<uint8_t>{1};
        for (size_t i = 0; i < document.lines.size(); ++i)
        {
            const SourceLine& source = document.lines[i];
            std::string error;
            if (keep && source.attrs.codec == _info.id && DecodeTasm2Line(source.attrs.bytes) == source.text)
                line = source.attrs.bytes;
            else if (!EncodeTasm2Line(source.text, line, error))
            {
                result.diagnostics.push_back({Severity::Error, static_cast<uint32_t>(i + 1), result.bytes.size(), error});
                continue;
            }
            result.bytes.insert(result.bytes.end(), line.begin(), line.end());
            if (!(unterminated && i + 1 == document.lines.size()))
                result.bytes.insert(result.bytes.end(), {'\r', '\n'});
        }
        result.ok = !HasErrors(result.diagnostics);
        return result;
    }
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
            if (sameFormat && !document.subversion.empty() && document.subversion != version && document.subversion != "2.0")
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
