// FrameScaler at 4K: the conversions the recording (NVENC's NV12 buffer, the videowall grab) runs per frame.
// Picture: a ZX-like CRT frame (stripes, a mask of a pixel) so the data is neither constant nor random noise.
// Run: core-benchmarks --benchmark_filter='FrameScaler'

#include <benchmark/benchmark.h>

#include <cstdint>
#include <vector>

#include "common/framescaler.h"

namespace
{
std::vector<uint8_t> MakePicture(uint32_t w, uint32_t h)
{
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
        {
            uint8_t* p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
            const uint32_t cell = (x / 8 + y / 8) & 7;
            const uint8_t level = (y % 3 == 2) ? 0x50 : 0xD7;  // a scanline every third row
            p[0] = (cell & 2) && x % 3 == 0 ? level : 0x10;  // the mask: R, G, B columns
            p[1] = (cell & 4) && x % 3 == 1 ? level : 0x10;
            p[2] = (cell & 1) && x % 3 == 2 ? level : 0x10;
            p[3] = 0xFF;
        }
    return rgba;
}

/// The whole-frame conversion, 4K in, 4K NV12 out (the videowall's 4K grab)
void BM_FrameScaler_PackedToNv12_4K(benchmark::State& state)
{
    const uint32_t w = 3840, h = 2160;
    const std::vector<uint8_t> src = MakePicture(w, h);
    std::vector<uint8_t> yPlane(static_cast<size_t>(w) * h), uvPlane(static_cast<size_t>(w) * h / 2);
    for (auto _ : state)
    {
        FrameScaler::PackedToNv12(src.data(), static_cast<size_t>(w) * 4, w, h, yPlane.data(), w, uvPlane.data(), w);
        benchmark::DoNotOptimize(yPlane.data());
        benchmark::DoNotOptimize(uvPlane.data());
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * w * h * 4);
}
BENCHMARK(BM_FrameScaler_PackedToNv12_4K)->Unit(benchmark::kMillisecond);

/// Scale + convert into NV12 at 4K: args = source width, height (1920x1080: 2x; 960x540: 4x; 352x288: fit 7.5x)
void BM_FrameScaler_ScaleIntoNv12_4K(benchmark::State& state)
{
    const uint32_t sw = static_cast<uint32_t>(state.range(0)), sh = static_cast<uint32_t>(state.range(1));
    const uint32_t w = 3840, h = 2160;
    const std::vector<uint8_t> src = MakePicture(sw, sh);
    std::vector<uint8_t> yPlane(static_cast<size_t>(w) * h), uvPlane(static_cast<size_t>(w) * h / 2);
    for (auto _ : state)
    {
        FrameScaler::ScaleIntoNv12(src.data(), sw, sh, yPlane.data(), w, uvPlane.data(), w, w, h);
        benchmark::DoNotOptimize(yPlane.data());
        benchmark::DoNotOptimize(uvPlane.data());
    }
}
BENCHMARK(BM_FrameScaler_ScaleIntoNv12_4K)
    ->Args({3840, 2160})
    ->Args({1920, 1080})
    ->Args({960, 540})
    ->Args({352, 288})
    ->Unit(benchmark::kMillisecond);

/// Scale into RGBA at 4K (the encoders that take RGBA / BGRA): args = source width, height, swap R and B
void BM_FrameScaler_ScaleInto_4K(benchmark::State& state)
{
    const uint32_t sw = static_cast<uint32_t>(state.range(0)), sh = static_cast<uint32_t>(state.range(1));
    const bool swap = state.range(2) != 0;
    const uint32_t w = 3840, h = 2160;
    const std::vector<uint8_t> src = MakePicture(sw, sh);
    std::vector<uint8_t> dst(static_cast<size_t>(w) * h * 4);
    for (auto _ : state)
    {
        FrameScaler::ScaleInto(src.data(), sw, sh, dst.data(), static_cast<size_t>(w) * 4, w, h, swap);
        benchmark::DoNotOptimize(dst.data());
    }
}
BENCHMARK(BM_FrameScaler_ScaleInto_4K)
    ->Args({1920, 1080, 0})
    ->Args({1920, 1080, 1})
    ->Args({960, 540, 0})
    ->Args({352, 288, 0})
    ->Unit(benchmark::kMillisecond);
}  // namespace
