#include "stdafx.h"
#include "pch.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/iturbosounddevice.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"

/// With `soundhq` off (or the turbo low-quality override on) the character
/// chains - punch / room post-processing on the AY, FM and beeper buffers -
/// are skipped entirely in SoundManager::handleFrameEnd: the buffers the
/// devices rendered reach the mixer untouched. With HQ on, the AY chain
/// (punch enabled by default) reshapes the buffer. The chains are reset when
/// HQ comes back so no stale room / envelope state replays.

namespace
{
constexpr uint32_t PENTAGON_FRAME = 71680;
}

class SoundHQChainBypass_Test : public ::testing::Test
{
protected:
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

    // HQ off via the turbo override: the chain is bypassed, the raw render
    // reaches the mixer byte-identical
    sound.setTurboLowQualityOverride(true);
    ASSERT_FALSE(sound.isHQActive());
    for (int i = 0; i < 5; i++)
        EXPECT_TRUE(FrameLeavesChipBufferUntouched(sound)) << "frame " << i << ": chain ran with HQ off";

    // HQ back on: processing resumes
    sound.setTurboLowQualityOverride(false);
    ASSERT_TRUE(sound.isHQActive());
    FrameLeavesChipBufferUntouched(sound);  // first HQ frame resets the chains
    EXPECT_FALSE(FrameLeavesChipBufferUntouched(sound)) << "the chain must run again once HQ is restored";
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
