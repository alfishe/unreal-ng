/// @file covox_demo_frame_benchmark.cpp
/// @brief Full-machine frame cost of a real workload: the covox scroller demo
/// (testdata/sound/covox/scroller_by_demarche.sna) on Pentagon 128 with the
/// default sound device, across core rates. This is the exact snapshot behind
/// the "20 ms frames" report from the Qt frontend, so it splits the question
/// "is the emulator core slow, or is it something the frontend does": if this
/// benchmark reproduces ~20 ms per frame the cost is in the core and can be
/// profiled here; if it stays in the low milliseconds, the frontend adds it.
///
/// Run with: ./core-benchmarks --benchmark_filter="BM_CovoxDemoFrame.*"

#include <benchmark/benchmark.h>

#include <string>

#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "emulator/sound/soundmanager.h"

namespace
{
const char* COVOX_SNA = "testdata/sound/covox/scroller_by_demarche.sna";

void RunCovoxDemoFrames(benchmark::State& state, size_t coreRate)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-covox-demo", "PENTAGON", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    MainLoopCUT* mainLoop = context ? reinterpret_cast<MainLoopCUT*>(context->pMainLoop) : nullptr;
    if (!mainLoop || !context->pSoundManager)
    {
        state.SkipWithError("main loop or sound manager unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    if (!emulator->LoadSnapshot(COVOX_SNA))
    {
        state.SkipWithError("snapshot not found - run from the repository root");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    if (coreRate != 0)
        context->pSoundManager->requestCoreRate(static_cast<uint32_t>(coreRate));

    // Warm-up: the rate change applies at a frame boundary, and the demo's
    // intro is light - the measured window must sit in the heavy scroller
    // section, so skip the first ~30 s of emulated time (1500 frames)
    for (int i = 0; i < 1500; i++)
        mainLoop->RunFramePublic();

    for (auto _ : state)
        mainLoop->RunFramePublic();

    state.SetLabel("covox demo, core " + std::to_string(context->pSoundManager->getCoreRate()) + " Hz");
    state.SetItemsProcessed(state.iterations());

    manager->RemoveEmulator(emulator->GetUUID());
}
}  // namespace

static void BM_CovoxDemoFrame_CoreRate(benchmark::State& state)
{
    RunCovoxDemoFrames(state, static_cast<size_t>(state.range(0)));
}

BENCHMARK(BM_CovoxDemoFrame_CoreRate)->Arg(44100)->Arg(192000)->Iterations(500)->Unit(benchmark::kMicrosecond);
