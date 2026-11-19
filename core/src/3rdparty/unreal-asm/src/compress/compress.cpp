// The crunchers of rasm (include/unrealasm/compress.h). Every call mirrors rasm.c 3.3.x: the section crunch (the
// "c r u n c h   L Z   s e c t i o n s" pass of the assembler) and the crunched INCBIN (the hexbin output of INCxxx),
// with their buffer sizes, flags, the zx0_reverse around the backward ZX0, and what each does with a crunch that
// fails or grows the data. research-rasm-crunchers.md lists the calls and the quirks.

#include "unrealasm/compress.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstddef>
#include <memory>
#include <mutex>

#include "compress/exomizer/exomizerunit.h"
#include "compress/lz4/lz4unit.h"
#include "compress/rasmcrunchers.h"

// The vendored compressors' entry points, declared as rasm.c declares them (the libraries' own headers share include
// guard names such as _SHRINK_H, so they cannot meet in one translation unit)
extern "C"
{
    size_t salvador_compress(const unsigned char* pInputData, unsigned char* pOutBuffer, const size_t nInputSize,
                             const size_t nMaxOutBufferSize, const unsigned int nFlags, const size_t nMaxOffset,
                             const size_t nDictionarySize,
                             void (*progress)(long long nOriginalSize, long long nCompressedSize), void* pStats);
    size_t salvador_get_max_compressed_size(const size_t nInputSize);
    size_t salvador_get_max_decompressed_size(const unsigned char* pInputData, size_t nInputSize,
                                              const unsigned int nFlags);
    size_t salvador_decompress(const unsigned char* pInputData, unsigned char* pOutData, size_t nInputSize,
                               size_t nMaxOutBufferSize, size_t nDictionarySize, const unsigned int nFlags);

    size_t apultra_compress(const unsigned char* pInputData, unsigned char* pOutBuffer, size_t nInputSize,
                            size_t nMaxOutBufferSize, const unsigned int nFlags, size_t nMaxWindowSize,
                            size_t nDictionarySize,
                            void (*progress)(long long nOriginalSize, long long nCompressedSize), void* pStats);
    size_t apultra_get_max_compressed_size(size_t nInputSize);

    size_t lzsa_compress_inmem(unsigned char* pInputData, unsigned char* pOutBuffer, size_t nInputSize,
                               size_t nMaxOutBufferSize, const unsigned int nFlags, const int nMinMatchSize,
                               const int nFormatVersion);
    size_t lzsa_get_max_compressed_size_inmem(size_t nInputSize);
    int lzsa_decompressor_expand_block(unsigned char* pInBlock, int nBlockSize, unsigned char* pOutData,
                                       int nOutDataOffset, int nBlockMaxSize, const int nFormatVersion,
                                       const int nFlags);
}

namespace unrealasm::compress
{
namespace
{
constexpr unsigned int kSalvadorInverted = 1;  // FLG_IS_INVERTED: ZX0 v2
constexpr unsigned int kSalvadorBackward = 2;  // FLG_IS_BACKWARD
constexpr size_t kSalvadorMaxOffset = 32640;   // rasm's ZX0 window
constexpr size_t kApultraWindow = 65536;
constexpr unsigned int kLzsaRawBlock = 1u << 1;  // LZSA_FLAG_RAW_BLOCK
constexpr int kLz4Capacity = 65536;              // LZ4_crunch: a 64K destination
constexpr int kLz4Level = 9;                     // LZ4HC_CLEVEL_DEFAULT
constexpr size_t kLzsaRawLimit = 65536;          // LZSA_crunch refuses a longer output
constexpr size_t kExomizerLimit = 65536;         // exomizer's tables are 64K long
constexpr size_t kDecrunchLimit = 16u << 20;     // Decrunch: the largest output it builds
// salvador and lzsa decoders can read a byte past the end of damaged data (lzsa v2's 13-bit offset after a nibble
// fetch): their input copy gets zeros past the size they are given
constexpr size_t kDecoderInputSlack = 16;

// Exomizer keeps its search buffers and tables in statics
std::mutex& ExomizerLock()
{
    static std::mutex lock;
    return lock;
}

struct Raw
{
    bool ok = false;
    std::vector<uint8_t> bytes;
    std::string error;
};

Raw Fail(std::string error)
{
    Raw raw;
    raw.error = std::move(error);
    return raw;
}

Raw Ok(std::vector<uint8_t> bytes)
{
    Raw raw;
    raw.ok = true;
    raw.bytes = std::move(bytes);
    return raw;
}

// salvador_compress returns (size_t)-1 when the output does not fit nMaxOutBufferSize: rasm sizes the buffer as the
// input for sections and backward ZX0, as the worst case for the forward INCZX0
Raw Zx0(const uint8_t* data, size_t size, bool backward, size_t maxOut)
{
    std::vector<uint8_t> input(data, data + size);
    if (backward)
    {
        std::reverse(input.begin(), input.end());
    }
    std::vector<uint8_t> out(std::max(maxOut, size_t{1}) + 16, 0);
    const size_t length = salvador_compress(input.data(), out.data(), size, maxOut,
                                            kSalvadorInverted | (backward ? kSalvadorBackward : 0), kSalvadorMaxOffset,
                                            0, nullptr, nullptr);
    if (length == static_cast<size_t>(-1) || length > maxOut)
    {
        return Fail("ZX0 output does not fit " + std::to_string(maxOut) + " bytes");
    }
    out.resize(length);
    if (backward)
    {
        std::reverse(out.begin(), out.end());
    }
    return Ok(std::move(out));
}

int LzsaMinMatch(int version, int minMatch)
{
    if (minMatch < 2 || minMatch > 5)
    {
        return version == 1 ? 5 : 2;
    }
    return minMatch;
}

// The cruncher call; zx0Max: the salvador output limit (0 = the worst case size, as the forward INCZX0)
Raw RunCruncher(Algorithm algorithm, const uint8_t* data, size_t size, int lzsaMinMatch, size_t zx0Max)
{
    switch (algorithm)
    {
        case Algorithm::Lz4: {
            if (size > static_cast<size_t>(INT_MAX))
            {
                return Fail("LZ4 input is too long");
            }
            std::vector<uint8_t> out(kLz4Capacity, 0);
            const int length =
                UnrealAsmLz4CompressHc(reinterpret_cast<const char*>(data), reinterpret_cast<char*>(out.data()),
                                       static_cast<int>(size), kLz4Capacity, kLz4Level);
            if (length <= 0)
            {
                return Fail("LZ4 output does not fit 64K");
            }
            out.resize(static_cast<size_t>(length));
            return Ok(std::move(out));
        }
        case Algorithm::Lz48:
            return Ok(detail::Lz48Encode(data, size));
        case Algorithm::Lz49:
            return Ok(detail::Lz49Encode(data, size));
        case Algorithm::Zx7:
            return Ok(detail::Zx7Encode(data, size));
        case Algorithm::Zx0:
            return Zx0(data, size, false, zx0Max != 0 ? zx0Max : salvador_get_max_compressed_size(size));
        case Algorithm::Zx0Backward:
            return Zx0(data, size, true, zx0Max != 0 ? zx0Max : salvador_get_max_compressed_size(size));
        case Algorithm::Exomizer: {
            if (size > kExomizerLimit)
            {
                return Fail("Exomizer cannot crunch more than 64K");
            }
            int length = 0;
            unsigned char* crunched = nullptr;
            {
                const std::lock_guard<std::mutex> lock(ExomizerLock());
                crunched = UnrealAsmExomizerCrunch(data, static_cast<int>(size), &length);
            }
            if (crunched == nullptr)
            {
                return Fail("Exomizer: out of memory");
            }
            std::vector<uint8_t> out(crunched, crunched + length);
            UnrealAsmExomizerFree(crunched);
            return Ok(std::move(out));
        }
        case Algorithm::ApUltra: {
            const size_t maxOut = apultra_get_max_compressed_size(size);
            std::vector<uint8_t> out(maxOut, 0);
            const size_t length =
                apultra_compress(data, out.data(), size, maxOut, 0, kApultraWindow, 0, nullptr, nullptr);
            if (length == static_cast<size_t>(-1))
            {
                return Fail("APULTRA compression error");
            }
            out.resize(length);
            return Ok(std::move(out));
        }
        case Algorithm::Lzsa1:
        case Algorithm::Lzsa2: {
            const int version = algorithm == Algorithm::Lzsa1 ? 1 : 2;
            const size_t maxOut = lzsa_get_max_compressed_size_inmem(size);
            std::vector<uint8_t> out(maxOut, 0);
            // lzsa_compress_inmem does not write its input, the pointer is not const upstream
            std::vector<uint8_t> input(data, data + size);
            const size_t length = lzsa_compress_inmem(input.data(), out.data(), size, maxOut, kLzsaRawBlock,
                                                      LzsaMinMatch(version, lzsaMinMatch), version);
            if (length == static_cast<size_t>(-1) || length > kLzsaRawLimit)
            {
                return Fail(size > kLzsaRawLimit ? "LZSA cannot crunch more than 64K in RAW MODE"
                                                 : "LZSA cannot crunch this");
            }
            out.resize(length);
            return Ok(std::move(out));
        }
    }
    return Fail("unknown algorithm");
}

bool IsLzsa(Algorithm algorithm)
{
    return algorithm == Algorithm::Lzsa1 || algorithm == Algorithm::Lzsa2;
}

struct DirectiveName
{
    std::string_view section;
    std::string_view include;
    Algorithm algorithm;
};

constexpr std::array<DirectiveName, 10> kDirectives{{
    {"LZ4", "INCLZ4", Algorithm::Lz4},
    {"LZ48", "INCL48", Algorithm::Lz48},
    {"LZ49", "INCL49", Algorithm::Lz49},
    {"LZX7", "INCZX7", Algorithm::Zx7},
    {"LZX0", "INCZX0", Algorithm::Zx0},
    {"LZX0B", "INCZX0B", Algorithm::Zx0Backward},
    {"LZEXO", "INCEXO", Algorithm::Exomizer},
    {"LZAPU", "INCAPU", Algorithm::ApUltra},
    {"LZSA1", "INCLZSA1", Algorithm::Lzsa1},
    {"LZSA2", "INCLZSA2", Algorithm::Lzsa2},
}};

bool EqualsNoCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
           });
}
}  // namespace

Result CrunchRaw(Algorithm algorithm, const uint8_t* data, size_t size, int lzsaMinMatch)
{
    Result result;
    if (size == 0 || data == nullptr)
    {
        result.error = "nothing to crunch";
        return result;
    }
    Raw raw = RunCruncher(algorithm, data, size, lzsaMinMatch, 0);
    if (!raw.ok)
    {
        result.error = std::move(raw.error);
        return result;
    }
    result.bytes = std::move(raw.bytes);
    result.crunched = true;
    return result;
}

Result Crunch(Algorithm algorithm, const uint8_t* data, size_t size, const Options& options)
{
    Result result;
    const bool section = options.origin == Origin::Section;
    if (size == 0 || data == nullptr)
    {
        // rasm: a section prints both warnings, an INCxxx of an empty file one; nothing is emitted
        result.warning = section ? "crunched section is empty" : "INCBIN of an empty file";
        return result;
    }

    // The salvador limits rasm passes: sections and INCZX0B the input size, INCZX0 the worst case size
    size_t zx0Max = size;
    if (!section && algorithm == Algorithm::Zx0)
    {
        zx0Max = 0;
    }
    const int minMatch = section ? options.lzsaMinMatch : 0;
    Raw raw = RunCruncher(algorithm, data, size, minMatch, zx0Max);

    if (section)
    {
        // LZSA_crunch's failure is an error of its own (MakeError) before the generic check
        if (!raw.ok && IsLzsa(algorithm))
        {
            result.error = raw.error;
            return result;
        }
        // input_size<lzlen || lzlen<=0: the section stays as it is. The backward ZX0 reversed the section's bytes in
        // place before crunching and does not reverse them back, so they stay reversed (a rasm 3.3.x bug, kept)
        if (!raw.ok || raw.bytes.empty() || raw.bytes.size() > size)
        {
            result.warning = "LZ section is bigger than original, it wont be crunched!";
            result.bytes.assign(data, data + size);
            if (algorithm == Algorithm::Zx0Backward)
            {
                std::reverse(result.bytes.begin(), result.bytes.end());
            }
            return result;
        }
        result.bytes = std::move(raw.bytes);
        result.crunched = true;
        return result;
    }

    // INCxxx: outputidx = the cruncher's length; !outputidx || outputidx>=size is "Cannot crunch"
    if (!raw.ok)
    {
        if (algorithm == Algorithm::Zx0Backward)
        {
            // salvador's (size_t)-1 becomes outputidx -1: rasm passes the check and then crashes copying it
            result.error = "INCZX0B of data that does not crunch (rasm 3.3.x crashes here)";
        }
        else if (IsLzsa(algorithm))
        {
            result.error = raw.error + "; Cannot crunch";
        }
        else
        {
            result.error = "Cannot crunch";
        }
        return result;
    }
    if (raw.bytes.empty() || raw.bytes.size() >= size)
    {
        result.error = "Cannot crunch";
        return result;
    }
    result.bytes = std::move(raw.bytes);
    result.crunched = true;
    return result;
}

Result Decrunch(Algorithm algorithm, const uint8_t* data, size_t size)
{
    Result result;
    if (size == 0 || data == nullptr)
    {
        result.error = "no data";
        return result;
    }
    std::optional<std::vector<uint8_t>> out;
    switch (algorithm)
    {
        case Algorithm::Lz4: {
            if (size > static_cast<size_t>(INT_MAX))
            {
                break;
            }
            // An LZ4 block does not store its decompressed size: grow the buffer until it fits
            for (size_t capacity = 65536; capacity <= kDecrunchLimit && !out; capacity *= 4)
            {
                // Not zeroed: a damaged block fails at every size up to the limit, and the decoder writes only what
                // it decodes
                const std::unique_ptr<uint8_t[]> buffer(new uint8_t[capacity]);
                const int length = UnrealAsmLz4DecompressSafe(reinterpret_cast<const char*>(data),
                                                              reinterpret_cast<char*>(buffer.get()),
                                                              static_cast<int>(size), static_cast<int>(capacity));
                if (length >= 0 && static_cast<size_t>(length) < capacity)
                {
                    out = std::vector<uint8_t>(buffer.get(), buffer.get() + length);
                }
            }
            break;
        }
        case Algorithm::Lz48:
            out = detail::Lz48Decode(data, size, kDecrunchLimit);
            break;
        case Algorithm::Lz49:
            out = detail::Lz49Decode(data, size, kDecrunchLimit);
            break;
        case Algorithm::Zx7:
            out = detail::Zx7Decode(data, size, kDecrunchLimit);
            break;
        case Algorithm::Exomizer:
            out = detail::ExomizerDecode(data, size, kDecrunchLimit);
            break;
        case Algorithm::Zx0:
        case Algorithm::Zx0Backward: {
            const bool backward = algorithm == Algorithm::Zx0Backward;
            std::vector<uint8_t> input(data, data + size);
            if (backward)
            {
                std::reverse(input.begin(), input.end());
            }
            input.resize(size + kDecoderInputSlack, 0);
            const unsigned int flags = kSalvadorInverted | (backward ? kSalvadorBackward : 0);
            const size_t maxSize = salvador_get_max_decompressed_size(input.data(), size, flags);
            if (maxSize == static_cast<size_t>(-1) || maxSize > kDecrunchLimit)
            {
                break;
            }
            std::vector<uint8_t> buffer(std::max(maxSize, size_t{1}), 0);
            const size_t length = salvador_decompress(input.data(), buffer.data(), size, maxSize, 0, flags);
            if (length != static_cast<size_t>(-1) && length <= maxSize)
            {
                buffer.resize(length);
                if (backward)
                {
                    std::reverse(buffer.begin(), buffer.end());
                }
                out = std::move(buffer);
            }
            break;
        }
        case Algorithm::ApUltra:
            out = detail::ApUltraDecode(data, size, kDecrunchLimit);
            break;
        case Algorithm::Lzsa1:
        case Algorithm::Lzsa2: {
            if (size > static_cast<size_t>(INT_MAX))
            {
                break;
            }
            const int version = algorithm == Algorithm::Lzsa1 ? 1 : 2;
            std::vector<uint8_t> input(data, data + size);
            input.resize(size + kDecoderInputSlack, 0);
            // The LZSA1 end marker is a match of offset 256 (offset byte 0) whose length escape reads 0, and lzsa
            // checks the offset before the length: with less than 256 bytes out it refuses the end of its own block.
            // The output starts after 256 bytes of zeros, as if they were a dictionary, so the marker is seen.
            const size_t prefix = version == 1 ? 256 : 0;
            for (size_t capacity = 65536; capacity <= kDecrunchLimit && !out; capacity *= 4)
            {
                // Only the dictionary prefix is zeroed, as for LZ4 above
                const std::unique_ptr<uint8_t[]> buffer(new uint8_t[prefix + capacity]);
                std::fill(buffer.get(), buffer.get() + prefix, uint8_t{0});
                const int length = lzsa_decompressor_expand_block(input.data(), static_cast<int>(size), buffer.get(),
                                                                  static_cast<int>(prefix), static_cast<int>(capacity),
                                                                  version, static_cast<int>(kLzsaRawBlock));
                if (length >= 0 && static_cast<size_t>(length) < capacity)
                {
                    const uint8_t* first = buffer.get() + prefix;
                    out = std::vector<uint8_t>(first, first + length);
                }
            }
            break;
        }
    }
    if (!out)
    {
        result.error = "damaged data";
        return result;
    }
    result.bytes = std::move(*out);
    return result;
}

std::optional<Algorithm> AlgorithmFromRasmDirective(std::string_view name)
{
    for (const DirectiveName& directive : kDirectives)
    {
        if (EqualsNoCase(name, directive.section) || EqualsNoCase(name, directive.include))
        {
            return directive.algorithm;
        }
    }
    return std::nullopt;
}

std::string_view RasmSectionDirective(Algorithm algorithm)
{
    for (const DirectiveName& directive : kDirectives)
    {
        if (directive.algorithm == algorithm)
        {
            return directive.section;
        }
    }
    return {};
}

std::string_view RasmIncludeDirective(Algorithm algorithm)
{
    for (const DirectiveName& directive : kDirectives)
    {
        if (directive.algorithm == algorithm)
        {
            return directive.include;
        }
    }
    return {};
}
}  // namespace unrealasm::compress
