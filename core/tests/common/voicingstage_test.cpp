#include "stdafx.h"
#include "pch.h"

#include <cmath>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "common/sound/filters/voicingstage.h"

/// VoicingStage: thread-safe profile requests applied at frame boundaries,
/// click-free switching (two-frame pre-roll + one-frame crossfade), history
/// invalidation. Design: docs/inprogress/2026-09-25-ay-tone-voicing/ay-tone-voicing-tdd.md §4.4

namespace
{
using Preset = FilterVoicing::Preset;

constexpr double kRate = 48000.0;
constexpr size_t kFrame = 960;  // 20 ms at 48 kHz
constexpr double kPi = 3.14159265358979323846;

/// One frame of a stereo AY-like tone: a square after the 5 Hz coupling is a
/// bipolar square around zero; left 41.2 Hz, right 110 Hz, -6 dBFS
std::vector<int16_t> ToneFrame(size_t frameIndex)
{
    std::vector<int16_t> frame(kFrame * 2);
    for (size_t n = 0; n < kFrame; n++)
    {
        const double t = static_cast<double>(frameIndex * kFrame + n) / kRate;
        frame[n * 2] = std::fmod(t * 41.2, 1.0) < 0.5 ? 8192 : -8192;
        frame[n * 2 + 1] = std::fmod(t * 110.0, 1.0) < 0.5 ? 8192 : -8192;
    }
    return frame;
}

/// Energy above ~8 kHz: a click is broadband and shows up here, while the
/// square's own edges are the same in every frame. First difference twice
/// (+12 dB/oct) is enough of a high-pass for a relative comparison
double HighFrequencyEnergy(const std::vector<int16_t>& frame)
{
    double energy = 0.0;
    for (size_t ch = 0; ch < 2; ch++)
    {
        for (size_t n = 2; n < kFrame; n++)
        {
            const double d2 = frame[n * 2 + ch] - 2.0 * frame[(n - 1) * 2 + ch] + frame[(n - 2) * 2 + ch];
            energy += d2 * d2;
        }
    }
    return energy;
}
}  // namespace

TEST(VoicingStage_Test, RequestIsVisibleAtOnceAndAppliedAtNextFrame)
{
    VoicingStage stage(kFrame);
    stage.setup(kRate);
    stage.setPresetImmediate(Preset::Classic);

    stage.request(Preset::Flat);
    EXPECT_EQ(stage.requested(), Preset::Flat) << "a read right after a write shows the request";
    EXPECT_EQ(stage.active(), Preset::Classic) << "nothing changes before the next frame";

    std::vector<int16_t> frame = ToneFrame(0);
    stage.process(frame.data(), kFrame);
    EXPECT_EQ(stage.active(), Preset::Flat);
}

TEST(VoicingStage_Test, SwitchMatchesIdealCrossfade)
{
    // Reference: two filters that ran continuously from the start, crossfaded
    // over the switch frame. With the two-frame pre-roll the stage must land
    // within +-2 LSB of it over the WHOLE switch frame (simulated < 0.01 LSB;
    // a bare reset leaves ~525 LSB, one frame of pre-roll ~1.3 LSB)
    VoicingStage stage(kFrame);
    stage.setup(kRate);
    stage.setPresetImmediate(Preset::Flat);
    FilterVoicing continuousClassic(kRate, Preset::Classic);

    const size_t switchFrame = 6;
    for (size_t f = 0; f <= switchFrame; f++)
    {
        std::vector<int16_t> input = ToneFrame(f);
        std::vector<int16_t> output = input;
        if (f == switchFrame)
            stage.request(Preset::Classic);
        stage.process(output.data(), kFrame);

        for (size_t n = 0; n < kFrame; n++)
        {
            for (size_t ch = 0; ch < 2; ch++)
            {
                const double x = input[n * 2 + ch];
                const double classicY = continuousClassic.process(ch, x);
                if (f == switchFrame)
                {
                    const double t = (static_cast<double>(n) + 0.5) / kFrame;
                    const double ideal = (1.0 - t) * x + t * classicY;
                    ASSERT_NEAR(output[n * 2 + ch], ideal, 2.0) << "sample " << n << " ch " << ch;
                }
            }
        }
    }
    EXPECT_EQ(stage.active(), Preset::Classic);
}

TEST(VoicingStage_Test, SwitchIsClickFree)
{
    // Flat -> Classic -> Flat on a steady tone: the switch frame carries no
    // more energy above ~8 kHz than its neighbours (+1 dB)
    VoicingStage stage(kFrame);
    stage.setup(kRate);
    stage.setPresetImmediate(Preset::Flat);

    std::vector<double> hf;
    for (size_t f = 0; f < 16; f++)
    {
        if (f == 6)
            stage.request(Preset::Classic);
        if (f == 11)
            stage.request(Preset::Flat);
        std::vector<int16_t> frame = ToneFrame(f);
        stage.process(frame.data(), kFrame);
        hf.push_back(HighFrequencyEnergy(frame));
    }

    for (size_t f : {6u, 11u})
    {
        const double neighbours = (hf[f - 1] + hf[f + 1]) / 2.0;
        EXPECT_LE(10.0 * std::log10(hf[f] / neighbours), 1.0) << "switch frame " << f;
    }
}

TEST(VoicingStage_Test, NoHistoryStartsCold)
{
    // First frame after reset(): no history, the switch still completes and
    // produces finite output (the cold start is documented, not asserted smooth)
    VoicingStage stage(kFrame);
    stage.setup(kRate);
    stage.setPresetImmediate(Preset::Flat);
    stage.reset();
    ASSERT_EQ(stage.historyFrames(), 0u);

    stage.request(Preset::Classic);
    std::vector<int16_t> frame = ToneFrame(0);
    stage.process(frame.data(), kFrame);
    EXPECT_EQ(stage.active(), Preset::Classic);
    EXPECT_EQ(stage.historyFrames(), 1u);
}

TEST(VoicingStage_Test, PreRollHistoryInvalidatedOnRateChange)
{
    VoicingStage stage(kFrame);
    stage.setup(kRate);
    stage.setPresetImmediate(Preset::Classic);
    for (size_t f = 0; f < 3; f++)
    {
        std::vector<int16_t> frame = ToneFrame(f);
        stage.process(frame.data(), kFrame);
    }
    ASSERT_EQ(stage.historyFrames(), VoicingStage::HISTORY_FRAMES);
    const double state = stage.filter().stateMagnitude();

    stage.setup(96000.0);
    EXPECT_EQ(stage.historyFrames(), 0u) << "old-rate samples must never pre-roll a new-rate filter";
    EXPECT_EQ(stage.filter().stateMagnitude(), state) << "filter state is kept across a rate change";

    stage.invalidateHistory();
    EXPECT_EQ(stage.historyFrames(), 0u);
}

TEST(VoicingStage_Test, ResetAppliesPendingRequestAtOnce)
{
    VoicingStage stage(kFrame);
    stage.setup(kRate);
    stage.setPresetImmediate(Preset::Classic);
    std::vector<int16_t> frame = ToneFrame(0);
    stage.process(frame.data(), kFrame);

    stage.request(Preset::Flat);
    stage.reset();
    EXPECT_EQ(stage.active(), Preset::Flat) << "after a reset there is nothing to crossfade from";
    EXPECT_EQ(stage.historyFrames(), 0u);
}

TEST(VoicingStage_Test, RequestFromOtherThreadAppliesAtFrameBoundary)
{
    // The only shared state is one atomic; the stream changes profile only
    // between process() calls. (Race freedom holds by construction - the repo
    // has no TSan build, so none is claimed here)
    VoicingStage stage(kFrame);
    stage.setup(kRate);
    stage.setPresetImmediate(Preset::Flat);

    std::thread writer([&stage]() { stage.request(Preset::Classic); });
    writer.join();

    EXPECT_EQ(stage.active(), Preset::Flat);
    std::vector<int16_t> frame = ToneFrame(0);
    stage.process(frame.data(), kFrame);
    EXPECT_EQ(stage.active(), Preset::Classic);
}
