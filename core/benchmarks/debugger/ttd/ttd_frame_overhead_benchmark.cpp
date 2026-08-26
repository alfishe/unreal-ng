/// @file ttd_frame_overhead_benchmark.cpp
/// @brief Where a frame's time goes with Time-Travel Debug on.
///
/// The set is a ladder, so each step attributes cost to one thing:
///
///   BM_Frame_PureCPU          Z80 cycles only, no frame handlers
///   BM_Frame_NoTTD            full frame, TTD off                  - baseline
///   BM_Frame_DebugModeOnly    + debug memory interface, TTD off
///   BM_Frame_TTD_HooksOnly    + TTD write hooks, no recording session
///   BM_Frame_TTD_Gaming       + active session (capture, no journal)
///   BM_Frame_TTD_Development  + write journal
///   BM_Frame_TTD_Gaming_SoundLQ  as Gaming but with SoundHQ off
///   BM_TTD_OnFrameBoundary    checkpoint capture alone, by dirty-page count
///
/// Means of 3 repetitions on an M-series Mac, Release, action.sna (us/frame):
///
///   PureCPU          422
///   NoTTD            911   baseline
///   DebugModeOnly    969   +58   debug memory interface
///   TTD_HooksOnly    974   +5    dirty tracking on every write
///   TTD_Gaming      1091  +117   per-frame checkpoint capture
///   TTD_Development 1102   +11   write journal (~2280 records/frame)
///   Gaming_SoundLQ   822  -269   HQ sound costs more than all of TTD
///
/// Run-to-run stddev is ~4us, so treat anything under ~10us as noise.
///
/// The shape matters more than the absolutes, which are machine and build
/// specific. TTD's cost is almost entirely the frame-boundary capture: the
/// per-write hooks and the journal are close to free (a 12-byte ring append is
/// ~3.5ns). BM_TTD_OnFrameBoundary shows what drives capture: ~11us fixed plus
/// ~50us for every 16 KB page whose content actually changed - about 330 MB/s
/// through hashing and the codec. Pages that only dedup against the previous
/// checkpoint are nearly free, which is why dirtying one byte per page reports a
/// fraction of the real cost and the sweep writes content instead.
///
/// Dirty tracking is per 16 KB RAM page while the store works in 4 KB
/// sub-pages, so a game touching the 6912-byte screen pushes four times more
/// data through the codec than it changed.
///
/// Run with: ./core-benchmarks --benchmark_filter="BM_Frame.*"

#include <benchmark/benchmark.h>

#include <atomic>

#include "emulator/mainloop.h"  // MainLoopCUT (needs _CODE_UNDER_TEST)
#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_write_journal.h"
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

    /// Debug mode ON, TTD OFF. Isolates the cost TTD inherits rather than causes:
    /// enabling time-travel also raises kDebugMode, which routes every memory
    /// access through the debug path (breakpoint dispatch) so the dirty-page hook
    /// has somewhere to live. Without this baseline the whole difference between
    /// BM_Frame_NoTTD and BM_Frame_TTD_Gaming is attributed to TTD.
    void EnableDebugModeOnly()
    {
        if (ttd)
            ttd->InvalidateSession("benchmark");
        fm->setFeature(Features::kTimeTravel, false);
        fm->setFeature(Features::kDebugMode, true);
        memory->UpdateFeatureCache();
    }

    /// TTD feature ON but no recording session. The per-write hooks in
    /// Memory::MemoryWriteDebug still run (they are gated by the feature cache,
    /// not by session state); RecordMemoryWrite returns immediately because the
    /// state is not Recording, and no checkpoint is ever captured. Isolates the
    /// hot-path cost from the frame-boundary cost.
    void EnableTTDHooksWithoutRecording()
    {
        if (ttd)
            ttd->InvalidateSession("benchmark");
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        memory->UpdateFeatureCache();
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

/// Second baseline: debug memory path only, no TTD. The gap to BM_Frame_NoTTD is
/// the price of kDebugMode; the gap from here to BM_Frame_TTD_Gaming is what TTD
/// itself adds on top (dirty-page tracking plus the frame-boundary checkpoint).
BENCHMARK_DEFINE_F(FrameBenchmarkFixture, BM_Frame_DebugModeOnly)(benchmark::State& state)
{
    if (!mainloop)
    {
        state.SkipWithError("MainLoop not initialized");
        return;
    }

    EnableDebugModeOnly();

    for (auto _ : state)
    {
        mainloop->RunFramePublic();
    }

    DisableTTD();
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(FrameBenchmarkFixture, BM_Frame_DebugModeOnly)
    ->Iterations(1000)
    ->Unit(benchmark::kMicrosecond);

/// Third baseline: TTD write hooks live, no recording session. The gap from
/// BM_Frame_DebugModeOnly is what the per-write TTD work costs; the gap from here
/// to BM_Frame_TTD_Gaming is what an active session adds (capture + journal).
BENCHMARK_DEFINE_F(FrameBenchmarkFixture, BM_Frame_TTD_HooksOnly)(benchmark::State& state)
{
    if (!mainloop)
    {
        state.SkipWithError("MainLoop not initialized");
        return;
    }

    EnableTTDHooksWithoutRecording();

    for (auto _ : state)
    {
        mainloop->RunFramePublic();
    }

    DisableTTD();
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(FrameBenchmarkFixture, BM_Frame_TTD_HooksOnly)
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
    // Evidence that the write path is live: a journal that stays empty means the
    // debug memory interface is not installed and these numbers measure nothing.
    if (const auto* j = ttd->GetWriteJournal())
        state.counters["journal_records"] = static_cast<double>(j->Size());

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
    if (!ttd || !mainloop || !memory)
    {
        state.SkipWithError("TTD not available");
        return;
    }

    const int pagesToDirty = static_cast<int>(state.range(0));
    uint8_t seed = 0;

    EnableTTD(true);
    mainloop->RunFramePublic();

    // Capture cost scales with the number of pages changed since the previous
    // checkpoint, so the dirty set has to be produced deliberately.
    //
    // Driving it with RunFramePublic() does not work: RunFrame captures at frame
    // end itself, so the explicit OnFrameBoundary() that followed always saw an
    // already-cleared bitmap and timed the empty path (the giveaway was
    // checkpoints == 2 x iterations). One write per page is enough - the dirty
    // bitmap is per page, not per byte.
    for (auto _ : state)
    {
        state.PauseTiming();
        for (int page = 0; page < pagesToDirty; ++page)
        {
            // Bank 1 (0x4000..0x7FFF) is RAM on every model; walk pages through it.
            memory->SetRAMPageToBank1(static_cast<uint16_t>(page));

            // Write CONTENT, not just a byte. Capture cost is dominated by
            // hashing and compressing the sub-pages whose content actually
            // changed - a single changed byte leaves the other sub-pages
            // identical, they dedup against the previous checkpoint and the
            // benchmark reports a fraction of the real cost.
            for (uint16_t off = 0; off < 0x4000; off += 64)
                memory->MemoryWriteDebug(static_cast<uint16_t>(0x4000 + off),
                                         static_cast<uint8_t>(seed + off + page));
        }
        ++seed;
        state.ResumeTiming();

        ttd->OnFrameBoundary();
    }

    const auto info = ttd->GetSessionInfo();
    state.counters["checkpoints"] = static_cast<double>(info.checkpointCount);
    state.counters["heap_kb"] = static_cast<double>(info.sessionHeapBytes) / 1024.0;
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(FrameBenchmarkFixture, BM_TTD_OnFrameBoundary)
    ->Arg(0)
    ->Arg(1)
    ->Arg(4)
    ->Arg(16)
    ->Arg(64)
    ->Iterations(500)
    ->Unit(benchmark::kMicrosecond);
