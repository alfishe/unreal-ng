#include "symbols/codecs/text/textlines.h"

#include <cctype>

namespace unrealasm::symbols::text
{
std::vector<std::string_view> Lines(std::span<const uint8_t> bytes)
{
    std::vector<std::string_view> out;
    const std::string_view all(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    size_t start = 0;
    while (start < all.size())
    {
        size_t end = all.find('\n', start);
        if (end == std::string_view::npos)
            end = all.size();
        std::string_view line = all.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        out.push_back(line);
        start = end + 1;
    }
    return out;
}

std::string_view Trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}

bool Skipped(std::string_view trimmed)
{
    return trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#';
}

bool ParseHex(std::string_view s, uint32_t& out)
{
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s.remove_prefix(2);
    else if (!s.empty() && s[0] == '$')
        s.remove_prefix(1);
    if (s.empty() || s.size() > 8)
        return false;
    uint32_t value = 0;
    for (const char c : s)
    {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
        value = value * 16 + static_cast<uint32_t>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::toupper(static_cast<unsigned char>(c)) - 'A' + 10);
    }
    out = value;
    return true;
}

void SplitComment(std::string_view line, std::string_view& code, std::string_view& comment)
{
    const size_t semicolon = line.find(';');
    code = Trim(line.substr(0, semicolon));
    comment = semicolon == std::string_view::npos ? std::string_view() : Trim(line.substr(semicolon + 1));
}

std::vector<std::string_view> Words(std::string_view s)
{
    std::vector<std::string_view> out;
    size_t p = 0;
    while (p < s.size())
    {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t'))
            ++p;
        const size_t start = p;
        while (p < s.size() && s[p] != ' ' && s[p] != '\t')
            ++p;
        if (p > start)
            out.push_back(s.substr(start, p - start));
    }
    return out;
}

bool ApplyTypeToken(std::string_view word, Symbol& s)
{
    if (word.size() < 3 || word.front() != '(' || word.back() != ')')
        return false;
    std::string type(word.substr(1, word.size() - 2));
    for (char& c : type)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!ParseKind(type, s.kind) || s.kind == SymbolKind::Unknown)
    {
        s.kind = SymbolKind::Unknown;
        s.provenance.type = type;
    }
    return true;
}

std::string TypeWord(const Symbol& s)
{
    if (s.kind != SymbolKind::Unknown)
        return std::string(KindName(s.kind));
    return s.provenance.type;
}

std::string Hex(uint32_t value, int digits)
{
    static const char kDigits[] = "0123456789ABCDEF";
    std::string out(static_cast<size_t>(digits), '0');
    for (int i = digits - 1; i >= 0; --i, value >>= 4)
        out[static_cast<size_t>(i)] = kDigits[value & 0xF];
    return out;
}

std::string Upper(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

int LineScore::Score(int base) const
{
    if (matched == 0 || data == 0)
        return 0;
    // Most data lines must match; a file of a few lines scores lower
    if (matched * 10 < data * 8)
        return base / 4;
    return matched >= 3 ? base : base * 2 / 3;
}
}  // namespace unrealasm::symbols::text
