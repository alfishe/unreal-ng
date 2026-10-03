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
#include <cstring>

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
        const uint8x8_t a = s.val[0];
        // Exact shortcuts of the formula: alpha 0 keeps the destination, alpha 255 gives
        // the source (sprites are mostly one or the other)
        const uint64_t alphas = vget_lane_u64(vreinterpret_u64_u8(a), 0);
        if (alphas == 0)
            continue;
        if (alphas == ~uint64_t{0})
        {
            const uint8x8x4_t opaque = {{s.val[3], s.val[2], s.val[1], s.val[0]}};
            vst4_u8(dst + 4 * i, opaque);
            continue;
        }
        uint8x8x4_t d = vld4_u8(dst + 4 * i);
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
        // Exact shortcuts: alpha 0 keeps the destination, alpha 255 gives the source
        const uint32_t a0 = p0 & 0xFF, a1 = p1 & 0xFF;
        if ((a0 | a1) == 0)
            continue;
        if ((a0 & a1) == 0xFF)
        {
            std::memcpy(dst + 4 * i, &s0, 4);
            std::memcpy(dst + 4 * i + 4, &s1, 4);
            continue;
        }
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
        if (a == 0)
            continue;
        const uint32_t inv = 255 - a;
        const uint32_t src[4] = {p >> 24, (p >> 16) & 0xFF, (p >> 8) & 0xFF, a};
        uint8_t* d = dst + 4 * i;
        if (a == 255)
        {
            for (int c = 0; c < 4; ++c)
                d[c] = static_cast<uint8_t>(src[c]);
            continue;
        }
        for (int c = 0; c < 4; ++c)
        {
            const uint32_t v = (src[c] * a + 127) / 255 + (d[c] * inv + 127) / 255;
            d[c] = static_cast<uint8_t>(v > 255 ? 255 : v);
        }
    }
}

/// floor(t / 255) for t <= 65152 is (t + 1 + (t >> 8)) >> 8: the rounded 8-bit product of
/// Multiply ((x * y + 127) / 255) without a division

/// dst.rgb = src.rgb x dst.a (Multiply per channel), dst.a kept: BLEND_FUNC(DST_ALPHA,
/// ZERO) with COLOR_MASK(1, 1, 1, 0). Line buffer bytes R, G, B, A; texels packed R in
/// bits 31..24, A in 7..0
inline void MultiplyRgbByDstAlpha(uint8_t* dst, const uint32_t* texels, uint32_t count)
{
    uint32_t i = 0;
#if defined(EVE_SIMD_NEON)
    const uint16x8_t half = vdupq_n_u16(127);
    const uint16x8_t one = vdupq_n_u16(1);
    for (; i + 8 <= count; i += 8)
    {
        const uint8x8x4_t s = vld4_u8(reinterpret_cast<const uint8_t*>(texels + i)); // A, B, G, R
        uint8x8x4_t d = vld4_u8(dst + 4 * i);                                       // R, G, B, A
        const uint8x8_t da = d.val[3];
        const uint8x8_t src[3] = {s.val[3], s.val[2], s.val[1]};
        for (int c = 0; c < 3; ++c)
        {
            uint16x8_t t = vaddq_u16(vmull_u8(src[c], da), half);
            t = vshrq_n_u16(vaddq_u16(vaddq_u16(t, one), vshrq_n_u16(t, 8)), 8);
            d.val[c] = vmovn_u16(t);
        }
        vst4_u8(dst + 4 * i, d);
    }
#elif defined(EVE_SIMD_SSE2)
    const __m128i zero = _mm_setzero_si128();
    const __m128i half = _mm_set1_epi16(127);
    const __m128i one = _mm_set1_epi16(1);
    const __m128i alphaLanes = _mm_set_epi16(-1, 0, 0, 0, -1, 0, 0, 0);
    for (; i + 2 <= count; i += 2)
    {
        const uint32_t p0 = texels[i], p1 = texels[i + 1];
        const uint32_t s0 = (p0 >> 24) | ((p0 >> 8) & 0xFF00) | ((p0 << 8) & 0xFF0000);
        const uint32_t s1 = (p1 >> 24) | ((p1 >> 8) & 0xFF00) | ((p1 << 8) & 0xFF0000);
        const __m128i s = _mm_unpacklo_epi8(_mm_set_epi32(0, 0, static_cast<int>(s1), static_cast<int>(s0)), zero);
        const __m128i d = _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(dst + 4 * i)), zero);
        const __m128i a = _mm_shufflehi_epi16(_mm_shufflelo_epi16(d, 0xFF), 0xFF);
        __m128i t = _mm_add_epi16(_mm_mullo_epi16(s, a), half);
        t = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(t, one), _mm_srli_epi16(t, 8)), 8);
        const __m128i out = _mm_or_si128(_mm_andnot_si128(alphaLanes, t), _mm_and_si128(alphaLanes, d));
        _mm_storel_epi64(reinterpret_cast<__m128i*>(dst + 4 * i), _mm_packus_epi16(out, zero));
    }
#endif
    for (; i < count; ++i)
    {
        const uint32_t p = texels[i];
        uint8_t* d = dst + 4 * i;
        const uint32_t da = d[3];
        d[0] = static_cast<uint8_t>((((p >> 24) & 0xFF) * da + 127) / 255);
        d[1] = static_cast<uint8_t>((((p >> 16) & 0xFF) * da + 127) / 255);
        d[2] = static_cast<uint8_t>((((p >> 8) & 0xFF) * da + 127) / 255);
    }
}

/// dst.rgb = min(src.rgb x (255 - dst.a) + dst.rgb, 255), dst.a kept: BLEND_FUNC(
/// ONE_MINUS_DST_ALPHA, ONE) with COLOR_MASK(1, 1, 1, 0)
inline void AddRgbTimesInverseDstAlpha(uint8_t* dst, const uint32_t* texels, uint32_t count)
{
    uint32_t i = 0;
#if defined(EVE_SIMD_NEON)
    const uint16x8_t half = vdupq_n_u16(127);
    const uint16x8_t one = vdupq_n_u16(1);
    const uint8x8_t full = vdup_n_u8(255);
    for (; i + 8 <= count; i += 8)
    {
        const uint8x8x4_t s = vld4_u8(reinterpret_cast<const uint8_t*>(texels + i));
        uint8x8x4_t d = vld4_u8(dst + 4 * i);
        const uint8x8_t inv = vsub_u8(full, d.val[3]);
        const uint8x8_t src[3] = {s.val[3], s.val[2], s.val[1]};
        for (int c = 0; c < 3; ++c)
        {
            uint16x8_t t = vaddq_u16(vmull_u8(src[c], inv), half);
            t = vshrq_n_u16(vaddq_u16(vaddq_u16(t, one), vshrq_n_u16(t, 8)), 8);
            d.val[c] = vqmovn_u16(vaddw_u8(t, d.val[c]));
        }
        vst4_u8(dst + 4 * i, d);
    }
#elif defined(EVE_SIMD_SSE2)
    const __m128i zero = _mm_setzero_si128();
    const __m128i half = _mm_set1_epi16(127);
    const __m128i one = _mm_set1_epi16(1);
    const __m128i full = _mm_set1_epi16(255);
    const __m128i alphaLanes = _mm_set_epi16(-1, 0, 0, 0, -1, 0, 0, 0);
    for (; i + 2 <= count; i += 2)
    {
        const uint32_t p0 = texels[i], p1 = texels[i + 1];
        const uint32_t s0 = (p0 >> 24) | ((p0 >> 8) & 0xFF00) | ((p0 << 8) & 0xFF0000);
        const uint32_t s1 = (p1 >> 24) | ((p1 >> 8) & 0xFF00) | ((p1 << 8) & 0xFF0000);
        const __m128i s = _mm_unpacklo_epi8(_mm_set_epi32(0, 0, static_cast<int>(s1), static_cast<int>(s0)), zero);
        const __m128i d = _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(dst + 4 * i)), zero);
        const __m128i inv = _mm_sub_epi16(full, _mm_shufflehi_epi16(_mm_shufflelo_epi16(d, 0xFF), 0xFF));
        __m128i t = _mm_add_epi16(_mm_mullo_epi16(s, inv), half);
        t = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(t, one), _mm_srli_epi16(t, 8)), 8);
        t = _mm_add_epi16(t, d);
        const __m128i out = _mm_or_si128(_mm_andnot_si128(alphaLanes, t), _mm_and_si128(alphaLanes, d));
        _mm_storel_epi64(reinterpret_cast<__m128i*>(dst + 4 * i), _mm_packus_epi16(out, zero));
    }
#endif
    for (; i < count; ++i)
    {
        const uint32_t p = texels[i];
        uint8_t* d = dst + 4 * i;
        const uint32_t inv = 255 - d[3];
        const uint32_t src[3] = {(p >> 24) & 0xFF, (p >> 16) & 0xFF, (p >> 8) & 0xFF};
        for (int c = 0; c < 3; ++c)
        {
            const uint32_t v = (src[c] * inv + 127) / 255 + d[c];
            d[c] = static_cast<uint8_t>(v > 255 ? 255 : v);
        }
    }
}

/// BILINEAR blend of four taps per pixel with weights out of 256 (8.8): w11 = fx x fy >> 8,
/// w10 = fx - w11, w01 = fy - w11, w00 = 256 - fx - fy + w11, each channel the sum of the
/// four products shifted right by 8 one by one (the general path's Weights / Blend4).
/// Texels packed, any channel order (the same arithmetic per byte); fx per pixel, fy for the span
inline void BilinearBlend(const uint32_t* t00, const uint32_t* t10, const uint32_t* t01, const uint32_t* t11,
                          const uint8_t* fx, uint32_t fy, uint32_t* out, uint32_t count)
{
    uint32_t i = 0;
#if defined(EVE_SIMD_NEON)
    const uint16x8_t vfy = vdupq_n_u16(static_cast<uint16_t>(fy));
    const uint16x8_t v256 = vdupq_n_u16(256);
    for (; i + 8 <= count; i += 8)
    {
        const uint16x8_t vfx = vmovl_u8(vld1_u8(fx + i));
        const uint16x8_t w11 = vshrq_n_u16(vmulq_u16(vfx, vfy), 8);
        const uint16x8_t w10 = vsubq_u16(vfx, w11);
        const uint16x8_t w01 = vsubq_u16(vfy, w11);
        const uint16x8_t w00 = vaddq_u16(vsubq_u16(vsubq_u16(v256, vfx), vfy), w11);
        const uint8x8x4_t a = vld4_u8(reinterpret_cast<const uint8_t*>(t00 + i));
        const uint8x8x4_t b = vld4_u8(reinterpret_cast<const uint8_t*>(t10 + i));
        const uint8x8x4_t c = vld4_u8(reinterpret_cast<const uint8_t*>(t01 + i));
        const uint8x8x4_t e = vld4_u8(reinterpret_cast<const uint8_t*>(t11 + i));
        uint8x8x4_t r;
        for (int k = 0; k < 4; ++k)
        {
            uint16x8_t sum = vshrq_n_u16(vmulq_u16(vmovl_u8(a.val[k]), w00), 8);
            sum = vaddq_u16(sum, vshrq_n_u16(vmulq_u16(vmovl_u8(b.val[k]), w10), 8));
            sum = vaddq_u16(sum, vshrq_n_u16(vmulq_u16(vmovl_u8(c.val[k]), w01), 8));
            sum = vaddq_u16(sum, vshrq_n_u16(vmulq_u16(vmovl_u8(e.val[k]), w11), 8));
            r.val[k] = vmovn_u16(sum);
        }
        vst4_u8(reinterpret_cast<uint8_t*>(out + i), r);
    }
#elif defined(EVE_SIMD_SSE2)
    const __m128i zero = _mm_setzero_si128();
    for (; i + 2 <= count; i += 2)
    {
        auto weights = [fy](uint32_t f, uint16_t* w) {
            const uint32_t w11 = (f * fy) >> 8;
            w[0] = static_cast<uint16_t>(256 - f - fy + w11);
            w[1] = static_cast<uint16_t>(f - w11);
            w[2] = static_cast<uint16_t>(fy - w11);
            w[3] = static_cast<uint16_t>(w11);
        };
        uint16_t w0[4], w1[4];
        weights(fx[i], w0);
        weights(fx[i + 1], w1);
        const __m128i taps[4] = {
            _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(t00 + i)), zero),
            _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(t10 + i)), zero),
            _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(t01 + i)), zero),
            _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(t11 + i)), zero)};
        __m128i sum = zero;
        for (int k = 0; k < 4; ++k)
        {
            const short a0 = static_cast<short>(w0[k]), a1 = static_cast<short>(w1[k]);
            const __m128i w = _mm_set_epi16(a1, a1, a1, a1, a0, a0, a0, a0);
            sum = _mm_add_epi16(sum, _mm_srli_epi16(_mm_mullo_epi16(taps[k], w), 8));
        }
        _mm_storel_epi64(reinterpret_cast<__m128i*>(out + i), _mm_packus_epi16(sum, zero));
    }
#endif
    for (; i < count; ++i)
    {
        const uint32_t f = fx[i];
        const uint32_t w11 = (f * fy) >> 8;
        const uint32_t w[4] = {256 - f - fy + w11, f - w11, fy - w11, w11};
        const uint32_t taps[4] = {t00[i], t10[i], t01[i], t11[i]};
        uint32_t result = 0;
        for (uint32_t shift = 0; shift < 32; shift += 8)
        {
            uint32_t v = 0;
            for (int k = 0; k < 4; ++k)
                v += (((taps[k] >> shift) & 0xFF) * w[k]) >> 8;
            result |= v << shift;
        }
        out[i] = result;
    }
}

} // namespace Simd
} // namespace EveLib

#endif
