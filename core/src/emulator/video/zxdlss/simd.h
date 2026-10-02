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
    // equal bytes -> 1, then psadbw sums each 8-byte half. No popcount: baseline
    // x86-64 has no POPCNT and std::popcount becomes a libgcc call per invocation
    const __m128i one = _mm_set1_epi8(1);
    const __m128i e0 = _mm_and_si128(one, _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a)),
                                                         _mm_loadu_si128(reinterpret_cast<const __m128i*>(b))));
    const __m128i e1 = _mm_and_si128(one, _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a + 16)),
                                                         _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + 16))));
    const __m128i s = _mm_sad_epu8(_mm_add_epi8(e0, e1), _mm_setzero_si128());
    return 32 - (_mm_cvtsi128_si32(s) + _mm_cvtsi128_si32(_mm_srli_si128(s, 8)));
#else
    return diff8Scalar(a, b) + diff8Scalar(a + 8, b + 8) + diff8Scalar(a + 16, b + 16) + diff8Scalar(a + 24, b + 24);
#endif
}

/// Differing bytes in a 32-byte wide column of `rows` rows (rows <= 255): row r
/// is a + r * stride against b[r] + off. Equal bytes are counted in byte lanes
/// and summed once at the end.
inline int diff32Rows(const uint8_t* a, size_t stride, const uint8_t* const* b, size_t off, int rows)
{
#if ZXDLSS_NEON
    uint8x16_t e0 = vdupq_n_u8(0), e1 = vdupq_n_u8(0);
    for (int r = 0; r < rows; ++r, a += stride)
    {
        // equal bytes are 0xFF: subtracting adds one
        e0 = vsubq_u8(e0, vceqq_u8(vld1q_u8(a), vld1q_u8(b[r] + off)));
        e1 = vsubq_u8(e1, vceqq_u8(vld1q_u8(a + 16), vld1q_u8(b[r] + off + 16)));
    }
    return rows * 32 - static_cast<int>(vaddlvq_u8(e0)) - static_cast<int>(vaddlvq_u8(e1));
#elif ZXDLSS_SSE2
    __m128i e0 = _mm_setzero_si128(), e1 = _mm_setzero_si128();
    for (int r = 0; r < rows; ++r, a += stride)
    {
        // equal bytes are 0xFF: subtracting adds one
        e0 = _mm_sub_epi8(e0, _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a)),
                                             _mm_loadu_si128(reinterpret_cast<const __m128i*>(b[r] + off))));
        e1 = _mm_sub_epi8(e1, _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a + 16)),
                                             _mm_loadu_si128(reinterpret_cast<const __m128i*>(b[r] + off + 16))));
    }
    const __m128i z = _mm_setzero_si128();
    const __m128i s = _mm_add_epi64(_mm_sad_epu8(e0, z), _mm_sad_epu8(e1, z));
    return rows * 32 - (_mm_cvtsi128_si32(s) + _mm_cvtsi128_si32(_mm_srli_si128(s, 8)));
#else
    int d = 0;
    for (int r = 0; r < rows; ++r, a += stride)
        d += diff32(a, b[r] + off);
    return d;
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
    // psadbw sums the equal-byte flags of each 8-byte half: one block per 64-bit lane
    const __m128i one = _mm_set1_epi8(1);
    for (; k + 2 <= blocks; k += 2)
    {
        const __m128i s = _mm_sad_epu8(
            _mm_and_si128(one, _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a + k * 8)),
                                              _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + k * 8)))),
            _mm_setzero_si128());
        out[k] += 8 - _mm_cvtsi128_si32(s);
        out[k + 1] += 8 - _mm_cvtsi128_si32(_mm_srli_si128(s, 8));
    }
#endif
    for (; k < blocks; ++k)
        out[k] += diff8Scalar(a + k * 8, b + k * 8);
}

/// Scene-stage counts of one 16 x 16 tile over 7 frames s[0..6] (t = s[3], t+1 =
/// s[2], t-1 = s[4]), top-left byte offset p, row stride `stride`:
///   stat: equal in all 7 frames, or s[k] == s[k + 2] for k < 5
///   dyn:  not stat, t != t-1 and t != t+1
///   edge: t != its right neighbor; the tile's last column only when `hasRight`
///         (false: it is outside the frame and is never read)
struct SceneCounts
{
    int stat = 0, dyn = 0, edge = 0;
};

inline SceneCounts sceneTile16(const uint8_t* const* s, size_t p, size_t stride, bool hasRight)
{
    SceneCounts r;
#if ZXDLSS_NEON
    uint8x16_t aStat = vdupq_n_u8(0), aDyn = vdupq_n_u8(0), aEdge = vdupq_n_u8(0);
    const uint8x16_t zero = vdupq_n_u8(0);
    uint8x16_t edgeMask = vdupq_n_u8(0xFF);
    if (!hasRight)
        edgeMask = vsetq_lane_u8(0, edgeMask, 15);
    for (int y = 0; y < 16; ++y, p += stride)
    {
        uint8x16_t v[7];
        for (int k = 0; k < 7; ++k)
            v[k] = vld1q_u8(s[k] + p);
        uint8x16_t cst = vceqq_u8(v[1], v[0]), p2 = vceqq_u8(v[0], v[2]);
        for (int k = 2; k < 7; ++k)
            cst = vandq_u8(cst, vceqq_u8(v[k], v[0]));
        for (int k = 1; k < 5; ++k)
            p2 = vandq_u8(p2, vceqq_u8(v[k], v[k + 2]));
        const uint8x16_t stat = vorrq_u8(cst, p2);
        // equal bytes are 0xFF: subtracting adds one
        aStat = vsubq_u8(aStat, stat);
        aDyn = vsubq_u8(aDyn, vmvnq_u8(vorrq_u8(stat, vorrq_u8(vceqq_u8(v[3], v[4]), vceqq_u8(v[3], v[2])))));
        // right neighbors: lane 15 reads the next tile, or is masked off (zero) at the frame edge
        const uint8x16_t right = hasRight ? vld1q_u8(s[3] + p + 1) : vextq_u8(v[3], zero, 1);
        aEdge = vsubq_u8(aEdge, vandq_u8(edgeMask, vmvnq_u8(vceqq_u8(v[3], right))));
    }
    r.stat = vaddlvq_u8(aStat);
    r.dyn = vaddlvq_u8(aDyn);
    r.edge = vaddlvq_u8(aEdge);
#elif ZXDLSS_SSE2
    const __m128i ones = _mm_set1_epi8(-1), z = _mm_setzero_si128();
    __m128i aStat = z, aDyn = z, aEdge = z;
    const __m128i edgeMask = hasRight ? ones : _mm_srli_si128(ones, 1);
    for (int y = 0; y < 16; ++y, p += stride)
    {
        __m128i v[7];
        for (int k = 0; k < 7; ++k)
            v[k] = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s[k] + p));
        __m128i cst = _mm_cmpeq_epi8(v[1], v[0]), p2 = _mm_cmpeq_epi8(v[0], v[2]);
        for (int k = 2; k < 7; ++k)
            cst = _mm_and_si128(cst, _mm_cmpeq_epi8(v[k], v[0]));
        for (int k = 1; k < 5; ++k)
            p2 = _mm_and_si128(p2, _mm_cmpeq_epi8(v[k], v[k + 2]));
        const __m128i stat = _mm_or_si128(cst, p2);
        // equal bytes are 0xFF: subtracting adds one
        aStat = _mm_sub_epi8(aStat, stat);
        aDyn = _mm_sub_epi8(aDyn, _mm_xor_si128(ones, _mm_or_si128(stat, _mm_or_si128(_mm_cmpeq_epi8(v[3], v[4]),
                                                                                      _mm_cmpeq_epi8(v[3], v[2])))));
        // right neighbors: lane 15 reads the next tile, or is masked off (zero) at the frame edge
        const __m128i right = hasRight ? _mm_loadu_si128(reinterpret_cast<const __m128i*>(s[3] + p + 1))
                                       : _mm_srli_si128(v[3], 1);
        aEdge = _mm_sub_epi8(aEdge, _mm_and_si128(edgeMask, _mm_xor_si128(ones, _mm_cmpeq_epi8(v[3], right))));
    }
    const auto sum = [z](__m128i a) {
        const __m128i t = _mm_sad_epu8(a, z);
        return _mm_cvtsi128_si32(t) + _mm_cvtsi128_si32(_mm_srli_si128(t, 8));
    };
    r.stat = sum(aStat);
    r.dyn = sum(aDyn);
    r.edge = sum(aEdge);
#else
    for (int y = 0; y < 16; ++y, p += stride)
        for (int x = 0; x < 16; ++x)
        {
            const size_t q = p + x;
            bool cst = true, p2 = true;
            for (int k = 0; k < 7; ++k)
            {
                cst &= s[k][q] == s[0][q];
                if (k < 5)
                    p2 &= s[k][q] == s[k + 2][q];
            }
            const bool stat = cst || p2;
            r.stat += stat;
            r.dyn += !stat && s[3][q] != s[4][q] && s[3][q] != s[2][q];
            r.edge += (x < 15 || hasRight) && s[3][q] != s[3][q + 1];
        }
#endif
    return r;
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
