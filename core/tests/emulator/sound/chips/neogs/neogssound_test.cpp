// NeoGS sound block (neogs-tdd.md §3.6; FPGA sound_main.v, sound_mulacc.v)

#include <gtest/gtest.h>

#include "emulator/sound/chips/neogs/neogssound.h"

namespace
{
constexpr uint8_t FOUR = 0x00;
constexpr uint8_t EIGHT = NeoGSSound::CFG_8CHANS;
constexpr uint8_t PAN = NeoGSSound::CFG_PAN4CH;
constexpr uint8_t INV = NeoGSSound::CFG_INV7B;
} // namespace

TEST(NeoGSSound, CaptureDecodeFourAndEightChannels)
{
    NeoGSSound s;
    EXPECT_EQ(s.capture(0x6300, 0x11, FOUR), 3);
    EXPECT_EQ(s.capture(0x6700, 0x22, FOUR), 3) << "A10 is ignored in 4-channel mode";
    EXPECT_EQ(s.sample(3), 0x22);
    EXPECT_EQ(s.capture(0x6700, 0x33, EIGHT), 7);
    EXPECT_EQ(s.capture(0x7F00, 0x44, EIGHT), 7) << "only A10:A8 matter";
    EXPECT_EQ(s.sample(7), 0x44);
}

TEST(NeoGSSound, SampleSignConventions)
{
    EXPECT_EQ(NeoGSSound::signedSample(0x80, FOUR), 0);
    EXPECT_EQ(NeoGSSound::signedSample(0xFF, FOUR), 127);
    EXPECT_EQ(NeoGSSound::signedSample(0x00, FOUR), -128);
    EXPECT_EQ(NeoGSSound::signedSample(0x80, INV), -128) << "INV7B: two's complement";
    EXPECT_EQ(NeoGSSound::signedSample(0x7F, INV), 127);
    EXPECT_EQ(NeoGSSound::signedSample(0x00, INV), 0);
}

TEST(NeoGSSound, WorkedExampleFourChannels)
{
    // neogs-tdd.md §3.6: channel 1 = #FF at volume 63, channel 2 silent
    NeoGSSound s;
    s.powerOn();
    s.setSampleRaw(0, 0xFF);
    s.setVolume(0, 63);
    s.setVolume(1, 63);
    EXPECT_EQ(s.mix(false, FOUR), 16002);
    EXPECT_EQ(s.mix(true, FOUR), 0);
}

TEST(NeoGSSound, EightChannelMixUsesAllEight)
{
    NeoGSSound s;
    s.powerOn();
    for (int ch = 0; ch < 8; ch++)
    {
        s.setSampleRaw(ch, static_cast<uint8_t>(0x80 + ch + 1)); // +1..+8
        s.setVolume(ch, static_cast<uint8_t>(7 * (ch + 1)));
    }
    // L = c1v1 + c2v2 + c5v5 + c6v6, R = c3v3 + c4v4 + c7v7 + c8v8
    EXPECT_EQ(s.mix(false, EIGHT), 1 * 7 + 2 * 14 + 5 * 35 + 6 * 42);
    EXPECT_EQ(s.mix(true, EIGHT), 3 * 21 + 4 * 28 + 7 * 49 + 8 * 56);
    EXPECT_EQ(s.mix(false, FOUR), 2 * (1 * 7 + 2 * 14)) << "4-channel mode ignores 5-8 but doubles";
}

TEST(NeoGSSound, Pan4chPansFourSamplesWithEightVolumes)
{
    NeoGSSound s;
    s.powerOn();
    for (int ch = 0; ch < 4; ch++)
        s.setSampleRaw(ch, static_cast<uint8_t>(0x80 + ch + 1));
    for (int ch = 0; ch < 8; ch++)
        s.setVolume(ch, static_cast<uint8_t>(ch + 1));
    // L = c1v1 + c2v2 + c3v5 + c4v6, R = c1v3 + c2v4 + c3v7 + c4v8
    EXPECT_EQ(s.mix(false, PAN), 1 * 1 + 2 * 2 + 3 * 5 + 4 * 6);
    EXPECT_EQ(s.mix(true, PAN), 1 * 3 + 2 * 4 + 3 * 7 + 4 * 8);
    EXPECT_EQ(s.mix(false, PAN | EIGHT), s.mix(false, EIGHT)) << "8CHANS wins over PAN4CH";
}

TEST(NeoGSSound, VolumesUseSixBitsAndFullScaleFits16Bits)
{
    NeoGSSound s;
    s.powerOn();
    s.setVolume(0, 0xFF);
    EXPECT_EQ(s.volume(0), 63);
    for (int ch = 0; ch < 8; ch++)
    {
        s.setSampleRaw(ch, 0x00);
        s.setVolume(ch, 63);
    }
    EXPECT_EQ(s.mix(false, EIGHT), -4 * 128 * 63);
    EXPECT_GE(s.mix(false, FOUR), -32768);
}
