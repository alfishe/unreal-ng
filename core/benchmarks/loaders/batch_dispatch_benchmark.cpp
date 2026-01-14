/// @file batch_dispatch_benchmark.cpp
/// @brief Benchmarks comparing different batch command dispatch strategies
/// @date 2026-01-13
///
/// Tests two dispatch patterns:
/// - Current: Direct LoadSnapshot from caller thread (with pause/resume)
/// - Simulated Batch: Pause all → Load all (pool threads) → Resume all

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

constexpr int VIDEOWALL_SIZE = 48;  // Use smaller for faster iterations
constexpr int POOL_THREADS = 4;

/// endregion </Configuration>

/// region <Helper Functions>

/// @brief Create emulator pool with videowall-optimized features
static std::vector<Emulator*> CreateEmulatorPool(int count)
{
    std::vector<Emulator*> pool;
    pool.reserve(count);

    for (int i = 0; i < count; i++)
    {
        Emulator* emulator = new Emulator(LoggerLevel::LogError);
        if (emulator->Init())
        {
            // Disable sound and debug features
            auto* fm = emulator->GetContext()->pFeatureManager;
            fm->setFeature("sound", false);
            fm->setFeature("soundhq", false);
            fm->setFeature("debugmode", false);
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

/// region <Current Pattern: Direct LoadSnapshot>

/// @brief Current pattern: Each LoadSnapshot does its own pause/resume
/// This is what we have today - baseline for comparison
template <int N, int THREADS>
static void BM_Direct_LoadSnapshot(benchmark::State& state)
{
    std::string path = BenchmarkPathHelper::GetTestDataPath("loaders/sna/Timing_Tests-48k_v1.0.sna");

    auto pool = CreateEmulatorPool(N);
    if (pool.size() < static_cast<size_t>(N))
    {
        state.SkipWithError("Failed to create emulator pool");
        return;
    }

    for (auto _ : state)
    {
        std::atomic<int> nextIndex{0};
        std::vector<std::thread> threads;
        threads.reserve(THREADS);

        for (int t = 0; t < THREADS; t++)
        {
            threads.emplace_back([&]() {
                while (true)
                {
                    int idx = nextIndex.fetch_add(1);
                    if (idx >= N)
                        break;

                    // Each LoadSnapshot does: Pause → Load → Resume
                    pool[idx]->LoadSnapshot(path);
                }
            });
        }

        for (auto& t : threads)
            t.join();
    }

    state.SetItemsProcessed(state.iterations() * N);
    state.counters["threads"] = THREADS;

    DestroyEmulatorPool(pool);
}

BENCHMARK_TEMPLATE(BM_Direct_LoadSnapshot, VIDEOWALL_SIZE, 4)->Iterations(10)->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Direct_LoadSnapshot, VIDEOWALL_SIZE, 8)->Iterations(10)->Unit(benchmark::kMillisecond);

/// endregion </Current Pattern>

/// region <Batch Pattern: Pause All, Load All, Resume All>

/// @brief Batch pattern: Coordinate pause across all, then load, then resume
/// This simulates Approach B from design doc
template <int N, int THREADS>
static void BM_Batch_PauseLoadResume(benchmark::State& state)
{
    std::string path = BenchmarkPathHelper::GetTestDataPath("loaders/sna/Timing_Tests-48k_v1.0.sna");

    auto pool = CreateEmulatorPool(N);
    if (pool.size() < static_cast<size_t>(N))
    {
        state.SkipWithError("Failed to create emulator pool");
        return;
    }

    for (auto _ : state)
    {
        // Phase 1: Pause all emulators (sequential - fast)
        for (auto* emu : pool)
            emu->Pause();

        // Phase 2: Load all snapshots in parallel (pool threads)
        // Note: LoadSnapshot will skip its internal pause since already paused
        {
            std::atomic<int> nextIndex{0};
            std::vector<std::thread> threads;
            threads.reserve(THREADS);

            for (int t = 0; t < THREADS; t++)
            {
                threads.emplace_back([&]() {
                    while (true)
                    {
                        int idx = nextIndex.fetch_add(1);
                        if (idx >= N)
                            break;

                        pool[idx]->LoadSnapshot(path);
                    }
                });
            }

            for (auto& t : threads)
                t.join();
        }

        // Phase 3: Resume all emulators (sequential - fast)
        for (auto* emu : pool)
            emu->Resume();
    }

    state.SetItemsProcessed(state.iterations() * N);
    state.counters["threads"] = THREADS;

    DestroyEmulatorPool(pool);
}

BENCHMARK_TEMPLATE(BM_Batch_PauseLoadResume, VIDEOWALL_SIZE, 4)->Iterations(10)->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Batch_PauseLoadResume, VIDEOWALL_SIZE, 8)->Iterations(10)->Unit(benchmark::kMillisecond);

/// endregion </Batch Pattern>

/// region <Scaling Tests>

/// @brief Test different instance counts with batch pattern
static void BM_Batch_Scaling(benchmark::State& state)
{
    int count = state.range(0);
    std::string path = BenchmarkPathHelper::GetTestDataPath("loaders/sna/Timing_Tests-48k_v1.0.sna");

    auto pool = CreateEmulatorPool(count);
    if (pool.size() < static_cast<size_t>(count))
    {
        state.SkipWithError("Failed to create emulator pool");
        return;
    }

    for (auto _ : state)
    {
        // Pause all
        for (auto* emu : pool)
            emu->Pause();

        // Load all (4 threads)
        std::atomic<int> nextIndex{0};
        std::vector<std::thread> threads;
        threads.reserve(POOL_THREADS);

        for (int t = 0; t < POOL_THREADS; t++)
        {
            threads.emplace_back([&]() {
                while (true)
                {
                    int idx = nextIndex.fetch_add(1);
                    if (idx >= count)
                        break;

                    pool[idx]->LoadSnapshot(path);
                }
            });
        }

        for (auto& t : threads)
            t.join();

        // Resume all
        for (auto* emu : pool)
            emu->Resume();
    }

    state.SetItemsProcessed(state.iterations() * count);

    DestroyEmulatorPool(pool);
}

BENCHMARK(BM_Batch_Scaling)->Arg(16)->Arg(48)->Arg(100)->Arg(180)->Unit(benchmark::kMillisecond);

/// endregion </Scaling Tests>

/// region <Pre-Caching Benchmarks>

#ifdef _CODE_UNDER_TEST
#include "loaders/snapshot/loader_sna.h"

/// @brief File I/O phase only (read + decompress to staging)
/// Measures disk access + decompression without apply
static void BM_FileIO_LoadToStaging(benchmark::State& state)
{
    int count = state.range(0);
    std::string path = BenchmarkPathHelper::GetTestDataPath("loaders/sna/Timing_Tests-48k_v1.0.sna");

    auto pool = CreateEmulatorPool(count);
    if (pool.size() < static_cast<size_t>(count))
    {
        state.SkipWithError("Failed to create emulator pool");
        return;
    }

    for (auto _ : state)
    {
        std::atomic<int> nextIndex{0};
        std::vector<std::thread> threads;
        std::vector<LoaderSNACUT*> loaders(count);
        threads.reserve(POOL_THREADS);

        for (int t = 0; t < POOL_THREADS; t++)
        {
            threads.emplace_back([&]() {
                while (true)
                {
                    int idx = nextIndex.fetch_add(1);
                    if (idx >= count)
                        break;

                    loaders[idx] = new LoaderSNACUT(pool[idx]->GetContext(), path);
                    loaders[idx]->loadToStaging();  // File read + decompress only
                }
            });
        }

        for (auto& t : threads)
            t.join();

        // Cleanup
        for (auto* loader : loaders)
            delete loader;
    }

    state.SetItemsProcessed(state.iterations() * count);
    DestroyEmulatorPool(pool);
}

BENCHMARK(BM_FileIO_LoadToStaging)->Arg(16)->Arg(48)->Arg(100)->Unit(benchmark::kMillisecond);

/// @brief Apply-only phase (pre-cached data to emulator)
/// Measures memory copy + screen render without file I/O
static void BM_ApplyOnly_FromStaging(benchmark::State& state)
{
    int count = state.range(0);
    std::string path = BenchmarkPathHelper::GetTestDataPath("loaders/sna/Timing_Tests-48k_v1.0.sna");

    auto pool = CreateEmulatorPool(count);
    if (pool.size() < static_cast<size_t>(count))
    {
        state.SkipWithError("Failed to create emulator pool");
        return;
    }

    // Pre-cache: Load all to staging ONCE (outside benchmark loop)
    std::vector<LoaderSNACUT*> stagedLoaders;
    for (int i = 0; i < count; i++)
    {
        auto* loader = new LoaderSNACUT(pool[i]->GetContext(), path);
        loader->loadToStaging();
        stagedLoaders.push_back(loader);
    }

    for (auto _ : state)
    {
        // Pause all
        for (auto* emu : pool)
            emu->Pause();

        // Apply staged data in parallel (NO file I/O)
        std::atomic<int> nextIndex{0};
        std::vector<std::thread> threads;
        threads.reserve(POOL_THREADS);

        for (int t = 0; t < POOL_THREADS; t++)
        {
            threads.emplace_back([&]() {
                while (true)
                {
                    int idx = nextIndex.fetch_add(1);
                    if (idx >= count)
                        break;

                    stagedLoaders[idx]->applySnapshotFromStaging();
                }
            });
        }

        for (auto& t : threads)
            t.join();

        // Resume all
        for (auto* emu : pool)
            emu->Resume();
    }

    state.SetItemsProcessed(state.iterations() * count);

    // Cleanup
    for (auto* loader : stagedLoaders)
        delete loader;
    DestroyEmulatorPool(pool);
}

BENCHMARK(BM_ApplyOnly_FromStaging)->Arg(16)->Arg(48)->Arg(100)->Unit(benchmark::kMillisecond);

#endif  // _CODE_UNDER_TEST

/// endregion </Pre-Caching Benchmarks>
