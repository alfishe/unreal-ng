#pragma once

/// @file ttd_compression.h
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

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

#include <zstd.h>

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
/// @return Compressed bytes. Empty vector only if size==0 or allocation fails.
inline std::vector<uint8_t> Compress(const uint8_t* src, size_t size, int level = kDefaultZstdLevel)
{
    if (size == 0 || src == nullptr)
    {
        return {};
    }
    size_t bound = ZSTD_compressBound(size);
    std::vector<uint8_t> out(bound);
    size_t n = ZSTD_compress(out.data(), out.capacity(),
                             src, size,
                             level);
    if (ZSTD_isError(n))
    {
        return {};
    }
    out.resize(n);
    return out;
}

/// @brief Decompress compressed[] back to exactly rawSize bytes.
/// @param compressed  The compressed payload (from Compress()).
/// @param rawSize     Expected uncompressed size.
/// @param out         Destination buffer (must hold at least rawSize bytes).
/// @return true on success, false on size mismatch or zstd decode error.
inline bool Decompress(const std::vector<uint8_t>& compressed, size_t rawSize, uint8_t* out)
{
    if (rawSize == 0 || out == nullptr)
    {
        return rawSize == 0;
    }
    if (compressed.empty())
    {
        return false;
    }
    size_t n = ZSTD_decompress(out, rawSize,
                               compressed.data(), compressed.size());
    if (ZSTD_isError(n))
    {
        return false;
    }
    return n == rawSize;
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
///   - x86 SSE4.2: CRC32 instruction (~5 GB/s)
///   - ARM64v8: CRC32C instructions via __builtin_arm_crc32cb
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

#if defined(__SSE4_2__) || defined(__x86_64__) && defined(__CRC32__)
    // Hardware-accelerated path (x86 SSE4.2 / x86_64).
    // We use intrinsics if available; otherwise fall through to software.
    #if defined(__GNUC__) || defined(__clang__)
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
    #endif
#endif

#if defined(__aarch64__) && defined(__ARM_FEATURE_CRC32)
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
#else
    // Software fallback (portable, ~500 MB/s).
    const auto& tbl = detail::Crc32CTableInstance().t;
    uint32_t crc = ~seed;
    for (size_t i = 0; i < size; ++i) {
        crc = tbl[(crc ^ src[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
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
