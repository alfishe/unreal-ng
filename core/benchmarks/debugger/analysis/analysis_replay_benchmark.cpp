#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "base/featuremanager.h"
#include "debugger/analyzers/analysis/l1model.h"
#include "debugger/analyzers/analysis/replaytracer.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"
#include "loaders/benchmark_path_helper.h"

/// The L1 build of the analysis layers (docs/emulator/analysis/analysis-layers.md, "L1: code behaviour"): a recorded
/// session replayed on an analysis instance with the per-instruction hooks, every instruction folded into L1, then
/// the structures built. Gate (docs/inprogress/2026-10-24-analysis-layers/plan.md, P2): at least 5x real time.
///
///   BM_AnalysisReplayL1/frames:<N>
///
/// Setup (once per process, untimed): Garfield (testdata/loaders/rzx/oracle/garfield-300.z80, 48K) loaded into a
/// muted machine and recorded for N frames by the TTD engine, no history limit, then captured as a TraceBundle. The
/// timed part is ReplayBuild: the analysis instance made from the recording, the replay, L1Builder::Build. Counters:
///   realtime_x       emulated seconds per host second (the gate: >= 5)
///   minstr_per_s     replayed instructions per host second, millions
///   builder_MB       what L1Builder held at the end of the replay (bounded: no trace is kept)
///   routines, loops  what the build found
///
///   core-benchmarks --benchmark_filter=BM_AnalysisReplayL1 --benchmark_repetitions=5
/// Results: docs/inprogress/2026-10-24-analysis-layers/baselines/replay-p2.md
namespace
{
constexpr const char* kSnapshot = "loaders/rzx/oracle/garfield-300.z80";

struct Recording
{
    analysis::TraceBundle bundle;
    double frameSeconds = 0.02;
    std::string error;
};

/// One recording per length, made on first use
const Recording& RecordingOf(uint32_t frames)
{
    static std::map<uint32_t, std::unique_ptr<Recording>> recordings;
    auto& slot = recordings[frames];
    if (slot)
        return *slot;
    slot = std::make_unique<Recording>();
    Recording& r = *slot;

    const std::string snapshot = BenchmarkPathHelper::RequireTestDataFile(kSnapshot);
    auto emulator = std::make_unique<Emulator>("bench-analysis-replay", LoggerLevel::LogNone);
    emulator->SetPreferredModel(MM_SPECTRUM48, 48);
    if (!emulator->Init())
    {
        r.error = "cannot create a 48K machine";
        return r;
    }
    EmulatorContext* context = emulator->GetContext();
    FeatureManager* features = emulator->GetFeatureManager();
    features->setFeature(Features::kSoundGeneration, false);
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    if (context->pSoundManager)
        context->pSoundManager->mute();
    context->pMemory->UpdateFeatureCache();
    if (!emulator->LoadSnapshot(snapshot))
    {
        r.error = "cannot load " + snapshot;
        return r;
    }
    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    ttd->SetHistoryLimit(0, 0);
    if (!ttd->StartRecording())
    {
        r.error = "the recording did not start: " + ttd->LastStartError();
        return r;
    }
    emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
    if (!analysis::TraceBundle::Capture(*emulator, r.bundle, r.error))
        return r;
    if (context->config.frame_duration_us > 0)
        r.frameSeconds = static_cast<double>(context->config.frame_duration_us) / 1e6;
    emulator->Stop();
    emulator->Release();
    return r;
}
}  // namespace

static void BM_AnalysisReplayL1(benchmark::State& state)
{
    const auto frames = static_cast<uint32_t>(state.range(0));
    const Recording& recording = RecordingOf(frames);
    if (!recording.error.empty())
    {
        state.SkipWithError(recording.error.c_str());
        return;
    }

    analysis::L1Result l1;
    double seconds = 0;
    for (auto _ : state)
    {
        analysis::ReplayOptions options;
        options.salt = 1;
        std::string error;
        const auto start = std::chrono::steady_clock::now();
        if (!analysis::ReplayBuild(recording.bundle, options, l1, error))
        {
            state.SkipWithError(error.c_str());
            return;
        }
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        state.SetIterationTime(elapsed);
        seconds += elapsed;
    }
    const double iterations = static_cast<double>(state.iterations());
    const double emulated = static_cast<double>(l1.replay.frames) * recording.frameSeconds;
    const double perBuild = seconds / iterations;
    state.counters["frames"] = static_cast<double>(l1.replay.frames);
    state.counters["realtime_x"] = perBuild > 0 ? emulated / perBuild : 0;
    state.counters["minstr_per_s"] = perBuild > 0 ? static_cast<double>(l1.replay.instructions) / perBuild / 1e6 : 0;
    state.counters["builder_MB"] = static_cast<double>(l1.replay.builderBytes) / (1024.0 * 1024.0);
    state.counters["routines"] = static_cast<double>(l1.routines.size());
    state.counters["loops"] = static_cast<double>(l1.loops.size());
    state.counters["checked"] = static_cast<double>(l1.replay.checked);
    state.counters["diverged"] = static_cast<double>(l1.replay.diverged);
    state.SetLabel("48K garfield");
}

// 1000 frames (20 s) and the 5-minute session of the gate (15000 frames)
BENCHMARK(BM_AnalysisReplayL1)
    ->ArgName("frames")
    ->Arg(1000)
    ->Arg(15000)
    ->UseManualTime()
    ->Iterations(1)
    ->Unit(benchmark::kMillisecond);
