#include "stdafx.h"
#include "pch.h"

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/iturbosounddevice.h"
#include "emulator/sound/soundmanager.h"

/// The mixer consumes exactly `samplesThisFrame` samples per frame from every
/// source buffer (SoundManager::handleFrameEnd, exact integer accumulator over
/// the frame length). The TurboSound device fills its buffers from its own
/// free-running sample PLL. When the two disagree for a frame the mixer reads
/// zeros past the device's last rendered sample (the buffers are cleared at
/// frame start), which is a one-sample dropout in the master mix - an
/// audible click at the beat frequency of the two accumulators (found on the
/// 2026-09-13 recordings of the Moe-bius tune: one exact-zero sample every 6
/// frames, 5419 samples apart at 44.1 kHz, with the HQ chain on and off).
///
/// Contract pinned here: the device's rendered count equals the mixer's count
/// on EVERY frame, at every supported core rate, in HQ and LQ.

namespace
{
constexpr uint32_t PENTAGON_FRAME = 71680;

struct CallbackCapture
{
    size_t lastNumSamples = 0;
    static void callback(void* obj, int16_t* samples, size_t numSamples)
    {
        (void)samples;
        static_cast<CallbackCapture*>(obj)->lastNumSamples = numSamples;
    }
};
}  // namespace

class FrameSampleCount_Test : public ::testing::Test
{
protected:
    SoundCardScope _turboSound{TestSound::TurboSound};  // the slot is the subject
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _context->config.frame = PENTAGON_FRAME;
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            _context->config.sound.coreRate = 0;
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

/// Count the frames whose device-rendered sample count differs from what the
/// mixer consumed, over `frames` frames of `frameT` T-states. A real frame ends
/// a few T-states past the boundary (the last instruction overshoots);
/// AdjustFrameCounters rebases t by the frame length before handleFrameStart.
/// Modelled with alternating overshoots 0..7 T, like a mix of instruction
/// lengths. The machine config carries the frame length and the rounded-up
/// pacing duration exactly as config loading sets them.
static int CountMismatches(EmulatorContext* context, uint32_t frameT, size_t rate, bool hq, int frames, int* firstMismatch)
{
    CallbackCapture capture;
    context->pAudioCallback.store(&CallbackCapture::callback, std::memory_order_release);
    context->pAudioManagerObj.store(&capture, std::memory_order_release);
    context->config.frame = frameT;
    context->config.frame_duration_us = CalculateFrameDurationUs(frameT);
    context->config.sound.coreRate = static_cast<unsigned>(rate);

    Z80* z80 = context->pCore->GetZ80();
    int mismatches = 0;
    {
        SoundManager sound(context);
        ITurboSoundDevice* device = sound.getTurboSound();
        EXPECT_NE(device, nullptr);
        if (!device)
            return -1;
        device->setHQEnabled(hq);

        uint32_t overshoot = 0;
        for (int frame = 0; frame < frames; frame++)
        {
            z80->t = overshoot;  // where AdjustFrameCounters left the counter
            sound.handleFrameStart();
            // mid-frame steps, then the last instruction runs past the end
            for (uint32_t t = 500; t < frameT; t += 500)
            {
                z80->t = t;
                sound.handleStep();
            }
            overshoot = uint32_t((frame * 5) % 8);
            z80->t = frameT + overshoot;
            sound.handleStep();
            // AdjustFrameCounters equivalent
            z80->t = overshoot;
            sound.handleFrameEnd();

            if (device->getRenderedSamplesThisFrame() != capture.lastNumSamples / 2 && mismatches++ == 0)
                *firstMismatch = frame;
        }
    }
    z80->t = 0;
    context->pAudioCallback.store(nullptr, std::memory_order_release);
    context->pAudioManagerObj.store(nullptr, std::memory_order_release);
    return mismatches;
}

TEST_F(FrameSampleCount_Test, DeviceRendersExactlyWhatTheMixerConsumes)
{
    // Frames per combination = the sample accumulator's fractional-pattern
    // period plus margin, so every fractional phase is visited (2 periods at
    // the short-period rates). Pentagon frame = 71680 T: @44100 that is 903.168 samples (0.168 =
    // 21/125 -> period 125 frames), @48000 983.04 and @96000 1966.08 (both
    // x/25 -> period 25 frames). The pre-fix code disagreed on ~30% of frames
    // from frame 4 on, so detection needs far fewer than any of these; the
    // full rate x mode matrix is covered by the multirate suites. Rendering is
    // real DSP (HQ runs the native-clock FIR path), so cost is linear in frames.
    struct Combo { size_t rate; bool hq; int frames; };
    const Combo combos[] = {{44100, true, 130}, {44100, false, 130}, {48000, true, 50}, {96000, false, 50}};
    for (const Combo& combo : combos)
    {
        SCOPED_TRACE(testing::Message() << "rate " << combo.rate << (combo.hq ? " HQ" : " LQ"));
        int first = -1;
        const int mismatches = CountMismatches(_context, PENTAGON_FRAME, combo.rate, combo.hq, combo.frames, &first);
        EXPECT_EQ(mismatches, 0) << mismatches << " of " << combo.frames
                                 << " frames rendered a different count than the mixer consumed (first at frame "
                                 << first << ")";
    }
}

TEST_F(FrameSampleCount_Test, DeviceMatchesMixerOnEveryShippedFrameLength)
{
    // The mixer counted from config.frame_duration_us, which is rounded UP to
    // whole microseconds; the devices count T-states. Where the frame is not a
    // whole number of microseconds (70908 T = 20259.43 us on 128K/+3, 99880 T
    // = 28537.14 us on ATM) the two disagreed on ~every other frame - the
    // mixer read a never-rendered zero sample or dropped one. 69888 (48K,
    // Scorpion) is exact and pins the unaffected case. Both TurboSound-slot
    // devices share the accumulator; the disagreement showed from the first
    // frames, so 60 frames per case is plenty (LQ keeps it cheap). Over the
    // 50 ms budget (~260 ms): 18 cases of real frame rendering on two devices.
    const uint32_t frameLengths[] = {69888, 70908, 99880};
    for (const TurboSoundKind kind : {TurboSoundKind::FM, TurboSoundKind::AY})
    {
        Emulator* emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", kind, LoggerLevel::LogError);
        ASSERT_NE(emulator, nullptr);
        for (const uint32_t frameT : frameLengths)
        {
            for (const size_t rate : {size_t(44100), size_t(48000), size_t(96000)})
            {
                SCOPED_TRACE(testing::Message() << (kind == TurboSoundKind::FM ? "TSFM" : "TurboSound") << " frame "
                                                << frameT << " rate " << rate);
                int first = -1;
                const int mismatches = CountMismatches(emulator->GetContext(), frameT, rate, false, 60, &first);
                EXPECT_EQ(mismatches, 0) << mismatches << " of 60 frames rendered a different count than the mixer "
                                         << "consumed (first at frame " << first << ")";
            }
        }
        emulator->GetContext()->config.sound.coreRate = 0;
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

TEST_F(FrameSampleCount_Test, BeeperMatchesMixerOnEveryShippedFrameLength)
{
    // The beeper's blip_buf is clocked in T-states and closes each frame at
    // config.frame; SoundManager pads a shortfall with the last sample and
    // drops an excess. With the mixer counting in rounded-up microseconds the
    // two disagreed by one sample on ~half the frames of a 70908 T (128K/+3)
    // or 99880 T (ATM) machine from the first frames on; counting in T-states
    // they are in lockstep. Over the 50 ms budget (~140 ms): 12 cases of real
    // frame rendering; fewer frames would miss the rarer 96 kHz disagreement.
    struct CountCapture
    {
        size_t lastNumSamples = 0;
        static void Callback(void* obj, int16_t*, size_t numSamples)
        {
            static_cast<CountCapture*>(obj)->lastNumSamples = numSamples;
        }
    } capture;
    _context->pAudioCallback.store(&CountCapture::Callback, std::memory_order_release);
    _context->pAudioManagerObj.store(&capture, std::memory_order_release);
    Z80* z80 = _context->pCore->GetZ80();

    for (const uint32_t frameT : {69888u, 70908u, 71680u, 99880u})
    {
        for (const unsigned rate : {44100u, 48000u, 96000u})
        {
            SCOPED_TRACE(testing::Message() << "frame " << frameT << " rate " << rate);
            _context->config.frame = frameT;
            _context->config.frame_duration_us = CalculateFrameDurationUs(frameT);
            _context->config.sound.coreRate = rate;
            SoundManager sound(_context);
            sound.setTurboLowQualityOverride(true);  // the device's HQ DSP is irrelevant here, and costly
            int mismatches = 0;
            for (int f = 0; f < 40; f++)
            {
                z80->t = 0;
                sound.handleFrameStart();
                z80->t = frameT;
                sound.handleStep();
                z80->t = 0;
                sound.handleFrameEnd();
                if (size_t(sound.getBeeper().getLastSamplesRead()) != capture.lastNumSamples / 2)
                    mismatches++;
            }
            EXPECT_EQ(mismatches, 0) << mismatches << " of 40 frames: beeper delivered a different count";
        }
    }
    z80->t = 0;
}
