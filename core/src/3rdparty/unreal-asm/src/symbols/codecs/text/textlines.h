#pragma once

// Helpers shared by the line-based symbol formats: lines, trimming, numbers, the "(TYPE)" kind token, comments.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "unrealasm/symbols/symbol.h"

namespace unrealasm::symbols::text
{
/// The lines of a text (LF, CR LF; a final line without a break included)
std::vector<std::string_view> Lines(std::span<const uint8_t> bytes);
std::string_view Trim(std::string_view s);
/// A comment line or an empty one (';' and '#' start comments)
bool Skipped(std::string_view trimmed);
/// Hex digits with an optional "0x" or "$" prefix, at most 8 digits
bool ParseHex(std::string_view s, uint32_t& out);
/// Splits "code ; comment" at the first ';' (both trimmed)
void SplitComment(std::string_view line, std::string_view& code, std::string_view& comment);
/// The words of a line split at blanks and tabs
std::vector<std::string_view> Words(std::string_view s);
/// "(CODE)" -> kind Code; "(bss)" -> kind Unknown with provenance.type "bss"; false when the word is no "(...)"
bool ApplyTypeToken(std::string_view word, Symbol& s);
/// The type word a symbol is written with ("code", "bss"); "" when it has none
std::string TypeWord(const Symbol& s);
std::string Hex(uint32_t value, int digits);
std::string Upper(std::string_view s);
/// Detection: the share of a probe's data lines a line test accepts -> 0..base
struct LineScore
{
    size_t data = 0;
    size_t matched = 0;
    int Score(int base) const;
};
}  // namespace unrealasm::symbols::text
