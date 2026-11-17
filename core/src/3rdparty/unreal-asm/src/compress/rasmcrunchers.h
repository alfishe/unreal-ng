#pragma once

// rasm's crunchers ported to C++ (rasmcrunchers.cpp): LZ48 v005 and LZ49 v002 (rasm.c, MIT, Edouard Berge) and the
// ZX7 compressor in the form rasm carries it (zx7.h, BSD-3-Clause, Einar Saukas), plus decoders for them and for
// Exomizer 2 raw. Byte-identical to rasm: same match search, same tie breaks, same integer types where they matter.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace unrealasm::compress::detail
{
/// LZ48_encode_legacy / LZ49_encode_legacy; size must be at least 1 (rasm never crunches nothing)
std::vector<uint8_t> Lz48Encode(const uint8_t* data, size_t size);
std::vector<uint8_t> Lz49Encode(const uint8_t* data, size_t size);
/// ZX7_compress(zx7_optimize(...)); size must be at least 1
std::vector<uint8_t> Zx7Encode(const uint8_t* data, size_t size);

/// Decoders: none for damaged data (a read or a reference outside the buffers, output over the limit)
std::optional<std::vector<uint8_t>> Lz48Decode(const uint8_t* data, size_t size, size_t limit);
std::optional<std::vector<uint8_t>> Lz49Decode(const uint8_t* data, size_t size, size_t limit);
std::optional<std::vector<uint8_t>> Zx7Decode(const uint8_t* data, size_t size, size_t limit);
/// aPLib, as apultra_decompress reads it (that one reads past the end of damaged data and its size probe loops)
std::optional<std::vector<uint8_t>> ApUltraDecode(const uint8_t* data, size_t size, size_t limit);
/// Exomizer 2 raw, as exomizer's exodecrunch reads what exocrunch writes
std::optional<std::vector<uint8_t>> ExomizerDecode(const uint8_t* data, size_t size, size_t limit);
}  // namespace unrealasm::compress::detail
