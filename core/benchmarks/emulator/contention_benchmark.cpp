#include <benchmark/benchmark.h>

#include <memory>
#include <string>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"

/// CPU cost of the memory access path per model - the performance gate of the contention rework
/// (docs/inprogress/2026-09-28-m1-contention/design.md §9). An instruction mix of data reads and writes,
/// stack, indexed and block accesses, port in/out and a jump runs through Z80Step with interrupts off,
/// the T-state counter rewound every block so every access sits inside the frame.
///
/// Arguments: model index (kModels) x placement (0 = code and data in uncontended RAM, 1 = code in
/// contended RAM #6000). On the Pentagon both placements must stay within noise of the baseline after
/// the rework (no contention cost on uncontended machines); on the 48K / +3 placement 1 reports the cost
/// of contended fetches.
namespace
{
const uint8_t kMix[] = {
    0x7E, 0x12, 0x23, 0x13, 0xC5, 0xC1, 0xE3, 0xE3, 0x34, 0xDD, 0xCB, 0x01, 0x46,
    0xFD, 0x77, 0x02, 0xED, 0xA0, 0x01, 0xFE, 0x40, 0xED, 0x78, 0xED, 0x79, 0x18, 0xE5,
};

const char* const kModels[] = { "PENTAGON", "48K", "PLUS3" };
constexpr int kBlock = 1000;  // instructions between T-state rewinds (~25-30 kT)
}  // namespace

static void BM_ContentionInstructionMix(benchmark::State& state)
{
    const char* model = kModels[state.range(0)];
    const bool codeContended = state.range(1) != 0;

    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-contention", model, LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    Z80* z80 = context->pCore->GetZ80();
    Memory* memory = context->pMemory;
    context->pScreen->InitFrame();

    const uint16_t code = codeContended ? 0x6000 : 0x8000;
    for (size_t i = 0; i < sizeof(kMix); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(code + i), kMix[i]);

    const uint32_t start = context->config.intstart + 1 + 14000;
    z80->pc = code;
    z80->hl = 0x9000;
    z80->de = 0xA000;
    z80->ix = 0x9100;
    z80->iy = 0x9200;
    z80->sp = 0xB000;
    z80->iff1 = 0;
    z80->iff2 = 0;

    for (auto _ : state)
    {
        z80->t = start;
        z80->hl = 0x9000;
        z80->de = 0xA000;
        for (int i = 0; i < kBlock; i++)
            z80->Z80Step();
        uint32_t t = z80->t;
        benchmark::DoNotOptimize(t);
    }

    state.SetItemsProcessed(state.iterations() * kBlock);
    state.SetLabel(std::string(model) + (codeContended ? " code@6000" : " code@8000"));
    manager->RemoveEmulator(emulator->GetUUID());
}
BENCHMARK(BM_ContentionInstructionMix)
    ->ArgsProduct({ { 0, 1, 2 }, { 0, 1 } })
    ->MinWarmUpTime(0.2)
    ->Unit(benchmark::kMicrosecond);
