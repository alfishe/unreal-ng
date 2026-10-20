#pragma once

/// @file cellexpand.h
/// @brief One ZX character cell row to pixels: bit 7 of the pixel byte is the leftmost pixel, a set bit takes ink,
/// a clear one paper. NEON (arm64), SSE2 (x86-64) and a portable fallback; every path writes the same values.

#include <cstdint>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define CELLEXPAND_NEON 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define CELLEXPAND_SSE2 1
#endif

namespace cellexpand
{

/// The portable paths (the fallback; tests check the SIMD ones against them)
inline void Expand8Scalar(uint8_t pixels, uint32_t ink, uint32_t paper, uint32_t* out)
{
    for (uint32_t i = 0; i < 8; i++)
    {
        const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>((pixels >> (7 - i)) & 1));
        out[i] = (ink & mask) | (paper & ~mask);
    }
}

inline void Expand8Scalar(uint8_t pixels, uint16_t ink, uint16_t paper, uint16_t* out)
{
    for (uint32_t i = 0; i < 8; i++)
    {
        const uint16_t mask = static_cast<uint16_t>(-static_cast<int32_t>((pixels >> (7 - i)) & 1));
        out[i] = static_cast<uint16_t>((ink & mask) | (paper & ~mask));
    }
}

/// 8 framebuffer pixels (any alignment)
inline void Expand8(uint8_t pixels, uint32_t ink, uint32_t paper, uint32_t* out)
{
#if CELLEXPAND_NEON
    static const uint32_t kBits[8] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
    const uint32x4_t p = vdupq_n_u32(pixels);
    const uint32x4_t vInk = vdupq_n_u32(ink);
    const uint32x4_t vPaper = vdupq_n_u32(paper);
    vst1q_u32(out, vbslq_u32(vtstq_u32(p, vld1q_u32(kBits)), vInk, vPaper));
    vst1q_u32(out + 4, vbslq_u32(vtstq_u32(p, vld1q_u32(kBits + 4)), vInk, vPaper));
#elif CELLEXPAND_SSE2
    const __m128i bits0 = _mm_set_epi32(0x10, 0x20, 0x40, 0x80);
    const __m128i bits1 = _mm_set_epi32(0x01, 0x02, 0x04, 0x08);
    const __m128i p = _mm_set1_epi32(pixels);
    const __m128i vInk = _mm_set1_epi32(static_cast<int>(ink));
    const __m128i vPaper = _mm_set1_epi32(static_cast<int>(paper));
    const __m128i m0 = _mm_cmpeq_epi32(_mm_and_si128(p, bits0), bits0);
    const __m128i m1 = _mm_cmpeq_epi32(_mm_and_si128(p, bits1), bits1);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out), _mm_or_si128(_mm_and_si128(m0, vInk), _mm_andnot_si128(m0, vPaper)));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 4), _mm_or_si128(_mm_and_si128(m1, vInk), _mm_andnot_si128(m1, vPaper)));
#else
    Expand8Scalar(pixels, ink, paper, out);
#endif
}

/// 8 plane-B cells (16-bit, any alignment)
inline void Expand8(uint8_t pixels, uint16_t ink, uint16_t paper, uint16_t* out)
{
#if CELLEXPAND_NEON
    static const uint16_t kBits[8] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
    const uint16x8_t mask = vtstq_u16(vdupq_n_u16(pixels), vld1q_u16(kBits));
    vst1q_u16(out, vbslq_u16(mask, vdupq_n_u16(ink), vdupq_n_u16(paper)));
#elif CELLEXPAND_SSE2
    const __m128i bits = _mm_set_epi16(0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80);
    const __m128i mask = _mm_cmpeq_epi16(_mm_and_si128(_mm_set1_epi16(pixels), bits), bits);
    const __m128i value = _mm_or_si128(_mm_and_si128(mask, _mm_set1_epi16(static_cast<short>(ink))),
                                       _mm_andnot_si128(mask, _mm_set1_epi16(static_cast<short>(paper))));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out), value);
#else
    Expand8Scalar(pixels, ink, paper, out);
#endif
}

}  // namespace cellexpand
