#include <gtest/gtest.h>
#include "_helpers/testpathhelper.h"
#include "_helpers/tzxtapebuilder.h"
#include "3rdparty/tinywav/tinywav.h"
#include "common/filehelper.h"
#include "ffmpeg_probe.h"
#include "tapeaudio/tapeaudioconfig.h"
#include "tapeaudio/tapeaudiorenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

/// Tape-audio renderer synthesis tests (tape-audio-bridge design §8.1):
/// exact sample counts on the absolute fixed-point grid, edge positions
/// within ±1 sample, pauses as digital silence, polarity inversion, WAV
/// header shape, block-range prefix property and the error paths. Everything
/// runs through the public RenderTapeToAudio entry point with synthetic TZX
/// images — no engine instance anywhere.

namespace
{
    constexpr uint32_t RATE = 44100;

    std::string WriteScratchTzx(const std::string& name, const TzxTapeBuilder& builder)
    {
        const std::string path = TestPathHelper::GetTestScratchPath("tapeaudio-render/" + name);
        EXPECT_TRUE(TzxTapeBuilder::WriteToFile(builder.Bytes(), path));
        return path;
    }

    /// Exact expected sample count for a total T-state duration: the renderer
    /// emits sample k while k * step < total (step = 32.32 T-states/sample),
    /// i.e. ceil(total / step) samples — the same integer math, verified.
    uint64_t ExpectedSampleCount(uint64_t totalTstates, uint32_t rate)
    {
        const uint64_t step = (uint64_t(TapeAudio::TSTATE_HZ) << 32) / rate;
        return ((uint64_t(totalTstates) << 32) + step - 1) / step;
    }

    /// First sample index at/after an edge: floor(edgeT / step) — the sample
    /// that first carries the new level.
    uint64_t ExpectedEdgeSample(uint64_t edgeTstates, uint32_t rate)
    {
        const uint64_t step = (uint64_t(TapeAudio::TSTATE_HZ) << 32) / rate;
        return (uint64_t(edgeTstates) << 32) / step;
    }

    std::vector<float> ReadWavMono(const std::string& path, uint32_t* outRate = nullptr)
    {
        std::vector<float> samples;

        TinyWav tw;
        if (tinywav_open_read(&tw, path.c_str(), TW_INTERLEAVED) != 0)
        {
            return samples;
        }

        if (outRate != nullptr)
        {
            *outRate = tw.h.SampleRate;
        }

        std::vector<float> chunk(4096);
        int read = 0;
        while ((read = tinywav_read_f(&tw, chunk.data(), static_cast<int>(chunk.size()))) > 0)
        {
            samples.insert(samples.end(), chunk.begin(), chunk.begin() + read);
        }
        tinywav_close_read(&tw);

        return samples;
    }

    /// Indices where the sample sign changes (the rendered edges).
    std::vector<size_t> FindSignChanges(const std::vector<float>& samples)
    {
        std::vector<size_t> edges;
        for (size_t i = 1; i < samples.size(); i++)
        {
            if ((samples[i - 1] < 0.0f && samples[i] > 0.0f) || (samples[i - 1] > 0.0f && samples[i] < 0.0f))
            {
                edges.push_back(i);
            }
        }
        return edges;
    }

    TapeRenderResult RenderSingleBlock(const std::string& source, const std::string& out, size_t blockIndex,
                                       bool invert = false)
    {
        TapeRenderRequest request;
        request.sourcePath = source;
        request.firstBlock = blockIndex;
        request.lastBlock = blockIndex;
        request.outputPath = out;
        request.sampleRate = RATE;
        request.invertLevel = invert;
        return RenderTapeToAudio(request);
    }
}

TEST(TapeAudioRenderer_Test, PulseSequenceRendersExactSampleCount)
{
    // Block 0: 4 known half-periods (1+2+1+2 ms = 6 ms of signal);
    // block 1: $20 pause of 100 ms (its own Control entry — design §5.4)
    TzxTapeBuilder builder;
    builder.AddPulseSequence({3500, 7000, 3500, 7000});
    builder.AddPause(100);
    const std::string source = WriteScratchTzx("pulses.tzx", builder);
    const std::string out = TestPathHelper::GetTestScratchPath("tapeaudio-render/pulses.wav");

    TapeRenderRequest request;
    request.sourcePath = source;
    request.outputPath = out;
    request.sampleRate = RATE;
    TapeRenderResult result = RenderTapeToAudio(request);

    ASSERT_TRUE(result.ok) << result.errorText;
    const uint64_t expected = ExpectedSampleCount(6 * 3500 + 100 * 3500, RATE);
    EXPECT_EQ(result.samplesWritten, expected);
    EXPECT_NEAR(result.durationSec, 0.106, 0.002);
}

TEST(TapeAudioRenderer_Test, EdgePositionsWithinOneSample)
{
    TzxTapeBuilder builder;
    builder.AddPulseSequence({3500, 7000, 3500, 7000});
    builder.AddPause(100);
    const std::string source = WriteScratchTzx("edges.tzx", builder);
    const std::string out = TestPathHelper::GetTestScratchPath("tapeaudio-render/edges.wav");

    TapeRenderRequest request;
    request.sourcePath = source;
    request.outputPath = out;
    request.sampleRate = RATE;
    ASSERT_TRUE(RenderTapeToAudio(request).ok);

    const std::vector<float> samples = ReadWavMono(out);
    ASSERT_GT(samples.size(), 100u);

    // Three mid-signal edges at cumulative T-states 3500 / 10500 / 14000; the
    // fourth (21000) is the signal->silence transition, checked via the last
    // non-zero sample instead (FindSignChanges skips zero crossings).
    const uint64_t expected[3] = {3500, 10500, 14000};
    const std::vector<size_t> edges = FindSignChanges(samples);
    ASSERT_GE(edges.size(), 3u);

    for (size_t i = 0; i < 3; i++)
    {
        const double predicted = static_cast<double>(ExpectedEdgeSample(expected[i], RATE));
        EXPECT_NEAR(static_cast<double>(edges[i]), predicted, 1.0)
            << "edge " << i << ": actual " << edges[i] << " vs predicted " << predicted;
    }

    size_t lastNonZero = samples.size() - 1;
    while (lastNonZero > 0 && samples[lastNonZero] == 0.0f)
    {
        lastNonZero--;
    }
    const double predictedEnd = static_cast<double>(ExpectedEdgeSample(21000, RATE));
    EXPECT_NEAR(static_cast<double>(lastNonZero), predictedEnd, 1.5);
}

TEST(TapeAudioRenderer_Test, PauseRendersAsDigitalSilence)
{
    // Whole tape: block 0 is 2 ms of signal, block 1 a $20 pause of 50 ms —
    // the pause is its own Control entry, so the range must include it
    TzxTapeBuilder builder;
    builder.AddPulseSequence({3500, 3500});
    builder.AddPause(50);
    const std::string source = WriteScratchTzx("pause.tzx", builder);
    const std::string out = TestPathHelper::GetTestScratchPath("tapeaudio-render/pause.wav");

    TapeRenderRequest request;
    request.sourcePath = source;
    request.outputPath = out;
    request.sampleRate = RATE;
    ASSERT_TRUE(RenderTapeToAudio(request).ok);

    const std::vector<float> samples = ReadWavMono(out);

    // ~52 ms total: signal present, then everything from 3 ms on is zero
    ASSERT_GE(samples.size(), RATE * 51 / 1000);
    ASSERT_LT(samples.size(), RATE * 53 / 1000);
    ASSERT_NE(samples[0], 0.0f) << "render is all silence — signal block missing";
    for (size_t i = RATE * 3 / 1000; i < samples.size(); i++)
    {
        ASSERT_EQ(samples[i], 0.0f) << "sample " << i << " in pause region is not silence";
    }
}

TEST(TapeAudioRenderer_Test, PolarityInversionFlipsOnlySign)
{
    TzxTapeBuilder builder;
    builder.AddPulseSequence({3500, 7000, 3500});
    builder.AddPause(10);
    const std::string source = WriteScratchTzx("invert.tzx", builder);

    const std::string outA = TestPathHelper::GetTestScratchPath("tapeaudio-render/invert-a.wav");
    const std::string outB = TestPathHelper::GetTestScratchPath("tapeaudio-render/invert-b.wav");

    ASSERT_TRUE(RenderSingleBlock(source, outA, 0, false).ok);
    ASSERT_TRUE(RenderSingleBlock(source, outB, 0, true).ok);

    const std::vector<float> a = ReadWavMono(outA);
    const std::vector<float> b = ReadWavMono(outB);

    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); i++)
    {
        // Same durations, opposite start level -> exact negation everywhere
        EXPECT_FLOAT_EQ(a[i], -b[i]) << "sample " << i;
    }
}

TEST(TapeAudioRenderer_Test, StandardBlockRendersRomPilotTiming)
{
    // ROM-standard byte block: pilot half-period 2168 T (~27.3 samples @44.1k)
    TzxTapeBuilder builder;
    builder.AddStandardBlock(10, {0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    const std::string source = WriteScratchTzx("rompilot.tzx", builder);
    const std::string out = TestPathHelper::GetTestScratchPath("tapeaudio-render/rompilot.wav");

    ASSERT_TRUE(RenderSingleBlock(source, out, 0).ok);

    const std::vector<float> samples = ReadWavMono(out);

    // Every sample is silence or exactly ±amplitude (mono int16 render)
    const float amplitude = 0.8f * 32767.0f / 32768.0f;
    for (float s : samples)
    {
        ASSERT_TRUE(s == 0.0f || std::abs(s - amplitude) < 0.001f || std::abs(s + amplitude) < 0.001f)
            << "unexpected sample value " << s;
    }

    // Median pilot run length (constant-sign stretch) lands in [24, 31] samples
    std::vector<size_t> runLengths;
    size_t run = 1;
    for (size_t i = 1; i < RATE; i++)  // first second is pure header pilot
    {
        if ((samples[i] > 0) == (samples[i - 1] > 0) && samples[i] != 0.0f && samples[i - 1] != 0.0f)
        {
            run++;
        }
        else if (samples[i - 1] != 0.0f)
        {
            runLengths.push_back(run);
            run = 1;
        }
    }
    ASSERT_GT(runLengths.size(), 100u);  // pilot: 8064 pulses in ~5 s -> ~1600/s
    std::sort(runLengths.begin(), runLengths.end());
    const size_t median = runLengths[runLengths.size() / 2];
    EXPECT_GE(median, 24u);
    EXPECT_LE(median, 31u);
}

TEST(TapeAudioRenderer_Test, RangeRenderIsPrefixOfWholeRender)
{
    TzxTapeBuilder builder;
    builder.AddPulseSequence({3500, 7000});
    builder.AddPause(20);
    builder.AddPulseSequence({7000, 3500});
    builder.AddPause(20);
    const std::string source = WriteScratchTzx("prefix.tzx", builder);

    const std::string outBlock0 = TestPathHelper::GetTestScratchPath("tapeaudio-render/prefix-b0.wav");
    const std::string outWhole = TestPathHelper::GetTestScratchPath("tapeaudio-render/prefix-all.wav");

    TapeRenderResult block0 = RenderSingleBlock(source, outBlock0, 0);
    TapeRenderRequest whole;
    whole.sourcePath = source;
    whole.outputPath = outWhole;
    whole.sampleRate = RATE;
    TapeRenderResult all = RenderTapeToAudio(whole);

    ASSERT_TRUE(block0.ok) << block0.errorText;
    ASSERT_TRUE(all.ok) << all.errorText;

    const std::vector<float> b = ReadWavMono(outBlock0);
    const std::vector<float> w = ReadWavMono(outWhole);

    // Block levels restart per block, so a single-block render is exactly the
    // prefix of the whole-tape render (same absolute grid, same start level)
    ASSERT_EQ(block0.samplesWritten, b.size());
    ASSERT_GE(w.size(), b.size());
    for (size_t i = 0; i < b.size(); i++)
    {
        EXPECT_FLOAT_EQ(b[i], w[i]) << "sample " << i;
    }
}

TEST(TapeAudioRenderer_Test, WavHeaderFieldsAreMonoInt16)
{
    TzxTapeBuilder builder;
    builder.AddPulseSequence({3500, 3500});
    const std::string source = WriteScratchTzx("header.tzx", builder);
    const std::string out = TestPathHelper::GetTestScratchPath("tapeaudio-render/header.wav");

    ASSERT_TRUE(RenderSingleBlock(source, out, 0).ok);

    TinyWav tw;
    ASSERT_EQ(tinywav_open_read(&tw, out.c_str(), TW_INTERLEAVED), 0);
    EXPECT_EQ(tw.h.NumChannels, 1);
    EXPECT_EQ(tw.h.SampleRate, RATE);
    EXPECT_EQ(tw.h.BitsPerSample, 16);
    EXPECT_EQ(tw.h.AudioFormat, 1);  // PCM
    tinywav_close_read(&tw);
}

TEST(TapeAudioRenderer_Test, ErrorPathsAreStructured)
{
    const std::string out = TestPathHelper::GetTestScratchPath("tapeaudio-render/err.wav");

    // Missing source
    TapeRenderRequest missing;
    missing.sourcePath = TestPathHelper::GetTestScratchPath("tapeaudio-render/does-not-exist.tzx");
    missing.outputPath = out;
    TapeRenderResult r1 = RenderTapeToAudio(missing);
    EXPECT_FALSE(r1.ok);
    EXPECT_FALSE(r1.errorText.empty());

    // Unsupported output extension
    TzxTapeBuilder builder;
    builder.AddPulseSequence({3500});
    const std::string source = WriteScratchTzx("err.tzx", builder);
    TapeRenderRequest badExt;
    badExt.sourcePath = source;
    badExt.outputPath = TestPathHelper::GetTestScratchPath("tapeaudio-render/err.mp3");
    TapeRenderResult r2 = RenderTapeToAudio(badExt);
    EXPECT_FALSE(r2.ok);
    EXPECT_NE(r2.errorText.find("extension"), std::string::npos);

    // Block index out of range
    TapeRenderResult r3 = RenderSingleBlock(source, out, 5);
    EXPECT_FALSE(r3.ok);
    EXPECT_NE(r3.errorText.find("out of range"), std::string::npos);
}

TEST(TapeAudioRenderer_Test, FlacRenderOrHonestSkip)
{
    TzxTapeBuilder builder;
    builder.AddPulseSequence({3500, 7000});
    builder.AddPause(10);
    const std::string source = WriteScratchTzx("flac.tzx", builder);
    const std::string out = TestPathHelper::GetTestScratchPath("tapeaudio-render/flac.flac");

    TapeRenderRequest request;
    request.sourcePath = source;
    request.outputPath = out;
    request.sampleRate = RATE;

    if (FFmpegProbe::isAvailable())
    {
        // Machine has ffmpeg: the render must succeed and produce output
        TapeRenderResult result = RenderTapeToAudio(request);
        ASSERT_TRUE(result.ok) << result.errorText;
        EXPECT_EQ(result.encoderUsed, "ffmpeg(flac)");
        EXPECT_GT(result.samplesWritten, 0u);
        EXPECT_TRUE(FileHelper::FileExists(out));
        EXPECT_GT(FileHelper::GetFileSize(out), 0u);
    }
    else
    {
        // No ffmpeg: refuse with the WAV alternative, never a silent failure
        TapeRenderResult result = RenderTapeToAudio(request);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.errorText.find("WAV"), std::string::npos);
    }
}
