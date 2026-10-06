#include "stdafx.h"
#include "pch.h"
#include <optional>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <memory>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/cdtestdisc.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/audiocapture/audiocaptureanalyzer.h"
#include "debugger/debugmanager.h"
#include "emulator/ports/portdecoder.h"
#ifdef UNREALNG_HAVE_OPL4
#include "emulator/sound/chips/soundchip_moonsound.h"
#endif
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/ide/ata/atapicdrom.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/storage/cd/cdimage.h"
#include "emulator/media/mediamanager.h"
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
    EXPECT_EQ(_context->config.sound.ayVoicing, Preset::Classic) << "classic is the built-in default";
    {
        SoundManager sound(_context);
        EXPECT_EQ(sound.getAYVoicing(), Preset::Classic);
        EXPECT_EQ(sound.getActiveAYVoicing(), Preset::Classic) << "no crossfade from flat at construction";
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

TEST_F(SoundManagerVoicing_Test, ConfiguredStereoSchemeReachesEveryChip)
{
    _context->config.sound.ayStereo = AYStereoMode::ACB;
    SoundManager sound(_context);
    ASSERT_GT(sound.getTurboSound()->getChipCount(), 0);
    for (int i = 0; i < sound.getTurboSound()->getChipCount(); i++)
        EXPECT_EQ(sound.getTurboSound()->getChip(i)->getStereoMode(), AYStereoMode::ACB) << "chip " << i;
}

TEST_F(SoundManagerVoicing_Test, GetReturnsRequestedPreset)
{
    SoundManager sound(_context);
    ProgramBassTone(*sound.getTurboSound());
    RunFrames(sound, 2);

    sound.setAYVoicing(Preset::Flat);
    EXPECT_EQ(sound.getAYVoicing(), Preset::Flat) << "a read right after a write shows the request";
    EXPECT_EQ(sound.getActiveAYVoicing(), Preset::Classic) << "audio switches at the next frame boundary";

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
    EXPECT_EQ(sound.getActiveAYVoicing(), Preset::Classic);
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

/// region <ATAPI CD drives (PLAN #83): one mixer row per CD drive, the line output
/// mixed sample-exact, zero work while nothing plays, the head on emulated time
/// whatever the host speed or turbo, volume / mute on the row>

namespace
{
    /// A ZX-Evo (ATM3: the CD drive on the IDE slave) with the music disc in the drive
    class SoundManagerCd_Test : public ::testing::Test
    {
    protected:
        Emulator* _emulator = nullptr;
        EmulatorContext* _context = nullptr;
        std::unique_ptr<ScratchFolder> _folder;

        void SetUp() override
        {
            _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
            ASSERT_NE(_emulator, nullptr);
            _context = _emulator->GetContext();
            _context->pSoundManager->setCoreRatePin(44100);
            _folder = std::make_unique<ScratchFolder>("sound-cd");
            MediaSource source;
            source.path = cdtest::WriteMusicDisc(_folder->Path(), 2, 2, 16, cdtest::MusicLayout::Mixed);
            InsertOptions options;
            options.immediate = true;
            const MediaResult result = _context->pMediaManager->Insert("ide0.slave", source, options);
            ASSERT_TRUE(result.Ok()) << result.message;
            _emulator->RunNFrames(1);  // the rate pin and the mixer rows settle at a frame boundary
        }
        void TearDown() override
        {
            _folder.reset();
            if (_emulator)
                EmulatorTestHelper::CleanupEmulator(_emulator);
            MessageCenter::DisposeDefaultMessageCenter();
        }

        AtapiCdrom& Cd()
        {
            AtaDevice* unit = _context->pIdeController->Channel().Unit(1);
            return *static_cast<AtapiCdrom*>(unit);
        }
        SoundManager& Sound() { return *_context->pSoundManager; }
        const AudioDeviceInfo* Row() { return Sound().device(AudioSourceType::CdAudio1); }
        /// Track 2 of the music disc: data LBA 0-15, pregap 16-165, INDEX 01 at 166
        static constexpr uint32_t kTrack2 = 166;
    };
}  // namespace

TEST_F(SoundManagerCd_Test, OneRowPerCdDrive)
{
    const AudioDeviceInfo* row = Row();
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->name, "CD ide0.slave");
    EXPECT_EQ(Sound().device(AudioSourceType::CdAudio0), nullptr) << "the master is a hard disk";
    EXPECT_EQ(IdeController::CdAudioName(3), "CD ide1.slave");

    // The unit becomes a hard disk: its row goes; back to a CD drive: it comes back
    ASSERT_TRUE(_context->pMediaManager->Eject("ide0.slave", {Disposition::Discard}).Ok());
    ASSERT_TRUE(_context->pIdeController->SetUnitKind(1, false));
    _emulator->RunNFrames(1);
    EXPECT_EQ(Row(), nullptr);
    ASSERT_TRUE(_context->pIdeController->SetUnitKind(1, true));
    _emulator->RunNFrames(1);
    EXPECT_NE(Row(), nullptr);
}

TEST_F(SoundManagerCd_Test, MixedSampleExactAndNothingWhileIdle)
{
    _emulator->RunNFrames(2);
    EXPECT_EQ(Sound().deviceBuffer(AudioSourceType::CdAudio1), nullptr) << "nothing plays: no buffer, no mixing";

    // Solo the CD row: the master mix is the drive's output, untouched
    Sound().setDeviceSolo(AudioSourceType::CdAudio1, true);
    CdImage* disc = Cd().Disc();
    ASSERT_NE(disc, nullptr);
    _emulator->RunNCPUCycles(1);  // the PLAY arrives inside a frame, as from a guest
    Cd().Audio().Play(kTrack2, kTrack2 + 150);
    std::vector<int16_t> mixed;
    for (int frame = 0; frame < 20; frame++)
    {
        _emulator->RunNFrames(1);
        const size_t samples = Sound().lastFrameSamples();
        ASSERT_GT(samples, 800u);
        const int16_t* out = Sound().deviceBuffer(AudioSourceType::MasterMix);
        mixed.insert(mixed.end(), out, out + samples * 2);
        EXPECT_TRUE(Row()->activeRecently);
    }
    // The stream is the track from INDEX 01 on, after a run-in of silence before the PLAY (it came a
    // few T-states into a frame): find where the track begins, then every sample must be the disc's
    int16_t frame[cdtest::kSamples * 2];
    auto discSample = [&](uint64_t k, int channel) {
        const uint64_t index = static_cast<uint64_t>(kTrack2) * cdtest::kSamples + k;
        disc->ReadAudio(static_cast<uint32_t>(index / cdtest::kSamples), frame);
        return frame[(index % cdtest::kSamples) * 2 + channel];
    };
    size_t start = 0;
    for (; start < 8; start++)
    {
        bool match = true;
        for (uint64_t k = 0; k < 64 && match; k++)
            match = mixed[2 * (start + k)] == discSample(k, 0);
        if (match)
            break;
    }
    ASSERT_LT(start, 8u) << "the track starts within the first samples";
    for (size_t i = 0; i < start; i++)
        EXPECT_EQ(mixed[2 * i], 0) << "silence before the PLAY";
    for (size_t i = start; i < mixed.size() / 2; i++)
    {
        const uint64_t k = i - start;
        if (k % cdtest::kSamples == 0 || i == start)
            ASSERT_EQ(disc->ReadAudio(static_cast<uint32_t>(kTrack2 + k / cdtest::kSamples), frame), CdImage::ReadResult::Ok);
        const size_t at = (k % cdtest::kSamples) * 2;
        ASSERT_EQ(mixed[2 * i], frame[at]) << "sample " << i;
        ASSERT_EQ(mixed[2 * i + 1], frame[at + 1]) << "sample " << i;
    }

    // Mute and volume act on the row
    Sound().setDeviceSolo(AudioSourceType::CdAudio1, false);
    Sound().setDeviceVolume(AudioSourceType::CdAudio1, 0.0f);
    _emulator->RunNFrames(1);
    EXPECT_NE(Sound().deviceBuffer(AudioSourceType::CdAudio1), nullptr) << "it still plays";
    EXPECT_GT(Row()->peak, 0.1f) << "the meter sees the drive";

    Cd().Audio().Stop();
    _emulator->RunNFrames(1);
    EXPECT_EQ(Sound().deviceBuffer(AudioSourceType::CdAudio1), nullptr);
    EXPECT_EQ(Row()->peak, 0.0f);
}

TEST_F(SoundManagerCd_Test, HeadRunsOnEmulatedTimeAtAnySpeed)
{
    // The head moves frame x 44100 / 3.5 MHz samples per frame: at the base clock,
    // at a 4x host multiplier (the CPU runs 4x the T-states in each frame) and in turbo
    const uint32_t frame = _context->config.frame;
    auto run = [&](int frames) {
        Cd().Audio().Play(kTrack2, kTrack2 + 150);
        const uint64_t start = Cd().Audio().HeadSample();
        _emulator->RunNFrames(frames);
        return Cd().Audio().HeadSample() - start;
    };
    _emulator->RunNFrames(1);  // a frame boundary: PLAY at elapsed 0
    const uint64_t base = run(10);
    EXPECT_EQ(base, static_cast<uint64_t>(10) * frame * 44100 / 3500000);

    ASSERT_TRUE(_emulator->SetSpeedMultiplier(4));
    _emulator->RunNFrames(1);
    EXPECT_EQ(run(10), base) << "4x host speed: the same emulated time per frame";
    ASSERT_TRUE(_emulator->SetSpeedMultiplier(1));
    _emulator->RunNFrames(1);

    _emulator->EnableTurboMode(false);
    _emulator->RunNFrames(1);
    EXPECT_EQ(run(10), base) << "turbo without audio: the head still moves";
    EXPECT_EQ(Sound().deviceBuffer(AudioSourceType::CdAudio1), nullptr) << "nothing rendered in turbo";
    _emulator->DisableTurboMode();
}

/// endregion </ATAPI CD drives>

/// Host output hold (SoundManager::HostOutputHold): a run not paced to real time - API run_frames and the other
/// direct runs, TTD seek / replay, turbo - hands nothing to the host audio callback, while every device still
/// renders exactly what it renders at normal speed (TTD determinism: only the host boundary changes).
class SoundManagerHostOutput_Test : public ::testing::Test
{
protected:
    SoundCardScope _turboSound{TestSound::TurboSound};
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    struct Sink
    {
        size_t calls = 0;
        size_t nonSilentCalls = 0;
    };
    Sink _sink;

    static void Collect(void* obj, int16_t* samples, size_t count)
    {
        Sink* sink = static_cast<Sink*>(obj);
        sink->calls++;
        for (size_t i = 0; i < count; i++)
        {
            if (samples[i] != 0)
            {
                sink->nonSilentCalls++;
                break;
            }
        }
    }

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";
        _context = _emulator->GetContext();
        _context->config.frame = PENTAGON_FRAME;
        _context->pAudioManagerObj.store(&_sink, std::memory_order_release);
        _context->pAudioCallback.store(&Collect, std::memory_order_release);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    /// A fresh sound stack playing an AY tone, a beeper square and a Covox swing
    std::unique_ptr<SoundManager> MakeSound()
    {
        auto sound = std::make_unique<SoundManager>(_context);
        sound->reset();
        ProgramTone(*sound->getTurboSound());
        return sound;
    }

    static void ProgramTone(ITurboSoundDevice& device)
    {
        auto poke = [&](uint8_t reg, uint8_t value) {
            device.portDeviceOutMethod(0xFFFD, reg);
            device.portDeviceOutMethod(0xBFFD, value);
        };
        poke(0, 0x40);
        poke(1, 0x00);
        poke(7, 0b00111110);
        poke(8, 15);
    }

    /// One frame as the main loop runs it, beeper toggling inside; returns the mixed output of the frame
    std::vector<int16_t> RunFrame(SoundManager& sound)
    {
        Z80* z80 = _context->pCore->GetZ80();
        z80->tt = 0;
        sound.handleFrameStart();
        for (int i = 0; i < 64; i++)
            sound.getBeeper().handlePortOut((i & 1) ? 0x10 : 0x00, 500 + i * 1000);
        z80->tt = static_cast<uint64_t>(PENTAGON_FRAME / 2) << 8;
        sound.handleStep();
        z80->tt = static_cast<uint64_t>(PENTAGON_FRAME) << 8;
        sound.handleStep();
        sound.handleFrameEnd();

        const AudioFrameDescriptor& desc = sound.getAudioBufferDescriptor();
        const int16_t* out = reinterpret_cast<const int16_t*>(desc.memoryBuffer);
        return std::vector<int16_t>(out, out + MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS);
    }
};

/// Held frames never reach the host, and the machine computes bit-identical samples held or not
TEST_F(SoundManagerHostOutput_Test, HeldFramesReachNoHostAndRenderIdentically)
{
    constexpr int kFrames = 4;

    std::vector<std::vector<int16_t>> live;
    {
        auto sound = MakeSound();
        for (int i = 0; i < kFrames; i++)
            live.push_back(RunFrame(*sound));
        EXPECT_EQ(sound->hostFramesDelivered(), static_cast<uint64_t>(kFrames));
        EXPECT_GT(sound->hostFramesAudible(), 0u) << "control: the drive must be audible when not held";
        EXPECT_EQ(sound->hostFramesHeld(), 0u);
    }
    ASSERT_EQ(_sink.calls, static_cast<size_t>(kFrames));
    ASSERT_GT(_sink.nonSilentCalls, 0u) << "control: the host got sound when not held";

    _sink = Sink{};
    auto sound = MakeSound();
    using Reason = SoundManager::HostHoldReason;
    std::optional<SoundManager::HostOutputHold> replay(std::in_place, sound.get(), Reason::TtdReplay);
    std::optional<SoundManager::HostOutputHold> run(std::in_place, sound.get(), Reason::DirectRun);  // nested
    EXPECT_TRUE(sound->isHostOutputHeld());
    for (int i = 0; i < kFrames; i++)
    {
        EXPECT_EQ(RunFrame(*sound), live[i]) << "held frame " << i << " rendered differently";
    }
    EXPECT_EQ(_sink.calls, 0u) << "a held frame reached the host audio callback";
    EXPECT_EQ(sound->hostFramesDelivered(), 0u);
    EXPECT_EQ(sound->hostFramesHeld(), static_cast<uint64_t>(kFrames));

    // One release still leaves the outer hold; the last one restores delivery; a second release is harmless
    run.reset();
    EXPECT_TRUE(sound->isHostOutputHeld());
    replay->Release();
    replay->Release();
    EXPECT_FALSE(sound->isHostOutputHeld());
    RunFrame(*sound);
    EXPECT_EQ(_sink.calls, 1u) << "delivery did not resume after the last release";
    EXPECT_EQ(sound->hostFramesDelivered(), 1u);
}

/// The guard is the only way to hold: keyed by reason, each guard gives back exactly what it took, once - moved,
/// released twice, or destroyed - and every reason is reported on its own
TEST_F(SoundManagerHostOutput_Test, GuardsHoldPerReasonAndReleaseExactlyOnce)
{
    using Reason = SoundManager::HostHoldReason;
    auto sound = MakeSound();
    {
        SoundManager::HostOutputHold a(sound.get(), Reason::DirectRun);
        SoundManager::HostOutputHold b(sound.get(), Reason::DirectRun);
        SoundManager::HostOutputHold turboHold(sound.get(), Reason::Turbo);
        EXPECT_EQ(sound->hostOutputHolds(Reason::DirectRun), 2);
        EXPECT_EQ(sound->hostOutputHolds(Reason::Turbo), 1);
        EXPECT_EQ(sound->hostOutputHolds(Reason::TtdReplay), 0);

        SoundManager::HostOutputHold moved(std::move(a));  // the hold travels, it is not doubled
        EXPECT_FALSE(a.IsHeld());
        EXPECT_EQ(sound->hostOutputHolds(Reason::DirectRun), 2);
        moved = std::move(b);  // the hold it had goes back first
        EXPECT_EQ(sound->hostOutputHolds(Reason::DirectRun), 1);
        moved.Release();
        moved.Release();
        EXPECT_EQ(sound->hostOutputHolds(Reason::DirectRun), 0);
        EXPECT_TRUE(sound->isHostOutputHeld()) << "the turbo hold is its own";
    }
    EXPECT_FALSE(sound->isHostOutputHeld()) << "a guard going out of scope must give its hold back";
    EXPECT_EQ(sound->hostOutputHoldsTaken(Reason::DirectRun), 2u);
    EXPECT_EQ(sound->hostOutputHoldsTaken(Reason::Turbo), 1u);
    SoundManager::HostOutputHold none(nullptr, Reason::Turbo);  // no sound manager: holds nothing
    EXPECT_FALSE(none.IsHeld());
}

/// A resume drops exactly the holds whose reason is not in effect (a leak) and keeps the live ones; the leaked
/// guard's late release then takes nothing from a newer hold of the same reason
TEST_F(SoundManagerHostOutput_Test, ReconcileDropsOnlyStaleHolds)
{
    using Reason = SoundManager::HostHoldReason;
    auto sound = MakeSound();
    auto leaked = std::make_unique<SoundManager::HostOutputHold>(sound.get(), Reason::DirectRun);
    SoundManager::HostOutputHold replay(sound.get(), Reason::TtdReplay);

    EXPECT_EQ(sound->reconcileHostOutputHolds(false, true, false), 1);
    EXPECT_EQ(sound->hostOutputHolds(Reason::DirectRun), 0);
    EXPECT_EQ(sound->hostOutputHolds(Reason::TtdReplay), 1) << "a replay in progress keeps its hold";
    EXPECT_EQ(sound->hostOutputStaleHoldsCleared(), 1u);

    SoundManager::HostOutputHold fresh(sound.get(), Reason::DirectRun);
    leaked.reset();  // the stale guard's release is void: it must not end the fresh run's hold
    EXPECT_EQ(sound->hostOutputHolds(Reason::DirectRun), 1);
    fresh.Release();
    replay.Release();
    EXPECT_FALSE(sound->isHostOutputHeld());
    EXPECT_EQ(sound->reconcileHostOutputHolds(false, false, false), 0) << "nothing left to drop";

    _sink = Sink{};
    RunFrame(*sound);
    EXPECT_EQ(_sink.calls, 1u);
    EXPECT_GT(_sink.nonSilentCalls, 0u);
}

/// region <Device output position across a host speed multiplier>

// The mixer's frame has the base frame's samples at every host speed (SoundManager::handleFrameEnd: the excess of a
// faster frame is dropped knowingly). A device that renders a frame's time into its own stream must come back to that
// frame grid at 1x: the same count as the mixer (the beeper reads its own count), and no backlog left in its stream
// (the Covox / SounDrive, the MoonSound read the mixer's count from a stream that a faster frame filled with more).
// Content check: a marker written at a T-state of a frame lands at the same sample of that frame's output before and
// after the event.
namespace devicemarker
{

enum class Event
{
    HostSpeed,    ///< x2 for four frames, back to x1
    HostSpeedX4,  ///< x4 for three frames, back to x1
    SoundOff      ///< the sound feature off for four frames, back on
};

/// A Pentagon with its [SLOTS] replaced; the audio capture analyzer on; the CPU parked in DI; HALT
class MarkerMachine
{
public:
    MarkerMachine(const std::string& slots, TestSound devices) : _scope(devices)
    {
        const std::filesystem::path source = TestPathHelper::FindProjectRoot() / "data/configs/pentagon128k/unreal.ini";
        std::ifstream in(source, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t shipped = text.find("\n[SLOTS]");
        if (shipped != std::string::npos)
        {
            const size_t next = text.find("\n[", shipped + 1);
            text.erase(shipped, next == std::string::npos ? std::string::npos : next - shipped);
        }
        text += "\n[SLOTS]\n" + slots + "\n";
        _path = TestPathHelper::GetUniqueTestScratchPath("devicemarker-pentagon.ini");
        std::ofstream out(_path, std::ios::binary);
        out << text;
        out.close();
        _emulator = std::make_unique<Emulator>(LoggerLevel::LogError);
        _emulator->SetCustomConfigPath(_path.string());
        _ok = _emulator->Init();
        if (_ok)
        {
            AnalyzerManager* analyzers = Context()->pDebugManager->GetAnalyzerManager();
            analyzers->activate("audiocapture");
            _capture = analyzers->getAnalyzer<AudioCaptureAnalyzer>("audiocapture");
            Z80* z80 = Context()->pCore->GetZ80();
            z80->DirectWrite(0x8000, 0xF3);
            z80->DirectWrite(0x8001, 0x76);
            z80->pc = 0x8000;
        }
    }
    ~MarkerMachine()
    {
        _emulator->Release();
        std::error_code ignored;
        std::filesystem::remove(_path, ignored);
    }

    bool Ok() const { return _ok && _capture != nullptr; }
    Emulator& Machine() { return *_emulator; }
    EmulatorContext* Context() const { return _emulator->GetContext(); }

    void Out(uint16_t port, uint8_t value)
    {
        Context()->pCore->GetZ80()->m1_pc = 0x8000;
        Context()->pPortDecoder->WriteCycle(port, value, 0x8000);
    }

    /// One frame; with `check`, the beeper delivered the mixer's count
    void Frame(bool check, const std::string& where)
    {
        Machine().RunFrame(true);
        if (check)
            EXPECT_EQ(size_t(Context()->pSoundManager->getBeeper().getLastSamplesRead()),
                      Context()->pSoundManager->lastFrameSamples())
                << where << ", frame " << Context()->emulatorState.frame_counter
                << ": the beeper delivered a different count than the mixer read";
    }
    void Frames(int n, bool check, const std::string& where)
    {
        for (int i = 0; i < n; i++)
            Frame(check, where);
    }

    /// The sample of a frame's output of `source` where a marker switched on at T 20000 and off 1000 T later shows:
    /// the first sample whose distance from the frame's first sample exceeds half the largest one; -1 without one
    int MarkerPosition(AudioSourceType source, const std::function<void(bool)>& marker, const std::string& where)
    {
        Z80& z80 = *Context()->pCore->GetZ80();
        const uint32_t frameT = Context()->config.frame;
        _capture->startCapture(2 * 2 * Context()->pSoundManager->lastFrameSamples(), source);
        Machine().RunTStates(20000 - z80.t);
        marker(true);
        Machine().RunTStates(1000);
        marker(false);
        Machine().RunTStates(frameT - z80.t);
        for (int guard = 0; guard < 8 && !_capture->isCaptureComplete(); guard++)
            Frame(true, where);
        const std::vector<int16_t>& buffer = _capture->getBuffer();
        const size_t samples = _capture->getCapturedSamples() / 2;
        int position = -1;
        double largest = 0.0;
        for (size_t i = 0; i < samples; i++)
            largest = std::max(largest, std::abs(double(buffer[i * 2]) - double(buffer[0])));
        for (size_t i = 0; i < samples && largest > 200.0; i++)
        {
            if (std::abs(double(buffer[i * 2]) - double(buffer[0])) > largest / 2)
            {
                position = int(i);
                break;
            }
        }
        _capture->stopCapture();
        Frames(6, true, where);   // the marker's tail and the stale-channel decay pass
        return position;
    }

    void RunEvent(Event event, const std::string& where)
    {
        switch (event)
        {
            case Event::HostSpeed:
                ASSERT_TRUE(Machine().SetSpeedMultiplier(2));
                Frames(4, false, where);
                ASSERT_TRUE(Machine().SetSpeedMultiplier(1));
                break;
            case Event::HostSpeedX4:
                ASSERT_TRUE(Machine().SetSpeedMultiplier(4));
                Frames(3, false, where);
                ASSERT_TRUE(Machine().SetSpeedMultiplier(1));
                break;
            case Event::SoundOff:
                Machine().GetFeatureManager()->setFeature(Features::kSoundGeneration, false);
                Frames(4, false, where);
                Machine().GetFeatureManager()->setFeature(Features::kSoundGeneration, true);
                break;
        }
        Frames(1, false, where);   // the frame the switch back falls in
        Frames(3, true, where + " (settle)");
    }

private:
    SoundCardScope _scope;
    std::filesystem::path _path;
    std::unique_ptr<Emulator> _emulator;
    bool _ok = false;
    AudioCaptureAnalyzer* _capture = nullptr;
};

const char* EventName(Event e)
{
    switch (e)
    {
        case Event::HostSpeed: return "HostSpeed";
        case Event::HostSpeedX4: return "HostSpeedX4";
        case Event::SoundOff: return "SoundOff";
    }
    return "?";
}

} // namespace devicemarker

/// Beeper and SounDrive (both port sets) on a Pentagon: before and after each event a marker lands on the same sample
/// of its frame, and the beeper delivers the mixer's count every frame (~100 ms: one machine, ~60 frames per event)
TEST(SoundManagerDeviceMarker_Test, BeeperAndSoundriveMarkersStayOnTheFrameGrid)
{
    using namespace devicemarker;
    for (const Event event : {Event::HostSpeed, Event::HostSpeedX4, Event::SoundOff})
    {
        const std::string where = EventName(event);
        MarkerMachine m("ay-socket = none\nzxbus.1 = soundrive\nzxbus.1.mode = both", TestSound::TurboSound);
        ASSERT_TRUE(m.Ok()) << where;
        ASSERT_NE(m.Context()->pSoundManager->getCovox(), nullptr) << where;
        m.Frames(3, true, where + " (start)");
        const auto beeper = [&m](bool on) { m.Out(0x00FE, on ? 0x10 : 0x00); };
        const auto soundrive = [&m](bool on) { m.Out(0x000F, on ? 0xFF : 0x80); };

        const int beeperBefore = m.MarkerPosition(AudioSourceType::Beeper, beeper, where + " beeper (before)");
        const int soundriveBefore = m.MarkerPosition(AudioSourceType::COVOX, soundrive, where + " SounDrive (before)");
        ASSERT_GT(beeperBefore, 0) << where;
        ASSERT_GT(soundriveBefore, 0) << where;

        m.RunEvent(event, where);
        EXPECT_NEAR(m.MarkerPosition(AudioSourceType::Beeper, beeper, where + " beeper (after)"), beeperBefore, 1)
            << where << ": the beeper marker moved";
        EXPECT_NEAR(m.MarkerPosition(AudioSourceType::COVOX, soundrive, where + " SounDrive (after)"), soundriveBefore, 1)
            << where << ": the SounDrive marker moved (a backlog in its stream)";
    }
}

#ifdef UNREALNG_HAVE_OPL4
/// The MoonSound's FM: a note keyed on at T 20000 of a frame starts on the same sample of that frame's output before
/// and after each event (~150 ms: the OPL4 renders every frame)
TEST(SoundManagerDeviceMarker_Test, MoonSoundMarkerStaysOnTheFrameGrid)
{
    using namespace devicemarker;
    for (const Event event : {Event::HostSpeed, Event::HostSpeedX4, Event::SoundOff})
    {
        const std::string where = EventName(event);
        MarkerMachine m("ay-socket = none\nzxbus.1 = moonsound", TestSound::TurboSound | TestSound::MoonSound);
        ASSERT_TRUE(m.Ok()) << where;
        SoundChip_Moonsound* moonsound = m.Context()->pSoundManager->getMoonSound();
        ASSERT_NE(moonsound, nullptr) << where;
        auto fm = [moonsound](uint8_t reg, uint8_t value)
        {
            moonsound->portDeviceOutMethod(0xC4, reg);
            moonsound->portDeviceOutMethod(0xC5, value);
        };
        // Channel 0: a sine carrier at full level, fastest attack and release
        for (const auto& [r, v] : std::initializer_list<std::pair<uint8_t, uint8_t>>{
                 {0x20, 0x01}, {0x23, 0x01}, {0x40, 0x3F}, {0x43, 0x00}, {0x60, 0xFF}, {0x63, 0xFF},
                 {0x80, 0x0F}, {0x83, 0x0F}, {0xE0, 0x00}, {0xE3, 0x00}, {0xC0, 0x31}, {0xA0, 0x41}})
            fm(r, v);
        m.Frames(3, true, where + " (start)");
        const auto note = [&fm](bool on) { fm(0xB0, on ? 0x32 : 0x12); };

        const int before = m.MarkerPosition(AudioSourceType::Moonsound_FM, note, where + " (before)");
        ASSERT_GT(before, 0) << where;
        m.RunEvent(event, where);
        EXPECT_NEAR(m.MarkerPosition(AudioSourceType::Moonsound_FM, note, where + " (after)"), before, 1)
            << where << ": the MoonSound note moved (a backlog in its stream)";
    }
}
#endif  // UNREALNG_HAVE_OPL4

/// endregion </Device output position across a host speed multiplier>
