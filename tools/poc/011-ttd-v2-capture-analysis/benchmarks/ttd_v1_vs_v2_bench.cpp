/**
 * ttd_v1_vs_v2_bench.cpp — End-to-end TTD V1 vs V2 comparison.
 *
 * Benchmarks:
 *   1. Capture cycle: I-frame (1/sec) + 49 P-frames with page back-references
 *   2. Seek/restore: random timeline jumping
 *
 * V1: Full frame snapshots, linear scan for seeks
 * V2: Page-granular XOR deltas, back-references for unchanged pages, O(1) index
 *
 * Uses real TTD file patterns for realistic workloads.
 */
#include <benchmark/benchmark.h>
#include <zstd.h>
#include <vector>
#include <array>
#include <random>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <chrono>

// TTD constants
constexpr size_t PAGE_SIZE = 4096;
constexpr size_t PAGES_PER_FRAME = 32;  // 128KB RAM model (typical 128K)
constexpr size_t FRAME_SIZE = PAGE_SIZE * PAGES_PER_FRAME;
constexpr int FRAMES_PER_SECOND = 50;
constexpr int I_FRAME_INTERVAL = FRAMES_PER_SECOND;  // 1 I-frame per second
constexpr int BATCH_SIZE = 4;  // Page batching for V2

// Realistic page change patterns from real TTD analysis
// Only ~1-2 pages change per frame out of 32 (VRAM + stack)
// Many pages are static (ROM shadow, unused RAM) - key V2 advantage
constexpr int STATIC_PAGES = 20;  // ~60% of RAM is static (ROM, unused)
constexpr int ACTIVE_PAGES = 12;  // ~40% can change (VRAM, stack, vars)
constexpr double ACTIVE_PAGE_CHANGE_PROB = 0.15;  // ~15% of active pages change per frame
constexpr double BYTES_CHANGED_PER_PAGE = 0.015;  // 1.5% of bytes in changed pages

//------------------------------------------------------------------------------
// V1 Codec: Full frame snapshots
//------------------------------------------------------------------------------
struct V1Codec {
    ZSTD_CCtx* cctx;
    ZSTD_DCtx* dctx;
    std::vector<uint8_t> compress_buf;
    std::vector<uint8_t> decompress_buf;

    V1Codec() {
        cctx = ZSTD_createCCtx();
        dctx = ZSTD_createDCtx();
        compress_buf.resize(ZSTD_compressBound(FRAME_SIZE));
        decompress_buf.resize(FRAME_SIZE);
    }

    ~V1Codec() {
        ZSTD_freeCCtx(cctx);
        ZSTD_freeDCtx(dctx);
    }

    // V1: Compress FULL frame snapshot (no XOR - that's the point of V1)
    size_t compress(const uint8_t* frame, std::vector<uint8_t>& out) {
        size_t csize = ZSTD_compressCCtx(cctx, compress_buf.data(), compress_buf.size(),
                                          frame, FRAME_SIZE, 1);
        out.assign(compress_buf.begin(), compress_buf.begin() + csize);
        return csize;
    }

    // V1: Decompress full frame directly (instant seek - no delta chain)
    void decompress(const std::vector<uint8_t>& compressed, uint8_t* out) {
        ZSTD_decompressDCtx(dctx, out, FRAME_SIZE, compressed.data(), compressed.size());
    }
};

//------------------------------------------------------------------------------
// V2 Codec: Page-granular XOR deltas with back-references
//------------------------------------------------------------------------------
struct V2PageSlot {
    enum Encoding : uint8_t { RAW, XOR_PREV, BACKREF };
    Encoding encoding;
    uint32_t prev_slot;      // For XOR_PREV: slot of previous version
    uint32_t backref_frame;  // For BACKREF: frame containing identical page
    std::vector<uint8_t> data;  // Compressed delta or raw page
};

struct V2Frame {
    uint64_t frame_id;
    bool is_iframe;
    std::array<uint32_t, PAGES_PER_FRAME> page_slots;  // Index into global slot table
};

struct V2Codec {
    ZSTD_CCtx* cctx;
    ZSTD_DCtx* dctx;
    std::vector<V2PageSlot> slots;
    std::vector<V2Frame> frames;
    std::vector<uint8_t> compress_buf;
    std::vector<uint8_t> decompress_buf;
    std::vector<uint8_t> xor_buf;

    // Page hash table for back-reference detection
    std::unordered_map<size_t, std::pair<uint32_t, uint32_t>> page_hash_to_slot;  // hash -> (frame_id, slot_id)

    V2Codec() {
        cctx = ZSTD_createCCtx();
        dctx = ZSTD_createDCtx();
        compress_buf.resize(ZSTD_compressBound(PAGE_SIZE * BATCH_SIZE));
        decompress_buf.resize(PAGE_SIZE * BATCH_SIZE);
        xor_buf.resize(PAGE_SIZE);
    }

    ~V2Codec() {
        ZSTD_freeCCtx(cctx);
        ZSTD_freeDCtx(dctx);
    }

    // Simple hash for page content
    size_t hash_page(const uint8_t* page) {
        size_t h = 0;
        for (size_t i = 0; i < PAGE_SIZE; i += 8) {
            h ^= *reinterpret_cast<const uint64_t*>(page + i) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }

    // Encode single page with XOR delta
    uint32_t encode_page_xor(const uint8_t* curr, const uint8_t* prev, uint32_t prev_slot) {
        // XOR delta
        for (size_t i = 0; i < PAGE_SIZE; ++i) {
            xor_buf[i] = curr[i] ^ prev[i];
        }

        V2PageSlot slot;
        slot.encoding = V2PageSlot::XOR_PREV;
        slot.prev_slot = prev_slot;

        size_t csize = ZSTD_compressCCtx(cctx, compress_buf.data(), compress_buf.size(),
                                          xor_buf.data(), PAGE_SIZE, 1);
        slot.data.assign(compress_buf.begin(), compress_buf.begin() + csize);

        uint32_t slot_id = static_cast<uint32_t>(slots.size());
        slots.push_back(std::move(slot));
        return slot_id;
    }

    // Encode page as back-reference (for I-frames when page unchanged)
    uint32_t encode_page_backref(uint32_t backref_frame, uint32_t backref_slot) {
        V2PageSlot slot;
        slot.encoding = V2PageSlot::BACKREF;
        slot.backref_frame = backref_frame;
        slot.prev_slot = backref_slot;
        // No data needed - just reference

        uint32_t slot_id = static_cast<uint32_t>(slots.size());
        slots.push_back(std::move(slot));
        return slot_id;
    }

    // Encode raw page (for truly new content)
    uint32_t encode_page_raw(const uint8_t* page) {
        V2PageSlot slot;
        slot.encoding = V2PageSlot::RAW;

        size_t csize = ZSTD_compressCCtx(cctx, compress_buf.data(), compress_buf.size(),
                                          page, PAGE_SIZE, 1);
        slot.data.assign(compress_buf.begin(), compress_buf.begin() + csize);

        uint32_t slot_id = static_cast<uint32_t>(slots.size());
        slots.push_back(std::move(slot));
        return slot_id;
    }

    // Restore single page
    void restore_page(uint32_t slot_id, uint8_t* out, const std::vector<std::vector<uint8_t>>& frame_cache) {
        const auto& slot = slots[slot_id];

        switch (slot.encoding) {
            case V2PageSlot::RAW:
                ZSTD_decompressDCtx(dctx, out, PAGE_SIZE, slot.data.data(), slot.data.size());
                break;

            case V2PageSlot::XOR_PREV: {
                ZSTD_decompressDCtx(dctx, xor_buf.data(), PAGE_SIZE, slot.data.data(), slot.data.size());
                // Need to recursively restore prev and XOR
                std::vector<uint8_t> prev_page(PAGE_SIZE);
                restore_page(slot.prev_slot, prev_page.data(), frame_cache);
                for (size_t i = 0; i < PAGE_SIZE; ++i) {
                    out[i] = prev_page[i] ^ xor_buf[i];
                }
                break;
            }

            case V2PageSlot::BACKREF:
                // Copy from cached frame
                if (slot.backref_frame < frame_cache.size()) {
                    const auto& cached = frame_cache[slot.backref_frame];
                    // Find page offset from slot
                    memcpy(out, cached.data() + (slot.prev_slot % PAGES_PER_FRAME) * PAGE_SIZE, PAGE_SIZE);
                }
                break;
        }
    }
};

//------------------------------------------------------------------------------
// Frame Generator: Creates realistic frame sequences from TTD patterns
//------------------------------------------------------------------------------
class FrameGenerator {
public:
    std::mt19937 rng;
    std::vector<uint8_t> current_frame;
    std::vector<uint8_t> previous_frame;

    FrameGenerator(uint32_t seed = 42) : rng(seed), current_frame(FRAME_SIZE), previous_frame(FRAME_SIZE) {
        // Initialize with random base content
        std::uniform_int_distribution<> byte_dist(0, 255);
        for (auto& b : current_frame) b = static_cast<uint8_t>(byte_dist(rng));
        previous_frame = current_frame;
    }

    // Generate next frame with realistic changes
    // Static pages (0..STATIC_PAGES-1) never change - ROM shadow, unused RAM
    // Active pages (STATIC_PAGES..end) change probabilistically - VRAM, stack
    const std::vector<uint8_t>& next_frame() {
        previous_frame = current_frame;

        std::uniform_real_distribution<> prob(0.0, 1.0);
        std::uniform_int_distribution<> byte_dist(0, 255);

        // Only active pages can change
        for (int page_idx = STATIC_PAGES; page_idx < static_cast<int>(PAGES_PER_FRAME); ++page_idx) {
            if (prob(rng) < ACTIVE_PAGE_CHANGE_PROB) {
                uint8_t* page = current_frame.data() + page_idx * PAGE_SIZE;

                // Modify ~1.5% of bytes in this page
                for (size_t j = 0; j < PAGE_SIZE; ++j) {
                    if (prob(rng) < BYTES_CHANGED_PER_PAGE) {
                        page[j] = static_cast<uint8_t>(byte_dist(rng));
                    }
                }
            }
        }

        return current_frame;
    }

    const std::vector<uint8_t>& get_current() const { return current_frame; }
    const std::vector<uint8_t>& get_previous() const { return previous_frame; }
};

//------------------------------------------------------------------------------
// Benchmark: Capture Cycle (I-frame + 49 P-frames)
//------------------------------------------------------------------------------
static void BM_V1_CaptureCycle(benchmark::State& state) {
    FrameGenerator gen;
    V1Codec codec;

    std::vector<std::vector<uint8_t>> compressed_frames;
    compressed_frames.reserve(FRAMES_PER_SECOND);

    size_t total_compressed = 0;

    for (auto _ : state) {
        compressed_frames.clear();
        total_compressed = 0;

        // V1: Store FULL frame snapshot every frame (fast seek, large storage)
        for (int f = 0; f < FRAMES_PER_SECOND; ++f) {
            const auto& frame = gen.next_frame();
            std::vector<uint8_t> compressed;
            total_compressed += codec.compress(frame.data(), compressed);
            compressed_frames.push_back(std::move(compressed));
        }

        benchmark::DoNotOptimize(total_compressed);
    }

    state.counters["bytes/sec"] = total_compressed;
    state.counters["bytes/frame"] = total_compressed / FRAMES_PER_SECOND;
}

static void BM_V2_CaptureCycle(benchmark::State& state) {
    FrameGenerator gen;
    V2Codec codec;

    size_t total_compressed = 0;

    for (auto _ : state) {
        codec.slots.clear();
        codec.frames.clear();
        codec.page_hash_to_slot.clear();
        total_compressed = 0;

        std::vector<uint32_t> prev_page_slots(PAGES_PER_FRAME, UINT32_MAX);
        std::vector<uint8_t> prev_frame(FRAME_SIZE);

        // Capture 1 second of frames
        for (int f = 0; f < FRAMES_PER_SECOND; ++f) {
            const auto& frame = gen.next_frame();
            bool is_iframe = (f % I_FRAME_INTERVAL == 0);

            V2Frame v2frame;
            v2frame.frame_id = f;
            v2frame.is_iframe = is_iframe;

            for (size_t p = 0; p < PAGES_PER_FRAME; ++p) {
                const uint8_t* curr_page = frame.data() + p * PAGE_SIZE;
                const uint8_t* prev_page = prev_frame.data() + p * PAGE_SIZE;

                if (is_iframe) {
                    // I-frame: check for back-reference to identical page
                    size_t hash = codec.hash_page(curr_page);
                    auto it = codec.page_hash_to_slot.find(hash);

                    if (it != codec.page_hash_to_slot.end() && prev_page_slots[p] != UINT32_MAX) {
                        // Back-reference to existing identical page
                        v2frame.page_slots[p] = codec.encode_page_backref(it->second.first, it->second.second);
                        // BACKREF has no data, just index entry
                    } else {
                        // Encode as XOR from previous version or RAW
                        if (prev_page_slots[p] != UINT32_MAX) {
                            v2frame.page_slots[p] = codec.encode_page_xor(curr_page, prev_page, prev_page_slots[p]);
                        } else {
                            v2frame.page_slots[p] = codec.encode_page_raw(curr_page);
                        }
                        codec.page_hash_to_slot[hash] = {static_cast<uint32_t>(f), v2frame.page_slots[p]};
                    }
                } else {
                    // P-frame: XOR delta from previous
                    if (prev_page_slots[p] != UINT32_MAX) {
                        // Check if page actually changed
                        bool changed = false;
                        for (size_t i = 0; i < PAGE_SIZE && !changed; ++i) {
                            if (curr_page[i] != prev_page[i]) changed = true;
                        }

                        if (changed) {
                            v2frame.page_slots[p] = codec.encode_page_xor(curr_page, prev_page, prev_page_slots[p]);
                        } else {
                            // Unchanged: just reference previous slot
                            v2frame.page_slots[p] = prev_page_slots[p];
                        }
                    } else {
                        v2frame.page_slots[p] = codec.encode_page_raw(curr_page);
                    }
                }

                prev_page_slots[p] = v2frame.page_slots[p];
            }

            prev_frame = frame;
            codec.frames.push_back(std::move(v2frame));
        }

        // Calculate total compressed size
        for (const auto& slot : codec.slots) {
            total_compressed += slot.data.size();
            total_compressed += 8;  // Index overhead per slot
        }

        benchmark::DoNotOptimize(total_compressed);
    }

    state.counters["bytes/sec"] = total_compressed;
    state.counters["bytes/frame"] = total_compressed / FRAMES_PER_SECOND;
}

BENCHMARK(BM_V1_CaptureCycle)->Unit(benchmark::kMillisecond)->MinTime(2.0);
BENCHMARK(BM_V2_CaptureCycle)->Unit(benchmark::kMillisecond)->MinTime(2.0);

//------------------------------------------------------------------------------
// Benchmark: Restore 1 frame to cache (random timeline position)
// Restore = unpack + unroll frame to RAM cache
//------------------------------------------------------------------------------
static void BM_V1_RestoreFrame(benchmark::State& state) {
    const int num_frames = state.range(0);

    FrameGenerator gen;
    V1Codec codec;

    // V1: Store full frame snapshot for every frame
    std::vector<std::vector<uint8_t>> compressed_frames;
    compressed_frames.reserve(num_frames);

    for (int f = 0; f < num_frames; ++f) {
        const auto& frame = gen.next_frame();
        std::vector<uint8_t> compressed;
        codec.compress(frame.data(), compressed);
        compressed_frames.push_back(std::move(compressed));
    }

    std::vector<uint8_t> restore_buf(FRAME_SIZE);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<> frame_dist(0, num_frames - 1);

    for (auto _ : state) {
        int target_frame = frame_dist(rng);

        // V1 seek: instant - just decompress target frame directly
        codec.decompress(compressed_frames[target_frame], restore_buf.data());

        benchmark::DoNotOptimize(restore_buf.data());
    }
}

static void BM_V2_RestoreFrame(benchmark::State& state) {
    const int num_frames = state.range(0);

    FrameGenerator gen;

    // V2 stores: keyframes (uncompressed) + per-page XOR deltas
    struct PageDelta {
        std::vector<uint8_t> compressed;
        bool changed;
    };
    std::vector<std::array<PageDelta, PAGES_PER_FRAME>> frame_deltas(num_frames);
    std::vector<std::vector<uint8_t>> keyframes;  // I-frames for fast seek base

    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    std::vector<uint8_t> compress_buf(ZSTD_compressBound(PAGE_SIZE));
    std::vector<uint8_t> xor_buf(PAGE_SIZE);
    std::vector<uint8_t> prev_frame(FRAME_SIZE);

    // Pre-generate and encode frames
    for (int f = 0; f < num_frames; ++f) {
        const auto& frame = gen.next_frame();
        bool is_iframe = (f % I_FRAME_INTERVAL == 0);

        if (is_iframe) {
            keyframes.push_back(frame);
        }

        for (size_t p = 0; p < PAGES_PER_FRAME; ++p) {
            const uint8_t* curr_page = frame.data() + p * PAGE_SIZE;
            const uint8_t* prev_page = prev_frame.data() + p * PAGE_SIZE;

            bool changed = (f == 0) || memcmp(curr_page, prev_page, PAGE_SIZE) != 0;
            frame_deltas[f][p].changed = changed;

            if (changed) {
                for (size_t i = 0; i < PAGE_SIZE; ++i) {
                    xor_buf[i] = curr_page[i] ^ prev_page[i];
                }
                size_t csize = ZSTD_compressCCtx(cctx, compress_buf.data(), compress_buf.size(),
                                                  xor_buf.data(), PAGE_SIZE, 1);
                frame_deltas[f][p].compressed.assign(compress_buf.begin(), compress_buf.begin() + csize);
            }
        }
        prev_frame = frame;
    }

    std::vector<uint8_t> restore_buf(FRAME_SIZE);
    std::vector<uint8_t> decomp_buf(PAGE_SIZE);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<> frame_dist(0, num_frames - 1);

    for (auto _ : state) {
        int target_frame = frame_dist(rng);

        // V2 seek: keyframe + apply deltas forward
        int nearest_iframe = (target_frame / I_FRAME_INTERVAL) * I_FRAME_INTERVAL;
        int keyframe_idx = target_frame / I_FRAME_INTERVAL;

        // Copy keyframe as base
        memcpy(restore_buf.data(), keyframes[keyframe_idx].data(), FRAME_SIZE);

        // Apply deltas from keyframe to target (worst case: 49 frames)
        for (int f = nearest_iframe + 1; f <= target_frame; ++f) {
            for (size_t p = 0; p < PAGES_PER_FRAME; ++p) {
                if (frame_deltas[f][p].changed) {
                    ZSTD_decompressDCtx(dctx, decomp_buf.data(), PAGE_SIZE,
                                        frame_deltas[f][p].compressed.data(),
                                        frame_deltas[f][p].compressed.size());
                    uint8_t* page = restore_buf.data() + p * PAGE_SIZE;
                    for (size_t i = 0; i < PAGE_SIZE; ++i) {
                        page[i] ^= decomp_buf[i];
                    }
                }
            }
        }

        benchmark::DoNotOptimize(restore_buf.data());
    }

    ZSTD_freeCCtx(cctx);
    ZSTD_freeDCtx(dctx);
}

BENCHMARK(BM_V1_RestoreFrame)->Arg(100)->Arg(1000)->Arg(5000)->Unit(benchmark::kMicrosecond)->MinTime(1.0);
BENCHMARK(BM_V2_RestoreFrame)->Arg(100)->Arg(1000)->Arg(5000)->Unit(benchmark::kMicrosecond)->MinTime(1.0);

//------------------------------------------------------------------------------
// Benchmark: V2 Batch=1 vs Batch=4 (storage vs restore trade-off)
//------------------------------------------------------------------------------
static void BM_V2_BatchComparison(benchmark::State& state) {
    const int batch_size = state.range(0);
    const int num_frames = 500;  // 10 seconds

    FrameGenerator gen(42);

    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    std::vector<uint8_t> compress_buf(ZSTD_compressBound(PAGE_SIZE * batch_size));
    std::vector<uint8_t> xor_buf(PAGE_SIZE);
    std::vector<uint8_t> prev_frame(FRAME_SIZE);

    // Storage: page deltas per frame (only changed pages)
    struct FrameData {
        std::vector<std::vector<uint8_t>> page_deltas;  // Compressed deltas for changed pages
        std::vector<int> changed_pages;  // Which pages changed
    };
    std::vector<FrameData> frames(num_frames);
    std::vector<std::vector<uint8_t>> keyframes;

    size_t total_storage = 0;

    // Encode all frames
    for (int f = 0; f < num_frames; ++f) {
        const auto& frame = gen.next_frame();

        if (f % I_FRAME_INTERVAL == 0) {
            keyframes.push_back(frame);
        }

        // Find changed pages and compress deltas
        for (size_t p = 0; p < PAGES_PER_FRAME; ++p) {
            const uint8_t* curr = frame.data() + p * PAGE_SIZE;
            const uint8_t* prev = prev_frame.data() + p * PAGE_SIZE;

            if (f == 0 || memcmp(curr, prev, PAGE_SIZE) != 0) {
                frames[f].changed_pages.push_back(p);

                // XOR delta
                for (size_t i = 0; i < PAGE_SIZE; ++i) {
                    xor_buf[i] = curr[i] ^ prev[i];
                }

                size_t csize = ZSTD_compressCCtx(cctx, compress_buf.data(), compress_buf.size(),
                                                  xor_buf.data(), PAGE_SIZE, 1);
                frames[f].page_deltas.emplace_back(compress_buf.begin(), compress_buf.begin() + csize);
                total_storage += csize;
            }
        }
        prev_frame = frame;
    }

    // If batching, recompress in batches (simulate batched storage)
    if (batch_size > 1) {
        total_storage = 0;
        for (int f = 0; f < num_frames; ++f) {
            // Batch pages together
            for (size_t i = 0; i < frames[f].page_deltas.size(); i += batch_size) {
                size_t batch_end = std::min(i + batch_size, frames[f].page_deltas.size());
                std::vector<uint8_t> batch_data;
                for (size_t j = i; j < batch_end; ++j) {
                    // Need original delta, decompress first
                    std::vector<uint8_t> delta(PAGE_SIZE);
                    ZSTD_decompressDCtx(dctx, delta.data(), PAGE_SIZE,
                                        frames[f].page_deltas[j].data(),
                                        frames[f].page_deltas[j].size());
                    batch_data.insert(batch_data.end(), delta.begin(), delta.end());
                }
                size_t csize = ZSTD_compressCCtx(cctx, compress_buf.data(), compress_buf.size(),
                                                  batch_data.data(), batch_data.size(), 1);
                total_storage += csize;
            }
        }
    }

    // Measure restore time
    std::vector<uint8_t> restore_buf(FRAME_SIZE);
    std::vector<uint8_t> decomp_buf(PAGE_SIZE);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<> frame_dist(0, num_frames - 1);

    for (auto _ : state) {
        int target = frame_dist(rng);
        int kf_idx = target / I_FRAME_INTERVAL;
        int kf_frame = kf_idx * I_FRAME_INTERVAL;

        memcpy(restore_buf.data(), keyframes[kf_idx].data(), FRAME_SIZE);

        for (int f = kf_frame + 1; f <= target; ++f) {
            for (size_t i = 0; i < frames[f].changed_pages.size(); ++i) {
                int p = frames[f].changed_pages[i];
                ZSTD_decompressDCtx(dctx, decomp_buf.data(), PAGE_SIZE,
                                    frames[f].page_deltas[i].data(),
                                    frames[f].page_deltas[i].size());
                uint8_t* page = restore_buf.data() + p * PAGE_SIZE;
                for (size_t j = 0; j < PAGE_SIZE; ++j) {
                    page[j] ^= decomp_buf[j];
                }
            }
        }
        benchmark::DoNotOptimize(restore_buf.data());
    }

    ZSTD_freeCCtx(cctx);
    ZSTD_freeDCtx(dctx);

    state.counters["storage_KB"] = total_storage / 1024.0;
}

BENCHMARK(BM_V2_BatchComparison)->Arg(1)->Arg(4)->Arg(8)->Unit(benchmark::kMicrosecond)->MinTime(1.0);

//------------------------------------------------------------------------------
// Benchmark: Storage efficiency comparison
//------------------------------------------------------------------------------
static void BM_StorageComparison(benchmark::State& state) {
    const int num_seconds = 10;  // 10 seconds of recording
    const int num_frames = num_seconds * FRAMES_PER_SECOND;

    FrameGenerator gen_v1(42);
    FrameGenerator gen_v2(42);  // Same seed for fair comparison

    V1Codec v1codec;
    V2Codec v2codec;

    size_t v1_total = 0, v2_total = 0;

    for (auto _ : state) {
        v1_total = 0;
        v2_total = 0;

        // Reset generators
        gen_v1 = FrameGenerator(42);
        gen_v2 = FrameGenerator(42);
        v2codec.slots.clear();
        v2codec.frames.clear();
        v2codec.page_hash_to_slot.clear();

        std::vector<uint32_t> prev_page_slots(PAGES_PER_FRAME, UINT32_MAX);
        std::vector<uint8_t> prev_frame(FRAME_SIZE);

        for (int f = 0; f < num_frames; ++f) {
            // V1: Full frame snapshot (no delta - instant seek but large)
            const auto& frame_v1 = gen_v1.next_frame();
            std::vector<uint8_t> compressed;
            v1_total += v1codec.compress(frame_v1.data(), compressed);

            // V2: page-granular encoding
            const auto& frame_v2 = gen_v2.next_frame();
            bool is_iframe = (f % I_FRAME_INTERVAL == 0);

            V2Frame v2frame;
            v2frame.frame_id = f;
            v2frame.is_iframe = is_iframe;

            for (size_t p = 0; p < PAGES_PER_FRAME; ++p) {
                const uint8_t* curr_page = frame_v2.data() + p * PAGE_SIZE;
                const uint8_t* prev_page = prev_frame.data() + p * PAGE_SIZE;

                if (prev_page_slots[p] != UINT32_MAX) {
                    bool changed = memcmp(curr_page, prev_page, PAGE_SIZE) != 0;
                    if (changed) {
                        v2frame.page_slots[p] = v2codec.encode_page_xor(curr_page, prev_page, prev_page_slots[p]);
                    } else {
                        v2frame.page_slots[p] = prev_page_slots[p];
                    }
                } else {
                    v2frame.page_slots[p] = v2codec.encode_page_raw(curr_page);
                }

                prev_page_slots[p] = v2frame.page_slots[p];
            }

            prev_frame = frame_v2;
            v2codec.frames.push_back(std::move(v2frame));
        }

        // Calculate V2 total
        for (const auto& slot : v2codec.slots) {
            v2_total += slot.data.size();
        }
        v2_total += v2codec.slots.size() * 8;  // Index overhead

        benchmark::DoNotOptimize(v1_total);
        benchmark::DoNotOptimize(v2_total);
    }

    double ratio = static_cast<double>(v1_total) / v2_total;
    state.counters["V1_bytes"] = v1_total;
    state.counters["V2_bytes"] = v2_total;
    state.counters["V2_savings"] = (1.0 - static_cast<double>(v2_total) / v1_total) * 100;
    state.counters["V1/V2_ratio"] = ratio;
}

BENCHMARK(BM_StorageComparison)->Unit(benchmark::kMillisecond)->MinTime(2.0);
