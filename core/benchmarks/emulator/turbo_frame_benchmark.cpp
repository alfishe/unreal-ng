#include <benchmark/benchmark.h>

#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"

/// Full-machine frame cost through MainLoop::RunFramePublic(): CPU frame
/// cycle, contingent ScreenHQ rendering, framebuffer latch and per-frame
/// bookkeeping. The turbo variant engages Core::EnableTurboMode() first, so
/// the difference between the two isolates what turbo skips today (pacing is
/// absent in both — RunFrame is driven directly, not via the paced Run loop).
///
/// Turbo tape design §3.1: machine timing per frame is identical in turbo;
/// only host-side per-frame work differs. These numbers are the denominator
/// of the wall-clock speedup factor (frame_duration / per-frame cost).
static void RunFrameCostBenchmark(benchmark::State& state, bool turbo)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-turbo-frame", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    // CUT reinterpret_cast is the established idiom (tape/FDC integration
    // tests): the wrapper adds no data members, and this target defines
    // _CODE_UNDER_BENCHMARK so the protected RunFrame() becomes reachable.
    MainLoopCUT* mainLoop = context ? reinterpret_cast<MainLoopCUT*>(context->pMainLoop) : nullptr;
    Core* core = context ? context->pCore : nullptr;
    if (!mainLoop || !core)
    {
        state.SkipWithError("main loop or core unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    // Warm-up: let one-off boot work (table builds, first-frame paths) settle
    for (int i = 0; i < 10; i++)
        mainLoop->RunFramePublic();

    if (turbo)
        core->EnableTurboMode();

    for (auto _ : state)
    {
        mainLoop->RunFramePublic();
    }

    state.SetLabel(turbo ? "turbo mode" : "normal mode");
    state.SetItemsProcessed(state.iterations());

    if (turbo)
        core->DisableTurboMode();

    manager->RemoveEmulator(emulator->GetUUID());
}

static void BM_FrameCostNormal(benchmark::State& state)
{
    RunFrameCostBenchmark(state, false);
}

static void BM_FrameCostTurbo(benchmark::State& state)
{
    RunFrameCostBenchmark(state, true);
}

BENCHMARK(BM_FrameCostNormal)->Iterations(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_FrameCostTurbo)->Iterations(2000)->Unit(benchmark::kMicrosecond);
