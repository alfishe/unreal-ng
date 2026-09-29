// VS1001 / VS1011-class MP3 decoder (neogs-tdd.md §5.6)

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <vector>

#include "3rdparty/minimp3/minimp3.h"
#include "_helpers/testpathhelper.h"
#include "emulator/sound/chips/neogs/vs10xx.h"

namespace
{
constexpr double kUnits = 1e6; // 1 unit = 1 us

std::vector<uint8_t> readFixture(const char* name)
{
    const auto path = TestPathHelper::FindProjectRoot() / "testdata/sound/neogs/mp3" / name;
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Sci
{
    SpiDevice* port;
    void write(uint8_t reg, uint16_t value)
    {
        port->select(true);
        port->exchange(0x02);
        port->exchange(reg);
        port->exchange(static_cast<uint8_t>(value >> 8));
        port->exchange(static_cast<uint8_t>(value));
        port->select(false);
    }
    uint16_t read(uint8_t reg)
    {
        port->select(true);
        port->exchange(0x03);
        port->exchange(reg);
        const uint8_t hi = port->exchange(0xFF);
        const uint8_t lo = port->exchange(0xFF);
        port->select(false);
        return static_cast<uint16_t>((hi << 8) | lo);
    }
};

/// Feed a whole stream the way players do (bytes while DREQ = 1), then let it
/// play out; returns the raw played samples
std::vector<int16_t> play(Vs10xxDecoder& d, const std::vector<uint8_t>& stream, int64_t& now, int trailingZeros = 0)
{
    std::vector<int16_t> out;
    std::vector<uint8_t> data = stream;
    data.insert(data.end(), trailingZeros, 0x00);
    size_t at = 0;
    while (at < data.size())
    {
        while (at < data.size() && d.dreq(now))
        {
            for (int i = 0; i < 32 && at < data.size(); i++)
                d.sdi()->exchange(data[at++]);
        }
        now += 250;
        const auto part = d.drainPlayed();
        out.insert(out.end(), part.begin(), part.end());
    }
    for (int i = 0; i < 4000; i++) // play out the tail
    {
        now += 250;
        d.advance(now);
        const auto part = d.drainPlayed();
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

/// minimp3 on its own, whole-buffer streaming (its documented usage)
std::vector<int16_t> reference(const std::vector<uint8_t>& stream, int& rate)
{
    mp3dec_t dec;
    memset(&dec, 0, sizeof dec);
    mp3dec_init(&dec);
    std::vector<int16_t> out;
    size_t at = 0;
    while (at < stream.size())
    {
        mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        mp3dec_frame_info_t info{};
        const int samples = mp3dec_decode_frame(&dec, stream.data() + at, static_cast<int>(stream.size() - at), pcm, &info);
        if (info.frame_bytes == 0)
            break;
        at += static_cast<size_t>(info.frame_bytes);
        rate = info.hz;
        for (int i = 0; i < samples; i++)
        {
            out.push_back(pcm[i * info.channels]);
            out.push_back(info.channels > 1 ? pcm[i * info.channels + 1] : pcm[i * info.channels]);
        }
    }
    return out;
}
} // namespace

TEST(Vs10xxDecoder, HeldInResetUntilXresetThenDreqAfterLatency)
{
    Vs10xxDecoder d(Vs10xxDecoder::Chip::VS1001, Vs10xxDecoder::Level::Software, kUnits);
    EXPECT_FALSE(d.dreq(0));
    Sci sci{d.sci()};
    EXPECT_EQ(sci.read(Vs10xxDecoder::SCI_STATUS), 0xFFFF) << "no answer while held in reset";

    d.setReset(true, 1000);
    // 50,000 crystal clocks at 14.318 MHz = 3.49 ms
    EXPECT_FALSE(d.dreq(1000 + 3400));
    EXPECT_TRUE(d.dreq(1000 + 3600));
}

TEST(Vs10xxDecoder, VersionBitsAndRegister2PerChip)
{
    for (auto chip : {Vs10xxDecoder::Chip::VS1001, Vs10xxDecoder::Chip::VS1011})
    {
        Vs10xxDecoder d(chip, Vs10xxDecoder::Level::Software, kUnits);
        d.setReset(true, 0);
        Sci sci{d.sci()};
        EXPECT_EQ((sci.read(Vs10xxDecoder::SCI_STATUS) >> 4) & 7, chip == Vs10xxDecoder::Chip::VS1011 ? 1 : 0);
        sci.write(Vs10xxDecoder::SCI_REG2, 0x8008); // INT_FCTLH clock doubler / BASS
        EXPECT_EQ(sci.read(Vs10xxDecoder::SCI_REG2), 0x8008) << "a plain read/write register on both";
        sci.write(Vs10xxDecoder::SCI_STATUS, 0x0070);
        EXPECT_EQ((sci.read(Vs10xxDecoder::SCI_STATUS) >> 4) & 7, chip == Vs10xxDecoder::Chip::VS1011 ? 1 : 0)
            << "version bits are read-only";
    }
}

TEST(Vs10xxDecoder, SoftwareResetKeepsVolumeAndHoldsDreq)
{
    Vs10xxDecoder d(Vs10xxDecoder::Chip::VS1001, Vs10xxDecoder::Level::Software, kUnits);
    d.setReset(true, 0);
    Sci sci{d.sci()};
    ASSERT_TRUE(d.dreq(10000));
    sci.write(Vs10xxDecoder::SCI_VOL, 0x2020);
    sci.write(Vs10xxDecoder::SCI_CLOCKF, 0x9B58);
    sci.write(Vs10xxDecoder::SCI_MODE, Vs10xxDecoder::SM_RESET);
    EXPECT_FALSE(d.dreq(10100)) << "6,000 clocks of reset";
    EXPECT_TRUE(d.dreq(10500));
    EXPECT_EQ(sci.read(Vs10xxDecoder::SCI_VOL), 0x2020);
    EXPECT_EQ(sci.read(Vs10xxDecoder::SCI_MODE) & Vs10xxDecoder::SM_RESET, 0);
}

TEST(Vs10xxDecoder, DreqFollowsTheInputFifo)
{
    Vs10xxDecoder d(Vs10xxDecoder::Chip::VS1001, Vs10xxDecoder::Level::Software, kUnits);
    d.setReset(true, 0);
    ASSERT_TRUE(d.dreq(10000));
    for (size_t i = 0; i < Vs10xxDecoder::INPUT_FIFO - Vs10xxDecoder::DREQ_FREE; i++)
        d.sdi()->exchange(0x00); // junk: never a frame, so it stays queued
    EXPECT_TRUE(d.dreq(10000)) << "exactly 32 bytes free";
    d.sdi()->exchange(0x00);
    EXPECT_FALSE(d.dreq(10000));
}

TEST(Vs10xxDecoder, DecodesExactlyLikeMinimp3)
{
    const auto mp3 = readFixture("eyeache1-44k-128k-cbr.mp3");
    ASSERT_GT(mp3.size(), 100000u);
    int rate = 0;
    const auto want = reference(mp3, rate);
    ASSERT_EQ(rate, 44100);

    Vs10xxDecoder d(Vs10xxDecoder::Chip::VS1001, Vs10xxDecoder::Level::Software, kUnits);
    d.setReset(true, 0);
    int64_t now = 10000;
    const auto got = play(d, mp3, now);
    ASSERT_EQ(got.size(), want.size()) << "every frame played";
    EXPECT_TRUE(got == want) << "sample-exact on the same build";
    EXPECT_EQ(d.streamRate(), 44100u);
    EXPECT_EQ(d.streamChannels(), 2);

    Sci sci{d.sci()};
    EXPECT_EQ(sci.read(Vs10xxDecoder::SCI_DECODE_TIME), 30) << "seconds of decoded audio";
    EXPECT_EQ(sci.read(Vs10xxDecoder::SCI_HDAT1) & 0xFFE0, 0xFFE0) << "frame sync in HDAT1";
    // VS1001 AUDATA: stereo, rate index 0 (44.1 kHz), 128 kbit/s
    EXPECT_EQ(sci.read(Vs10xxDecoder::SCI_AUDATA), 0x8000 | 128);
}

TEST(Vs10xxDecoder, Id3v2AndTrailingZerosAndMonoVbr)
{
    // The firmware ends every file with 2,048 zeros: the last frame must still play
    const auto mp3 = readFixture("eyeache1-22k-mono-vbr-id3.mp3");
    int rate = 0;
    const auto want = reference(mp3, rate);
    ASSERT_EQ(rate, 22050);

    Vs10xxDecoder d(Vs10xxDecoder::Chip::VS1011, Vs10xxDecoder::Level::Software, kUnits);
    d.setReset(true, 0);
    int64_t now = 10000;
    const auto got = play(d, mp3, now, 2048);
    EXPECT_EQ(got.size(), want.size());
    EXPECT_TRUE(got == want);
    Sci sci{d.sci()};
    EXPECT_EQ(sci.read(Vs10xxDecoder::SCI_AUDATA), 22050 & 0xFFFE) << "VS1011 AUDATA: rate/2 << 1, mono";
}

TEST(Vs10xxDecoder, LayerTwo)
{
    const auto mp2 = readFixture("eyeache1-44k-layer2.mp2");
    int rate = 0;
    const auto want = reference(mp2, rate);
    ASSERT_GT(want.size(), 0u);
    Vs10xxDecoder d(Vs10xxDecoder::Chip::VS1001, Vs10xxDecoder::Level::Software, kUnits);
    d.setReset(true, 0);
    int64_t now = 10000;
    const auto got = play(d, mp2, now);
    EXPECT_TRUE(got == want);
}

TEST(Vs10xxDecoder, PlaysAtTheStreamRateInEmulatedTime)
{
    // 100 ms of emulated time plays 4,410 samples of a 44.1 kHz stream
    const auto mp3 = readFixture("eyeache1-44k-128k-cbr.mp3");
    Vs10xxDecoder d(Vs10xxDecoder::Chip::VS1001, Vs10xxDecoder::Level::Software, kUnits);
    d.setReset(true, 0);
    int64_t now = 10000;
    size_t at = 0;
    while (d.dreq(now))
        d.sdi()->exchange(mp3[at++]);
    d.advance(now + 1);
    (void)d.drainPlayed();
    const uint64_t before = d.samplesPlayed();
    // keep it fed for 100 ms
    for (int step = 0; step < 400; step++)
    {
        now += 250;
        while (d.dreq(now) && at < mp3.size())
            d.sdi()->exchange(mp3[at++]);
    }
    EXPECT_NEAR(static_cast<double>(d.samplesPlayed() - before), 4410.0, 12.0);
}

TEST(Vs10xxDecoder, StubAcceptsDataButStaysSilent)
{
    Vs10xxDecoder d(Vs10xxDecoder::Chip::VS1011, Vs10xxDecoder::Level::Stub, kUnits);
    d.setReset(true, 0);
    const auto mp3 = readFixture("eyeache1-44k-128k-cbr.mp3");
    for (size_t i = 0; i < 20000; i++)
        d.sdi()->exchange(mp3[i]);
    EXPECT_TRUE(d.dreq(100000)) << "stub: DREQ always up";
    int16_t frame[882 * 2];
    EXPECT_FALSE(d.renderFrame(frame, 882, 1.0));
    Sci sci{d.sci()};
    EXPECT_EQ((sci.read(Vs10xxDecoder::SCI_STATUS) >> 4) & 7, 1) << "the chip still identifies itself";
}
