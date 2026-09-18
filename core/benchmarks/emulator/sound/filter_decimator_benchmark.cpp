/// @file filter_decimator_benchmark.cpp
/// @brief Cost of the AY/FM anti-alias decimators per output sample, before and
/// after the exactness-preserving optimizations in FilterDecimator:
///
///   BM_Decimator_Legacy_*      in-file copy of the previous implementation
///                              (modulo ring index, one full convolution per
///                              stream per output, no zero-history shortcut)
///   BM_Decimator_Individual_*  current class, one getOutput() per stream
///   BM_Decimator_Batch_*       current class, getOutputBatch() - one tap pass
///                              with one accumulator per stream (bit-identical)
///   BM_Decimator_Batch_Silent  batch on all-zero history (a muted FM part /
///                              silent AY): the zero-history fast path
///
/// _SSG: the four 96-tap SSG streams (2 chips x L/R) at 218.75 kHz input.
/// _FM:  the two 192-tap FM slaves at 437.5 kHz input.
/// The Arg is the core (output) rate; multiply Time by the outputs_per_frame
/// counter (frame_T / 3.5 MHz x rate) for the per-frame cost, comparable with
/// the "step-sound" figure of the MainLoop frame diagnostics.
///
/// Run with: ./core-benchmarks --benchmark_filter="BM_Decimator.*"

#include <benchmark/benchmark.h>

#include <cstring>
#include <vector>

#include "common/sound/filters/filter_decimator.h"

namespace
{

constexpr double PENTAGON_FRAME_TSTATES = 71680.0;
constexpr double CPU_CLOCK = 3500000.0;

/// The implementation as shipped before the optimizations, kept verbatim so
/// the baseline number is the real old cost, not a guess.
class LegacyDecimator
{
public:
    explicit LegacyDecimator(double outputRate, double inputRate = FilterDecimator::INPUT_RATE)
    {
        FilterDecimator design;
        design.configure(outputRate, FilterDecimator::Quality::Reference, false, inputRate);
        _coeffs = design.coefficients();
        _taps = design.taps();
        _samplesPerOutput = design.samplesPerOutput();
        std::memset(_buffer, 0, sizeof(_buffer));
    }

    void feedSample(double sample)
    {
        _buffer[_bufferIndex] = sample;
        _bufferIndex = (_bufferIndex + 1) % FilterDecimator::MAX_TAPS;
        _phase += 1.0;
    }

    bool hasOutput() const { return _phase >= _samplesPerOutput; }

    double getOutput()
    {
        _phase -= _samplesPerOutput;
        double sum = 0.0;
        size_t idx = _bufferIndex;
        for (size_t i = 0; i < _taps; i++)
        {
            idx = (idx == 0) ? FilterDecimator::MAX_TAPS - 1 : idx - 1;
            sum += _buffer[idx] * _coeffs[i];
        }
        return sum;
    }

private:
    std::vector<double> _coeffs;
    size_t _taps = 0;
    double _samplesPerOutput = 1.0;
    double _buffer[FilterDecimator::MAX_TAPS];
    size_t _bufferIndex = 0;
    double _phase = 0.0;
};

/// Deterministic noise-like generator output, never exactly zero
inline double Signal(size_t stream, size_t tick)
{
    uint64_t x = (tick + 1) * 6364136223846793005ULL + (stream + 1) * 1442695040888963407ULL;
    x ^= x >> 29;
    return static_cast<double>(static_cast<int64_t>(x % 200001) - 100000) / 100000.0 + 1e-3;
}

void SetFrameCounter(benchmark::State& state, double outputRate)
{
    const double outputsPerFrame = PENTAGON_FRAME_TSTATES / CPU_CLOCK * outputRate;
    // Per-frame cost = Time x outputs_per_frame (e.g. 133 ns x 3932 = 0.52 ms)
    state.counters["outputs_per_frame"] = outputsPerFrame;
    state.SetItemsProcessed(state.iterations());
}

template <size_t N>
void RunLegacy(benchmark::State& state, double inputRate, size_t feedsPerTick)
{
    const double rate = static_cast<double>(state.range(0));
    std::vector<LegacyDecimator> d;
    for (size_t k = 0; k < N; k++)
        d.emplace_back(rate, inputRate);

    size_t tick = 0;
    double sink = 0.0;
    for (auto _ : state)
    {
        // Feed until one output is due (~1.14 ticks per output at 192 k)
        while (!d[0].hasOutput())
        {
            for (size_t f = 0; f < feedsPerTick; f++)
                for (size_t k = 0; k < N; k++)
                    d[k].feedSample(Signal(k, tick * feedsPerTick + f));
            tick++;
        }
        for (size_t k = 0; k < N; k++)
            sink += d[k].getOutput();
    }
    benchmark::DoNotOptimize(sink);
    SetFrameCounter(state, rate);
}

template <size_t N>
void RunCurrent(benchmark::State& state, double inputRate, size_t feedsPerTick, bool batch, bool silent)
{
    const double rate = static_cast<double>(state.range(0));
    FilterDecimator d[N];
    FilterDecimator* ptrs[N];
    for (size_t k = 0; k < N; k++)
    {
        d[k].configure(rate, FilterDecimator::Quality::Reference, false, inputRate);
        ptrs[k] = &d[k];
    }

    size_t tick = 0;
    double sink = 0.0;
    double out[N];
    for (auto _ : state)
    {
        while (!d[0].hasOutput())
        {
            for (size_t f = 0; f < feedsPerTick; f++)
                for (size_t k = 0; k < N; k++)
                    d[k].feedSample(silent ? 0.0 : Signal(k, tick * feedsPerTick + f));
            tick++;
        }
        if (batch)
        {
            FilterDecimator::getOutputBatch(ptrs, out);
            for (size_t k = 0; k < N; k++)
                sink += out[k];
        }
        else
        {
            for (size_t k = 0; k < N; k++)
                sink += d[k].getOutput();
        }
    }
    benchmark::DoNotOptimize(sink);
    SetFrameCounter(state, rate);
}

constexpr double SSG_INPUT = FilterDecimator::INPUT_RATE;  // 218.75 kHz, 96 taps
constexpr double FM_INPUT = 437500.0;                       // 437.5 kHz, 192 taps

}  // namespace

// ---- SSG: four 96-tap streams ----
static void BM_Decimator_Legacy_SSG(benchmark::State& s)       { RunLegacy<4>(s, SSG_INPUT, 1); }
static void BM_Decimator_Individual_SSG(benchmark::State& s)   { RunCurrent<4>(s, SSG_INPUT, 1, false, false); }
static void BM_Decimator_Batch_SSG(benchmark::State& s)        { RunCurrent<4>(s, SSG_INPUT, 1, true, false); }
static void BM_Decimator_Batch_Silent_SSG(benchmark::State& s) { RunCurrent<4>(s, SSG_INPUT, 1, true, true); }

// ---- FM: two 192-tap streams fed twice per SSG tick ----
static void BM_Decimator_Legacy_FM(benchmark::State& s)        { RunLegacy<2>(s, FM_INPUT, 2); }
static void BM_Decimator_Individual_FM(benchmark::State& s)    { RunCurrent<2>(s, FM_INPUT, 2, false, false); }
static void BM_Decimator_Batch_FM(benchmark::State& s)         { RunCurrent<2>(s, FM_INPUT, 2, true, false); }
static void BM_Decimator_Batch_Silent_FM(benchmark::State& s)  { RunCurrent<2>(s, FM_INPUT, 2, true, true); }

#define DECIMATOR_RATES ->Arg(44100)->Arg(192000)->Unit(benchmark::kNanosecond)

BENCHMARK(BM_Decimator_Legacy_SSG) DECIMATOR_RATES;
BENCHMARK(BM_Decimator_Individual_SSG) DECIMATOR_RATES;
BENCHMARK(BM_Decimator_Batch_SSG) DECIMATOR_RATES;
BENCHMARK(BM_Decimator_Batch_Silent_SSG) DECIMATOR_RATES;
BENCHMARK(BM_Decimator_Legacy_FM) DECIMATOR_RATES;
BENCHMARK(BM_Decimator_Individual_FM) DECIMATOR_RATES;
BENCHMARK(BM_Decimator_Batch_FM) DECIMATOR_RATES;
BENCHMARK(BM_Decimator_Batch_Silent_FM) DECIMATOR_RATES;
