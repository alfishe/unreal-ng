// VDAC4 ESP sound on the mixer (Vdac4Audio): the app's PCM resampled to the mixer rate, frame by frame.

#include <gtest/gtest.h>

#include <vector>

#include "emulator/platforms/tsconf/vdac4/vdac4audio.h"

namespace
{
std::vector<int16_t> Ramp(size_t frames, int16_t start, int16_t step)
{
    std::vector<int16_t> out;
    for (size_t i = 0; i < frames; i++)
    {
        const auto v = static_cast<int16_t>(start + static_cast<int>(i) * step);
        out.push_back(v);
        out.push_back(static_cast<int16_t>(-v));
    }
    return out;
}
}  // namespace

TEST(Vdac4Audio_Test, UpsamplesFourTimesLinearly)
{
    // DOOM's 11025 Hz into a 44100 Hz mixer: 4 output frames per ESP frame, on the line between them
    Vdac4Audio audio;
    audio.SetOutputRate(44100);
    const std::vector<int16_t> in = Ramp(4, 400, 400);  // 400 800 1200 1600
    audio.Push(in.data(), 4, 11025);
    EXPECT_EQ(audio.Queued(), 16u);
    audio.FrameEnd(16);
    const int16_t* out = audio.Buffer();
    // From the silent start towards 400, then 400 -> 800 ...
    EXPECT_EQ(out[0], 0);
    EXPECT_EQ(out[2], 100);
    EXPECT_EQ(out[8], 400);
    EXPECT_EQ(out[10], 500);
    EXPECT_EQ(out[11], -500);
    EXPECT_TRUE(audio.HadSoundLastFrame());
    EXPECT_EQ(audio.GetStats().inputRate, 11025u);
    EXPECT_EQ(audio.GetStats().underruns, 0u);
}

TEST(Vdac4Audio_Test, UnderrunHoldsTheLastLevel)
{
    Vdac4Audio audio;
    audio.SetOutputRate(44100);
    const std::vector<int16_t> in = Ramp(2, 1000, 0);
    audio.Push(in.data(), 2, 44100);
    audio.FrameEnd(5);
    const int16_t* out = audio.Buffer();
    EXPECT_EQ(out[2], 1000);
    EXPECT_EQ(out[4], 1000) << "padded with the last sample, not a step to zero";
    EXPECT_EQ(out[9], -1000);
    EXPECT_EQ(audio.GetStats().underruns, 1u);
}

TEST(Vdac4Audio_Test, LagIsBoundedToThreeFrames)
{
    // The ESP ran far ahead (a long SPI burst): the oldest samples go, the latency stays bounded
    Vdac4Audio audio;
    audio.SetOutputRate(44100);
    const std::vector<int16_t> in = Ramp(1000, 0, 1);
    audio.Push(in.data(), 1000, 44100);
    audio.FrameEnd(100);
    EXPECT_EQ(audio.Queued(), 100u * Vdac4Audio::kMaxLagFrames);
    EXPECT_EQ(audio.GetStats().dropped, 1000u - 100u - 300u);
}

TEST(Vdac4Audio_Test, ExposesOneMixerRow)
{
    Vdac4Audio audio;
    std::vector<CardMixerRow> rows;
    audio.MixerRows(rows);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].type, AudioSourceType::Vdac4Esp);
    EXPECT_FALSE(rows[0].ssgRow);
    EXPECT_EQ(audio.MixerBuffer(AudioSourceType::Vdac4Esp), nullptr) << "no frame mixed yet";
    audio.FrameEnd(4);
    EXPECT_NE(audio.MixerBuffer(AudioSourceType::Vdac4Esp), nullptr);
    EXPECT_EQ(audio.MixerBuffer(AudioSourceType::COVOX), nullptr);
}
