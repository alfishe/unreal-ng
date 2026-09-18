#include <benchmark/benchmark.h>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"

/// TurboSound per-frame baseline (TSFM implementation plan, P0).
///
/// Measures the full-machine frame cost on Pentagon 128 with the current
/// legacy TurboSound device at the default core rate (44.1 kHz) and HQ DSP:
///  - Idle: machine halted, no sound port traffic
///  - PlayerLoad: a Z80 driver that pokes 64 register/data pairs to
///    #FFFD/#BFFD per frame with status polls before each write, the same
///    write/poll shape the TFM Music Compiler player uses (sub_628A /
///    the outi pair loop at 0x62DF). The pair count calibrates the sustained
///    register traffic of a 6-channel TSFM tune (see
///    verification/perf-baseline.md).
///
/// These numbers are the denominator for the P2 gate (interface adoption must
/// not change turbo frame cost) and the reference point for the TSFM chip
/// core budget (design §12.6).
namespace
{
/// Hand-assembled driver at 0x8000 (see comment block for the shape):
///   ld hl,table; ld de,0xffbf; ld c,0xfd; ld a,64
/// loop: ld b,d; [poll #FFFD]; outi (addr); ld b,d; [poll #FFFD]; ld b,e; outi (data);
///   dec a; jr nz,loop; ei; halt; jr entry
/// Data values keep bit 7 clear so the legacy device's register-data reads
/// never park the poll loops; the table lives at 0x8021.
constexpr uint16_t DRIVER_BASE = 0x8000;
constexpr uint16_t DRIVER_TABLE = 0x8021;
constexpr uint8_t DRIVER_PAIRS = 64;

const uint8_t DRIVER_CODE[] = {
    0x21, 0x21, 0x80,             // ld hl,0x8021
    0x11, 0xBF, 0xFF,             // ld de,0xffbf
    0x0E, 0xFD,                   // ld c,0xfd
    0x3E, DRIVER_PAIRS,           // ld a,64
    0x42,                         // loop: ld b,d
    0xED, 0x70,                   // w0: in f,(c)
    0xFA, 0x0B, 0x80,             // jp m,w0
    0xED, 0xA3,                   // outi (addr -> #FFFD)
    0x42,                         // ld b,d
    0xED, 0x70,                   // w1: in f,(c)
    0xFA, 0x12, 0x80,             // jp m,w1
    0x43,                         // ld b,e
    0xED, 0xA3,                   // outi (data -> #BFFD)
    0x3D,                         // dec a
    0x20, 0xED,                   // jr nz,loop
    0xFB,                         // ei
    0x76,                         // halt (wakes on INT)
    0x18, 0xDF,                   // jr entry
};

void PokeDriver(Z80* z80)
{
    size_t offset = 0;
    for (uint8_t byte : DRIVER_CODE)
        z80->DirectWrite(static_cast<uint16_t>(DRIVER_BASE + offset++), byte);

    for (uint8_t i = 0; i < DRIVER_PAIRS; i++)
    {
        // Register/data pair; data keeps bit 7 clear so the legacy device's
        // register-data status reads never park the poll loops
        z80->DirectWrite(static_cast<uint16_t>(DRIVER_TABLE + i * 2), static_cast<uint8_t>(i & 0x0F));
        z80->DirectWrite(static_cast<uint16_t>(DRIVER_TABLE + i * 2 + 1), static_cast<uint8_t>((i * 5) & 0x7F));
    }

    z80->pc = DRIVER_BASE;
    z80->sp = 0xFF00;
    z80->im = 1;
    z80->iff1 = 0;
    z80->iff2 = 0;
    z80->halted = 0;
}

void RunFrameCostBenchmark(benchmark::State& state, bool playerLoad, bool turbo, size_t coreRate = 0)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-turbosound", "PENTAGON", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    // CUT reinterpret_cast is the established idiom (turbo_frame_benchmark):
    // the wrapper adds no data members, and this target defines
    // _CODE_UNDER_BENCHMARK so the protected RunFrame() becomes reachable
    MainLoopCUT* mainLoop = context ? reinterpret_cast<MainLoopCUT*>(context->pMainLoop) : nullptr;
    Core* core = context ? context->pCore : nullptr;
    if (!mainLoop || !core)
    {
        state.SkipWithError("main loop or core unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    // Core-rate variants: the output stage synthesizes at the core rate, so
    // its per-frame cost scales with it (192 k = 4.35x the outputs of 44.1 k).
    // Applied at the next frame boundary - the warm-up frames absorb it.
    if (coreRate != 0 && context->pSoundManager)
        context->pSoundManager->requestCoreRate(static_cast<uint32_t>(coreRate));

    if (playerLoad)
        PokeDriver(core->GetZ80());
    else
        core->GetZ80()->halted = 1;  // Idle: keep the CPU parked

    // Warm-up: table builds, first-frame paths, driver steady state
    for (int i = 0; i < 10; i++)
        mainLoop->RunFramePublic();

    if (turbo)
        core->EnableTurboMode();

    for (auto _ : state)
        mainLoop->RunFramePublic();

    std::string label = playerLoad ? "player-load" : "idle";
    if (coreRate != 0)
        label += ", core " + std::to_string(coreRate) + " Hz";
    if (turbo)
        label += ", turbo";
    state.SetLabel(label);
    state.SetItemsProcessed(state.iterations());

    if (turbo)
        core->DisableTurboMode();

    manager->RemoveEmulator(emulator->GetUUID());
}
}  // namespace

static void BM_TurboSoundFrame_Idle(benchmark::State& state)
{
    RunFrameCostBenchmark(state, false, false);
}

static void BM_TurboSoundFrame_PlayerLoad(benchmark::State& state)
{
    RunFrameCostBenchmark(state, true, false);
}

static void BM_TurboSoundFrame_PlayerLoad_Turbo(benchmark::State& state)
{
    RunFrameCostBenchmark(state, true, true);
}

BENCHMARK(BM_TurboSoundFrame_Idle)->Iterations(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_TurboSoundFrame_PlayerLoad)->Iterations(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_TurboSoundFrame_PlayerLoad_Turbo)->Iterations(2000)->Unit(benchmark::kMicrosecond);

// ---- Core-rate ladder: the same idle / player-load frames with the output
// stage running at 44.1 k, 96 k and 192 k. Compare against the MainLoop
// frame diagnostics ("step-sound") and BM_Decimator_* (the FIR share). ----
static void BM_TurboSoundFrame_Idle_CoreRate(benchmark::State& state)
{
    RunFrameCostBenchmark(state, false, false, static_cast<size_t>(state.range(0)));
}

static void BM_TurboSoundFrame_PlayerLoad_CoreRate(benchmark::State& state)
{
    RunFrameCostBenchmark(state, true, false, static_cast<size_t>(state.range(0)));
}

BENCHMARK(BM_TurboSoundFrame_Idle_CoreRate)->Arg(44100)->Arg(96000)->Arg(192000)->Iterations(500)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_TurboSoundFrame_PlayerLoad_CoreRate)->Arg(44100)->Arg(96000)->Arg(192000)->Iterations(500)->Unit(benchmark::kMicrosecond);
