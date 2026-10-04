#include <benchmark/benchmark.h>

#include <memory>
#include <string>

#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/rom.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/ports/models/sprinter/sprinterbios.h"
#include "emulator/video/screen.h"

/// Sprinter frame cost (Sprinter tdd-video §3, the naive v1 renderer): BIOS 3.04
/// (selected explicitly, so the numbers stay comparable; the shipped default is 3.06 Hotfix 2) at its logo (frame 60: graphics squares for the logo, 80-column text, border
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
    SprinterBios::Options bios;
    bios.bios = "3.04";
    std::string error;
    if (!SprinterBios::ApplyToConfig(context->config, bios, error) || !context->pCore->GetROM()->LoadROM())
    {
        state.SkipWithError(("BIOS 3.04 not loaded: " + error).c_str());
        EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
        return nullptr;
    }
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

/// The ZX mode's "original waits" (tdd-zx-mode.md §3.3, T-ZX-10): a frame of LD A,(#4000) / JP loops at
/// 3.5 MHz from window 2 with ALL_MODE bit 2 set (/0: the overlay is not installed - what every other mode pays)
/// and clear (/1: every read of #4000 goes through SprinterOrigWaits)
static void BM_SprinterFrame_ScreenReads(benchmark::State& state)
{
    auto emulator = BootSprinterToLogo(state);
    if (!emulator)
        return;
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoder_Sprinter*>(context->pPortDecoder);
    Z80* z80 = context->pCore->GetZ80();
    const uint8_t loop[] = {0x3A, 0x00, 0x40, 0xC3, 0x00, 0x80};  // LD A,(#4000) : JP #8000
    for (uint16_t i = 0; i < sizeof(loop); i++)
        context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), loop[i]);
    z80->pc = 0x8000;
    z80->iff1 = z80->iff2 = 0;
    SprinterPldState& pld = decoder->GetPldState();
    pld.turbo = 0;
    pld.allMode = state.range(0) ? 0xFA : 0xFE;
    decoder->OnTtdStateLoaded();  // the clock and both wait overlays follow the PLD state
    if (decoder->OrigWaitsActive() != (state.range(0) != 0))
    {
        state.SkipWithError("the original waits did not follow ALL_MODE");
        return;
    }
    MainLoopCUT* mainLoop = reinterpret_cast<MainLoopCUT*>(context->pMainLoop);
    for (auto _ : state)
        mainLoop->RunFramePublic();
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}

BENCHMARK(BM_SprinterFrame_ScreenReads)->Arg(0)->Arg(1)->Iterations(100)->Unit(benchmark::kMicrosecond);
