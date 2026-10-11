#include <benchmark/benchmark.h>

#include <memory>

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

/// What one edit of the machine by a tool costs (Emulator::EditMemoryFromTool): a script's poke in a loop, the
/// debugger's memory write. Live (no TTD session): since M6 an operation of the machine coordinator (the rules, the
/// log) instead of a bare call; the machine is not running, so no park either way.
///
///   core-benchmarks --benchmark_filter=BM_ToolEdit
static void BM_ToolEdit_LivePoke(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-tool-edit", "PENTAGON",
                                                                          LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }
    Memory* memory = emulator->GetMemory();
    uint8_t value = 0;
    for (auto _ : state)
    {
        emulator->EditMemoryFromTool("bench", [&]() { memory->DirectWriteToZ80Memory(0x8000, value); });
        ++value;
    }
    benchmark::DoNotOptimize(value);
    manager->RemoveEmulator(emulator->GetUUID());
}
BENCHMARK(BM_ToolEdit_LivePoke)->Iterations(200000)->Unit(benchmark::kNanosecond);
