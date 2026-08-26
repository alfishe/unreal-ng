/// @file ttd_coverage_benchmark.cpp
/// @brief Footprint and rebuild cost of the reverse-search coverage index.
///
/// The query-side win is measured in ttd_reverse_benchmark.cpp
/// (BM_TTD_FindLastAccess_Coverage). What this file answers is the other half
/// of the trade: what the index costs to hold and what it would cost to
/// reconstruct if it were not persisted.
///
/// Both numbers drive the persistence design — see
/// docs/emulator/design/debugger/time-travel-debug/ttd-container-format.md.
///
/// These are reported as benchmark counters rather than timings, because the
/// quantity of interest is bytes per emulated frame, not wall time. The one
/// genuine timing here is the record pass, which doubles as the upper bound on
/// rebuild-by-replay.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_coverage_index.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{

constexpr int64_t kWorkloadRomStartup = 0;
constexpr int64_t kWorkloadSnapshot   = 1;

/// Snapshot used for the "real program" workload. Tracked in the repository so
/// the benchmark runs from a fresh checkout.
constexpr const char* kSnapshotPath = "testdata/loaders/sna/Dizzy Y.sna";

struct CoverageRun
{
    Emulator* emulator = nullptr;
    ttd::TimeTravelManager* ttd = nullptr;
    bool ok = false;

    explicit CoverageRun(bool loadSnapshot)
    {
        emulator = new Emulator(LoggerLevel::LogError);
        if (!emulator->Init())
            return;

        EmulatorContext* ctx = emulator->GetContext();
        ttd = ctx->pTimeTravelManager;
        FeatureManager* fm = emulator->GetFeatureManager();
        if (!ttd || !fm)
            return;

        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        ctx->pMemory->UpdateFeatureCache();

        if (loadSnapshot && !emulator->LoadSnapshot(kSnapshotPath))
            return;  // Missing corpus — caller skips.

        ok = true;
    }

    ~CoverageRun()
    {
        if (emulator)
        {
            emulator->Stop();
            emulator->Release();
            delete emulator;
        }
    }
};

}  // namespace

// ===========================================================================
// Index footprint per emulated frame
// ===========================================================================
//
// Reports compressed and uncompressed bytes per frame for each coverage kind,
// plus the projected hourly volume. Two workloads, because they bracket the
// range: ROM startup touches thousands of addresses per frame but touches the
// same ones every frame (huge volume, enormous compression ratio), while a real
// program touches fewer addresses less repetitively.

static void BM_TTD_CoverageIndex_Footprint(benchmark::State& state)
{
    const bool useSnapshot = state.range(0) == kWorkloadSnapshot;
    const uint32_t frames = static_cast<uint32_t>(state.range(1));

    for (auto _ : state)
    {
        state.PauseTiming();
        CoverageRun run(useSnapshot);
        if (!run.ok)
        {
            state.SkipWithError("fixture init failed (missing snapshot?)");
            return;
        }
        state.ResumeTiming();

        // Timed section: the whole record pass. This is also the cost of
        // rebuilding an index by replaying a session, which is what a reader
        // would have to do for a file that carries no coverage section.
        run.ttd->StartRecording();
        run.emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
        run.ttd->StopRecording();

        state.PauseTiming();

        const ttd::TTDCoverageIndex& idx = run.ttd->GetCoverageIndex();

        size_t compressed = 0;
        size_t raw = 0;
        for (ttd::TTDCoverageKind kind : {ttd::TTDCoverageKind::Executed,
                                          ttd::TTDCoverageKind::Written,
                                          ttd::TTDCoverageKind::Read})
        {
            compressed += idx.EncodedBytes(kind);
            raw += idx.RawEncodedBytes(kind);
        }

        state.counters["exec_B_frame"]  =
            double(idx.EncodedBytes(ttd::TTDCoverageKind::Executed)) / frames;
        state.counters["write_B_frame"] =
            double(idx.EncodedBytes(ttd::TTDCoverageKind::Written)) / frames;
        state.counters["read_B_frame"]  =
            double(idx.EncodedBytes(ttd::TTDCoverageKind::Read)) / frames;
        state.counters["total_B_frame"] = double(compressed) / frames;
        state.counters["zstd_ratio"]    = compressed ? double(raw) / double(compressed) : 0.0;
        state.counters["MB_per_hour"]   =
            (double(compressed) / frames) * 50.0 * 3600.0 / (1024.0 * 1024.0);
        state.counters["fixed_heap_KB"] = double(idx.HeapBytes()) / 1024.0;

        state.ResumeTiming();
    }

    state.SetLabel(std::string(useSnapshot ? "snapshot" : "rom-startup") +
                   " frames=" + std::to_string(frames));
}
BENCHMARK(BM_TTD_CoverageIndex_Footprint)
    ->Args({kWorkloadRomStartup, 300})
    ->Args({kWorkloadSnapshot, 300})
    ->Iterations(3)
    ->Unit(benchmark::kMillisecond);

// ===========================================================================
// Rebuild-by-replay, expressed per frame
// ===========================================================================
//
// A session loaded from a file that has no coverage section can only get one by
// replaying itself. The per-frame cost below multiplied by 50 x seconds gives
// the wait: at ~1 ms/frame a ten-minute session takes roughly half a minute
// before the first reverse search can be accelerated.

static void BM_TTD_CoverageIndex_RebuildPerFrame(benchmark::State& state)
{
    const uint32_t frames = static_cast<uint32_t>(state.range(0));

    for (auto _ : state)
    {
        state.PauseTiming();
        CoverageRun run(/*loadSnapshot=*/true);
        if (!run.ok)
        {
            state.SkipWithError("fixture init failed (missing snapshot?)");
            return;
        }
        run.ttd->StartRecording();
        state.ResumeTiming();

        run.emulator->RunNFrames(frames, /*skipBreakpoints=*/true);

        state.PauseTiming();
        run.ttd->StopRecording();
        state.ResumeTiming();
    }

    state.counters["frames"] = frames;
    state.counters["ms_per_frame"] = benchmark::Counter(
        frames, benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
    state.SetLabel("replay rebuild upper bound");
}
BENCHMARK(BM_TTD_CoverageIndex_RebuildPerFrame)
    ->Arg(300)
    ->Iterations(3)
    ->Unit(benchmark::kMillisecond);

// ===========================================================================
// Loaded sessions: does the persisted index actually do anything?
// ===========================================================================
//
// The index is written into the .ttd so a loaded recording searches at live
// speed instead of falling back to replay. That claim is only worth making if
// the loaded index is genuinely consulted, so these load a real fixture from
// disk and run the same query with the index enabled and disabled.
//
// Args({operation, coverageEnabled}) where operation 0 = FindLastAccess (read
// watchpoint), 1 = ReverseContinue (reverse breakpoint).

#include <fstream>

#include "debugger/ttd/ttd_probe.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"

namespace
{

constexpr const char* kSessionFixture = "testdata/ttd/demo_across-the-edge-second.ttd";

/// Load a recorded session into a fresh emulator. Returns null when the
/// fixture is missing, so the benchmark skips rather than reporting nonsense.
struct LoadedSession
{
    Emulator* emulator = nullptr;
    ttd::TimeTravelManager* ttd = nullptr;
    bool ok = false;

    LoadedSession()
    {
        emulator = new Emulator(LoggerLevel::LogError);
        if (!emulator->Init())
            return;

        EmulatorContext* ctx = emulator->GetContext();
        ttd = ctx->pTimeTravelManager;
        FeatureManager* fm = emulator->GetFeatureManager();
        if (!ttd || !fm)
            return;

        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        ctx->pMemory->UpdateFeatureCache();

        std::ifstream in(kSessionFixture, std::ios::binary);
        if (!in)
            return;

        std::string err;
        if (!ttd->DeserializeSession(in, err))
            return;

        ok = ttd->GetCheckpointCount() > 1;
    }

    ~LoadedSession()
    {
        if (emulator)
        {
            emulator->Stop();
            emulator->Release();
            delete emulator;
        }
    }
};

}  // namespace

static void BM_TTD_LoadedSession_Query(benchmark::State& state)
{
    const bool isReverseContinue = state.range(0) != 0;
    const bool coverageOn = state.range(1) != 0;

    LoadedSession session;
    if (!session.ok)
    {
        state.SkipWithError("could not load testdata/ttd fixture "
                            "(run record_fixtures.py, and run from the repo root)");
        return;
    }

    // Report whether the file actually carried an index, so a silently absent
    // section cannot masquerade as "the index does not help".
    const size_t indexedFrames =
        session.ttd->GetCoverageIndex().SealedFrameCount(ttd::TTDCoverageKind::Executed);
    state.counters["indexed_frames"] = double(indexedFrames);

    session.ttd->SetEnableCoverageIndex(coverageOn);

    const ttd::TTDTimePoint end = session.ttd->SessionEndPosition();

    // A target the workload does not touch, so the query has to consider the
    // whole history rather than stopping at the first frame.
    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = 0xFFFF;
    q.access = ttd::TTDAccessType::Read;

    for (auto _ : state)
    {
        state.PauseTiming();
        session.ttd->SeekTo(end);
        state.ResumeTiming();

        if (isReverseContinue)
            benchmark::DoNotOptimize(session.ttd->ReverseContinue({uint16_t(0xFFFF)}));
        else
            benchmark::DoNotOptimize(session.ttd->FindLastAccess(q));
    }

    state.SetLabel(std::string(isReverseContinue ? "reverse-continue" : "find-last") +
                   (coverageOn ? " +index" : " no-index"));
}
BENCHMARK(BM_TTD_LoadedSession_Query)
    ->Args({0, 0})->Args({0, 1})
    ->Args({1, 0})->Args({1, 1})
    ->Iterations(5)
    ->Unit(benchmark::kMillisecond);
