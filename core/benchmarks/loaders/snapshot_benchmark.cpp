/// @file snapshot_benchmark.cpp
/// @brief Benchmarks for SNA and Z80 snapshot loading performance
/// @date 2026-01-13
///
/// This benchmark suite measures the performance of snapshot loading operations
/// to establish baselines and compare optimizations.
///
/// Each benchmark uses a fully initialized Emulator instance with loaded config and ROMs,
/// matching real-world usage patterns.

#include "snapshot_benchmark.h"

#include <benchmark/benchmark.h>

#include <string>
#include <vector>

#include "benchmark_path_helper.h"
#include "common/filehelper.h"
#include "emulator/emulator.h"
#include "stdafx.h"

/// region <Helper Functions>

/// @brief Create and initialize a full emulator instance for benchmarking
/// This matches real-world usage by loading config and ROMs
static Emulator* CreateBenchmarkEmulator()
{
    Emulator* emulator = new Emulator(LoggerLevel::LogError);
    if (!emulator->Init())
    {
        delete emulator;
        return nullptr;
    }
    return emulator;
}

/// @brief Clean up benchmark emulator
static void DestroyBenchmarkEmulator(Emulator* emulator)
{
    if (emulator != nullptr)
    {
        emulator->Release();
        delete emulator;
    }
}

/// endregion </Helper Functions>

/// region <SNA Benchmarks>

/// @brief Benchmark SNA 48K snapshot loading - End-to-end
/// Measures complete LoadSnapshot() operation including validate, staging, and apply
static void BM_SNA_Load_48K(benchmark::State& state)
{
    static std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/sna/Timing_Tests-48k_v1.0.sna");

    for (auto _ : state)
    {
        state.PauseTiming();
        Emulator* emulator = CreateBenchmarkEmulator();
        if (!emulator)
        {
            state.SkipWithError("Failed to create emulator");
            return;
        }
        state.ResumeTiming();

        bool result = emulator->LoadSnapshot(snapshotPath);
        benchmark::DoNotOptimize(result);

        state.PauseTiming();
        DestroyBenchmarkEmulator(emulator);
        state.ResumeTiming();
    }

    state.SetBytesProcessed(state.iterations() * 49179);  // SNA-48K file size
}
BENCHMARK(BM_SNA_Load_48K)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark SNA 128K snapshot loading - End-to-end
/// Measures complete LoadSnapshot() operation for 128K snapshots
static void BM_SNA_Load_128K(benchmark::State& state)
{
    static std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/sna/multifix.sna");

    for (auto _ : state)
    {
        state.PauseTiming();
        Emulator* emulator = CreateBenchmarkEmulator();
        if (!emulator)
        {
            state.SkipWithError("Failed to create emulator");
            return;
        }
        state.ResumeTiming();

        bool result = emulator->LoadSnapshot(snapshotPath);
        benchmark::DoNotOptimize(result);

        state.PauseTiming();
        DestroyBenchmarkEmulator(emulator);
        state.ResumeTiming();
    }

    state.SetBytesProcessed(state.iterations() * 131103);  // SNA-128K file size
}
BENCHMARK(BM_SNA_Load_128K)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark multiple different SNA 128K snapshots
/// Tests loading performance across various snapshot types
static void BM_SNA_Load_128K_Multiple(benchmark::State& state)
{
    static std::vector<std::string> snapshotPaths = {
        BenchmarkPathHelper::GetTestDataPath("loaders/sna/multifix.sna"),
        BenchmarkPathHelper::GetTestDataPath("loaders/sna/action.sna"),
        BenchmarkPathHelper::GetTestDataPath("loaders/sna/eyeache1.sna"),
        BenchmarkPathHelper::GetTestDataPath("loaders/sna/vibrations.sna")};

    size_t idx = 0;
    for (auto _ : state)
    {
        state.PauseTiming();
        Emulator* emulator = CreateBenchmarkEmulator();
        if (!emulator)
        {
            state.SkipWithError("Failed to create emulator");
            return;
        }
        const std::string& path = snapshotPaths[idx % snapshotPaths.size()];
        state.ResumeTiming();

        bool result = emulator->LoadSnapshot(path);
        benchmark::DoNotOptimize(result);

        state.PauseTiming();
        DestroyBenchmarkEmulator(emulator);
        idx++;
        state.ResumeTiming();
    }

    state.SetBytesProcessed(state.iterations() * 131103);
}
BENCHMARK(BM_SNA_Load_128K_Multiple)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// endregion </SNA Benchmarks>

/// region <Z80 Benchmarks>

/// @brief Benchmark Z80 highly compressed snapshot loading
/// Tests decompression performance with high RLE ratio
static void BM_Z80_Load_Compressed(benchmark::State& state)
{
    // newbench.z80 is only 2,883 bytes - highly compressed
    static std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/z80/newbench.z80");

    for (auto _ : state)
    {
        state.PauseTiming();
        Emulator* emulator = CreateBenchmarkEmulator();
        if (!emulator)
        {
            state.SkipWithError("Failed to create emulator");
            return;
        }
        state.ResumeTiming();

        bool result = emulator->LoadSnapshot(snapshotPath);
        benchmark::DoNotOptimize(result);

        state.PauseTiming();
        DestroyBenchmarkEmulator(emulator);
        state.ResumeTiming();
    }

    state.SetBytesProcessed(state.iterations() * 2883);
}
BENCHMARK(BM_Z80_Load_Compressed)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark Z80 medium compressed snapshot loading
/// Tests typical snapshot compression ratio
static void BM_Z80_Load_MediumCompression(benchmark::State& state)
{
    // dizzyx.z80 is 66,515 bytes - medium compression
    static std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/z80/dizzyx.z80");

    for (auto _ : state)
    {
        state.PauseTiming();
        Emulator* emulator = CreateBenchmarkEmulator();
        if (!emulator)
        {
            state.SkipWithError("Failed to create emulator");
            return;
        }
        state.ResumeTiming();

        bool result = emulator->LoadSnapshot(snapshotPath);
        benchmark::DoNotOptimize(result);

        state.PauseTiming();
        DestroyBenchmarkEmulator(emulator);
        state.ResumeTiming();
    }

    state.SetBytesProcessed(state.iterations() * 66515);
}
BENCHMARK(BM_Z80_Load_MediumCompression)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark Z80 low compression snapshot loading
/// Tests performance with minimal RLE compression
static void BM_Z80_Load_LowCompression(benchmark::State& state)
{
    // Binary Love III.z80 is 107,864 bytes - low compression
    static std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/z80/Binary Love III.z80");

    for (auto _ : state)
    {
        state.PauseTiming();
        Emulator* emulator = CreateBenchmarkEmulator();
        if (!emulator)
        {
            state.SkipWithError("Failed to create emulator");
            return;
        }
        state.ResumeTiming();

        bool result = emulator->LoadSnapshot(snapshotPath);
        benchmark::DoNotOptimize(result);

        state.PauseTiming();
        DestroyBenchmarkEmulator(emulator);
        state.ResumeTiming();
    }

    state.SetBytesProcessed(state.iterations() * 107864);
}
BENCHMARK(BM_Z80_Load_LowCompression)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark Z80 128K snapshot loading
/// Tests 128K model with multiple memory banks
static void BM_Z80_Load_128K(benchmark::State& state)
{
    // BBG128.z80 is a 128K snapshot
    static std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/z80/BBG128.z80");

    for (auto _ : state)
    {
        state.PauseTiming();
        Emulator* emulator = CreateBenchmarkEmulator();
        if (!emulator)
        {
            state.SkipWithError("Failed to create emulator");
            return;
        }
        state.ResumeTiming();

        bool result = emulator->LoadSnapshot(snapshotPath);
        benchmark::DoNotOptimize(result);

        state.PauseTiming();
        DestroyBenchmarkEmulator(emulator);
        state.ResumeTiming();
    }

    state.SetBytesProcessed(state.iterations() * 3566);
}
BENCHMARK(BM_Z80_Load_128K)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark multiple Z80 snapshots - throughput test
static void BM_Z80_Load_Multiple(benchmark::State& state)
{
    static std::vector<std::string> snapshotPaths = {
        BenchmarkPathHelper::GetTestDataPath("loaders/z80/newbench.z80"),
        BenchmarkPathHelper::GetTestDataPath("loaders/z80/dizzyx.z80"),
        BenchmarkPathHelper::GetTestDataPath("loaders/z80/Binary Love I.z80"),
        BenchmarkPathHelper::GetTestDataPath("loaders/z80/Binary Love II.z80")};

    size_t idx = 0;
    for (auto _ : state)
    {
        state.PauseTiming();
        Emulator* emulator = CreateBenchmarkEmulator();
        if (!emulator)
        {
            state.SkipWithError("Failed to create emulator");
            return;
        }
        const std::string& path = snapshotPaths[idx % snapshotPaths.size()];
        state.ResumeTiming();

        bool result = emulator->LoadSnapshot(path);
        benchmark::DoNotOptimize(result);

        state.PauseTiming();
        DestroyBenchmarkEmulator(emulator);
        idx++;
        state.ResumeTiming();
    }
}
BENCHMARK(BM_Z80_Load_Multiple)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// endregion </Z80 Benchmarks>

/// region <Comparison Benchmarks>

/// @brief Direct comparison: SNA 128K vs Z80 128K compressed
/// Run side-by-side to compare format performance
static void BM_Compare_SNA_vs_Z80_128K(benchmark::State& state)
{
    static std::string snaPath = BenchmarkPathHelper::GetTestDataPath("loaders/sna/multifix.sna");
    static std::string z80Path = BenchmarkPathHelper::GetTestDataPath("loaders/z80/BBG128.z80");

    bool useSNA = (state.range(0) == 0);
    const std::string& path = useSNA ? snaPath : z80Path;

    for (auto _ : state)
    {
        state.PauseTiming();
        Emulator* emulator = CreateBenchmarkEmulator();
        if (!emulator)
        {
            state.SkipWithError("Failed to create emulator");
            return;
        }
        state.ResumeTiming();

        bool result = emulator->LoadSnapshot(path);
        benchmark::DoNotOptimize(result);

        state.PauseTiming();
        DestroyBenchmarkEmulator(emulator);
        state.ResumeTiming();
    }

    state.SetLabel(useSNA ? "SNA-128K" : "Z80-128K");
}
BENCHMARK(BM_Compare_SNA_vs_Z80_128K)
    ->Arg(0)  // SNA
    ->Arg(1)  // Z80
    ->Iterations(100)
    ->Unit(benchmark::kMicrosecond);

/// endregion </Comparison Benchmarks>

/// region <File I/O Baseline Benchmarks>

/// @brief Baseline: Pure file read performance
/// Measures raw file I/O without parsing overhead
static void BM_FileIO_Baseline_48K(benchmark::State& state)
{
    static std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/sna/Timing_Tests-48k_v1.0.sna");
    const size_t fileSize = 49179;

    std::vector<uint8_t> buffer(fileSize);

    for (auto _ : state)
    {
        FILE* file = FileHelper::OpenExistingFile(snapshotPath);
        if (file)
        {
            size_t read = fread(buffer.data(), 1, fileSize, file);
            benchmark::DoNotOptimize(read);
            FileHelper::CloseFile(file);
        }
    }

    state.SetBytesProcessed(state.iterations() * fileSize);
}
BENCHMARK(BM_FileIO_Baseline_48K)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// @brief Baseline: Pure file read performance for 128K files
static void BM_FileIO_Baseline_128K(benchmark::State& state)
{
    static std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/sna/multifix.sna");
    const size_t fileSize = 131103;

    std::vector<uint8_t> buffer(fileSize);

    for (auto _ : state)
    {
        FILE* file = FileHelper::OpenExistingFile(snapshotPath);
        if (file)
        {
            size_t read = fread(buffer.data(), 1, fileSize, file);
            benchmark::DoNotOptimize(read);
            FileHelper::CloseFile(file);
        }
    }

    state.SetBytesProcessed(state.iterations() * fileSize);
}
BENCHMARK(BM_FileIO_Baseline_128K)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// endregion </File I/O Baseline Benchmarks>

/// region <Memory Copy Baseline Benchmarks>

/// @brief Baseline: memcpy performance for 48K
static void BM_Memcpy_Baseline_48K(benchmark::State& state)
{
    const size_t size = 48 * 1024;  // 48KB
    std::vector<uint8_t> src(size, 0xAA);
    std::vector<uint8_t> dst(size);

    for (auto _ : state)
    {
        memcpy(dst.data(), src.data(), size);
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(state.iterations() * size);
}
BENCHMARK(BM_Memcpy_Baseline_48K)->Iterations(1000)->Unit(benchmark::kMicrosecond);

/// @brief Baseline: memcpy performance for 128K
static void BM_Memcpy_Baseline_128K(benchmark::State& state)
{
    const size_t size = 128 * 1024;  // 128KB
    std::vector<uint8_t> src(size, 0xAA);
    std::vector<uint8_t> dst(size);

    for (auto _ : state)
    {
        memcpy(dst.data(), src.data(), size);
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(state.iterations() * size);
}
BENCHMARK(BM_Memcpy_Baseline_128K)->Iterations(1000)->Unit(benchmark::kMicrosecond);

/// endregion </Memory Copy Baseline Benchmarks>

/// region <Single-Instance Reload Benchmarks>
/// These benchmarks reuse a single emulator instance across all iterations
/// to measure pure snapshot loading overhead without emulator lifecycle costs

/// @brief Single-instance 100x reload - Pure snapshot loading measurement
/// Creates one emulator, then reloads the same snapshot 100 times
static void BM_SingleInstance_SNA_Reload_100x(benchmark::State& state)
{
    static std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/sna/Timing_Tests-48k_v1.0.sna");

    // Create emulator ONCE before all iterations
    Emulator* emulator = CreateBenchmarkEmulator();
    if (!emulator)
    {
        state.SkipWithError("Failed to create emulator");
        return;
    }

    for (auto _ : state)
    {
        bool result = emulator->LoadSnapshot(snapshotPath);
        benchmark::DoNotOptimize(result);
    }

    // Cleanup after all iterations
    DestroyBenchmarkEmulator(emulator);

    state.SetBytesProcessed(state.iterations() * 49179);
}
BENCHMARK(BM_SingleInstance_SNA_Reload_100x)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// @brief Single-instance 100x reload for 128K SNA
static void BM_SingleInstance_SNA128K_Reload_100x(benchmark::State& state)
{
    static std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/sna/multifix.sna");

    Emulator* emulator = CreateBenchmarkEmulator();
    if (!emulator)
    {
        state.SkipWithError("Failed to create emulator");
        return;
    }

    for (auto _ : state)
    {
        bool result = emulator->LoadSnapshot(snapshotPath);
        benchmark::DoNotOptimize(result);
    }

    DestroyBenchmarkEmulator(emulator);
    state.SetBytesProcessed(state.iterations() * 131103);
}
BENCHMARK(BM_SingleInstance_SNA128K_Reload_100x)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// @brief Single-instance 100x reload for Z80 compressed
static void BM_SingleInstance_Z80_Reload_100x(benchmark::State& state)
{
    static std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/z80/newbench.z80");

    Emulator* emulator = CreateBenchmarkEmulator();
    if (!emulator)
    {
        state.SkipWithError("Failed to create emulator");
        return;
    }

    for (auto _ : state)
    {
        bool result = emulator->LoadSnapshot(snapshotPath);
        benchmark::DoNotOptimize(result);
    }

    DestroyBenchmarkEmulator(emulator);
    state.SetBytesProcessed(state.iterations() * 2883);
}
BENCHMARK(BM_SingleInstance_Z80_Reload_100x)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// endregion </Single-Instance Reload Benchmarks>

/// region <Full-Lifecycle Benchmarks>
/// These benchmarks include emulator creation and destruction in timing
/// to measure complete per-snapshot overhead including lifecycle

/// @brief Full lifecycle 100x - Includes emulator create/destroy in timing
/// Measures: Create Emulator + Init + LoadSnapshot + Release + Delete
static void BM_FullLifecycle_SNA_100x(benchmark::State& state)
{
    static std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/sna/Timing_Tests-48k_v1.0.sna");

    for (auto _ : state)
    {
        Emulator* emulator = new Emulator(LoggerLevel::LogError);
        if (emulator->Init())
        {
            bool result = emulator->LoadSnapshot(snapshotPath);
            benchmark::DoNotOptimize(result);
            emulator->Release();
        }
        delete emulator;
    }

    state.SetBytesProcessed(state.iterations() * 49179);
}
BENCHMARK(BM_FullLifecycle_SNA_100x)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// @brief Full lifecycle for 128K SNA
static void BM_FullLifecycle_SNA128K_100x(benchmark::State& state)
{
    static std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/sna/multifix.sna");

    for (auto _ : state)
    {
        Emulator* emulator = new Emulator(LoggerLevel::LogError);
        if (emulator->Init())
        {
            bool result = emulator->LoadSnapshot(snapshotPath);
            benchmark::DoNotOptimize(result);
            emulator->Release();
        }
        delete emulator;
    }

    state.SetBytesProcessed(state.iterations() * 131103);
}
BENCHMARK(BM_FullLifecycle_SNA128K_100x)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// @brief Full lifecycle for Z80 compressed
static void BM_FullLifecycle_Z80_100x(benchmark::State& state)
{
    static std::string snapshotPath = BenchmarkPathHelper::GetTestDataPath("loaders/z80/newbench.z80");

    for (auto _ : state)
    {
        Emulator* emulator = new Emulator(LoggerLevel::LogError);
        if (emulator->Init())
        {
            bool result = emulator->LoadSnapshot(snapshotPath);
            benchmark::DoNotOptimize(result);
            emulator->Release();
        }
        delete emulator;
    }

    state.SetBytesProcessed(state.iterations() * 2883);
}
BENCHMARK(BM_FullLifecycle_Z80_100x)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// endregion </Full-Lifecycle Benchmarks>

/// region <decompressPage Benchmarks>

#include "loaders/snapshot/loader_z80.h"

/// @brief Create test data with RLE patterns for decompression benchmarking
static std::vector<uint8_t> CreateTestCompressedData(size_t rleRuns, uint8_t runLength)
{
    std::vector<uint8_t> data;

    // Mix of RLE sequences and literal bytes
    for (size_t i = 0; i < rleRuns; i++)
    {
        // RLE sequence: ED ED nn bb
        data.push_back(0xED);
        data.push_back(0xED);
        data.push_back(runLength);                       // count
        data.push_back(static_cast<uint8_t>(i & 0xFF));  // value

        // Add some literal bytes between RLE sequences
        for (int j = 0; j < 8; j++)
        {
            data.push_back(static_cast<uint8_t>((i + j) & 0xFF));
        }
    }

    return data;
}

/// @brief Benchmark original decompressPage (byte-by-byte loop)
static void BM_DecompressPage_Original(benchmark::State& state)
{
    // Setup: create test page with RLE and literal data
    auto compressedData = CreateTestCompressedData(100, 100);  // 100 runs of 100 bytes each
    std::vector<uint8_t> outputBuffer(16384);                  // 16K page

    // Create minimal context for LoaderZ80
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError);
    LoaderZ80 loader(context, "");

    for (auto _ : state)
    {
        loader.decompressPage_Original(compressedData.data(), compressedData.size(), outputBuffer.data(),
                                       outputBuffer.size());
    }

    state.SetBytesProcessed(state.iterations() * outputBuffer.size());

    delete context;
}
BENCHMARK(BM_DecompressPage_Original)->Iterations(1000);

/// @brief Benchmark optimized decompressPage (memset for RLE)
static void BM_DecompressPage_Optimized(benchmark::State& state)
{
    // Setup: create test page with RLE and literal data
    auto compressedData = CreateTestCompressedData(100, 100);  // 100 runs of 100 bytes each
    std::vector<uint8_t> outputBuffer(16384);                  // 16K page

    // Create minimal context for LoaderZ80
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError);
    LoaderZ80 loader(context, "");

    for (auto _ : state)
    {
        loader.decompressPage_Optimized(compressedData.data(), compressedData.size(), outputBuffer.data(),
                                        outputBuffer.size());
    }

    state.SetBytesProcessed(state.iterations() * outputBuffer.size());

    delete context;
}
BENCHMARK(BM_DecompressPage_Optimized)->Iterations(1000);

/// endregion </decompressPage Benchmarks>
