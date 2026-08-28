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
//   BM_TTD_Busy_RingFill_Frame   — replay ONE frame forward once (ring fill)
//   BM_TTD_Ring_ReadEntry        — read one cached M1 record (array access)
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

// Fixed-size CPU-history record the ring would store, per reverse-debugging.md §5.
struct M1Record
{
    uint16_t pc, sp, af, bc, de, hl, ix, iy;
    uint16_t af2, bc2, de2, hl2;
    uint8_t  i, r, im, _pad;
    uint8_t  opcodes[4];
    uint16_t spContent;
    uint8_t  slots[4];
};

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

// t-state of the last recorded instruction in frame F-1 (the frame just before
// session end), for a real mid-frame replay target.
uint32_t LastTInFrameBeforeEnd(uint64_t& frameOut)
{
    PositionAtSessionEnd();
    g_fixture.ttd->StepBackInstruction();          // land on last instr of previous frame
    ttd::TTDTimePoint p = g_fixture.ttd->CurrentPosition();
    frameOut = p.frame;
    PositionAtSessionEnd();
    return p.tInFrame;
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

static void BM_TTD_Busy_RingFill_Frame(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }
    ResetBusyRecording(50);
    const uint32_t instrPerFrame = CountBusyInstrPerFrame();

    uint64_t F = 0;
    const uint32_t lastT = LastTInFrameBeforeEnd(F);
    const ttd::TTDTimePoint frameStart{F, 0};
    const ttd::TTDTimePoint frameEnd{F, lastT};   // last instruction IN the frame (mid-frame → real replay)
    for (auto _ : state)
    {
        state.PauseTiming();
        g_fixture.ttd->SeekTo(frameStart);
        state.ResumeTiming();
        g_fixture.ttd->SeekTo(frameEnd);   // replay the whole busy frame forward == ring fill
    }
    state.counters["instr_per_frame"] = instrPerFrame;
    state.SetLabel("one forward busy-frame replay == ring-cache fill");
}
BENCHMARK(BM_TTD_Busy_RingFill_Frame)->Iterations(50)->Unit(benchmark::kMillisecond);

static void BM_TTD_Ring_ReadEntry(benchmark::State& state)
{
    // Pure ring read cost: an array of M1 records the size of one busy frame,
    // read sequentially with a full-record copy (what serving CMD_GET_HISTORY_ENTRY
    // from a ring would do — no emulation).
    const size_t n = 8000;
    std::vector<M1Record> ring(n);
    for (size_t i = 0; i < n; ++i) { ring[i].pc = static_cast<uint16_t>(i); ring[i].opcodes[0] = 0x3C; }

    size_t idx = 0;
    for (auto _ : state)
    {
        M1Record r = ring[idx % n];      // index + 40-byte copy
        benchmark::DoNotOptimize(r);
        ++idx;
    }
    state.counters["ring_entries"] = static_cast<double>(n);
    state.SetLabel("read one cached M1 record (no emulation)");
}
BENCHMARK(BM_TTD_Ring_ReadEntry)->Unit(benchmark::kNanosecond);
