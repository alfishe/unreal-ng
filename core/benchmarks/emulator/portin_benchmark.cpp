#include <benchmark/benchmark.h>

#include <cstdio>
#include <memory>
#include <string>

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
