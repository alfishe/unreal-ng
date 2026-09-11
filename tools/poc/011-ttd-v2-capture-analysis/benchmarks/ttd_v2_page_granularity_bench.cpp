/// @file ttd_v2_page_granularity_bench.cpp
/// @brief Benchmark page-granular peripheral capture for large state (512KB GS SRAM)
///
/// Current approach captures entire peripheral state every frame.
/// For 512KB GeneralSound SRAM, this means XOR-delta of 512KB = 274us, ~25KB.
/// Proposed: Use COW page-store pattern (same as RAM).

#include <benchmark/benchmark.h>
#include <chrono>
#include <vector>
#include <random>
#include <cstring>

constexpr size_t kPageSize = 4096;

struct PagedPeripheralState {
    size_t pageCount;
    std::vector<uint32_t> pageSlots;
    std::vector<bool> dirtyFlags;
    std::vector<uint8_t> fullState;

    explicit PagedPeripheralState(size_t totalSize)
        : pageCount((totalSize + kPageSize - 1) / kPageSize)
        , pageSlots(pageCount, 0)
        , dirtyFlags(pageCount, false)
        , fullState(totalSize, 0)
    {}
    
    void MarkDirty(size_t pageIdx) {
        if (pageIdx < pageCount) dirtyFlags[pageIdx] = true;
    }
};

static void BM_TTD_Paged_Peripheral_CaptureFull(benchmark::State& state) {
    const size_t totalSize = state.range(0);
    PagedPeripheralState pstate(totalSize);
    std::vector<uint8_t> prevState(totalSize, 0);
    
    std::mt19937 gen(42);
    std::uniform_int_distribution<> byteDist(0, 255);
    for (auto& b : pstate.fullState) b = byteDist(gen);
    prevState = pstate.fullState;
    
    size_t dirtyPages = state.range(1);
    std::uniform_int_distribution<size_t> pageDist(0, pstate.pageCount - 1);
    for (size_t i = 0; i < dirtyPages; ++i) {
        size_t p = pageDist(gen);
        pstate.MarkDirty(p);
        for (size_t b = 0; b < kPageSize && p * kPageSize + b < totalSize; ++b) {
            pstate.fullState[p * kPageSize + b] = byteDist(gen);
        }
    }
    
    for (auto _ : state) {
        std::vector<uint8_t> delta(totalSize);
        for (size_t i = 0; i < totalSize; ++i)
            delta[i] = pstate.fullState[i] ^ prevState[i];
            
        size_t nonZero = 0;
        for (uint8_t b : delta) if (b != 0) ++nonZero;
        
        benchmark::DoNotOptimize(delta);
        state.counters["data_stored_bytes"] = nonZero + (nonZero / 64) * 4;
        state.counters["total_bytes"] = state.counters["data_stored_bytes"];
    }
}
BENCHMARK(BM_TTD_Paged_Peripheral_CaptureFull)
    ->Args({524288, 1})
    ->Args({524288, 2})
    ->Args({1048576, 4})
    ->Unit(benchmark::kMicrosecond);

static void BM_TTD_Paged_Peripheral_CapturePaged(benchmark::State& state) {
    const size_t totalSize = state.range(0);
    PagedPeripheralState pstate(totalSize);
    
    std::mt19937 gen(42);
    std::uniform_int_distribution<> byteDist(0, 255);
    for (auto& b : pstate.fullState) b = byteDist(gen);
    
    size_t dirtyPages = state.range(1);
    std::uniform_int_distribution<size_t> pageDist(0, pstate.pageCount - 1);
    for (size_t i = 0; i < dirtyPages; ++i) {
        size_t p = pageDist(gen);
        pstate.MarkDirty(p);
        for (size_t b = 0; b < kPageSize && p * kPageSize + b < totalSize; ++b) {
            pstate.fullState[p * kPageSize + b] = byteDist(gen);
        }
    }
    
    uint32_t slot = 1;
    for (auto _ : state) {
        size_t dirtyCount = 0;
        for (size_t p = 0; p < pstate.pageCount; ++p) {
            if (pstate.dirtyFlags[p]) {
                ++dirtyCount;
                ++slot;
            }
        }
        benchmark::DoNotOptimize(dirtyCount);
        state.counters["data_stored_bytes"] = dirtyCount * kPageSize;
        state.counters["refs_stored_bytes"] = pstate.pageCount * sizeof(uint32_t);
        state.counters["total_bytes"] = state.counters["data_stored_bytes"] + state.counters["refs_stored_bytes"];
    }
}
BENCHMARK(BM_TTD_Paged_Peripheral_CapturePaged)
    ->Args({524288, 1})
    ->Args({524288, 2})
    ->Args({1048576, 4})
    ->Unit(benchmark::kMicrosecond);
