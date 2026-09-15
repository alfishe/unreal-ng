/**
 * v2_restore_breakdown.cpp — Profile V2 restore to find bottleneck
 */
#include <benchmark/benchmark.h>
#include <zstd.h>
#include <vector>
#include <random>
#include <cstring>

constexpr size_t PAGE_SIZE = 4096;
constexpr size_t PAGES_PER_FRAME = 32;
constexpr size_t FRAME_SIZE = PAGE_SIZE * PAGES_PER_FRAME;

// Benchmark: just memcpy keyframe (128KB)
static void BM_MemcpyKeyframe(benchmark::State& state) {
    std::vector<uint8_t> src(FRAME_SIZE), dst(FRAME_SIZE);
    for (auto _ : state) {
        memcpy(dst.data(), src.data(), FRAME_SIZE);
        benchmark::DoNotOptimize(dst.data());
    }
}

// Benchmark: decompress one page delta
static void BM_DecompressOnePage(benchmark::State& state) {
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    ZSTD_DCtx* dctx = ZSTD_createDCtx();

    // Create sparse XOR delta (~1.5% nonzero)
    std::vector<uint8_t> delta(PAGE_SIZE, 0);
    std::mt19937 rng(42);
    for (size_t i = 0; i < PAGE_SIZE; ++i) {
        if (rng() % 100 < 2) delta[i] = rng() & 0xFF;
    }

    std::vector<uint8_t> compressed(ZSTD_compressBound(PAGE_SIZE));
    size_t csize = ZSTD_compressCCtx(cctx, compressed.data(), compressed.size(),
                                      delta.data(), PAGE_SIZE, 1);
    compressed.resize(csize);

    std::vector<uint8_t> out(PAGE_SIZE);

    for (auto _ : state) {
        ZSTD_decompressDCtx(dctx, out.data(), PAGE_SIZE, compressed.data(), compressed.size());
        benchmark::DoNotOptimize(out.data());
    }

    ZSTD_freeCCtx(cctx);
    ZSTD_freeDCtx(dctx);

    state.counters["compressed_bytes"] = csize;
}

// Benchmark: XOR apply one page
static void BM_XorOnePage(benchmark::State& state) {
    std::vector<uint8_t> page(PAGE_SIZE), delta(PAGE_SIZE);
    std::mt19937 rng(42);
    for (auto& b : delta) b = rng() & 0xFF;

    for (auto _ : state) {
        for (size_t i = 0; i < PAGE_SIZE; ++i) {
            page[i] ^= delta[i];
        }
        benchmark::DoNotOptimize(page.data());
    }
}

// Benchmark: decompress + XOR one page
static void BM_DecompressAndXorOnePage(benchmark::State& state) {
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    ZSTD_DCtx* dctx = ZSTD_createDCtx();

    std::vector<uint8_t> delta(PAGE_SIZE, 0);
    std::mt19937 rng(42);
    for (size_t i = 0; i < PAGE_SIZE; ++i) {
        if (rng() % 100 < 2) delta[i] = rng() & 0xFF;
    }

    std::vector<uint8_t> compressed(ZSTD_compressBound(PAGE_SIZE));
    size_t csize = ZSTD_compressCCtx(cctx, compressed.data(), compressed.size(),
                                      delta.data(), PAGE_SIZE, 1);
    compressed.resize(csize);

    std::vector<uint8_t> page(PAGE_SIZE, 0);
    std::vector<uint8_t> decomp(PAGE_SIZE);

    for (auto _ : state) {
        ZSTD_decompressDCtx(dctx, decomp.data(), PAGE_SIZE, compressed.data(), compressed.size());
        for (size_t i = 0; i < PAGE_SIZE; ++i) {
            page[i] ^= decomp[i];
        }
        benchmark::DoNotOptimize(page.data());
    }

    ZSTD_freeCCtx(cctx);
    ZSTD_freeDCtx(dctx);
}

// Benchmark: full V2 restore with different I-frame intervals
static void BM_V2_RestoreByIframeInterval(benchmark::State& state) {
    const int iframe_interval = state.range(0);

    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    ZSTD_DCtx* dctx = ZSTD_createDCtx();

    // Pre-create compressed page deltas (simulating ~2 pages changed per frame)
    std::vector<uint8_t> delta(PAGE_SIZE, 0);
    std::mt19937 rng(42);
    for (size_t i = 0; i < PAGE_SIZE; ++i) {
        if (rng() % 100 < 2) delta[i] = rng() & 0xFF;
    }

    std::vector<uint8_t> compressed(ZSTD_compressBound(PAGE_SIZE));
    size_t csize = ZSTD_compressCCtx(cctx, compressed.data(), compressed.size(),
                                      delta.data(), PAGE_SIZE, 1);
    compressed.resize(csize);

    std::vector<uint8_t> keyframe(FRAME_SIZE);
    std::vector<uint8_t> restore_buf(FRAME_SIZE);
    std::vector<uint8_t> decomp(PAGE_SIZE);

    std::uniform_int_distribution<> frame_dist(0, 499);

    for (auto _ : state) {
        int target = frame_dist(rng);
        int kf_frame = (target / iframe_interval) * iframe_interval;
        int delta_frames = target - kf_frame;

        // 1. Copy keyframe
        memcpy(restore_buf.data(), keyframe.data(), FRAME_SIZE);

        // 2. Apply deltas (2 pages per frame)
        for (int f = 0; f < delta_frames; ++f) {
            for (int p = 0; p < 2; ++p) {
                ZSTD_decompressDCtx(dctx, decomp.data(), PAGE_SIZE,
                                    compressed.data(), compressed.size());
                uint8_t* page = restore_buf.data() + (p * PAGE_SIZE);
                for (size_t i = 0; i < PAGE_SIZE; ++i) {
                    page[i] ^= decomp[i];
                }
            }
        }

        benchmark::DoNotOptimize(restore_buf.data());
    }

    ZSTD_freeCCtx(cctx);
    ZSTD_freeDCtx(dctx);

    state.counters["avg_delta_frames"] = iframe_interval / 2.0;
    state.counters["avg_decompressions"] = iframe_interval;  // 2 pages × avg frames
}

BENCHMARK(BM_MemcpyKeyframe)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_DecompressOnePage)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_XorOnePage)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_DecompressAndXorOnePage)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_V2_RestoreByIframeInterval)->Arg(10)->Arg(25)->Arg(50)->Unit(benchmark::kMicrosecond)->MinTime(1.0);
