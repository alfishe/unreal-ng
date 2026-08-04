#pragma once

#include <cmath>
#include <cstring>

// SIMD support detection
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define DECIMATOR_STEREO_USE_NEON 1
#elif defined(__SSE2__)
#include <emmintrin.h>
#define DECIMATOR_STEREO_USE_SSE2 1
#endif

/// Stereo polyphase FIR decimator for AY native clock rendering
/// Processes L/R channels together for better cache locality and fewer function calls
/// Decimates from PSG_CLOCK_RATE/8 (218.75 kHz) to 44.1 kHz
///
/// Design: 96-tap FIR lowpass, Fc=20kHz @ 218.75kHz, Kaiser window (beta=5)
/// Optimized with SIMD - processes L+R simultaneously
class FilterDecimatorStereo
{
public:
    static constexpr size_t FIR_TAPS = 96;
    static constexpr double INPUT_RATE = 218750.0;
    static constexpr double OUTPUT_RATE = 44100.0;
    static constexpr double SAMPLES_PER_OUTPUT = INPUT_RATE / OUTPUT_RATE;  // ~4.96

private:
    // Interleaved stereo double-buffer: [L0,R0,L1,R1,...] x 2
    // Total: FIR_TAPS * 2 (stereo) * 2 (double-buffer) = 384 doubles
    alignas(32) double _buffer[FIR_TAPS * 2 * 2];
    size_t _writeIndex;  // Current write position (0 to FIR_TAPS-1)
    double _phase;

    // 96-tap lowpass coefficients (same as mono version)
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
    FilterDecimatorStereo()
    {
        reset();
    }

    void reset()
    {
        std::memset(_buffer, 0, sizeof(_buffer));
        _writeIndex = 0;
        _phase = 0.0;
    }

    /// Feed one stereo sample pair at generator rate (218.75 kHz)
    /// Writes to both halves of double-buffer for wrap-free convolution
    inline void feedSample(double left, double right)
    {
        const size_t idx = _writeIndex * 2;
        // First half
        _buffer[idx] = left;
        _buffer[idx + 1] = right;
        // Mirror to second half
        _buffer[idx + FIR_TAPS * 2] = left;
        _buffer[idx + FIR_TAPS * 2 + 1] = right;

        _writeIndex = (_writeIndex + 1) % FIR_TAPS;
        _phase += 1.0;
    }

    /// Check if enough samples for one output (fractional)
    inline bool hasOutput() const
    {
        return _phase >= SAMPLES_PER_OUTPUT;
    }

    /// Get stereo output sample pair (call only when hasOutput() is true)
    void getOutput(double& outLeft, double& outRight)
    {
        _phase -= SAMPLES_PER_OUTPUT;

        // Convolution starts at oldest sample
        const double* src = &_buffer[_writeIndex * 2];

#if DECIMATOR_STEREO_USE_NEON
        convolveStereoNEON(src, outLeft, outRight);
#elif DECIMATOR_STEREO_USE_SSE2
        convolveStereoSSE2(src, outLeft, outRight);
#else
        convolveStereoScalar(src, outLeft, outRight);
#endif
    }

private:
    /// Scalar stereo convolution (fallback)
    static void convolveStereoScalar(const double* src, double& outL, double& outR)
    {
        double sumL = 0.0, sumR = 0.0;
        for (size_t i = 0; i < FIR_TAPS; i++)
        {
            double coef = FIR_COEFFS[i];
            sumL += src[i * 2] * coef;
            sumR += src[i * 2 + 1] * coef;
        }
        outL = sumL;
        outR = sumR;
    }

#if DECIMATOR_STEREO_USE_NEON
    /// NEON stereo convolution - process L+R with 4 accumulators each
    /// 8 taps per iteration (4 stereo pairs), 12 iterations total
    static void convolveStereoNEON(const double* src, double& outL, double& outR)
    {
        float64x2_t sumL0 = vdupq_n_f64(0.0);
        float64x2_t sumL1 = vdupq_n_f64(0.0);
        float64x2_t sumR0 = vdupq_n_f64(0.0);
        float64x2_t sumR1 = vdupq_n_f64(0.0);

        for (size_t i = 0; i < FIR_TAPS; i += 4)
        {
            // Load 4 stereo pairs (8 doubles): L0,R0,L1,R1,L2,R2,L3,R3
            float64x2_t s0 = vld1q_f64(&src[i * 2]);      // L0, R0
            float64x2_t s1 = vld1q_f64(&src[i * 2 + 2]);  // L1, R1
            float64x2_t s2 = vld1q_f64(&src[i * 2 + 4]);  // L2, R2
            float64x2_t s3 = vld1q_f64(&src[i * 2 + 6]);  // L3, R3

            // Load 4 coefficients and duplicate for stereo
            float64x2_t c0 = vdupq_n_f64(FIR_COEFFS[i]);
            float64x2_t c1 = vdupq_n_f64(FIR_COEFFS[i + 1]);
            float64x2_t c2 = vdupq_n_f64(FIR_COEFFS[i + 2]);
            float64x2_t c3 = vdupq_n_f64(FIR_COEFFS[i + 3]);

            // FMA: each s contains [L,R], multiply by same coef
            float64x2_t p0 = vmulq_f64(s0, c0);  // [L0*c0, R0*c0]
            float64x2_t p1 = vmulq_f64(s1, c1);  // [L1*c1, R1*c1]
            float64x2_t p2 = vmulq_f64(s2, c2);
            float64x2_t p3 = vmulq_f64(s3, c3);

            // Accumulate pairs
            sumL0 = vaddq_f64(sumL0, p0);
            sumL1 = vaddq_f64(sumL1, p1);
            sumR0 = vaddq_f64(sumR0, p2);
            sumR1 = vaddq_f64(sumR1, p3);
        }

        // Combine accumulators
        float64x2_t total0 = vaddq_f64(sumL0, sumL1);
        float64x2_t total1 = vaddq_f64(sumR0, sumR1);
        float64x2_t total = vaddq_f64(total0, total1);

        // total contains [sumL, sumR] interleaved - extract
        outL = vgetq_lane_f64(total, 0);
        outR = vgetq_lane_f64(total, 1);
    }
#endif

#if DECIMATOR_STEREO_USE_SSE2
    /// SSE2 stereo convolution - process L+R with 4 accumulators each
    static void convolveStereoSSE2(const double* src, double& outL, double& outR)
    {
        __m128d sumL0 = _mm_setzero_pd();
        __m128d sumL1 = _mm_setzero_pd();
        __m128d sumR0 = _mm_setzero_pd();
        __m128d sumR1 = _mm_setzero_pd();

        for (size_t i = 0; i < FIR_TAPS; i += 4)
        {
            // Load 4 stereo pairs
            __m128d s0 = _mm_loadu_pd(&src[i * 2]);      // L0, R0
            __m128d s1 = _mm_loadu_pd(&src[i * 2 + 2]);  // L1, R1
            __m128d s2 = _mm_loadu_pd(&src[i * 2 + 4]);  // L2, R2
            __m128d s3 = _mm_loadu_pd(&src[i * 2 + 6]);  // L3, R3

            // Load and duplicate coefficients
            __m128d c0 = _mm_set1_pd(FIR_COEFFS[i]);
            __m128d c1 = _mm_set1_pd(FIR_COEFFS[i + 1]);
            __m128d c2 = _mm_set1_pd(FIR_COEFFS[i + 2]);
            __m128d c3 = _mm_set1_pd(FIR_COEFFS[i + 3]);

            // Multiply and accumulate
            sumL0 = _mm_add_pd(sumL0, _mm_mul_pd(s0, c0));
            sumL1 = _mm_add_pd(sumL1, _mm_mul_pd(s1, c1));
            sumR0 = _mm_add_pd(sumR0, _mm_mul_pd(s2, c2));
            sumR1 = _mm_add_pd(sumR1, _mm_mul_pd(s3, c3));
        }

        // Combine accumulators
        __m128d total0 = _mm_add_pd(sumL0, sumL1);
        __m128d total1 = _mm_add_pd(sumR0, sumR1);
        __m128d total = _mm_add_pd(total0, total1);

        // Extract L and R
        alignas(16) double result[2];
        _mm_store_pd(result, total);
        outL = result[0];
        outR = result[1];
    }
#endif
};
