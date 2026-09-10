#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__SSE2__) || defined(_M_AMD64) || defined(_M_IX86)
#include <emmintrin.h>
#define USE_SSE2
#elif defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define USE_NEON
#endif

namespace VideoUtils
{

// High-performance framebuffer copy for frame latching / presentation.
//
// Used on the emulation thread to latch the completed frame into the
// presentation buffer at frame end, and on the GUI thread to pull the latched
// frame into the widget backing store. Both copies sit on latency-sensitive
// paths (the latch briefly blocks the emulation loop), so the bulk is done
// 64 bytes per iteration with platform SIMD and a scalar memcpy tail/fallback.
//
// - SSE2: 4x 128-bit unaligned load/store per iteration
// - NEON: 4x 128-bit vld1q/vst1q per iteration
// - Fallback: plain memcpy (already vectorized in modern libc, kept for
//   platforms without SSE2/NEON and for the unaligned tail)
//
// @param dst Destination buffer (must not overlap src)
// @param src Source buffer
// @param bytes Number of bytes to copy
inline void CopyFrameBuffer(uint8_t* dst, const uint8_t* src, size_t bytes)
{
    size_t i = 0;

#if defined(USE_SSE2)
    for (; i + 64 <= bytes; i += 64)
    {
        __m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 16));
        __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 32));
        __m128i v3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 48));

        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), v0);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 16), v1);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 32), v2);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 48), v3);
    }
#elif defined(USE_NEON)
    for (; i + 64 <= bytes; i += 64)
    {
        uint8x16_t v0 = vld1q_u8(src + i);
        uint8x16_t v1 = vld1q_u8(src + i + 16);
        uint8x16_t v2 = vld1q_u8(src + i + 32);
        uint8x16_t v3 = vld1q_u8(src + i + 48);

        vst1q_u8(dst + i, v0);
        vst1q_u8(dst + i + 16, v1);
        vst1q_u8(dst + i + 32, v2);
        vst1q_u8(dst + i + 48, v3);
    }
#endif

    // Scalar tail (and full fallback when no SIMD is available)
    if (i < bytes)
    {
        memcpy(dst + i, src + i, bytes - i);
    }
}

}  // namespace VideoUtils
