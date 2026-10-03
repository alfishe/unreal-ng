#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "emulator/sound/soundmanager.h"

/// The AY clock at run time (SoundChip_TurboSound::SetPsgClock, Profi hi-res phase H2b in
/// docs/inprogress/2026-10-01-profi-v3-v5/design-hires.md): the generators follow the clock from the T-state of
/// the switch, the output carries on without a click, and the clock state survives a TTD save / load.
///
/// The device is driven directly, the way multirate_test.cpp does it: a frame = handleFrameStart at T 0, then one
/// handleStep at the frame end.
class SoundChipTurboSound_Test : public ::testing::Test
{
protected:
    static constexpr uint32_t kFrame = 71680;  // Pentagon
    static constexpr uint32_t kHiresClock = 1'500'000;

    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", TurboSoundKind::AY);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _context->config.frame = kFrame;
        _z80->t = 0;
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _z80->t = 0;
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    static SoundChip_TurboSound* Device(SoundManager& sound)
    {
        return dynamic_cast<SoundChip_TurboSound*>(sound.getTurboSound());
    }

    static void Poke(SoundChip_TurboSound* device, uint8_t reg, uint8_t value)
    {
        device->portDeviceOutMethod(0xFFFD, reg);
        device->portDeviceOutMethod(0xBFFD, value);
    }

    /// Channel A: tone of the given period at full volume, or (period 0) the tone off - a constant level
    static void ProgramChannelA(SoundChip_TurboSound* device, uint16_t period)
    {
        Poke(device, 0, static_cast<uint8_t>(period & 0xFF));
        Poke(device, 1, static_cast<uint8_t>(period >> 8));
        Poke(device, 7, period ? 0b00111110 : 0b00111111);
        Poke(device, 8, 15);
    }

    void StartFrame(SoundChip_TurboSound* device)
    {
        _z80->t = 0;
        device->handleFrameStart();
    }

    void StepTo(SoundChip_TurboSound* device, uint32_t t)
    {
        _z80->t = t;
        device->handleStep();
    }

    /// Left channel of chip 0, frame after frame
    std::vector<int16_t> RenderFrames(SoundChip_TurboSound* device, int frames)
    {
        std::vector<int16_t> stream;
        for (int f = 0; f < frames; f++)
        {
            StartFrame(device);
            StepTo(device, kFrame);
            AppendFrame(device, stream);
        }
        return stream;
    }

    static void AppendFrame(SoundChip_TurboSound* device, std::vector<int16_t>& stream)
    {
        const size_t samples = device->getRenderedSamplesThisFrame();
        const int16_t* buffer = device->getChipBuffer(0);
        for (size_t i = 0; i < samples; i++)
            stream.push_back(buffer[i * 2]);
    }

    /// Midline crossings with hysteresis (multirate_test.cpp)
    static uint64_t Crossings(const std::vector<int16_t>& stream)
    {
        const auto [lo, hi] = std::minmax_element(stream.begin(), stream.end());
        const int32_t mid = (int32_t(*lo) + int32_t(*hi)) / 2;
        const int32_t hysteresis = (int32_t(*hi) - int32_t(*lo)) / 8;
        uint64_t crossings = 0;
        int state = 0;
        for (int16_t s : stream)
        {
            if (s > mid + hysteresis && state <= 0)
            {
                crossings += state < 0 ? 1 : 0;
                state = 1;
            }
            else if (s < mid - hysteresis && state >= 0)
            {
                crossings += state > 0 ? 1 : 0;
                state = -1;
            }
        }
        return crossings;
    }

    static std::vector<uint8_t> Save(const SoundChip_TurboSound* device)
    {
        std::vector<uint8_t> blob(device->TTDStateSize());
        device->TTDSaveState(blob.data());
        return blob;
    }
};

/// 1.75 MHz unless the machine says otherwise; requests are rounded to 100 Hz and checked against the range a
/// clock marker carries
TEST_F(SoundChipTurboSound_Test, DefaultClockAndRequestValidation)
{
    SoundManager sound(_context);
    SoundChip_TurboSound* device = Device(sound);
    ASSERT_NE(device, nullptr);

    EXPECT_EQ(device->GetPsgClock(), PSG_CLOCK_RATE);
    EXPECT_EQ(sound.GetPsgClock(), PSG_CLOCK_RATE);
    EXPECT_TRUE(device->SetPsgClock(PSG_CLOCK_RATE)) << "the current clock again is accepted";

    EXPECT_FALSE(device->SetPsgClock(0));
    EXPECT_FALSE(device->SetPsgClock(SoundChip_TurboSound::kMaxPsgClock + 100));
    EXPECT_EQ(device->GetRequestedPsgClock(), PSG_CLOCK_RATE) << "a refused request changes nothing";

    EXPECT_TRUE(sound.SetPsgClock(1'500'049));
    EXPECT_EQ(device->GetRequestedPsgClock(), kHiresClock) << "rounded to 100 Hz";
    EXPECT_EQ(device->GetPsgClock(), PSG_CLOCK_RATE) << "queued on the render timeline, not reached yet";
}

/// A tone renders at 6/7 of its pitch at 1.5 MHz, in both output paths (HQ FIR decimation, LQ boxcar)
TEST_F(SoundChipTurboSound_Test, TonePitchFollowsTheClock)
{
    for (const bool hq : {true, false})
    {
        SCOPED_TRACE(hq ? "HQ" : "LQ");
        uint64_t crossings[2] = {};
        for (int i = 0; i < 2; i++)
        {
            SoundManager sound(_context);
            SoundChip_TurboSound* device = Device(sound);
            ASSERT_NE(device, nullptr);
            device->setHQEnabled(hq);
            _z80->t = 0;
            if (i == 1)
                ASSERT_TRUE(device->SetPsgClock(kHiresClock));
            ProgramChannelA(device, 60);  // 1823 Hz at 1.75 MHz, 1563 Hz at 1.5 MHz
            RenderFrames(device, 4);  // the output DC blocker settles from the level step at the start
            const std::vector<int16_t> stream = RenderFrames(device, 12);
            crossings[i] = Crossings(stream);
            EXPECT_EQ(device->GetPsgClock(), i == 1 ? kHiresClock : PSG_CLOCK_RATE);
        }
        ASSERT_GT(crossings[0], 700u) << "the tone must render (~880 crossings in 12 frames)";
        const double ratio = double(crossings[1]) / double(crossings[0]);
        EXPECT_NEAR(ratio, 6.0 / 7.0, 0.006) << crossings[1] << " / " << crossings[0];
    }
}

/// A switch requested at T 30000 reaches the generators there - not before - and the output carries on: a constant
/// level renders the same as on a device that never switched (the FIR history is kept and the DC blocker keeps its
/// cutoff; a cleared history would dip to zero for a filter length)
TEST_F(SoundChipTurboSound_Test, SwitchLandsOnItsTStateWithoutAClick)
{
    SoundManager soundS(_context);
    SoundManager soundR(_context);
    SoundChip_TurboSound* switched = Device(soundS);
    SoundChip_TurboSound* reference = Device(soundR);
    ASSERT_NE(switched, nullptr);
    ASSERT_NE(reference, nullptr);
    std::vector<int16_t> frameS;
    std::vector<int16_t> frameR;
    for (SoundChip_TurboSound* device : {switched, reference})
    {
        device->setHQEnabled(true);
        ProgramChannelA(device, 0);
        RenderFrames(device, 2);

        StartFrame(device);
        _z80->t = 30000;
        if (device == switched)
            ASSERT_TRUE(device->SetPsgClock(kHiresClock));
        StepTo(device, 20000);
        EXPECT_EQ(device->GetPsgClock(), PSG_CLOCK_RATE) << "rendering has not reached T 30000";
        StepTo(device, 30000);
        EXPECT_EQ(device->GetPsgClock(), PSG_CLOCK_RATE) << "the render cursor lags the CPU";
        StepTo(device, kFrame);
        EXPECT_EQ(device->GetPsgClock(), device == switched ? kHiresClock : PSG_CLOCK_RATE);
        AppendFrame(device, device == switched ? frameS : frameR);
    }

    ASSERT_EQ(frameS.size(), frameR.size());
    ASSERT_GT(frameS.size(), 800u);
    ASSERT_GT(frameR.front(), 1000) << "channel A holds a level";
    for (size_t i = 0; i < frameS.size(); i++)
        ASSERT_NEAR(frameS[i], frameR[i], 4) << "sample " << i;
}

/// Asking for the clock the device already runs at queues nothing: the device stays byte-identical to one never
/// asked, and a capture at the default clock has the plain cursor of earlier builds (sign extension in its upper half)
TEST_F(SoundChipTurboSound_Test, DefaultClockKeepsTheRenderAndTheCaptureLayout)
{
    SoundManager soundA(_context);
    SoundManager soundB(_context);
    SoundChip_TurboSound* a = Device(soundA);
    SoundChip_TurboSound* b = Device(soundB);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ProgramChannelA(a, 37);
    ProgramChannelA(b, 37);
    EXPECT_TRUE(b->SetPsgClock(PSG_CLOCK_RATE));

    const std::vector<int16_t> streamA = RenderFrames(a, 3);
    const std::vector<int16_t> streamB = RenderFrames(b, 3);
    EXPECT_EQ(streamA, streamB);
    const std::vector<uint8_t> blobA = Save(a);
    EXPECT_EQ(blobA, Save(b));

    // The timeline tail starts after the chip selector and both 73-byte chip payloads
    const size_t cursorAt = 1 + 2 * a->getChip(0)->TTDStateSize();
    int64_t cursor = 0;
    std::memcpy(&cursor, blobA.data() + cursorAt, sizeof(cursor));
    EXPECT_LT(cursor, 0) << "the cursor lags the frame end";
    EXPECT_GT(cursor, -4 * kTurboSoundRenderLagT) << "a plain i64 offset: no clock state in its upper half";
}

/// Save at 1.5 MHz with a switch back still queued and the cursor between whole T-states; a fresh device loaded from
/// it runs the same clock, has the same request pending, re-saves the same bytes and renders the same next frame
TEST_F(SoundChipTurboSound_Test, TtdRoundTripKeepsTheClockState)
{
    SoundManager soundA(_context);
    SoundManager soundB(_context);
    SoundChip_TurboSound* a = Device(soundA);
    SoundChip_TurboSound* b = Device(soundB);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    ASSERT_TRUE(a->SetPsgClock(kHiresClock));
    ProgramChannelA(a, 29);
    RenderFrames(a, 2);
    ASSERT_EQ(a->GetPsgClock(), kHiresClock);

    StartFrame(a);
    StepTo(a, kFrame - 1000);
    _z80->t = kFrame - 100;
    ASSERT_TRUE(a->SetPsgClock(PSG_CLOCK_RATE));
    StepTo(a, kFrame);
    ASSERT_EQ(a->GetPsgClock(), kHiresClock) << "the switch back is still queued at the frame end";
    ASSERT_EQ(a->GetRequestedPsgClock(), PSG_CLOCK_RATE);

    const std::vector<uint8_t> blob = Save(a);
    b->TTDLoadState(blob.data());
    EXPECT_EQ(b->GetPsgClock(), kHiresClock);
    EXPECT_EQ(b->GetRequestedPsgClock(), PSG_CLOCK_RATE);
    EXPECT_EQ(Save(b), blob);

    // The next frame from both: the queued switch lands on the same tick
    std::vector<int16_t> nextA;
    std::vector<int16_t> nextB;
    for (SoundChip_TurboSound* device : {a, b})
    {
        StartFrame(device);
        StepTo(device, kFrame);
        AppendFrame(device, device == a ? nextA : nextB);
        EXPECT_EQ(device->GetPsgClock(), PSG_CLOCK_RATE);
    }
    EXPECT_EQ(Save(a), Save(b)) << "generators, cursor and queues in step a frame later";
    // Output content is not TTD state (the load flushes the decimators, the DC blockers are host-side): only the count
    EXPECT_EQ(nextA.size(), nextB.size());
}
