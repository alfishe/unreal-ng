#pragma once

#include <stdafx.h>


/// Implements 40th order FIR DSP filter with 64x (2^6) oversampling
static struct UnrealFilter
{
    /// region <Filter constants>
protected:
    static constexpr size_t OVERSAMPLING_FACTOR = 64;
    static constexpr size_t FILTER_ARRAY_SIZE = OVERSAMPLING_FACTOR * 2;
    static constexpr size_t OVERSAMPLING_FACTOR_BITMASK = OVERSAMPLING_FACTOR - 1;

    /// Cutoff frequency is absolute (Hz, not a fraction of the sample rate):
    /// this preserves the shipped tonal character at every core rate.
    /// The shipped MATLAB table (fdatool: FIR window Hamming, order 127,
    /// fs = 44100 x 64 = 2822400, fc = 11025) is reproduced bit-exactly by
    /// the runtime designer at 44100 (asserted by fir_designer_test.cpp).
    static constexpr double CUTOFF_HZ = 11025.0;

    /// 128-tap Hamming windowed-sinc lowpass, designed at construction /
    /// setTimings for the active output sample rate (fs = rate x 64)
    double _oversamplingFIRCoefficients[FILTER_ARRAY_SIZE];

    // Helper array with cumulative coefficients sum
    size_t _stepResponseCoefficients[FILTER_ARRAY_SIZE];

    static constexpr double _filterSumFull = 1.0;
    static constexpr double _filterSumHalf = 0.5;
    static constexpr size_t _filterSumFullUnsigned = (size_t)(_filterSumFull * 0x10000);
    static constexpr size_t _filterSumHalfUnsigned = (size_t)(_filterSumHalf * 0x10000);

    /// endregion </Filter constants>

    /// region <Fields>
protected:
    // Timing parameters
    size_t _systemClockRate;
    size_t _audioChipClockRate;
    size_t _outputSampleRate;

    // Interpolation-related
    size_t s1_l;
    size_t s1_r;
    size_t s2_l;
    size_t s2_r;

    /// endregion </Fields>

    /// region <Constructor / destructors>
public:
    /// Design the FIR and helper arrays for the default 44100 Hz output rate
    UnrealFilter();
    virtual ~UnrealFilter() = default;
    /// endregion </Constructor / destructors>

    /// region <Methods>
public:
    void setTimings(size_t systemClockRate, size_t audioChipClockRate, size_t outputSampleRate);

    void interpolate(uint32_t startTick, uint32_t endTick, uint32_t left, uint32_t right);

    void applyFilter(uint16_t* input, uint16_t* output, size_t samplesLen);

    /// Active FIR coefficients (fir_designer_test.cpp validates the 44100
    /// design against the shipped golden table)
    const double* coefficients() const { return _oversamplingFIRCoefficients; }

protected:
    /// (Re)design the oversampling FIR and its step-response table for a rate
    void designForRate(size_t outputSampleRate);
    /// region </Methods>
} UnrealDSPFilter;
