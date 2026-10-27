#pragma once

// The shared lexer of the text symbol formats (symbols/tdd.md §4.1): one line into tokens that view the line (no
// allocation per token), with 1-based columns for diagnostics.
//
//   Number   decimal 1234; hex #C000 $C000 0xC000 C000h 0C000H; binary %1010 0b1010 1010b; octal 17q 17o (Options)
//   Ident    the format's identifier characters, never starting with a digit
//   String   "..." and '...'; \ escapes when the format enables them
//   Punct    one character: : = , ( ) [ ] { } | + - and the rest that is not part of another token
//   Comment  from a comment starter to the end of the line

#include <cstdint>
#include <string_view>
#include <vector>

namespace unrealasm::symbols::text
{
enum class TokenKind : uint8_t
{
    Number,
    Ident,
    String,
    Punct,
    Comment,
};

struct Token
{
    TokenKind kind = TokenKind::Punct;
    std::string_view text;      ///< as written (a string with its quotes)
    uint32_t column = 1;
    int64_t value = 0;          ///< Number
};

struct TokenizerOptions
{
    std::string_view identExtra = "_.?!@$";   ///< characters an identifier may hold besides letters and digits
    std::string_view comments = ";";          ///< characters that start a comment
    bool slashComment = false;                ///< "//" starts a comment
    bool octal = false;                       ///< 17q / 17o
    bool escapes = false;                     ///< \ in strings
};

/// Every notation of TokenizerOptions in one word; false when the word is no number
bool ParseNumber(std::string_view word, int64_t& value, bool octal = false);

std::vector<Token> Tokenize(std::string_view line, const TokenizerOptions& options = {});
}  // namespace unrealasm::symbols::text
