/**
 * iframe_interval_tradeoff.cpp — I-frame interval vs storage/restore trade-off
 */
#include <benchmark/benchmark.h>
#include <zstd.h>
#include <vector>
#include <random>
#include <cstring>

constexpr size_t PAGE_SIZE = 4096;
constexpr size_t PAGES_PER_FRAME = 32;
constexpr size_t FRAME_SIZE = PAGE_SIZE * PAGES_PER_FRAME;
constexpr int STATIC_PAGES = 20;
constexpr double ACTIVE_PAGE_CHANGE_PROB = 0.15;
constexpr double BYTES_CHANGED_PER_PAGE = 0.015;

class FrameGenerator {
public:
    std::mt19937 rng;
    std::vector<uint8_t> current_frame, previous_frame;

    FrameGenerator(uint32_t seed = 42) : rng(seed), current_frame(FRAME_SIZE), previous_frame(FRAME_SIZE) {
        std::uniform_int_distribution<> d(0, 255);
        for (auto& b : current_frame) b = d(rng);
        previous_frame = current_frame;
    }

    const std::vector<uint8_t>& next_frame() {
        previous_frame = current_frame;
        std::uniform_real_distribution<> prob(0.0, 1.0);
        std::uniform_int_distribution<> byte_dist(0, 255);
        for (int p = STATIC_PAGES; p < static_cast<int>(PAGES_PER_FRAME); ++p) {
            if (prob(rng) < ACTIVE_PAGE_CHANGE_PROB) {
                uint8_t* page = current_frame.data() + p * PAGE_SIZE;
                for (size_t j = 0; j < PAGE_SIZE; ++j) {
                    if (prob(rng) < BYTES_CHANGED_PER_PAGE) page[j] = byte_dist(rng);
                }
            }
        }
        return current_frame;
    }
};

static void BM_V2_IframeTradeoff(benchmark::State& state) {
    const int iframe_interval = state.range(0);
    const int num_frames = 500;  // 10 seconds

    FrameGenerator gen(42);
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    ZSTD_DCtx* dctx = ZSTD_createDCtx();

    struct FrameData {
        std::vector<std::vector<uint8_t>> page_deltas;
        std::vector<int> page_indices;
    };

    std::vector<FrameData> frames(num_frames);
    std::vector<std::vector<uint8_t>> keyframes;
    std::vector<uint8_t> prev_frame(FRAME_SIZE);
    std::vector<uint8_t> xor_buf(PAGE_SIZE);
    std::vector<uint8_t> compress_buf(ZSTD_compressBound(PAGE_SIZE));

    size_t delta_storage = 0;
    size_t keyframe_storage = 0;

    for (int f = 0; f < num_frames; ++f) {
        const auto& frame = gen.next_frame();

        if (f % iframe_interval == 0) {
            // Store compressed keyframe
            std::vector<uint8_t> kf_compressed(ZSTD_compressBound(FRAME_SIZE));
            size_t kf_size = ZSTD_compressCCtx(cctx, kf_compressed.data(), kf_compressed.size(),
                                                frame.data(), FRAME_SIZE, 1);
            kf_compressed.resize(kf_size);
            keyframe_storage += kf_size;
            keyframes.push_back(frame);  // Keep uncompressed for restore
        }

        // Store page deltas
        for (size_t p = 0; p < PAGES_PER_FRAME; ++p) {
            const uint8_t* curr = frame.data() + p * PAGE_SIZE;
            const uint8_t* prev = prev_frame.data() + p * PAGE_SIZE;

            if (f == 0 || memcmp(curr, prev, PAGE_SIZE) != 0) {
                frames[f].page_indices.push_back(p);
                for (size_t i = 0; i < PAGE_SIZE; ++i) xor_buf[i] = curr[i] ^ prev[i];
                size_t csize = ZSTD_compressCCtx(cctx, compress_buf.data(), compress_buf.size(),
                                                  xor_buf.data(), PAGE_SIZE, 1);
                frames[f].page_deltas.emplace_back(compress_buf.begin(), compress_buf.begin() + csize);
                delta_storage += csize;
            }
        }
        prev_frame = frame;
    }

    std::vector<uint8_t> restore_buf(FRAME_SIZE);
    std::vector<uint8_t> decomp(PAGE_SIZE);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<> dist(0, num_frames - 1);

    for (auto _ : state) {
        int target = dist(rng);
        int kf_idx = target / iframe_interval;
        int kf_frame = kf_idx * iframe_interval;

        memcpy(restore_buf.data(), keyframes[kf_idx].data(), FRAME_SIZE);

        for (int f = kf_frame + 1; f <= target; ++f) {
            for (size_t i = 0; i < frames[f].page_deltas.size(); ++i) {
                ZSTD_decompressDCtx(dctx, decomp.data(), PAGE_SIZE,
                                    frames[f].page_deltas[i].data(),
                                    frames[f].page_deltas[i].size());
                int p = frames[f].page_indices[i];
                uint8_t* page = restore_buf.data() + p * PAGE_SIZE;
                for (size_t j = 0; j < PAGE_SIZE; ++j) page[j] ^= decomp[j];
            }
        }
        benchmark::DoNotOptimize(restore_buf.data());
    }

    ZSTD_freeCCtx(cctx);
    ZSTD_freeDCtx(dctx);

    size_t total = keyframe_storage + delta_storage;
    state.counters["total_KB"] = total / 1024.0;
    state.counters["keyframes_KB"] = keyframe_storage / 1024.0;
    state.counters["deltas_KB"] = delta_storage / 1024.0;
    state.counters["num_keyframes"] = num_frames / iframe_interval;
}

BENCHMARK(BM_V2_IframeTradeoff)->Arg(5)->Arg(10)->Arg(25)->Arg(50)->Unit(benchmark::kMicrosecond)->MinTime(1.0);
