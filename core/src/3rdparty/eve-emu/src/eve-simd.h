// eve-emu - SIMD kernels with a portable fallback.
//
// Each kernel has three bodies with the same results, byte for byte: NEON (AArch64 and
// ARMv7 with NEON), SSE2 (every x86-64 compiler; 32-bit x86 with SSE2 enabled), and plain
// C++ for everything else. EVE_NO_SIMD (CMake EVE_SIMD=OFF) forces the plain C++ bodies,
// so the fallback is built and tested on any host. Nothing here depends on CPU detection at
// run time: the baseline instruction set of each architecture is enough.
#ifndef EVE_SIMD_H
#define EVE_SIMD_H

#include <cstdint>

#if !defined(EVE_NO_SIMD) && (defined(__ARM_NEON) || defined(__ARM_NEON__))
#define EVE_SIMD_NEON 1
#include <arm_neon.h>
#elif !defined(EVE_NO_SIMD) && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#define EVE_SIMD_SSE2 1
#include <emmintrin.h>
#endif

namespace EveLib
{
namespace Simd
{

/// The instruction set the kernels use in this build ("neon", "sse2" or "scalar")
inline const char* Name()
{
#if defined(EVE_SIMD_NEON)
    return "neon";
#elif defined(EVE_SIMD_SSE2)
    return "sse2";
#else
    return "scalar";
#endif
}

/// Line buffer pixels (bytes R, G, B, A) to host pixels ARGB8888 (0xFFRRGGBB): alpha is
/// replaced by opaque
inline void RgbaToArgb(const uint8_t* rgba, uint32_t* out, uint32_t count)
{
    uint32_t i = 0;
#if defined(EVE_SIMD_NEON)
    // 16 pixels: deinterleave the channels, store them back as B, G, R, 0xFF
    const uint8x16_t opaque = vdupq_n_u8(0xFF);
    for (; i + 16 <= count; i += 16)
    {
        const uint8x16x4_t in = vld4q_u8(rgba + 4 * i);
        uint8x16x4_t argb;
        argb.val[0] = in.val[2];
        argb.val[1] = in.val[1];
        argb.val[2] = in.val[0];
        argb.val[3] = opaque;
        vst4q_u8(reinterpret_cast<uint8_t*>(out + i), argb);
    }
#elif defined(EVE_SIMD_SSE2)
    // 4 pixels as little-endian words 0xAABBGGRR: R to bits 23..16, B to 7..0, G stays
    const __m128i opaque = _mm_set1_epi32(static_cast<int>(0xFF000000u));
    const __m128i lowByte = _mm_set1_epi32(0xFF);
    const __m128i green = _mm_set1_epi32(0xFF00);
    for (; i + 4 <= count; i += 4)
    {
        const __m128i x = _mm_loadu_si128(reinterpret_cast<const __m128i*>(rgba + 4 * i));
        const __m128i r = _mm_slli_epi32(_mm_and_si128(x, lowByte), 16);
        const __m128i g = _mm_and_si128(x, green);
        const __m128i b = _mm_and_si128(_mm_srli_epi32(x, 16), lowByte);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i),
                         _mm_or_si128(_mm_or_si128(opaque, r), _mm_or_si128(g, b)));
    }
#endif
    for (; i < count; ++i)
    {
        const uint8_t* p = rgba + 4 * i;
        out[i] = 0xFF000000u | (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
    }
}

/// The default blend (SRC_ALPHA, ONE_MINUS_SRC_ALPHA, all channels written) of `count`
/// texels over line buffer pixels: d = min(s x a / 255 + d x (255 - a) / 255, 255) per
/// channel, each product rounded as Multiply does ((x + 127) / 255). Exact for a = 0 (d
/// stays) and a = 255 (s replaces d). Texels are packed R in bits 31..24, A in 7..0.
inline void BlendSrcAlpha(uint8_t* dst, const uint32_t* texels, uint32_t count)
{
    uint32_t i = 0;
#if defined(EVE_SIMD_NEON)
    // floor(t / 255) for t <= 65152 is (t + 1 + (t >> 8)) >> 8
    const uint16x8_t half = vdupq_n_u16(127);
    const uint16x8_t one = vdupq_n_u16(1);
    const uint8x8_t full = vdup_n_u8(255);
    for (; i + 8 <= count; i += 8)
    {
        // A texel's bytes in memory are A, B, G, R
        const uint8x8x4_t s = vld4_u8(reinterpret_cast<const uint8_t*>(texels + i));
        uint8x8x4_t d = vld4_u8(dst + 4 * i);
        const uint8x8_t a = s.val[0];
        const uint8x8_t inv = vsub_u8(full, a);
        const uint8x8_t src[4] = {s.val[3], s.val[2], s.val[1], s.val[0]};
        for (int c = 0; c < 4; ++c)
        {
            uint16x8_t ts = vaddq_u16(vmull_u8(src[c], a), half);
            uint16x8_t td = vaddq_u16(vmull_u8(d.val[c], inv), half);
            ts = vshrq_n_u16(vaddq_u16(vaddq_u16(ts, one), vshrq_n_u16(ts, 8)), 8);
            td = vshrq_n_u16(vaddq_u16(vaddq_u16(td, one), vshrq_n_u16(td, 8)), 8);
            d.val[c] = vqmovn_u16(vaddq_u16(ts, td));
        }
        vst4_u8(dst + 4 * i, d);
    }
#elif defined(EVE_SIMD_SSE2)
    // Two pixels per 16-bit lane group: unpack to words, multiply, divide, pack saturated
    const __m128i zero = _mm_setzero_si128();
    const __m128i half = _mm_set1_epi16(127);
    const __m128i one = _mm_set1_epi16(1);
    const __m128i full = _mm_set1_epi16(255);
    for (; i + 2 <= count; i += 2)
    {
        // Source channels into R, G, B, A byte order (the line buffer's), alpha in every lane
        const uint32_t p0 = texels[i], p1 = texels[i + 1];
        const uint32_t s0 = (p0 >> 24) | ((p0 >> 8) & 0xFF00) | ((p0 << 8) & 0xFF0000) | (p0 << 24);
        const uint32_t s1 = (p1 >> 24) | ((p1 >> 8) & 0xFF00) | ((p1 << 8) & 0xFF0000) | (p1 << 24);
        const __m128i s = _mm_unpacklo_epi8(_mm_set_epi32(0, 0, static_cast<int>(s1), static_cast<int>(s0)), zero);
        const __m128i a = _mm_set_epi16(static_cast<short>(p1 & 0xFF), static_cast<short>(p1 & 0xFF),
                                        static_cast<short>(p1 & 0xFF), static_cast<short>(p1 & 0xFF),
                                        static_cast<short>(p0 & 0xFF), static_cast<short>(p0 & 0xFF),
                                        static_cast<short>(p0 & 0xFF), static_cast<short>(p0 & 0xFF));
        const __m128i inv = _mm_sub_epi16(full, a);
        const __m128i d = _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(dst + 4 * i)), zero);
        __m128i ts = _mm_add_epi16(_mm_mullo_epi16(s, a), half);
        __m128i td = _mm_add_epi16(_mm_mullo_epi16(d, inv), half);
        ts = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(ts, one), _mm_srli_epi16(ts, 8)), 8);
        td = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(td, one), _mm_srli_epi16(td, 8)), 8);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(dst + 4 * i), _mm_packus_epi16(_mm_add_epi16(ts, td), zero));
    }
#endif
    for (; i < count; ++i)
    {
        const uint32_t p = texels[i];
        const uint32_t a = p & 0xFF;
        const uint32_t inv = 255 - a;
        const uint32_t src[4] = {p >> 24, (p >> 16) & 0xFF, (p >> 8) & 0xFF, a};
        uint8_t* d = dst + 4 * i;
        for (int c = 0; c < 4; ++c)
        {
            const uint32_t v = (src[c] * a + 127) / 255 + (d[c] * inv + 127) / 255;
            d[c] = static_cast<uint8_t>(v > 255 ? 255 : v);
        }
    }
}

} // namespace Simd
} // namespace EveLib

#endif
