// tools/poc/011-ttd-v2-capture-analysis/benchmarks/ttd_v2_tiered_storage_bench.cpp
// Tiered Storage Simulation Benchmark: LZ4-fast (Hot RAM Tier) vs. Zstd-1 (Cold File Tier) with LRU Eviction

#include <benchmark/benchmark.h>
#include <lz4.h>
#include <zstd.h>
#include <vector>
#include <list>
#include <unordered_map>
#include <chrono>
#include <numeric>
#include <random>
#include <cstring>
#include <cstdint>

namespace {

// Helper to generate realistic XOR delta buffer (4KB page with 1% sparsity ~ 40 dirty bytes)
std::vector<uint8_t> GenerateRealisticXorDelta(size_t size = 4096, double nonzeroRate = 0.01) {
    std::vector<uint8_t> buffer(size, 0);
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    std::uniform_int_distribution<uint16_t> valDist(1, 255);

    for (size_t i = 0; i < size; ++i) {
        if (dist(rng) < nonzeroRate) {
            buffer[i] = static_cast<uint8_t>(valDist(rng));
        }
    }
    return buffer;
}

// Helper to generate realistic dense struct delta (164B CPU struct with 30% mutation)
std::vector<uint8_t> GenerateDenseStructDelta(size_t size = 164, double nonzeroRate = 0.30) {
    std::vector<uint8_t> buffer(size, 0);
    std::mt19937 rng(1337);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    std::uniform_int_distribution<uint16_t> valDist(1, 255);

    for (size_t i = 0; i < size; ++i) {
        if (dist(rng) < nonzeroRate) {
            buffer[i] = static_cast<uint8_t>(valDist(rng));
        }
    }
    return buffer;
}

// -----------------------------------------------------------------------------
// 1. Hot Tier (LZ4-fast) vs Cold Tier (Zstd-1) 4KB Page Compression Benchmark
// -----------------------------------------------------------------------------

static void BM_TTD_TieredStorage_LZ4_Fast_Encode_4KB(benchmark::State& state) {
    auto payload = GenerateRealisticXorDelta(4096, 0.01);
    std::vector<char> compressed(LZ4_compressBound(static_cast<int>(payload.size())));

    for (auto _ : state) {
        int compSize = LZ4_compress_default(
            reinterpret_cast<const char*>(payload.data()),
            compressed.data(),
            static_cast<int>(payload.size()),
            static_cast<int>(compressed.size())
        );
        benchmark::DoNotOptimize(compSize);
    }
    state.SetBytesProcessed(state.iterations() * payload.size());
}
BENCHMARK(BM_TTD_TieredStorage_LZ4_Fast_Encode_4KB);

static void BM_TTD_TieredStorage_Zstd1_Encode_4KB(benchmark::State& state) {
    auto payload = GenerateRealisticXorDelta(4096, 0.01);
    std::vector<uint8_t> compressed(ZSTD_compressBound(payload.size()));

    for (auto _ : state) {
        size_t compSize = ZSTD_compress(
            compressed.data(),
            compressed.size(),
            payload.data(),
            payload.size(),
            1 // Level 1 (Zstd-1 fast)
        );
        benchmark::DoNotOptimize(compSize);
    }
    state.SetBytesProcessed(state.iterations() * payload.size());
}
BENCHMARK(BM_TTD_TieredStorage_Zstd1_Encode_4KB);

// -----------------------------------------------------------------------------
// 2. Hot Tier (LZ4-fast) vs Cold Tier (Zstd-1) 4KB Page Decompress / Restore
// -----------------------------------------------------------------------------

static void BM_TTD_TieredStorage_LZ4_Fast_Decode_4KB(benchmark::State& state) {
    auto payload = GenerateRealisticXorDelta(4096, 0.01);
    std::vector<char> compressed(LZ4_compressBound(static_cast<int>(payload.size())));
    int compSize = LZ4_compress_default(
        reinterpret_cast<const char*>(payload.data()),
        compressed.data(),
        static_cast<int>(payload.size()),
        static_cast<int>(compressed.size())
    );

    std::vector<char> restored(4096);
    for (auto _ : state) {
        int decompBytes = LZ4_decompress_safe(
            compressed.data(),
            restored.data(),
            compSize,
            static_cast<int>(restored.size())
        );
        benchmark::DoNotOptimize(decompBytes);
    }
    state.SetBytesProcessed(state.iterations() * payload.size());
}
BENCHMARK(BM_TTD_TieredStorage_LZ4_Fast_Decode_4KB);

static void BM_TTD_TieredStorage_Zstd1_Decode_4KB(benchmark::State& state) {
    auto payload = GenerateRealisticXorDelta(4096, 0.01);
    std::vector<uint8_t> compressed(ZSTD_compressBound(payload.size()));
    size_t compSize = ZSTD_compress(
        compressed.data(),
        compressed.size(),
        payload.data(),
        payload.size(),
        1
    );

    std::vector<uint8_t> restored(4096);
    for (auto _ : state) {
        size_t decompBytes = ZSTD_decompress(
            restored.data(),
            restored.size(),
            compressed.data(),
            compSize
        );
        benchmark::DoNotOptimize(decompBytes);
    }
    state.SetBytesProcessed(state.iterations() * payload.size());
}
BENCHMARK(BM_TTD_TieredStorage_Zstd1_Decode_4KB);

// -----------------------------------------------------------------------------
// 3. Dense Struct (164B) LZ4-fast vs Zstd-1 Comparison
// -----------------------------------------------------------------------------

static void BM_TTD_TieredStorage_DenseStruct_LZ4_vs_Zstd(benchmark::State& state) {
    auto structPayload = GenerateDenseStructDelta(164, 0.30);

    // LZ4
    std::vector<char> lz4Compressed(LZ4_compressBound(164));
    int lz4CompSize = LZ4_compress_default(
        reinterpret_cast<const char*>(structPayload.data()),
        lz4Compressed.data(),
        164,
        static_cast<int>(lz4Compressed.size())
    );

    // Zstd-1
    std::vector<uint8_t> zstdCompressed(ZSTD_compressBound(164));
    size_t zstdCompSize = ZSTD_compress(
        zstdCompressed.data(),
        zstdCompressed.size(),
        structPayload.data(),
        164,
        1
    );

    state.counters["raw_bytes"] = 164.0;
    state.counters["lz4_compressed_bytes"] = static_cast<double>(lz4CompSize);
    state.counters["zstd_compressed_bytes"] = static_cast<double>(zstdCompSize);
    state.counters["lz4_compression_ratio"] = 164.0 / std::max(1, lz4CompSize);
    state.counters["zstd_compression_ratio"] = 164.0 / std::max<size_t>(1, zstdCompSize);

    for (auto _ : state) {
        int cSize = LZ4_compress_default(
            reinterpret_cast<const char*>(structPayload.data()),
            lz4Compressed.data(),
            164,
            static_cast<int>(lz4Compressed.size())
        );
        benchmark::DoNotOptimize(cSize);
    }
}
BENCHMARK(BM_TTD_TieredStorage_DenseStruct_LZ4_vs_Zstd);

// -----------------------------------------------------------------------------
// 4. LRU Eviction & Tiered Storage Pipeline Simulation
// -----------------------------------------------------------------------------

struct HotFrameSlot {
    uint32_t frameId;
    std::vector<uint8_t> rawXorBuffer;
    std::vector<char> lz4Compressed;
    int lz4Size;
};

struct ColdFrameSlot {
    uint32_t frameId;
    std::vector<uint8_t> zstdCompressed;
    size_t zstdSize;
};

class TieredStorageSimulation {
public:
    explicit TieredStorageSimulation(size_t hotCapacityFrames = 50)
        : m_hotCapacity(hotCapacityFrames) {}

    void CaptureFrame(uint32_t frameId, const std::vector<uint8_t>& xorDelta) {
        // Step 1: Fast compress with LZ4 for hot RAM buffer
        std::vector<char> lz4Buffer(LZ4_compressBound(static_cast<int>(xorDelta.size())));
        int compSize = LZ4_compress_default(
            reinterpret_cast<const char*>(xorDelta.data()),
            lz4Buffer.data(),
            static_cast<int>(xorDelta.size()),
            static_cast<int>(lz4Buffer.size())
        );
        lz4Buffer.resize(compSize);

        HotFrameSlot hotSlot{frameId, xorDelta, std::move(lz4Buffer), compSize};

        // If hot buffer is full, evict LRU frame to cold tier (Zstd-1)
        if (m_hotMap.size() >= m_hotCapacity) {
            uint32_t evictedId = m_lruList.back();
            m_lruList.pop_back();
            
            auto it = m_hotMap.find(evictedId);
            if (it != m_hotMap.end()) {
                EvictToColdTier(it->second);
                m_hotMap.erase(it);
            }
        }

        // Insert into hot LRU ring
        m_lruList.push_front(frameId);
        m_hotMap[frameId] = std::move(hotSlot);
    }

    bool RestoreFrame(uint32_t frameId, std::vector<uint8_t>& outRestored) {
        outRestored.resize(4096);
        // Check Hot Tier
        auto hotIt = m_hotMap.find(frameId);
        if (hotIt != m_hotMap.end()) {
            // Touch LRU
            m_lruList.remove(frameId);
            m_lruList.push_front(frameId);

            LZ4_decompress_safe(
                hotIt->second.lz4Compressed.data(),
                reinterpret_cast<char*>(outRestored.data()),
                hotIt->second.lz4Size,
                4096
            );
            return true; // Hot hit
        }

        // Check Cold Tier
        auto coldIt = m_coldMap.find(frameId);
        if (coldIt != m_coldMap.end()) {
            ZSTD_decompress(
                outRestored.data(),
                4096,
                coldIt->second.zstdCompressed.data(),
                coldIt->second.zstdSize
            );
            return false; // Cold hit
        }

        return false;
    }

    size_t GetHotMemoryBytes() const {
        size_t total = 0;
        for (const auto& kv : m_hotMap) {
            total += kv.second.lz4Compressed.size() + sizeof(HotFrameSlot);
        }
        return total;
    }

    size_t GetColdMemoryBytes() const {
        size_t total = 0;
        for (const auto& kv : m_coldMap) {
            total += kv.second.zstdCompressed.size() + sizeof(ColdFrameSlot);
        }
        return total;
    }

private:
    void EvictToColdTier(const HotFrameSlot& hotSlot) {
        std::vector<uint8_t> zstdBuf(ZSTD_compressBound(hotSlot.rawXorBuffer.size()));
        size_t zSize = ZSTD_compress(
            zstdBuf.data(),
            zstdBuf.size(),
            hotSlot.rawXorBuffer.data(),
            hotSlot.rawXorBuffer.size(),
            1
        );
        zstdBuf.resize(zSize);

        m_coldMap[hotSlot.frameId] = ColdFrameSlot{hotSlot.frameId, std::move(zstdBuf), zSize};
    }

    size_t m_hotCapacity;
    std::list<uint32_t> m_lruList;
    std::unordered_map<uint32_t, HotFrameSlot> m_hotMap;
    std::unordered_map<uint32_t, ColdFrameSlot> m_coldMap;
};

static void BM_TTD_TieredStorage_LRU_Eviction_Pipeline(benchmark::State& state) {
    auto frameDelta = GenerateRealisticXorDelta(4096, 0.01);
    const size_t hotCapacity = 50;
    const size_t totalFrames = 200;

    for (auto _ : state) {
        TieredStorageSimulation storage(hotCapacity);
        for (size_t f = 0; f < totalFrames; ++f) {
            storage.CaptureFrame(static_cast<uint32_t>(f), frameDelta);
        }

        // Perform timeline scrubbing (mix of hot and cold restores)
        std::vector<uint8_t> restored(4096);
        // Hot hits (recent frames)
        for (uint32_t f = 160; f < 200; ++f) {
            storage.RestoreFrame(f, restored);
        }
        // Cold hits (older frames evicted to Zstd-1)
        for (uint32_t f = 10; f < 30; ++f) {
            storage.RestoreFrame(f, restored);
        }

        benchmark::DoNotOptimize(storage.GetHotMemoryBytes());
        benchmark::DoNotOptimize(storage.GetColdMemoryBytes());
    }
}
BENCHMARK(BM_TTD_TieredStorage_LRU_Eviction_Pipeline);

// -----------------------------------------------------------------------------
// 5. Long-Session 5-Minute Sustained Capture (15,000 Frames) with LRU Eviction
// -----------------------------------------------------------------------------
// Simulates a full 5-minute recording session at 50Hz to measure:
//   - Sustained eviction throughput (LZ4 -> Zstd re-compression under load)
//   - Peak cold-tier memory (simulating cumulative disk write volume)
//   - Hot vs Cold hit ratio during post-capture timeline scrubbing
//   - Per-frame amortized capture cost over 15,000 frames

static void BM_TTD_TieredStorage_LongSession_5Min(benchmark::State& state) {
    const size_t hotCapacity = static_cast<size_t>(state.range(0));
    const size_t totalFrames = 15000;  // 5 minutes @ 50Hz
    const size_t dirtyPagesPerFrame = 4;  // Typical: VRAM + stack + vars + page

    // Pre-generate dirty page deltas (4 realistic 4KB XOR buffers per frame)
    std::vector<std::vector<uint8_t>> pageDeltas;
    pageDeltas.reserve(dirtyPagesPerFrame);
    for (size_t p = 0; p < dirtyPagesPerFrame; ++p) {
        pageDeltas.push_back(GenerateRealisticXorDelta(4096, 0.01));
    }

    for (auto _ : state) {
        TieredStorageSimulation storage(hotCapacity);

        // Phase 1: Sustained 5-minute capture with LRU eviction
        auto captureStart = std::chrono::high_resolution_clock::now();
        for (size_t f = 0; f < totalFrames; ++f) {
            for (size_t p = 0; p < dirtyPagesPerFrame; ++p) {
                uint32_t slotId = static_cast<uint32_t>(f * dirtyPagesPerFrame + p);
                storage.CaptureFrame(slotId, pageDeltas[p]);
            }
        }
        auto captureEnd = std::chrono::high_resolution_clock::now();

        double captureMs = std::chrono::duration<double, std::milli>(captureEnd - captureStart).count();

        // Phase 2: Timeline scrubbing (mix of hot and cold restores)
        std::vector<uint8_t> restored(4096);
        size_t hotHits = 0;
        size_t coldHits = 0;

        // Scrub recent frames (should be hot hits)
        for (uint32_t f = static_cast<uint32_t>((totalFrames - 10) * dirtyPagesPerFrame);
             f < static_cast<uint32_t>(totalFrames * dirtyPagesPerFrame); ++f) {
            if (storage.RestoreFrame(f, restored)) hotHits++; else coldHits++;
        }

        // Scrub old frames (should be cold hits)
        for (uint32_t f = 0; f < 40; ++f) {
            if (storage.RestoreFrame(f, restored)) hotHits++; else coldHits++;
        }

        // Scrub mid-session frames
        uint32_t midBase = static_cast<uint32_t>(7500 * dirtyPagesPerFrame);
        for (uint32_t f = midBase; f < midBase + 40; ++f) {
            if (storage.RestoreFrame(f, restored)) hotHits++; else coldHits++;
        }

        size_t hotBytes = storage.GetHotMemoryBytes();
        size_t coldBytes = storage.GetColdMemoryBytes();
        benchmark::DoNotOptimize(hotBytes);
        benchmark::DoNotOptimize(coldBytes);

        state.counters["total_frames"] = static_cast<double>(totalFrames);
        state.counters["hot_capacity"] = static_cast<double>(hotCapacity);
        state.counters["dirty_pages_per_frame"] = static_cast<double>(dirtyPagesPerFrame);
        state.counters["capture_ms"] = captureMs;
        state.counters["per_frame_us"] = (captureMs * 1000.0) / totalFrames;
        state.counters["hot_mem_mb"] = static_cast<double>(hotBytes) / (1024.0 * 1024.0);
        state.counters["cold_mem_mb"] = static_cast<double>(coldBytes) / (1024.0 * 1024.0);
        state.counters["total_mem_mb"] = static_cast<double>(hotBytes + coldBytes) / (1024.0 * 1024.0);
        state.counters["hot_hits"] = static_cast<double>(hotHits);
        state.counters["cold_hits"] = static_cast<double>(coldHits);
        state.counters["evicted_frames"] = static_cast<double>(
            totalFrames * dirtyPagesPerFrame > hotCapacity
                ? totalFrames * dirtyPagesPerFrame - hotCapacity : 0);
    }
}
BENCHMARK(BM_TTD_TieredStorage_LongSession_5Min)
    ->Arg(50)     // Small hot buffer: aggressive eviction
    ->Arg(200)    // Medium hot buffer
    ->Arg(1000)   // Large hot buffer: less eviction pressure
    ->Iterations(3)
    ->Unit(benchmark::kMillisecond);

// -----------------------------------------------------------------------------
// 6. Multi-Page Per-Frame Tiered Pipeline (Realistic Aggregate Cost)
// -----------------------------------------------------------------------------
// Real Z80 frames produce 2-12 dirty 4KB sub-pages. This benchmark measures
// aggregate per-frame cost of compressing + storing multiple dirty pages
// through the full LRU tiered pipeline.

static void BM_TTD_TieredStorage_MultiPage_Frame(benchmark::State& state) {
    const size_t dirtyPagesPerFrame = static_cast<size_t>(state.range(0));
    const size_t hotCapacity = 200;
    const size_t totalFrames = 500;

    // Pre-generate page deltas with varying sparsity
    std::vector<std::vector<uint8_t>> pageDeltas;
    pageDeltas.reserve(dirtyPagesPerFrame);
    for (size_t p = 0; p < dirtyPagesPerFrame; ++p) {
        // VRAM pages are denser (~2-5%), stack/vars are sparser (~0.5-1%)
        double sparsity = (p < 2) ? 0.03 : 0.01;
        pageDeltas.push_back(GenerateRealisticXorDelta(4096, sparsity));
    }

    std::vector<double> perFrameLatencies;
    perFrameLatencies.reserve(totalFrames);

    for (auto _ : state) {
        TieredStorageSimulation storage(hotCapacity);
        perFrameLatencies.clear();

        for (size_t f = 0; f < totalFrames; ++f) {
            auto frameStart = std::chrono::high_resolution_clock::now();

            for (size_t p = 0; p < dirtyPagesPerFrame; ++p) {
                uint32_t slotId = static_cast<uint32_t>(f * dirtyPagesPerFrame + p);
                storage.CaptureFrame(slotId, pageDeltas[p]);
            }

            auto frameEnd = std::chrono::high_resolution_clock::now();
            double latencyUs = std::chrono::duration<double, std::micro>(frameEnd - frameStart).count();
            perFrameLatencies.push_back(latencyUs);
        }

        benchmark::DoNotOptimize(storage.GetHotMemoryBytes());
        benchmark::DoNotOptimize(storage.GetColdMemoryBytes());

        if (!perFrameLatencies.empty()) {
            double sum = std::accumulate(perFrameLatencies.begin(), perFrameLatencies.end(), 0.0);
            double mean = sum / perFrameLatencies.size();
            double maxVal = *std::max_element(perFrameLatencies.begin(), perFrameLatencies.end());

            std::sort(perFrameLatencies.begin(), perFrameLatencies.end());
            size_t n = perFrameLatencies.size();

            state.counters["dirty_pages"] = static_cast<double>(dirtyPagesPerFrame);
            state.counters["mean_frame_us"] = mean;
            state.counters["p50_frame_us"] = perFrameLatencies[n / 2];
            state.counters["p95_frame_us"] = perFrameLatencies[static_cast<size_t>(0.95 * (n - 1))];
            state.counters["p99_frame_us"] = perFrameLatencies[static_cast<size_t>(0.99 * (n - 1))];
            state.counters["max_frame_us"] = maxVal;
            state.counters["cpu_pct_of_20ms"] = (mean / 20000.0) * 100.0;
        }
    }
}
BENCHMARK(BM_TTD_TieredStorage_MultiPage_Frame)
    ->Arg(2)    // Minimal: stack + 1 VRAM page
    ->Arg(4)    // Typical: VRAM + stack + vars + paged bank
    ->Arg(6)    // Heavy: multi-bank screen update + game state
    ->Arg(12)   // Extreme: 14MHz turbo scattered writes
    ->Iterations(5)
    ->Unit(benchmark::kMicrosecond);

} // namespace
