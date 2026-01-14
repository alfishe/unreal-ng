/// @file videowall_benchmark.cpp
/// @brief Benchmarks for videowall multi-instance snapshot loading
/// @date 2026-01-13
///
/// POC benchmark for 180 emulator instances loading snapshots in parallel
/// to validate batch loading strategies for videowall use case.
///
/// Target: Load 180 snapshots within 20-40ms (1-2 frames at 50Hz)

#include <benchmark/benchmark.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "benchmark_path_helper.h"
#include "common/filehelper.h"
#include "emulator/emulator.h"
#include "stdafx.h"

/// region <Configuration>

// Test configurations - adjust based on available cores
constexpr int VIDEOWALL_1X = 180;    // 4K at 1x resolution
constexpr int VIDEOWALL_2X = 48;     // 4K at 2x resolution
constexpr int VIDEOWALL_SMALL = 16;  // Minimum test case

/// endregion </Configuration>

/// region <Helper Functions>

/// @brief Create emulator pool with features optimized for videowall
/// @param count Number of emulators to create
/// @param disableSound Disable sound feature for faster operation
/// @return Vector of initialized emulators
static std::vector<Emulator*> CreateEmulatorPool(int count, bool disableSound = true)
{
    std::vector<Emulator*> pool;
    pool.reserve(count);

    for (int i = 0; i < count; i++)
    {
        Emulator* emulator = new Emulator(LoggerLevel::LogError);
        if (emulator->Init())
        {
            if (disableSound)
            {
                // Disable sound for videowall (saves ~18% CPU per instance)
                emulator->GetContext()->pFeatureManager->setFeature("sound", false);
                emulator->GetContext()->pFeatureManager->setFeature("soundhq", false);
            }
            // Disable debug features
            emulator->GetContext()->pFeatureManager->setFeature("debugmode", false);
            emulator->GetContext()->pFeatureManager->setFeature("breakpoints", false);
            emulator->GetContext()->pFeatureManager->setFeature("calltrace", false);
            emulator->GetContext()->pFeatureManager->setFeature("memorytracking", false);

            pool.push_back(emulator);
        }
        else
        {
            delete emulator;
        }
    }

    return pool;
}

/// @brief Destroy emulator pool
static void DestroyEmulatorPool(std::vector<Emulator*>& pool)
{
    for (auto* emulator : pool)
    {
        if (emulator)
        {
            emulator->Release();
            delete emulator;
        }
    }
    pool.clear();
}

/// endregion </Helper Functions>

/// region <Sequential Loading Benchmarks>

/// @brief Sequential loading - baseline (worst case)
/// All 180 emulators load one after another
template <int N>
static void BM_Videowall_Sequential(benchmark::State& state)
{
    static std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/sna/Timing_Tests-48k_v1.0.sna");

    // Pre-create emulator pool (initialization time not measured)
    auto pool = CreateEmulatorPool(N);
    if (pool.size() < static_cast<size_t>(N))
    {
        state.SkipWithError("Failed to create full emulator pool");
        return;
    }

    for (auto _ : state)
    {
        for (auto* emulator : pool)
        {
            bool result = emulator->LoadSnapshot(snapshotPath);
            benchmark::DoNotOptimize(result);
        }
    }

    state.SetItemsProcessed(state.iterations() * N);

    DestroyEmulatorPool(pool);
}

BENCHMARK_TEMPLATE(BM_Videowall_Sequential, VIDEOWALL_SMALL)->Iterations(10)->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Videowall_Sequential, VIDEOWALL_2X)->Iterations(10)->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Videowall_Sequential, VIDEOWALL_1X)->Iterations(5)->Unit(benchmark::kMillisecond);

/// endregion </Sequential Loading Benchmarks>

/// region <Parallel Loading Benchmarks>

/// @brief Parallel loading using std::thread
/// Each emulator loads in its own thread (simulates emulator-per-thread architecture)
template <int N>
static void BM_Videowall_Parallel_StdThread(benchmark::State& state)
{
    std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/sna/Timing_Tests-48k_v1.0.sna");

    auto pool = CreateEmulatorPool(N);
    if (pool.size() < static_cast<size_t>(N))
    {
        state.SkipWithError("Failed to create full emulator pool");
        return;
    }

    for (auto _ : state)
    {
        std::vector<std::thread> threads;
        threads.reserve(N);

        for (auto* emulator : pool)
        {
            threads.emplace_back([emulator, &snapshotPath]() {
                bool result = emulator->LoadSnapshot(snapshotPath);
                benchmark::DoNotOptimize(result);
            });
        }

        for (auto& t : threads)
        {
            t.join();
        }
    }

    state.SetItemsProcessed(state.iterations() * N);

    DestroyEmulatorPool(pool);
}

BENCHMARK_TEMPLATE(BM_Videowall_Parallel_StdThread, VIDEOWALL_SMALL)->Iterations(10)->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Videowall_Parallel_StdThread, VIDEOWALL_2X)->Iterations(10)->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Videowall_Parallel_StdThread, VIDEOWALL_1X)->Iterations(5)->Unit(benchmark::kMillisecond);

/// endregion </Parallel Loading Benchmarks>

/// region <Thread Pool Simulation>

/// @brief Parallel loading with limited thread count (simulates real cores)
/// Tests with 4, 6, 8, 12 threads to find optimal parallelism
template <int N, int THREADS>
static void BM_Videowall_ThreadPool(benchmark::State& state)
{
    std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/sna/Timing_Tests-48k_v1.0.sna");

    auto pool = CreateEmulatorPool(N);
    if (pool.size() < static_cast<size_t>(N))
    {
        state.SkipWithError("Failed to create full emulator pool");
        return;
    }

    for (auto _ : state)
    {
        std::atomic<int> nextIndex{0};
        std::vector<std::thread> threads;
        threads.reserve(THREADS);

        // Worker threads that pull work from shared counter
        for (int t = 0; t < THREADS; t++)
        {
            threads.emplace_back([&pool, &nextIndex, &snapshotPath]() {
                while (true)
                {
                    int idx = nextIndex.fetch_add(1);
                    if (idx >= static_cast<int>(pool.size()))
                        break;

                    bool result = pool[idx]->LoadSnapshot(snapshotPath);
                    benchmark::DoNotOptimize(result);
                }
            });
        }

        for (auto& t : threads)
        {
            t.join();
        }

        // Reset for next iteration
        nextIndex = 0;
    }

    state.SetItemsProcessed(state.iterations() * N);
    state.counters["threads"] = THREADS;

    DestroyEmulatorPool(pool);
}

// Test with different thread counts for VIDEOWALL_2X (48 instances)
BENCHMARK_TEMPLATE(BM_Videowall_ThreadPool, VIDEOWALL_2X, 4)->Iterations(10)->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Videowall_ThreadPool, VIDEOWALL_2X, 6)->Iterations(10)->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Videowall_ThreadPool, VIDEOWALL_2X, 8)->Iterations(10)->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Videowall_ThreadPool, VIDEOWALL_2X, 12)->Iterations(10)->Unit(benchmark::kMillisecond);

// Test with different thread counts for VIDEOWALL_1X (180 instances)
BENCHMARK_TEMPLATE(BM_Videowall_ThreadPool, VIDEOWALL_1X, 4)->Iterations(5)->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Videowall_ThreadPool, VIDEOWALL_1X, 6)->Iterations(5)->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Videowall_ThreadPool, VIDEOWALL_1X, 8)->Iterations(5)->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Videowall_ThreadPool, VIDEOWALL_1X, 12)->Iterations(5)->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Videowall_ThreadPool, VIDEOWALL_1X, 16)->Iterations(5)->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Videowall_ThreadPool, VIDEOWALL_1X, 20)->Iterations(5)->Unit(benchmark::kMillisecond);

/// endregion </Thread Pool Simulation>

/// region <Mixed Snapshot Types>

/// @brief Test with different snapshot types (SNA vs Z80)
static void BM_Videowall_MixedSnapshots(benchmark::State& state)
{
    std::vector<std::string> snapshotPaths = {
        BenchmarkPathHelper::GetTestDataPath("loaders/sna/Timing_Tests-48k_v1.0.sna"),
        BenchmarkPathHelper::GetTestDataPath("loaders/z80/Testing80.z80")};

    constexpr int N = VIDEOWALL_2X;
    auto pool = CreateEmulatorPool(N);
    if (pool.size() < N)
    {
        state.SkipWithError("Failed to create full emulator pool");
        return;
    }

    for (auto _ : state)
    {
        std::vector<std::thread> threads;
        threads.reserve(N);

        for (int i = 0; i < N; i++)
        {
            const std::string& path = snapshotPaths[i % snapshotPaths.size()];
            threads.emplace_back([emu = pool[i], &path]() {
                bool result = emu->LoadSnapshot(path);
                benchmark::DoNotOptimize(result);
            });
        }

        for (auto& t : threads)
        {
            t.join();
        }
    }

    state.SetItemsProcessed(state.iterations() * N);

    DestroyEmulatorPool(pool);
}
BENCHMARK(BM_Videowall_MixedSnapshots)->Iterations(10)->Unit(benchmark::kMillisecond);

/// endregion </Mixed Snapshot Types>
