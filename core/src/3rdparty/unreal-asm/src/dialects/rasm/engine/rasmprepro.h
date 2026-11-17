#pragma once

// rasm's preprocessor (rasm.c v3.3.3: EarlyPrepSrc, PreProcessing): the source text becomes rasm's word list. Ported rule for
// rule, since everything the assembler does later depends on how the words were cut:
//
// - each line is upper-cased outside quotes, tabs become blanks, a comment (; or //) becomes ':' characters, /* */ blanks,
//   `<<` / `>>` become `[` / `]`, `AF'` after a comma loses its quote, a line ending in `\` is joined with the next;
// - INCLUDE 'file' is replaced by the file's lines where it stands (whatever IF surrounds it); INCBIN / INC* 'file' become
//   `HEXBIN #n`, n indexing PreproResult::hexbins (the file is read when the directive runs);
// - the words: blanks and commas separate them until a word is an instruction, a directive or a macro name, then blanks
//   are dropped (an expression is one word up to the next comma outside parentheses); `name EQU value` becomes the word
//   `NAME~VALUE`; `name=expr` / `name<op>=expr` and comparisons give a word whose `e` is the position + 1 of the operator;
//   ':' ends a statement (the last word of a statement has t = 1).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "unrealasm/diagnostics.h"

namespace unrealasm::rasm
{
/// A word of rasm's word list (s_wordlist)
struct Word
{
    std::string w;
    int line = 0;     ///< 1-based line in its file
    int file = 0;     ///< index in PreproResult::files
    uint8_t t = 0;    ///< 0: more words follow in the statement; 1: last word of the statement; 2: the end of the list
    int e = 0;        ///< position + 1 of the '=' / comparison operator (an assignment or a condition); 0: none
};

/// A binary file named by INCBIN / INC* (s_hexbin): resolved by the assembler when the directive runs
struct HexBin
{
    std::string fileName;   ///< merged with the including file's directory (or found in an include path)
    int crunch = 0;         ///< rasm's legacy code: 0 plain, 4 LZ4, 7 ZX7, 8 EXO, 17 APU, 18 LZSA, 48 / 49 LZ48 / LZ49, 70 / 71 ZX0 / ZX0B
    int version = 0;        ///< LZSA 1 / 2
    int minMatch = 0;       ///< LZSA minimum match
    bool missingInclude = false;   ///< an INCLUDE whose file was not found (ERRRD n): an error when it is assembled
};

struct FileSystem
{
    /// The bytes of a file; none when it does not exist
    std::function<std::optional<std::vector<uint8_t>>(const std::string& path)> read;
    std::vector<std::string> includePaths;
};

struct PreproOptions
{
    bool noAmpersand = false;   ///< -amper: '&' is a hexadecimal prefix (written '#')
    bool utf8 = false;          ///< -utf8: accented letters of quoted strings mapped to the CPC's French / Spanish codes
    bool freeQuote = false;     ///< -fq: any character inside quotes
    bool macroMultiLine = false;
    bool noWarning = false;
    bool as80 = false;          ///< AS80 / UZ80 macro declarations (NAME MACRO)
};

struct PreproResult
{
    std::vector<Word> words;            ///< words[0] = "BEGIN", then two internal words, ..., last = "END" with t = 2
    std::vector<std::string> files;     ///< file names as rasm keeps them (main first)
    std::vector<HexBin> hexbins;
    std::vector<std::string> macroNames;   ///< every MACRO name the preprocessor saw (rasm's MacroFast)
    bool freeQuote = false;             ///< FREEQUOTES met or -fq
    Diagnostics diagnostics;
    bool fatal = false;                 ///< an invalid character: rasm stops (exit(1))
};

/// rasm's MergePath: `name` relative to the directory of `parent` (unless absolute or "./")
std::string MergePath(const std::string& parent, const std::string& name);

/// Preprocesses the main file (its bytes given, its name as rasm would print it)
PreproResult Preprocess(const std::string& mainName, const std::vector<uint8_t>& mainBytes, const FileSystem& fs,
                        const PreproOptions& options);

/// The instruction / directive names of rasm's keyword table (upper case)
bool IsKeyword(const std::string& word);
}  // namespace unrealasm::rasm
