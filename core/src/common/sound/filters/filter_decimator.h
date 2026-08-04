#pragma once

#include <cmath>
#include <cstring>

// SIMD support detection
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define DECIMATOR_USE_NEON 1
#elif defined(__SSE2__)
#include <emmintrin.h>
#define DECIMATOR_USE_SSE2 1
#endif

/// Polyphase FIR decimator for AY native clock rendering
/// Decimates from PSG_CLOCK_RATE/8 (218.75 kHz) to 44.1 kHz
/// Uses fractional phase accumulator for non-integer ratio (~4.96:1)
///
/// Design: 96-tap FIR lowpass, Fc=20kHz @ 218.75kHz, Kaiser window (beta=5)
/// Optimized with SIMD (NEON/SSE2) and double-buffer to eliminate linearization
class FilterDecimator
{
public:
    static constexpr size_t FIR_TAPS = 96;
    static constexpr double INPUT_RATE = 218750.0;
    static constexpr double OUTPUT_RATE = 44100.0;
    static constexpr double SAMPLES_PER_OUTPUT = INPUT_RATE / OUTPUT_RATE;  // ~4.96

private:
    // Double-buffer: samples written to both halves for wrap-free SIMD access
    // Layout: [0..95] mirrors [96..191], feedSample writes to both
    alignas(16) double _buffer[FIR_TAPS * 2];
    size_t _writeIndex;  // Current write position (0 to FIR_TAPS-1)
    double _phase;

    // 96-tap lowpass, Fc=20kHz @ Fs=218.75kHz, Kaiser beta=5
    alignas(16) static constexpr double FIR_COEFFS[FIR_TAPS] = {
         2.052603086353451149e-04,  3.210251422678139137e-04,  3.437220785102240109e-04,  2.109182111510678919e-04,
        -8.813007722059908102e-05, -4.870812384487524906e-04, -8.458617388491266806e-04, -9.899287167638743268e-04,
        -7.779213535741368686e-04, -1.752127463883414616e-04,  6.977284471930135209e-04,  1.570219993981382964e-03,
         2.091556053222377084e-03,  1.953639329633998986e-03,  1.027983115324567712e-03, -5.359199887113940504e-04,
        -2.300145079851742807e-03, -3.640391776539313954e-03, -3.945303735737179344e-03, -2.855862974611067008e-03,
        -4.599512339460455341e-04,  2.642683488913957334e-03,  5.456686481103634884e-03,  6.880934400135874096e-03,
         6.096971819493468385e-03,  2.929874713795616644e-03, -1.957650331064132720e-03, -7.140749659778805429e-03,
        -1.079993258728825789e-02, -1.130107842741509723e-02, -7.818418433736594106e-03, -7.870218846449325567e-04,
         8.004251012786215216e-03,  1.577440165148706955e-02,  1.952116805334440375e-02,  1.701080481789962739e-02,
         7.695582406293094944e-03, -6.749873188107499179e-03, -2.250139455088128945e-02, -3.433154299241062551e-02,
        -3.689163058992080135e-02, -2.621237878612778932e-02, -1.007910885111672206e-03,  3.661206987612355968e-02,
         8.127633587913619950e-02,  1.253609490622179801e-01,  1.606540571497764303e-01,  1.802624694847022313e-01,
         1.802624694847022313e-01,  1.606540571497764303e-01,  1.253609490622179801e-01,  8.127633587913619950e-02,
         3.661206987612355968e-02, -1.007910885111672206e-03, -2.621237878612778932e-02, -3.689163058992080135e-02,
        -3.433154299241062551e-02, -2.250139455088128945e-02, -6.749873188107499179e-03,  7.695582406293094944e-03,
         1.701080481789962739e-02,  1.952116805334440375e-02,  1.577440165148706955e-02,  8.004251012786215216e-03,
        -7.870218846449325567e-04, -7.818418433736594106e-03, -1.130107842741509723e-02, -1.079993258728825789e-02,
        -7.140749659778805429e-03, -1.957650331064132720e-03,  2.929874713795616644e-03,  6.096971819493468385e-03,
         6.880934400135874096e-03,  5.456686481103634884e-03,  2.642683488913957334e-03, -4.599512339460455341e-04,
        -2.855862974611067008e-03, -3.945303735737179344e-03, -3.640391776539313954e-03, -2.300145079851742807e-03,
        -5.359199887113940504e-04,  1.027983115324567712e-03,  1.953639329633998986e-03,  2.091556053222377084e-03,
         1.570219993981382964e-03,  6.977284471930135209e-04, -1.752127463883414616e-04, -7.779213535741368686e-04,
        -9.899287167638743268e-04, -8.458617388491266806e-04, -4.870812384487524906e-04, -8.813007722059908102e-05,
         2.109182111510678919e-04,  3.437220785102240109e-04,  3.210251422678139137e-04,  2.052603086353451149e-04
    };

public:
    FilterDecimator()
    {
        reset();
    }

    void reset()
    {
        std::memset(_buffer, 0, sizeof(_buffer));
        _writeIndex = 0;
        _phase = 0.0;
    }

    /// Feed one input sample at generator rate (218.75 kHz)
    /// Writes to both halves of double-buffer for wrap-free convolution
    inline void feedSample(double sample)
    {
        _buffer[_writeIndex] = sample;
        _buffer[_writeIndex + FIR_TAPS] = sample;
        _writeIndex = (_writeIndex + 1) % FIR_TAPS;
        _phase += 1.0;
    }

    /// Check if enough samples for one output (fractional)
    inline bool hasOutput() const
    {
        return _phase >= SAMPLES_PER_OUTPUT;
    }

    /// Get output sample (call only when hasOutput() is true)
    /// Convolution reads contiguous memory from double-buffer, no linearization needed
    double getOutput()
    {
        _phase -= SAMPLES_PER_OUTPUT;

        // Convolution starts at oldest sample: _writeIndex points to next write,
        // so oldest is at _writeIndex (will be overwritten next), read FIR_TAPS forward
        const double* src = &_buffer[_writeIndex];

#if DECIMATOR_USE_NEON
        return convolveNEON(src);
#elif DECIMATOR_USE_SSE2
        return convolveSSE2(src);
#else
        return convolveScalar(src);
#endif
    }

private:
    /// Scalar convolution (fallback)
    static double convolveScalar(const double* src)
    {
        double sum = 0.0;
        for (size_t i = 0; i < FIR_TAPS; i++)
        {
            sum += src[i] * FIR_COEFFS[i];
        }
        return sum;
    }

#if DECIMATOR_USE_NEON
    /// NEON-optimized convolution (ARM64)
    /// Process 8 doubles per iteration using four float64x2_t accumulators
    /// 96 taps / 8 = 12 iterations, maximizes instruction-level parallelism
    static double convolveNEON(const double* src)
    {
        float64x2_t sum0 = vdupq_n_f64(0.0);
        float64x2_t sum1 = vdupq_n_f64(0.0);
        float64x2_t sum2 = vdupq_n_f64(0.0);
        float64x2_t sum3 = vdupq_n_f64(0.0);

        for (size_t i = 0; i < FIR_TAPS; i += 8)
        {
            // Load 8 buffer samples (4 vectors)
            float64x2_t buf0 = vld1q_f64(&src[i]);
            float64x2_t buf1 = vld1q_f64(&src[i + 2]);
            float64x2_t buf2 = vld1q_f64(&src[i + 4]);
            float64x2_t buf3 = vld1q_f64(&src[i + 6]);

            // Load 8 coefficients (4 vectors)
            float64x2_t coef0 = vld1q_f64(&FIR_COEFFS[i]);
            float64x2_t coef1 = vld1q_f64(&FIR_COEFFS[i + 2]);
            float64x2_t coef2 = vld1q_f64(&FIR_COEFFS[i + 4]);
            float64x2_t coef3 = vld1q_f64(&FIR_COEFFS[i + 6]);

            // Fused multiply-accumulate into four independent accumulators
            sum0 = vfmaq_f64(sum0, buf0, coef0);
            sum1 = vfmaq_f64(sum1, buf1, coef1);
            sum2 = vfmaq_f64(sum2, buf2, coef2);
            sum3 = vfmaq_f64(sum3, buf3, coef3);
        }

        // Combine accumulators and horizontal add
        float64x2_t total01 = vaddq_f64(sum0, sum1);
        float64x2_t total23 = vaddq_f64(sum2, sum3);
        float64x2_t total = vaddq_f64(total01, total23);
        return vgetq_lane_f64(total, 0) + vgetq_lane_f64(total, 1);
    }
#endif

#if DECIMATOR_USE_SSE2
    /// SSE2-optimized convolution (x86-64)
    /// Process 8 doubles per iteration using four __m128d accumulators
    /// 96 taps / 8 = 12 iterations, maximizes instruction-level parallelism
    static double convolveSSE2(const double* src)
    {
        __m128d sum0 = _mm_setzero_pd();
        __m128d sum1 = _mm_setzero_pd();
        __m128d sum2 = _mm_setzero_pd();
        __m128d sum3 = _mm_setzero_pd();

        for (size_t i = 0; i < FIR_TAPS; i += 8)
        {
            // Load 8 buffer samples (4 vectors)
            __m128d buf0 = _mm_loadu_pd(&src[i]);
            __m128d buf1 = _mm_loadu_pd(&src[i + 2]);
            __m128d buf2 = _mm_loadu_pd(&src[i + 4]);
            __m128d buf3 = _mm_loadu_pd(&src[i + 6]);

            // Load 8 coefficients (aligned)
            __m128d coef0 = _mm_load_pd(&FIR_COEFFS[i]);
            __m128d coef1 = _mm_load_pd(&FIR_COEFFS[i + 2]);
            __m128d coef2 = _mm_load_pd(&FIR_COEFFS[i + 4]);
            __m128d coef3 = _mm_load_pd(&FIR_COEFFS[i + 6]);

            // Multiply-accumulate into four independent accumulators
            sum0 = _mm_add_pd(sum0, _mm_mul_pd(buf0, coef0));
            sum1 = _mm_add_pd(sum1, _mm_mul_pd(buf1, coef1));
            sum2 = _mm_add_pd(sum2, _mm_mul_pd(buf2, coef2));
            sum3 = _mm_add_pd(sum3, _mm_mul_pd(buf3, coef3));
        }

        // Combine accumulators and horizontal add
        __m128d total01 = _mm_add_pd(sum0, sum1);
        __m128d total23 = _mm_add_pd(sum2, sum3);
        __m128d total = _mm_add_pd(total01, total23);
        alignas(16) double result[2];
        _mm_store_pd(result, total);
        return result[0] + result[1];
    }
#endif
};
