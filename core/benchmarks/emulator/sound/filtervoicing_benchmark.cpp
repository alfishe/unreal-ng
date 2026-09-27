#include <cmath>
#include <cstdint>
#include <vector>

#include "benchmark/benchmark.h"
#include "common/sound/filters/filtervoicing.h"
#include "common/sound/filters/voicingstage.h"

// AY tone voicing cost per chip buffer (one 20 ms frame of stereo int16).
// Runs twice per frame (two TurboSound chips) in HQ and LQ alike, so it must
// stay noise next to the AY generators. Arg = core rate in Hz.

namespace
{
std::vector<int16_t> BassFrame(size_t frames, double rate)
{
    std::vector<int16_t> buffer(frames * 2);
    for (size_t n = 0; n < frames; n++)
    {
        const int16_t v = std::fmod(static_cast<double>(n) * 110.0 / rate, 1.0) < 0.5 ? 8192 : -8192;
        buffer[n * 2] = v;
        buffer[n * 2 + 1] = v;
    }
    return buffer;
}
}  // namespace

/// Classic profile, one frame (steady state, no switch)
static void BM_FilterVoicing_ClassicFrame(benchmark::State& state)
{
    const double rate = static_cast<double>(state.range(0));
    const size_t frames = static_cast<size_t>(rate / 50.0);
    const std::vector<int16_t> source = BassFrame(frames, rate);
    std::vector<int16_t> buffer = source;
    FilterVoicing filter(rate, FilterVoicing::Preset::Classic);

    for (auto _ : state)
    {
        buffer = source;
        filter.processInt16(buffer.data(), frames);
        benchmark::DoNotOptimize(buffer.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(frames));
}
BENCHMARK(BM_FilterVoicing_ClassicFrame)->Arg(48000)->Arg(192000);

/// VoicingStage steady state: history copy + Classic filtering (the per-frame SoundManager cost)
static void BM_VoicingStage_SteadyFrame(benchmark::State& state)
{
    const double rate = static_cast<double>(state.range(0));
    const size_t frames = static_cast<size_t>(rate / 50.0);
    const std::vector<int16_t> source = BassFrame(frames, rate);
    std::vector<int16_t> buffer = source;
    VoicingStage stage(frames);
    stage.setup(rate);
    stage.setPresetImmediate(FilterVoicing::Preset::Classic);

    for (auto _ : state)
    {
        buffer = source;
        stage.process(buffer.data(), frames);
        benchmark::DoNotOptimize(buffer.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(frames));
}
BENCHMARK(BM_VoicingStage_SteadyFrame)->Arg(48000)->Arg(192000);

/// VoicingStage profile switch frame: two-frame pre-roll + crossfade (only on a user change)
static void BM_VoicingStage_SwitchFrame(benchmark::State& state)
{
    const double rate = static_cast<double>(state.range(0));
    const size_t frames = static_cast<size_t>(rate / 50.0);
    const std::vector<int16_t> source = BassFrame(frames, rate);
    std::vector<int16_t> buffer = source;
    VoicingStage stage(frames);
    stage.setup(rate);
    stage.setPresetImmediate(FilterVoicing::Preset::Flat);
    for (int i = 0; i < 2; i++)
    {
        buffer = source;
        stage.process(buffer.data(), frames);
    }

    bool classic = true;
    for (auto _ : state)
    {
        buffer = source;
        stage.request(classic ? FilterVoicing::Preset::Classic : FilterVoicing::Preset::Flat);
        classic = !classic;
        stage.process(buffer.data(), frames);
        benchmark::DoNotOptimize(buffer.data());
    }
}
BENCHMARK(BM_VoicingStage_SwitchFrame)->Arg(48000)->Arg(192000);
