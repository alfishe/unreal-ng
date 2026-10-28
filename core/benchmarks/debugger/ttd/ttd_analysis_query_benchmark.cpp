/// @file ttd_analysis_query_benchmark.cpp
/// @brief Query latency of the analysis queries on the engine (docs/inprogress/2026-10-28-ttd-analysis-feedback):
/// each new one-call query against the loop of old calls a client ran for the same answer.
///
/// One session, recorded once: 3,000 frames of a Pentagon program (the one in ttdanalysisqueries_test.cpp) that
/// stores a new value to #8100 every pass, flips #8103 every 16 passes and every 64 passes (about every third
/// frame) swaps page 1 / page 3 into bank 3 and calls #C000 there. Two copies: with the write journal and without.
///
/// TTD_ANALYSIS_BASELINE (defined when this file is built against the code before the change) keeps only the
/// benchmarks the old API can run: the coverage scan and the client loops.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

namespace
{
constexpr unsigned kFrames = 3000;

struct Session
{
    std::shared_ptr<Emulator> emulator;
    EmulatorContext* context = nullptr;
    ttd::TimeTravelController* ttd = nullptr;

    explicit Session(bool writeJournal)
    {
        emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("bench-ttd-analysis", "PENTAGON",
                                                                           LoggerLevel::LogNone);
        if (!emulator)
            return;
        context = emulator->GetContext();
        context->pFeatureManager->setFeature(Features::kDebugMode, true);
        context->pFeatureManager->setFeature(Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();
        emulator->EnableTurboMode();
        ttd = context->pTimeTravelController;
        ttd->SetEnableWriteJournal(writeJournal);

        const std::vector<uint8_t> loop = {0xF3, 0x0E, 0x00, 0x3E, 0x11, 0x32, 0xF0, 0x80, 0x06, 0x00, 0x10, 0xFE,
                                           0x0C, 0x79, 0x32, 0x00, 0x81, 0x3E, 0x05, 0x32, 0x01, 0x81, 0x79, 0x0F,
                                           0x0F, 0x0F, 0x0F, 0xE6, 0x01, 0x32, 0x03, 0x81, 0x3A, 0x00, 0x00, 0x79,
                                           0xE6, 0x3F, 0xCC, 0x00, 0x82, 0x18, 0xDD};
        const std::vector<uint8_t> swap = {0xC5, 0x3A, 0xF0, 0x80, 0xEE, 0x02, 0x32, 0xF0, 0x80, 0x01,
                                           0xFD, 0x7F, 0xED, 0x79, 0xCD, 0x00, 0xC0, 0xC1, 0xC9};
        Memory* memory = context->pMemory;
        for (size_t i = 0; i < loop.size(); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), loop[i]);
        for (size_t i = 0; i < swap.size(); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8200 + i), swap[i]);
        memory->RAMPageAddress(1)[0] = 0xC9;
        memory->RAMPageAddress(3)[0] = 0xC9;
        context->pCore->GetZ80()->pc = 0x8000;
        context->pCore->GetZ80()->sp = 0xBF00;

        ttd->StartRecording();
        emulator->RunNFrames(kFrames, /*skipBreakpoints=*/true);
        ttd->StopRecording();
        ttd->SeekTo(ttd->SessionEndPosition());
    }
    ~Session()
    {
        if (emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
    }
    bool Ok() const { return ttd && ttd->GetCheckpointCount() > 1; }
};

/// Recorded on first use and kept to the process end (no teardown against the manager's static lifetime)
Session& Get(bool writeJournal)
{
    static Session* withJournal = nullptr;
    static Session* withoutJournal = nullptr;
    Session*& s = writeJournal ? withJournal : withoutJournal;
    if (!s)
        s = new Session(writeJournal);
    return *s;
}

ttd::TTDSearchQuery Writes(uint16_t addr)
{
    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = addr;
    q.access = ttd::TTDAccessType::Write;
    return q;
}

// ---------------------------------------------------------------------------
// coverage-scan: 3,000 frames, the routine's ~1,000 frames listed (the block lookup is the change)
// ---------------------------------------------------------------------------
void BM_Analysis_CoverageScan_Oldest(benchmark::State& state)
{
    Session& s = Get(false);
    if (!s.Ok())
        return state.SkipWithError("no session");
    size_t matches = 0;
    for (auto _ : state)
    {
        const ttd::TTDCoverageScanResult r =
            s.ttd->QueryCoverageScan(0, UINT64_MAX, ttd::TTDCoverageKind::Executed, 0x8200, 0x8200, ttd::PhysPage(2), 10000);
        matches = r.frames.size();
        benchmark::DoNotOptimize(r.lastMatch);
    }
    state.counters["frames_listed"] = static_cast<double>(matches);
}
BENCHMARK(BM_Analysis_CoverageScan_Oldest)->Unit(benchmark::kMicrosecond);

#ifndef TTD_ANALYSIS_BASELINE
void BM_Analysis_CoverageScan_NewestRanges(benchmark::State& state)
{
    Session& s = Get(false);
    if (!s.Ok())
        return state.SkipWithError("no session");
    for (auto _ : state)
    {
        const ttd::TTDCoverageScanResult r = s.ttd->QueryCoverageScan(
            0, UINT64_MAX, ttd::TTDCoverageKind::Executed, 0x8200, 0x8200, ttd::PhysPage(2), 10000, true, true);
        benchmark::DoNotOptimize(r.ranges.data());
    }
}
BENCHMARK(BM_Analysis_CoverageScan_NewestRanges)->Unit(benchmark::kMicrosecond);
#endif

// ---------------------------------------------------------------------------
// reverse-continue: the newest 20 hits of #8200
// ---------------------------------------------------------------------------

/// The old way: 20 single calls from the end, each seeks to its hit
void BM_Analysis_ReverseContinue_20Singles(benchmark::State& state)
{
    Session& s = Get(false);
    if (!s.Ok())
        return state.SkipWithError("no session");
    for (auto _ : state)
    {
        state.PauseTiming();
        s.ttd->SeekTo(s.ttd->SessionEndPosition());
        state.ResumeTiming();
        for (int i = 0; i < 20; ++i)
            benchmark::DoNotOptimize(s.ttd->ReverseContinue(std::vector<uint16_t>{0x8200}).matched);
    }
}
BENCHMARK(BM_Analysis_ReverseContinue_20Singles)->Unit(benchmark::kMillisecond);

#ifndef TTD_ANALYSIS_BASELINE
void BM_Analysis_ReverseContinue_All20(benchmark::State& state)
{
    Session& s = Get(false);
    if (!s.Ok())
        return state.SkipWithError("no session");
    s.ttd->SeekTo(s.ttd->SessionEndPosition());
    ttd::TTDReverseContinueQuery q;
    q.pcs = {0x8200};
    q.all = true;
    q.limit = 20;
    for (auto _ : state)
        benchmark::DoNotOptimize(s.ttd->ReverseContinue(q).hits.size());
}
BENCHMARK(BM_Analysis_ReverseContinue_All20)->Unit(benchmark::kMillisecond);
#endif

// ---------------------------------------------------------------------------
// find-last: the newest 10 changes of #8103 (it flips every 16 writes: ~160 writes to walk)
// ---------------------------------------------------------------------------

/// The old way: single calls stepping before_tin back, a change wherever two neighbours differ
void FindChangesByLoop(benchmark::State& state, bool writeJournal)
{
    Session& s = Get(writeJournal);
    if (!s.Ok())
        return state.SkipWithError("no session");
    size_t calls = 0;
    for (auto _ : state)
    {
        ttd::TTDSearchQuery q = Writes(0x8103);
        q.beforeGlobalT = s.ttd->GlobalT(s.ttd->SessionEndPosition());
        int changes = 0;
        int previous = -1;
        calls = 0;
        while (changes < 10)
        {
            const auto hit = s.ttd->FindLastAccess(q);
            ++calls;
            if (!hit)
                break;
            if (previous >= 0 && hit->value != previous)
                ++changes;
            previous = hit->value;
            q.beforeGlobalT = s.ttd->GlobalT(hit->time) - 1;
        }
        benchmark::DoNotOptimize(changes);
    }
    state.counters["calls"] = static_cast<double>(calls);
}
void BM_Analysis_FindChanges_LoopJournal(benchmark::State& state) { FindChangesByLoop(state, true); }
void BM_Analysis_FindChanges_LoopReplay(benchmark::State& state) { FindChangesByLoop(state, false); }
BENCHMARK(BM_Analysis_FindChanges_LoopJournal)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Analysis_FindChanges_LoopReplay)->Unit(benchmark::kMillisecond);

#ifndef TTD_ANALYSIS_BASELINE
void FindChangesAtOnce(benchmark::State& state, bool writeJournal)
{
    Session& s = Get(writeJournal);
    if (!s.Ok())
        return state.SkipWithError("no session");
    s.ttd->SeekTo(s.ttd->SessionEndPosition());
    ttd::TTDFindOptions options;
    options.all = true;
    options.changesOnly = true;
    options.limit = 10;
    for (auto _ : state)
        benchmark::DoNotOptimize(s.ttd->FindAccesses(Writes(0x8103), options).hits.size());
}
void BM_Analysis_FindChanges_AtOnceJournal(benchmark::State& state) { FindChangesAtOnce(state, true); }
void BM_Analysis_FindChanges_AtOnceReplay(benchmark::State& state) { FindChangesAtOnce(state, false); }
BENCHMARK(BM_Analysis_FindChanges_AtOnceJournal)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Analysis_FindChanges_AtOnceReplay)->Unit(benchmark::kMillisecond);
#endif

// ---------------------------------------------------------------------------
// The Z80's 64 KB at a past point
// ---------------------------------------------------------------------------

/// The old way: seek there, read, seek back
void BM_Analysis_CpuView_SeekReadSeekBack(benchmark::State& state)
{
    Session& s = Get(false);
    if (!s.Ok())
        return state.SkipWithError("no session");
    const ttd::TTDTimePoint end = s.ttd->SessionEndPosition();
    const ttd::TTDTimePoint at{end.frame - kFrames / 2, 30000};
    std::vector<uint8_t> bytes(0x10000);
    for (auto _ : state)
    {
        s.ttd->SeekTo(at);
        for (uint32_t a = 0; a < 0x10000; ++a)
            bytes[a] = s.context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(a));
        s.ttd->SeekTo(end);
        benchmark::DoNotOptimize(bytes.data());
    }
}
BENCHMARK(BM_Analysis_CpuView_SeekReadSeekBack)->Unit(benchmark::kMillisecond);

#ifndef TTD_ANALYSIS_BASELINE
void BM_Analysis_CpuView(benchmark::State& state)
{
    Session& s = Get(false);
    if (!s.Ok())
        return state.SkipWithError("no session");
    const ttd::TTDTimePoint end = s.ttd->SessionEndPosition();
    s.ttd->SeekTo(end);
    const ttd::TTDTimePoint at{end.frame - kFrames / 2, 30000};
    for (auto _ : state)
        benchmark::DoNotOptimize(s.ttd->ReadCpuView(at, 0, 0x10000).bytes.size());
}
BENCHMARK(BM_Analysis_CpuView)->Unit(benchmark::kMillisecond);
#endif
}  // namespace
