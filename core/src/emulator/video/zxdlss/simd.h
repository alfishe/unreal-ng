#pragma once

/// @file simd.h
/// @brief Byte-compare kernels of mod-tpgw: NEON (arm64), SSE2 (x86-64), and a
/// portable fallback. All count DIFFERING bytes; results are identical on every path.

#include <bit>
#include <cstdint>
#include <cstring>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define ZXDLSS_NEON 1
#elif defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#define ZXDLSS_SSE2 1
#endif

namespace zxdlss::simd
{

/// Portable: differing bytes in two 8-byte groups of values < 128.
inline int diff8Scalar(const uint8_t* a, const uint8_t* b)
{
    uint64_t x, y;
    std::memcpy(&x, a, 8);
    std::memcpy(&y, b, 8);
    uint64_t t = x ^ y;
    t |= t >> 4;
    t |= t >> 2;
    t |= t >> 1;
    return std::popcount(t & 0x0101010101010101ull);
}

/// Differing bytes in 32 bytes.
inline int diff32(const uint8_t* a, const uint8_t* b)
{
#if ZXDLSS_NEON
    const uint8x16_t e0 = vceqq_u8(vld1q_u8(a), vld1q_u8(b));
    const uint8x16_t e1 = vceqq_u8(vld1q_u8(a + 16), vld1q_u8(b + 16));
    // equal bytes are 0xFF: shift to 1 and add up
    return 32 - static_cast<int>(vaddvq_u8(vaddq_u8(vshrq_n_u8(e0, 7), vshrq_n_u8(e1, 7))));
#elif ZXDLSS_SSE2
    const int m0 = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a)),
                                                    _mm_loadu_si128(reinterpret_cast<const __m128i*>(b))));
    const int m1 = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a + 16)),
                                                    _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + 16))));
    return 32 - std::popcount(static_cast<unsigned>(m0 | (m1 << 16)));
#else
    return diff8Scalar(a, b) + diff8Scalar(a + 8, b + 8) + diff8Scalar(a + 16, b + 16) + diff8Scalar(a + 24, b + 24);
#endif
}

/// For each 8-byte block of a row of `blocks` blocks, add its differing bytes to out[block].
inline void addDiffBlocks8(const uint8_t* a, const uint8_t* b, int blocks, int* out)
{
    int k = 0;
#if ZXDLSS_NEON
    for (; k + 2 <= blocks; k += 2)
    {
        const uint8x16_t e = vshrq_n_u8(vceqq_u8(vld1q_u8(a + k * 8), vld1q_u8(b + k * 8)), 7);
        out[k] += 8 - static_cast<int>(vaddv_u8(vget_low_u8(e)));
        out[k + 1] += 8 - static_cast<int>(vaddv_u8(vget_high_u8(e)));
    }
#elif ZXDLSS_SSE2
    for (; k + 2 <= blocks; k += 2)
    {
        const unsigned m = static_cast<unsigned>(_mm_movemask_epi8(
            _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a + k * 8)),
                           _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + k * 8)))));
        out[k] += 8 - std::popcount(m & 0xFFu);
        out[k + 1] += 8 - std::popcount(m >> 8);
    }
#endif
    for (; k < blocks; ++k)
        out[k] += diff8Scalar(a + k * 8, b + k * 8);
}

/// Name of the compiled kernel path (for zxdlss-bench).
inline const char* path()
{
#if ZXDLSS_NEON
    return "NEON";
#elif ZXDLSS_SSE2
    return "SSE2";
#else
    return "scalar";
#endif
}

}  // namespace zxdlss::simd
