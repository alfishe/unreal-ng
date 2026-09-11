/**
 * real_ttd_v1_v2_bench.cpp — V1 vs V2 on REAL TTD files
 */
#include <benchmark/benchmark.h>
#include <zstd.h>
#include <fstream>
#include <vector>
#include <random>
#include <cstring>
#include <filesystem>

constexpr size_t PAGE_SIZE = 4096;
constexpr int I_FRAME_INTERVAL = 50;
constexpr int BATCH_SIZE = 4;

struct TTDData {
    std::vector<std::vector<uint8_t>> frames;
    size_t frame_size;
    size_t num_pages;
    std::string name;
};

// Load exported .frames file (from export_ttd_frames.py)
static TTDData LoadFramesFile(const std::string& path) {
    TTDData data;
    data.name = std::filesystem::path(path).stem().string();

    std::ifstream f(path, std::ios::binary);
    if (!f) return data;

    uint32_t num_frames, num_pages;
    f.read(reinterpret_cast<char*>(&num_frames), 4);
    f.read(reinterpret_cast<char*>(&num_pages), 4);

    data.num_pages = num_pages;
    data.frame_size = num_pages * PAGE_SIZE;
    data.frames.reserve(num_frames);

    for (uint32_t i = 0; i < num_frames; ++i) {
        std::vector<uint8_t> frame(data.frame_size);
        f.read(reinterpret_cast<char*>(frame.data()), data.frame_size);
        data.frames.push_back(std::move(frame));
    }

    return data;
}

struct V1Storage {
    std::vector<std::vector<uint8_t>> compressed;
    size_t total_bytes = 0;
};

struct V2Storage {
    std::vector<std::vector<uint8_t>> keyframes;
    struct FrameDeltas {
        std::vector<int> page_indices;
        std::vector<std::pair<int, std::vector<uint8_t>>> batches;
    };
    std::vector<FrameDeltas> deltas;
    size_t keyframe_bytes = 0;
    size_t delta_bytes = 0;
};

static V1Storage EncodeV1(const TTDData& data) {
    V1Storage v1;
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    std::vector<uint8_t> buf(ZSTD_compressBound(data.frame_size));

    for (const auto& frame : data.frames) {
        size_t csize = ZSTD_compressCCtx(cctx, buf.data(), buf.size(),
                                          frame.data(), frame.size(), 1);
        v1.compressed.emplace_back(buf.begin(), buf.begin() + csize);
        v1.total_bytes += csize;
    }

    ZSTD_freeCCtx(cctx);
    return v1;
}

static V2Storage EncodeV2(const TTDData& data) {
    V2Storage v2;
    v2.deltas.resize(data.frames.size());

    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    std::vector<uint8_t> kf_buf(ZSTD_compressBound(data.frame_size));
    std::vector<uint8_t> delta_buf(ZSTD_compressBound(PAGE_SIZE * BATCH_SIZE));
    std::vector<uint8_t> prev_frame(data.frame_size, 0);

    for (size_t f = 0; f < data.frames.size(); ++f) {
        const auto& frame = data.frames[f];

        if (f % I_FRAME_INTERVAL == 0) {
            size_t csize = ZSTD_compressCCtx(cctx, kf_buf.data(), kf_buf.size(),
                                              frame.data(), frame.size(), 1);
            v2.keyframes.emplace_back(kf_buf.begin(), kf_buf.begin() + csize);
            v2.keyframe_bytes += csize;
        }

        std::vector<int> changed_pages;
        std::vector<std::vector<uint8_t>> changed_deltas;

        for (size_t p = 0; p < data.num_pages; ++p) {
            const uint8_t* curr = frame.data() + p * PAGE_SIZE;
            const uint8_t* prev = prev_frame.data() + p * PAGE_SIZE;

            if (memcmp(curr, prev, PAGE_SIZE) != 0) {
                changed_pages.push_back(p);
                std::vector<uint8_t> delta(PAGE_SIZE);
                for (size_t i = 0; i < PAGE_SIZE; ++i) {
                    delta[i] = curr[i] ^ prev[i];
                }
                changed_deltas.push_back(std::move(delta));
            }
        }

        v2.deltas[f].page_indices = changed_pages;

        for (size_t i = 0; i < changed_deltas.size(); i += BATCH_SIZE) {
            size_t batch_end = std::min(i + BATCH_SIZE, changed_deltas.size());
            int pages_in_batch = batch_end - i;

            std::vector<uint8_t> batch_data;
            batch_data.reserve(pages_in_batch * PAGE_SIZE);
            for (size_t j = i; j < batch_end; ++j) {
                batch_data.insert(batch_data.end(), changed_deltas[j].begin(), changed_deltas[j].end());
            }

            size_t csize = ZSTD_compressCCtx(cctx, delta_buf.data(), delta_buf.size(),
                                              batch_data.data(), batch_data.size(), 1);
            v2.deltas[f].batches.emplace_back(pages_in_batch,
                std::vector<uint8_t>(delta_buf.begin(), delta_buf.begin() + csize));
            v2.delta_bytes += csize;
        }

        prev_frame = frame;
    }

    ZSTD_freeCCtx(cctx);
    return v2;
}

// Global storage for all files
static std::vector<std::tuple<std::string, TTDData, V1Storage, V2Storage>> g_files;
static bool g_initialized = false;

static void Initialize() {
    if (g_initialized) return;
    g_initialized = true;

    std::vector<std::string> paths = {
        "demo_7threality.frames",
        "demo_across-the-edge-second.frames",
        "active_demo.frames",
        "idle_session.frames"
    };

    printf("\n=== REAL TTD FILES: V1 vs V2 (I@%d + batch=%d) ===\n\n", I_FRAME_INTERVAL, BATCH_SIZE);
    printf("%-40s %8s %12s %12s %8s\n", "File", "Frames", "V1", "V2", "Ratio");
    printf("%-40s %8s %12s %12s %8s\n", "----", "------", "--", "--", "-----");

    size_t total_v1 = 0, total_v2 = 0;

    for (const auto& filename : paths) {
        auto data = LoadFramesFile(filename);
        if (data.frames.empty()) continue;

        auto v1 = EncodeV1(data);
        auto v2 = EncodeV2(data);

        size_t v2_total = v2.keyframe_bytes + v2.delta_bytes;
        total_v1 += v1.total_bytes;
        total_v2 += v2_total;

        printf("%-40s %8zu %10.1fKB %10.1fKB %7.1fx\n",
               data.name.c_str(), data.frames.size(),
               v1.total_bytes/1024.0, v2_total/1024.0,
               (double)v1.total_bytes/v2_total);

        g_files.emplace_back(data.name, std::move(data), std::move(v1), std::move(v2));
    }

    printf("%-40s %8s %10.1fKB %10.1fKB %7.1fx\n", "TOTAL", "",
           total_v1/1024.0, total_v2/1024.0, (double)total_v1/total_v2);
    printf("\n");
}

static void BM_V1_Restore_RealTTD(benchmark::State& state) {
    Initialize();
    if (g_files.empty()) {
        state.SkipWithError("No TTD files");
        return;
    }

    // Use first file
    const auto& [name, data, v1, v2] = g_files[0];

    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    std::vector<uint8_t> restore_buf(data.frame_size);

    std::mt19937 rng(42);
    std::uniform_int_distribution<> dist(0, v1.compressed.size() - 1);

    for (auto _ : state) {
        int target = dist(rng);
        ZSTD_decompressDCtx(dctx, restore_buf.data(), restore_buf.size(),
                            v1.compressed[target].data(), v1.compressed[target].size());
        benchmark::DoNotOptimize(restore_buf.data());
    }

    ZSTD_freeDCtx(dctx);
    state.counters["file"] = benchmark::Counter(0);
    state.counters["v1_KB"] = v1.total_bytes / 1024.0;
}

static void BM_V2_Restore_RealTTD(benchmark::State& state) {
    Initialize();
    if (g_files.empty()) {
        state.SkipWithError("No TTD files");
        return;
    }

    const auto& [name, data, v1, v2] = g_files[0];

    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    std::vector<uint8_t> restore_buf(data.frame_size);
    std::vector<uint8_t> decomp_buf(PAGE_SIZE * BATCH_SIZE);

    std::mt19937 rng(42);
    std::uniform_int_distribution<> dist(0, data.frames.size() - 1);

    for (auto _ : state) {
        int target = dist(rng);
        int kf_idx = target / I_FRAME_INTERVAL;
        int kf_frame = kf_idx * I_FRAME_INTERVAL;

        ZSTD_decompressDCtx(dctx, restore_buf.data(), restore_buf.size(),
                            v2.keyframes[kf_idx].data(), v2.keyframes[kf_idx].size());

        for (int f = kf_frame + 1; f <= target; ++f) {
            const auto& fd = v2.deltas[f];
            int page_offset = 0;

            for (const auto& [pages_in_batch, compressed] : fd.batches) {
                ZSTD_decompressDCtx(dctx, decomp_buf.data(), pages_in_batch * PAGE_SIZE,
                                    compressed.data(), compressed.size());

                for (int i = 0; i < pages_in_batch; ++i) {
                    int p = fd.page_indices[page_offset + i];
                    const uint8_t* delta = decomp_buf.data() + i * PAGE_SIZE;
                    uint8_t* page = restore_buf.data() + p * PAGE_SIZE;
                    for (size_t j = 0; j < PAGE_SIZE; ++j) {
                        page[j] ^= delta[j];
                    }
                }
                page_offset += pages_in_batch;
            }
        }

        benchmark::DoNotOptimize(restore_buf.data());
    }

    ZSTD_freeDCtx(dctx);
    state.counters["v2_KB"] = (v2.keyframe_bytes + v2.delta_bytes) / 1024.0;
}

BENCHMARK(BM_V1_Restore_RealTTD)->Unit(benchmark::kMicrosecond)->MinTime(2.0);
BENCHMARK(BM_V2_Restore_RealTTD)->Unit(benchmark::kMicrosecond)->MinTime(2.0);
