#include <cmath>
#include <cstdint>
#include <vector>

#include "benchmark/benchmark.h"
#include "common/sound/filters/audio_character_chain.h"

// AudioCharacterChain (punch / room) cost per buffer: one 50 Hz frame of
// stereo int16 at 44.1 kHz, the call SoundManager makes per chip buffer
// (AY 0 / 1, FM 0 / 1, beeper, card SSG rows). Arg = effects: 0 none (the
// FM and default beeper chains), 1 punch, 2 room, 3 punch + room (the
// default AY chains).

namespace
{
constexpr double kRate = 44100.0;
constexpr int32_t kFrame = 882;

std::vector<int16_t> SquareFrame()
{
    std::vector<int16_t> buffer(static_cast<size_t>(kFrame) * 2);
    for (int32_t n = 0; n < kFrame; n++)
    {
        buffer[n * 2] = std::fmod(n * 440.0 / kRate, 1.0) < 0.5 ? 8192 : -8192;
        buffer[n * 2 + 1] = std::fmod(n * 660.0 / kRate, 1.0) < 0.5 ? 6144 : -6144;
    }
    return buffer;
}
}  // namespace

static void BM_AudioCharacterChain_Frame(benchmark::State& state)
{
    const int effects = static_cast<int>(state.range(0));
    AudioCharacterChain chain;
    chain.setup(kRate);
    chain.setChipType(AudioCharacterChain::ChipType::AY);
    chain.setPunchPreset(AudioCharacterChain::PunchPreset::AY);
    chain.setPunchEnabled((effects & 1) != 0);
    chain.setRoomMode((effects & 2) != 0 ? AudioCharacterChain::RoomMode::Room_9dB : AudioCharacterChain::RoomMode::Off);

    const std::vector<int16_t> source = SquareFrame();
    std::vector<int16_t> buffer = source;
    for (auto _ : state)
    {
        buffer = source;
        chain.processInt16(buffer.data(), kFrame);
        benchmark::DoNotOptimize(buffer.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kFrame);
    static const char* const labels[] = {"off", "punch", "room", "punch+room"};
    state.SetLabel(labels[effects & 3]);
}
BENCHMARK(BM_AudioCharacterChain_Frame)->Arg(0)->Arg(1)->Arg(2)->Arg(3);
