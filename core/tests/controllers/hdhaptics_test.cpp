/// @file hdhaptics_test.cpp
/// @brief H4 (haptics.md §6, §9): effect envelopes and priorities in the mixer, the target fallbacks of a rule
/// (Haptics_Fallback of §12), and the DualSense output report.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "haptics/hapticmixer.h"
#include "_helpers/fixedmixerclock.h"
#include "haptics/hapticprofile.h"
#include "haptics/profilehaptics.h"
#include "haptics/testeffects.h"
#include "hid/drivers/dualsenseoutput.h"

using namespace controllers;
using namespace std::chrono_literals;

TEST(HdHaptics_Test, EnvelopesAndPriorities)
{
    HapticEffect adsr({1.0f, 0.0f, 0.0f, 0.0f}, 100);
    adsr.shape = HapticShape::Adsr;
    adsr.attackMs = 10;
    adsr.decayMs = 20;
    adsr.sustain = 0.5f;
    adsr.releaseMs = 40;
    EXPECT_FLOAT_EQ(adsr.Envelope(5), 0.5f) << "half the attack";
    EXPECT_FLOAT_EQ(adsr.Envelope(10), 1.0f) << "the peak";
    EXPECT_FLOAT_EQ(adsr.Envelope(20), 0.75f) << "half the decay";
    EXPECT_FLOAT_EQ(adsr.Envelope(50), 0.5f) << "the sustain";
    EXPECT_FLOAT_EQ(adsr.Envelope(80), 0.25f) << "half the release";
    EXPECT_FLOAT_EQ(adsr.Envelope(100), 0.0f) << "over";

    HapticEffect pattern({1.0f, 0.0f, 0.0f, 0.0f}, 100);
    pattern.shape = HapticShape::Pattern;
    pattern.pattern = {{10, 1.0f}, {10, 0.2f}};
    EXPECT_FLOAT_EQ(pattern.Envelope(5), 1.0f);
    EXPECT_FLOAT_EQ(pattern.Envelope(15), 0.2f);
    EXPECT_FLOAT_EQ(pattern.Envelope(25), 1.0f) << "the pattern repeats";

    HapticMixer mixer([](const HostDeviceId&, const RumbleLevels&) { return true; }, false, FixedMixerClock());
    HapticEffect low({0.8f, 0.0f, 0.0f, 0.0f}, 60000);
    HapticEffect high({0.0f, 0.4f, 0.0f, 0.0f}, 60000);
    high.priority = 2;
    mixer.Play("pad", low);
    EXPECT_FLOAT_EQ(mixer.Written("pad").low, 0.8f);
    mixer.Play("pad", high);
    EXPECT_FLOAT_EQ(mixer.Written("pad").low, 0.8f * HapticMixer::kDuck) << "ducked under the higher priority";
    EXPECT_FLOAT_EQ(mixer.Written("pad").high, 0.4f);
}

TEST(HdHaptics_Test, Haptics_Fallback)
{
    JsonValue section;
    std::string error;
    ASSERT_TRUE(ParseJson(R"({"sources": {"hit": {"probe": "write", "address": 16384}},
                              "rules": [{"on": "hit", "play": "life-lost", "target": "trigger.right|handle.high"}]})",
                          section, error))
        << error;
    HapticProfile profile;
    ASSERT_TRUE(ParseHapticProfile(section, profile, error)) << error;
    ASSERT_EQ(profile.rules[0].fallbacks, std::vector<uint8_t>{kHandleHigh});
    EXPECT_EQ(profile.rules[0].TargetFor(kTargetAll), kTriggerRight);
    EXPECT_EQ(profile.rules[0].TargetFor(kHandleLow | kHandleHigh), kHandleHigh) << "no trigger motors";

    HapticMixer mixer([](const HostDeviceId&, const RumbleLevels&) { return true; }, false, FixedMixerClock());
    ProfileHaptics feed(
        profile, "fallback", mixer, [] { return std::vector<HostDeviceId>{"xinput", "elite"}; },
        [](const HostDeviceId& device) {
            return device == "elite" ? static_cast<uint8_t>(kTargetAll) : static_cast<uint8_t>(kHandleLow | kHandleHigh);
        });
    HapticFrame frame;
    frame.watchCount = 1;
    frame.watched[0] = 1;
    feed.OnHapticFrame(frame);
    frame.watched[0] = 2;
    feed.OnHapticFrame(frame);
    EXPECT_EQ(mixer.Written("xinput"), (RumbleLevels{0.0f, 1.0f, 0.0f, 0.0f, 0, 160}))
        << "handle.high without triggers, at life-lost's high-band frequency";
    EXPECT_EQ(mixer.Written("elite"), (RumbleLevels{0.0f, 0.0f, 0.0f, 1.0f})) << "the right trigger where it is";
}

TEST(HdHaptics_Test, DualSenseOutputReport)
{
    EXPECT_TRUE(DualSenseOutput::Handles(0x054C, 0x0CE6));
    EXPECT_TRUE(DualSenseOutput::Handles(0x054C, 0x0DF2)) << "DualSense Edge";
    EXPECT_FALSE(DualSenseOutput::Handles(0x054C, 0x09CC)) << "DualShock 4";

    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(DualSenseOutput::Crc32(check, sizeof(check)), 0xCBF43926u) << "the CRC-32 check value";

    const RumbleLevels levels{1.0f, 0.5f, 0.0f, 128.0f / 255.0f};
    const std::vector<uint8_t> usb = DualSenseOutput::UsbReport(levels);
    ASSERT_EQ(usb.size(), 48u);
    EXPECT_EQ(usb[0], 0x02);
    EXPECT_EQ(usb[1], 0x0F) << "vibration, haptics select, both triggers";
    EXPECT_EQ(usb[3], 128) << "high -> the right motor";
    EXPECT_EQ(usb[4], 255) << "low -> the left motor";
    EXPECT_EQ(usb[11], 0x01) << "right trigger: continuous resistance";
    EXPECT_EQ(usb[13], 128) << "its force";
    EXPECT_EQ(usb[22], 0x00) << "left trigger off";
    EXPECT_EQ(usb[39], 0x04) << "compatible vibration 2";

    const std::vector<uint8_t> bt = DualSenseOutput::BluetoothReport(levels, 5);
    ASSERT_EQ(bt.size(), 78u);
    EXPECT_EQ(bt[0], 0x31);
    EXPECT_EQ(bt[1], 0x50);
    EXPECT_EQ(bt[2], 0x10);
    EXPECT_EQ(bt[3], 0x0F);
    // zlib.crc32(b"\xa2" + report[:74]) for these levels
    const uint32_t crc = bt[74] | (bt[75] << 8) | (bt[76] << 16) | (static_cast<uint32_t>(bt[77]) << 24);
    EXPECT_EQ(crc, 0xCA573445u);
}

TEST(HdHaptics_Test, FrequencyOfTheLoudestEffectPerBand)
{
    // The mixer: each band's frequency is the one of the effect loudest in it, 0 while the band is silent
    HapticMixer mixer([](const HostDeviceId&, const RumbleLevels&) { return true; }, false, FixedMixerClock());
    HapticEffect deep({0.9f, 0.2f, 0.0f, 0.0f, 50, 120}, 60000);
    HapticEffect bright({0.3f, 0.7f, 0.0f, 0.0f, 150, 300}, 60000);
    mixer.Play("pad", deep);
    EXPECT_EQ(mixer.Written("pad"), (RumbleLevels{0.9f, 0.2f, 0.0f, 0.0f, 50, 120}));
    mixer.Play("pad", bright);
    EXPECT_EQ(mixer.Written("pad"), (RumbleLevels{0.9f, 0.7f, 0.0f, 0.0f, 50, 300})) << "low from deep, high from bright";
    mixer.StopAll();
    HapticEffect trigger({0.0f, 0.0f, 0.0f, 1.0f, 50, 120}, 60000);
    mixer.Play("pad", trigger);
    EXPECT_EQ(mixer.Written("pad"), (RumbleLevels{0.0f, 0.0f, 0.0f, 1.0f, 0, 0})) << "silent bands carry no frequency";
    mixer.StopAll();

    // The library's defaults, and a rule's `hd` over them
    HapticEffect explosion;
    ASSERT_TRUE(BuiltinHapticEffect("explosion", explosion));
    EXPECT_EQ(explosion.levels.lowHz, 50);
    EXPECT_EQ(explosion.levels.highHz, 120);
    JsonValue section;
    std::string error;
    ASSERT_TRUE(ParseJson(R"({"sources": {"boom": {"probe": "exec", "address": "0x8000"}},
                              "rules": [{"on": "boom", "play": "explosion", "hd": {"frequency_hz": 70, "high_hz": 200}},
                                        {"on": "boom", "play": "tick", "target": "handle.low"}]})",
                          section, error))
        << error;
    HapticProfile profile;
    ASSERT_TRUE(ParseHapticProfile(section, profile, error)) << error;
    EXPECT_EQ(profile.rules[0].lowHz, 70) << "frequency_hz: both bands";
    EXPECT_EQ(profile.rules[0].highHz, 200) << "high_hz over it";
    ProfileHaptics feed(profile, "hd", mixer, [] { return std::vector<HostDeviceId>{"pad"}; });
    HapticFrame frame;
    frame.probeCount = 1;
    frame.probeHits[0] = 1;
    feed.OnHapticFrame(frame);
    mixer.Tick(FixedMixerTime() + std::chrono::milliseconds(10));
    const RumbleLevels written = mixer.Written("pad");
    EXPECT_GT(written.low, 0.0f);
    EXPECT_EQ(written.lowHz, 70) << "the explosion is the louder low; the rule's frequency";
    EXPECT_EQ(written.highHz, 200);

    // A tick moved to the low band takes its high-band frequency with it
    HapticEffect tick;
    ASSERT_TRUE(BuiltinHapticEffect("tick", tick));
    const RumbleLevels moved = RetargetLevels(tick.levels, kHandleLow);
    EXPECT_GT(moved.low, 0.0f);
    EXPECT_EQ(moved.lowHz, 320);
    EXPECT_EQ(moved.highHz, 0);

    for (const char* bad : {R"({"frequency_hz": 0})", R"({"frequency_hz": 5000})", R"({"waveform": "triangle"})", "7"})
    {
        JsonValue rule;
        ASSERT_TRUE(ParseJson(std::string(R"({"sources": {"s": {"probe": "exec", "address": 1}},
                                              "rules": [{"on": "s", "play": "tick", "hd": )") + bad + "}]}",
                              rule, error))
            << error;
        EXPECT_FALSE(ParseHapticProfile(rule, profile, error)) << bad;
    }
}

TEST(HdHaptics_Test, VoiceCoilTargetsAndTones)
{
    // haptics.md §6.1: `hd.left` / `hd.right` name the voice coils, `hd` gives their tone and wave
    JsonValue section;
    std::string error;
    ASSERT_TRUE(ParseJson(R"({"sources": {"s": {"probe": "exec", "address": 1}},
                              "rules": [{"on": "s", "play": "whip", "target": "hd.right|handle.high",
                                         "hd": {"frequency_hz": 90, "waveform": "square"}},
                                        {"on": "s", "play": "tick", "target": "hd.left"},
                                        {"on": "s", "play": "tick", "target": "all"}]})",
                          section, error))
        << error;
    HapticProfile profile;
    ASSERT_TRUE(ParseHapticProfile(section, profile, error)) << error;
    EXPECT_EQ(profile.rules[0].target, kHdRight);
    ASSERT_EQ(profile.rules[0].fallbacks.size(), 1u);
    EXPECT_EQ(profile.rules[0].hdFrequency, 90);
    EXPECT_EQ(profile.rules[0].lowHz, 90) << "frequency_hz still sets the bands";
    ASSERT_TRUE(profile.rules[0].hdWave.has_value());
    EXPECT_EQ(*profile.rules[0].hdWave, HdWave::Square);
    EXPECT_EQ(profile.rules[1].target, kHdLeft);
    EXPECT_EQ(profile.rules[2].target, kTargetAll);
    EXPECT_NE(kTargetAll & kHdLeft, 0);
    EXPECT_NE(kTargetAll & kHdRight, 0);

    // A pad without coils plays the fallback
    constexpr uint8_t handlesOnly = kRumbleLow | kRumbleHigh;
    constexpr uint8_t withCoils = handlesOnly | kRumbleHdLeft | kRumbleHdRight;
    EXPECT_EQ(profile.rules[0].TargetFor(handlesOnly), kHandleHigh);
    EXPECT_EQ(profile.rules[0].TargetFor(withCoils), kHdRight);

    // Retargeting: one coil gets the loudest level, the other stays silent; a motor target takes no coil level
    HapticEffect whip;
    ASSERT_TRUE(BuiltinHapticEffect("whip", whip));
    EXPECT_FLOAT_EQ(whip.levels.hdLeft, 0.0f) << "a whip lands on the side it flies to";
    EXPECT_GT(whip.levels.hdRight, 0.0f);
    const RumbleLevels coil = RetargetLevels(whip.levels, kHdLeft);
    EXPECT_GT(coil.hdLeft, 0.0f);
    EXPECT_FLOAT_EQ(coil.hdRight, 0.0f);
    EXPECT_FLOAT_EQ(coil.low, 0.0f);
    EXPECT_EQ(coil.hdFrequency, whip.levels.hdFrequency);
    const RumbleLevels motor = RetargetLevels(whip.levels, kHandleHigh);
    EXPECT_FLOAT_EQ(motor.hdLeft, 0.0f);
    EXPECT_FLOAT_EQ(motor.hdRight, 0.0f);
    EXPECT_FLOAT_EQ(motor.high, std::max(whip.levels.low, whip.levels.high)) << "the motor peak ignores the coils";
    const RumbleLevels all = RetargetLevels(whip.levels, kTargetAll);
    EXPECT_FLOAT_EQ(all.hdRight, whip.levels.hdRight) << "`all` keeps a coil's own level";
    EXPECT_GT(all.hdLeft, 0.0f) << "and gives the loudest to a coil the effect has none for";

    for (const char* bad : {R"({"waveform": "triangle"})", R"({"waveform": 3})"})
    {
        JsonValue rule;
        ASSERT_TRUE(ParseJson(std::string(R"({"sources": {"s": {"probe": "exec", "address": 1}},
                                              "rules": [{"on": "s", "play": "tick", "hd": )") + bad + "}]}",
                              rule, error))
            << error;
        EXPECT_FALSE(ParseHapticProfile(rule, profile, error)) << bad;
    }
}

TEST(HdHaptics_Test, VoiceCoilsInTheMixer)
{
    HapticMixer mixer([](const HostDeviceId&, const RumbleLevels&) { return true; }, false, FixedMixerClock());
    RumbleLevels left;
    left.hdLeft = 0.4f;
    left.hdFrequency = 80;
    left.hdWave = HdWave::Square;
    RumbleLevels right;
    right.hdRight = 0.9f;
    right.hdFrequency = 200;
    right.hdWave = HdWave::Noise;
    mixer.Play("pad", HapticEffect(left, 60000));
    EXPECT_FLOAT_EQ(mixer.Written("pad").hdLeft, 0.4f);
    EXPECT_FLOAT_EQ(mixer.Written("pad").hdRight, 0.0f);
    EXPECT_EQ(mixer.Written("pad").hdFrequency, 80);
    mixer.Play("pad", HapticEffect(right, 60000));
    const RumbleLevels mixed = mixer.Written("pad");
    EXPECT_FLOAT_EQ(mixed.hdLeft, 0.4f) << "each coil keeps its own level";
    EXPECT_FLOAT_EQ(mixed.hdRight, 0.9f);
    EXPECT_EQ(mixed.hdFrequency, 200) << "the tone is the loudest effect's";
    EXPECT_EQ(mixed.hdWave, HdWave::Noise);
    mixer.StopAll();
    EXPECT_FLOAT_EQ(mixer.Written("pad").hdRight, 0.0f);

    // A rule's `hd` sets the coils' tone over the effect's
    JsonValue section;
    std::string error;
    ASSERT_TRUE(ParseJson(R"({"sources": {"boom": {"probe": "exec", "address": "0x8000"}},
                              "rules": [{"on": "boom", "play": "thump", "target": "hd.left",
                                         "hd": {"frequency_hz": 45, "waveform": "noise"}}]})",
                          section, error))
        << error;
    HapticProfile profile;
    ASSERT_TRUE(ParseHapticProfile(section, profile, error)) << error;
    ProfileHaptics feed(profile, "hd", mixer, [] { return std::vector<HostDeviceId>{"pad"}; });
    HapticFrame frame;
    frame.probeCount = 1;
    frame.probeHits[0] = 1;
    feed.OnHapticFrame(frame);
    mixer.Tick(FixedMixerTime() + std::chrono::milliseconds(10));
    const RumbleLevels written = mixer.Written("pad");
    EXPECT_GT(written.hdLeft, 0.0f);
    EXPECT_FLOAT_EQ(written.hdRight, 0.0f);
    EXPECT_EQ(written.hdFrequency, 45);
    EXPECT_EQ(written.hdWave, HdWave::Noise);
}

TEST(HdHaptics_Test, VoiceCoilTestEffects)
{
    constexpr uint8_t deck = kRumbleLow | kRumbleHigh | kRumbleHdLeft | kRumbleHdRight | kHapticClicker;
    std::vector<std::string> ids;
    for (const TestEffect& effect : TestEffects(deck))
        ids.push_back(effect.id);
    for (const char* id : {"hd-left", "hd-right", "hd-knocks", "click"})
        EXPECT_NE(std::find(ids.begin(), ids.end(), id), ids.end()) << id;
    const std::vector<std::string> names = ActuatorNames(deck);
    EXPECT_NE(std::find(names.begin(), names.end(), "hd-left"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "hd-right"), names.end());

    for (const TestEffect& effect : TestEffects(kRumbleLow | kRumbleHigh))
        EXPECT_NE(effect.id.rfind("hd-", 0), 0u) << "a pad without coils lists no coil effect: " << effect.id;
}
