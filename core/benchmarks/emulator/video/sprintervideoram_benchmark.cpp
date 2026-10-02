#include <benchmark/benchmark.h>

#include <cstdint>

#include "emulator/video/sprinter/sprintervideoram.h"

/// SprinterVideoRam::Write, the Sprinter's per-byte video RAM path (every graphics-page store
/// and every Spectrum-shadow store goes through it). Two shapes:
///   _Screen: changing bytes in columns #000-#2FF (pictures, fonts) - the common case, which
///            the video change log (Sprinter automation audit G6) must not slow down;
///   _Tables: changing bytes in the mode table and palette columns (#300-#3FF) - the cold
///            branch that refreshes pens, the frame INT and notes the change log.
/// The listeners are set as in PortDecoder_Sprinter (empty catch-up, a counting table hook).
/// Used for the A/B of the change-log hook (performance-guidelines.md section 4):
///   core-benchmarks --benchmark_filter=BM_SprinterVideoRamWrite --benchmark_repetitions=5
namespace
{
constexpr uint32_t kWritesPerIteration = 64 * 1024;

void Wire(SprinterVideoRam& vram, uint64_t& notes)
{
    vram.SetBeforeChangeListener([]() {});
    vram.SetIntModeListener([]() {});
    vram.SetTableWriteListener([&notes](uint32_t, bool) { notes++; });
}
}  // namespace

static void BM_SprinterVideoRamWrite_Screen(benchmark::State& state)
{
    SprinterVideoRam vram;
    uint64_t notes = 0;
    Wire(vram, notes);
    uint8_t value = 1;
    for (auto _ : state)
    {
        for (uint32_t i = 0; i < kWritesPerIteration; i++)
        {
            const uint32_t row = (i / 0x300) & 0xFF;
            vram.Write(row * SprinterVideoRam::kRowBytes + (i % 0x300), value);
        }
        value = static_cast<uint8_t>(value + 1);  // every write changes its byte
        benchmark::DoNotOptimize(vram.Data());
    }
    benchmark::DoNotOptimize(notes);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * kWritesPerIteration);
}

static void BM_SprinterVideoRamWrite_Tables(benchmark::State& state)
{
    SprinterVideoRam vram;
    uint64_t notes = 0;
    Wire(vram, notes);
    uint8_t value = 1;
    for (auto _ : state)
    {
        for (uint32_t i = 0; i < kWritesPerIteration; i++)
        {
            const uint32_t row = (i / 0x100) & 0xFF;
            vram.Write(row * SprinterVideoRam::kRowBytes + 0x300 + (i & 0xFF), value);
        }
        value = static_cast<uint8_t>(value + 1);
        benchmark::DoNotOptimize(vram.Data());
    }
    benchmark::DoNotOptimize(notes);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * kWritesPerIteration);
}

BENCHMARK(BM_SprinterVideoRamWrite_Screen)->Iterations(200)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_SprinterVideoRamWrite_Tables)->Iterations(200)->Unit(benchmark::kMicrosecond);
