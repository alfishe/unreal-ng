#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "common/sound/filters/masterlimiter.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/soundmanager.h"

/// Master mix headroom for loud TSFM material (TSFM ISSUES.md #6).
///
/// At the board's measured FM level (kFmBaseGain, materials/volume) one chip's full FM word is 0.70 of full
/// scale, so two chips at full FM sum to 1.4 and with both SSGs flat out to over 2. The int16 saturating mix
/// hard-clipped such material; a TSFM machine mixes on the wide float bus with the master soft limiter instead.
///
/// Boot-bound (one Pentagon per test, a few frames of the full SoundManager path).

namespace
{
constexpr uint32_t PENTAGON_FRAME = 71680;
}

class TsfmHeadroom_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    SoundManager* _soundManager = nullptr;

    void Boot(TurboSoundKind kind)
    {
        _emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", kind, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _soundManager = _context->pSoundManager;
        ASSERT_NE(_soundManager, nullptr);
        ASSERT_NE(_soundManager->getTurboSound(), nullptr);
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

    void SetT(uint32_t t)
    {
        _context->pCore->GetZ80()->tt = t << 8;
    }

    void Out(uint16_t port, uint8_t value)
    {
        _soundManager->getTurboSound()->portDeviceOutMethod(port, value);
    }

    void Reg(uint8_t reg, uint8_t value)
    {
        Out(PORT_FFFD, reg);
        Out(PORT_BFFD, value);
    }

    /// One chip (control word 0xF8 / 0xF9: FM on): `channels` FM channels in algorithm 7 at the volume test's pitch
    /// (block 5, fnum 0x225, ~1165 Hz), each with `carriers` operators at TL 0 and the rest silent; optionally the
    /// three SSG tones at volume 15
    void Play(int chip, int channels, int carriers, bool ssg)
    {
        Out(PORT_FFFD, uint8_t(0xF8 | chip));
        Reg(0x2F, 0x00);  // prescaler /6
        Reg(0x2D, 0x00);
        for (int ch = 0; ch < channels; ch++)
        {
            for (int op = 0; op < 4; op++)
            {
                const uint8_t slot = uint8_t(op * 4 + ch);
                Reg(0x30 + slot, 0x01);                       // MUL 1
                Reg(0x40 + slot, op < carriers ? 0x00 : 0x7F);  // TL
                Reg(0x50 + slot, 0x1F);                       // AR 31
                Reg(0x60 + slot, 0x00);
                Reg(0x70 + slot, 0x00);
                Reg(0x80 + slot, 0x0F);  // SL 0, RR 15
            }
            Reg(0xB0 + ch, 0x07);  // algorithm 7: four carriers
            Reg(0xA4 + ch, (5 << 3) | 0x02);
            Reg(0xA0 + ch, 0x25);
            Reg(0x28, uint8_t(0xF0 | ch));
        }
        if (ssg)
        {
            for (int c = 0; c < 3; c++)
            {
                Reg(uint8_t(c * 2), 0x5E);
                Reg(uint8_t(c * 2 + 1), 0x00);
                Reg(uint8_t(8 + c), 15);
            }
            Reg(7, 0x38);  // tones on, noise off
        }
        Out(PORT_FFFD, 0xF8);
    }

    struct Levels
    {
        double sourcesPeak = 0.;  // the TSFM sources summed without clipping, of full scale
        int masterPeak = 0;
        size_t masterAtFullScale = 0;
    };

    /// Runs whole frames through the SoundManager; levels of the last `measured` frames
    Levels Run(int frames, int measured)
    {
        Levels levels;
        const AudioSourceType sources[] = {AudioSourceType::AY1_All, AudioSourceType::AY2_All, AudioSourceType::FM1,
                                           AudioSourceType::FM2};
        for (int frame = 0; frame < frames; frame++)
        {
            SetT(0);
            _soundManager->handleFrameStart();
            SetT(PENTAGON_FRAME);
            _soundManager->handleStep();
            _soundManager->handleFrameEnd();
            if (frame < frames - measured)
                continue;

            const size_t values = _soundManager->getTurboSound()->getRenderedSamplesThisFrame() * AUDIO_CHANNELS;
            const int16_t* master = _soundManager->masterMix();
            for (size_t i = 0; i < values; i++)
            {
                double sum = 0.;
                for (AudioSourceType type : sources)
                    sum += _soundManager->deviceBuffer(type)[i];
                levels.sourcesPeak = std::max(levels.sourcesPeak, std::fabs(sum) / 32768.);
                levels.masterPeak = std::max(levels.masterPeak, std::abs(int(master[i])));
                levels.masterAtFullScale += master[i] >= 32767 || master[i] <= -32768;
            }
        }
        return levels;
    }
};

TEST_F(TsfmHeadroom_Test, FmBoardMixesOnTheWideBus)
{
    Boot(TurboSoundKind::FM);
    EXPECT_TRUE(_soundManager->wideMixEnabled());
}

TEST_F(TsfmHeadroom_Test, AyBoardKeepsTheIntegerMix)
{
    // R6: a classic TurboSound (no FM) keeps the legacy integer path, byte for byte
    Boot(TurboSoundKind::AY);
    EXPECT_FALSE(_soundManager->wideMixEnabled());
}

TEST_F(TsfmHeadroom_Test, BothChipsAtFullFmDoNotClip)
{
    Boot(TurboSoundKind::FM);
    Play(0, 3, 4, true);
    Play(1, 3, 4, true);
    const Levels levels = Run(8, 4);

    // The material is past full scale at the board's levels: the old saturating mix flattened most of it
    EXPECT_GT(levels.sourcesPeak, 1.5);
    EXPECT_EQ(levels.masterAtFullScale, 0u) << "master peak " << levels.masterPeak;
    EXPECT_GT(levels.masterPeak, static_cast<int>(MasterLimiter::KNEE_LINEAR));
    EXPECT_LT(levels.masterPeak, static_cast<int>(MasterLimiter::CEILING) + 1);
}

TEST_F(TsfmHeadroom_Test, VolumeTestLevelsPassLinearly)
{
    // The volume test's sources (one carrier at TL 0, SSG at 15) on one chip: under the knee the wide bus is the plain
    // sum - the measured FM-to-SSG balance reaches the output as it is
    Boot(TurboSoundKind::FM);
    Play(0, 1, 1, true);
    const Levels levels = Run(8, 4);

    ASSERT_GT(levels.sourcesPeak, 0.2);
    ASSERT_LT(levels.sourcesPeak * 32768., MasterLimiter::KNEE_LINEAR);
    // The master's 5 Hz DC blocker shifts a ~1 kHz tone's peak by well under 1 %
    EXPECT_NEAR(levels.masterPeak, levels.sourcesPeak * 32768., levels.sourcesPeak * 32768. * 0.01);
}
