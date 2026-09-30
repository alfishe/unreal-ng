#include <benchmark/benchmark.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "emulator/memory/hostbusoverlay.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"

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
        context->pCore->AddBusOverlay(&passThrough);

    for (auto _ : state)
        mainLoop->RunFramePublic();

    if (overlay)
        context->pCore->RemoveBusOverlay(&passThrough);
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

/// NeoGS fitted, ZX-DMA in each mode (neogs-zxdma-design.md §9):
///  - Idle: the card program never selects the ZX module (mode Off);
///  - Watch: ZxDmaWatch=always, the host in its BASIC idle loop (every ROM
///    access catches the card up - the worst case);
///  - Read: the card runs the module and the host streams #0000-#3FFF with LDIR
enum class NeoGSBench { Idle, Watch, Read };

static void RunNeoGSFrame(benchmark::State& state, NeoGSBench scenario, bool debug)
{
    std::srand(0x4E47); // power-on RAM is rand()-filled: the same machine in every mode
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-neogs-zxdma", "PENTAGON", LoggerLevel::LogNone);
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
    context->config.ngs.zxDmaWatch = scenario == NeoGSBench::Watch ? NeoGSConfig::ZxDmaWatch::Always : NeoGSConfig::ZxDmaWatch::Selected;
    auto* card = context->pSoundManager->switchGeneralSoundCard(GSTypeKind::NGS)
                     ? dynamic_cast<SoundChip_NeoGS*>(context->pSoundManager->getGeneralSound())
                     : nullptr;
    if (!card)
    {
        state.SkipWithError("NeoGS not fitted");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    // Host boots to BASIC first: a running ZX module would feed its ROM fetches
    for (int i = 0; i < 150; i++)
        mainLoop->RunFramePublic();

    // Host: stays in the BASIC idle loop, or streams #0000-#3FFF with LDIR
    if (scenario == NeoGSBench::Read)
    {
        Z80* z80 = context->pCore->GetZ80();
        const uint8_t code[] = {0xF3, 0x21, 0x00, 0x00, 0x11, 0x00, 0x90, 0x01, 0x00, 0x10, 0xED, 0xB0, 0x18, 0xF3}; // DI; loop: LD HL,0; LD DE,#9000; LD BC,#1000; LDIR; JR loop
        for (size_t i = 0; i < sizeof code; i++)
            z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), code[i]);
        z80->pc = 0x8000;
    }

    // Card: start the ZX module at #012000 (Read), or just idle
    std::vector<uint8_t> cardCode = {0xF3};
    if (scenario == NeoGSBench::Read)
        cardCode.insert(cardCode.end(), {0x3E, 0x01, 0xD3, 0x1B, 0x3E, 0x01, 0xD3, 0x1C, 0x3E, 0x20, 0xD3, 0x1D,
                                         0xAF, 0xD3, 0x1E, 0x3E, 0x80, 0xD3, 0x1F});
    cardCode.insert(cardCode.end(), {0x18, 0xFE});
    card->flash().load(cardCode.data(), cardCode.size());
    card->reset();
    for (int i = 0; i < 3; i++)
        mainLoop->RunFramePublic();
    const uint64_t bytesBefore = card->zxDma().bytesRead();

    for (auto _ : state)
        mainLoop->RunFramePublic();

    const char* names[] = {"idle (Off)", "Watch always, BASIC", "Divert, LDIR read"};
    state.SetLabel(std::string("NeoGS ") + names[static_cast<int>(scenario)] + (debug ? " debug" : " fast") +
                   " bytes/frame=" + std::to_string((card->zxDma().bytesRead() - bytesBefore) / state.iterations()));
    state.SetItemsProcessed(state.iterations());
    manager->RemoveEmulator(emulator->GetUUID());
}

static void BM_HostFrame_NeoGS_Idle_Fast(benchmark::State& s) { RunNeoGSFrame(s, NeoGSBench::Idle, false); }
static void BM_HostFrame_NeoGS_Watch_Fast(benchmark::State& s) { RunNeoGSFrame(s, NeoGSBench::Watch, false); }
static void BM_HostFrame_NeoGS_Read_Fast(benchmark::State& s) { RunNeoGSFrame(s, NeoGSBench::Read, false); }
static void BM_HostFrame_NeoGS_Idle_Debug(benchmark::State& s) { RunNeoGSFrame(s, NeoGSBench::Idle, true); }
static void BM_HostFrame_NeoGS_Watch_Debug(benchmark::State& s) { RunNeoGSFrame(s, NeoGSBench::Watch, true); }
static void BM_HostFrame_NeoGS_Read_Debug(benchmark::State& s) { RunNeoGSFrame(s, NeoGSBench::Read, true); }

BENCHMARK(BM_HostFrame_NeoGS_Idle_Fast)->Iterations(300)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_HostFrame_NeoGS_Watch_Fast)->Iterations(300)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_HostFrame_NeoGS_Read_Fast)->Iterations(300)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_HostFrame_NeoGS_Idle_Debug)->Iterations(300)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_HostFrame_NeoGS_Watch_Debug)->Iterations(300)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_HostFrame_NeoGS_Read_Debug)->Iterations(300)->Unit(benchmark::kMicrosecond);
