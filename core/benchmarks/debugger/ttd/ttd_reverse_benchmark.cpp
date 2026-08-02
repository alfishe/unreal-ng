// ttd_reverse_benchmark.cpp — measures the per-call cost of the Phase 4
// reverse-execution primitives:
//
//   - ReverseStepInstructions(n) — go back N opcodes
//   - ReverseStepTStates(n)      — go back N t-states, land at nearest M1
//   - ReverseContinue(pcs)       — backward until any PC matches
//
// Each case is run in three variants so the numbers are directly comparable:
//
//   A_seq     — repeated StepBackInstruction() (single-opcode primitive)
//   B_m1list  — single M1 enumeration + indexed pick (ReverseStepInstructions)
//   B_direct  — direct SeekTo(target) for t-states (no M1 alignment)
//
// Stage C of the reverse-execution plan picks the strategy thresholds from
// these numbers. The constants in timetravelmanager.h
// (`kReverseSeqStepMaxN`, `kReverseM1ListLargeN`) reference this benchmark.
//
// Run with:  ./core-benchmarks --benchmark_filter="TTD_Reverse.*"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <vector>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

namespace
{

/// @brief Per-benchmark Emulator + TTD setup.
///
/// Constructed once per benchmark process (static instance), reset per
/// benchmark case via ResetRecording(). Lives on the benchmark thread —
/// Google Benchmark runs each case sequentially on its main thread, so we
/// don't need thread synchronization.
struct TTDReverseFixture
{
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;
    ttd::TimeTravelManager* ttd = nullptr;
    FeatureManager* fm = nullptr;
    Memory* memory = nullptr;

    bool Init()
    {
        emulator = new Emulator(LoggerLevel::LogError);
        if (!emulator->Init())
            return false;
        context = emulator->GetContext();
        if (!context) return false;
        ttd = context->pTimeTravelManager;
        if (!ttd) return false;
        memory = context->pMemory;
        if (!memory) return false;
        fm = emulator->GetFeatureManager();
        if (!fm) return false;

        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        memory->UpdateFeatureCache();
        return true;
    }

    void Shutdown()
    {
        if (emulator)
        {
            emulator->Stop();
            emulator->Release();
            delete emulator;
            emulator = nullptr;
        }
    }

    /// @brief Discard any existing session, start a fresh recording, run
    /// `frames` frames, stop. The emulator ends parked at the session-end
    /// frame boundary (Detached state).
    void ResetRecording(uint32_t frames)
    {
        ttd->InvalidateSession("benchmark reset");
        ttd->StartRecording();
        emulator->RunNFrames(static_cast<unsigned>(frames), /*skipBreakpoints=*/true);
        ttd->StopRecording();

        // Position at session-end so the first reverse step has somewhere
        // to step back from.
        const ttd::TTDTimePoint end = ttd->SessionEndPosition();
        ttd->SeekTo(end);
    }
};

TTDReverseFixture g_fixture;
bool g_fixtureReady = false;

/// @brief One-time process-wide fixture init. Idempotent.
/// Returns false on failure (caller will assert in main path).
bool EnsureFixture()
{
    if (g_fixtureReady) return true;
    if (!g_fixture.Init())
        return false;
    g_fixtureReady = true;
    return true;
}

/// @brief Common per-iteration setup: position at session end so every
/// reverse measurement starts from the same point.
inline void PositionAtSessionEnd()
{
    const ttd::TTDTimePoint end = g_fixture.ttd->SessionEndPosition();
    g_fixture.ttd->SeekTo(end);
}

/// @brief Variant tag — distinguishes the three implementations.
enum class Variant
{
    A_seq,     // repeated StepBackInstruction()
    B_m1list,  // single ReverseStepInstructions call
    B_direct,  // direct SeekTo (only valid for t-states)
};

const char* VariantLabel(Variant v)
{
    switch (v)
    {
        case Variant::A_seq:    return "A_seq";
        case Variant::B_m1list: return "B_m1list";
        case Variant::B_direct: return "B_direct";
    }
    return "?";
}

} // namespace

// ===========================================================================
// ReverseStepInstructions — N={1,4,16,64,256,1024,4096}
// ===========================================================================
//
// A_seq vs B_m1list.  B_direct is not applicable here (instruction count
// doesn't have a direct-t-state equivalent without an estimate).

static void BM_TTD_Reverse_StepBack_N(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    const uint32_t n = static_cast<uint32_t>(state.range(0));
    const Variant variant = static_cast<Variant>(state.range(1));
    const uint32_t frames = 50;  // ~50 frames of history

    g_fixture.ResetRecording(frames);

    for (auto _ : state)
    {
        state.PauseTiming();
        PositionAtSessionEnd();
        state.ResumeTiming();

        switch (variant)
        {
            case Variant::A_seq:
                for (uint32_t i = 0; i < n; ++i)
                    g_fixture.ttd->StepBackInstruction();
                break;
            case Variant::B_m1list:
                g_fixture.ttd->ReverseStepInstructions(n);
                break;
            case Variant::B_direct:
                // Not applicable for instruction-count targets.
                break;
        }
    }

    state.SetLabel(std::string(VariantLabel(variant)) + " N=" + std::to_string(n));
}
BENCHMARK(BM_TTD_Reverse_StepBack_N)
    ->Args({1,    static_cast<int64_t>(Variant::A_seq)})
    ->Args({4,    static_cast<int64_t>(Variant::A_seq)})
    ->Args({16,   static_cast<int64_t>(Variant::A_seq)})
    ->Args({64,   static_cast<int64_t>(Variant::A_seq)})
    ->Args({256,  static_cast<int64_t>(Variant::A_seq)})
    ->Args({1,    static_cast<int64_t>(Variant::B_m1list)})
    ->Args({4,    static_cast<int64_t>(Variant::B_m1list)})
    ->Args({16,   static_cast<int64_t>(Variant::B_m1list)})
    ->Args({64,   static_cast<int64_t>(Variant::B_m1list)})
    ->Args({256,  static_cast<int64_t>(Variant::B_m1list)})
    ->Args({1024, static_cast<int64_t>(Variant::B_m1list)})
    ->Args({4096, static_cast<int64_t>(Variant::B_m1list)})
    ->Iterations(50)
    ->Unit(benchmark::kMicrosecond);

// ===========================================================================
// ReverseStepTStates — N={1,100,1000,69888,700000}
// ===========================================================================
//
// B_direct (direct SeekTo) vs B_m1list (M1-aligning ReverseStepTStates).
// A_seq isn't applicable — there's no single-t-state StepBack primitive.

static void BM_TTD_Reverse_TStates_N(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    const uint64_t n = static_cast<uint64_t>(state.range(0));
    const Variant variant = static_cast<Variant>(state.range(1));
    const uint32_t frames = 50;

    g_fixture.ResetRecording(frames);

    for (auto _ : state)
    {
        state.PauseTiming();
        PositionAtSessionEnd();
        state.ResumeTiming();

        switch (variant)
        {
            case Variant::B_m1list:
                g_fixture.ttd->ReverseStepTStates(n);
                break;
            case Variant::B_direct:
            {
                // Decompose n into (frame, tInFrame) and SeekTo directly.
                const ttd::TTDTimePoint now = g_fixture.ttd->CurrentPosition();
                const uint32_t frameT = g_fixture.context->config.frame;
                const uint64_t nowGlobalT =
                    static_cast<uint64_t>(now.frame) * frameT + now.tInFrame;
                if (n < nowGlobalT)
                {
                    const uint64_t target = nowGlobalT - n;
                    ttd::TTDTimePoint tp;
                    tp.frame    = target / frameT;
                    tp.tInFrame = static_cast<uint32_t>(target % frameT);
                    g_fixture.ttd->SeekTo(tp);
                }
                break;
            }
            case Variant::A_seq:
                break;  // Not applicable.
        }
    }

    state.SetLabel(std::string(VariantLabel(variant)) + " T=" + std::to_string(n));
}
BENCHMARK(BM_TTD_Reverse_TStates_N)
    ->Args({1,      static_cast<int64_t>(Variant::B_m1list)})
    ->Args({100,    static_cast<int64_t>(Variant::B_m1list)})
    ->Args({1000,   static_cast<int64_t>(Variant::B_m1list)})
    ->Args({69888,  static_cast<int64_t>(Variant::B_m1list)})
    ->Args({700000, static_cast<int64_t>(Variant::B_m1list)})
    ->Args({1,      static_cast<int64_t>(Variant::B_direct)})
    ->Args({100,    static_cast<int64_t>(Variant::B_direct)})
    ->Args({1000,   static_cast<int64_t>(Variant::B_direct)})
    ->Args({69888,  static_cast<int64_t>(Variant::B_direct)})
    ->Args({700000, static_cast<int64_t>(Variant::B_direct)})
    ->Iterations(50)
    ->Unit(benchmark::kMicrosecond);

// ===========================================================================
// ReverseContinue — Bp={1,10}, target planted at known PC
// ===========================================================================
//
// All variants use B_m1list internally (there's no other strategy). The
// sweep is over the number of reverse breakpoints (linear-scan cost in
// the post-enumeration PC match).

static void BM_TTD_Reverse_Continue_Bp(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    const size_t bpCount = static_cast<size_t>(state.range(0));
    const uint32_t frames = 50;

    g_fixture.ResetRecording(frames);

    // Build a breakpoint set: bpCount PCs spread across the address range.
    // Use ones we know aren't the current PC so the scan walks the whole
    // M1 list backward.
    std::vector<uint16_t> bps;
    bps.reserve(bpCount);
    for (size_t i = 0; i < bpCount; ++i)
        bps.push_back(static_cast<uint16_t>(0x8000 + i * 0x10));

    for (auto _ : state)
    {
        state.PauseTiming();
        PositionAtSessionEnd();
        state.ResumeTiming();

        g_fixture.ttd->ReverseContinue(bps);
    }

    state.SetLabel(std::string("B_m1list Bp=") + std::to_string(bpCount));
}
BENCHMARK(BM_TTD_Reverse_Continue_Bp)
    ->Args({1})
    ->Args({10})
    ->Args({100})
    ->Iterations(50)
    ->Unit(benchmark::kMicrosecond);
