// Host audio files to Red Book PCM (audiofiledecoder.h): WAV in every sample format
// (made here), MP3 and FLAC (testdata/media/audio, tools/cd/make-audio-file-fixtures.py)
// checked against the tones they were made of: length (MP3 gapless trimming), frequency,
// amplitude, lossless FLAC sample for sample, the resampler against an ideal sine, mono
// upmix and the 5.1 downmix.

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/io/storage/cd/audiofiledecoder.h"

using namespace AudioFileDecoder;

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    std::vector<uint8_t> ReadFixture(const std::string& name)
    {
        std::ifstream in(TestPathHelper::GetTestDataPath("media/audio/" + name), std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
    }

    /// A RIFF WAVE file: `format` 1 (PCM) / 3 (float) / 0xFFFE (extensible of `sub`), interleaved `data`
    std::vector<uint8_t> WaveFile(uint16_t format, uint16_t channels, uint32_t rate, uint16_t bits, const std::vector<uint8_t>& data,
                                  uint16_t sub = 1)
    {
        std::vector<uint8_t> out;
        auto u32 = [&out](uint32_t v) { for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>(v >> (8 * i))); };
        auto u16 = [&out](uint16_t v) { out.push_back(static_cast<uint8_t>(v)); out.push_back(static_cast<uint8_t>(v >> 8)); };
        const uint32_t fmtSize = format == 0xFFFE ? 40 : 16;
        out.insert(out.end(), {'R', 'I', 'F', 'F'});
        u32(static_cast<uint32_t>(4 + 8 + fmtSize + 8 + data.size()));
        out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
        u32(fmtSize);
        u16(format);
        u16(channels);
        u32(rate);
        u32(rate * channels * bits / 8);
        u16(static_cast<uint16_t>(channels * bits / 8));
        u16(bits);
        if (format == 0xFFFE)
        {
            u16(22);
            u16(bits);
            u32(0);
            u16(sub);
            for (int i = 0; i < 14; i++)
                out.push_back(0);
        }
        out.insert(out.end(), {'d', 'a', 't', 'a'});
        u32(static_cast<uint32_t>(data.size()));
        out.insert(out.end(), data.begin(), data.end());
        return out;
    }

    /// Goertzel: the amplitude of `hz` in `count` samples (stride 2: one channel of stereo pairs)
    double Amplitude(const std::vector<int16_t>& pcm, size_t first, size_t count, int channel, double hz, double rate = 44100)
    {
        const double w = 2 * kPi * hz / rate;
        double s1 = 0;
        double s2 = 0;
        for (size_t i = 0; i < count; i++)
        {
            const double s = pcm[2 * (first + i) + channel] / 32768.0 + 2 * std::cos(w) * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        const double power = s1 * s1 + s2 * s2 - 2 * std::cos(w) * s1 * s2;
        return 2 * std::sqrt(std::max(0.0, power)) / static_cast<double>(count);
    }

    int16_t Tone(double amplitude, double hz, uint64_t n, uint32_t rate)
    {
        return static_cast<int16_t>(std::lround(amplitude * 32767 * std::sin(2 * kPi * hz * static_cast<double>(n) / rate)));
    }
}  // namespace

TEST(AudioFileDecoder_Test, KindByExtension)
{
    EXPECT_EQ(KindOf("a/Song.MP3"), Kind::Mp3);
    EXPECT_EQ(KindOf("x.flac"), Kind::Flac);
    EXPECT_EQ(KindOf("x.Wav"), Kind::Wav);
    EXPECT_EQ(KindOf("x.wave"), Kind::Wav);
    EXPECT_EQ(KindOf("x.ogg"), Kind::None);
    EXPECT_EQ(KindOf("mp3"), Kind::None);
}

TEST(AudioFileDecoder_Test, WaveSampleFormats)
{
    // The same 7 values in every format: 16-bit stereo at 44.1 kHz passes bit for bit
    const int16_t values[] = {0, 1, -1, 12345, -12345, 32767, -32768};
    std::vector<uint8_t> pcm16;
    for (int16_t v : values)
        for (int c = 0; c < 2; c++)
            pcm16.push_back(static_cast<uint8_t>(v)), pcm16.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8));
    Pcm pcm;
    std::string error;
    ASSERT_TRUE(Decode(Kind::Wav, WaveFile(1, 2, 44100, 16, pcm16), pcm, &error)) << error;
    std::vector<int16_t> red = ToRedBook(pcm);
    ASSERT_EQ(red.size(), 14u);
    for (size_t i = 0; i < 7; i++)
    {
        EXPECT_EQ(red[2 * i], values[i]);
        EXPECT_EQ(red[2 * i + 1], values[i]);
    }

    // 24-bit, 32-bit integer, float 32 / 64, extensible: the 16-bit value in the top bits
    std::vector<uint8_t> pcm24, pcm32, f32, f64;
    for (int16_t v : values)
    {
        const int32_t wide = static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint16_t>(v)) << 16);
        for (int b = 1; b < 4; b++)
            pcm24.push_back(static_cast<uint8_t>(wide >> (8 * b)));
        for (int b = 0; b < 4; b++)
            pcm32.push_back(static_cast<uint8_t>(wide >> (8 * b)));
        const float f = v / 32768.0f;
        uint32_t fb = 0;
        std::memcpy(&fb, &f, 4);
        for (int b = 0; b < 4; b++)
            f32.push_back(static_cast<uint8_t>(fb >> (8 * b)));
        const double d = v / 32768.0;
        uint64_t db = 0;
        std::memcpy(&db, &d, 8);
        for (int b = 0; b < 8; b++)
            f64.push_back(static_cast<uint8_t>(db >> (8 * b)));
    }
    const std::vector<std::vector<uint8_t>> files = {WaveFile(1, 1, 44100, 24, pcm24), WaveFile(1, 1, 44100, 32, pcm32),
                                                     WaveFile(3, 1, 44100, 32, f32), WaveFile(3, 1, 44100, 64, f64),
                                                     WaveFile(0xFFFE, 1, 44100, 24, pcm24, 1), WaveFile(0xFFFE, 1, 44100, 32, f32, 3)};
    for (size_t f = 0; f < files.size(); f++)
    {
        ASSERT_TRUE(Decode(Kind::Wav, files[f], pcm, &error)) << f << ": " << error;
        red = ToRedBook(pcm);
        ASSERT_EQ(red.size(), 14u) << f;
        for (size_t i = 0; i < 7; i++)
        {
            EXPECT_EQ(red[2 * i], values[i]) << "file " << f << " value " << i;
            EXPECT_EQ(red[2 * i + 1], values[i]) << "mono: both sides";
        }
    }

    // 8-bit unsigned: 128 is silence
    ASSERT_TRUE(Decode(Kind::Wav, WaveFile(1, 1, 44100, 8, {128, 255, 0}), pcm, &error)) << error;
    red = ToRedBook(pcm);
    EXPECT_EQ(red[0], 0);
    EXPECT_EQ(red[2], 127 * 256);
    EXPECT_EQ(red[4], -32768);

    // Errors say why
    EXPECT_FALSE(Decode(Kind::Wav, {'R', 'I', 'F', 'F'}, pcm, &error));
    EXPECT_NE(error.find("RIFF WAVE"), std::string::npos) << error;
    EXPECT_FALSE(Decode(Kind::Wav, WaveFile(2, 1, 44100, 4, {1, 2}), pcm, &error));  // ADPCM
    EXPECT_NE(error.find("not supported"), std::string::npos) << error;
}

TEST(AudioFileDecoder_Test, ResamplerMatchesAnIdealSine)
{
    // 1 kHz at 48 kHz, 0.2 s -> 44.1 kHz: the length rule, and inside (away from the edges) the
    // samples of the same sine at 44.1 kHz within -60 dB
    const uint32_t frames = 9600;
    std::vector<uint8_t> data;
    for (uint32_t n = 0; n < frames; n++)
    {
        const int16_t v = Tone(0.5, 1000, n, 48000);
        data.push_back(static_cast<uint8_t>(v));
        data.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8));
    }
    Pcm pcm;
    std::string error;
    ASSERT_TRUE(Decode(Kind::Wav, WaveFile(1, 1, 48000, 16, data), pcm, &error)) << error;
    const std::vector<int16_t> red = ToRedBook(pcm);
    ASSERT_EQ(red.size() / 2, RedBookFrames(frames, 48000));
    EXPECT_EQ(red.size() / 2, 8820u);
    double worst = 0;
    for (size_t n = 200; n < 8620; n++)
    {
        const double ideal = 0.5 * 32767 * std::sin(2 * kPi * 1000 * static_cast<double>(n) / 44100);
        worst = std::max(worst, std::abs(red[2 * n] - ideal));
        ASSERT_EQ(red[2 * n], red[2 * n + 1]);
    }
    EXPECT_LT(worst, 32767 * 0.001) << "the windowed sinc keeps the tone (error under -60 dB)";
    EXPECT_EQ(RedBookFrames(1, 48000), 1u);
    EXPECT_EQ(RedBookFrames(22050, 22050), 44100u);
    EXPECT_EQ(RedBookFrames(441, 44100), 441u);
}

TEST(AudioFileDecoder_Test, Mp3ToneGaplessLengthAndPitch)
{
    // 0.5 s at 44.1 kHz, left 440 Hz / right 660 Hz, amplitude 0.5, LAME CBR: the LAME tag's delay
    // and padding cut, exactly the 22050 samples that went in
    Pcm pcm;
    std::string error;
    ASSERT_TRUE(Decode(Kind::Mp3, ReadFixture("tone-440-660-44k-stereo.mp3"), pcm, &error)) << error;
    EXPECT_EQ(pcm.rate, 44100u);
    EXPECT_EQ(pcm.channels, 2u);
    EXPECT_EQ(pcm.Frames(), 22050u) << "gapless: encoder delay and padding removed";
    const std::vector<int16_t> red = ToRedBook(pcm);
    ASSERT_EQ(red.size(), 44100u);
    EXPECT_NEAR(Amplitude(red, 2000, 17640, 0, 440), 0.5, 0.03);
    EXPECT_NEAR(Amplitude(red, 2000, 17640, 1, 660), 0.5, 0.03);
    EXPECT_LT(Amplitude(red, 2000, 17640, 0, 660), 0.02) << "the channels are not swapped or mixed";
    // The phase too: the first sample is the sine's start (no delay left): the samples follow sin()
    double error1 = 0;
    for (size_t n = 1000; n < 1100; n++)
        error1 = std::max(error1, std::abs(red[2 * n] - static_cast<double>(Tone(0.5, 440, n, 44100))));
    EXPECT_LT(error1, 32767 * 0.03) << "aligned to the input sample for sample";
}

TEST(AudioFileDecoder_Test, Mp3MonoAt48kHzUpmixedAndResampled)
{
    Pcm pcm;
    std::string error;
    ASSERT_TRUE(Decode(Kind::Mp3, ReadFixture("tone-1000-48k-mono.mp3"), pcm, &error)) << error;
    EXPECT_EQ(pcm.rate, 48000u);
    EXPECT_EQ(pcm.channels, 1u);
    EXPECT_EQ(pcm.Frames(), 24000u);
    const std::vector<int16_t> red = ToRedBook(pcm);
    ASSERT_EQ(red.size() / 2, 22050u);
    for (size_t n = 0; n < red.size() / 2; n++)
        ASSERT_EQ(red[2 * n], red[2 * n + 1]) << "mono plays on both sides";
    EXPECT_NEAR(Amplitude(red, 2000, 17640, 0, 1000), 0.5, 0.03);
}

TEST(AudioFileDecoder_Test, FlacLosslessResampledAndDownmixed)
{
    // 16-bit stereo at 44.1 kHz: lossless, every sample as made
    Pcm pcm;
    std::string error;
    ASSERT_TRUE(Decode(Kind::Flac, ReadFixture("tone-440-44k-stereo-16.flac"), pcm, &error)) << error;
    ASSERT_EQ(pcm.Frames(), 11025u);
    std::vector<int16_t> red = ToRedBook(pcm);
    for (uint64_t n = 0; n < 11025; n++)
    {
        ASSERT_EQ(red[2 * n], Tone(0.5, 440, n, 44100)) << n;
        ASSERT_EQ(red[2 * n + 1], Tone(0.5, 660, n, 44100)) << n;
    }

    // 24-bit mono at 96 kHz: the 24-bit samples exact before conversion; 4410 sample pairs after
    ASSERT_TRUE(Decode(Kind::Flac, ReadFixture("tone-1000-96k-mono-24.flac"), pcm, &error)) << error;
    EXPECT_EQ(pcm.rate, 96000u);
    ASSERT_EQ(pcm.Frames(), 9600u);
    for (uint64_t n = 0; n < 9600; n += 37)
    {
        const double expected = std::lround(0.5 * 8388607 * std::sin(2 * kPi * 1000 * static_cast<double>(n) / 96000)) / 8388608.0;
        ASSERT_NEAR(pcm.samples[n], expected, 1e-7) << n;
    }
    red = ToRedBook(pcm);
    EXPECT_EQ(red.size() / 2, 4410u);
    EXPECT_NEAR(Amplitude(red, 400, 3528, 0, 1000), 0.5, 0.01);

    // 6 channels (FL 440, FR 660, C 1000, LFE / BL / BR silent): L = (FL + 0.7071 C + 0.7071 BL) / 2.414
    ASSERT_TRUE(Decode(Kind::Flac, ReadFixture("tone-6ch-44k-16.flac"), pcm, &error)) << error;
    ASSERT_EQ(pcm.channels, 6u);
    red = ToRedBook(pcm);
    ASSERT_EQ(red.size() / 2, 4410u);
    const double norm = 1 + 2 * 0.70710678;
    for (uint64_t n = 0; n < 4410; n += 7)
    {
        const double left = (Tone(0.5, 440, n, 44100) + 0.70710678 * Tone(0.5, 1000, n, 44100)) / norm;
        const double right = (Tone(0.5, 660, n, 44100) + 0.70710678 * Tone(0.5, 1000, n, 44100)) / norm;
        ASSERT_NEAR(red[2 * n], left, 1.0) << n;
        ASSERT_NEAR(red[2 * n + 1], right, 1.0) << n;
    }

    EXPECT_FALSE(Decode(Kind::Flac, {'f', 'L', 'a', 'C', 0}, pcm, &error));
    EXPECT_FALSE(error.empty());
}
