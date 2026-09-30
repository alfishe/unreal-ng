#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <functional>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/iturbosounddevice.h"
#include "emulator/sound/soundmanager.h"

/// SoundManager: AY / SSG tone voicing wiring and the thread-safe sound
/// character settings (voicing, AY punch / room, beeper punch).
/// Pinned: the configured profile is live from the first frame; voicing runs
/// in HQ and LQ (unlike the punch / room chains) and is not reset when HQ
/// returns; Flat leaves the chip buffers bit-identical; gaps (sound off,
/// turbo without audio) drop the pre-roll history; TSFM voices the SSG buffers
/// but never the FM buffers; requests are visible at once and applied at the
/// next frame boundary; direct chain edits are not reverted.
/// Design: docs/inprogress/2026-09-25-ay-tone-voicing/ay-tone-voicing-tdd.md §9.2

namespace
{
constexpr uint32_t PENTAGON_FRAME = 71680;
using Preset = FilterVoicing::Preset;
}  // namespace

class SoundManagerVoicing_Test : public ::testing::Test
{
protected:
    SoundCardScope _turboSound{TestSound::TurboSound};  // the AY slot is the subject
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";
        _context = _emulator->GetContext();
        _context->config.frame = PENTAGON_FRAME;
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _context->config.turbo_mode = false;
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    /// Program chip 0 channel A: 110 Hz square (bass - where voicing acts), full volume
    static void ProgramBassTone(ITurboSoundDevice& device)
    {
        auto poke = [&](uint8_t reg, uint8_t value) {
            device.portDeviceOutMethod(0xFFFD, reg);
            device.portDeviceOutMethod(0xBFFD, value);
        };
        poke(0, 0xE2);  // period 994: 1.75 MHz / (16 * 994) = 110 Hz
        poke(1, 0x03);
        poke(7, 0b00111110);
        poke(8, 15);
    }

    /// One frame: render the device, snapshot the raw chip-0 buffer, run the
    /// manager's frame end. Returns true when chip 0 left it byte-identical
    bool FrameLeavesChipBufferUntouched(SoundManager& sound, int chip = 0)
    {
        ITurboSoundDevice* device = sound.getTurboSound();
        Z80* z80 = _context->pCore->GetZ80();

        sound.handleFrameStart();
        z80->t = PENTAGON_FRAME;
        sound.handleStep();
        z80->t = 0;

        const size_t bytes = device->getRenderedSamplesThisFrame() * AUDIO_CHANNELS * sizeof(int16_t);
        std::vector<uint8_t> raw(bytes);
        memcpy(raw.data(), device->getChipBuffer(chip), bytes);

        sound.handleFrameEnd();
        return memcmp(raw.data(), device->getChipBuffer(chip), bytes) == 0;
    }

    void RunFrames(SoundManager& sound, int frames)
    {
        for (int i = 0; i < frames; i++)
            FrameLeavesChipBufferUntouched(sound);
    }
};

TEST_F(SoundManagerVoicing_Test, ConfiguredProfileIsLiveFromFirstFrame)
{
    EXPECT_EQ(_context->config.sound.ayVoicing, Preset::Headphones) << "soft highs is the built-in default";
    {
        SoundManager sound(_context);
        EXPECT_EQ(sound.getAYVoicing(), Preset::Headphones);
        EXPECT_EQ(sound.getActiveAYVoicing(), Preset::Headphones) << "no crossfade from flat at construction";
    }

    _context->config.sound.ayVoicing = Preset::Flat;
    SoundManager flat(_context);
    EXPECT_EQ(flat.getActiveAYVoicing(), Preset::Flat);
}

TEST_F(SoundManagerVoicing_Test, FlatLeavesChipBuffersUntouched)
{
    // Flat + LQ (chains skipped): the device render reaches the mixer bit-identical
    _context->config.sound.ayVoicing = Preset::Flat;
    SoundManager sound(_context);
    ProgramBassTone(*sound.getTurboSound());
    sound.setTurboLowQualityOverride(true);

    for (int i = 0; i < 4; i++)
        EXPECT_TRUE(FrameLeavesChipBufferUntouched(sound)) << "frame " << i;
}

TEST_F(SoundManagerVoicing_Test, VoicingRunsInLQ)
{
    // The punch / room chains are HQ-only; voicing is not - the bass must not
    // change when HQ is toggled or turbo ends
    SoundManager sound(_context);
    ProgramBassTone(*sound.getTurboSound());
    sound.setTurboLowQualityOverride(true);
    ASSERT_FALSE(sound.isHQActive());

    RunFrames(sound, 3);
    EXPECT_FALSE(FrameLeavesChipBufferUntouched(sound)) << "classic voicing must process the LQ chip buffer";
    EXPECT_GT(sound.getAYVoicingStage(0).filter().stateMagnitude(), 0.0);
}

TEST_F(SoundManagerVoicing_Test, VoicingNotResetOnHQReturn)
{
    // HQ on -> off -> on: the chains are reset on the HQ return, voicing is
    // not (its state ran through the LQ frames, and the HQ switch is not a gap
    // in the stream). Checked on the stage state, not on audio: the HQ switch
    // changes the decimator output itself, so no continuous reference exists
    SoundManager sound(_context);
    ProgramBassTone(*sound.getTurboSound());
    RunFrames(sound, 3);

    sound.setTurboLowQualityOverride(true);
    RunFrames(sound, 3);
    ASSERT_EQ(sound.getAYVoicingStage(0).historyFrames(), VoicingStage::HISTORY_FRAMES);

    sound.setTurboLowQualityOverride(false);
    RunFrames(sound, 1);
    EXPECT_EQ(sound.getAYVoicingStage(0).historyFrames(), VoicingStage::HISTORY_FRAMES)
        << "the HQ return must not reset the voicing stage";
    EXPECT_GT(sound.getAYVoicingStage(0).filter().stateMagnitude(), 0.0);
}

TEST_F(SoundManagerVoicing_Test, GetReturnsRequestedPreset)
{
    SoundManager sound(_context);
    ProgramBassTone(*sound.getTurboSound());
    RunFrames(sound, 2);

    sound.setAYVoicing(Preset::Flat);
    EXPECT_EQ(sound.getAYVoicing(), Preset::Flat) << "a read right after a write shows the request";
    EXPECT_EQ(sound.getActiveAYVoicing(), Preset::Headphones) << "audio switches at the next frame boundary";

    RunFrames(sound, 1);
    EXPECT_EQ(sound.getActiveAYVoicing(), Preset::Flat);
    EXPECT_EQ(sound.getAYVoicingStage(1).active(), Preset::Flat) << "both chips follow one setting";
}

TEST_F(SoundManagerVoicing_Test, GapsDropThePreRollHistory)
{
    SoundManager sound(_context);
    ProgramBassTone(*sound.getTurboSound());
    RunFrames(sound, 3);
    ASSERT_EQ(sound.getAYVoicingStage(0).historyFrames(), VoicingStage::HISTORY_FRAMES);

    // Sound feature off: nothing is voiced, the history no longer precedes the next frame
    _context->pFeatureManager->setFeature(Features::kSoundGeneration, false);
    sound.UpdateFeatureCache();
    RunFrames(sound, 1);
    EXPECT_EQ(sound.getAYVoicingStage(0).historyFrames(), 0u);

    _context->pFeatureManager->setFeature(Features::kSoundGeneration, true);
    sound.UpdateFeatureCache();
    RunFrames(sound, 3);
    ASSERT_EQ(sound.getAYVoicingStage(0).historyFrames(), VoicingStage::HISTORY_FRAMES);

    // Turbo without audio: the host-audio half of the frame end is skipped
    _context->config.turbo_mode = true;
    _context->config.turbo_mode_audio = false;
    RunFrames(sound, 1);
    EXPECT_EQ(sound.getAYVoicingStage(0).historyFrames(), 0u);
}

TEST_F(SoundManagerVoicing_Test, CoreRateChangeKeepsProfileDropsHistory)
{
    SoundManager sound(_context);
    ProgramBassTone(*sound.getTurboSound());
    RunFrames(sound, 3);

    const uint32_t newRate = sound.getCoreRate() == 48000 ? 44100 : 48000;
    sound.requestCoreRate(newRate);
    sound.handleFrameStart();  // applies the pending rate at the frame boundary
    ASSERT_EQ(sound.getCoreRate(), newRate);
    EXPECT_EQ(sound.getActiveAYVoicing(), Preset::Headphones);
    EXPECT_EQ(sound.getAYVoicingStage(0).historyFrames(), 0u) << "old-rate history must be dropped";
    EXPECT_GT(sound.getAYVoicingStage(0).filter().stateMagnitude(), 0.0) << "filter state is kept (no step)";
}

TEST_F(SoundManagerVoicing_Test, TsfmSsgIsVoicedFmIsNot)
{
    _context->config.sound.turboSoundKind = TurboSoundKind::FM;
    SoundManager sound(_context);
    ITurboSoundDevice* device = sound.getTurboSound();
    ASSERT_NE(device, nullptr);
    ASSERT_TRUE(device->hasFm());
    ProgramBassTone(*device);
    sound.setTurboLowQualityOverride(true);  // LQ: no chain touches any buffer, only voicing
    RunFrames(sound, 3);

    Z80* z80 = _context->pCore->GetZ80();
    sound.handleFrameStart();
    z80->t = PENTAGON_FRAME;
    sound.handleStep();
    z80->t = 0;
    const size_t bytes = device->getRenderedSamplesThisFrame() * AUDIO_CHANNELS * sizeof(int16_t);
    std::vector<uint8_t> ssg(bytes), fm0(bytes), fm1(bytes);
    memcpy(ssg.data(), device->getChipBuffer(0), bytes);
    memcpy(fm0.data(), device->getFmBuffer(0), bytes);
    memcpy(fm1.data(), device->getFmBuffer(1), bytes);
    sound.handleFrameEnd();

    EXPECT_NE(memcmp(ssg.data(), device->getChipBuffer(0), bytes), 0) << "the SSG half is voiced";
    EXPECT_EQ(memcmp(fm0.data(), device->getFmBuffer(0), bytes), 0) << "FM 1 must never be voiced";
    EXPECT_EQ(memcmp(fm1.data(), device->getFmBuffer(1), bytes), 0) << "FM 2 must never be voiced";
}

TEST_F(SoundManagerVoicing_Test, ResetKeepsProfileClearsState)
{
    SoundManager sound(_context);
    sound.setAYVoicing(Preset::Flat);
    ProgramBassTone(*sound.getTurboSound());
    RunFrames(sound, 3);

    sound.setAYVoicing(Preset::Classic);
    sound.reset();
    EXPECT_EQ(sound.getActiveAYVoicing(), Preset::Classic) << "a pending request applies at once on reset";
    EXPECT_EQ(sound.getAYVoicingStage(0).historyFrames(), 0u);
    EXPECT_EQ(sound.getAYVoicingStage(0).filter().stateMagnitude(), 0.0);
}

TEST_F(SoundManagerVoicing_Test, CharacterRequestsApplyAtFrameBoundary)
{
    SoundManager sound(_context);
    ASSERT_TRUE(sound.getAYChain().isPunchEnabled());

    sound.setAYPunch(false);
    sound.setAYRoomMode(AudioCharacterChain::RoomMode::Room_9dB);
    sound.setBeeperPunch(true);
    EXPECT_FALSE(sound.getAYPunch()) << "requests are visible at once";
    EXPECT_EQ(sound.getAYRoomMode(), AudioCharacterChain::RoomMode::Room_9dB);
    EXPECT_TRUE(sound.getBeeperPunch());
    EXPECT_TRUE(sound.getAYChain().isPunchEnabled()) << "the chains change only at the frame boundary";

    RunFrames(sound, 1);
    EXPECT_FALSE(sound.getAYChain().isPunchEnabled());
    EXPECT_EQ(sound.getAYChain().getRoomMode(), AudioCharacterChain::RoomMode::Room_9dB);
    EXPECT_TRUE(sound.getBeeperChain().isPunchEnabled());
}

TEST_F(SoundManagerVoicing_Test, DirectChainEditIsNotReverted)
{
    // Legacy path (tests, older callers): edit chain 0, sync. The handoff
    // applies only CHANGED requests, and the sync updates the request, so the
    // next frame boundary must not undo the edit
    SoundManager sound(_context);
    sound.getAYChain().setRoomMode(AudioCharacterChain::RoomMode::Room_6dB);
    sound.syncAYChainSettings();
    EXPECT_EQ(sound.getAYRoomMode(), AudioCharacterChain::RoomMode::Room_6dB);

    RunFrames(sound, 2);
    EXPECT_EQ(sound.getAYChain().getRoomMode(), AudioCharacterChain::RoomMode::Room_6dB);
}


/// region <Output delay line (temporal video effects)>

namespace
{
/// Everything the device callback received, one entry per frame
struct DelayCapture
{
    std::vector<std::vector<int16_t>> frames;
    static void callback(void* obj, int16_t* samples, size_t count)
    {
        static_cast<DelayCapture*>(obj)->frames.emplace_back(samples, samples + count);
    }
};

/// Frames with a beeper tone on frames 0, 3, 6, ... and silence otherwise (so a
/// shifted stream is told apart by content, not only by length). Returns what the
/// device got; `delayAt` maps a frame index to the output delay set before it.
std::vector<std::vector<int16_t>> RunBeeperFrames(EmulatorContext* context, int frames,
                                                  const std::function<int(int)>& delayAt)
{
    DelayCapture capture;
    context->pAudioCallback.store(&DelayCapture::callback, std::memory_order_release);
    context->pAudioManagerObj.store(&capture, std::memory_order_release);
    SoundManager sound(context);
    sound.reset();
    sound.getBeeperChain().setPunchEnabled(false);
    Z80* z80 = context->pCore->GetZ80();
    for (int f = 0; f < frames; ++f)
    {
        sound.setOutputDelayFrames(delayAt(f));
        z80->tt = 0;
        sound.handleFrameStart();
        if (f % 3 == 0)
            for (int i = 0; i < 100; i++)
                sound.getBeeper().handlePortOut((i & 1) ? 0x10 : 0x00, 500 + i * 600);
        z80->tt = static_cast<uint64_t>(context->config.frame) << 8;
        sound.handleStep();
        sound.handleFrameEnd();
    }
    context->pAudioCallback.store(nullptr, std::memory_order_release);
    context->pAudioManagerObj.store(nullptr, std::memory_order_release);
    return capture.frames;
}

bool Silent(const std::vector<int16_t>& v)
{
    for (int16_t s : v)
        if (s != 0)
            return false;
    return true;
}
}  // namespace

TEST(SoundManagerOutputDelay_Test, DeviceGetsTheFrameFromDelayFramesAgo)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    const auto plain = RunBeeperFrames(context, 12, [](int) { return 0; });
    const auto delayed = RunBeeperFrames(context, 12, [](int) { return 2; });
    ASSERT_EQ(plain.size(), 12u);
    ASSERT_EQ(delayed.size(), 12u);
    ASSERT_FALSE(Silent(plain[0])) << "the beeper frames must be audible for this test";
    ASSERT_FALSE(plain[0] == plain[1]) << "the frames must differ for a shift to show";

    // Filling: silence as long as the frame itself
    for (int f = 0; f < 2; ++f)
    {
        EXPECT_TRUE(Silent(delayed[f])) << "frame " << f;
        EXPECT_EQ(delayed[f].size(), plain[f].size()) << "frame " << f;
    }
    for (int f = 2; f < 12; ++f)
        EXPECT_TRUE(delayed[f] == plain[f - 2]) << "frame " << f << " must carry frame " << f - 2;

    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(SoundManagerOutputDelay_Test, ShrinkingTheDelayDropsTheOldestFrames)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    const auto plain = RunBeeperFrames(context, 10, [](int) { return 0; });
    // Delay 2 for frames 0..5, then none: frame 6 plays itself at once
    const auto out = RunBeeperFrames(context, 10, [](int f) { return f < 6 ? 2 : 0; });
    ASSERT_EQ(out.size(), 10u);
    EXPECT_TRUE(out[5] == plain[3]);
    for (int f = 6; f < 10; ++f)
        EXPECT_TRUE(out[f] == plain[f]) << "frame " << f;

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// endregion </Output delay line>
