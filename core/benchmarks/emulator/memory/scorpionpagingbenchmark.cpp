#include <benchmark/benchmark.h>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"

/// Scorpion ZS-256 memory-subsystem baselines (testing-plan §7, Task 7).
///
///   - ScorpionPagingStorm: bank-switch write storm through the full decoder
///     path (DecodePortOut #7FFD/#1FFD -> UpdateZ80Banks -> ScorpionMemory::
///     UpdateModelBanks)
///   - ScorpionRomReadPath: MemoryReadFast sweep - the ProfROM read-strobe
///     guard (cached bool + addr < 4) executes on every one of these reads
///     through the ScorpionMemory override, so this is the guard-cost
///     baseline for the base machine
static std::shared_ptr<Emulator> CreateScorpionBenchEmulator(const char* name)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    return manager->CreateEmulatorWithModel(name, "SCORPION", LoggerLevel::LogNone);
}

static void ScorpionPagingStorm(benchmark::State& state)
{
    std::shared_ptr<Emulator> emulator = CreateScorpionBenchEmulator("bench-scorpion-paging");
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    PortDecoder* decoder = context->pPortDecoder;
    uint8_t bank = 0;
    for (auto _ : state)
    {
        decoder->DecodePortOut(0x7FFD, bank++, 0x0000);
        decoder->DecodePortOut(0x1FFD, bank, 0x0000);
    }
}
BENCHMARK(ScorpionPagingStorm);

static void ScorpionRomReadPath(benchmark::State& state)
{
    std::shared_ptr<Emulator> emulator = CreateScorpionBenchEmulator("bench-scorpion-read");
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    Memory* memory = emulator->GetContext()->pMemory;
    uint16_t addr = 0;
    uint64_t sink = 0;
    for (auto _ : state)
    {
        sink += memory->MemoryReadFast(addr++, false);
    }
    benchmark::DoNotOptimize(sink);
}
BENCHMARK(ScorpionRomReadPath);
