#include "symbols/codecs/text/tokenizer.h"

#include <cctype>

namespace unrealasm::symbols::text
{
namespace
{
bool Digits(std::string_view s, int base, int64_t& value)
{
    if (s.empty() || s.size() > 32)
        return false;
    uint64_t v = 0;
    for (const char c : s)
    {
        const int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                      : std::isalpha(static_cast<unsigned char>(c)) ? std::toupper(static_cast<unsigned char>(c)) - 'A' + 10
                                                                    : 99;
        if (d >= base)
            return false;
        v = v * static_cast<uint64_t>(base) + static_cast<uint64_t>(d);
        if (v > 0xFFFFFFFFull)
            return false;
    }
    value = static_cast<int64_t>(v);
    return true;
}

bool IsWordChar(char c, std::string_view extra)
{
    return std::isalnum(static_cast<unsigned char>(c)) || extra.find(c) != std::string_view::npos;
}
}  // namespace

bool ParseNumber(std::string_view w, int64_t& value, bool octal)
{
    if (w.empty())
        return false;
    if (w[0] == '#' || w[0] == '$')
        return Digits(w.substr(1), 16, value);
    if (w[0] == '%')
        return Digits(w.substr(1), 2, value);
    if (w.size() > 2 && w[0] == '0' && (w[1] == 'x' || w[1] == 'X'))
        return Digits(w.substr(2), 16, value);
    if (w.size() > 2 && w[0] == '0' && (w[1] == 'b' || w[1] == 'B') && Digits(w.substr(2), 2, value))
        return true;
    if (!std::isdigit(static_cast<unsigned char>(w[0])))
        return false;
    const char last = static_cast<char>(std::tolower(static_cast<unsigned char>(w.back())));
    const std::string_view body = w.substr(0, w.size() - 1);
    if (last == 'h')
        return Digits(body, 16, value);
    if (last == 'b' && Digits(body, 2, value))
        return true;
    if (octal && (last == 'q' || last == 'o'))
        return Digits(body, 8, value);
    return Digits(w, 10, value);
}

std::vector<Token> Tokenize(std::string_view line, const TokenizerOptions& options)
{
    std::vector<Token> out;
    size_t p = 0;
    while (p < line.size())
    {
        const char c = line[p];
        if (c == ' ' || c == '\t')
        {
            ++p;
            continue;
        }
        Token t;
        t.column = static_cast<uint32_t>(p + 1);
        const size_t start = p;
        if (options.comments.find(c) != std::string_view::npos || (options.slashComment && line.substr(p, 2) == "//"))
        {
            t.kind = TokenKind::Comment;
            p = line.size();
        }
        else if (c == '"' || c == '\'')
        {
            t.kind = TokenKind::String;
            ++p;
            while (p < line.size() && line[p] != c)
                p += options.escapes && line[p] == '\\' && p + 1 < line.size() ? 2 : 1;
            if (p < line.size())
                ++p;   // the closing quote
        }
        else if (std::isdigit(static_cast<unsigned char>(c)) || ((c == '#' || c == '$' || c == '%') && p + 1 < line.size() &&
                                                                  std::isxdigit(static_cast<unsigned char>(line[p + 1]))))
        {
            ++p;
            while (p < line.size() && std::isalnum(static_cast<unsigned char>(line[p])))
                ++p;
            t.kind = ParseNumber(line.substr(start, p - start), t.value, options.octal) ? TokenKind::Number : TokenKind::Ident;
        }
        else if (IsWordChar(c, options.identExtra))
        {
            t.kind = TokenKind::Ident;
            while (p < line.size() && IsWordChar(line[p], options.identExtra))
                ++p;
        }
        else
        {
            t.kind = TokenKind::Punct;
            ++p;
        }
        t.text = line.substr(start, p - start);
        out.push_back(t);
    }
    return out;
}
}  // namespace unrealasm::symbols::text
