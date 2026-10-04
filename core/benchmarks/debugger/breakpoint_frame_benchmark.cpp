#include <benchmark/benchmark.h>

#include <random>

#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "base/featuremanager.h"

/// What the breakpoint check costs a whole frame (hotpath-matching-design.md §8): a 48K idling in BASIC (the ROM
/// scans the keyboard, takes the frame interrupt, updates its system variables), one frame per iteration through
/// MainLoop::RunFramePublic(), in four states:
///   off      debug mode off: the fast memory interface, no check at all
///   unarmed  debug mode and the breakpoints feature on, no breakpoint: the debug interface and the gates
///   armed/N  N breakpoints that never hit (code points in #8000-#FFFF, read / write points in #C000-#FFFF, port
///            points on odd ports): the filters on every access, a "maybe" never
/// Only the single-address API that every version has, so the same file measures master and a branch (A/B).
/// The breakpoints must not hit: the emulator's own run pauses and waits on a hit.
namespace
{
enum class Mode
{
    Off,
    Unarmed,
    Armed
};

void RunBreakpointFrame(benchmark::State& state, Mode mode, int count)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-bp-frame", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }
    EmulatorContext* context = emulator->GetContext();
    MainLoopCUT* mainLoop = context ? reinterpret_cast<MainLoopCUT*>(context->pMainLoop) : nullptr;
    if (!mainLoop)
    {
        state.SkipWithError("main loop unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    // Boot to the BASIC prompt first, without the debugger
    for (int i = 0; i < 150; i++)
        mainLoop->RunFramePublic();

    if (mode != Mode::Off)
    {
        emulator->DebugOn();
        context->pFeatureManager->setFeature(Features::kDebugMode, true);
        context->pFeatureManager->setFeature(Features::kBreakpoints, true);
        emulator->GetMemory()->UpdateFeatureCache();
    }
    if (mode == Mode::Armed)
    {
        BreakpointManager* bpm = emulator->GetBreakpointManager();
        std::mt19937 rng(22);
        for (int i = 0; i < count; i++)
        {
            switch (i % 8)
            {
                case 0:
                case 1:
                case 2:
                case 3:
                    bpm->AddExecutionBreakpoint(static_cast<uint16_t>(0x8000 | (rng() & 0x7FFF)));
                    break;
                case 4:
                case 5:
                    bpm->AddMemWriteBreakpoint(static_cast<uint16_t>(0xC000 | (rng() & 0x3FFF)));
                    break;
                case 6:
                    bpm->AddMemReadBreakpoint(static_cast<uint16_t>(0xC000 | (rng() & 0x3FFF)));
                    break;
                default:
                    bpm->AddPortInBreakpoint(static_cast<uint16_t>((rng() & 0xFFFF) | 0x0001));  // odd: never the ULA
                    break;
            }
        }
    }
    for (int i = 0; i < 5; i++)
        mainLoop->RunFramePublic();

    for (auto _ : state)
        mainLoop->RunFramePublic();

    state.SetItemsProcessed(state.iterations());
    state.counters["breakpoints"] = mode == Mode::Armed ? count : 0;
    manager->RemoveEmulator(emulator->GetUUID());
}
}  // namespace

static void BM_BreakpointFrame_Off(benchmark::State& state) { RunBreakpointFrame(state, Mode::Off, 0); }
static void BM_BreakpointFrame_Unarmed(benchmark::State& state) { RunBreakpointFrame(state, Mode::Unarmed, 0); }
static void BM_BreakpointFrame_Armed(benchmark::State& state)
{
    RunBreakpointFrame(state, Mode::Armed, static_cast<int>(state.range(0)));
}

BENCHMARK(BM_BreakpointFrame_Off)->Iterations(600)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_BreakpointFrame_Unarmed)->Iterations(600)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_BreakpointFrame_Armed)->Arg(10)->Arg(1000)->Arg(10000)->Iterations(600)->Unit(benchmark::kMicrosecond);
