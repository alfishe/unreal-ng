/// @file ttd4mbbenchmark.cpp
/// @brief Benchmark TTD scalability for 4MB machines (ZX Evo / ATM3).
///
/// Per Phase 3 §3.4: Validates that TTD scales correctly to machines with
/// 256 RAM pages (4MB). This is the upper bound for the emulator's memory
/// model and represents the worst-case scenario for checkpoint capture.
///
/// Metrics:
///   - Checkpoint capture time with 256 RAM pages
///   - Coverage index memory (should be constant per-frame, not per-page)
///   - Seek time with full 4MB working set
///   - Comparison against 128K baseline
///
/// Run with: ./core-benchmarks --benchmark_filter="TTD_4MB.*"

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{

/// Configuration for RAM size testing
struct RamConfig
{
    const char* name;
    uint16_t ramPages;    // Number of 16KB pages
    size_t ramBytes;      // Total RAM in bytes
};

constexpr RamConfig kConfigs[] = {
    {"48K",   3,   48 * 1024},
    {"128K",  8,  128 * 1024},
    {"512K", 32,  512 * 1024},
    {"1MB",  64, 1024 * 1024},
    {"4MB", 256, 4 * 1024 * 1024},
};

/// @brief Per-benchmark Emulator + TTD setup.
struct ScalabilityFixture
{
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;
    ttd::TimeTravelManager* ttd = nullptr;
    FeatureManager* fm = nullptr;
    Memory* memory = nullptr;

    bool Init()
    {
        emulator = new Emulator(LoggerLevel::LogError);
        emulator->SetCustomConfigPath("data/configs/pentagon/unreal.ini");
        if (!emulator->Init())
            return false;
        context = emulator->GetContext();
        if (!context) return false;
        ttd = context->pTimeTravelManager;
        if (!ttd) return false;
        memory = context->pMemory;
        if (!memory) return false;
        fm = emulator->GetFeatureManager();
        if (!fm) return false;

        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        memory->UpdateFeatureCache();
        return true;
    }

    void Shutdown()
    {
        if (emulator)
        {
            emulator->Stop();
            emulator->Release();
            delete emulator;
            emulator = nullptr;
        }
    }

    /// @brief Start recording and run for `frames` frames.
    void RecordSession(uint32_t frames)
    {
        ttd->InvalidateSession("benchmark reset");
        ttd->StartRecording();
        emulator->RunNFrames(static_cast<unsigned>(frames), /*skipBreakpoints=*/true);
        ttd->StopRecording();
        ttd->SeekTo(ttd->SessionEndPosition());
    }

    /// @brief Dirty a specified number of RAM pages with random content.
    ///
    /// Simulates a 4MB machine with many active pages. The benchmark can't
    /// actually change the machine model at runtime, so we simulate the
    /// effect by writing to multiple banks.
    void DirtyPages(uint16_t pageCount, uint8_t seed)
    {
        // The emulator may not have 256 pages available depending on model,
        // so we wrap around and dirty whatever pages exist. The goal is to
        // measure TTD's handling of many dirty pages, not to actually switch
        // to a 4MB model.
        const uint16_t maxPages = ttd ? ttd->GetModelRamPages() : 0;
        const uint16_t actualPages = std::min(pageCount, maxPages);

        for (uint16_t page = 0; page < actualPages; ++page)
        {
            // Map page to bank 1 (0x4000-0x7FFF) and write content
            memory->SetRAMPageToBank1(page);

            // Write to multiple locations in the page to ensure it's marked dirty
            for (uint16_t offset = 0; offset < 0x4000; offset += 256)
            {
                memory->MemoryWriteDebug(
                    static_cast<uint16_t>(0x4000 + offset),
                    static_cast<uint8_t>(seed + page + offset));
            }
        }
    }
};

ScalabilityFixture g_fixture;
bool g_fixtureReady = false;

bool EnsureFixture()
{
    if (g_fixtureReady) return true;
    if (!g_fixture.Init())
        return false;
    g_fixtureReady = true;
    return true;
}

double Percentile(std::vector<double>& sorted, double p)
{
    if (sorted.empty()) return 0.0;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(p / 100.0 * (sorted.size() - 1));
    return sorted[idx];
}

} // namespace

// ===========================================================================
// OnFrameBoundary capture cost — varying dirty page counts
// ===========================================================================
//
// Measures the cost of OnFrameBoundary() when different numbers of pages
// have been modified. This is the core scalability test: capture cost should
// scale with the number of dirty pages, not the total machine RAM.

static void BM_TTD_4MB_Capture_DirtyPages(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    const int dirtyPageCount = static_cast<int>(state.range(0));
    uint8_t seed = 0;

    g_fixture.ttd->InvalidateSession("benchmark");
    g_fixture.ttd->StartRecording();
    g_fixture.emulator->RunNFrames(1, true);  // Establish baseline

    std::vector<double> latencies;
    latencies.reserve(state.max_iterations);

    for (auto _ : state)
    {
        state.PauseTiming();
        g_fixture.DirtyPages(static_cast<uint16_t>(dirtyPageCount), ++seed);
        state.ResumeTiming();

        auto start = std::chrono::high_resolution_clock::now();
        g_fixture.ttd->OnFrameBoundary();
        auto end = std::chrono::high_resolution_clock::now();

        double latencyUs = std::chrono::duration<double, std::micro>(end - start).count();
        latencies.push_back(latencyUs);
    }

    g_fixture.ttd->StopRecording();

    if (!latencies.empty())
    {
        double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        double mean = sum / latencies.size();
        double maxVal = *std::max_element(latencies.begin(), latencies.end());

        state.counters["mean_us"] = mean;
        state.counters["max_us"] = maxVal;
        state.counters["dirty_pages"] = static_cast<double>(dirtyPageCount);

        // Calculate bytes per microsecond (throughput)
        size_t dirtyBytes = dirtyPageCount * 16 * 1024;
        state.counters["MB_per_sec"] = (dirtyBytes / mean) * 1000.0 / (1024 * 1024);
    }

    auto info = g_fixture.ttd->GetSessionInfo();
    state.counters["checkpoints"] = static_cast<double>(info.checkpointCount);
    state.counters["heap_kb"] = static_cast<double>(info.sessionHeapBytes) / 1024.0;

    state.SetLabel(std::to_string(dirtyPageCount) + " dirty pages");
}
BENCHMARK(BM_TTD_4MB_Capture_DirtyPages)
    ->Arg(0)     // No dirty pages (baseline overhead)
    ->Arg(1)     // Single page (minimum)
    ->Arg(4)     // Typical game frame (screen + some RAM)
    ->Arg(8)     // 128KB working set
    ->Arg(32)    // 512KB working set
    ->Arg(64)    // 1MB working set
    ->Arg(128)   // 2MB working set
    ->Arg(256)   // Full 4MB (worst case)
    ->Iterations(100)
    ->Unit(benchmark::kMicrosecond);

// ===========================================================================
// Coverage index memory — per-frame vs per-page
// ===========================================================================
//
// Validates that coverage index memory scales with frame count, not page count.
// Records sessions with different dirty patterns and measures index size.

static void BM_TTD_4MB_CoverageIndexMemory(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    const int dirtyPageCount = static_cast<int>(state.range(0));
    const int frameCount = static_cast<int>(state.range(1));

    g_fixture.ttd->InvalidateSession("benchmark");
    g_fixture.ttd->SetEnableCoverageIndex(true);

    for (auto _ : state)
    {
        state.PauseTiming();
        g_fixture.ttd->InvalidateSession("benchmark iteration");
        g_fixture.ttd->StartRecording();
        state.ResumeTiming();

        for (int frame = 0; frame < frameCount; ++frame)
        {
            g_fixture.DirtyPages(static_cast<uint16_t>(dirtyPageCount),
                                 static_cast<uint8_t>(frame));
            g_fixture.ttd->OnFrameBoundary();
        }

        state.PauseTiming();
        g_fixture.ttd->StopRecording();
        state.ResumeTiming();
    }

    auto info = g_fixture.ttd->GetSessionInfo();

    state.counters["frames"] = static_cast<double>(info.checkpointCount);
    state.counters["coverage_frames"] = static_cast<double>(info.coverageIndexFrames);
    state.counters["coverage_kb"] = static_cast<double>(info.coverageIndexBytes) / 1024.0;
    state.counters["dirty_pages"] = static_cast<double>(dirtyPageCount);

    // Calculate bytes per frame (should be roughly constant regardless of dirty page count)
    if (info.coverageIndexFrames > 0)
    {
        double bytesPerFrame = static_cast<double>(info.coverageIndexBytes) / info.coverageIndexFrames;
        state.counters["bytes_per_frame"] = bytesPerFrame;
    }

    state.SetLabel(std::to_string(dirtyPageCount) + " pages x " + std::to_string(frameCount) + " frames");
}
BENCHMARK(BM_TTD_4MB_CoverageIndexMemory)
    ->Args({4, 100})     // Typical game: 4 pages/frame, 100 frames
    ->Args({8, 100})     // 8 pages/frame
    ->Args({32, 100})    // 32 pages/frame (512KB working set)
    ->Args({4, 1000})    // Long session: 4 pages/frame
    ->Args({32, 1000})   // Long session: large working set
    ->Unit(benchmark::kMillisecond);

// ===========================================================================
// Seek with large working set
// ===========================================================================
//
// Measures seek latency when the session contains checkpoints with many
// dirty pages. The hypothesis is that seek time scales with checkpoint
// complexity, not session length alone.

static void BM_TTD_4MB_SeekLargeWorkingSet(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    const int dirtyPageCount = static_cast<int>(state.range(0));
    const int frameCount = 500;

    // Record session with specified dirty pattern
    g_fixture.ttd->InvalidateSession("benchmark");
    g_fixture.ttd->StartRecording();

    for (int frame = 0; frame < frameCount; ++frame)
    {
        g_fixture.DirtyPages(static_cast<uint16_t>(dirtyPageCount),
                             static_cast<uint8_t>(frame));
        g_fixture.ttd->OnFrameBoundary();
    }

    g_fixture.ttd->StopRecording();

    auto info = g_fixture.ttd->GetSessionInfo();

    std::mt19937 rng(42);
    std::uniform_int_distribution<uint64_t> frameDist(
        info.sessionStartFrame, info.currentEndFrame);

    std::vector<double> latencies;
    latencies.reserve(state.max_iterations);

    for (auto _ : state)
    {
        state.PauseTiming();
        g_fixture.ttd->SeekTo(g_fixture.ttd->SessionEndPosition());

        uint64_t targetFrame = frameDist(rng);
        ttd::TTDTimePoint target;
        target.frame = targetFrame;
        target.tInFrame = 0;
        state.ResumeTiming();

        auto start = std::chrono::high_resolution_clock::now();
        g_fixture.ttd->SeekTo(target);
        auto end = std::chrono::high_resolution_clock::now();

        double latencyMs = std::chrono::duration<double, std::milli>(end - start).count();
        latencies.push_back(latencyMs);
    }

    if (!latencies.empty())
    {
        double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        double mean = sum / latencies.size();
        double maxVal = *std::max_element(latencies.begin(), latencies.end());

        state.counters["mean_ms"] = mean;
        state.counters["p95_ms"] = Percentile(latencies, 95);
        state.counters["max_ms"] = maxVal;
        state.counters["exceeds_6ms"] = maxVal > 6.0 ? 1.0 : 0.0;
    }

    state.counters["dirty_pages"] = static_cast<double>(dirtyPageCount);
    state.counters["heap_mb"] = static_cast<double>(info.sessionHeapBytes) / (1024.0 * 1024.0);
    state.counters["checkpoints"] = static_cast<double>(info.checkpointCount);

    state.SetLabel(std::to_string(dirtyPageCount) + " pages/frame");
}
BENCHMARK(BM_TTD_4MB_SeekLargeWorkingSet)
    ->Arg(4)     // Baseline (typical game)
    ->Arg(8)     // 128KB/frame
    ->Arg(32)    // 512KB/frame
    ->Arg(64)    // 1MB/frame
    ->Arg(128)   // 2MB/frame
    ->Iterations(100)
    ->Unit(benchmark::kMillisecond);

// ===========================================================================
// 128K vs 4MB comparison
// ===========================================================================
//
// Direct comparison between 128K baseline and simulated 4MB machine behavior.
// Both run the same workload pattern to isolate the effect of page count.

static void BM_TTD_4MB_Comparison_Session(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    const int configIdx = static_cast<int>(state.range(0));
    const RamConfig& config = kConfigs[configIdx];
    const int frameCount = 200;

    // Simulate the number of dirty pages this config would produce
    // In a real scenario, a 4MB machine doesn't necessarily dirty all pages,
    // but we use proportional dirtying to model typical workloads
    // Realistic dirty pages based on Z80 T-state hardware cycle write limits:
    // - 3.5 MHz (48K/128K): Max ~10KB/frame -> 1 16KB page
    // - 7 MHz (512K/1MB): Max ~20KB/frame -> 2 16KB pages
    // - 14 MHz (4MB ZX-Evo): Max ~40KB/frame -> 3 16KB pages
    uint16_t dirtyPagesPerFrame = 1;
    if (config.ramBytes >= 4 * 1024 * 1024) {
        dirtyPagesPerFrame = 3;
    } else if (config.ramBytes >= 512 * 1024) {
        dirtyPagesPerFrame = 2;
    } else {
        dirtyPagesPerFrame = 1;
    }

    std::vector<double> captureLatencies;
    captureLatencies.reserve(frameCount);

    for (auto _ : state)
    {
        state.PauseTiming();
        g_fixture.ttd->InvalidateSession("benchmark");
        g_fixture.ttd->StartRecording();
        captureLatencies.clear();
        state.ResumeTiming();

        for (int frame = 0; frame < frameCount; ++frame)
        {
            g_fixture.DirtyPages(dirtyPagesPerFrame, static_cast<uint8_t>(frame));

            auto start = std::chrono::high_resolution_clock::now();
            g_fixture.ttd->OnFrameBoundary();
            auto end = std::chrono::high_resolution_clock::now();

            double latencyUs = std::chrono::duration<double, std::micro>(end - start).count();
            captureLatencies.push_back(latencyUs);
        }

        state.PauseTiming();
        g_fixture.ttd->StopRecording();
        state.ResumeTiming();
    }

    auto info = g_fixture.ttd->GetSessionInfo();

    if (!captureLatencies.empty())
    {
        double sum = std::accumulate(captureLatencies.begin(), captureLatencies.end(), 0.0);
        double mean = sum / captureLatencies.size();
        double maxVal = *std::max_element(captureLatencies.begin(), captureLatencies.end());

        state.counters["mean_capture_us"] = mean;
        state.counters["max_capture_us"] = maxVal;
    }

    state.counters["config_ram_kb"] = static_cast<double>(config.ramBytes) / 1024.0;
    state.counters["dirty_pages_per_frame"] = static_cast<double>(dirtyPagesPerFrame);
    state.counters["heap_mb"] = static_cast<double>(info.sessionHeapBytes) / (1024.0 * 1024.0);
    state.counters["checkpoints"] = static_cast<double>(info.checkpointCount);

    state.SetLabel(config.name);
}
BENCHMARK(BM_TTD_4MB_Comparison_Session)
    ->Arg(0)  // 48K
    ->Arg(1)  // 128K (baseline)
    ->Arg(2)  // 512K
    ->Arg(3)  // 1MB
    ->Arg(4)  // 4MB
    ->Iterations(10)
    ->Unit(benchmark::kMillisecond);

// ===========================================================================
// Page store memory efficiency at scale
// ===========================================================================
//
// Measures how efficiently the COW page store uses memory when many pages
// are being tracked. Good deduplication should keep memory sublinear.

static void BM_TTD_4MB_PageStoreEfficiency(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    const int frameCount = static_cast<int>(state.range(0));
    const int dirtyPagesPerFrame = 4;  // Constant: typical game

    for (auto _ : state)
    {
        state.PauseTiming();
        g_fixture.ttd->InvalidateSession("benchmark");
        g_fixture.ttd->StartRecording();
        state.ResumeTiming();

        for (int frame = 0; frame < frameCount; ++frame)
        {
            g_fixture.DirtyPages(dirtyPagesPerFrame, static_cast<uint8_t>(frame));
            g_fixture.ttd->OnFrameBoundary();
        }

        state.PauseTiming();
        g_fixture.ttd->StopRecording();
        state.ResumeTiming();
    }

    auto info = g_fixture.ttd->GetSessionInfo();

    // Calculate theoretical vs actual memory
    size_t theoreticalBytes = frameCount * dirtyPagesPerFrame * 16 * 1024;
    double efficiency = static_cast<double>(info.pageStoreUsedBytes) / theoreticalBytes;

    state.counters["frames"] = static_cast<double>(frameCount);
    state.counters["theoretical_mb"] = static_cast<double>(theoreticalBytes) / (1024.0 * 1024.0);
    state.counters["actual_mb"] = static_cast<double>(info.pageStoreUsedBytes) / (1024.0 * 1024.0);
    state.counters["heap_mb"] = static_cast<double>(info.sessionHeapBytes) / (1024.0 * 1024.0);
    state.counters["compression_ratio"] = info.compressionRatio;
    state.counters["efficiency"] = efficiency;

    state.SetLabel(std::to_string(frameCount) + " frames");
}
BENCHMARK(BM_TTD_4MB_PageStoreEfficiency)
    ->Arg(100)
    ->Arg(500)
    ->Arg(1000)
    ->Arg(3000)
    ->Unit(benchmark::kMillisecond);

// ===========================================================================
// Baseline capture overhead (no dirty pages)
// ===========================================================================
//
// Measures the fixed overhead of OnFrameBoundary when no pages are dirty.
// This is the floor for capture latency.

static void BM_TTD_4MB_CaptureOverhead(benchmark::State& state)
{
    if (!EnsureFixture()) { state.SkipWithError("fixture init failed"); return; }

    g_fixture.ttd->InvalidateSession("benchmark");
    g_fixture.ttd->StartRecording();
    g_fixture.emulator->RunNFrames(1, true);  // Establish baseline

    std::vector<double> latencies;
    latencies.reserve(state.max_iterations);

    for (auto _ : state)
    {
        // Don't dirty any pages — measure pure overhead
        auto start = std::chrono::high_resolution_clock::now();
        g_fixture.ttd->OnFrameBoundary();
        auto end = std::chrono::high_resolution_clock::now();

        double latencyUs = std::chrono::duration<double, std::micro>(end - start).count();
        latencies.push_back(latencyUs);
    }

    g_fixture.ttd->StopRecording();

    if (!latencies.empty())
    {
        double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        double mean = sum / latencies.size();
        double maxVal = *std::max_element(latencies.begin(), latencies.end());

        state.counters["mean_us"] = mean;
        state.counters["max_us"] = maxVal;
    }

    auto info = g_fixture.ttd->GetSessionInfo();
    state.counters["checkpoints"] = static_cast<double>(info.checkpointCount);

    state.SetLabel("zero dirty pages (fixed overhead)");
}
BENCHMARK(BM_TTD_4MB_CaptureOverhead)->Iterations(1000)->Unit(benchmark::kNanosecond);
