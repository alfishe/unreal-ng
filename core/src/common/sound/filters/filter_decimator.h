#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "fir_designer.h"

/// Polyphase FIR decimator for AY native clock rendering
/// Decimates from PSG_CLOCK_RATE/8 (218.75 kHz) to the core output rate
/// Uses fractional phase accumulator for non-integer ratios (~4.96:1 at 44.1k)
///
/// The anti-alias FIR is designed at construction (FirDesigner, Kaiser
/// windowed-sinc): coefficients depend only on the cutoff because the input
/// side is fixed at 218.75 kHz — changing the output rate changes only the
/// phase step. At (44100, Reference) the design is bit-identical to the
/// historically shipped MATLAB table (asserted by fir_designer_test.cpp).
class FilterDecimator
{
public:
    /// Reference: 96-tap Kaiser beta=5 (~56 dB stopband) — the shipped character
    /// HighFidelity: 192-tap Kaiser beta=9 (~90 dB stopband)
    enum class Quality
    {
        Reference,
        HighFidelity
    };

    static constexpr double INPUT_RATE = 218750.0;
    static constexpr size_t MAX_TAPS = 192;

private:
    std::vector<double> _coeffs;
    size_t _taps;
    double _samplesPerOutput;

    double _buffer[MAX_TAPS];
    size_t _bufferIndex;
    double _phase;

public:
    FilterDecimator()
    {
        configure(44100.0);
    }

    /// Design the anti-alias FIR and set the phase step for outputRate.
    /// Default cutoff is 20 kHz at every rate (identical tonal character).
    /// extendedBandwidth opens the passband for archival capture at high
    /// rates: 40 kHz at >=88.2k output, 80 kHz at >=176.4k.
    void configure(double outputRate, Quality quality = Quality::Reference, bool extendedBandwidth = false)
    {
        double fc = 20000.0;
        if (extendedBandwidth)
        {
            if (outputRate >= 176400.0)
                fc = 80000.0;
            else if (outputRate >= 88200.0)
                fc = 40000.0;
        }
        // Nyquist guard for sub-44.1k rates; never triggers at supported rates,
        // so the (44100, Reference) design stays bit-identical to the shipped table
        if (fc >= outputRate / 2.0)
            fc = 0.45 * outputRate;

        _taps = (quality == Quality::HighFidelity) ? 192 : 96;
        const double beta = (quality == Quality::HighFidelity) ? 9.0 : 5.0;
        _coeffs = FirDesigner::kaiser(_taps, fc, INPUT_RATE, beta);
        _samplesPerOutput = INPUT_RATE / outputRate;

        reset();
    }

    void reset()
    {
        std::memset(_buffer, 0, sizeof(_buffer));
        _bufferIndex = 0;
        _phase = 0.0;
    }

    size_t taps() const { return _taps; }
    double samplesPerOutput() const { return _samplesPerOutput; }
    const std::vector<double>& coefficients() const { return _coeffs; }

    /// Feed one input sample at generator rate (218.75 kHz)
    void feedSample(double sample)
    {
        _buffer[_bufferIndex] = sample;
        _bufferIndex = (_bufferIndex + 1) % MAX_TAPS;
        _phase += 1.0;
    }

    /// Check if enough samples for one output (fractional)
    bool hasOutput() const
    {
        return _phase >= _samplesPerOutput;
    }

    /// Get output sample (call only when hasOutput() is true)
    double getOutput()
    {
        _phase -= _samplesPerOutput;

        double sum = 0.0;
        size_t idx = _bufferIndex;
        for (size_t i = 0; i < _taps; i++)
        {
            idx = (idx == 0) ? MAX_TAPS - 1 : idx - 1;
            sum += _buffer[idx] * _coeffs[i];
        }
        return sum;
    }
};
