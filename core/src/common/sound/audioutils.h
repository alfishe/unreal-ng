#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__SSE2__) || defined(_M_AMD64) || defined(_M_IX86)
#include <emmintrin.h>
#define USE_SSE2
#elif defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define USE_NEON
#endif

namespace AudioUtils
{

// Performs high-performance, safe audio mixing of two 16-bit PCM streams.
//
// This method is the primary audio mixer for combining multiple sound sources (e.g.,
// AY-3-8910 chip and the Beeper). It is designed as a drop-in replacement for scalar
// loops, providing significant performance benefits through platform-specific SIMD
// acceleration while ensuring audio fidelity through hardware-level saturating addition.
//
// Key features:
// - Clipping Protection: Uses saturating arithmetic (SSE2 adds_epi16 / NEON vqadd)
//   to prevent digital "wrap-around" noise when the combined amplitude exceeds PCM16 range.
// - SIMD Acceleration: Processes 8 samples (128 bits) per instruction on modern CPUs.
// - Bit-Exact: Handles odd sample counts correctly using unaligned loads and tail processing.
// - Low Latency: Optimized to run inside the frame-end hardware emulation events.
//
// @param src1 First source buffer (typically the primary sound chip output)
// @param src2 Second source buffer (typically the beeper or secondary chip)
// @param dst Destination buffer (can be the same as src1 or src2 for in-place mixing)
// @param count Total number of samples (int16_t) to process
inline void MixAudio(const int16_t* src1, const int16_t* src2, int16_t* dst, size_t count)
{
    size_t i = 0;

#if defined(USE_SSE2)
    // Process 8 samples (128 bits) at a time using SSE2
    for (; i + 7 < count; i += 8)
    {
        // Load 8 signed 16-bit integers from unaligned memory into a 128-bit XMM register
        __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src1 + i));
        __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src2 + i));

        // Sum 8 pairs of 16-bit integers with saturation:
        // If the result exceeds 32767, it stays at 32767.
        // If the result is less than -32768, it stays at -32768.
        // This is critical for audio mixing to prevent wrap-around noise (clipping).
        __m128i res = _mm_adds_epi16(v1, v2);

        // Store the 128-bit result back to unaligned memory
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), res);
    }

    // Process remaining 4 samples (64 bits) if available using SSE2
    // This handles the specific 1764 % 8 == 4 case without a loop
    if (i + 3 < count)
    {
        __m128i v1 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(src1 + i));
        __m128i v2 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(src2 + i));
        __m128i res = _mm_adds_epi16(v1, v2);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(dst + i), res);
        i += 4;
    }
#elif defined(USE_NEON)
    // Process 8 samples (128 bits) at a time using ARM NEON
    for (; i + 7 < count; i += 8)
    {
        // Load 8 signed 16-bit integers into a 128-bit Q register
        int16x8_t v1 = vld1q_s16(src1 + i);
        int16x8_t v2 = vld1q_s16(src2 + i);

        // Saturating addition of two 128-bit vectors containing 8 signed 16-bit elements
        // This provides hardware-accelerated clipping protection for high-amplitude audio samples.
        int16x8_t res = vqaddq_s16(v1, v2);

        // Store the 128-bit result vector to memory
        vst1q_s16(dst + i, res);
    }

    // Process remaining 4 samples (64 bits) using NEON 64-bit wide intrinsics
    if (i + 3 < count)
    {
        int16x4_t v1 = vld1_s16(src1 + i);
        int16x4_t v2 = vld1_s16(src2 + i);
        int16x4_t res = vqadd_s16(v1, v2);
        vst1_s16(dst + i, res);
        i += 4;
    }
#endif

    // Fallback for all samples (on non-SIMD platforms) or remainder samples (on SIMD platforms)
    const size_t remaining = count - i;
    const int16_t* psrc1 = src1 + i;
    const int16_t* psrc2 = src2 + i;
    int16_t* pdst = dst + i;

    for (size_t k = 0; k < remaining; k++)
    {
        int32_t mixed = static_cast<int32_t>(psrc1[k]) + static_cast<int32_t>(psrc2[k]);
        if (mixed > 32767)
            mixed = 32767;
        else if (mixed < -32768)
            mixed = -32768;
        pdst[k] = static_cast<int16_t>(mixed);
    }
}

}  // namespace AudioUtils
