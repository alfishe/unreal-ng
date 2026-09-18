/**
 * batch_size_bench.cpp — Measure real restore latency for page batching.
 *
 * Restore = decompress batch + extract page + XOR apply to RAM buffer.
 * Tests batch sizes 1, 2, 4, 8, 16, 32 pages.
 */
#include <benchmark/benchmark.h>
#include <zstd.h>
#include <vector>
#include <random>
#include <cstring>
#include <numeric>
#include <algorithm>

constexpr size_t PAGE_SIZE = 4096;

// Generate realistic XOR delta: sparse with ~1-2% nonzero bytes
static std::vector<uint8_t> GenerateSparseDelta(size_t size, double nonzero_frac, std::mt19937& rng) {
    std::vector<uint8_t> data(size, 0);
    std::uniform_real_distribution<> prob(0.0, 1.0);
    std::uniform_int_distribution<> byte_val(1, 255);

    for (size_t i = 0; i < size; ++i) {
        if (prob(rng) < nonzero_frac) {
            data[i] = static_cast<uint8_t>(byte_val(rng));
        }
    }
    return data;
}

// Compress batch of pages
static std::vector<uint8_t> CompressBatch(const std::vector<std::vector<uint8_t>>& pages) {
    std::vector<uint8_t> concatenated;
    concatenated.reserve(pages.size() * PAGE_SIZE);
    for (const auto& page : pages) {
        concatenated.insert(concatenated.end(), page.begin(), page.end());
    }

    std::vector<uint8_t> compressed(ZSTD_compressBound(concatenated.size()));
    size_t csize = ZSTD_compress(compressed.data(), compressed.size(),
                                  concatenated.data(), concatenated.size(), 1);
    compressed.resize(csize);
    return compressed;
}

// Benchmark: restore single page from batch
static void BM_RestoreFromBatch(benchmark::State& state) {
    const int batch_size = state.range(0);
    const double nonzero_frac = 0.015;  // ~1.5% typical

    std::mt19937 rng(42);

    // Generate pages and compress into batches
    constexpr int NUM_BATCHES = 100;
    std::vector<std::vector<uint8_t>> compressed_batches;
    compressed_batches.reserve(NUM_BATCHES);

    size_t total_raw = 0, total_compressed = 0;

    for (int b = 0; b < NUM_BATCHES; ++b) {
        std::vector<std::vector<uint8_t>> pages;
        pages.reserve(batch_size);
        for (int p = 0; p < batch_size; ++p) {
            pages.push_back(GenerateSparseDelta(PAGE_SIZE, nonzero_frac, rng));
        }
        total_raw += batch_size * PAGE_SIZE;
        auto compressed = CompressBatch(pages);
        total_compressed += compressed.size();
        compressed_batches.push_back(std::move(compressed));
    }

    // Decompression context (reusable)
    ZSTD_DCtx* dctx = ZSTD_createDCtx();

    // RAM page buffer (simulates precache target)
    std::vector<uint8_t> ram_page(PAGE_SIZE, 0);

    // Decompression buffer
    std::vector<uint8_t> decomp_buf(batch_size * PAGE_SIZE);

    std::uniform_int_distribution<> batch_pick(0, NUM_BATCHES - 1);
    std::uniform_int_distribution<> page_pick(0, batch_size - 1);

    for (auto _ : state) {
        // Pick random batch and page
        int batch_idx = batch_pick(rng);
        int page_idx = page_pick(rng);

        const auto& compressed = compressed_batches[batch_idx];

        // 1. Decompress batch
        size_t dsize = ZSTD_decompressDCtx(dctx, decomp_buf.data(), decomp_buf.size(),
                                            compressed.data(), compressed.size());
        benchmark::DoNotOptimize(dsize);

        // 2. Extract target page
        const uint8_t* page_data = decomp_buf.data() + page_idx * PAGE_SIZE;

        // 3. XOR apply to RAM buffer
        for (size_t i = 0; i < PAGE_SIZE; ++i) {
            ram_page[i] ^= page_data[i];
        }
        benchmark::DoNotOptimize(ram_page.data());
    }

    ZSTD_freeDCtx(dctx);

    // Report metrics
    double bytes_per_page = static_cast<double>(total_compressed) / (NUM_BATCHES * batch_size);
    state.counters["bytes/page"] = bytes_per_page;
    state.counters["ratio"] = (PAGE_SIZE * batch_size * NUM_BATCHES) / static_cast<double>(total_compressed);
}

BENCHMARK(BM_RestoreFromBatch)
    ->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16)->Arg(32)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(1.0);

// Benchmark: decompress only (no XOR)
static void BM_DecompressOnly(benchmark::State& state) {
    const int batch_size = state.range(0);
    const double nonzero_frac = 0.015;

    std::mt19937 rng(42);

    // Generate and compress one batch
    std::vector<std::vector<uint8_t>> pages;
    for (int p = 0; p < batch_size; ++p) {
        pages.push_back(GenerateSparseDelta(PAGE_SIZE, nonzero_frac, rng));
    }
    auto compressed = CompressBatch(pages);

    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    std::vector<uint8_t> decomp_buf(batch_size * PAGE_SIZE);

    for (auto _ : state) {
        size_t dsize = ZSTD_decompressDCtx(dctx, decomp_buf.data(), decomp_buf.size(),
                                            compressed.data(), compressed.size());
        benchmark::DoNotOptimize(dsize);
    }

    ZSTD_freeDCtx(dctx);

    state.counters["compressed_size"] = compressed.size();
}

BENCHMARK(BM_DecompressOnly)
    ->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16)->Arg(32)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(1.0);

// Benchmark: XOR apply only (baseline)
static void BM_XorApplyOnly(benchmark::State& state) {
    std::vector<uint8_t> src(PAGE_SIZE);
    std::vector<uint8_t> dst(PAGE_SIZE, 0);

    std::mt19937 rng(42);
    std::generate(src.begin(), src.end(), [&]() { return rng() & 0xFF; });

    for (auto _ : state) {
        for (size_t i = 0; i < PAGE_SIZE; ++i) {
            dst[i] ^= src[i];
        }
        benchmark::DoNotOptimize(dst.data());
    }
}

BENCHMARK(BM_XorApplyOnly)->Unit(benchmark::kNanosecond)->MinTime(1.0);
