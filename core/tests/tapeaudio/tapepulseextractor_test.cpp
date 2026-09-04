#include <gtest/gtest.h>
#include "tapeaudio/tapeaudioconfig.h"
#include "tapeaudio/tapepulseextractor.h"

#include <cstdint>
#include <vector>

/// Tape pulse extractor tests (tape-audio-bridge design §8.2): Schmitt
/// recovery of exact half-periods, envelope normalization (DC offset +
/// gain), hysteresis noise rejection, gap entries, runt merging and the
/// degenerate inputs. Synthesis here is deliberately independent of the
/// renderer — samples are placed on a plain k * TSTATE_HZ / rate grid.

namespace
{
    /// Square wave from T-state half-periods; level holds from each edge to
    /// the next. Independent of the renderer's fixed-point grid on purpose.
    std::vector<float> Synthesize(const std::vector<uint64_t>& periodsT, uint32_t rate,
                                  float amplitude = 0.8f, bool startHigh = true, float dc = 0.0f)
    {
        std::vector<uint64_t> edges;
        uint64_t acc = 0;
        for (uint64_t period : periodsT)
        {
            acc += period;
            edges.push_back(acc);
        }

        const uint64_t totalSamples = acc * rate / TapeAudio::TSTATE_HZ + 2;
        std::vector<float> out;
        out.reserve(static_cast<size_t>(totalSamples));

        bool level = startHigh;
        size_t edgeIndex = 0;
        for (uint64_t k = 0; k < totalSamples; k++)
        {
            const uint64_t t = k * TapeAudio::TSTATE_HZ / rate;
            while (edgeIndex < edges.size() && t >= edges[edgeIndex])
            {
                level = !level;
                edgeIndex++;
            }
            out.push_back(dc + (level ? amplitude : -amplitude));
        }
        return out;
    }

    /// Two sample clocks of tolerance (edge quantization both sides) plus
    /// the round-to-nearest T conversion.
    uint64_t ToleranceT(uint32_t rate)
    {
        return 2 * (TapeAudio::TSTATE_HZ / rate) + 2;
    }
}

TEST(TapePulseExtractor_Test, RecoversHalfPeriods)
{
    const std::vector<uint64_t> periods = {855, 1710, 855, 855, 2168, 667};
    const TapePulseTrack track = TapePulseExtractor::Extract(Synthesize(periods, 44100), 44100);

    ASSERT_EQ(track.entries.size(), periods.size());
    for (size_t i = 0; i < periods.size(); i++)
    {
        EXPECT_NEAR(track.entries[i], static_cast<double>(periods[i]), ToleranceT(44100))
            << "entry " << i;
    }
    EXPECT_EQ(track.signalEdges, periods.size());
    EXPECT_EQ(track.gapCount, 0u);
}

TEST(TapePulseExtractor_Test, NormalizesDcOffsetAndGain)
{
    const std::vector<uint64_t> periods = {855, 1710, 855, 1710};
    // Same signal buried at +0.3 DC with a tenth of the amplitude — the
    // envelope placement must recover the identical edges
    const TapePulseTrack track = TapePulseExtractor::Extract(Synthesize(periods, 44100, 0.1f, true, 0.3f), 44100);

    ASSERT_EQ(track.entries.size(), periods.size());
    for (size_t i = 0; i < periods.size(); i++)
    {
        EXPECT_NEAR(track.entries[i], static_cast<double>(periods[i]), ToleranceT(44100))
            << "entry " << i;
    }
}

TEST(TapePulseExtractor_Test, HysteresisRejectsSubBandNoise)
{
    const std::vector<uint64_t> periods = {1710, 855, 1710, 855};
    std::vector<float> samples = Synthesize(periods, 44100);

    // Deterministic +-0.05 ripple: 6% of the 0.8 amplitude, well inside the
    // 20% Schmitt band — must not produce a single extra edge
    for (size_t k = 0; k < samples.size(); k++)
    {
        // Cast before subtracting: (k * 7) % 13 is size_t, and modulo < 6
        // would underflow to ~1.8e19 before the float conversion
        const int rippleUnit = static_cast<int>((k * 7) % 13) - 6;
        const float ripple = static_cast<float>(rippleUnit) / 13.0f * 0.1f;
        samples[k] += ripple;
    }

    const TapePulseTrack track = TapePulseExtractor::Extract(samples, 44100);
    ASSERT_EQ(track.entries.size(), periods.size());
}

TEST(TapePulseExtractor_Test, SilenceBetweenSignalsBecomesGapEntry)
{
    // 2 ms signal, 100 ms of held-level silence, 2 ms signal
    const std::vector<uint64_t> periods = {3500, 3500, 100 * 3500ull, 3500, 3500};
    const TapePulseTrack track = TapePulseExtractor::Extract(Synthesize(periods, 44100), 44100);

    ASSERT_EQ(track.entries.size(), 5u);
    EXPECT_EQ(track.signalEdges, 4u);
    EXPECT_EQ(track.gapCount, 1u);
    EXPECT_NEAR(track.entries[2], 100.0 * 3500, ToleranceT(44100));

    const uint32_t gapMs = track.entries[2] / 3500;
    EXPECT_NEAR(gapMs, 100u, 2u);
}

TEST(TapePulseExtractor_Test, RuntPulsesMergeIntoNextEdge)
{
    // At 3.5 MHz one sample == one T-state, so a 5 T glitch is placeable
    const std::vector<uint64_t> periods = {855, 5, 855};
    const TapePulseTrack track = TapePulseExtractor::Extract(Synthesize(periods, 3500000), 3500000);

    ASSERT_EQ(track.entries.size(), 2u);
    EXPECT_EQ(track.entries[0], 855u);
    EXPECT_EQ(track.entries[1], 860u);  // 855 + the merged 5 T runt
}

TEST(TapePulseExtractor_Test, DegenerateInputsYieldEmptyTracks)
{
    TapePulseTrack track = TapePulseExtractor::Extract({}, 44100);
    EXPECT_TRUE(track.entries.empty());

    track = TapePulseExtractor::Extract(std::vector<float>(1000, 0.0f), 44100);
    EXPECT_TRUE(track.entries.empty());

    track = TapePulseExtractor::Extract(std::vector<float>(1000, 0.5f), 44100);
    EXPECT_TRUE(track.entries.empty());
}
