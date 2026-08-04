/// @file ttd_frame_overhead_benchmark.cpp
/// @brief Measures per-frame execution time in different TTD modes.
///
/// Benchmarks pure MainLoop::RunFrame() execution (no sync, no frame limiting):
///   1. BM_Frame_NoTTD — Pure frame (no TTD) — baseline for emulator core
///   2. BM_Frame_TTD_Gaming — TTD enabled, write journal disabled (rewind capability)
///   3. BM_Frame_TTD_Development — TTD enabled, write journal enabled (full replay)
///   4. BM_Frame_TTD_Gaming_SoundLQ — TTD Gaming with SoundHQ disabled
///   5. BM_Frame_TTD_Gaming_Batch — Parameterized test for audio batch intervals
///   6. BM_Frame_PureCPU — Z80 cycle execution only (no frame handlers)
///   7. BM_TTD_OnFrameBoundary — TTD checkpoint capture overhead alone
///
/// Performance targets (2026-08-04 optimization round):
///   - BM_Frame_NoTTD: ~400µs (2,500 fps capacity)
///   - BM_Frame_TTD_Gaming: ~480µs (2,080 fps capacity)
///   - TTD overhead: <100µs per frame (~20% of pure frame time)
///
/// Run with: ./core-benchmarks --benchmark_filter="BM_Frame.*"

#include <benchmark/benchmark.h>

#include "../../tests/_helpers/mainloop_cut.h"
#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{

// Path to action.sna for realistic workload
static const char* ACTION_SNA = "testdata/loaders/sna/action.sna";

class FrameBenchmarkFixture : public benchmark::Fixture
{
public:
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;
    MainLoopCUT* mainloop = nullptr;
    ttd::TimeTravelManager* ttd = nullptr;
    Memory* memory = nullptr;
    FeatureManager* fm = nullptr;

    void SetUp(const benchmark::State&) override
    {
        emulator = new Emulator(LoggerLevel::LogError);
        if (!emulator->Init())
        {
            delete emulator;
            emulator = nullptr;
            return;
        }

        context = emulator->GetContext();
        ttd = context->pTimeTravelManager;
        memory = context->pMemory;
        fm = emulator->GetFeatureManager();

        // Load action.sna for realistic workload
        emulator->LoadSnapshot(ACTION_SNA);

        // Create CUT wrapper for MainLoop to access RunFrame directly
        mainloop = new MainLoopCUT(context);
    }

    void TearDown(const benchmark::State&) override
    {
        delete mainloop;
        mainloop = nullptr;

        if (emulator)
        {
            emulator->Stop();
            emulator->Release();
            delete emulator;
            emulator = nullptr;
        }
    }

    void EnableTTD(bool enableJournal)
    {
        if (ttd)
        {
            ttd->SetEnableWriteJournal(enableJournal);
            fm->setFeature(Features::kDebugMode, true);
            fm->setFeature(Features::kTimeTravel, true);
            memory->UpdateFeatureCache();
            ttd->StartRecording();
        }
    }

    void DisableTTD()
    {
        if (ttd)
        {
            ttd->InvalidateSession("benchmark");
            fm->setFeature(Features::kDebugMode, false);
            fm->setFeature(Features::kTimeTravel, false);
            memory->UpdateFeatureCache();
        }
    }

    void SetSoundHQ(bool enabled)
    {
        fm->setFeature(Features::kSoundHQ, enabled);
    }
};

} // namespace

/// Baseline: pure frame execution without TTD (no sync, no limiting)
BENCHMARK_DEFINE_F(FrameBenchmarkFixture, BM_Frame_NoTTD)(benchmark::State& state)
{
    if (!mainloop)
    {
        state.SkipWithError("MainLoop not initialized");
        return;
    }

    DisableTTD();

    for (auto _ : state)
    {
        mainloop->RunFramePublic();
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(FrameBenchmarkFixture, BM_Frame_NoTTD)
    ->Iterations(1000)
    ->Unit(benchmark::kMicrosecond);

/// TTD Gaming mode: TTD enabled, write journal disabled
BENCHMARK_DEFINE_F(FrameBenchmarkFixture, BM_Frame_TTD_Gaming)(benchmark::State& state)
{
    if (!mainloop || !ttd)
    {
        state.SkipWithError("Not initialized");
        return;
    }

    EnableTTD(false);

    for (auto _ : state)
    {
        mainloop->RunFramePublic();
    }

    auto info = ttd->GetSessionInfo();
    state.counters["checkpoints"] = static_cast<double>(info.checkpointCount);
    state.counters["heap_kb"] = static_cast<double>(info.sessionHeapBytes) / 1024.0;
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(FrameBenchmarkFixture, BM_Frame_TTD_Gaming)
    ->Iterations(1000)
    ->Unit(benchmark::kMicrosecond);

/// TTD Development mode: TTD enabled, write journal enabled
BENCHMARK_DEFINE_F(FrameBenchmarkFixture, BM_Frame_TTD_Development)(benchmark::State& state)
{
    if (!mainloop || !ttd)
    {
        state.SkipWithError("Not initialized");
        return;
    }

    EnableTTD(true);

    for (auto _ : state)
    {
        mainloop->RunFramePublic();
    }

    auto info = ttd->GetSessionInfo();
    state.counters["checkpoints"] = static_cast<double>(info.checkpointCount);
    state.counters["heap_kb"] = static_cast<double>(info.sessionHeapBytes) / 1024.0;
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(FrameBenchmarkFixture, BM_Frame_TTD_Development)
    ->Iterations(1000)
    ->Unit(benchmark::kMicrosecond);

/// TTD Gaming mode with SoundHQ OFF: measure impact of sound quality setting
BENCHMARK_DEFINE_F(FrameBenchmarkFixture, BM_Frame_TTD_Gaming_SoundLQ)(benchmark::State& state)
{
    if (!mainloop || !ttd)
    {
        state.SkipWithError("Not initialized");
        return;
    }

    SetSoundHQ(false);
    EnableTTD(false);

    for (auto _ : state)
    {
        mainloop->RunFramePublic();
    }

    auto info = ttd->GetSessionInfo();
    state.counters["checkpoints"] = static_cast<double>(info.checkpointCount);
    state.counters["heap_kb"] = static_cast<double>(info.sessionHeapBytes) / 1024.0;
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(FrameBenchmarkFixture, BM_Frame_TTD_Gaming_SoundLQ)
    ->Iterations(1000)
    ->Unit(benchmark::kMicrosecond);

/// TTD Gaming with sound batch interval (parameterized)
BENCHMARK_DEFINE_F(FrameBenchmarkFixture, BM_Frame_TTD_Gaming_Batch)(benchmark::State& state)
{
    if (!mainloop || !ttd)
    {
        state.SkipWithError("Not initialized");
        return;
    }

    uint32_t batchInterval = static_cast<uint32_t>(state.range(0));
    context->pSoundManager->setBatchInterval(batchInterval);
    EnableTTD(false);

    for (auto _ : state)
    {
        mainloop->RunFramePublic();
    }

    // Reset batch interval
    context->pSoundManager->setBatchInterval(0);

    auto info = ttd->GetSessionInfo();
    state.counters["checkpoints"] = static_cast<double>(info.checkpointCount);
    state.counters["batch_interval"] = static_cast<double>(batchInterval);
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(FrameBenchmarkFixture, BM_Frame_TTD_Gaming_Batch)
    ->Arg(0)    // No batching (baseline)
    ->Arg(40)   // 40 t-states
    ->Arg(80)   // 80 t-states (1 audio sample)
    ->Arg(160)  // 160 t-states (2 audio samples)
    ->Iterations(1000)
    ->Unit(benchmark::kMicrosecond);

/// Pure CPU cycle execution (no frame handlers)
BENCHMARK_DEFINE_F(FrameBenchmarkFixture, BM_Frame_PureCPU)(benchmark::State& state)
{
    if (!mainloop)
    {
        state.SkipWithError("MainLoop not initialized");
        return;
    }

    DisableTTD();

    for (auto _ : state)
    {
        mainloop->ExecuteCPUFrameCyclePublic();
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(FrameBenchmarkFixture, BM_Frame_PureCPU)
    ->Iterations(1000)
    ->Unit(benchmark::kMicrosecond);

/// OnFrameBoundary overhead alone
BENCHMARK_DEFINE_F(FrameBenchmarkFixture, BM_TTD_OnFrameBoundary)(benchmark::State& state)
{
    if (!ttd || !mainloop)
    {
        state.SkipWithError("TTD not available");
        return;
    }

    EnableTTD(true);
    mainloop->RunFramePublic();

    for (auto _ : state)
    {
        ttd->OnFrameBoundary();
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(FrameBenchmarkFixture, BM_TTD_OnFrameBoundary)
    ->Iterations(1000)
    ->Unit(benchmark::kMicrosecond);
