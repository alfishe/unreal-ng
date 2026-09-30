#include <benchmark/benchmark.h>

#include <memory>

#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/platforms/tsconf/tsconfstate.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
#include "emulator/video/screen.h"

/// TS-Conf frame cost (TSConf implementation-plan BENCH-1): the TS-BIOS Setup
/// screen (TXT mode) rendered per T-state, with the TSU off and with the TSU
/// at its limit (both tile layers and all 85 descriptors as 64x64 sprites),
/// next to a Pentagon frame with the same per-T rendering.
/// Targets: TSU off <= 1.1x the ZX frame, TSU on <= 2x TSU off.
///
/// The machine is often loaded: check vm.loadavg and compare interleaved runs.
///   core-benchmarks --benchmark_filter=BM_TsConfFrame --benchmark_repetitions=5
static std::shared_ptr<Emulator> BootFrameBench(benchmark::State& state, const char* model)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-tsconf-frame", model, LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return nullptr;
    }
    EmulatorContext* context = emulator->GetContext();
    context->pFeatureManager->setFeature(Features::kScreenHQ, true);
    context->pFeatureManager->setFeature(Features::kSoundHQ, false);
    context->pMemory->UpdateFeatureCache();

    MainLoopCUT* mainLoop = reinterpret_cast<MainLoopCUT*>(context->pMainLoop);
    for (int i = 0; i < 150; i++)
        mainLoop->RunFramePublic();
    return emulator;
}

static void RunFrames(benchmark::State& state, const std::shared_ptr<Emulator>& emulator)
{
    MainLoopCUT* mainLoop = reinterpret_cast<MainLoopCUT*>(emulator->GetContext()->pMainLoop);
    for (auto _ : state)
        mainLoop->RunFramePublic();
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}

/// The TSU at its limit on a booted machine (see BM_TsConfFrame_TsuOn)
static bool LoadTsu(benchmark::State& state, const std::shared_ptr<Emulator>& emulator)
{
    auto* decoder = dynamic_cast<PortDecoder_TSConf*>(emulator->GetContext()->pPortDecoder);
    if (!decoder)
    {
        state.SkipWithError("not a TS-Conf decoder");
        return false;
    }
    // Graphics pages are whatever RAM holds (mostly non-zero power-on data):
    // tiles and sprites draw most of their pixels
    TsConfState& ts = decoder->GetState();
    decoder->WriteRegister(TsConfReg::TMapPage, 0x40);
    decoder->WriteRegister(TsConfReg::T0GPage, 0x48);
    decoder->WriteRegister(TsConfReg::T1GPage, 0x50);
    decoder->WriteRegister(TsConfReg::SGPage, 0x58);
    for (uint32_t d = 0; d < 85; d++)
    {
        ts.sfile[d * 3] = static_cast<uint16_t>(0x2000 | (7 << 9) | ((d * 37) & 0x1FF));  // active, 64 high
        ts.sfile[d * 3 + 1] = static_cast<uint16_t>((7 << 9) | ((d * 53) & 0x1FF));      // 64 wide
        ts.sfile[d * 3 + 2] = static_cast<uint16_t>(((d & 15) << 12) | (d * 7));
    }
    decoder->WriteRegister(TsConfReg::TConfig, 0xE0 | 0x0C);  // sprites, T1, T0, tile 0 drawn
    return true;
}

/// The frame renderer alone (TS-O1..O3): one whole frame drawn from the
/// engine's line table, no CPU. Read the minimum of the repetitions on a
/// loaded machine:
///   core-benchmarks --benchmark_filter=BM_TsConfRender --benchmark_repetitions=9
static void RenderOnly(benchmark::State& state, const std::shared_ptr<Emulator>& emulator)
{
    Screen* screen = emulator->GetContext()->pScreen;
    for (auto _ : state)
        screen->RenderFrameBatch();
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}

static void BM_TsConfRender_Setup(benchmark::State& state)
{
    auto emulator = BootFrameBench(state, "TSL");
    if (emulator)
        RenderOnly(state, emulator);
}

static void BM_TsConfRender_Tsu(benchmark::State& state)
{
    auto emulator = BootFrameBench(state, "TSL");
    if (!emulator || !LoadTsu(state, emulator))
        return;
    MainLoopCUT* mainLoop = reinterpret_cast<MainLoopCUT*>(emulator->GetContext()->pMainLoop);
    mainLoop->RunFramePublic();  // the engine fills its TSU line buffers
    RenderOnly(state, emulator);
}

static void BM_TsConfFrame_TsuOff(benchmark::State& state)
{
    auto emulator = BootFrameBench(state, "TSL");
    if (emulator)
        RunFrames(state, emulator);
}

static void BM_TsConfFrame_TsuOn(benchmark::State& state)
{
    auto emulator = BootFrameBench(state, "TSL");
    if (emulator && LoadTsu(state, emulator))
        RunFrames(state, emulator);
}

static void BM_TsConfFrame_PentagonReference(benchmark::State& state)
{
    auto emulator = BootFrameBench(state, "PENTAGON");
    if (emulator)
        RunFrames(state, emulator);
}

BENCHMARK(BM_TsConfFrame_TsuOff)->Iterations(300)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_TsConfFrame_TsuOn)->Iterations(300)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_TsConfFrame_PentagonReference)->Iterations(300)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_TsConfRender_Setup)->Iterations(300)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_TsConfRender_Tsu)->Iterations(300)->Unit(benchmark::kMicrosecond);
