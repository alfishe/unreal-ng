#include <benchmark/benchmark.h>

#include <memory>

#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"

/// Sprinter frame cost (Sprinter tdd-video §3, the naive v1 renderer): BIOS 3.04
/// at its logo (frame 60: graphics squares for the logo, 80-column text, border
/// squares), the whole 736x288 frame drawn in one go (RenderFrameBatch), and a
/// full frame of CPU + per-T catch-up rendering. The square cache (MAME's
/// tilemap idea) waits in the Sprinter TODO for these numbers.
///
/// The machine is often loaded: check vm.loadavg and compare interleaved runs.
///   core-benchmarks --benchmark_filter=BM_Sprinter --benchmark_repetitions=5
static std::shared_ptr<Emulator> BootSprinterToLogo(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-sprinter", "SPRINTER", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed (data/rom/sprinter/sp2k-3.04.rom?)");
        return nullptr;
    }
    EmulatorContext* context = emulator->GetContext();
    context->config.sprinter.fast_start = 1;
    emulator->Reset();
    context->pFeatureManager->setFeature(Features::kScreenHQ, true);
    context->pFeatureManager->setFeature(Features::kSoundHQ, false);
    context->pMemory->UpdateFeatureCache();

    MainLoopCUT* mainLoop = reinterpret_cast<MainLoopCUT*>(context->pMainLoop);
    while (context->emulatorState.frame_counter < 60)
        mainLoop->RunFramePublic();
    return emulator;
}

static void BM_SprinterRender_Logo(benchmark::State& state)
{
    auto emulator = BootSprinterToLogo(state);
    if (!emulator)
        return;
    Screen* screen = emulator->GetContext()->pScreen;
    for (auto _ : state)
        screen->RenderFrameBatch();
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}

static void BM_SprinterFrame_Logo(benchmark::State& state)
{
    auto emulator = BootSprinterToLogo(state);
    if (!emulator)
        return;
    MainLoopCUT* mainLoop = reinterpret_cast<MainLoopCUT*>(emulator->GetContext()->pMainLoop);
    for (auto _ : state)
        mainLoop->RunFramePublic();
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}

BENCHMARK(BM_SprinterRender_Logo)->Iterations(300)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_SprinterFrame_Logo)->Iterations(100)->Unit(benchmark::kMicrosecond);
