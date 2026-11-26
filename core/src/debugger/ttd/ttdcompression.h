#pragma once

/// @file ttdcompression.h
/// @brief Compression + integrity primitives for the TTD codec page store.
///
/// Per Phase 5 PoC results (docs/inprogress/2026-07-19-time-travel/
/// phase-5-codec-poc-results.md):
///   - zstd level 1 is the chosen compressor (2.66x ratio at 43 us/page)
///   - CRC32C is the chosen per-slot integrity check (hardware-accelerated
///     on x86 SSE4.2 and ARM64)
///
/// This header is self-contained: it wraps the C zstd API and provides a
/// portable CRC32C implementation (SSE4.2 intrinsics where available,
/// fallback table-based otherwise). No other TTD header depends on zstd
/// directly — they all go through this API.
///
/// Thread model: stateless. All functions are reentrant and safe to call
/// from any thread. The ZSTD context objects used internally are thread-
/// local to avoid the (small) per-call allocation cost of creating fresh
/// contexts.

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <istream>
#include <vector>

#include <zstd.h>

#if defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64)
#include <intrin.h>
#include <nmmintrin.h>
#endif

namespace ttd::codec {

// ---------------------------------------------------------------------------
// Compression
// ---------------------------------------------------------------------------

/// Compression level used for all TTD page data. Empirically chosen:
///   - level 1 compresses 4 KB in ~10 us on modern hardware
///   - gives 2.66x ratio on real workload (Binary Love I demo)
///   - levels 3+ give <1 % additional ratio at much higher CPU cost
/// Override per-call if needed (e.g. for archive-mode dumps).
constexpr int kDefaultZstdLevel = 1;

/// @brief Compress src[0..size). Returns the compressed byte buffer.
/// @param src     Source data.
/// @param size    Source size in bytes.
/// @param level   zstd compression level (1 = fast, 9 = best, default 1).
/// @return Compressed bytes, allocated at exactly their size. Empty vector
///         only if size==0 or allocation fails.
///
/// zstd needs room for the worst case (ZSTD_compressBound: 4,174 bytes for a
/// 4 KB page), but the result is usually far smaller. Compressing straight
/// into a vector of the worst-case size and shrinking it left that capacity
/// allocated for as long as the result was stored: ~4 KB per page-store
/// slot and per coverage block, whatever they compressed to (TTD v2 POC 011
/// experiment E5). The worst-case buffer is therefore a scratch, and only the
/// exact bytes are copied out: a per-thread one reused by every call for
/// inputs up to kCompressScratchMax (pages, coverage and journal blocks), a
/// temporary one for larger inputs (clip export chunks), so no thread keeps a
/// large buffer for good.
constexpr size_t kCompressScratchMax = 64 * 1024;

inline std::vector<uint8_t> Compress(const uint8_t* src, size_t size, int level = kDefaultZstdLevel)
{
    if (size == 0 || src == nullptr)
    {
        return {};
    }
    const size_t bound = ZSTD_compressBound(size);
    thread_local std::vector<uint8_t> scratch;
    std::vector<uint8_t> large;
    std::vector<uint8_t>& buffer = (size <= kCompressScratchMax) ? scratch : large;
    if (buffer.size() < bound)
        buffer.resize(bound);
    // One context per thread, reused: ZSTD_compress creates and frees one per call, about half of compressing a
    // small buffer (the coverage summaries, P1.S10 follow-up 2026-11-21). The bytes are the same; the context keeps
    // its workspace (the largest input's) for the thread's life
    struct CompressContext
    {
        ZSTD_CCtx* ctx = ZSTD_createCCtx();
        ~CompressContext() { ZSTD_freeCCtx(ctx); }
    };
    thread_local CompressContext context;
    const size_t n = context.ctx ? ZSTD_compressCCtx(context.ctx, buffer.data(), bound, src, size, level)
                                 : ZSTD_compress(buffer.data(), bound, src, size, level);
    if (ZSTD_isError(n))
    {
        return {};
    }
    return std::vector<uint8_t>(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n));
}

/// @brief Decompress compressed[] back to exactly rawSize bytes.
/// @param compressed  The compressed payload (from Compress()).
/// @param rawSize     Expected uncompressed size.
/// @param out         Destination buffer (must hold at least rawSize bytes).
/// @return true on success, false on size mismatch or zstd decode error.
inline bool Decompress(const uint8_t* compressed, size_t compressedSize, size_t rawSize, uint8_t* out)
{
    if (rawSize == 0 || out == nullptr)
    {
        return rawSize == 0;
    }
    if (compressed == nullptr || compressedSize == 0)
    {
        return false;
    }
    // One context per thread, reused: ZSTD_decompress creates and frees one per call, which was a tenth of a read
    // back and of a restore's page decodes (P1.S10 follow-up 2026-11-21)
    struct DecompressContext
    {
        ZSTD_DCtx* ctx = ZSTD_createDCtx();
        ~DecompressContext() { ZSTD_freeDCtx(ctx); }
    };
    thread_local DecompressContext context;
    const size_t n = context.ctx ? ZSTD_decompressDCtx(context.ctx, out, rawSize, compressed, compressedSize)
                                 : ZSTD_decompress(out, rawSize, compressed, compressedSize);
    if (ZSTD_isError(n))
    {
        return false;
    }
    return n == rawSize;
}

inline bool Decompress(const std::vector<uint8_t>& compressed, size_t rawSize, uint8_t* out)
{
    return Decompress(compressed.data(), compressed.size(), rawSize, out);
}

/// @brief The decompressed size a zstd frame declares in its header.
/// Compress() always records it, so a loader can check a stored raw size against
/// it before allocating that many bytes.
/// @return The declared size, or 0 when the frame is malformed or carries none.
inline uint64_t DeclaredContentSize(const std::vector<uint8_t>& compressed)
{
    if (compressed.empty())
        return 0;
    const unsigned long long n = ZSTD_getFrameContentSize(compressed.data(), compressed.size());
    if (n == ZSTD_CONTENTSIZE_UNKNOWN || n == ZSTD_CONTENTSIZE_ERROR)
        return 0;
    return static_cast<uint64_t>(n);
}

/// @brief Read exactly `size` bytes into `out`, growing it only as data arrives.
/// A size taken from a corrupt file then fails at the end of the stream instead
/// of allocating up to 4 GB up front.
inline bool ReadExact(std::istream& in, size_t size, std::vector<uint8_t>& out)
{
    constexpr size_t kChunk = 64 * 1024;
    out.clear();
    while (out.size() < size)
    {
        const size_t n = std::min(kChunk, size - out.size());
        const size_t at = out.size();
        out.resize(at + n);
        in.read(reinterpret_cast<char*>(out.data() + at), static_cast<std::streamsize>(n));
        if (!in)
            return false;
    }
    return true;
}

/// Convenience: round-trip a byte buffer (used by tests).
inline std::vector<uint8_t> CompressDecompressRoundTrip(const uint8_t* src, size_t size)
{
    auto c = Compress(src, size);
    std::vector<uint8_t> out(size);
    if (!Decompress(c, size, out.data()))
    {
        return {};
    }
    return out;
}

// ---------------------------------------------------------------------------
// CRC32C (Castagnoli polynomial 0x1EDC6F41)
// ---------------------------------------------------------------------------

/// CRC32C polynomial. Standard for storage integrity (ext4, BTRFS, iSCSI,
/// LevelDB, SQLite WAL). Distinct from zlib's CRC32 (IEEE polynomial).
/// Hardware-accelerated:
///   - x86-64: the SSE4.2 CRC32 instruction (~5 GB/s). Chosen at run time when the build's baseline lacks SSE4.2
///     (gcc / clang / MinGW without -msse4.2, every MSVC build): the table path cost a recording frame 8x more
///   - ARM64v8: CRC32C instructions
/// Fallback: table-based software implementation (~500 MB/s).

namespace detail {

/// Software fallback CRC32C table (computed lazily on first use).
struct Crc32CTable {
    uint32_t t[256];
    Crc32CTable() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c >> 1) ^ ((c & 1) ? 0x82F63B78u : 0);  // reversed polynomial
            }
            t[i] = c;
        }
    }
};

inline const Crc32CTable& Crc32CTableInstance()
{
    static const Crc32CTable instance;
    return instance;
}

/// The table path: crc is the running (inverted) value
inline uint32_t Crc32CTableRun(const uint8_t* src, size_t size, uint32_t crc)
{
    const auto& tbl = Crc32CTableInstance().t;
    for (size_t i = 0; i < size; ++i) {
        crc = tbl[(crc ^ src[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__)) && !defined(__SSE4_2__)
#define UNREAL_CRC32C_X86_DISPATCH 1
/// The SSE4.2 path in a build whose baseline lacks it: compiled for SSE4.2, called only where the CPU has it
__attribute__((target("sse4.2"))) inline uint32_t Crc32CHardwareRun(const uint8_t* src, size_t size, uint32_t crc)
{
    size_t i = 0;
    while (i + 8 <= size) {
        uint64_t v;
        std::memcpy(&v, src + i, 8);
        crc = static_cast<uint32_t>(__builtin_ia32_crc32di(crc, v));
        i += 8;
    }
    while (i < size) {
        crc = __builtin_ia32_crc32qi(crc, src[i]);
        ++i;
    }
    return crc;
}
inline bool Crc32CHardware()
{
    static const bool has = __builtin_cpu_supports("sse4.2");
    return has;
}
#elif defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64)
#define UNREAL_CRC32C_X86_DISPATCH 1
/// MSVC never defines __SSE4_2__; its CRC32 intrinsics need no flag, the CPU is asked once
inline uint32_t Crc32CHardwareRun(const uint8_t* src, size_t size, uint32_t crc)
{
    size_t i = 0;
    uint64_t wide = crc;
    while (i + 8 <= size) {
        uint64_t v;
        std::memcpy(&v, src + i, 8);
        wide = _mm_crc32_u64(wide, v);
        i += 8;
    }
    crc = static_cast<uint32_t>(wide);
    while (i < size) {
        crc = _mm_crc32_u8(crc, src[i]);
        ++i;
    }
    return crc;
}
inline bool Crc32CHardware()
{
    static const bool has = [] {
        int info[4] = {};
        __cpuid(info, 1);
        return (info[2] & (1 << 20)) != 0;  // ECX bit 20: SSE4.2
    }();
    return has;
}
#endif

}  // namespace detail

/// Compute CRC32C of a buffer.
/// @param src     Data to checksum.
/// @param size    Length in bytes.
/// @param seed    Initial value (continuation; use 0 for a new checksum).
/// @return CRC32C value.
inline uint32_t Crc32C(const uint8_t* src, size_t size, uint32_t seed = 0)
{
    if (src == nullptr || size == 0)
    {
        return seed;
    }

    // One path is compiled: x86 SSE4.2 builtins, AArch64 CRC instructions, or the table (on x86-64 without SSE4.2
    // in the baseline: the SSE4.2 path chosen at run time, the table where the CPU lacks it). All give the same
    // CRC32C. (A build with -msse4.2 / -march=x86-64-v2+ used to compile the x86 path AND the table fallback into
    // one scope: "redeclaration of crc".)
#if (defined(__SSE4_2__) || (defined(__x86_64__) && defined(__CRC32__))) && (defined(__GNUC__) || defined(__clang__))
    // Hardware-accelerated path (x86 SSE4.2 / x86_64)
    uint32_t crc = ~seed;
    // Process 8 bytes at a time when possible
    size_t i = 0;
    while (i + 8 <= size) {
        uint64_t v;
        std::memcpy(&v, src + i, 8);
        crc = static_cast<uint32_t>(__builtin_ia32_crc32di(crc, v));
        i += 8;
    }
    while (i < size) {
        crc = __builtin_ia32_crc32qi(crc, src[i]);
        ++i;
    }
    return ~crc;
#elif defined(__aarch64__) && defined(__ARM_FEATURE_CRC32)
    uint32_t crc = ~seed;
    size_t i = 0;
    while (i + 8 <= size) {
        uint64_t v;
        std::memcpy(&v, src + i, 8);
        asm volatile("crc32cx %w[c], %w[c], %x[v]" : [c] "+r" (crc) : [v] "r" (v));
        i += 8;
    }
    while (i < size) {
        asm volatile("crc32cb %w[c], %w[c], %w[v]" : [c] "+r" (crc) : [v] "r" (src[i]));
        ++i;
    }
    return ~crc;
#elif defined(UNREAL_CRC32C_X86_DISPATCH)
    if (detail::Crc32CHardware())
        return ~detail::Crc32CHardwareRun(src, size, ~seed);
    return ~detail::Crc32CTableRun(src, size, ~seed);
#else
    // Software fallback (portable, ~500 MB/s).
    return ~detail::Crc32CTableRun(src, size, ~seed);
#endif
}

inline uint32_t Crc32C(const std::vector<uint8_t>& buf, uint32_t seed = 0)
{
    return Crc32C(buf.data(), buf.size(), seed);
}

// ---------------------------------------------------------------------------
// XOR delta
// ---------------------------------------------------------------------------

/// @brief XOR a buffer with another (used for P-frame delta encoding).
/// Computes dst[i] = a[i] ^ b[i] for i in [0, size).
/// Output buffer must be at least `size` bytes; can alias neither input.
inline void XorBuffers(const uint8_t* a, const uint8_t* b, uint8_t* dst, size_t size)
{
    if (size == 0) return;
    // Word-at-a-time XOR (auto-vectorizes well; ~10 GB/s on modern CPUs).
    size_t i = 0;
    while (i + 8 <= size) {
        uint64_t x, y;
        std::memcpy(&x, a + i, 8);
        std::memcpy(&y, b + i, 8);
        uint64_t z = x ^ y;
        std::memcpy(dst + i, &z, 8);
        i += 8;
    }
    while (i < size) {
        dst[i] = a[i] ^ b[i];
        ++i;
    }
}

/// @brief Returns true if the buffer is all zeros.
/// Used to detect "page became zero" (e.g., VRAM cleared) without scanning
/// every byte in the inner loop. Vectorizes to a single SIMD compare.
inline bool IsAllZero(const uint8_t* buf, size_t size)
{
    if (size == 0) return true;
    size_t i = 0;
    while (i + 8 <= size) {
        uint64_t v;
        std::memcpy(&v, buf + i, 8);
        if (v != 0) return false;
        i += 8;
    }
    while (i < size) {
        if (buf[i] != 0) return false;
        ++i;
    }
    return true;
}

}  // namespace ttd::codec
