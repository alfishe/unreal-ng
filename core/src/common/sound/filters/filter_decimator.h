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
/// windowed-sinc): coefficients depend on the cutoff because the input
/// side is fixed at 218.75 kHz — changing the output rate changes only the
/// phase step. At (44100, Reference) the design is bit-identical to the
/// historically shipped MATLAB table (asserted by fir_designer_test.cpp).
///
/// TSFM extension (design §6.3): configure() takes an inputRate (the FM
/// sample clock is 2x the SSG generator rate, 437.5 kHz); the tap count
/// scales with it so the transition width in Hz stays constant. A decimator
/// can also run in slave mode — no phase accumulator of its own, output
/// exactly when a master decimator outputs — which locks the FM streams
/// sample-for-sample to the SSG master.
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
    /// 384 = 192 (HighFidelity base) scaled 2x for the TSFM FM input rate
    static constexpr size_t MAX_TAPS = 384;

private:
    std::vector<double> _coeffs;
    size_t _taps;
    double _samplesPerOutput;

    /// Slave-mode master (§6.3): when set, this decimator keeps no phase of
    /// its own and produces output exactly when the master does. nullptr =
    /// standalone. Cleared by configure() — re-attach after a redesign.
    const FilterDecimator* _master = nullptr;

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
    /// inputRate redesigns the filter for a different input side (TSFM FM
    /// decimation runs at 437.5 kHz, §6.3): taps scale with it so the
    /// transition width in Hz stays constant — at the default INPUT_RATE the
    /// tap counts stay 96/192 and the (44100, Reference) design stays
    /// bit-identical to the shipped table.
    void configure(double outputRate, Quality quality = Quality::Reference, bool extendedBandwidth = false,
                   double inputRate = INPUT_RATE)
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

        const size_t baseTaps = (quality == Quality::HighFidelity) ? 192 : 96;
        _taps = size_t(std::lround(double(baseTaps) * inputRate / INPUT_RATE));
        if (_taps > MAX_TAPS)
            _taps = MAX_TAPS;
        const double beta = (quality == Quality::HighFidelity) ? 9.0 : 5.0;
        _coeffs = FirDesigner::kaiser(_taps, fc, inputRate, beta);
        _samplesPerOutput = inputRate / outputRate;

        _master = nullptr;  // a redesigned filter is its own clock again
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

    /// Slave mode (§6.3): produce output exactly when `master` does. The
    /// master must outlive the slave and must not itself be a slave. Rate
    /// compatibility is the caller's contract: fed 2x as often as the
    /// master, a 2x-input-rate slave always has enough history when the
    /// master's phase trips.
    void attachMaster(const FilterDecimator* master)
    {
        _master = master;
    }

    /// Feed one input sample at the generator rate. In slave mode the
    /// sample joins the history ring; the master owns the output cadence.
    void feedSample(double sample)
    {
        _buffer[_bufferIndex] = sample;
        _bufferIndex = (_bufferIndex + 1) % MAX_TAPS;
        if (!_master)
            _phase += 1.0;
    }

    /// Check if enough samples for one output (fractional). A slave
    /// delegates: it outputs exactly when its master does.
    bool hasOutput() const
    {
        return _master ? _master->hasOutput() : _phase >= _samplesPerOutput;
    }

    /// Get output sample (call only when hasOutput() is true). A slave
    /// never touches a phase — the master already consumed its own.
    double getOutput()
    {
        if (!_master)
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
