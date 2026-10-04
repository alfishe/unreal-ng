#include <benchmark/benchmark.h>

#include <cstdio>
#include <memory>
#include <string>

#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"

/// CPU cost of the I/O read path (Z80::inFromBus): the port decoder, the ULA's I/O waits, the floating bus
/// (docs/inprogress/2026-09-30-fusetest-core-defects). A block of `IN A,(C)` runs through Z80Step with
/// interrupts off, the T-state counter rewound every block so every read falls in the picture.
///
/// Arguments: model index (kModels) x port (0 = #00FF: no device, uncontended high byte, the floating bus;
/// 1 = #40FF: the same with the high byte in contended memory, ULA waits after IORQ; 2 = #00FE: the ULA port;
/// 3 = #FFFD with AY register 14 selected and port A an input: the I/O port read, where the 128K-family boards
/// add their wiring (Spectrum128AyIoPort) and every other machine reads the chip's pull-ups)
namespace
{
const char* const kModels[] = { "48K", "128k", "PENTAGON" };
const uint16_t kPorts[] = { 0x00FF, 0x40FF, 0x00FE, 0xFFFD };
constexpr int kBlock = 1000;
}  // namespace

static void BM_PortIn(benchmark::State& state)
{
    const char* model = kModels[state.range(0)];
    const uint16_t port = kPorts[state.range(1)];

    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-portin", model, LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    Z80* z80 = context->pCore->GetZ80();
    Memory* memory = context->pMemory;
    context->pScreen->InitFrame();
    context->config.floatbus = 1;

    // kBlock x IN A,(C), then JP #8000
    uint16_t a = 0x8000;
    for (int i = 0; i < kBlock; i++)
    {
        memory->DirectWriteToZ80Memory(a++, 0xED);
        memory->DirectWriteToZ80Memory(a++, 0x78);
    }
    memory->DirectWriteToZ80Memory(a++, 0xC3);
    memory->DirectWriteToZ80Memory(a++, 0x00);
    memory->DirectWriteToZ80Memory(a, 0x80);

    // #FFFD reads the selected AY register: select R14 (port A, an input after reset)
    if (SoundChip_AY8910* ay = context->pSoundManager ? context->pSoundManager->getAYChip(0) : nullptr)
        ay->setRegister(14);

    const uint32_t start = context->config.intstart + 1 + 14300;
    z80->iff1 = 0;
    z80->iff2 = 0;

    for (auto _ : state)
    {
        z80->pc = 0x8000;
        z80->bc = port;
        z80->t = start;
        for (int i = 0; i < kBlock; i++)
            z80->Z80Step();
        uint8_t v = z80->a;
        benchmark::DoNotOptimize(v);
    }

    state.SetItemsProcessed(state.iterations() * kBlock);
    char label[64];
    std::snprintf(label, sizeof(label), "%s IN #%04X", model, port);
    state.SetLabel(label);
    manager->RemoveEmulator(emulator->GetUUID());
}
BENCHMARK(BM_PortIn)->ArgsProduct({ { 0, 1, 2 }, { 0, 1, 2, 3 } })->Unit(benchmark::kMicrosecond);

/// CPU cost of the I/O write path (Z80::out): the full-decode observer tap, the port decoder, the ULA's I/O waits.
/// A block of `OUT (C),A` as BM_PortIn. Arguments: model index (kModels) x port (kPorts[0..2]; #00FE writes the border)
static void BM_PortOut(benchmark::State& state)
{
    const char* model = kModels[state.range(0)];
    const uint16_t port = kPorts[state.range(1)];

    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-portout", model, LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    Z80* z80 = context->pCore->GetZ80();
    Memory* memory = context->pMemory;
    context->pScreen->InitFrame();

    // kBlock x OUT (C),A, then JP #8000
    uint16_t a = 0x8000;
    for (int i = 0; i < kBlock; i++)
    {
        memory->DirectWriteToZ80Memory(a++, 0xED);
        memory->DirectWriteToZ80Memory(a++, 0x79);
    }
    memory->DirectWriteToZ80Memory(a++, 0xC3);
    memory->DirectWriteToZ80Memory(a++, 0x00);
    memory->DirectWriteToZ80Memory(a, 0x80);

    const uint32_t start = context->config.intstart + 1 + 14300;
    z80->iff1 = 0;
    z80->iff2 = 0;

    for (auto _ : state)
    {
        z80->pc = 0x8000;
        z80->bc = port;
        z80->a = 0x07;
        z80->t = start;
        for (int i = 0; i < kBlock; i++)
            z80->Z80Step();
        uint32_t t = z80->t;
        benchmark::DoNotOptimize(t);
    }

    state.SetItemsProcessed(state.iterations() * kBlock);
    char label[64];
    std::snprintf(label, sizeof(label), "%s OUT #%04X", model, port);
    state.SetLabel(label);
    manager->RemoveEmulator(emulator->GetUUID());
}
BENCHMARK(BM_PortOut)->ArgsProduct({ { 0, 1, 2 }, { 0, 1, 2 } })->Unit(benchmark::kMicrosecond);

/// The port path on a machine with a full-decode card (ZXM-MoonSound, low-byte observer on #C4-#C7 / #7E / #7F)
/// fitted or not: the ZX-bus slots claim table (docs/inprogress/2026-10-03-zx-bus-slots/tdd.md §3, SL-2).
///
/// Arguments: model (kCardModels) x card (0 = none, 1 = MoonSound) x access (0 = IN, 1 = OUT) x port
/// (0 = #00FD: no card claims it; 1 = #00C4: the card's FM status / address port, claimed when the card is fitted).
/// The shipped Pentagon and ATM3 configs fit the MoonSound; the config hook sets it per run.
namespace
{
const char* const kCardModels[] = { "PENTAGON", "ATM3" };
const uint16_t kCardPorts[] = { 0x00FD, 0x00C4 };
}  // namespace

static void BM_PortCard(benchmark::State& state)
{
    const char* model = kCardModels[state.range(0)];
    const bool card = state.range(1) != 0;
    const bool out = state.range(2) != 0;
    const uint16_t port = kCardPorts[state.range(3)];

    const Config::ConfigLoadedHook previous = Config::GetConfigLoadedHook();
    Config::SetConfigLoadedHook([previous, card](CONFIG& config)
    {
        if (previous)
            previous(config);
        config.sound.moonsound = card ? 1 : 0;
    });
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-portcard", model, LoggerLevel::LogNone);
    Config::SetConfigLoadedHook(previous);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (card != (context->pSoundManager && context->pSoundManager->hasMoonSound()))
    {
        state.SkipWithError("MoonSound fitment differs from the request");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }
    Z80* z80 = context->pCore->GetZ80();
    Memory* memory = context->pMemory;
    context->pScreen->InitFrame();
    context->config.floatbus = 1;

    // kBlock x IN A,(C) / OUT (C),A, then JP #8000
    uint16_t a = 0x8000;
    for (int i = 0; i < kBlock; i++)
    {
        memory->DirectWriteToZ80Memory(a++, 0xED);
        memory->DirectWriteToZ80Memory(a++, out ? 0x79 : 0x78);
    }
    memory->DirectWriteToZ80Memory(a++, 0xC3);
    memory->DirectWriteToZ80Memory(a++, 0x00);
    memory->DirectWriteToZ80Memory(a, 0x80);

    const uint32_t start = context->config.intstart + 1 + 14300;
    z80->iff1 = 0;
    z80->iff2 = 0;

    for (auto _ : state)
    {
        z80->pc = 0x8000;
        z80->bc = port;
        z80->a = 0x01;
        z80->t = start;
        for (int i = 0; i < kBlock; i++)
            z80->Z80Step();
        uint8_t v = z80->a;
        benchmark::DoNotOptimize(v);
    }

    state.SetItemsProcessed(state.iterations() * kBlock);
    char label[64];
    std::snprintf(label, sizeof(label), "%s %s %s #%04X", model, card ? "MoonSound" : "no card", out ? "OUT" : "IN",
                  port);
    state.SetLabel(label);
    manager->RemoveEmulator(emulator->GetUUID());
}
BENCHMARK(BM_PortCard)
    ->ArgsProduct({ { 0, 1 }, { 0, 1 }, { 0, 1 }, { 0, 1 } })
    ->Unit(benchmark::kMicrosecond);
