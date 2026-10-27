#pragma once

#include <cmath>
#include <cstddef>

#if defined(__SSE2__) || defined(_M_AMD64) || defined(_M_X64)
#if defined(__FMA__) && !defined(_MSC_VER)
#include <immintrin.h>
#else
#include <emmintrin.h>
#endif
#define DECIMATOR_DOT_SSE2 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#if defined(_MSC_VER) && !defined(__clang__)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#define DECIMATOR_DOT_NEON 1
#endif

/// @file decimatordot.h
/// @brief The dot products of FilterDecimator::getOutput / getOutputs: K input streams against one pair of
/// coefficient rows (the two tabulated phases the output instant falls between).
///
/// Every stream keeps the summation order of the original scalar loop: four interleaved accumulators per row
/// (accumulator j sums the taps i with i % 4 == j, in increasing i), the taps past the last group of four go to
/// accumulator 0, the result is (acc0 + acc1) + (acc2 + acc3). The SIMD kernels hold accumulators (0, 1) and (2, 3)
/// in two 2-lane registers, which is that same order lane by lane, so every kernel returns the same bits as the
/// scalar one. The multiply-add is fused or not by one rule for every kernel (kFused), the rule the compilers apply
/// to the original `acc += s * c` in their default modes: GCC and clang contract it to one fused multiply-add where
/// the target has one (aarch64; x86-64 with -mfma / -march=x86-64-v3), MSVC never does. So the kernels return the
/// historical bits in the default builds and agree with each other under any flags (-ffp-contract=off included).
/// DecimatorDot_Test checks scalar == SIMD bit for bit.
///
/// Several streams share the coefficient loads of one pass over the taps (the YM2203 pair evaluates eight
/// decimators at one output instant): more independent accumulator chains, fewer loads per multiply-add.
///
/// x[k] points at stream k's newest tap; tap i is x[k][i] (FilterDecimator stores its history newest-first).
namespace DecimatorDot
{
/// The multiply-add rule (see above)
#if !defined(_MSC_VER) && (defined(__aarch64__) || defined(__FMA__))
constexpr bool kFused = true;
#else
constexpr bool kFused = false;
#endif

inline double MultiplyAdd(double acc, double s, double c)
{
    if constexpr (kFused)
        return std::fma(s, c, acc);
    else
        return acc + s * c;
}

/// Streams one kernel call handles (registers: K streams x 4 accumulators + 4 coefficient registers)
#if defined(DECIMATOR_DOT_NEON)
constexpr size_t kMaxStreams = 4;   // 32 vector registers
#else
constexpr size_t kMaxStreams = 2;   // 16 xmm registers on x86-64; the scalar fallback
#endif

template <size_t K>
inline void Scalar(const double* const* x, const double* a, const double* b, size_t length, double* ya, double* yb)
{
    double a0[K], a1[K], a2[K], a3[K], b0[K], b1[K], b2[K], b3[K];
    for (size_t k = 0; k < K; k++)
        a0[k] = a1[k] = a2[k] = a3[k] = b0[k] = b1[k] = b2[k] = b3[k] = 0.0;
    size_t i = 0;
    for (; i + 3 < length; i += 4)
    {
        for (size_t k = 0; k < K; k++)
        {
            const double s0 = x[k][i];
            const double s1 = x[k][i + 1];
            const double s2 = x[k][i + 2];
            const double s3 = x[k][i + 3];
            a0[k] = MultiplyAdd(a0[k], s0, a[i]);
            a1[k] = MultiplyAdd(a1[k], s1, a[i + 1]);
            a2[k] = MultiplyAdd(a2[k], s2, a[i + 2]);
            a3[k] = MultiplyAdd(a3[k], s3, a[i + 3]);
            b0[k] = MultiplyAdd(b0[k], s0, b[i]);
            b1[k] = MultiplyAdd(b1[k], s1, b[i + 1]);
            b2[k] = MultiplyAdd(b2[k], s2, b[i + 2]);
            b3[k] = MultiplyAdd(b3[k], s3, b[i + 3]);
        }
    }
    for (size_t k = 0; k < K; k++)
    {
        for (size_t j = i; j < length; j++)
        {
            const double s0 = x[k][j];
            a0[k] = MultiplyAdd(a0[k], s0, a[j]);
            b0[k] = MultiplyAdd(b0[k], s0, b[j]);
        }
        ya[k] = (a0[k] + a1[k]) + (a2[k] + a3[k]);
        yb[k] = (b0[k] + b1[k]) + (b2[k] + b3[k]);
    }
}

#if defined(DECIMATOR_DOT_NEON)
inline float64x2_t MultiplyAdd(float64x2_t acc, float64x2_t s, float64x2_t c)
{
    if constexpr (kFused)
        return vfmaq_f64(acc, s, c);
    else
        return vaddq_f64(acc, vmulq_f64(s, c));
}

template <size_t K>
inline void Simd(const double* const* x, const double* a, const double* b, size_t length, double* ya, double* yb)
{
    float64x2_t accA01[K], accA23[K], accB01[K], accB23[K];
    for (size_t k = 0; k < K; k++)
        accA01[k] = accA23[k] = accB01[k] = accB23[k] = vdupq_n_f64(0.0);
    size_t i = 0;
    for (; i + 3 < length; i += 4)
    {
        const float64x2_t ca01 = vld1q_f64(a + i);
        const float64x2_t ca23 = vld1q_f64(a + i + 2);
        const float64x2_t cb01 = vld1q_f64(b + i);
        const float64x2_t cb23 = vld1q_f64(b + i + 2);
        for (size_t k = 0; k < K; k++)
        {
            const float64x2_t s01 = vld1q_f64(x[k] + i);
            const float64x2_t s23 = vld1q_f64(x[k] + i + 2);
            accA01[k] = MultiplyAdd(accA01[k], s01, ca01);
            accA23[k] = MultiplyAdd(accA23[k], s23, ca23);
            accB01[k] = MultiplyAdd(accB01[k], s01, cb01);
            accB23[k] = MultiplyAdd(accB23[k], s23, cb23);
        }
    }
    for (size_t k = 0; k < K; k++)
    {
        double a0 = vgetq_lane_f64(accA01[k], 0);
        double b0 = vgetq_lane_f64(accB01[k], 0);
        for (size_t j = i; j < length; j++)
        {
            const double s0 = x[k][j];
            a0 = MultiplyAdd(a0, s0, a[j]);
            b0 = MultiplyAdd(b0, s0, b[j]);
        }
        ya[k] = (a0 + vgetq_lane_f64(accA01[k], 1)) + (vgetq_lane_f64(accA23[k], 0) + vgetq_lane_f64(accA23[k], 1));
        yb[k] = (b0 + vgetq_lane_f64(accB01[k], 1)) + (vgetq_lane_f64(accB23[k], 0) + vgetq_lane_f64(accB23[k], 1));
    }
}
#elif defined(DECIMATOR_DOT_SSE2)
inline __m128d MultiplyAdd(__m128d acc, __m128d s, __m128d c)
{
#if defined(__FMA__) && !defined(_MSC_VER)
    return _mm_fmadd_pd(s, c, acc);
#else
    return _mm_add_pd(acc, _mm_mul_pd(s, c));
#endif
}
inline double Low(__m128d v) { return _mm_cvtsd_f64(v); }
inline double High(__m128d v) { return _mm_cvtsd_f64(_mm_unpackhi_pd(v, v)); }

template <size_t K>
inline void Simd(const double* const* x, const double* a, const double* b, size_t length, double* ya, double* yb)
{
    __m128d accA01[K], accA23[K], accB01[K], accB23[K];
    for (size_t k = 0; k < K; k++)
        accA01[k] = accA23[k] = accB01[k] = accB23[k] = _mm_setzero_pd();
    size_t i = 0;
    for (; i + 3 < length; i += 4)
    {
        const __m128d ca01 = _mm_loadu_pd(a + i);
        const __m128d ca23 = _mm_loadu_pd(a + i + 2);
        const __m128d cb01 = _mm_loadu_pd(b + i);
        const __m128d cb23 = _mm_loadu_pd(b + i + 2);
        for (size_t k = 0; k < K; k++)
        {
            const __m128d s01 = _mm_loadu_pd(x[k] + i);
            const __m128d s23 = _mm_loadu_pd(x[k] + i + 2);
            accA01[k] = MultiplyAdd(accA01[k], s01, ca01);
            accA23[k] = MultiplyAdd(accA23[k], s23, ca23);
            accB01[k] = MultiplyAdd(accB01[k], s01, cb01);
            accB23[k] = MultiplyAdd(accB23[k], s23, cb23);
        }
    }
    for (size_t k = 0; k < K; k++)
    {
        double a0 = Low(accA01[k]);
        double b0 = Low(accB01[k]);
        for (size_t j = i; j < length; j++)
        {
            const double s0 = x[k][j];
            a0 = MultiplyAdd(a0, s0, a[j]);
            b0 = MultiplyAdd(b0, s0, b[j]);
        }
        ya[k] = (a0 + High(accA01[k])) + (Low(accA23[k]) + High(accA23[k]));
        yb[k] = (b0 + High(accB01[k])) + (Low(accB23[k]) + High(accB23[k]));
    }
}
#else
// SIMD-CANDIDATE(fir-decimator-dot): no SIMD kernel for this target; the scalar loop is the reference order
template <size_t K>
inline void Simd(const double* const* x, const double* a, const double* b, size_t length, double* ya, double* yb)
{
    Scalar<K>(x, a, b, length, ya, yb);
}
#endif

/// `count` streams (any number) against one pair of rows: kernel calls of up to kMaxStreams streams
inline void Rows(const double* const* x, size_t count, const double* a, const double* b, size_t length, double* ya,
                 double* yb)
{
    size_t k = 0;
    for (; k + kMaxStreams <= count; k += kMaxStreams)
        Simd<kMaxStreams>(x + k, a, b, length, ya + k, yb + k);
    switch (count - k)
    {
        case 1:
            Simd<1>(x + k, a, b, length, ya + k, yb + k);
            break;
#if defined(DECIMATOR_DOT_NEON)
        case 2:
            Simd<2>(x + k, a, b, length, ya + k, yb + k);
            break;
        case 3:
            Simd<3>(x + k, a, b, length, ya + k, yb + k);
            break;
#endif
        default:
            break;
    }
}
}  // namespace DecimatorDot
