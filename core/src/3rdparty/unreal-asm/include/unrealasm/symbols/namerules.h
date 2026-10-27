#pragma once

// What names an export target takes and how a name it cannot take is changed (symbols/architecture.md DT-3), and what
// happens to a symbol whose place the format cannot hold (DT-4). Every symbol codec writes through Prepare(), so each
// format renames and folds the same way and reports it the same way.

#include <string>
#include <string_view>
#include <vector>

#include "unrealasm/symbols/symbol.h"

namespace unrealasm::symbols
{
enum class Charset : uint8_t
{
    Any,         ///< every character but the line break (and `forbidden`)
    NoBlank,     ///< no blank or tab (a word of a line format)
    Identifier,  ///< letters, digits and `extra`
};

struct NameRules
{
    Charset charset = Charset::NoBlank;
    std::string_view extra;          ///< Identifier: characters besides letters and digits
    std::string_view firstExtra;     ///< Identifier: characters a name may start with besides letters
    std::string_view forbidden;      ///< characters never taken (a format's separators)
    bool upper = false;              ///< the target writes names in capitals
    size_t maxLength = 0;            ///< 0 = no limit
    bool reserveZ80 = false;         ///< Z80 mnemonics, registers and conditions are reserved
    std::vector<std::string_view> reserved;   ///< more reserved words (case-insensitive)
};

struct Rename
{
    std::string from;
    std::string to;
};

/// DT-3: the names of every symbol valid for the target and collision-free: an invalid character becomes '_', a name
/// that may not start so gets a '_' in front, a reserved word a '_' after it, a long name is cut and gets a 4-digit
/// hash, a clash _2, _3, ...; parents follow their symbol's new name. A name read from `keepFrom` (the target format
/// itself) is kept as that format wrote it
std::vector<Rename> ApplyNameRules(SymbolFile& file, const NameRules& rules, std::string_view keepFrom = {});

/// DT-4: what a codec does with a symbol whose space it cannot hold (a page in a format without pages)
enum class Unrepresentable : uint8_t
{
    Fold,        ///< written at its CPU address (the window its source showed it in, else the usual one)
    Comment,     ///< written as a comment line, when the format has comments (else dropped)
    Drop,        ///< left out
};

std::string_view UnrepresentableName(Unrepresentable u);
bool ParseUnrepresentable(std::string_view text, Unrepresentable& out);
}  // namespace unrealasm::symbols
