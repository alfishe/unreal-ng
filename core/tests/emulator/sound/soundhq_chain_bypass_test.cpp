#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/iturbosounddevice.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"

/// With `soundhq` off (or the turbo low-quality override on) the character
/// chains - punch / room post-processing on the AY, FM and beeper buffers -
/// are bypassed in SoundManager::handleFrameEnd after one ramp-out frame: the
/// buffers the devices rendered reach the mixer untouched. The same holds with
/// HQ on and every effect off (owner decision 2026-10-07: zero cost and
/// bit-exact when not used). With HQ on, the AY chain (punch enabled by
/// default) reshapes the buffer. A chain that ramped out holds no audio, so
/// nothing stale replays when HQ comes back.

namespace
{
constexpr uint32_t PENTAGON_FRAME = 71680;
}

class SoundHQChainBypass_Test : public ::testing::Test
{
protected:
    SoundCardScope _turboSound{TestSound::TurboSound};  // the slot is the subject
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";
        _context = _emulator->GetContext();
        _context->config.frame = PENTAGON_FRAME;
        // The punch / room chains are the subject: AY tone voicing (which runs
        // in HQ and LQ alike) would change the buffers in the bypassed frames
        _context->config.sound.ayVoicing = FilterVoicing::Preset::Flat;
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

    /// Program AY chip 0 channel A: ~1 kHz square, full volume
    static void ProgramTone(ITurboSoundDevice& device)
    {
        auto poke = [&](uint8_t reg, uint8_t value) {
            device.portDeviceOutMethod(0xFFFD, reg);
            device.portDeviceOutMethod(0xBFFD, value);
        };
        poke(0, 109);
        poke(1, 0);
        poke(7, 0b00111110);
        poke(8, 15);
    }

    /// One frame: render the device for the full frame, snapshot the raw
    /// chip-0 buffer, then let the manager run its frame end (chains + mix).
    /// Returns true when the chain left the chip-0 buffer byte-identical.
    /// One frame as above; true when the chains left every chip buffer (AY / SSG 0 and 1, FM 0 and 1 on a TSFM)
    /// byte-identical
    bool FrameLeavesAllChipBuffersUntouched(SoundManager& sound)
    {
        ITurboSoundDevice* device = sound.getTurboSound();
        Z80* z80 = _context->pCore->GetZ80();

        sound.handleFrameStart();
        z80->t = PENTAGON_FRAME;
        sound.handleStep();
        z80->t = 0;

        const size_t bytes = device->getRenderedSamplesThisFrame() * AUDIO_CHANNELS * sizeof(int16_t);
        std::vector<int16_t*> buffers;
        for (int chip = 0; chip < device->getChipCount(); chip++)
            buffers.push_back(device->getChipBuffer(chip));
        if (device->hasFm())
            for (int chip = 0; chip < 2; chip++)
                buffers.push_back(device->getFmBuffer(chip));
        std::vector<std::vector<uint8_t>> raw;
        for (int16_t* buffer : buffers)
        {
            raw.emplace_back(bytes);
            memcpy(raw.back().data(), buffer, bytes);
        }

        sound.handleFrameEnd();
        bool same = true;
        for (size_t i = 0; i < buffers.size(); i++)
            same = same && memcmp(raw[i].data(), buffers[i], bytes) == 0;
        return same;
    }

    bool FrameLeavesChipBufferUntouched(SoundManager& sound)
    {
        ITurboSoundDevice* device = sound.getTurboSound();
        Z80* z80 = _context->pCore->GetZ80();

        sound.handleFrameStart();
        z80->t = PENTAGON_FRAME;
        sound.handleStep();
        z80->t = 0;

        const size_t bytes = device->getRenderedSamplesThisFrame() * AUDIO_CHANNELS * sizeof(int16_t);
        std::vector<uint8_t> raw(bytes);
        memcpy(raw.data(), device->getChipBuffer(0), bytes);

        sound.handleFrameEnd();
        return memcmp(raw.data(), device->getChipBuffer(0), bytes) == 0;
    }
};

TEST_F(SoundHQChainBypass_Test, ChainsSkippedWhileHQOff)
{
    SoundManager sound(_context);
    ITurboSoundDevice* device = sound.getTurboSound();
    ASSERT_NE(device, nullptr);
    ASSERT_TRUE(sound.getAYChain().isPunchEnabled()) << "AY punch is expected on by default";
    ProgramTone(*device);

    // HQ on: the AY chain reshapes the rendered buffer (settle a few frames
    // past the DC blocker / decimator warm-up first)
    ASSERT_TRUE(sound.isHQActive());
    for (int i = 0; i < 5; i++)
        FrameLeavesChipBufferUntouched(sound);
    EXPECT_FALSE(FrameLeavesChipBufferUntouched(sound)) << "with HQ on the AY chain must process the buffer";

    // HQ off via the turbo override: the first frame ramps the chain out (no
    // click), from then on it is bypassed and the raw render reaches the
    // mixer byte-identical
    sound.setTurboLowQualityOverride(true);
    ASSERT_FALSE(sound.isHQActive());
    EXPECT_FALSE(FrameLeavesChipBufferUntouched(sound)) << "the HQ-off frame ramps the chain out";
    EXPECT_TRUE(sound.getAYChain().isBypassed());
    for (int i = 0; i < 5; i++)
        EXPECT_TRUE(FrameLeavesAllChipBuffersUntouched(sound)) << "frame " << i << ": chain ran with HQ off";

    // HQ back on: processing resumes (ramping in on the first HQ frame)
    sound.setTurboLowQualityOverride(false);
    ASSERT_TRUE(sound.isHQActive());
    EXPECT_FALSE(FrameLeavesChipBufferUntouched(sound)) << "the first HQ frame ramps the chain in";
    EXPECT_FALSE(FrameLeavesChipBufferUntouched(sound)) << "the chain must run again once HQ is restored";
}

TEST_F(SoundHQChainBypass_Test, EffectsOffLeaveEveryChipBufferUntouched)
{
    // Punch and room off with Sound HQ on and off: every chain is bypassed, the AY / SSG and FM buffers reach the
    // mixer bit-identical (no int16 round trip). The FM chains are always off. Switched live: one ramp frame
    SoundManager sound(_context);
    ITurboSoundDevice* device = sound.getTurboSound();
    ASSERT_NE(device, nullptr);
    ProgramTone(*device);
    for (int i = 0; i < 3; i++)
        FrameLeavesChipBufferUntouched(sound);

    sound.setAYPunch(false);
    sound.setAYRoomMode(AudioCharacterChain::RoomMode::Off);
    sound.setBeeperPunch(false);
    EXPECT_FALSE(FrameLeavesChipBufferUntouched(sound)) << "the switch frame ramps the effects out";
    for (const bool hq : {true, false, true})
    {
        sound.setTurboLowQualityOverride(!hq);
        for (int i = 0; i < 4; i++)
            EXPECT_TRUE(FrameLeavesAllChipBuffersUntouched(sound)) << "HQ " << hq << ", frame " << i;
        EXPECT_TRUE(sound.getAYChain().isBypassed());
        EXPECT_TRUE(sound.getBeeperChain().isBypassed());
    }
}

TEST_F(SoundHQChainBypass_Test, ChainsResetWhenHQReturns)
{
    // Room mode on makes the chain stateful over ~ms: audio rendered before a
    // bypass must not echo into the first frames after HQ returns. Compare
    // against a reference manager that never left HQ and was reset at the
    // same point - identical rendering, identical (freshly cleared) chain.
    SoundManager sound(_context);
    sound.getAYChain().setRoomMode(AudioCharacterChain::RoomMode::Room_6dB);
    sound.syncAYChainSettings();
    ProgramTone(*sound.getTurboSound());

    for (int i = 0; i < 10; i++)
        FrameLeavesChipBufferUntouched(sound);  // fill the room delay line with the tone

    // Bypass, then silence the tone and let the raw render settle to flat
    // while the chain is skipped - its delay line still holds the tone. The
    // AY output coupling (one-pole high-pass) releases the tone's DC offset
    // with its time constant: 8 tau leave ~0.03% (a few LSB)
    sound.setTurboLowQualityOverride(true);
    sound.getTurboSound()->portDeviceOutMethod(0xFFFD, 8);
    sound.getTurboSound()->portDeviceOutMethod(0xBFFD, 0);
    const double tauMs = 1000.0 / (2.0 * 3.14159265358979323846 * SoundChip_AY8910::OUTPUT_HIGHPASS_HZ);
    const int settleFrames = int(std::ceil(8.0 * tauMs / 20.0));
    for (int i = 0; i < settleFrames; i++)
        FrameLeavesChipBufferUntouched(sound);

    ITurboSoundDevice* device = sound.getTurboSound();
    auto range = [&](size_t fromSample, size_t toSample) {
        const int16_t* buf = device->getChipBuffer(0);
        int32_t lo = buf[fromSample * 2], hi = lo;
        for (size_t i = fromSample * 2; i < toSample * 2; i++)
        {
            lo = std::min<int32_t>(lo, buf[i]);
            hi = std::max<int32_t>(hi, buf[i]);
        }
        return hi - lo;
    };
    ASSERT_LT(range(0, 128), 64) << "raw render did not settle to flat while bypassed";

    // First HQ frame after the bypass: a stale room delay line would echo
    // the tone at -6 dB (thousands of LSB) over its first ~2 ms (the AY room
    // delay, 88 samples at 44.1 k); a reset chain over a flat input stays
    // flat. The device clears its own decimator history on the LQ -> HQ
    // switch (ISSUES #7), so the window starts at the first sample
    sound.setTurboLowQualityOverride(false);
    FrameLeavesChipBufferUntouched(sound);
    EXPECT_LT(range(0, 84), 64) << "pre-bypass audio replayed (device decimator or stale room delay line)";
}

TEST_F(SoundHQChainBypass_Test, ChainsResetOnTtdRestore)
{
    // A TTD restore of the TurboSound device is a gap: the restored machine must not hear the room delay line's
    // audio from the timeline it left. Room at -6 dB (punch off, so the room is the only effect), the tone plays
    // and fills the delay line, then a state saved before the tone is restored. Over the first 2 ms after the
    // restore (the AY room delay: 88 samples at 44.1 kHz) a reset chain has nothing to cross-feed yet - its output
    // is the render itself (up to the processed path's x 32767 / 32768 rounding) - while a stale line would add the
    // tone at -6 dB (thousands of LSB). The render itself is not silent there: the AY output coupling releases the
    // tone's DC with its own time constant
    SoundManager sound(_context);
    sound.getAYChain().setPunchEnabled(false);
    sound.getAYChain().setRoomMode(AudioCharacterChain::RoomMode::Room_6dB);
    sound.syncAYChainSettings();
    ITurboSoundDevice* device = sound.getTurboSound();
    ASSERT_NE(device, nullptr);
    for (int i = 0; i < 3; i++)
        FrameLeavesChipBufferUntouched(sound);

    std::vector<uint8_t> silent(device->TTDStateSize());
    device->TTDSaveState(silent.data());
    const uint64_t epoch = device->renderEpoch();

    ProgramTone(*device);
    for (int i = 0; i < 10; i++)
        FrameLeavesChipBufferUntouched(sound);

    device->TTDLoadState(silent.data());
    EXPECT_NE(device->renderEpoch(), epoch) << "a restore restarts the render layers";

    // The frame after the restore, split as in FrameLeavesChipBufferUntouched: the render, then the frame end
    Z80* z80 = _context->pCore->GetZ80();
    sound.handleFrameStart();
    z80->t = PENTAGON_FRAME;
    sound.handleStep();
    z80->t = 0;
    constexpr size_t kWindow = 84;
    const std::vector<int16_t> raw(device->getChipBuffer(0), device->getChipBuffer(0) + kWindow * 2);
    sound.handleFrameEnd();
    const int16_t* out = device->getChipBuffer(0);
    int worst = 0;
    for (size_t i = 0; i < kWindow * 2; i++)
        worst = std::max(worst, std::abs(int(out[i]) - int(raw[i])));
    EXPECT_LE(worst, 2) << "audio from before the restore replayed through the room delay line";
}
