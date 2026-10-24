/// @file hapticfilters_test.cpp
/// @brief H2 / H3 (haptics.md §4, §5, §12): the DSP filters on the 8 kHz sample streams (`envelope`, `lowband`,
/// `onset`) in integer arithmetic, the breakpoint probes' parsing and evaluation (`exec`, `read`, `port-in`,
/// `port-out`, `write` with `each_write`), and the replay hold of a feed.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "haptics/audiofilters.h"
#include "haptics/hapticmixer.h"
#include "haptics/hapticprofile.h"
#include "haptics/profilehaptics.h"
#include "haptics/soundhaptics.h"
#include "profiles/json.h"

using namespace controllers;

namespace
{
constexpr uint32_t kRate = HapticStreamFrame::kRate;
constexpr size_t kFrame = kRate / 50;  // the samples of one 50 Hz frame

/// A square wave of @p hz at @p amplitude, @p frames frames of it from sample @p start
std::vector<int16_t> Square(double hz, int16_t amplitude, size_t samples, size_t start = 0)
{
    std::vector<int16_t> out(samples);
    const double half = kRate / hz / 2.0;
    for (size_t i = 0; i < samples; i++)
        out[i] = (static_cast<size_t>(static_cast<double>(start + i) / half) % 2) ? amplitude : static_cast<int16_t>(-amplitude);
    return out;
}

/// The peak a filter reports over one second of @p signal, frame by frame
template <typename Filter>
float PeakOver(Filter& filter, const std::vector<int16_t>& signal)
{
    float peak = 0;
    for (size_t at = 0; at + kFrame <= signal.size(); at += kFrame)
        peak = std::max(peak, filter.Process(signal.data() + at, kFrame));
    return peak;
}

HapticProfile Parse(const char* text)
{
    JsonValue section;
    std::string error;
    EXPECT_TRUE(ParseJson(text, section, error)) << error;
    HapticProfile haptics;
    EXPECT_TRUE(ParseHapticProfile(section, haptics, error)) << error;
    return haptics;
}

HapticMixer MakeMixer()
{
    return HapticMixer([](const HostDeviceId&, const RumbleLevels&) { return true; }, false);
}
}  // namespace

TEST(HapticFilters_Test, Haptics_LowbandFilter)
{
    LowbandFilter band;
    band.Configure(20.0, 150.0, 5.0, 80.0, kRate);
    const float low = PeakOver(band, Square(60.0, 16000, kRate));
    band.Reset();
    const float high = PeakOver(band, Square(2000.0, 16000, kRate));
    EXPECT_GT(low, 0.3f) << "a 60 Hz burst passes";
    EXPECT_LT(high, 0.02f) << "a 2 kHz tone does not";
    band.Reset();
    const std::vector<int16_t> dc(kRate, 12000);
    EXPECT_GT(PeakOver(band, dc), 0.1f) << "the step into DC is a thump";
    EXPECT_LT(PeakOver(band, dc), 0.02f) << "steady DC is below the band";

    // Bit-exact on every compiler: integers from the input on, so the level of a fixed signal is a fixed number
    band.Reset();
    const std::vector<int16_t> signal = Square(60.0, 16000, kFrame * 5);
    int32_t sum = 0;
    for (size_t at = 0; at < signal.size(); at += kFrame)
        sum += static_cast<int32_t>(band.Process(signal.data() + at, kFrame) * 32767.0f + 0.5f);
    EXPECT_EQ(sum, 67517) << "golden: the sum of five frame levels (integer DSP: every compiler)";
}

TEST(HapticFilters_Test, EnvelopeAttackAndRelease)
{
    EnvelopeFilter envelope;
    envelope.Configure(5.0, 80.0, kRate);
    const std::vector<int16_t> tone = Square(1000.0, 16384, kFrame);
    EXPECT_NEAR(EnvelopeLevel(envelope, tone.data(), tone.size()), 0.5f, 0.02f) << "a half-scale tone, past the attack";
    const std::vector<int16_t> silence(kFrame, 0);
    const float after = EnvelopeLevel(envelope, silence.data(), silence.size());
    envelope.Step(0);
    EXPECT_GT(after, 0.4f) << "the frame's peak is its start";
    EXPECT_LT(envelope.Level(), 16384 * 0.85) << "and it falls with the 80 ms release (20 ms later: e^-1/4)";
    EXPECT_GT(envelope.Level(), 16384 * 0.7);
}

TEST(HapticFilters_Test, OnsetFiresOnARiseOnly)
{
    OnsetFilter onset;
    onset.Configure(12.0, 30.0, 0.05, 50.0, kRate);
    const std::vector<int16_t> silence(kFrame, 0);
    const std::vector<int16_t> loud = Square(400.0, 20000, kFrame);
    EXPECT_FALSE(onset.Process(silence.data(), kFrame));
    EXPECT_TRUE(onset.Process(loud.data(), kFrame)) << "silence to a loud tone: an onset";
    int fired = 0;
    for (int frame = 0; frame < 10; frame++)
        fired += onset.Process(loud.data(), kFrame) ? 1 : 0;
    EXPECT_EQ(fired, 0) << "a held tone is no onset";

    onset.Reset();
    const std::vector<int16_t> quiet = Square(400.0, 1000, kFrame);
    onset.Process(silence.data(), kFrame);
    EXPECT_FALSE(onset.Process(quiet.data(), kFrame)) << "below the floor";
    EXPECT_EQ(OnsetFilter::RatioQ16(12.0), 260904) << "12 dB from the table";
    EXPECT_EQ(OnsetFilter::RatioQ16(0.0), 65536);
}

TEST(HapticFilters_Test, ProbesParse)
{
    const HapticProfile haptics = Parse(R"({
      "sources": {
        "shot":  { "probe": "exec", "address": "page:2:0xC010" },
        "fire":  { "probe": "port-in", "port": "0x1F", "port_mask": "0xFF" },
        "red":   { "probe": "port-out", "port": "0xFE", "port_mask": "0x01", "value": 2, "mask": 7 },
        "hit":   { "probe": "write", "address": "0x5F20", "when": "decreased", "each_write": true },
        "bass":  { "filter": "lowband", "source": "ay:1", "high_hz": 120 },
        "bang":  { "filter": "onset", "source": "next-dac", "rise_db": 9, "within_ms": 20 },
        "hum":   { "filter": "envelope", "source": "covox", "scale": 2 }
      },
      "rules": [ { "on": "shot", "play": "shot" } ]
    })");
    ASSERT_EQ(haptics.sources.size(), 7u);
    std::vector<HapticSource> by(7);
    for (const HapticSource& source : haptics.sources)
    {
        if (source.name == "shot")
            by[0] = source;
        else if (source.name == "fire")
            by[1] = source;
        else if (source.name == "red")
            by[2] = source;
        else if (source.name == "hit")
            by[3] = source;
        else if (source.name == "bass")
            by[4] = source;
        else if (source.name == "bang")
            by[5] = source;
        else
            by[6] = source;
    }
    EXPECT_EQ(by[0].kind, HapticSourceKind::Exec);
    EXPECT_EQ(by[0].Probe().page, 2);
    EXPECT_EQ(by[0].Probe().address, 0x0010);
    EXPECT_EQ(by[1].Probe().kind, HapticProbeKind::PortIn);
    EXPECT_EQ(by[1].Probe().portMask, 0xFF);
    EXPECT_EQ(by[2].Probe().kind, HapticProbeKind::PortOut);
    EXPECT_TRUE(by[2].hasValue);
    EXPECT_EQ(by[2].mask, 7);
    EXPECT_TRUE(by[3].IsBreakpointProbe()) << "each_write: a breakpoint, not a frame-end watch";
    EXPECT_FALSE(by[3].IsProbe());
    EXPECT_EQ(by[4].stream, HapticStream::Ay1);
    EXPECT_FLOAT_EQ(by[4].highHz, 120.0f);
    EXPECT_EQ(by[5].stream, HapticStream::NextDac);
    EXPECT_TRUE(by[5].IsEvent());
    EXPECT_EQ(by[6].stream, HapticStream::Covox);

    struct Bad
    {
        const char* section;
        const char* reason;
    };
    const Bad bad[] = {
        {R"({"sources": {"s": {"probe": "exec"}}})", "'address' is required"},
        {R"({"sources": {"s": {"probe": "port-out"}}})", "'port' is required"},
        {R"({"sources": {"s": {"probe": "port-in", "port": 254, "value": 1}}})", "port-out only"},
        {R"({"sources": {"s": {"probe": "port-in", "port": 65536}}})", "above #FFFF"},
        {R"({"sources": {"s": {"probe": "value", "address": 1, "each_write": true}}})", "each_write"},
        {R"({"sources": {"s": {"filter": "lowband", "source": "saa"}}})", "'source' 'saa' unknown"},
        {R"({"sources": {"s": {"filter": "lowband", "low_hz": 200, "high_hz": 100}}})", "below 'high_hz'"},
        {R"({"sources": {"s": {"filter": "onset", "rise_db": 90}}})", "'rise_db' is 1..60"},
        {R"({"sources": {"s": {"filter": "ringing"}}})", "unknown"},
    };
    for (const Bad& entry : bad)
    {
        JsonValue section;
        std::string error;
        ASSERT_TRUE(ParseJson(entry.section, section, error)) << error;
        HapticProfile parsed;
        EXPECT_FALSE(ParseHapticProfile(section, parsed, error)) << entry.section;
        EXPECT_NE(error.find(entry.reason), std::string::npos) << error;
    }
}

TEST(HapticFilters_Test, ProbeEventsPlayTheirRules)
{
    HapticMixer mixer = MakeMixer();
    ProfileHaptics feed(Parse(R"({
      "sources": {
        "shot": { "probe": "exec", "address": "0x8002" },
        "red":  { "probe": "port-out", "port": "0xFE", "port_mask": "0x01", "value": 2, "mask": 7 },
        "hit":  { "probe": "write", "address": "0x9000", "when": "decreased", "each_write": true },
        "bass": { "filter": "lowband", "source": "mix" }
      },
      "rules": [
        { "on": "shot", "play": "shot", "target": "handle.high" },
        { "on": "red",  "play": "hit", "target": "trigger.left" },
        { "on": "hit",  "play": "life-lost", "target": "trigger.right" },
        { "on": "bass", "play": "engine", "target": "handle.low" }
      ]
    })"), "game/test", mixer, [] { return std::vector<HostDeviceId>{"pad"}; });
    const std::vector<HapticProbe> probes = feed.Probes();
    ASSERT_EQ(probes.size(), 3u) << "exec, port-out, each_write";
    EXPECT_EQ(probes[0].kind, HapticProbeKind::Exec);
    EXPECT_EQ(probes[1].kind, HapticProbeKind::PortOut);
    EXPECT_EQ(probes[2].kind, HapticProbeKind::Write);
    EXPECT_TRUE(feed.Watches().empty()) << "no frame-end watch for an each_write probe";
    ASSERT_EQ(feed.Streams().size(), 1u);
    EXPECT_EQ(feed.Streams()[0], HapticStream::Mix);

    auto level = [&](const char* name) {
        for (const auto& [source, value] : feed.SourceLevels())
            if (source == name)
                return value;
        return -1.0f;
    };

    HapticFrame frame;
    frame.probeCount = 3;
    frame.streamCount = 1;
    frame.streams[0].count = kFrame;
    feed.OnHapticFrame(frame);
    EXPECT_EQ(mixer.Written("pad"), RumbleLevels{}) << "nothing hit, silence";

    frame.probeHits = {1, 2, 2};
    frame.probeEventCount = 4;
    frame.probeEvents[0] = {1, 0, 0x05};  // OUT #FE, 5: cyan, not red
    frame.probeEvents[1] = {2, 5, 4};     // the write that takes a life...
    frame.probeEvents[2] = {2, 4, 5};     // ...undone in the same frame: the frame end sees no change
    frame.probeEvents[3] = {1, 0, 0x0A};  // OUT #FE, #0A: & 7 = 2, red
    const std::vector<int16_t> bass = Square(60.0, 20000, kFrame);
    std::copy(bass.begin(), bass.end(), frame.streams[0].samples.begin());
    feed.OnHapticFrame(frame);
    EXPECT_EQ(level("shot"), 1.0f);
    EXPECT_EQ(level("red"), 1.0f) << "the second OUT matched value & mask";
    EXPECT_EQ(level("hit"), 1.0f) << "a decrease undone within one frame";
    EXPECT_GT(level("bass"), 0.2f);
    mixer.Tick(HapticMixer::Clock::now() + std::chrono::milliseconds(10));  // past the effects' attacks
    const RumbleLevels written = mixer.Written("pad");
    EXPECT_GT(written.high, 0.0f);
    EXPECT_GT(written.leftTrigger, 0.0f);
    EXPECT_GT(written.rightTrigger, 0.0f);
    EXPECT_GT(written.low, 0.0f);

    // The replay hold: the game's effects stop at once and the frames are ignored until it ends
    feed.OnHapticHold(true);
    EXPECT_EQ(mixer.Written("pad"), RumbleLevels{}) << "a seek stops the motors in the same call";
    feed.OnHapticFrame(frame);
    EXPECT_EQ(mixer.Written("pad"), RumbleLevels{}) << "held: frames do not play";
    feed.OnHapticHold(false);
    feed.OnHapticFrame(frame);
    mixer.Tick(HapticMixer::Clock::now() + std::chrono::milliseconds(10));
    EXPECT_GT(mixer.Written("pad").high, 0.0f) << "live again";

    frame.probeEventCount = 1;
    frame.probeEvents[0] = {1, 0, 0x05};
    frame.probeHits = {0, 1, 0};
    mixer.StopAll();
    feed.OnHapticFrame(frame);
    EXPECT_EQ(level("red"), 0.0f) << "an OUT of another colour";
}

TEST(HapticFilters_Test, SoundHapticsDspDefaults)
{
    HapticMixer mixer = MakeMixer();
    SoundHaptics plain(mixer, [] { return std::vector<HostDeviceId>{"pad"}; });
    EXPECT_TRUE(plain.Streams().empty()) << "the DSP defaults are off for players";
    SoundHaptics dsp(mixer, [] { return std::vector<HostDeviceId>{"pad"}; }, true);
    ASSERT_EQ(dsp.Streams().size(), 1u);

    HapticFrame frame;
    frame.streamCount = 1;
    frame.streams[0].count = kFrame;
    dsp.OnHapticFrame(frame);
    EXPECT_EQ(mixer.Written("pad"), RumbleLevels{});
    const std::vector<int16_t> bass = Square(60.0, 20000, kFrame);
    std::copy(bass.begin(), bass.end(), frame.streams[0].samples.begin());
    dsp.OnHapticFrame(frame);
    EXPECT_GT(dsp.Last().low, 0.3f) << "lowband(mix) on the low motor";
    EXPECT_GT(mixer.Written("pad").high, 0.0f) << "onset(mix): the tick on the high motor";
    dsp.OnHapticHold(true);
    EXPECT_EQ(mixer.Written("pad"), RumbleLevels{});
}

TEST(HapticFilters_Test, AddProbeFromDebugger)
{
    JsonValue haptics;  // a profile without haptics
    std::string name, error;
    HapticProbe write;
    write.kind = HapticProbeKind::Write;
    write.address = write.end = 0x5C3A;
    write.page = 5;
    ASSERT_TRUE(AddHapticProbeSource(haptics, write, "hit", name, error)) << error;
    EXPECT_EQ(name, "probe-1");
    HapticProbe out;
    out.kind = HapticProbeKind::PortOut;
    out.address = out.end = 0x00FE;
    out.portMask = 0x00FF;
    ASSERT_TRUE(AddHapticProbeSource(haptics, out, "tick", name, error)) << error;
    EXPECT_EQ(name, "probe-2");

    HapticProfile parsed;
    ASSERT_TRUE(ParseHapticProfile(haptics, parsed, error)) << error;
    ASSERT_EQ(parsed.sources.size(), 2u);
    ASSERT_EQ(parsed.rules.size(), 2u);
    const HapticSource& first = parsed.sources[0].name == "probe-1" ? parsed.sources[0] : parsed.sources[1];
    EXPECT_TRUE(first.eachWrite);
    EXPECT_EQ(first.page, 5);
    EXPECT_EQ(first.address, 0x1C3A) << "the offset in the page";
    EXPECT_EQ(parsed.rules[1].on, "probe-2");
    EXPECT_FALSE(AddHapticProbeSource(haptics, out, "boing", name, error)) << "an unknown effect is refused";
    EXPECT_EQ(haptics.Find("rules")->Items().size(), 2u) << "and nothing changed";
}
