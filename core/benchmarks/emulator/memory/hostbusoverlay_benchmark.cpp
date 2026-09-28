#include <benchmark/benchmark.h>

#include <string>

#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "emulator/memory/hostbusoverlay.h"
#include "emulator/memory/memory.h"

/// Host frame cost per model and memory-interface mode
/// (neogs-zxdma-design.md §9). The machine sits in its ROM's BASIC idle loop,
/// so most memory accesses hit #0000-#3FFF: the worst case for anything that
/// watches that window.
///
/// Compare builds with an interleaved A/B run (the machine is often loaded):
///   core-benchmarks --benchmark_filter=BM_HostFrame --benchmark_repetitions=5
namespace
{
/// Sees #0000-#3FFF and changes nothing: the cost of an installed overlay
/// itself, without any device work behind it
struct PassThroughOverlay : HostBusOverlay
{
    PassThroughOverlay() { windowEnd = 0x4000; }
    uint8_t onRead(uint16_t, uint8_t normal, bool, bool) override { return normal; }
    void onWrite(uint16_t, uint8_t, bool) override {}
};
} // namespace

static void RunHostFrame(benchmark::State& state, const char* model, bool debug, bool overlay = false)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-host-frame", model, LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }
    EmulatorContext* context = emulator->GetContext();
    MainLoopCUT* mainLoop = reinterpret_cast<MainLoopCUT*>(context->pMainLoop);
    context->pFeatureManager->setFeature(Features::kScreenHQ, false);
    context->pFeatureManager->setFeature(Features::kSoundHQ, false);
    context->pFeatureManager->setFeature(Features::kDebugMode, debug);
    context->pMemory->UpdateFeatureCache();

    // Boot to the BASIC idle loop
    for (int i = 0; i < 150; i++)
        mainLoop->RunFramePublic();

    PassThroughOverlay passThrough;
    if (overlay)
        context->pCore->SetBusOverlay(&passThrough);

    for (auto _ : state)
        mainLoop->RunFramePublic();

    if (overlay)
        context->pCore->SetBusOverlay(nullptr);
    state.SetLabel(std::string(model) + (debug ? " debug" : " fast") + (overlay ? " + pass-through overlay" : ""));
    state.SetItemsProcessed(state.iterations());
    manager->RemoveEmulator(emulator->GetUUID());
}

static void BM_HostFrame_48K_Fast(benchmark::State& s) { RunHostFrame(s, "48K", false); }
static void BM_HostFrame_48K_Debug(benchmark::State& s) { RunHostFrame(s, "48K", true); }
static void BM_HostFrame_Pentagon_Fast(benchmark::State& s) { RunHostFrame(s, "PENTAGON", false); }
static void BM_HostFrame_Pentagon_Debug(benchmark::State& s) { RunHostFrame(s, "PENTAGON", true); }
static void BM_HostFrame_Scorpion_Fast(benchmark::State& s) { RunHostFrame(s, "SCORPION", false); }
static void BM_HostFrame_Scorpion_Debug(benchmark::State& s) { RunHostFrame(s, "SCORPION", true); }

static void BM_HostFrame_Pentagon_Overlay_Fast(benchmark::State& s) { RunHostFrame(s, "PENTAGON", false, true); }
static void BM_HostFrame_Pentagon_Overlay_Debug(benchmark::State& s) { RunHostFrame(s, "PENTAGON", true, true); }

BENCHMARK(BM_HostFrame_48K_Fast)->Iterations(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_HostFrame_48K_Debug)->Iterations(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_HostFrame_Pentagon_Fast)->Iterations(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_HostFrame_Pentagon_Debug)->Iterations(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_HostFrame_Scorpion_Fast)->Iterations(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_HostFrame_Scorpion_Debug)->Iterations(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_HostFrame_Pentagon_Overlay_Fast)->Iterations(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_HostFrame_Pentagon_Overlay_Debug)->Iterations(1000)->Unit(benchmark::kMicrosecond);
