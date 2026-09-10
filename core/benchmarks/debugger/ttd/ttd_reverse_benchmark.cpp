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

// ===========================================================================
// FindLastAccess — coverage index on vs off
// ===========================================================================
//
// Reverse search for Read and Execute has no journal to consult, so it walks
// backward restoring and replaying one frame at a time. The coverage index
// exists to skip frames that provably cannot match. This is the benchmark that
// says whether collecting it pays for itself.
//
// The distance to the target dominates: a hit in the current frame is found
// immediately either way, while a target last touched long ago is where the
// index earns its keep. Args({distanceInFrames, coverageEnabled}).

static void BM_TTD_FindLastAccess_Coverage(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    const uint32_t frames  = static_cast<uint32_t>(state.range(0));
    const bool coverageOn  = state.range(1) != 0;

    g_fixture.ttd->SetEnableCoverageIndex(coverageOn);
    g_fixture.ResetRecording(frames);

    // An address the workload does not touch, so the search has to walk the
    // whole history rather than stopping at the first frame. That is the worst
    // case, and the one that scales with session length.
    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = 0xFFFF;
    q.access = ttd::TTDAccessType::Read;

    for (auto _ : state)
    {
        state.PauseTiming();
        PositionAtSessionEnd();
        state.ResumeTiming();

        benchmark::DoNotOptimize(g_fixture.ttd->FindLastAccess(q));
    }

    state.SetLabel(std::string(coverageOn ? "index" : "replay-scan") +
                   " frames=" + std::to_string(frames));

    // Leave the fixture in the default state for whatever runs next.
    g_fixture.ttd->SetEnableCoverageIndex(true);
}
BENCHMARK(BM_TTD_FindLastAccess_Coverage)
    ->Args({10,  0})->Args({10,  1})
    ->Args({50,  0})->Args({50,  1})
    ->Args({200, 0})->Args({200, 1})
    ->Iterations(20)
    ->Unit(benchmark::kMicrosecond);

// ===========================================================================
// DeZog reverse-debugging: step-by-step full-frame rewind vs ring cache
// ===========================================================================
//
// Scenario (reverse-debugging.md §4/§5): the user rewinds execution one
// instruction at a time. Worst case for TTD is that each single
// StepBackInstruction restores the frame-start checkpoint and replays forward
// to the target t-mark, so walking back through a busy frame re-emulates that
// frame ~once per instruction.
//
// The stock benchmark workload (ROM boot) idles in a HALT loop — ~1 instruction
// per frame — which is NOT representative. These cases install a busy
// straight-line program so a frame packs thousands of instructions, matching a
// game/demo running flat out. All three quantities are measured on the same
// workload so the comparison is apples-to-apples:
//
//   BM_TTD_Busy_StepBack_Single  — one back-step to a late-in-frame t-mark
//                                  (the "fast-forward from frame start" cost)
//   BM_TTD_Busy_Rewind_Frame     — rewind ONE full frame step-by-step
//
// instr_per_frame is reported as a counter so §5's memory math uses a measured
// density, not an assumption.

namespace
{
// Busy program at 0x8000: a varied ALU/load/inc loop with no HALT, so it runs
// continuously and fills each frame with instructions.
//   8000: INC A            (3C)
//   8001: ADD A,L          (85)
//   8002: LD (HL),A        (77)
//   8003: INC L            (2C)
//   8004: DEC B            (05)
//   8005: RLCA             (07)
//   8006: JP 8000          (C3 00 80)
const uint8_t kBusyProgram[] = {0x3C, 0x85, 0x77, 0x2C, 0x05, 0x07, 0xC3, 0x00, 0x80};
constexpr uint16_t kBusyStart = 0x8000;

// Record a fresh busy session: install the program, point PC at it, run frames.
void ResetBusyRecording(uint32_t frames)
{
    g_fixture.ttd->InvalidateSession("busy benchmark reset");
    for (uint16_t i = 0; i < sizeof(kBusyProgram); ++i)
        g_fixture.memory->DirectWriteToZ80Memory(kBusyStart + i, kBusyProgram[i]);
    Z80State* z80 = g_fixture.emulator->GetZ80State();
    z80->pc = kBusyStart;
    z80->sp = 0xFF00;

    g_fixture.ttd->StartRecording();
    g_fixture.emulator->RunNFrames(static_cast<unsigned>(frames), /*skipBreakpoints=*/true);
    g_fixture.ttd->StopRecording();
    PositionAtSessionEnd();
}

// Instructions recorded in one frame: session end sits on a frame boundary, so
// the first back-step enters the previous frame F; count further steps until the
// cursor leaves F.
uint32_t CountBusyInstrPerFrame()
{
    PositionAtSessionEnd();
    if (!g_fixture.ttd->StepBackInstruction())
        return 0;
    const uint64_t F = g_fixture.ttd->CurrentPosition().frame;
    uint32_t n = 1;
    for (; n < 500000; ++n)
    {
        if (!g_fixture.ttd->StepBackInstruction())
            break;
        if (g_fixture.ttd->CurrentPosition().frame < F)
            break;
    }
    PositionAtSessionEnd();
    return n;
}

} // namespace

static void BM_TTD_Busy_StepBack_Single(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }
    ResetBusyRecording(50);
    const uint32_t instrPerFrame = CountBusyInstrPerFrame();

    for (auto _ : state)
    {
        state.PauseTiming();
        PositionAtSessionEnd();          // late in the frame → worst-case replay distance
        state.ResumeTiming();
        g_fixture.ttd->StepBackInstruction();
    }
    state.counters["instr_per_frame"] = instrPerFrame;
    state.SetLabel("one back-step, target late in a busy frame");
}
BENCHMARK(BM_TTD_Busy_StepBack_Single)->Iterations(200)->Unit(benchmark::kMillisecond);

// Bounded step-by-step rewind: 500 consecutive back-steps (spanning into the
// busy frame, replay distance shrinking as t decreases). Reports total and
// per-step average. A full-frame rewind = instr_per_frame x this per-step cost.
static void BM_TTD_Busy_Rewind_500(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }
    ResetBusyRecording(50);
    const uint32_t instrPerFrame = CountBusyInstrPerFrame();

    const uint32_t STEPS = 500;
    for (auto _ : state)
    {
        state.PauseTiming();
        PositionAtSessionEnd();
        state.ResumeTiming();
        for (uint32_t i = 0; i < STEPS; ++i)
            if (!g_fixture.ttd->StepBackInstruction()) break;
    }
    state.counters["instr_per_frame"] = instrPerFrame;
    state.counters["ms_per_step"] = benchmark::Counter(
        static_cast<double>(STEPS),
        benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert,
        benchmark::Counter::kIs1000);
    state.SetLabel("500 consecutive back-steps (step-by-step rewind)");
}
BENCHMARK(BM_TTD_Busy_Rewind_500)->Iterations(20)->Unit(benchmark::kMillisecond);

// ===========================================================================
// Per-frame decode cache: real cached lookup vs the replay baseline
// ===========================================================================
//
// Apples-to-apples with BM_TTD_Busy_StepBack_Single (retrieve one decoded
// instruction record), on the same busy workload, through the same manager:
//
//   BM_TTD_FrameCache_Fill        — GetFrameCache() first-touch (one frame replay)
//   BM_TTD_FrameCache_ReadEntry   — read one entry from the built cache (no replay)
//   BM_TTD_FrameCache_ReadVsReplay/0 — baseline: one StepBackInstruction (replay)
//   BM_TTD_FrameCache_ReadVsReplay/1 — cached:   one GetFrameCache+index (post-fill)
//
// Reported counters: instr_per_frame (density) and cache_bytes (footprint).

static void BM_TTD_FrameCache_Fill(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }
    ResetBusyRecording(50);
    const uint32_t instrPerFrame = CountBusyInstrPerFrame();
    const uint64_t frame = g_fixture.ttd->SessionEndPosition().frame - 1;

    for (auto _ : state)
    {
        state.PauseTiming();
        g_fixture.ttd->ClearFrameCache();   // force a rebuild each iteration
        state.ResumeTiming();
        benchmark::DoNotOptimize(g_fixture.ttd->GetFrameCache(frame));
    }
    state.counters["instr_per_frame"] = instrPerFrame;
    state.counters["cache_bytes"] = static_cast<double>(g_fixture.ttd->GetFrameCacheBytes());
    state.SetLabel("GetFrameCache first-touch (one frame replay fills the cache)");
}
BENCHMARK(BM_TTD_FrameCache_Fill)->Iterations(50)->Unit(benchmark::kMillisecond);

static void BM_TTD_FrameCache_ReadEntry(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }
    ResetBusyRecording(50);
    const uint64_t frame = g_fixture.ttd->SessionEndPosition().frame - 1;
    const ttd::TTDFrameCache* c = g_fixture.ttd->GetFrameCache(frame);
    if (!c || c->entries.empty()) { state.SkipWithError("cache build failed"); return; }

    const size_t n = c->entries.size();
    size_t idx = 0;
    for (auto _ : state)
    {
        // Serve one history entry from the cache: index + copy the record.
        ttd::TTDFrameCacheEntry e = c->entries[idx % n];
        benchmark::DoNotOptimize(e);
        ++idx;
    }
    state.counters["instr_per_frame"] = static_cast<double>(n);
    state.counters["cache_bytes"] = static_cast<double>(g_fixture.ttd->GetFrameCacheBytes());
    state.SetLabel("read one entry from the built cache (no emulation)");
}
BENCHMARK(BM_TTD_FrameCache_ReadEntry)->Unit(benchmark::kNanosecond);

// Single benchmark, two variants, so the numbers sit side by side.
static void BM_TTD_FrameCache_ReadVsReplay(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }
    const bool cached = state.range(0) != 0;
    ResetBusyRecording(50);
    const uint32_t instrPerFrame = CountBusyInstrPerFrame();
    const uint64_t frame = g_fixture.ttd->SessionEndPosition().frame - 1;

    if (cached)
    {
        const ttd::TTDFrameCache* c = g_fixture.ttd->GetFrameCache(frame);
        if (!c || c->entries.empty()) { state.SkipWithError("cache build failed"); return; }
        const size_t n = c->entries.size();
        size_t idx = 0;
        for (auto _ : state)
        {
            ttd::TTDFrameCacheEntry e = c->entries[idx % n];  // O(1) cached lookup
            benchmark::DoNotOptimize(e);
            ++idx;
        }
        state.counters["cache_bytes"] = static_cast<double>(g_fixture.ttd->GetFrameCacheBytes());
        state.SetLabel("cached: GetFrameCache index (post-fill)");
    }
    else
    {
        for (auto _ : state)
        {
            state.PauseTiming();
            PositionAtSessionEnd();
            state.ResumeTiming();
            benchmark::DoNotOptimize(g_fixture.ttd->StepBackInstruction());  // replay per read
        }
        state.SetLabel("replay: one StepBackInstruction (baseline)");
    }
    state.counters["instr_per_frame"] = instrPerFrame;
}
BENCHMARK(BM_TTD_FrameCache_ReadVsReplay)
    ->Arg(0)->Arg(1)
    ->Iterations(200)
    ->Unit(benchmark::kNanosecond);

// ===========================================================================
// Frame-cache record LAYOUT comparison (structure selection)
// ===========================================================================
//
// The per-frame cache stores one record per instruction. The question is the
// record layout. Three candidates are materialized from the SAME real captured
// frame and measured for: hot lookup (random access to CPU/history fields, the
// DeZog getHistoryEntry path), sequential PC scan (reverse-continue path), and
// footprint. Goal: reasonable memory, fast lookup, minimal allocations.
//
//   Fat AoS   — current: fixed record with INLINE mem/port arrays (~92 B)
//   Split     — minimal fixed record + a reference {offset,memCount,portCount}
//               into ONE shared access arena (hot/cold split)
//   CpuOnly   — minimal fixed record, no accesses at all (lower bound)
//
// Split and CpuOnly keep the hot record small (better cache behaviour) and use
// at most two amortized allocations (records vector + arena vector).

namespace layoutbench
{
// -- Fat AoS (mirrors ttd::TTDFrameCacheEntry) --
struct Access { uint16_t addr; uint8_t value; };
struct FatEntry
{
    uint32_t tInFrame;
    uint16_t pc, sp, af, bc, de, hl, ix, iy, af2, bc2, de2, hl2;
    uint8_t  i, r, im, _pad;
    uint8_t  opcodes[4];
    uint16_t spContent;
    uint8_t  slotCount; uint8_t slots[8];
    uint8_t  memWriteCount, portWriteCount; bool memOverflow, portOverflow;
    Access   memWrites[4];
    Access   portWrites[2];
};

// -- Split: minimal hot record + arena reference --
struct SplitEntry
{
    uint32_t tInFrame;
    uint16_t pc, sp, af, bc, de, hl, ix, iy, af2, bc2, de2, hl2;
    uint8_t  i, r, im;
    uint8_t  opcodes[4];
    uint16_t spContent;
    uint8_t  slotCount; uint8_t slots[8];
    uint32_t accessOffset;   // into a shared arena
    uint8_t  memCount, portCount;
};
struct ArenaAccess { uint16_t addr; uint8_t value; uint8_t kind; };  // kind: 0=mem 1=port

// -- CpuOnly: no accesses --
struct CpuOnlyEntry
{
    uint32_t tInFrame;
    uint16_t pc, sp, af, bc, de, hl, ix, iy, af2, bc2, de2, hl2;
    uint8_t  i, r, im;
    uint8_t  opcodes[4];
    uint16_t spContent;
    uint8_t  slotCount; uint8_t slots[8];
};

// -- TinyHot: minimal hot record {tInFrame,pc} + full CPU state in an extension --
struct TinyHotEntry { uint32_t tInFrame; uint16_t pc; uint32_t coldOffset; };
struct TinyColdEntry
{
    uint16_t sp, af, bc, de, hl, ix, iy, af2, bc2, de2, hl2;
    uint8_t  i, r, im;
    uint8_t  opcodes[4];
    uint16_t spContent;
    uint8_t  slotCount; uint8_t slots[8];
};

// Build all three from a captured frame (returns instruction count).
struct Base
{
    std::vector<FatEntry> fat;
    std::vector<SplitEntry> split;
    std::vector<CpuOnlyEntry> cpu;
    std::vector<TinyHotEntry> tiny;
    std::vector<TinyColdEntry> tinyCold;
    std::vector<ArenaAccess> arena;
};
Base g_base;
bool g_baseReady = false;

// Materialize one real captured frame into each layout (base tile).
const Base& EnsureBase()
{
    if (g_baseReady) return g_base;
    ResetBusyRecording(50);
    const uint64_t frame = g_fixture.ttd->SessionEndPosition().frame - 1;
    const ttd::TTDFrameCache* c = g_fixture.ttd->GetFrameCache(frame);
    if (c)
    {
        const size_t n = c->entries.size();
        g_base.fat.reserve(n); g_base.split.reserve(n); g_base.cpu.reserve(n);
        g_base.arena.reserve(c->accesses.size());
        for (size_t idx = 0; idx < n; ++idx)
        {
            const ttd::TTDFrameCacheEntry& e = c->entries[idx];
            FatEntry f{};
            f.tInFrame=e.tInFrame; f.pc=e.pc; f.sp=e.sp; f.af=e.af; f.bc=e.bc; f.de=e.de;
            f.hl=e.hl; f.ix=e.ix; f.iy=e.iy; f.af2=e.af2; f.bc2=e.bc2; f.de2=e.de2; f.hl2=e.hl2;
            f.i=e.i; f.r=e.r; f.im=e.im; for(int b=0;b<4;++b) f.opcodes[b]=e.opcodes[b];
            f.spContent=e.spContent; f.slotCount=e.slotCount; for(int sx=0;sx<8;++sx) f.slots[sx]=e.slots[sx];
            uint16_t nacc=0; const ttd::TTDFrameCacheAccess* acc=c->AccessesOf(idx,nacc);
            uint8_t mc=0,pc=0;
            for(uint16_t a=0;a<nacc;++a){ if(acc[a].kind==ttd::TTDAccessKind::PortWrite){ if(pc<2) f.portWrites[pc]={acc[a].addr,acc[a].value}; pc++;} else { if(mc<4) f.memWrites[mc]={acc[a].addr,acc[a].value}; mc++;} }
            f.memWriteCount=mc; f.portWriteCount=pc;
            g_base.fat.push_back(f);

            SplitEntry sp{};
            sp.tInFrame=e.tInFrame; sp.pc=e.pc; sp.sp=e.sp; sp.af=e.af; sp.bc=e.bc; sp.de=e.de;
            sp.hl=e.hl; sp.ix=e.ix; sp.iy=e.iy; sp.af2=e.af2; sp.bc2=e.bc2; sp.de2=e.de2; sp.hl2=e.hl2;
            sp.i=e.i; sp.r=e.r; sp.im=e.im; for(int b=0;b<4;++b) sp.opcodes[b]=e.opcodes[b];
            sp.spContent=e.spContent; sp.slotCount=e.slotCount; for(int sx=0;sx<8;++sx) sp.slots[sx]=e.slots[sx];
            sp.accessOffset=static_cast<uint32_t>(g_base.arena.size());
            sp.memCount=mc; sp.portCount=pc;
            for(uint16_t a=0;a<nacc;++a) g_base.arena.push_back({acc[a].addr,acc[a].value,(uint8_t)acc[a].kind});
            g_base.split.push_back(sp);

            CpuOnlyEntry co{};
            co.tInFrame=e.tInFrame; co.pc=e.pc; co.sp=e.sp; co.af=e.af; co.bc=e.bc; co.de=e.de;
            co.hl=e.hl; co.ix=e.ix; co.iy=e.iy; co.af2=e.af2; co.bc2=e.bc2; co.de2=e.de2; co.hl2=e.hl2;
            co.i=e.i; co.r=e.r; co.im=e.im; for(int b=0;b<4;++b) co.opcodes[b]=e.opcodes[b];
            co.spContent=e.spContent; co.slotCount=e.slotCount; for(int sx=0;sx<8;++sx) co.slots[sx]=e.slots[sx];
            g_base.cpu.push_back(co);

            TinyHotEntry th{}; th.tInFrame=e.tInFrame; th.pc=e.pc; th.coldOffset=static_cast<uint32_t>(g_base.tinyCold.size());
            g_base.tiny.push_back(th);
            TinyColdEntry tc{};
            tc.sp=e.sp; tc.af=e.af; tc.bc=e.bc; tc.de=e.de; tc.hl=e.hl; tc.ix=e.ix; tc.iy=e.iy;
            tc.af2=e.af2; tc.bc2=e.bc2; tc.de2=e.de2; tc.hl2=e.hl2; tc.i=e.i; tc.r=e.r; tc.im=e.im;
            for(int b=0;b<4;++b) tc.opcodes[b]=e.opcodes[b];
            tc.spContent=e.spContent; tc.slotCount=e.slotCount; for(int sx=0;sx<8;++sx) tc.slots[sx]=e.slots[sx];
            g_base.tinyCold.push_back(tc);
        }
    }
    g_baseReady = true;
    return g_base;
}

// Cheap deterministic index mix (no Math.random in the fixture).
inline size_t mix(size_t x){ x^=x>>16; x*=0x7feb352dU; x^=x>>15; x*=0x846ca68bU; x^=x>>16; return x; }

// A working set is the base tiled `frames` times — models a windowed cache
// holding that many browsed frames. Cached per (layout,frames) so the tiling
// cost is not measured.
template <class T>
std::vector<T>& TiledWorkingSet(const std::vector<T>& base, int frames)
{
    static std::vector<std::vector<T>> cache;   // indexed by frames (small)
    if (static_cast<int>(cache.size()) <= frames) cache.resize(frames + 1);
    std::vector<T>& ws = cache[frames];
    if (ws.empty() && !base.empty())
    {
        ws.reserve(base.size() * frames);
        for (int f = 0; f < frames; ++f) ws.insert(ws.end(), base.begin(), base.end());
    }
    return ws;
}
} // namespace layoutbench

// Random hot lookup at a realistic working-set size. Arg0: layout (0 fat,
// 1 split, 2 cpu). Arg1: frames held (working set = frames x one busy frame).
// Small frames fit in cache (record size hidden); large frames exceed L2/L3, so
// the smaller record misses less and wins.
static void BM_TTD_Layout_HotLookup(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }
    using namespace layoutbench;
    EnsureBase();
    const int which = static_cast<int>(state.range(0));
    const int frames = static_cast<int>(state.range(1));

    size_t n = 0, recBytes = 0, totalBytes = 0;
    const FatEntry* fp=nullptr; const SplitEntry* spp=nullptr; const CpuOnlyEntry* cp=nullptr;
    if (which==0){ auto& ws=TiledWorkingSet(g_base.fat,frames);   n=ws.size(); fp=ws.data();  recBytes=sizeof(FatEntry);     totalBytes=n*recBytes; }
    else if(which==1){ auto& ws=TiledWorkingSet(g_base.split,frames); n=ws.size(); spp=ws.data(); recBytes=sizeof(SplitEntry); totalBytes=n*recBytes + g_base.arena.size()*frames*sizeof(ArenaAccess); }
    else if(which==3){ auto& ws=TiledWorkingSet(g_base.tiny,frames); n=ws.size(); recBytes=sizeof(TinyHotEntry)+sizeof(TinyColdEntry); totalBytes=n*recBytes; }
    else { auto& ws=TiledWorkingSet(g_base.cpu,frames); n=ws.size(); cp=ws.data(); recBytes=sizeof(CpuOnlyEntry); totalBytes=n*recBytes; }
    if (n==0){ state.SkipWithError("no data"); return; }
    // TinyHot full-state lookup reads the hot record then dereferences the cold
    // extension — two arrays, so two potential cache misses per lookup.
    std::vector<TinyHotEntry>* thw = (which==3)? &TiledWorkingSet(g_base.tiny,frames) : nullptr;
    std::vector<TinyColdEntry>* tcw = (which==3)? &TiledWorkingSet(g_base.tinyCold,frames) : nullptr;

    size_t k=0; uint64_t sink=0;
    for (auto _ : state)
    {
        size_t idx = mix(k++) % n;
        if (which==0){ const auto& e=fp[idx];  sink += e.pc+e.af+e.hl+e.sp+e.opcodes[0]+e.spContent+e.slots[1]; }
        else if(which==1){ const auto& e=spp[idx]; sink += e.pc+e.af+e.hl+e.sp+e.opcodes[0]+e.spContent+e.slots[1]; }
        else if(which==3){ const auto& h=(*thw)[idx]; const auto& cptr=(*tcw)[idx]; sink += h.pc+cptr.af+cptr.hl+cptr.sp+cptr.opcodes[0]+cptr.spContent+cptr.slots[1]; }
        else { const auto& e=cp[idx]; sink += e.pc+e.af+e.hl+e.sp+e.opcodes[0]+e.spContent+e.slots[1]; }
    }
    benchmark::DoNotOptimize(sink);
    state.counters["record_bytes"] = static_cast<double>(recBytes);
    state.counters["ws_MB"] = static_cast<double>(totalBytes) / (1024.0*1024.0);
    state.counters["records"] = static_cast<double>(n);
    state.SetLabel(which==0?"Fat AoS":which==1?"Split (record+arena)":which==3?"TinyHot (hot+cold ext)":"CpuOnly");
}
BENCHMARK(BM_TTD_Layout_HotLookup)
    ->Args({0,1})->Args({1,1})->Args({2,1})       // ~1x frame: fits L2
    ->Args({0,8})->Args({1,8})->Args({2,8})       // 8x: exceeds L2
    ->Args({3,8})                                  // TinyHot full lookup (indirection)
    ->Args({0,32})->Args({1,32})->Args({2,32})    // 32x: exceeds L3
    ->Args({3,32})
    ->Unit(benchmark::kNanosecond);

// Single-field scan (reverse-continue on PC): streams only the field touched.
// This is where a tiny hot record wins — smaller stride, less bandwidth.
static void BM_TTD_Layout_PcScan(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }
    using namespace layoutbench;
    EnsureBase();
    const int which = static_cast<int>(state.range(0));   // 0 fat,1 split,2 cpu,3 tinyhot
    const int frames = static_cast<int>(state.range(1));
    uint64_t sink=0;
    if (which==0){ auto& ws=TiledWorkingSet(g_base.fat,frames);  if(ws.empty()){state.SkipWithError("no data");return;} for(auto _:state){ sink=0; for(const auto& e:ws) sink+=e.pc; benchmark::DoNotOptimize(sink);} state.SetLabel("Fat AoS 76B stride"); }
    else if(which==1){ auto& ws=TiledWorkingSet(g_base.split,frames); if(ws.empty()){state.SkipWithError("no data");return;} for(auto _:state){ sink=0; for(const auto& e:ws) sink+=e.pc; benchmark::DoNotOptimize(sink);} state.SetLabel("Split 56B stride"); }
    else if(which==3){ auto& ws=TiledWorkingSet(g_base.tiny,frames); if(ws.empty()){state.SkipWithError("no data");return;} for(auto _:state){ sink=0; for(const auto& e:ws) sink+=e.pc; benchmark::DoNotOptimize(sink);} state.SetLabel("TinyHot 10B stride"); }
    else { auto& ws=TiledWorkingSet(g_base.cpu,frames); if(ws.empty()){state.SkipWithError("no data");return;} for(auto _:state){ sink=0; for(const auto& e:ws) sink+=e.pc; benchmark::DoNotOptimize(sink);} state.SetLabel("CpuOnly 48B stride"); }
}
BENCHMARK(BM_TTD_Layout_PcScan)
    ->Args({0,8})->Args({1,8})->Args({3,8})
    ->Args({0,32})->Args({1,32})->Args({3,32})
    ->Unit(benchmark::kMicrosecond);
