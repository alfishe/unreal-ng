/// @file ttd_v2_index_overhead_bench.cpp
/// @brief Analyze v2 format index overhead for hot buffer + file stream

#include <benchmark/benchmark.h>

struct PageSlotEntry {
    uint32_t slotId;
    uint32_t hotBufferOffset;
    uint64_t fileOffset;
    uint16_t pageSize;
    uint16_t flags;
};

static void BM_TTD_Index_Overhead(benchmark::State& state) {
    const size_t hotBufferMB = state.range(0);
    const size_t framesIn5Min = 15000;
    const size_t dirtyPagesPerFrame = state.range(1);
    
    const size_t pageSize = 4096;
    size_t pageCapacity = (hotBufferMB * 1024 * 1024) / pageSize;
    
    // Calculate page index size (with 30% hash table overhead)
    size_t pageIndexBytes = pageCapacity * sizeof(PageSlotEntry) * 1.3;
    
    // Frame index bytes per frame
    size_t frameIndexBytesPerFrame = 36 + dirtyPagesPerFrame * 4;
    
    size_t totalIndexForSession = pageIndexBytes + framesIn5Min * frameIndexBytesPerFrame;
    double overheadPercent = 100.0 * totalIndexForSession / (hotBufferMB * 1024 * 1024);
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(overheadPercent);
        state.counters["hot_buffer_mb"] = hotBufferMB;
        state.counters["page_index_mb"] = static_cast<double>(pageIndexBytes) / (1024.0 * 1024.0);
        state.counters["frame_index_kb"] = static_cast<double>(framesIn5Min * frameIndexBytesPerFrame) / 1024.0;
        state.counters["total_index_mb"] = static_cast<double>(totalIndexForSession) / (1024.0 * 1024.0);
        state.counters["overhead_percent"] = overheadPercent;
    }
}
BENCHMARK(BM_TTD_Index_Overhead)
    ->Args({64, 2})
    ->Args({128, 2})
    ->Args({256, 2})
    ->Args({512, 2})
    ->Args({1024, 2})
    ->Unit(benchmark::kMicrosecond);
