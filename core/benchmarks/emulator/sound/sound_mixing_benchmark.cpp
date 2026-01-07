#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "benchmark/benchmark.h"
#include "common/sound/audioutils.h"

// Audio constants
static constexpr size_t AUDIO_BUFFER_SAMPLES_PER_FRAME = 1764;  // 882 * 2

/// @brief Measures scalar mixing performance
static void BM_AudioMixing_Scalar(benchmark::State& state)
{
    std::vector<int16_t> src1(AUDIO_BUFFER_SAMPLES_PER_FRAME, 0x1000);
    std::vector<int16_t> src2(AUDIO_BUFFER_SAMPLES_PER_FRAME, 0x0800);
    std::vector<int16_t> dst(AUDIO_BUFFER_SAMPLES_PER_FRAME);

    for (auto _ : state)
    {
        for (size_t i = 0; i < AUDIO_BUFFER_SAMPLES_PER_FRAME; i++)
        {
            int32_t mixed = static_cast<int32_t>(src1[i]) + static_cast<int32_t>(src2[i]);
            if (mixed > 32767)
                mixed = 32767;
            else if (mixed < -32768)
                mixed = -32768;
            dst[i] = static_cast<int16_t>(mixed);
        }
        benchmark::DoNotOptimize(dst.data());
    }
}
BENCHMARK(BM_AudioMixing_Scalar);

/// @brief Measures SIMD mixing performance (SSE2/NEON/Fallback)
static void BM_AudioMixing_SIMD(benchmark::State& state)
{
    std::vector<int16_t> src1(AUDIO_BUFFER_SAMPLES_PER_FRAME, 0x1000);
    std::vector<int16_t> src2(AUDIO_BUFFER_SAMPLES_PER_FRAME, 0x0800);
    std::vector<int16_t> dst(AUDIO_BUFFER_SAMPLES_PER_FRAME);

    for (auto _ : state)
    {
        AudioUtils::MixAudio(src1.data(), src2.data(), dst.data(), AUDIO_BUFFER_SAMPLES_PER_FRAME);
        benchmark::DoNotOptimize(dst.data());
    }
}
BENCHMARK(BM_AudioMixing_SIMD);
