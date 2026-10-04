// MultiSoundDacs (core/src/emulator/slots/cards/multisound/multisounddacs): the measured transfer (tdd-card-logic.md
// §7 F9), the channel routing (hardware-reference.md §4.5), the ordering of strobes by their end (F7, rule L14), the
// Authentic RC and the TTD blob.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "emulator/slots/cards/multisound/multisounddacs.h"

namespace
{
constexpr uint32_t kHostRate = 3500000;
constexpr uint32_t kOutputRate = 44100;
constexpr uint64_t kFrame = 70000;                  // 20 ms of the 3.5 MHz axis
constexpr size_t kFrameSamples = 882;               // 20 ms at 44.1 kHz

class Bench
{
public:
    explicit Bench(MultiSoundRenderMode mode = MultiSoundRenderMode::HiFi)
    {
        MultiSoundDacsConfig cfg;
        cfg.hostTickRate = kHostRate;
        cfg.outputRate = kOutputRate;
        cfg.renderMode = mode;
        dacs.Configure(cfg);
    }

    /// Renders one frame ending at frameIndex x kFrame
    std::vector<int16_t> Frame(uint64_t frameIndex)
    {
        std::vector<int16_t> out(kFrameSamples * 2);
        dacs.EndFrame(frameIndex * kFrame, out.data(), kFrameSamples);
        return out;
    }

    MultiSoundDacs dacs;
};

int16_t Left(const std::vector<int16_t>& s, size_t i) { return s[i * 2]; }
int16_t Right(const std::vector<int16_t>& s, size_t i) { return s[i * 2 + 1]; }
} // namespace

TEST(MultiSoundDacs_Test, TransferPoints)
{
    // level x gain: level -127..+127 with #7F and #80 both 0; gain = volume, 63 counts as 64
    auto out = [](uint8_t busByte, uint8_t volume)
    {
        return MultiSoundDacs::Transfer(MultiSoundDacState{ MultiSoundLogic::ConvertSample(busByte), volume });
    };
    EXPECT_EQ(out(0x7F, 63), 0);
    EXPECT_EQ(out(0x80, 63), 0);
    EXPECT_EQ(out(0xFF, 63), 127 * 64);
    EXPECT_EQ(out(0x00, 63), -127 * 64);
    EXPECT_EQ(out(0x81, 63), 1 * 64);
    EXPECT_EQ(out(0x7E, 63), -1 * 64);
    EXPECT_EQ(out(0xFF, 62), 127 * 62);
    EXPECT_EQ(out(0xFF, 1), 127);
    EXPECT_EQ(out(0xFF, 0), 0);
    EXPECT_EQ(out(0x00, 0), 0);
    // Full scale (128 x 64) is never reached: the RTL's mean is 0.5 + 0.5 x 127/128 at most
    EXPECT_LT(out(0xFF, 63), MultiSoundDacs::kChannelFullScale);
}

TEST(MultiSoundDacs_Test, VolumeSixtyThreeCountsAsSixtyFour)
{
    Bench b;
    b.dacs.GsSample(100, 0, 0xFF);
    b.dacs.GsVolume(200, 0, 62);
    b.dacs.Run(300);
    EXPECT_EQ(b.dacs.OutputLeft(), 127 * 62);
    b.dacs.GsVolume(400, 0, 63);
    b.dacs.Run(500);
    EXPECT_EQ(b.dacs.OutputLeft(), 127 * 64);
    // The GS volume port keeps six bits
    b.dacs.GsVolume(600, 0, 0xC0 | 31);
    b.dacs.Run(700);
    EXPECT_EQ(b.dacs.Channel(0).volume, 31);
}

TEST(MultiSoundDacs_Test, ChannelRouting)
{
    // Channels 0 and 1 left, 2 and 3 right, no cross-feed; each one alone, then all
    const int32_t full = 127 * 64;
    for (int channel = 0; channel < 4; channel++)
    {
        Bench b;
        b.dacs.SoundriveWrite(1000, channel, 0xFF);
        const std::vector<int16_t> s = b.Frame(1);
        const bool left = channel < 2;
        EXPECT_EQ(Left(s, kFrameSamples - 1), left ? full : 0) << channel;
        EXPECT_EQ(Right(s, kFrameSamples - 1), left ? 0 : full) << channel;
    }

    Bench b;
    b.dacs.SoundriveWrite(1000, 0, 0xFF);
    b.dacs.SoundriveWrite(1000, 1, 0xFF);
    b.dacs.SoundriveWrite(1000, 2, 0x00);
    b.dacs.SoundriveWrite(1000, 3, 0x80);
    const std::vector<int16_t> s = b.Frame(1);
    EXPECT_EQ(Left(s, kFrameSamples - 1), 2 * full);
    EXPECT_EQ(Right(s, kFrameSamples - 1), -full);
}

TEST(MultiSoundDacs_Test, StepLandsAtItsTime)
{
    // A step at 35000 ticks (10 ms) crosses half its height at sample 441 plus blip_buf's fixed kernel delay of
    // 8 samples (half its 16-sample step kernel; every blip_buf user has it)
    Bench b;
    b.dacs.SoundriveWrite(35000, 0, 0xFF);
    const std::vector<int16_t> s = b.Frame(1);
    size_t crossing = 0;
    while (crossing < kFrameSamples && Left(s, crossing) < 127 * 32)
        crossing++;
    EXPECT_NEAR(static_cast<double>(crossing), 441.0 + 8.0, 1.5);
    EXPECT_EQ(Left(s, 400), 0);
}

TEST(MultiSoundDacs_Test, LaterStrobeEndWinsWhateverTheCallOrder)
{
    // The GS timeline hands over its events after the host's: a SounDrive write ending at 2000 and a GS sample
    // ending at 1000 arrive in that order; the SounDrive value is what the channel holds afterwards
    Bench b;
    b.dacs.GsVolume(500, 0, 63);
    b.dacs.SoundriveWrite(2000, 0, 0xFF);
    b.dacs.GsSample(1000, 0, 0x00);
    EXPECT_EQ(b.dacs.PendingEvents(), 3u);

    b.dacs.Run(1500);
    EXPECT_EQ(b.dacs.Channel(0).sample, MultiSoundLogic::ConvertSample(0x00));
    EXPECT_EQ(b.dacs.OutputLeft(), -127 * 64);
    b.dacs.Run(2500);
    EXPECT_EQ(b.dacs.Channel(0).sample, MultiSoundLogic::ConvertSample(0xFF));
    EXPECT_EQ(b.dacs.OutputLeft(), 127 * 64);
    EXPECT_EQ(b.dacs.PendingEvents(), 0u);
    EXPECT_EQ(b.dacs.LateEvents(), 0u);
}

TEST(MultiSoundDacs_Test, SameEdgePriority)
{
    // Equal strobe end: GS sample over SounDrive sample, SounDrive volume over GS volume, in every submission order
    const MultiSoundDacs::Event events[3] = {
        { 1000, MultiSoundDacStrobe::SoundriveWrite, 1, 0xFF },
        { 1000, MultiSoundDacStrobe::GsSample, 1, 0x40 },
        { 1000, MultiSoundDacStrobe::GsVolume, 1, 10 },
    };
    const int orders[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } };
    for (const auto& order : orders)
    {
        Bench b;
        for (int i : order)
            b.dacs.Submit(events[i]);
        b.dacs.Run(1000);
        EXPECT_EQ(b.dacs.Channel(1).sample, MultiSoundLogic::ConvertSample(0x40)) << order[0] << order[1] << order[2];
        EXPECT_EQ(b.dacs.Channel(1).volume, 63) << order[0] << order[1] << order[2];
    }
}

TEST(MultiSoundDacs_Test, LateEventAppliesAtTheCurrentTime)
{
    Bench b;
    b.dacs.Run(5000);
    b.dacs.GsVolume(100, 2, 63);
    b.dacs.GsSample(100, 2, 0xC0);
    EXPECT_EQ(b.dacs.LateEvents(), 2u);
    EXPECT_EQ(b.dacs.PendingEvents(), 0u);
    EXPECT_EQ(b.dacs.OutputRight(), 64 * 64);
}

TEST(MultiSoundDacs_Test, FullQueueRunsToItsEarliestEvent)
{
    Bench b;
    for (size_t i = 0; i < MultiSoundDacs::kMaxPendingEvents; i++)
        b.dacs.GsSample(1000 + i, 0, static_cast<uint8_t>(i));
    EXPECT_EQ(b.dacs.PendingEvents(), MultiSoundDacs::kMaxPendingEvents);
    b.dacs.GsSample(5000, 0, 0xAA);
    EXPECT_EQ(b.dacs.PendingEvents(), MultiSoundDacs::kMaxPendingEvents);
    EXPECT_EQ(b.dacs.Channel(0).sample, MultiSoundLogic::ConvertSample(0x00));
    EXPECT_EQ(b.dacs.Time(), 1000u);
    b.dacs.Run(6000);
    EXPECT_EQ(b.dacs.Channel(0).sample, MultiSoundLogic::ConvertSample(0xAA));
}

TEST(MultiSoundDacs_Test, ResetClearsChannelsAndQueue)
{
    Bench b;
    b.dacs.SoundriveWrite(100, 3, 0xFF);
    b.dacs.GsSample(9000, 3, 0x00);
    b.dacs.Run(200);
    b.dacs.Reset(300);
    EXPECT_EQ(b.dacs.PendingEvents(), 0u);
    EXPECT_EQ(b.dacs.Channel(3).sample, 0);
    EXPECT_EQ(b.dacs.Channel(3).volume, 0);
    EXPECT_EQ(b.dacs.OutputRight(), 0);
}

TEST(MultiSoundDacs_Test, AuthenticIsTheBoardRcOnTheHiFiOutput)
{
    // The same square wave (SounDrive writes at about 6.3 kHz) in both modes; the HiFi output through the board's
    // 1-pole RC equals the Authentic output to the rounding
    Bench hifi(MultiSoundRenderMode::HiFi);
    Bench authentic(MultiSoundRenderMode::Authentic);
    for (uint64_t t = 100, i = 0; t < 2 * kFrame; t += 277, i++)
    {
        hifi.dacs.SoundriveWrite(t, 0, (i & 1) ? 0xFF : 0x00);
        authentic.dacs.SoundriveWrite(t, 0, (i & 1) ? 0xFF : 0x00);
    }
    MultiSoundRcFilter rc = MultiSoundRcFilter::LowPass1(MultiSoundBoard::DacCornerHz(), kOutputRate);
    double hifiEnergy = 0.0;
    double authenticEnergy = 0.0;
    for (uint64_t frame = 1; frame <= 2; frame++)
    {
        const std::vector<int16_t> h = hifi.Frame(frame);
        const std::vector<int16_t> a = authentic.Frame(frame);
        for (size_t i = 0; i < kFrameSamples; i++)
        {
            const double expected = rc.Process(Left(h, i));
            ASSERT_NEAR(Left(a, i), expected, 1.0) << frame << ":" << i;
            hifiEnergy += static_cast<double>(Left(h, i)) * Left(h, i);
            authenticEnergy += static_cast<double>(Left(a, i)) * Left(a, i);
        }
    }
    // The square wave's harmonics above the corner are cut: less energy, not none
    EXPECT_LT(authenticEnergy, hifiEnergy);
    EXPECT_GT(authenticEnergy, 0.5 * hifiEnergy);
}

TEST(MultiSoundDacs_Test, TtdRoundTrip)
{
    Bench a;
    a.dacs.SoundriveWrite(100, 0, 0xFF);
    a.dacs.GsVolume(200, 1, 33);
    a.dacs.GsSample(300, 1, 0x10);
    a.dacs.SoundriveWrite(400, 2, 0x22);
    a.dacs.GsVolume(500, 3, 7);
    a.dacs.GsSample(600, 3, 0xF0);
    a.Frame(1);
    // Events past the frame wait in the queue and must survive
    a.dacs.GsSample(kFrame + 5000, 0, 0x01);
    a.dacs.SoundriveWrite(kFrame + 3000, 3, 0x99);

    std::vector<uint8_t> blob(a.dacs.TTDStateSize());
    a.dacs.TTDSaveState(blob.data());

    Bench b;
    b.dacs.TTDLoadState(blob.data());
    for (int c = 0; c < 4; c++)
    {
        EXPECT_EQ(b.dacs.Channel(c).sample, a.dacs.Channel(c).sample) << c;
        EXPECT_EQ(b.dacs.Channel(c).volume, a.dacs.Channel(c).volume) << c;
    }
    EXPECT_EQ(b.dacs.PendingEvents(), 2u);
    EXPECT_EQ(b.dacs.Time(), a.dacs.Time());
    EXPECT_EQ(b.dacs.TTDHashState(), a.dacs.TTDHashState());

    // Restored into itself too (the load restarts the render layer the same way): both continue identically
    a.dacs.TTDLoadState(blob.data());
    a.dacs.GsSample(kFrame + 9000, 2, 0x7F);
    b.dacs.GsSample(kFrame + 9000, 2, 0x7F);
    EXPECT_EQ(a.Frame(2), b.Frame(2));
    EXPECT_EQ(b.dacs.Channel(0).sample, MultiSoundLogic::ConvertSample(0x01));
    EXPECT_EQ(b.dacs.Channel(3).volume, 63);

    // A blob of another layout version is refused
    std::vector<uint8_t> foreign = blob;
    foreign[0] = MultiSoundDacs::kStateVersion + 1;
    Bench c;
    c.dacs.TTDLoadState(foreign.data());
    EXPECT_EQ(c.dacs.PendingEvents(), 0u);
    EXPECT_EQ(c.dacs.Channel(0).volume, 0);
}
