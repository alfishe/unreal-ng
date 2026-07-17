#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "emulator/recording/encoders/dsd/dsd_types.h"
#include "emulator/recording/encoders/dsd/dsf_writer.h"
#include "emulator/recording/encoders/dsd/native_dsd_converter.h"
#include "emulator/sound/native_audio_tap.h"

namespace
{
constexpr uint32_t NATIVE_RATE = 218750;  // PSG_CLOCK_RATE / 8 (1.75 MHz Pentagon)

/// Fraction of 1-bits in a DSD byte stream for one channel
/// (input is byte-interleaved by channel)
double BitDensity(const std::vector<uint8_t>& interleaved, uint8_t channels, uint8_t channel)
{
    uint64_t ones = 0;
    uint64_t total = 0;

    for (size_t i = channel; i < interleaved.size(); i += channels)
    {
        for (int b = 0; b < 8; b++)
        {
            if (interleaved[i] & (1 << b))
                ones++;
            total++;
        }
    }

    return total ? static_cast<double>(ones) / total : 0.0;
}
}  // namespace

/// region <NativeAudioTap>

TEST(NativeAudioTapTest, PushPopRoundTrip)
{
    NativeAudioTap tap;
    tap.activate(1024);

    for (int i = 0; i < 100; i++)
    {
        tap.push(static_cast<float>(i), static_cast<float>(-i));
    }

    EXPECT_EQ(tap.available(), 100u);

    std::vector<float> out(200 * 2);
    size_t got = tap.pop(out.data(), 200);

    EXPECT_EQ(got, 100u);
    EXPECT_FLOAT_EQ(out[0], 0.0f);
    EXPECT_FLOAT_EQ(out[2], 1.0f);
    EXPECT_FLOAT_EQ(out[3], -1.0f);
    EXPECT_FLOAT_EQ(out[198], 99.0f);
    EXPECT_FLOAT_EQ(out[199], -99.0f);
    EXPECT_EQ(tap.overruns(), 0u);

    tap.deactivate();
    EXPECT_FALSE(tap.isActive());
}

TEST(NativeAudioTapTest, OverrunDropsFramesWithoutCorruption)
{
    NativeAudioTap tap;
    tap.activate(64);  // Tiny ring

    for (int i = 0; i < 200; i++)
    {
        tap.push(1.0f, 2.0f);
    }

    EXPECT_GT(tap.overruns(), 0u);
    EXPECT_LE(tap.available(), 64u);

    std::vector<float> out(64 * 2);
    size_t got = tap.pop(out.data(), 64);
    EXPECT_EQ(got, tap.overruns() > 0 ? 64u : got);

    for (size_t i = 0; i < got; i++)
    {
        EXPECT_FLOAT_EQ(out[i * 2], 1.0f);
        EXPECT_FLOAT_EQ(out[i * 2 + 1], 2.0f);
    }
}

/// endregion </NativeAudioTap>

/// region <NativeDSDConverter>

TEST(NativeDSDConverterTest, ExactRationalBitCount)
{
    // 218750 Hz -> DSD64 (2822400 Hz) is exactly 8064 bits per 625 input
    // samples. First frame only primes the interpolator, so 626 frames
    // give exactly 625 intervals -> 8064 bits per channel = 1008 bytes/ch.
    NativeDSDConverter converter(NATIVE_RATE, DSDRate::DSD64, 2);

    std::vector<float> frames(626 * 2, 0.0f);
    std::vector<uint8_t> dsd;
    converter.Process(frames.data(), 626, dsd);
    converter.Flush(dsd);

    // 1008 bytes per channel * 2 channels; 8064 % 8 == 0 so Flush adds nothing
    EXPECT_EQ(dsd.size(), 1008u * 2u);
}

TEST(NativeDSDConverterTest, SilenceGivesHalfDensity)
{
    // DSD encodes 0.0 as ~50% ones
    NativeDSDConverter converter(NATIVE_RATE, DSDRate::DSD64, 2);
    converter.SetInputGain(0.0);

    std::vector<float> frames(20000 * 2, 0.0f);
    std::vector<uint8_t> dsd;
    converter.Process(frames.data(), 20000, dsd);

    EXPECT_NEAR(BitDensity(dsd, 2, 0), 0.5, 0.02);
    EXPECT_NEAR(BitDensity(dsd, 2, 1), 0.5, 0.02);
}

TEST(NativeDSDConverterTest, PositiveDCRaisesDensity)
{
    // DC +0.5 (unity gain) should give ~75% ones: density = (1 + x) / 2
    NativeDSDConverter converter(NATIVE_RATE, DSDRate::DSD64, 2);
    converter.SetInputGain(0.0);

    std::vector<float> frames(20000 * 2, 0.5f);
    std::vector<uint8_t> dsd;
    converter.Process(frames.data(), 20000, dsd);

    EXPECT_NEAR(BitDensity(dsd, 2, 0), 0.75, 0.02);
    EXPECT_NEAR(BitDensity(dsd, 2, 1), 0.75, 0.02);
}

TEST(NativeDSDConverterTest, IndependentChannels)
{
    // Left at +0.4, right at -0.4: densities must diverge symmetrically
    NativeDSDConverter converter(NATIVE_RATE, DSDRate::DSD64, 2);
    converter.SetInputGain(0.0);

    std::vector<float> frames(20000 * 2);
    for (size_t i = 0; i < 20000; i++)
    {
        frames[i * 2] = 0.4f;
        frames[i * 2 + 1] = -0.4f;
    }

    std::vector<uint8_t> dsd;
    converter.Process(frames.data(), 20000, dsd);

    EXPECT_NEAR(BitDensity(dsd, 2, 0), 0.7, 0.02);
    EXPECT_NEAR(BitDensity(dsd, 2, 1), 0.3, 0.02);
}

TEST(NativeDSDConverterTest, SineAveragesToHalfDensity)
{
    // A full number of sine periods should average to ~50% density
    NativeDSDConverter converter(NATIVE_RATE, DSDRate::DSD128, 2);
    converter.SetInputGain(0.0);

    constexpr size_t frameCount = 43750;  // 200 periods of 1 kHz at 218750 Hz
    std::vector<float> frames(frameCount * 2);
    for (size_t i = 0; i < frameCount; i++)
    {
        float s = 0.5f * std::sin(2.0 * M_PI * 1000.0 * i / NATIVE_RATE);
        frames[i * 2] = s;
        frames[i * 2 + 1] = s;
    }

    std::vector<uint8_t> dsd;
    converter.Process(frames.data(), frameCount, dsd);

    // DSD128 from 218750 Hz: ratio 5644800/218750 = 25.8048
    // 43749 intervals * 25.8048 ~ 1128935 bits/ch
    EXPECT_NEAR(BitDensity(dsd, 2, 0), 0.5, 0.01);

    // Modulator should not be overloaded by a -6 dBFS sine
    EXPECT_LT(converter.GetStats().modulatorLoad, 0.9);
}

TEST(NativeDSDConverterTest, SineReconstructsFromBitstream)
{
    // End-to-end signal integrity: encode a 1 kHz sine, decode the DSD
    // bitstream with a boxcar lowpass, and correlate against the ideal.
    constexpr double FREQ = 1000.0;
    constexpr float AMP = 0.5f;

    NativeDSDConverter converter(NATIVE_RATE, DSDRate::DSD64, 2);
    converter.SetInputGain(0.0);

    constexpr size_t frameCount = 65625;  // 300 ms
    std::vector<float> frames(frameCount * 2);
    for (size_t i = 0; i < frameCount; i++)
    {
        float s = AMP * static_cast<float>(std::sin(2.0 * M_PI * FREQ * i / NATIVE_RATE));
        frames[i * 2] = s;
        frames[i * 2 + 1] = s;
    }

    std::vector<uint8_t> dsd;
    converter.Process(frames.data(), frameCount, dsd);

    // Unpack channel 0 bits to +/-1
    std::vector<double> bits;
    bits.reserve(dsd.size() / 2 * 8);
    for (size_t i = 0; i < dsd.size(); i += 2)
    {
        for (int b = 7; b >= 0; b--)
            bits.push_back((dsd[i] >> b) & 1 ? 1.0 : -1.0);
    }

    // Boxcar lowpass, window 512 at 2.8224 MHz (~5.5 kHz cutoff): passes
    // 1 kHz, kills shaped noise
    constexpr size_t WINDOW = 512;
    ASSERT_GT(bits.size(), WINDOW * 20);

    // Correlate decoded signal with ideal sine and with 90-degree shifted
    // version (phase-insensitive amplitude estimate), skipping the first
    // 10000 bits (modulator settle + interpolator prime)
    constexpr uint32_t DSD_RATE_HZ = 2822400;
    double sumSin = 0, sumCos = 0, sumSq = 0;
    size_t n = 0;

    double acc = 0;
    for (size_t i = 0; i < WINDOW; i++)
        acc += bits[i];

    for (size_t i = WINDOW; i + 1 < bits.size(); i++)
    {
        acc += bits[i] - bits[i - WINDOW];

        if (i < 10000)
            continue;

        double decoded = acc / WINDOW;
        // Time at the window center
        double t = (i - WINDOW / 2.0) / DSD_RATE_HZ;

        sumSin += decoded * std::sin(2.0 * M_PI * FREQ * t);
        sumCos += decoded * std::cos(2.0 * M_PI * FREQ * t);
        sumSq += decoded * decoded;
        n++;
    }

    // Recovered amplitude via quadrature projection
    double ampSin = 2.0 * sumSin / n;
    double ampCos = 2.0 * sumCos / n;
    double recovered = std::sqrt(ampSin * ampSin + ampCos * ampCos);

    // The boxcar itself attenuates 1 kHz by sinc(f * W / Fs); compensate
    double x = FREQ * WINDOW / static_cast<double>(DSD_RATE_HZ);
    double boxcarGain = std::sin(M_PI * x) / (M_PI * x);
    double expected = AMP * boxcarGain;

    // Amplitude must survive within 2%
    EXPECT_NEAR(recovered, expected, 0.02 * expected);

    // Signal power should dominate the decoded stream: the sine accounts
    // for recovered^2/2 of sumSq/n total power
    double signalPower = recovered * recovered / 2.0;
    double totalPower = sumSq / n;
    EXPECT_GT(signalPower / totalPower, 0.9);
}

TEST(NativeDSDConverterTest, PunchDoesNotBreakModulator)
{
    NativeDSDConverter converter(NATIVE_RATE, DSDRate::DSD64, 2);
    converter.SetInputGain(-6.0);
    converter.SetPunchEnabled(true);
    converter.SetPunchAmount(0.7);

    constexpr size_t frameCount = 21875;  // 100 ms
    std::vector<float> frames(frameCount * 2);
    for (size_t i = 0; i < frameCount; i++)
    {
        // Harsh square-ish content: worst case for saturation
        float s = (std::sin(2.0 * M_PI * 3000.0 * i / NATIVE_RATE) > 0) ? 0.9f : -0.9f;
        frames[i * 2] = s;
        frames[i * 2 + 1] = s;
    }

    std::vector<uint8_t> dsd;
    converter.Process(frames.data(), frameCount, dsd);

    EXPECT_GT(dsd.size(), 0u);
    EXPECT_NEAR(BitDensity(dsd, 2, 0), 0.5, 0.05);
    EXPECT_LT(converter.GetStats().modulatorLoad, 1.0);
}

/// endregion </NativeDSDConverter>

/// region <DSDEncoder end-to-end>

#include "emulator/recording/encoders/dsd/dsd_encoder.h"
#include "emulator/recording/encoder_config.h"

#include <chrono>
#include <memory>
#include <thread>

class DSDEncoderE2ETest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _tempDir = std::filesystem::temp_directory_path() / "dsd_encoder_e2e_test";
        std::filesystem::create_directories(_tempDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(_tempDir, ec);
    }

    std::filesystem::path _tempDir;
};

TEST_F(DSDEncoderE2ETest, NativeModeWritesValidDSFFile)
{
    // Full production path: tap -> worker thread -> converter -> DSF on disk.
    // Simulates the emulation thread pushing native-rate frames while the
    // encoder's worker consumes them, exactly as during real recording.
    auto path = (_tempDir / "native_e2e.dsf").string();

    auto tap = std::make_shared<NativeAudioTap>();

    DSDEncoder encoder;
    encoder.SetDSDRate(DSDRate::DSD128);
    encoder.SetPunchEnabled(true);
    encoder.SetPunchAmount(0.4);
    encoder.SetNativeTap(tap, NATIVE_RATE);

    EncoderConfig config;
    config.audioSampleRate = 44100;
    config.audioChannels = 2;

    ASSERT_TRUE(encoder.Start(path, config)) << encoder.GetLastError();
    ASSERT_TRUE(encoder.IsRecording());
    ASSERT_TRUE(tap->isActive());

    // Producer: 500 ms of a 440 Hz tone at native rate, pushed in
    // emulation-sized bursts (4375 frames = one 20 ms video frame) at
    // realtime pace. The DSD128 worker converts at ~3.3x realtime, so
    // the ring must never overrun at 1x.
    constexpr size_t TOTAL_FRAMES = NATIVE_RATE / 2;
    size_t pushed = 0;
    auto nextFrame = std::chrono::steady_clock::now();
    while (pushed < TOTAL_FRAMES)
    {
        size_t burst = std::min<size_t>(4375, TOTAL_FRAMES - pushed);
        for (size_t i = 0; i < burst; i++)
        {
            float s = 0.6f * static_cast<float>(
                std::sin(2.0 * M_PI * 440.0 * (pushed + i) / NATIVE_RATE));
            tap->push(s, s);
        }
        pushed += burst;

        nextFrame += std::chrono::milliseconds(20);
        std::this_thread::sleep_until(nextFrame);
    }

    EXPECT_EQ(tap->overruns(), 0u);

    encoder.Stop();
    EXPECT_FALSE(tap->isActive());

    // Encoder must have consumed everything the tap accepted
    EXPECT_EQ(encoder.GetAudioSamplesEncoded(), TOTAL_FRAMES);

    // Validate the file on disk
    std::ifstream f(path, std::ios::binary);
    ASSERT_TRUE(f.is_open());
    std::vector<uint8_t> content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());

    // Expected size: 0.5 s at DSD128 = 2822400 bits/ch = 352800 bytes/ch,
    // 2 channels, block-padded to 4096 + 92-byte headers
    ASSERT_GT(content.size(), 700000u);

    // Magic chunks
    EXPECT_EQ(std::memcmp(content.data(), "DSD ", 4), 0);
    EXPECT_EQ(std::memcmp(content.data() + 28, "fmt ", 4), 0);
    EXPECT_EQ(std::memcmp(content.data() + 80, "data", 4), 0);

    // Header total-size field must match actual file size
    uint64_t storedSize = 0;
    for (int i = 0; i < 8; i++)
        storedSize |= static_cast<uint64_t>(content[12 + i]) << (i * 8);
    EXPECT_EQ(storedSize, content.size());

    // Sample rate field: DSD128
    uint32_t storedRate = 0;
    for (int i = 0; i < 4; i++)
        storedRate |= static_cast<uint32_t>(content[56 + i]) << (i * 8);
    EXPECT_EQ(storedRate, 5644800u);

    // Channel count field
    uint32_t storedChannels = 0;
    for (int i = 0; i < 4; i++)
        storedChannels |= static_cast<uint32_t>(content[52 + i]) << (i * 8);
    EXPECT_EQ(storedChannels, 2u);

    // Data must not be silence-collapsed: a healthy DSD stream of a tone
    // has roughly half the bits set
    uint64_t ones = 0, total = 0;
    for (size_t i = 92; i < content.size(); i++)
    {
        for (int b = 0; b < 8; b++)
            if (content[i] & (1 << b))
                ones++;
        total += 8;
    }
    double density = static_cast<double>(ones) / total;
    EXPECT_NEAR(density, 0.5, 0.05);
}

TEST_F(DSDEncoderE2ETest, PCMModeStillWritesDSF)
{
    // Regression check for the non-native path (44.1 kHz mix input)
    auto path = (_tempDir / "pcm_mode.dsf").string();

    DSDEncoder encoder;
    encoder.SetDSDRate(DSDRate::DSD64);

    EncoderConfig config;
    config.audioSampleRate = 44100;
    config.audioChannels = 2;

    ASSERT_TRUE(encoder.Start(path, config)) << encoder.GetLastError();
    EXPECT_FALSE(encoder.IsNativeMode());

    // 100 ms of 1 kHz tone as int16 PCM, fed like CaptureAudio does
    constexpr size_t FRAMES = 4410;
    std::vector<int16_t> pcm(FRAMES * 2);
    for (size_t i = 0; i < FRAMES; i++)
    {
        int16_t s = static_cast<int16_t>(12000.0 * std::sin(2.0 * M_PI * 1000.0 * i / 44100.0));
        pcm[i * 2] = s;
        pcm[i * 2 + 1] = s;
    }
    encoder.OnAudioSamples(pcm.data(), FRAMES * 2, 0.0);

    encoder.Stop();

    ASSERT_TRUE(std::filesystem::exists(path));
    // 100 ms at DSD64: 282240 bits/ch = 35280 bytes/ch * 2 + headers
    EXPECT_GT(std::filesystem::file_size(path), 70000u);
}

/// endregion </DSDEncoder end-to-end>

/// region <DSFWriter>

class DSFWriterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _tempDir = std::filesystem::temp_directory_path() / "dsf_writer_test";
        std::filesystem::create_directories(_tempDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(_tempDir, ec);
    }

    std::filesystem::path _tempDir;
};

TEST_F(DSFWriterTest, ChannelBlockInterleaving)
{
    // Feed byte-interleaved data (ch0 = 0xAA, ch1 = 0x55) spanning more
    // than one 4096-byte block per channel; DSF requires the data chunk
    // to hold a full 4096-byte block of ch0, then one of ch1, etc.
    auto path = (_tempDir / "interleave.dsf").string();

    DSFWriter writer;
    ASSERT_TRUE(writer.Open(path, 2822400, 2, 1));

    constexpr size_t BYTES_PER_CH = 5000;  // > 4096: forces one full flush + padding
    std::vector<uint8_t> data(BYTES_PER_CH * 2);
    for (size_t i = 0; i < BYTES_PER_CH; i++)
    {
        data[i * 2] = 0xAA;      // ch0
        data[i * 2 + 1] = 0x55;  // ch1
    }

    ASSERT_TRUE(writer.Write(data));
    writer.Close();

    // Read the file back and inspect the data chunk
    std::ifstream f(path, std::ios::binary);
    ASSERT_TRUE(f.is_open());

    std::vector<uint8_t> content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());

    // Layout: DSD chunk (28) + fmt chunk (52) + data header (12) = offset 92
    constexpr size_t DATA_OFFSET = 92;
    ASSERT_GE(content.size(), DATA_OFFSET + 4096 * 4);

    // Block 1: 4096 bytes of ch0
    for (size_t i = 0; i < 4096; i++)
    {
        ASSERT_EQ(content[DATA_OFFSET + i], 0xAA) << "ch0 block corrupted at " << i;
    }

    // Block 2: 4096 bytes of ch1
    for (size_t i = 0; i < 4096; i++)
    {
        ASSERT_EQ(content[DATA_OFFSET + 4096 + i], 0x55) << "ch1 block corrupted at " << i;
    }

    // Block 3: partial ch0 (904 data bytes then zero padding)
    for (size_t i = 0; i < BYTES_PER_CH - 4096; i++)
    {
        ASSERT_EQ(content[DATA_OFFSET + 4096 * 2 + i], 0xAA);
    }
    for (size_t i = BYTES_PER_CH - 4096; i < 4096; i++)
    {
        ASSERT_EQ(content[DATA_OFFSET + 4096 * 2 + i], 0x00) << "padding not zeroed at " << i;
    }

    // Block 4: partial ch1
    for (size_t i = 0; i < BYTES_PER_CH - 4096; i++)
    {
        ASSERT_EQ(content[DATA_OFFSET + 4096 * 3 + i], 0x55);
    }
}

TEST_F(DSFWriterTest, HeaderSampleRateAndSize)
{
    auto path = (_tempDir / "header.dsf").string();

    DSFWriter writer;
    ASSERT_TRUE(writer.Open(path, 5644800, 2, 1));  // DSD128

    std::vector<uint8_t> data(8192, 0x69);
    ASSERT_TRUE(writer.Write(data));
    writer.Close();

    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());

    // Total file size stored at offset 12 (uint64 LE)
    uint64_t storedSize = 0;
    for (int i = 0; i < 8; i++)
        storedSize |= static_cast<uint64_t>(content[12 + i]) << (i * 8);
    EXPECT_EQ(storedSize, content.size());

    // Sample rate at offset 28 (DSD hdr) + 28 (into fmt) = 56... verify layout:
    // fmt starts at 28; version(4) id(4) chtype(4) chnum(4) rate(4) begins
    // at 28 + 12 + 16 = offset 28+28 = 56? fmt: id(4) size(8) version(4)
    // id(4) chtype(4) chnum(4) rate -> 28 + 4 + 8 + 4 + 4 + 4 + 4 = 56
    uint32_t storedRate = 0;
    for (int i = 0; i < 4; i++)
        storedRate |= static_cast<uint32_t>(content[56 + i]) << (i * 8);
    EXPECT_EQ(storedRate, 5644800u);
}

/// endregion </DSFWriter>
