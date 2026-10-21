// The mixer's channel layer (mixerchannels.h, SoundManager::mixerChannels / setMixerChannel, AudioMixer's
// <source>.<channel> keys) and the host channel gains it drives: AY A / B / C, the YM2203 FM channels, the Covox /
// SounDrive DACs, the NeoGS mixer, the Next's parts and DAC; the sound settings every plane shares
// (SoundCharacterSettings: ay_stereo, ay_chip, fm_trim_db, neogs_stereo, covox_dc_removal, fir_filter); the catalogue
// actions, toggles and readouts over them. Gains at unity leave every path bit-exact (the audio digests of the other
// suites); the tests below measure the chips' real output with a gain or a mute.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "../../../automation/cli/src/commands/cli-audio-mixer.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/readouts.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/sound/audiomixer.h"
#include "emulator/sound/chips/neogs/neogssound.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"
#include "emulator/sound/chips/tsfm/ym2203_engine.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/mixerchannel.h"
#include "emulator/sound/nextaudio.h"
#include "emulator/sound/soundcharactersettings.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"

namespace
{
AudioMixer::Change Change(const char* muted, const char* solo, const char* volume = "", const char* gainDb = "")
{
    AudioMixer::Change change;
    std::string error;
    EXPECT_TRUE(AudioMixer::ChangeFromStrings(muted, solo, volume, gainDb, change, error)) << error;
    return change;
}

const StateNode* Row(const StateNode& mixer, const std::string& source)
{
    if (const StateNode* devices = mixer.find("devices"))
        for (const StateNode& d : devices->items)
            if (const StateNode* s = d.find("source"); s && s->s == source)
                return &d;
    return nullptr;
}

actions::ActionValue Press(std::initializer_list<std::pair<const char*, StateNode>> params)
{
    actions::ActionValue value;
    value.pressed = true;
    for (const auto& [name, node] : params)
        value.params[name] = node;
    return value;
}

actions::ActionOrigin Hands()
{
    actions::ActionOrigin origin;
    origin.origin = execution::Origin::HostDevice;
    origin.identity = "deck-1";
    origin.client = "deck-1";
    return origin;
}
}  // namespace

/// region <Gains: pure arithmetic>

TEST(MixerChannelGain_Test, UnityIsTheValueAndZeroIsSilence)
{
    EXPECT_EQ(MixerChannelGainQ15(false, 1.0f), 32768);
    EXPECT_EQ(MixerChannelGainQ15(true, 1.0f), 0);
    EXPECT_EQ(MixerChannelGainQ15(false, 0.0f), 0);
    EXPECT_EQ(MixerChannelGainQ15(false, 0.5f), 16384);
    for (int32_t v : {-32768, -1, 0, 1, 12345, 32767})
        EXPECT_EQ(ApplyMixerGainQ15(v, 32768), v);
    EXPECT_EQ(ApplyMixerGainQ15(1000, 16384), 500);
    EXPECT_EQ(ApplyMixerGainQ15(-1000, 16384), -500) << "toward zero, the same on every compiler";
    EXPECT_EQ(ApplyMixerGainQ15(1000, 0), 0);
}

TEST(MixerChannelGain_Test, NeoGSGainedMixIsTheBoardMixAtUnityAndDropsAChannel)
{
    NeoGSSound snd;
    snd.powerOn();
    const uint8_t samples[8] = {0xF0, 0x20, 0xC0, 0x90, 0x10, 0xE0, 0x70, 0xA0};
    for (int i = 0; i < 8; i++)
    {
        snd.setSampleRaw(i, samples[i]);
        snd.setVolume(i, static_cast<uint8_t>(0x20 + i * 3));
    }
    const int32_t unity[8] = {32768, 32768, 32768, 32768, 32768, 32768, 32768, 32768};
    for (uint8_t cfg : {uint8_t(0), NeoGSSound::CFG_8CHANS, NeoGSSound::CFG_PAN4CH, NeoGSSound::CFG_INV7B})
        for (bool right : {false, true})
            EXPECT_EQ(snd.mixGained(right, cfg, unity), snd.mix(right, cfg)) << "cfg " << int(cfg);

    // 4-channel mode: L = 2 (c1 v1 + c2 v2); channel 1 at 0 leaves 2 c2 v2
    int32_t gains[8] = {0, 32768, 32768, 32768, 32768, 32768, 32768, 32768};
    const int32_t c2 = NeoGSSound::signedSample(samples[1], 0) * snd.volume(1);
    EXPECT_EQ(snd.mixGained(false, 0, gains), 2 * c2);
    EXPECT_EQ(snd.mixGained(true, 0, gains), snd.mix(true, 0)) << "the other side keeps its channels";
}

/// endregion </Gains>

/// region <Chips: real output>

TEST(MixerChannelChips_Test, CovoxChannelGainScalesTheDacLevelAndMuteSilencesIt)
{
    EmulatorContext context(LoggerLevel::LogError);
    context.config.sound.covoxFB = 1;
    SoundManager sound(&context);
    sound.reset();
    Covox* covox = sound.getCovox();
    ASSERT_NE(covox, nullptr);
    covox->portDeviceOutMethod(0x00FB, 0xC0);  // the mono port: one channel, centred on both sides
    const int32_t full = covox->lastLeftAmplitude();
    ASSERT_EQ(full, (0xC0 - 128) * 128);
    EXPECT_EQ(covox->lastRightAmplitude(), full);

    covox->setChannelGain(Covox::Channel::RightB, 16384);
    covox->portDeviceOutMethod(0x00FB, 0xC0);
    EXPECT_EQ(covox->lastLeftAmplitude(), full / 2);
    covox->setChannelMute(Covox::Channel::RightB, true);
    covox->portDeviceOutMethod(0x00FB, 0xC0);
    EXPECT_EQ(covox->lastLeftAmplitude(), 0);
    covox->setChannelMute(Covox::Channel::RightB, false);
    covox->setChannelGain(Covox::Channel::RightB, 32768);
    covox->portDeviceOutMethod(0x00FB, 0xC0);
    EXPECT_EQ(covox->lastLeftAmplitude(), full) << "unity: the hardware level again";
}

namespace
{
/// One YM2203 with FM channel 0 sounding (algorithm 7: four carriers at TL 0)
struct FmTone
{
    Ym2203Interface intf;
    Ym2203Engine fm{intf};

    explicit FmTone(int channel = 0)
    {
        fm.reset();
        auto reg = [this](uint8_t r, uint8_t v) {
            fm.write_address(r);
            fm.write_data(v);
        };
        const uint8_t c = static_cast<uint8_t>(channel);
        reg(0xB0 + c, 0x07);
        for (uint8_t op : {0x00, 0x04, 0x08, 0x0C})
        {
            reg(0x30 + op + c, 0x01);  // multiple 1
            reg(0x40 + op + c, 0x00);  // TL 0
            reg(0x50 + op + c, 0x1F);  // attack rate max
            reg(0x60 + op + c, 0x00);
            reg(0x70 + op + c, 0x00);
            reg(0x80 + op + c, 0x0F);
        }
        reg(0xA4 + c, 0x22);
        reg(0xA0 + c, 0x69);
        reg(0x28, static_cast<uint8_t>(0xF0 | c));  // key on, all slots
    }

    std::vector<int16_t> Samples(size_t n)
    {
        std::vector<int16_t> out(n);
        for (int16_t& s : out)
            s = fm.clockFmOnce();
        return out;
    }
};

int32_t MaxAbs(const std::vector<int16_t>& samples)
{
    int32_t m = 0;
    for (int16_t s : samples)
        m = std::max(m, std::abs(static_cast<int32_t>(s)));
    return m;
}
}  // namespace

TEST(MixerChannelChips_Test, Ym2203FmChannelGainIsExactAtUnityAndScalesOneChannel)
{
    FmTone ref, silentOther, zero, half;
    silentOther.fm.setChannelGain(1, 0);  // channel 1 plays nothing: the per-channel sum equals clock_fm()
    zero.fm.setChannelGain(0, 0);
    half.fm.setChannelGain(0, 16384);
    const std::vector<int16_t> a = ref.Samples(4000);
    ASSERT_GT(MaxAbs(a), 1000);
    EXPECT_EQ(silentOther.Samples(4000), a) << "the gained path sums the channels as ymfm does";
    EXPECT_EQ(MaxAbs(zero.Samples(4000)), 0);
    const int32_t halfMax = MaxAbs(half.Samples(4000));
    EXPECT_GT(halfMax, MaxAbs(a) * 3 / 10);
    EXPECT_LT(halfMax, MaxAbs(a) * 7 / 10);
}

/// endregion </Chips>

/// region <The channel layer on a machine>

class MixerChannels_Test : public ::testing::Test
{
protected:
    SoundCardScope _turboSound{TestSound::TurboSound};  // the shipped Pentagon: TurboSound FM in the AY socket
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    SoundManager* _sound = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero,
                                                                [](CONFIG& config) { config.sound.sd = 1; });
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _sound = _context->pSoundManager;
        ASSERT_NE(_sound, nullptr);
        ASSERT_NE(_sound->device(AudioSourceType::AY1_All), nullptr);
    }
    void TearDown() override { EmulatorTestHelper::CleanupEmulator(_emulator); }

    actions::ActionResult Run(const char* id, actions::ActionValue value)
    {
        return actions::ActionRegistry::Instance().Run(id, std::move(value), _emulator->GetId(), Hands());
    }
};

TEST_F(MixerChannels_Test, AySoloAndVolumeReachTheChipAtTheNextFrame)
{
    const std::vector<SoundManager::MixerChannelState> channels = _sound->mixerChannels(AudioSourceType::AY1_All);
    ASSERT_EQ(channels.size(), 3u);
    EXPECT_EQ(channels[0].desc.key, "a");
    EXPECT_EQ(channels[2].desc.key, "c");

    std::string error;
    ASSERT_TRUE(AudioMixer::Apply(_context, "ay1.b", Change("", "1"), error)) << error;
    ASSERT_TRUE(AudioMixer::Apply(_context, "AY1.A", Change("", "", "0.25"), error)) << error;
    SoundChip_AY8910* ay = _sound->getAYChip(0);
    ASSERT_NE(ay, nullptr);
    EXPECT_FALSE(ay->isChannelMuted(0)) << "requested only: the device takes it at the frame start";

    EmulatorTestHelper::RunFramesFast(_emulator, 1);
    EXPECT_TRUE(ay->isChannelMuted(0)) << "solo B silences A and C of the same chip";
    EXPECT_FALSE(ay->isChannelMuted(1));
    EXPECT_TRUE(ay->isChannelMuted(2));
    EXPECT_DOUBLE_EQ(ay->getChannelVolume(0), 0.25);
    EXPECT_FALSE(_sound->getAYChip(1)->isChannelMuted(0)) << "the other chip is not touched";

    // The report: every channel by its full key, audible as the solo leaves it
    const StateNode mixer = DeviceState::AudioMixer(_context);
    const StateNode* ay1 = Row(mixer, "ay1");
    ASSERT_NE(ay1, nullptr);
    const StateNode* list = ay1->find("channels");
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->items.size(), 3u);
    EXPECT_EQ(list->items[0].find("key")->s, "ay1.a");
    EXPECT_FALSE(list->items[0].find("audible")->b);
    EXPECT_TRUE(list->items[1].find("audible")->b);
    EXPECT_NEAR(list->items[0].find("gain_db")->d, -12.0, 0.1);

    ASSERT_TRUE(AudioMixer::Apply(_context, "ay1.b", Change("", "0"), error)) << error;
    EmulatorTestHelper::RunFramesFast(_emulator, 1);
    EXPECT_FALSE(ay->isChannelMuted(0));
    EXPECT_FALSE(ay->isChannelMuted(2));
}

TEST_F(MixerChannels_Test, UnknownChannelsAndSourcesAreRefusedWithWhatThereIs)
{
    std::string error;
    EXPECT_FALSE(AudioMixer::Apply(_context, "ay1.z", Change("1", ""), error));
    EXPECT_NE(error.find("a, b, c"), std::string::npos) << error;
    EXPECT_FALSE(AudioMixer::Apply(_context, "beeper.a", Change("1", ""), error));
    EXPECT_NE(error.find("no channels"), std::string::npos) << error;
    EXPECT_FALSE(AudioMixer::Apply(_context, "ms_midi.1", Change("1", ""), error)) << "not fitted";
    EXPECT_FALSE(AudioMixer::Apply(_context, "master.a", Change("1", ""), error));

    StateNode node;
    ASSERT_TRUE(AudioMixer::Read(_context, "ay2.c", node, error)) << error;
    EXPECT_EQ(node.find("source")->s, "ay2.c");
    EXPECT_FALSE(AudioMixer::Read(_context, "ay2.q", node, error));
}

TEST_F(MixerChannels_Test, TsfmFmAndSounDriveChannelsTakeTheirGains)
{
    auto* tsfm = dynamic_cast<SoundChip_TurboSoundFM*>(_sound->getTurboSound());
    ASSERT_NE(tsfm, nullptr) << "the shipped Pentagon has a TurboSound FM";
    ASSERT_EQ(_sound->mixerChannels(AudioSourceType::FM1).size(), 3u);
    ASSERT_EQ(_sound->mixerChannels(AudioSourceType::COVOX).size(), 4u);

    std::string error;
    ASSERT_TRUE(AudioMixer::Apply(_context, "fm1.2", Change("", "", "0.5"), error)) << error;
    ASSERT_TRUE(AudioMixer::Apply(_context, "fm2.3", Change("1", ""), error)) << error;
    ASSERT_TRUE(AudioMixer::Apply(_context, "covox.lb", Change("", "", "", "-6"), error)) << error;
    EmulatorTestHelper::RunFramesFast(_emulator, 1);
    EXPECT_EQ(tsfm->chip(0)->fm.channelGain(1), 16384);
    EXPECT_EQ(tsfm->chip(0)->fm.channelGain(0), 32768);
    EXPECT_EQ(tsfm->chip(1)->fm.channelGain(2), 0);
    EXPECT_NEAR(_sound->getCovox()->channelGain(Covox::Channel::LeftB), 16423, 2);  // -6 dB
    EXPECT_EQ(_sound->getCovox()->channelGain(Covox::Channel::LeftA), 32768);
}

TEST_F(MixerChannels_Test, CliShowsTheChannelsOfOneSourceAndSetsOne)
{
    const std::string rows = CliAudioMixer::Text(_context, {"mixer"});
    EXPECT_NE(rows.find("channels: mixer <source>"), std::string::npos) << rows;
    EXPECT_EQ(rows.find("ay1.a"), std::string::npos) << "the rows alone";
    const std::string set = CliAudioMixer::Text(_context, {"mixer", "ay1.c", "muted=1"});
    EXPECT_NE(set.find("ay1.c"), std::string::npos) << set;
    EXPECT_NE(set.find("silent"), std::string::npos) << set;
    EXPECT_EQ(set.find("beeper "), std::string::npos) << "only the row asked for";
    EXPECT_NE(CliAudioMixer::Text(_context, {"mixer", "ay1.x", "muted=1"}).find("Error:"), std::string::npos);
}

TEST_F(MixerChannels_Test, ActionsToggleMuteAndSoloOfAChannelAndSetAVolume)
{
    actions::ActionResult muted = Run("audio.source_mute", Press({{"source", "ay1"}, {"channel", "b"}}));
    ASSERT_TRUE(muted.ok) << muted.message;
    EXPECT_TRUE(muted.body.find("muted")->b);
    EXPECT_EQ(AudioMixer::Flag(_context, "ay1.b", "muted"), std::optional<bool>(true));
    actions::ActionValue state = Press({{"source", "ay1.b"}});
    EXPECT_EQ(actions::ActionRegistry::Instance().ToggleState("audio.source_mute", _emulator->GetId(), state.params),
              std::optional<bool>(true));
    ASSERT_TRUE(Run("audio.source_mute", Press({{"source", "ay1.b"}})).ok) << "flips back";
    EXPECT_EQ(AudioMixer::Flag(_context, "ay1.b", "muted"), std::optional<bool>(false));

    ASSERT_TRUE(Run("audio.source_solo", Press({{"source", "beeper"}, {"on", true}})).ok);
    EXPECT_TRUE(_sound->device(AudioSourceType::Beeper)->solo);
    ASSERT_TRUE(Run("audio.source_solo", Press({{"source", "beeper"}})).ok);
    EXPECT_FALSE(_sound->device(AudioSourceType::Beeper)->solo);

    ASSERT_TRUE(Run("audio.source_volume", Press({{"source", "ay2"}, {"gain_db", -6.0}})).ok);
    EXPECT_NEAR(_sound->device(AudioSourceType::AY2_All)->volume, 0.501f, 0.001f);
    ASSERT_TRUE(Run("audio.source_volume", Press({{"source", "ay2.a"}, {"volume", 0.3}})).ok);
    EXPECT_NEAR(_sound->mixerChannels(AudioSourceType::AY2_All)[0].volume, 0.3f, 1e-6f);
    EXPECT_EQ(Run("audio.source_volume", Press({{"source", "ay2.a"}})).error, "Bad Request");

    actions::ActionValue dial = Press({{"source", "ay2.a"}, {"step", 0.1}});
    dial.kind = actions::ValueKind::Delta;
    dial.delta = 2;
    ASSERT_TRUE(Run("audio.volume_by", dial).ok);
    EXPECT_NEAR(_sound->mixerChannels(AudioSourceType::AY2_All)[0].volume, 0.5f, 1e-4f);
    EXPECT_EQ(Run("audio.source_mute", Press({{"source", "nope.a"}})).error, "Not Found");
}

TEST_F(MixerChannels_Test, ReadoutsShowTheLevelsAndOneSource)
{
    std::string error;
    ASSERT_TRUE(AudioMixer::Apply(_context, "ay1.a", Change("1", ""), error)) << error;
    const StateNode levels = actions::ReadReadout(*_emulator, "audio.mixer");
    const StateNode* ay1 = Row(levels, "ay1");
    ASSERT_NE(ay1, nullptr);
    EXPECT_NE(ay1->find("peak"), nullptr);
    ASSERT_NE(ay1->find("channels"), nullptr);
    EXPECT_TRUE(ay1->find("channels")->items[0].find("muted")->b);
    EXPECT_EQ(ay1->find("channels")->items[0].find("name"), nullptr) << "the levels are compact";

    const StateNode one = actions::ReadReadout(*_emulator, "audio.source.ay1.a");
    ASSERT_NE(one.find("muted"), nullptr);
    EXPECT_TRUE(one.find("muted")->b);
    EXPECT_EQ(actions::ReadReadout(*_emulator, "audio.source.nope").kind, StateNode::Kind::Null);
    const StateNode toggle = actions::ReadReadout(*_emulator, "toggle.audio.source_mute.ay1.a");
    ASSERT_NE(toggle.find("on"), nullptr);
    EXPECT_TRUE(toggle.find("on")->b);
}

TEST_F(MixerChannels_Test, SoundSettingsOnEveryPlaneShareOneTable)
{
    // AY stereo and chip: requested at once, the socket's chips at the frame start; a TSFM refuses another chip
    std::string error;
    ASSERT_TRUE(SoundCharacterSettings::Set(*_sound, "ay_stereo", "acb", error)) << error;
    EXPECT_EQ(SoundCharacterSettings::Get(*_sound, "ay_stereo"), "acb");
    EmulatorTestHelper::RunFramesFast(_emulator, 1);
    EXPECT_EQ(_sound->getAYChip(0)->getStereoMode(), AYStereoMode::ACB);
    EXPECT_EQ(_sound->getAYChip(1)->getStereoMode(), AYStereoMode::ACB);
    EXPECT_FALSE(SoundCharacterSettings::Set(*_sound, "ay_stereo", "bca", error));
    EXPECT_EQ(SoundCharacterSettings::Get(*_sound, "ay_chip"), "ym2203");
    EXPECT_FALSE(SoundCharacterSettings::Set(*_sound, "ay_chip", "ay8910", error));
    EXPECT_NE(error.find("TurboSound FM"), std::string::npos) << error;

    // FM trim: rounded to 0.5 dB, ±12
    ASSERT_TRUE(SoundCharacterSettings::Set(*_sound, "fm_trim_db", "1.3", error)) << error;
    EXPECT_EQ(SoundCharacterSettings::Get(*_sound, "fm_trim_db"), "1.5");
    EXPECT_FALSE(SoundCharacterSettings::Set(*_sound, "fm_trim_db", "13", error));
    EXPECT_FALSE(SoundCharacterSettings::Set(*_sound, "fm_trim_db", "loud", error));

    ASSERT_TRUE(SoundCharacterSettings::Set(*_sound, "neogs_stereo", "mono", error)) << error;
    EXPECT_EQ(_sound->neoGSStereoMode(), NeoGSConfig::StereoMode::Mono);
    ASSERT_TRUE(SoundCharacterSettings::Set(*_sound, "covox_dc_removal", "on", error)) << error;
    EXPECT_TRUE(_sound->getCovox()->isDCRemovalEnabled());
    const bool hq = _context->pFeatureManager->isEnabled(Features::kSoundHQ);
    ASSERT_TRUE(SoundCharacterSettings::Set(*_sound, "fir_filter", hq ? "off" : "on", error)) << error;
    EXPECT_NE(_context->pFeatureManager->isEnabled(Features::kSoundHQ), hq);

    // The same through the catalogue (set.value / set.toggle), with a number and the core rate
    ASSERT_TRUE(Run("set.value", Press({{"name", "fm_trim_db"}, {"value", -2.0}})).ok);
    EXPECT_EQ(SoundCharacterSettings::Get(*_sound, "fm_trim_db"), "-2.0");
    ASSERT_TRUE(Run("set.value", Press({{"name", "ay_stereo"}, {"value", "mono"}})).ok);
    ASSERT_TRUE(Run("set.toggle", Press({{"name", "covox_dc_removal"}})).ok);
    EXPECT_FALSE(_sound->getCovox()->isDCRemovalEnabled());
    const actions::ActionResult rate = Run("set.value", Press({{"name", "audio_rate"}, {"value", "48000"}}));
    ASSERT_TRUE(rate.ok) << rate.message;
    EXPECT_EQ(_sound->getCoreRatePin(), 48000u);
    ASSERT_TRUE(Run("set.value", Press({{"name", "audio_rate"}, {"value", "auto"}})).ok);
    EXPECT_EQ(_sound->getCoreRatePin(), 0u);
    EXPECT_EQ(Run("set.value", Press({{"name", "audio_rate"}, {"value", 12345}})).error, "Bad Request");
}

TEST(MixerChannelsAbsent_Test, SettingsAMachineDoesNotHaveReadUnavailable)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero,
                                                                    [](CONFIG& config) {
                                                                        config.sound.sd = 0;
                                                                        config.sound.covoxFB = 0;
                                                                    });
    ASSERT_NE(emulator, nullptr);
    SoundManager& sound = *emulator->GetContext()->pSoundManager;
    std::string error;
    EXPECT_EQ(SoundCharacterSettings::Get(sound, "fm_trim_db"), "unavailable") << "no YM2203 FM (no TurboSound here)";
    EXPECT_FALSE(SoundCharacterSettings::Set(sound, "fm_trim_db", "1", error));
    EXPECT_EQ(SoundCharacterSettings::Get(sound, "covox_dc_removal"), "unavailable");
    EXPECT_FALSE(SoundCharacterSettings::Set(sound, "covox_dc_removal", "on", error));
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// endregion </The channel layer on a machine>

/// region <The Next: parts as rows>

class MixerChannelsNext_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
        _ports->Board().Write(0x08, 0x02 | 0x08);  // turbosound and the DAC on
    }
    void TearDown() override { EmulatorTestHelper::CleanupEmulator(_emulator); }

    /// The first left sample of the Next's buffer after `frames` frames (a change lands at the next frame start, so the
    /// first frame that renders with it is the second)
    int16_t LeftAfter(int frames)
    {
        EmulatorTestHelper::RunFramesFast(_emulator, frames);
        return _ports->Audio().AudioBuffer()[0];
    }
};

TEST_F(MixerChannelsNext_Test, ThreeAysAndTheDacAreRowsOfTheirOwn)
{
    SoundManager* sound = _context->pSoundManager;
    for (AudioSourceType type : {AudioSourceType::NextAy1, AudioSourceType::NextAy2, AudioSourceType::NextAy3,
                                 AudioSourceType::NextDac})
        EXPECT_NE(sound->device(type), nullptr) << AudioMixer::Key(type);
    EXPECT_EQ(sound->device(AudioSourceType::COVOX), nullptr) << "no COVOX row: the parts replace it";
    EXPECT_EQ(sound->mixerChannels(AudioSourceType::NextAy2).size(), 3u);
    ASSERT_EQ(sound->mixerChannels(AudioSourceType::NextDac).size(), 4u);
    EXPECT_EQ(sound->mixerChannels(AudioSourceType::NextDac)[3].desc.key, "d");
}

TEST_F(MixerChannelsNext_Test, TheDacRowAndItsChannelsSetTheirGainsInTheNextMix)
{
    _ports->Audio().WriteDac(0, 0xFF);  // A: left
    const int16_t level = LeftAfter(2);
    ASSERT_GT(level, 1500) << "A at #FF: +127 x 16 of 32768";

    std::string error;
    ASSERT_TRUE(AudioMixer::Apply(_context, "next_dac.a", Change("", "", "0.5"), error)) << error;
    const int16_t half = LeftAfter(2);
    EXPECT_NEAR(half, level / 2, 2);

    ASSERT_TRUE(AudioMixer::Apply(_context, "next_dac", Change("1", ""), error)) << error;
    EXPECT_LE(std::abs(LeftAfter(2)), 2) << "the row muted: its gain in the Next's mix is 0";
    ASSERT_TRUE(AudioMixer::Apply(_context, "next_dac", Change("0", ""), error)) << error;
    ASSERT_TRUE(AudioMixer::Apply(_context, "next_ay1", Change("", "1"), error)) << error;
    EXPECT_LE(std::abs(LeftAfter(2)), 2) << "a solo elsewhere leaves the DAC out";
    ASSERT_TRUE(AudioMixer::Apply(_context, "next_ay1", Change("", "0"), error)) << error;
    ASSERT_TRUE(AudioMixer::Apply(_context, "next_dac.a", Change("", "", "1"), error)) << error;
    EXPECT_EQ(LeftAfter(2), level) << "unity again: the hardware mix, bit-exact";
}

/// endregion </The Next>
