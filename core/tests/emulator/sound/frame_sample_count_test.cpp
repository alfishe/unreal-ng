#include "stdafx.h"
#include "pch.h"

#include <vector>

#include "_helpers/emulatortesthelper.h"
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

TEST_F(FrameSampleCount_Test, DeviceRendersExactlyWhatTheMixerConsumes)
{
    CallbackCapture capture;
    _context->pAudioCallback.store(&CallbackCapture::callback, std::memory_order_release);
    _context->pAudioManagerObj.store(&capture, std::memory_order_release);

    Z80* z80 = _context->pCore->GetZ80();
    const size_t rates[] = {44100, 48000, 96000};
    for (size_t rate : rates)
    {
        for (bool hq : {true, false})
        {
            SCOPED_TRACE(testing::Message() << "rate " << rate << (hq ? " HQ" : " LQ"));
            _context->config.sound.coreRate = static_cast<unsigned>(rate);
            SoundManager sound(_context);
            ITurboSoundDevice* device = sound.getTurboSound();
            ASSERT_NE(device, nullptr);
            device->setHQEnabled(hq);

            // A real frame ends a few T-states past the boundary (the last
            // instruction overshoots); AdjustFrameCounters rebases t by the
            // frame length before handleFrameStart. Model that: alternate
            // overshoots 0..7 T like a mix of instruction lengths does.
            int mismatches = 0;
            int firstMismatch = -1;
            uint32_t overshoot = 0;
            const int kFrames = 1500;
            for (int frame = 0; frame < kFrames; frame++)
            {
                z80->t = overshoot;  // where AdjustFrameCounters left the counter
                sound.handleFrameStart();
                // mid-frame steps, then the last instruction runs past the end
                for (uint32_t t = 500; t < PENTAGON_FRAME; t += 500)
                {
                    z80->t = t;
                    sound.handleStep();
                }
                overshoot = uint32_t((frame * 5) % 8);
                z80->t = PENTAGON_FRAME + overshoot;
                sound.handleStep();
                // AdjustFrameCounters equivalent
                z80->t = overshoot;
                sound.handleFrameEnd();

                const size_t rendered = device->getRenderedSamplesThisFrame();
                const size_t mixed = capture.lastNumSamples / 2;
                if (rendered != mixed)
                {
                    mismatches++;
                    if (firstMismatch < 0)
                        firstMismatch = frame;
                }
            }
            EXPECT_EQ(mismatches, 0) << mismatches << " of " << kFrames
                                     << " frames rendered a different count than the mixer consumed (first at frame "
                                     << firstMismatch << ")";
        }
    }
    z80->t = 0;
}
