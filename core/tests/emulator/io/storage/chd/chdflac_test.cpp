// The FLAC frames of the CHD `flac` codec (chdflac.h). libFLAC's frames (LPC, Rice2, wasted bits as libFLAC chooses
// them) are read in chdfile_test.cpp from chdman's mixed-flac.chd; here our encoder against our decoder, every
// stereo mode and block edge, and the frame CRCs.

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

#include "emulator/io/storage/chd/chdflac.h"

using namespace chd;

namespace
{
    std::vector<int16_t> RoundTrip(const std::vector<int16_t>& samples, int channels, uint32_t blockSize, size_t* encodedBytes = nullptr)
    {
        std::vector<uint8_t> encoded;
        const uint32_t frames = static_cast<uint32_t>(samples.size() / static_cast<size_t>(channels));
        flac::Encode(samples.data(), frames, channels, blockSize, encoded);
        if (encodedBytes)
            *encodedBytes = encoded.size();
        std::vector<int16_t> decoded(samples.size());
        EXPECT_TRUE(flac::Decode(encoded.data(), encoded.size(), decoded.data(), frames, channels));
        return decoded;
    }
}  // namespace

TEST(ChdFlac_Test, FramesRoundTripEveryKindOfSignal)
{
    std::mt19937 rng(7);
    std::vector<int16_t> sine(2048);  // 1024 stereo frames: one block of a 4 KB hunk
    for (size_t i = 0; i < 1024; i++)
    {
        sine[2 * i] = static_cast<int16_t>(12000 * std::sin(i / 15.0));
        sine[2 * i + 1] = static_cast<int16_t>(12000 * std::sin(i / 15.0) - 300);  // close to the left: side wins
    }
    size_t bytes = 0;
    EXPECT_EQ(RoundTrip(sine, 2, 1024, &bytes), sine);
    EXPECT_LT(bytes, 4096u / 2) << "a sine compresses";

    std::vector<int16_t> noise(4096);
    for (int16_t& s : noise)
        s = static_cast<int16_t>(rng());
    EXPECT_EQ(RoundTrip(noise, 2, 1024), noise) << "full-scale noise (verbatim subframes, 17-bit side)";

    EXPECT_EQ(RoundTrip(std::vector<int16_t>(2048, -5), 2, 1024), std::vector<int16_t>(2048, -5)) << "constant";

    std::vector<int16_t> extremes(2048);
    for (size_t i = 0; i < extremes.size(); i++)
        extremes[i] = (i / 2) % 2 ? 32767 : -32768;
    EXPECT_EQ(RoundTrip(extremes, 2, 1024), extremes) << "the largest residuals";

    std::vector<int16_t> odd(2 * 777);
    for (size_t i = 0; i < odd.size(); i++)
        odd[i] = static_cast<int16_t>((i * 37) % 2000 - 1000);
    EXPECT_EQ(RoundTrip(odd, 2, 256), odd) << "several frames, the last one short (frame numbers, block-size codes)";
    EXPECT_EQ(RoundTrip(odd, 2, 300), odd) << "a block size without its own code";

    std::vector<int16_t> mono(1000);
    for (size_t i = 0; i < mono.size(); i++)
        mono[i] = static_cast<int16_t>(i * 13);
    EXPECT_EQ(RoundTrip(mono, 1, 1000), mono);
}

TEST(ChdFlac_Test, DamagedFramesAreRefused)
{
    std::vector<int16_t> samples(2048);
    for (size_t i = 0; i < samples.size(); i++)
        samples[i] = static_cast<int16_t>(i * 5);
    std::vector<uint8_t> encoded;
    flac::Encode(samples.data(), 1024, 2, 1024, encoded);
    std::vector<int16_t> out(2048);
    ASSERT_TRUE(flac::Decode(encoded.data(), encoded.size(), out.data(), 1024, 2));

    std::vector<uint8_t> damaged = encoded;
    damaged[damaged.size() / 2] ^= 0x01;
    EXPECT_FALSE(flac::Decode(damaged.data(), damaged.size(), out.data(), 1024, 2)) << "CRC-16 of the frame";
    damaged = encoded;
    damaged[3] ^= 0x01;
    EXPECT_FALSE(flac::Decode(damaged.data(), damaged.size(), out.data(), 1024, 2)) << "CRC-8 of the header";
    EXPECT_FALSE(flac::Decode(encoded.data(), encoded.size() / 2, out.data(), 1024, 2)) << "cut short";
    EXPECT_FALSE(flac::Decode(encoded.data(), encoded.size(), out.data(), 1024, 1)) << "wrong channel count";
}
