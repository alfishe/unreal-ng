#pragma once

// The crunchers of the rasm assembler (research-rasm-crunchers.md): the bytes rasm 3.3.x emits for a crunched section
// (LZ4 ... LZCLOSE, LZX0 ... LZCLOSE, ...) or a file included crunched (INCZX0 'file', INCL48 'file', ...).
// The compressors are the very sources rasm links (LZ4 1.7 HC, ZX7, Exomizer 2 raw, salvador 1.4.2, apultra 1.3.6,
// lzsa 1.3.5) and ports of rasm's own LZ48 / LZ49, called with rasm's parameters, so the output is byte-identical.
// All functions are thread-safe.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace unrealasm::compress
{
enum class Algorithm
{
    Lz4,          ///< LZ4 / INCLZ4: LZ4 1.7 HC level 9, raw block without frame, output limited to 64K
    Lz48,         ///< LZ48 / INCL48: rasm's own (Roudoudou's) LZ48, 8-bit offsets
    Lz49,         ///< LZ49 / INCL49: rasm's own LZ49, 9-bit offsets
    Zx7,          ///< LZX7 / INCZX7: ZX7 (Einar Saukas), the "optimal" compressor
    Zx0,          ///< LZX0 / INCZX0: ZX0 v2 (inverted Elias), salvador, max offset 32640
    Zx0Backward,  ///< LZX0B / INCZX0B: ZX0 v2 backward, salvador on the reversed data, output reversed back
    Exomizer,     ///< LZEXO / INCEXO: Exomizer 2 raw (forward), default options
    ApUltra,      ///< LZAPU / INCAPU: aPLib format, apultra, window 64K
    Lzsa1,        ///< LZSA1 / INCLZSA1: LZSA1 raw block, minimum match 5 unless a section names another
    Lzsa2,        ///< LZSA2 / INCLZSA2: LZSA2 raw block, minimum match 2 unless a section names another
};

/// Where the bytes come from: rasm treats a failed or useless crunch differently in the two places
enum class Origin
{
    Section,  ///< LZxxx ... LZCLOSE: a crunch that grows the data keeps the section as it is, with a warning
    Incbin,   ///< INCxxx 'file': a crunch that does not shrink the data is an error
};

struct Options
{
    Origin origin = Origin::Section;
    /// LZSA1 / LZSA2 section argument (LZSA1 4 ... LZCLOSE); outside 2..5 = rasm's default (LZSA1 5, LZSA2 2).
    /// INCLZSA1 / INCLZSA2 always use the default, so the value is ignored for Origin::Incbin.
    int lzsaMinMatch = 0;
};

struct Result
{
    std::vector<uint8_t> bytes;  ///< what rasm puts in memory for this input (empty when error is set)
    std::string error;           ///< rasm stops with this error (Incbin crunch that does not shrink, LZSA over 64K ...)
    std::string warning;         ///< rasm prints this warning and goes on (section kept uncrunched, empty input)
    bool crunched = false;       ///< bytes are the cruncher's output; false: kept as they were, or empty
};

/// The bytes rasm emits for data crunched with an algorithm, from a section or an INCxxx file
Result Crunch(Algorithm algorithm, const uint8_t* data, size_t size, const Options& options = {});

/// The plain compressor output as rasm's cruncher returns it, before rasm decides to keep it (no section / INCBIN
/// policy): empty input or a compressor failure gives an error
Result CrunchRaw(Algorithm algorithm, const uint8_t* data, size_t size, int lzsaMinMatch = 0);

/// Decompresses the output of CrunchRaw (round trips, debuggers); error when the data is damaged
Result Decrunch(Algorithm algorithm, const uint8_t* data, size_t size);

/// The algorithm of a rasm directive, case-insensitive: section openers (LZ4, LZ48, LZ49, LZX7, LZX0, LZX0B, LZEXO,
/// LZAPU, LZSA1, LZSA2) and crunched includes (INCLZ4, INCL48, INCL49, INCZX7, INCZX0, INCZX0B, INCEXO, INCAPU,
/// INCLZSA1, INCLZSA2); none for anything else (INCBIN, LZCLOSE ...)
std::optional<Algorithm> AlgorithmFromRasmDirective(std::string_view name);

/// rasm's section directive for an algorithm ("LZX0B")
std::string_view RasmSectionDirective(Algorithm algorithm);
/// rasm's crunched include directive for an algorithm ("INCZX0B")
std::string_view RasmIncludeDirective(Algorithm algorithm);
}  // namespace unrealasm::compress
