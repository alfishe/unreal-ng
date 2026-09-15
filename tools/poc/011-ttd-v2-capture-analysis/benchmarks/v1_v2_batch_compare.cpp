/**
 * v1_v2_batch_compare.cpp — V1 vs V2 batch=1 vs V2 batch=4
 *
 * Measures restore (unpack + unroll 1 frame to cache) for:
 *   - V1: Full frame snapshot
 *   - V2 batch=1: Per-page XOR deltas
 *   - V2 batch=4: Batched page XOR deltas
 */
#include <benchmark/benchmark.h>
#include <zstd.h>
#include <vector>
#include <array>
#include <random>
#include <cstring>

constexpr size_t PAGE_SIZE = 4096;
constexpr size_t PAGES_PER_FRAME = 32;
constexpr size_t FRAME_SIZE = PAGE_SIZE * PAGES_PER_FRAME;
constexpr int FRAMES_PER_SECOND = 50;
constexpr int I_FRAME_INTERVAL = 50;

// 60% static, 40% active
constexpr int STATIC_PAGES = 20;
constexpr double ACTIVE_PAGE_CHANGE_PROB = 0.15;
constexpr double BYTES_CHANGED_PER_PAGE = 0.015;

class FrameGenerator {
public:
    std::mt19937 rng;
    std::vector<uint8_t> current_frame;
    std::vector<uint8_t> previous_frame;

    FrameGenerator(uint32_t seed = 42) : rng(seed), current_frame(FRAME_SIZE), previous_frame(FRAME_SIZE) {
        std::uniform_int_distribution<> byte_dist(0, 255);
        for (auto& b : current_frame) b = static_cast<uint8_t>(byte_dist(rng));
        previous_frame = current_frame;
    }

    const std::vector<uint8_t>& next_frame() {
        previous_frame = current_frame;
        std::uniform_real_distribution<> prob(0.0, 1.0);
        std::uniform_int_distribution<> byte_dist(0, 255);

        for (int page_idx = STATIC_PAGES; page_idx < static_cast<int>(PAGES_PER_FRAME); ++page_idx) {
            if (prob(rng) < ACTIVE_PAGE_CHANGE_PROB) {
                uint8_t* page = current_frame.data() + page_idx * PAGE_SIZE;
                for (size_t j = 0; j < PAGE_SIZE; ++j) {
                    if (prob(rng) < BYTES_CHANGED_PER_PAGE) {
                        page[j] = static_cast<uint8_t>(byte_dist(rng));
                    }
                }
            }
        }
        return current_frame;
    }
};

//------------------------------------------------------------------------------
// V1: Full snapshot restore
//------------------------------------------------------------------------------
static void BM_V1_Restore(benchmark::State& state) {
    FrameGenerator gen;
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    ZSTD_DCtx* dctx = ZSTD_createDCtx();

    // Compress frames
    std::vector<std::vector<uint8_t>> compressed;
    size_t total_size = 0;

    for (int f = 0; f < 500; ++f) {
        const auto& frame = gen.next_frame();
        std::vector<uint8_t> buf(ZSTD_compressBound(FRAME_SIZE));
        size_t csize = ZSTD_compressCCtx(cctx, buf.data(), buf.size(), frame.data(), FRAME_SIZE, 1);
        buf.resize(csize);
        total_size += csize;
        compressed.push_back(std::move(buf));
    }

    std::vector<uint8_t> restore_buf(FRAME_SIZE);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<> dist(0, 499);

    for (auto _ : state) {
        int target = dist(rng);
        ZSTD_decompressDCtx(dctx, restore_buf.data(), FRAME_SIZE,
                            compressed[target].data(), compressed[target].size());
        benchmark::DoNotOptimize(restore_buf.data());
    }

    ZSTD_freeCCtx(cctx);
    ZSTD_freeDCtx(dctx);

    state.counters["storage_KB"] = total_size / 1024.0;
    state.counters["KB/frame"] = (total_size / 500.0) / 1024.0;
}

//------------------------------------------------------------------------------
// V2: Page XOR deltas (batch=1 or batch=N)
//------------------------------------------------------------------------------
static void BM_V2_Restore(benchmark::State& state) {
    const int batch_size = state.range(0);

    FrameGenerator gen;
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    ZSTD_DCtx* dctx = ZSTD_createDCtx();

    struct FrameDeltas {
        std::vector<std::vector<uint8_t>> batches;  // Compressed batches
        std::vector<int> pages_in_batch;  // How many pages per batch
        std::vector<int> page_indices;  // Which pages changed
    };

    std::vector<FrameDeltas> frames(500);
    std::vector<std::vector<uint8_t>> keyframes;
    std::vector<uint8_t> prev_frame(FRAME_SIZE);
    std::vector<uint8_t> xor_buf(PAGE_SIZE);
    std::vector<uint8_t> compress_buf(ZSTD_compressBound(PAGE_SIZE * batch_size));

    size_t total_size = 0;

    for (int f = 0; f < 500; ++f) {
        const auto& frame = gen.next_frame();

        if (f % I_FRAME_INTERVAL == 0) {
            keyframes.push_back(frame);
        }

        // Collect changed pages
        std::vector<std::vector<uint8_t>> page_deltas;
        for (size_t p = 0; p < PAGES_PER_FRAME; ++p) {
            const uint8_t* curr = frame.data() + p * PAGE_SIZE;
            const uint8_t* prev = prev_frame.data() + p * PAGE_SIZE;

            if (f == 0 || memcmp(curr, prev, PAGE_SIZE) != 0) {
                frames[f].page_indices.push_back(p);
                std::vector<uint8_t> delta(PAGE_SIZE);
                for (size_t i = 0; i < PAGE_SIZE; ++i) {
                    delta[i] = curr[i] ^ prev[i];
                }
                page_deltas.push_back(std::move(delta));
            }
        }

        // Compress in batches
        for (size_t i = 0; i < page_deltas.size(); i += batch_size) {
            size_t batch_end = std::min(i + static_cast<size_t>(batch_size), page_deltas.size());
            int pages_this_batch = batch_end - i;

            // Concatenate pages in batch
            std::vector<uint8_t> batch_data;
            batch_data.reserve(pages_this_batch * PAGE_SIZE);
            for (size_t j = i; j < batch_end; ++j) {
                batch_data.insert(batch_data.end(), page_deltas[j].begin(), page_deltas[j].end());
            }

            size_t csize = ZSTD_compressCCtx(cctx, compress_buf.data(), compress_buf.size(),
                                              batch_data.data(), batch_data.size(), 1);
            frames[f].batches.emplace_back(compress_buf.begin(), compress_buf.begin() + csize);
            frames[f].pages_in_batch.push_back(pages_this_batch);
            total_size += csize;
        }

        prev_frame = frame;
    }

    std::vector<uint8_t> restore_buf(FRAME_SIZE);
    std::vector<uint8_t> decomp_buf(PAGE_SIZE * batch_size);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<> dist(0, 499);

    for (auto _ : state) {
        int target = dist(rng);
        int kf_idx = target / I_FRAME_INTERVAL;
        int kf_frame = kf_idx * I_FRAME_INTERVAL;

        // Copy keyframe
        memcpy(restore_buf.data(), keyframes[kf_idx].data(), FRAME_SIZE);

        // Apply deltas from keyframe to target
        for (int f = kf_frame + 1; f <= target; ++f) {
            int page_offset = 0;
            for (size_t b = 0; b < frames[f].batches.size(); ++b) {
                int pages_this_batch = frames[f].pages_in_batch[b];

                // Decompress batch
                ZSTD_decompressDCtx(dctx, decomp_buf.data(), pages_this_batch * PAGE_SIZE,
                                    frames[f].batches[b].data(), frames[f].batches[b].size());

                // Apply XOR to each page in batch
                for (int p = 0; p < pages_this_batch; ++p) {
                    int page_idx = frames[f].page_indices[page_offset + p];
                    const uint8_t* delta = decomp_buf.data() + p * PAGE_SIZE;
                    uint8_t* page = restore_buf.data() + page_idx * PAGE_SIZE;
                    for (size_t i = 0; i < PAGE_SIZE; ++i) {
                        page[i] ^= delta[i];
                    }
                }
                page_offset += pages_this_batch;
            }
        }

        benchmark::DoNotOptimize(restore_buf.data());
    }

    ZSTD_freeCCtx(cctx);
    ZSTD_freeDCtx(dctx);

    state.counters["storage_KB"] = total_size / 1024.0;
    state.counters["KB/frame"] = (total_size / 500.0) / 1024.0;
}

BENCHMARK(BM_V1_Restore)->Unit(benchmark::kMicrosecond)->MinTime(1.0);
BENCHMARK(BM_V2_Restore)->Arg(1)->Arg(4)->Arg(8)->Unit(benchmark::kMicrosecond)->MinTime(1.0);
