/// @file cpu_speed_probe_benchmark.cpp
/// @brief Reference numbers for the environment probes printed by the MainLoop
/// frame diagnostics ("env: ... 10M-LCG probe N us, clock read N ns"): the same
/// fixed integer workload and clock read, measured in this process. If the
/// frontend prints a probe several times slower than this, its emulation
/// thread runs in a slower environment (throttled core, contention, ...)
/// rather than executing more work.
///
/// Run with: ./core-benchmarks --benchmark_filter="BM_CpuSpeedProbe.*"

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdint>

static void BM_CpuSpeedProbe_LCG10M(benchmark::State& state)
{
    uint64_t x = 0x9E3779B97F4A7C15ULL;
    for (auto _ : state)
    {
        for (uint32_t i = 0; i < 10'000'000; i++)
            x = x * 6364136223846793005ULL + 1442695040888963407ULL;
        benchmark::DoNotOptimize(x);
    }
}

static void BM_CpuSpeedProbe_ClockRead(benchmark::State& state)
{
    uint64_t sink = 0;
    for (auto _ : state)
    {
        sink += static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    }
    benchmark::DoNotOptimize(sink);
}

BENCHMARK(BM_CpuSpeedProbe_LCG10M)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_CpuSpeedProbe_ClockRead)->Unit(benchmark::kNanosecond);
