#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <vector>

#include "decimatordot.h"
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
    // The design's parameters (setInputRate redesigns with the others kept)
    double _outputRate = 44100.0;
    double _inputRate = INPUT_RATE;
    Quality _quality = Quality::Reference;
    bool _extendedBandwidth = false;

    /// Slave-mode master (§6.3): when set, this decimator keeps no phase of
    /// its own and produces output exactly when the master does. nullptr =
    /// standalone. Cleared by configure() — re-attach after a redesign.
    const FilterDecimator* _master = nullptr;

    // History ring, newest first and mirrored: a sample is written below the
    // previous one (the index counts down) and stored at i and at i + HISTORY,
    // so the taps of getOutput() - newest first - read one contiguous run
    // forwards from any start without a wrap check per tap (the same values in
    // the same order, bit-identical output). Forwards: the SIMD kernels load
    // two taps per register in tap order (DecimatorDot)
    double _buffer[2 * HISTORY];
    size_t _bufferIndex;   // the newest sample
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

    /// The anti-alias FIR and the phase step (configure / setInputRate)
    void design(double outputRate, Quality quality, bool extendedBandwidth, double inputRate)
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
        _outputRate = outputRate;
        _inputRate = inputRate;
        _quality = quality;
        _extendedBandwidth = extendedBandwidth;
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
        design(outputRate, quality, extendedBandwidth, inputRate);
        _master = nullptr;  // a redesigned filter is its own clock again
        reset();
    }

    /// Change the input (generator) rate in flight - a switched AY clock
    /// (SoundChip_TurboSound::SetPsgClock): the filter is redesigned for the
    /// new rate (the same cutoff in Hz), while the history and the resampling
    /// phase stay, so the output carries on without a gap or a click. The
    /// phase needs no conversion: it counts input samples since the last
    /// output instant, and the next output now falls samplesPerOutput() of
    /// the new rate after it. The history keeps the samples taken at the old
    /// rate - a transient of one filter length, inaudible
    void setInputRate(double inputRate)
    {
        if (inputRate == _inputRate)
            return;
        design(_outputRate, _quality, _extendedBandwidth, inputRate);
    }

    double inputRate() const { return _inputRate; }

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
    /// Input samples one output can read: the FIR's row (taps + 1, the fractional shift) behind the output instant,
    /// which lies up to MAX_DELAY whole samples back. When the newest window() samples fed are all zero, getOutput()
    /// is exactly +0.0 (every product is a signed zero, every sum of them +0.0)
    size_t window() const { return _taps + 1 + MAX_DELAY; }
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
        _bufferIndex = (_bufferIndex == 0) ? HISTORY - 1 : _bufferIndex - 1;
        _buffer[_bufferIndex] = sample;
        _buffer[_bufferIndex + HISTORY] = sample;
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
        const Instant in = takeInstant();
        const double* x = &_buffer[_bufferIndex + in.whole];
        double ya;
        double yb;
        DecimatorDot::Rows(&x, 1, in.a, in.b, _table->rowLength, &ya, &yb);
        return ya + in.t * (yb - ya);
    }

    /// getOutput() of `count` decimators at one output instant, out[k] for decimators[k] - the same bits as calling
    /// getOutput() on each in this order. Decimators whose instant falls on the same pair of coefficient rows (one
    /// design, the same phase: the slaves of one master at its rate, standalone decimators fed in lockstep) share
    /// one pass over the taps (DecimatorDot: the coefficient loads, independent accumulator chains). Neighbours in
    /// the list group, so list such decimators next to each other
    static void getOutputs(FilterDecimator* const* decimators, size_t count, double* out)
    {
        constexpr size_t kChunk = 8;
        Instant in[kChunk];
        const double* x[kChunk];
        double ya[kChunk];
        double yb[kChunk];
        for (size_t base = 0; base < count; base += kChunk)
        {
            const size_t n = std::min(kChunk, count - base);
            for (size_t k = 0; k < n; k++)
            {
                FilterDecimator& d = *decimators[base + k];
                in[k] = d.takeInstant();
                x[k] = &d._buffer[d._bufferIndex + in[k].whole];
            }
            for (size_t first = 0; first < n;)
            {
                size_t last = first + 1;
                while (last < n && in[last].a == in[first].a && in[last].t == in[first].t)
                    last++;
                DecimatorDot::Rows(&x[first], last - first, in[first].a, in[first].b,
                                   decimators[base + first]->_table->rowLength, &ya[first], &yb[first]);
                for (size_t k = first; k < last; k++)
                    out[base + k] = ya[k] + in[k].t * (yb[k] - ya[k]);
                first = last;
            }
        }
    }

private:
    /// Where an output falls: the two coefficient rows around the instant, the weight between them and how many
    /// whole input samples back from the newest one it lies
    struct Instant
    {
        const double* a;
        const double* b;
        double t;
        size_t whole;
    };

    /// The output instant of the getOutput() due now (a standalone or master decimator consumes its phase)
    Instant takeInstant()
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

        // Taps newest first from the output instant: the run starting `whole` samples behind the newest one
        // (length <= MAX_TAPS + 1 and whole <= MAX_DELAY keep it inside the mirrored ring)
        const size_t length = _table->rowLength;
        const double* a = &_table->rows[row * length];
        return Instant{a, a + length, t, whole};
    }
};
