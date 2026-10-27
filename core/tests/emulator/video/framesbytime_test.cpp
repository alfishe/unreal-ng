#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "common/sound/audiodevicedescriptor.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"

/// Screen frames by time (docs/inprogress/2026-10-26-frame-presenter, section 3): every latched frame gets a target
/// time at its frame end (StampPresentTiming), and readers pick by time (CopyFrameAt) or take each frame once
/// (CopyNextFrame). Synthetic clocks; the emulator does not run

namespace
{
constexpr int64_t PERIOD = 20'000'000;
constexpr int64_t BASE = 5'000'000'000'000;  // a steady-clock time far from 0
}  // namespace

class FramesByTime_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    Screen* _screen = nullptr;
    size_t _size = 0;
    int _frames = 0;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _screen = _emulator->GetContext()->pScreen;
        ASSERT_NE(_screen, nullptr);
        _size = _screen->GetFramebufferDescriptor().memoryBufferSize;
        ASSERT_GT(_size, 0u);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->GetContext()->pAudioDeviceDescriptor.store(nullptr);
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    /// Latch frame k (its pixels all = marker k) without stamping it
    void Latch()
    {
        FramebufferDescriptor& fb = _screen->GetFramebufferDescriptor();
        memset(fb.memoryBuffer, 0x10 + _frames, fb.memoryBufferSize);
        _screen->LatchFramebuffer();
        _frames++;
    }

    /// The frame end of the last latched frame on the emulated clock: frame k ends at BASE + k periods
    FrameStampInput EmulatedStamp() const
    {
        FrameStampInput in;
        in.periodNs = PERIOD;
        in.haveEmulated = true;
        in.emulatedEndNs = BASE + static_cast<int64_t>(_frames) * PERIOD;
        in.nowNs = in.emulatedEndNs;
        return in;
    }

    void LatchAndStamp()
    {
        Latch();
        _screen->StampPresentTiming(EmulatedStamp());
    }

    /// Frame k's target on the emulated clock: its end plus one period of delay
    static int64_t Target(int k) { return BASE + static_cast<int64_t>(k + 1) * PERIOD + PERIOD; }
    static uint8_t Marker(int k) { return static_cast<uint8_t>(0x10 + k); }
};

TEST_F(FramesByTime_Test, NewestFrameDueAtTheTime)
{
    for (int k = 0; k < 5; k++)
        LatchAndStamp();

    std::vector<uint8_t> out(_size), next(_size);
    PresentFrameInfo info;
    // A quarter of the way from frame 2's target to frame 3's
    ASSERT_TRUE(_screen->CopyFrameAt(Target(2) + PERIOD / 4, out.data(), out.size(), &info, next.data()));
    EXPECT_EQ(out[0], Marker(2));
    EXPECT_EQ(next[0], Marker(3));
    EXPECT_EQ(info.targetNs, Target(2));
    EXPECT_EQ(info.nextTargetNs, Target(3));
    EXPECT_EQ(info.nextSerial, info.serial + 1);
    EXPECT_EQ(info.source, FrameClockSource::Emulated);
    EXPECT_NEAR(info.blend, 0.25f, 1e-4f);
    EXPECT_FALSE(info.early);

    // Exactly at a target: that frame, no blend
    ASSERT_TRUE(_screen->CopyFrameAt(Target(4), out.data(), out.size(), &info));
    EXPECT_EQ(out[0], Marker(4));
    EXPECT_EQ(info.nextSerial, 0u) << "no newer frame";
    EXPECT_EQ(info.blend, 0.0f);

    // Before every target (start up): the oldest frame, marked early
    ASSERT_TRUE(_screen->CopyFrameAt(BASE, out.data(), out.size(), &info));
    EXPECT_EQ(out[0], Marker(0));
    EXPECT_TRUE(info.early);
}

/// Between the latch and the frame end's stamp a frame has no time: a reader never gets it early
TEST_F(FramesByTime_Test, UntimedFrameIsNotServed)
{
    for (int k = 0; k < 3; k++)
        LatchAndStamp();
    Latch();  // frame 3, its sound not delivered yet

    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    ASSERT_TRUE(_screen->CopyFrameAt(INT64_MAX / 2, out.data(), out.size(), &info));
    EXPECT_EQ(out[0], Marker(2));

    _screen->StampPresentTiming(EmulatedStamp());
    ASSERT_TRUE(_screen->CopyFrameAt(INT64_MAX / 2, out.data(), out.size(), &info));
    EXPECT_EQ(out[0], Marker(3));
}

/// Present-at-time takes every frame once, in order
TEST_F(FramesByTime_Test, NextFrameInOrder)
{
    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    EXPECT_FALSE(_screen->CopyNextFrame(0, out.data(), out.size(), &info)) << "nothing latched";

    for (int k = 0; k < 4; k++)
        LatchAndStamp();
    uint64_t serial = 0;
    for (int k = 0; k < 4; k++)
    {
        ASSERT_TRUE(_screen->CopyNextFrame(serial, out.data(), out.size(), &info));
        EXPECT_EQ(out[0], Marker(k));
        EXPECT_EQ(info.targetNs, Target(k));
        serial = info.serial;
    }
    EXPECT_FALSE(_screen->CopyNextFrame(serial, out.data(), out.size(), &info)) << "all taken";
}

/// With the sound on a device, a frame's target is when its last sample is heard
TEST_F(FramesByTime_Test, AudioClockTimesTheFrames)
{
    AudioDeviceDescriptor device;
    _emulator->GetContext()->pAudioDeviceDescriptor.store(&device);

    // A device at 44.1 kHz, 256-frame callbacks, no jitter: stream frame s is heard at BASE + s / rate + latency
    constexpr uint32_t rate = 44100;
    const auto heard = [](uint64_t s) { return BASE + static_cast<int64_t>(s * 1e9 / rate) + static_cast<int64_t>(512 * 1e9 / rate); };
    uint64_t stream = 0;
    for (int i = 0; i < 64; i++, stream += 256)
        device.clock.OnDeviceCallback(BASE + static_cast<int64_t>(stream * 1e9 / rate), stream, 256, true, rate, 512);

    Latch();
    FrameStampInput in = EmulatedStamp();
    in.nowNs = BASE + static_cast<int64_t>(stream * 1e9 / rate);
    in.haveAudio = true;
    in.audioEndStreamFrame = stream + 4000;
    in.audioFramesPerFrame = 882;
    _screen->StampPresentTiming(in);

    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    ASSERT_TRUE(_screen->CopyNextFrame(0, out.data(), out.size(), &info));
    EXPECT_EQ(info.source, FrameClockSource::Audio);
    EXPECT_NEAR(static_cast<double>(info.targetNs), static_cast<double>(heard(stream + 4000)), 1000.0);
    EXPECT_EQ(_screen->GetFrameClockStats().audioFrames, 1u);
}

/// A seek repaints once and stops: that frame is shown at once, and the clock starts over after it
TEST_F(FramesByTime_Test, FlushServesAtOnce)
{
    for (int k = 0; k < 3; k++)
        LatchAndStamp();
    FramebufferDescriptor& fb = _screen->GetFramebufferDescriptor();
    memset(fb.memoryBuffer, 0x77, fb.memoryBufferSize);
    _screen->FlushAndPresentFramebuffer();

    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    const int64_t now =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    ASSERT_TRUE(_screen->CopyFrameAt(now, out.data(), out.size(), &info));
    EXPECT_EQ(out[0], 0x77);
    EXPECT_FALSE(info.early);
    EXPECT_EQ(info.source, FrameClockSource::Latch);
}

/// A screen nobody stamps (no MainLoop): frames are served by their latch time, so readers still work
TEST_F(FramesByTime_Test, UnstampedScreenUsesLatchTimes)
{
    Latch();
    Latch();
    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    const int64_t now =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    ASSERT_TRUE(_screen->CopyFrameAt(now, out.data(), out.size(), &info));
    EXPECT_EQ(out[0], Marker(1));
    EXPECT_EQ(info.source, FrameClockSource::None);
}
