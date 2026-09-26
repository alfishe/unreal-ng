#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <vector>

#include "fir_designer.h"

/// Polyphase FIR decimator for AY native clock rendering
/// Decimates from PSG_CLOCK_RATE/8 (218.75 kHz) to the core output rate
/// Uses fractional phase accumulator for non-integer ratios (~4.96:1 at 44.1k)
///
/// Exact output instants: an output falls due when the phase accumulator
/// crosses samplesPerOutput, which happens between two input samples - the
/// residue left after the subtraction is how many input samples ago the
/// output instant was. The filter is evaluated at that instant: the kernel is
/// tabulated at PHASES fractional offsets (rows normalized to unit DC gain)
/// and interpolated linearly between neighbouring rows. Evaluating at the
/// newest input sample instead put every output up to one input sample
/// (4.57 us) late, a jitter that raised non-harmonic error to ~-35 dB on a
/// 1 kHz tone (-15 dB at 10 kHz); at the exact instants the same 96-tap
/// design reaches ~79 dB. Row 0 is the integer design itself.
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
    /// Fractional offsets tabulated per input sample (linear in between)
    static constexpr size_t PHASES = 256;

private:
    /// Output instants reach back at most this many whole input samples (a
    /// 2x slave: 2 x a master residue below 1)
    static constexpr size_t MAX_DELAY = 3;
    /// History ring: a fractionally delayed row spans taps + 1 samples
    static constexpr size_t HISTORY = MAX_TAPS + 1 + MAX_DELAY;

    /// (PHASES + 1) rows of taps + 1 coefficients; row p is the kernel shifted
    /// by p / PHASES input samples. Shared by every decimator with the design
    struct PhaseTable
    {
        size_t rowLength = 0;
        std::vector<double> rows;
    };

    std::vector<double> _coeffs;
    std::shared_ptr<const PhaseTable> _table;
    size_t _taps;
    double _samplesPerOutput;

    /// Slave-mode master (§6.3): when set, this decimator keeps no phase of
    /// its own and produces output exactly when the master does. nullptr =
    /// standalone. Cleared by configure() — re-attach after a redesign.
    const FilterDecimator* _master = nullptr;

    // History ring, mirrored: every sample is stored at i and at i + HISTORY,
    // so the taps of getOutput() read one contiguous run backwards from any
    // start without a wrap check per tap (the same values in the same order,
    // bit-identical output - it only removes the per-tap branch/modulo)
    double _buffer[2 * HISTORY];
    size_t _bufferIndex;
    double _phase;

    static std::shared_ptr<const PhaseTable> phaseTableFor(size_t taps, double fc, double inputRate, double beta)
    {
        // Designs repeat (4 SSG decimators per device, one device per
        // emulator instance): build each once per process. Held strongly:
        // there are only a handful of (taps, fc, rate, quality) designs, a few
        // hundred KB each, while a weak cache expired whenever the last
        // instance went away and every new instance (every test) redesigned
        // them - ~257 x 97 Kaiser evaluations each
        static std::mutex mutex;
        static std::map<std::tuple<size_t, double, double, double>, std::shared_ptr<const PhaseTable>> cache;
        const auto key = std::make_tuple(taps, fc, inputRate, beta);
        std::lock_guard<std::mutex> lock(mutex);
        if (auto cached = cache[key])
            return cached;

        auto table = std::make_shared<PhaseTable>();
        table->rowLength = taps + 1;
        table->rows.resize((PHASES + 1) * table->rowLength);
        const double i0beta = FirDesigner::besselI0(beta);
        for (size_t p = 0; p <= PHASES; p++)
        {
            double* row = &table->rows[p * table->rowLength];
            const double shift = double(p) / double(PHASES);
            double sum = 0.0;
            for (size_t i = 0; i < table->rowLength; i++)
            {
                row[i] = FirDesigner::kaiserAt(double(i) - shift, taps, fc, inputRate, beta, i0beta);
                sum += row[i];
            }
            for (size_t i = 0; i < table->rowLength; i++)
                row[i] /= sum;  // unity DC gain at every offset
        }
        cache[key] = table;
        return table;
    }

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
        _table = phaseTableFor(_taps, fc, inputRate, beta);
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

    /// Clear the FIR history only; the resampling phase (a tick-gating
    /// accumulator, see phase()) keeps its position. For a history that went
    /// stale while the filter was not fed (LQ mode, suppressed synthesis)
    void clearHistory()
    {
        std::memset(_buffer, 0, sizeof(_buffer));
        _bufferIndex = 0;
    }

    size_t taps() const { return _taps; }
    double samplesPerOutput() const { return _samplesPerOutput; }
    const std::vector<double>& coefficients() const { return _coeffs; }

    /// Fractional resampling phase (TTD state for a standalone/master
    /// decimator - a slave's own phase is unused while `_master` is set).
    /// This is a generator-tick-gating accumulator, not just output
    /// buffering: it decides how many input ticks land before the next
    /// output sample, so a caller restoring historical state needs to set
    /// it back explicitly after reset() (which zeroes it along with the
    /// FIR history) rather than leaving it at either zero or a stale value.
    double phase() const { return _phase; }
    void setPhase(double p) { _phase = p; }

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
        _buffer[_bufferIndex + HISTORY] = sample;
        _bufferIndex = (_bufferIndex + 1) % HISTORY;
        if (!_master)
            _phase += 1.0;
    }

    /// Check if enough samples for one output (fractional). A slave
    /// delegates: it outputs exactly when its master does.
    bool hasOutput() const
    {
        return _master ? _master->hasOutput() : _phase >= _samplesPerOutput;
    }

    /// Get output sample (call only when hasOutput() is true), evaluated at
    /// the exact output instant. A slave never touches a phase — it takes
    /// the instant from its master (scaled to its own input rate), whether
    /// the master has already produced this output or not.
    double getOutput()
    {
        double delay;  // input samples back from the newest one
        if (_master)
        {
            const double mp = _master->_phase;
            const double ms = _master->_samplesPerOutput;
            delay = ((mp >= ms) ? mp - ms : mp) * (_samplesPerOutput / ms);
        }
        else
        {
            _phase -= _samplesPerOutput;
            delay = _phase;
        }

        size_t whole = 0;
        double frac = 0.0;
        if (delay > 0.0)
        {
            whole = size_t(delay);
            frac = delay - double(whole);
            if (whole > MAX_DELAY)
            {
                whole = MAX_DELAY;
                frac = 0.0;
            }
        }
        const double position = frac * double(PHASES);
        size_t row = size_t(position);
        double t = position - double(row);
        if (row >= PHASES)
        {
            row = PHASES - 1;
            t = 1.0;
        }

        const size_t length = _table->rowLength;
        const double* a = &_table->rows[row * length];
        const double* b = a + length;
        double ya = 0.0;
        double yb = 0.0;
        // Newest-first taps: the sample just before `start`, then backwards.
        // In the mirrored ring that is one contiguous run ending at
        // start + HISTORY - 1 (length <= MAX_TAPS < HISTORY keeps it in range).
        // Four interleaved accumulators per dot product: independent chains the
        // compiler can pipeline/vectorize (a single running sum is one serial
        // dependency chain under strict FP). Deterministic - the summation
        // order is fixed - but not bit-identical to a single-chain sum
        const size_t start = (_bufferIndex + HISTORY - whole) % HISTORY;
        const double* x = &_buffer[start + HISTORY - 1];
        double ya0 = 0.0, ya1 = 0.0, ya2 = 0.0, ya3 = 0.0;
        double yb0 = 0.0, yb1 = 0.0, yb2 = 0.0, yb3 = 0.0;
        size_t i = 0;
        for (; i + 3 < length; i += 4)
        {
            const double s0 = x[-static_cast<ptrdiff_t>(i)];
            const double s1 = x[-static_cast<ptrdiff_t>(i + 1)];
            const double s2 = x[-static_cast<ptrdiff_t>(i + 2)];
            const double s3 = x[-static_cast<ptrdiff_t>(i + 3)];
            ya0 += s0 * a[i];
            ya1 += s1 * a[i + 1];
            ya2 += s2 * a[i + 2];
            ya3 += s3 * a[i + 3];
            yb0 += s0 * b[i];
            yb1 += s1 * b[i + 1];
            yb2 += s2 * b[i + 2];
            yb3 += s3 * b[i + 3];
        }
        for (; i < length; i++)
        {
            const double s0 = x[-static_cast<ptrdiff_t>(i)];
            ya0 += s0 * a[i];
            yb0 += s0 * b[i];
        }
        ya = (ya0 + ya1) + (ya2 + ya3);
        yb = (yb0 + yb1) + (yb2 + yb3);
        return ya + t * (yb - ya);
    }
};
