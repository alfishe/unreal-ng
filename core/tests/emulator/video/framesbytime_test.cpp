#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testthread.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "common/sound/audiodevicedescriptor.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"

/// Screen frames by time (docs/inprogress/2026-10-26-frame-presenter, section 3): every latched frame gets a target
/// time at its frame end (StampPresentTiming), and readers pick by time (CopyFrameAt) or take each frame once
/// (CopyNextFrame). Synthetic clocks; the emulator does not run

namespace
{
constexpr int64_t kPeriod = 20'000'000;
constexpr int64_t kBase = 5'000'000'000'000;  // a steady-clock time far from 0
}  // namespace

class FramesByTime_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    Screen* _screen = nullptr;
    size_t _size = 0;
    int _frames = 0;
    int64_t _base = kBase;  ///< frame k ends at _base + k periods

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
        if (_gate)
            _gate->Set(true);  // a worker held at the gate goes on: the screen joins it
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

    /// The frame end of the last latched frame on the emulated clock: frame k ends at kBase + k periods
    FrameStampInput EmulatedStamp() const
    {
        FrameStampInput in;
        in.periodNs = kPeriod;
        in.haveEmulated = true;
        in.emulatedEndNs = _base + static_cast<int64_t>(_frames) * kPeriod;
        in.nowNs = in.emulatedEndNs;
        return in;
    }

    void LatchAndStamp()
    {
        Latch();
        _screen->StampPresentTiming(EmulatedStamp());
    }

    /// Frame k's target on the emulated clock: its end plus one period of delay
    static int64_t Target(int k) { return kBase + static_cast<int64_t>(k + 1) * kPeriod + kPeriod; }
    static uint8_t Marker(int k) { return static_cast<uint8_t>(0x10 + k); }

    /// The temporal worker's write-backs held at will (Screen::SetTemporalWriteGateForTest). Shared with the worker:
    /// it outlives the test body if a write-back comes after it
    struct Gate
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool open = true;
        uint64_t waiting = 0;  ///< the serial whose output waits at the gate (0: none)
        void Pass(uint64_t serial)
        {
            std::unique_lock<std::mutex> lock(mutex);
            waiting = serial;
            changed.wait(lock, [this] { return open; });
            waiting = 0;
        }
        void Set(bool on)
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                open = on;
            }
            changed.notify_all();
        }
        uint64_t Waiting()
        {
            std::lock_guard<std::mutex> lock(mutex);
            return waiting;
        }
    };
    std::shared_ptr<Gate> _gate;

    /// ZX DLSS (mod-tpgwafsd: 6 frames of look-ahead + 1 for the worker) on the synthetic frames: plane B all zero, so
    /// its output is black where the latched marker was not
    void StartTemporalEffect()
    {
        _gate = std::make_shared<Gate>();
        _screen->SetTemporalWriteGateForTest([gate = _gate](uint64_t serial) { gate->Pass(serial); });
        ASSERT_TRUE(_screen->SetTemporalAlgorithm("mod-tpgwafsd"));
        _screen->SetPlaneBEnabled(true);
    }
    /// A frame through the effect, the worker done with it (one at a time: a worker two behind would restart)
    void LatchProcessed()
    {
        const uint64_t before = _screen->GetTemporalStats().processed;
        LatchAndStamp();
        ASSERT_TRUE(TestWait::For([&] { return _screen->GetTemporalStats().processed == before + 1; }));
    }
    static int64_t SteadyNow()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
};

TEST_F(FramesByTime_Test, NewestFrameDueAtTheTime)
{
    for (int k = 0; k < 5; k++)
        LatchAndStamp();

    std::vector<uint8_t> out(_size), next(_size);
    PresentFrameInfo info;
    // A quarter of the way from frame 2's target to frame 3's
    ASSERT_TRUE(_screen->CopyFrameAt(Target(2) + kPeriod / 4, out.data(), out.size(), &info, next.data()));
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
    ASSERT_TRUE(_screen->CopyFrameAt(kBase, out.data(), out.size(), &info));
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

/// Smooth motion reads the frames ahead of the screen: what is on screen stays the frame CopyFrameAt served
TEST_F(FramesByTime_Test, NextFrameReadAheadLeavesTheShownFrame)
{
    for (int k = 0; k < 4; k++)
        LatchAndStamp();
    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    ASSERT_TRUE(_screen->CopyFrameAt(Target(0), out.data(), out.size(), &info));
    uint64_t serial = 0;
    for (int k = 0; k < 4; k++)
    {
        ASSERT_TRUE(_screen->CopyNextFrame(serial, out.data(), out.size(), &info, false));
        serial = info.serial;
    }
    EXPECT_EQ(_screen->GetShownDelayFrames(), 3u) << "frame 0 still on screen, 3 newer";
    // Presenting each frame once marks it shown
    ASSERT_TRUE(_screen->CopyNextFrame(serial - 1, out.data(), out.size(), &info));
    EXPECT_EQ(_screen->GetShownDelayFrames(), 0u) << "the newest on screen";
}

/// With the sound on a device, a frame's target is when its last sample is heard
TEST_F(FramesByTime_Test, AudioClockTimesTheFrames)
{
    AudioDeviceDescriptor device;
    _emulator->GetContext()->pAudioDeviceDescriptor.store(&device);

    // A device at 44.1 kHz, 256-frame callbacks, no jitter: stream frame s is heard at start + s / rate + latency.
    // It started 400 ms before the frame's end (kBase + kPeriod): this frame's sound is heard 54 ms after it
    constexpr uint32_t rate = 44100;
    constexpr int64_t start = kBase - 400'000'000;
    const auto heard = [](uint64_t s) {
        return start + static_cast<int64_t>(s * 1e9 / rate) + static_cast<int64_t>(512 * 1e9 / rate);
    };
    uint64_t stream = 0;
    for (int i = 0; i < 64; i++, stream += 256)
        device.clock.OnDeviceCallback(start + static_cast<int64_t>(stream * 1e9 / rate), stream, 256, true, rate, 512);

    Latch();
    FrameStampInput in = EmulatedStamp();
    in.nowNs = start + static_cast<int64_t>(stream * 1e9 / rate);
    in.haveAudio = true;
    in.audioEndStreamFrame = stream + 4000;
    in.audioFramesPerFrame = 882;
    _screen->StampPresentTiming(in);

    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    ASSERT_TRUE(_screen->CopyNextFrame(0, out.data(), out.size(), &info));
    EXPECT_EQ(info.source, FrameClockSource::Audio);
    EXPECT_NEAR(static_cast<double>(info.targetNs), static_cast<double>(heard(stream + 4000)), 1000.0);
    EXPECT_EQ(_screen->GetPresentTimingStats().source, FrameClockSource::Audio);
    EXPECT_FALSE(_screen->GetPresentTimingStats().delayClamped);
}

/// Feature `lowlatency`: a frame is due at its latch, not when its sound is heard; the newest latched frame is shown.
/// Off again, the audio clock times the frames
TEST_F(FramesByTime_Test, LowLatencyFrameIsDueAtItsLatch)
{
    FeatureManager* features = _emulator->GetContext()->pFeatureManager;
    ASSERT_NE(features, nullptr);
    EXPECT_FALSE(features->isEnabled(Features::kLowLatency)) << "off by default";
    EXPECT_EQ(_screen->GetEffectivePresentDelayFrames(), Screen::kAudioPathDelayFrames);
    ASSERT_TRUE(features->setFeature(Features::kLowLatency, true));
    EXPECT_EQ(_screen->GetEffectivePresentDelayFrames(), 0);

    // The sound on a device, heard ~54 ms after each frame's end (as AudioClockTimesTheFrames)
    AudioDeviceDescriptor device;
    _emulator->GetContext()->pAudioDeviceDescriptor.store(&device);
    constexpr uint32_t rate = 44100;
    constexpr int64_t start = kBase - 400'000'000;
    uint64_t stream = 0;
    for (int i = 0; i < 64; i++, stream += 256)
        device.clock.OnDeviceCallback(start + static_cast<int64_t>(stream * 1e9 / rate), stream, 256, true, rate, 512);
    const auto latchAndStampWithAudio = [&]() {
        Latch();
        FrameStampInput in = EmulatedStamp();
        in.nowNs = start + static_cast<int64_t>(stream * 1e9 / rate);
        in.haveAudio = true;
        in.audioEndStreamFrame = stream + 4000 + static_cast<uint64_t>(_frames) * 882;
        in.audioFramesPerFrame = 882;
        _screen->StampPresentTiming(in);
    };

    for (int k = 0; k < 3; k++)
        latchAndStampWithAudio();
    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    uint64_t serial = 0;
    for (int k = 0; k < 3; k++)
    {
        ASSERT_TRUE(_screen->CopyNextFrame(serial, out.data(), out.size(), &info, false));
        EXPECT_EQ(out[0], Marker(k));
        EXPECT_EQ(info.source, FrameClockSource::Latch) << "frame " << k;
        EXPECT_EQ(info.targetNs, info.latchNs) << "frame " << k;
        serial = info.serial;
    }
    const int64_t now =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    ASSERT_TRUE(_screen->CopyFrameAt(now, out.data(), out.size(), &info));
    EXPECT_EQ(out[0], Marker(2)) << "the newest latched frame";
    EXPECT_FALSE(info.early);

    ASSERT_TRUE(features->setFeature(Features::kLowLatency, false));
    EXPECT_EQ(_screen->GetEffectivePresentDelayFrames(), Screen::kAudioPathDelayFrames);
    latchAndStampWithAudio();
    ASSERT_TRUE(_screen->CopyNextFrame(serial, out.data(), out.size(), &info, false));
    EXPECT_EQ(out[0], Marker(3));
    EXPECT_EQ(info.source, FrameClockSource::Audio);
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

/// CopyPresentedFramebuffer (screenshots, captures, the video wall) serves the frame due now, not the newest latched
TEST_F(FramesByTime_Test, PresentedFramebufferIsTheFrameDueNow)
{
    // Four frames 100 ms apart on the emulated clock, timed so that the third is due now and the fourth 50 ms later
    constexpr int64_t P = 100'000'000;
    const int64_t now =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const int64_t start = now - 7 * P / 2;
    for (int k = 0; k < 4; k++)
    {
        Latch();
        FrameStampInput in;
        in.periodNs = P;
        in.haveEmulated = true;
        in.emulatedEndNs = start + k * P;  // target: one period later
        in.nowNs = now;
        _screen->StampPresentTiming(in);
    }
    std::vector<uint8_t> out(_size);
    ASSERT_TRUE(_screen->CopyPresentedFramebuffer(out.data(), out.size()));
    EXPECT_EQ(out[0], Marker(2));
    // The window's readout is the frame a window got (CopyFrameAt), the same one now
    PresentFrameInfo info;
    ASSERT_TRUE(_screen->CopyFrameAt(now, out.data(), out.size(), &info));
    EXPECT_EQ(out[0], Marker(2));
    EXPECT_EQ(_screen->GetShownDelayFrames(), 1u) << "one frame latched after it";
    EXPECT_EQ(_screen->GetPresentDelayUs(), 0u) << "the emulated clock times the frame end, the latch is now";
}

/// A machine that stops (a pause, the end of a direct run) has nothing to keep in step with: every frame it latched
/// is due at once, so a capture after the stop gets the newest whatever the time
TEST_F(FramesByTime_Test, SettleMakesEveryFrameDue)
{
    const int64_t now =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    for (int k = 0; k < 3; k++)
    {
        Latch();
        FrameStampInput in;
        in.periodNs = kPeriod;
        in.haveEmulated = true;
        in.emulatedEndNs = now + 1'000'000'000 + k * kPeriod;  // a second ahead
        in.nowNs = now;
        _screen->StampPresentTiming(in);
    }
    Latch();  // its sound not delivered yet

    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    ASSERT_TRUE(_screen->CopyFrameAt(now, out.data(), out.size(), &info));
    EXPECT_TRUE(info.early) << "nothing due yet";
    EXPECT_EQ(out[0], Marker(0));

    _screen->SettlePresentTiming();
    ASSERT_TRUE(_screen->CopyPresentedFramebuffer(out.data(), out.size()));
    EXPECT_EQ(out[0], Marker(3)) << "the newest, even the one not stamped yet";
    // The window shows it (the settled frames are due at the settle's time, after `now`)
    const int64_t settled =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    ASSERT_TRUE(_screen->CopyFrameAt(settled, out.data(), out.size(), &info));
    EXPECT_EQ(out[0], Marker(3));
    EXPECT_EQ(_screen->GetShownDelayFrames(), 0u);

    // The next frame starts a new sequence: timed again, not due before its time. The settled frames are due at the
    // settle's time, after `now`
    const int64_t later =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    Latch();
    FrameStampInput again;
    again.periodNs = kPeriod;
    again.haveEmulated = true;
    again.emulatedEndNs = later + 1'000'000'000;
    again.nowNs = later;
    _screen->StampPresentTiming(again);
    ASSERT_TRUE(_screen->CopyFrameAt(later, out.data(), out.size(), &info));
    EXPECT_EQ(out[0], Marker(3));
    EXPECT_EQ(info.nextSerial, info.serial + 1);
}

/// A screenshot or a capture (CopyPresentedFramebuffer) is not what the window shows: the shown frame's delay readout
/// stays the window's
TEST_F(FramesByTime_Test, ScreenshotLeavesTheShownFrame)
{
    for (int k = 0; k < 4; k++)
        LatchAndStamp();
    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    ASSERT_TRUE(_screen->CopyFrameAt(Target(0), out.data(), out.size(), &info));
    EXPECT_EQ(_screen->GetShownDelayFrames(), 3u);
    const uint32_t delayUs = _screen->GetPresentDelayUs();

    // Long after every target: the screenshot gets the newest frame
    _screen->SettlePresentTiming();
    ASSERT_TRUE(_screen->CopyPresentedFramebuffer(out.data(), out.size()));
    EXPECT_EQ(out[0], Marker(3));
    EXPECT_EQ(_screen->GetShownDelayFrames(), 3u) << "the window still shows frame 0";
    EXPECT_EQ(_screen->GetPresentDelayUs(), delayUs);
}

/// A long audio path (a Bluetooth headset, 300 ms) is longer than the present slots hold the frames: the targets are
/// clamped to the longest delay they hold, so every refresh still finds its frame (not the oldest one held, early),
/// and the report says so
TEST_F(FramesByTime_Test, LongAudioPathIsClamped)
{
    AudioDeviceDescriptor device;
    _emulator->GetContext()->pAudioDeviceDescriptor.store(&device);
    constexpr uint32_t rate = 44100;
    constexpr uint32_t latency = rate * 3 / 10;  // 300 ms from the callback to the speaker
    uint64_t stream = 0;
    for (int i = 0; i < 64; i++, stream += 256)
        device.clock.OnDeviceCallback(kBase + static_cast<int64_t>(stream * 1e9 / rate), stream, 256, true, rate,
                                      latency);
    const int64_t lastCallback = kBase + static_cast<int64_t>((stream - 256) * 1e9 / rate);

    constexpr int frames = 30;
    for (int k = 0; k < frames; k++)
    {
        Latch();
        FrameStampInput in = EmulatedStamp();
        in.nowNs = lastCallback;
        in.haveAudio = true;
        in.audioEndStreamFrame = stream + 4000 + static_cast<uint64_t>(k) * 882;
        in.audioFramesPerFrame = 882;
        _screen->StampPresentTiming(in);
    }

    const PresentTimingStats stats = _screen->GetPresentTimingStats();
    EXPECT_EQ(stats.source, FrameClockSource::Audio);
    EXPECT_TRUE(stats.delayClamped);
    EXPECT_EQ(stats.clampedFrames, static_cast<uint64_t>(frames));
    ASSERT_GT(stats.maxDelayNs, 0);
    EXPECT_EQ(stats.maxDelayNs % kPeriod, 0) << "whole frames";
    const int64_t maxFrames = stats.maxDelayNs / kPeriod;
    EXPECT_LE(maxFrames, 10) << "the frame and the next still held when the next is due";

    // The emulation's now: the newest frame's end. Frame k ends at kBase + (k + 1) periods, due maxFrames later
    const int64_t now = kBase + static_cast<int64_t>(frames) * kPeriod;
    const int due = frames - 1 - static_cast<int>(maxFrames);
    std::vector<uint8_t> out(_size), next(_size);
    PresentFrameInfo info;
    ASSERT_TRUE(_screen->CopyFrameAt(now + kPeriod / 4, out.data(), out.size(), &info, next.data()));
    EXPECT_FALSE(info.early) << "its frame is held";
    EXPECT_EQ(out[0], Marker(due));
    EXPECT_EQ(next[0], Marker(due + 1));
    EXPECT_NEAR(info.blend, 0.25f, 1e-4f);
    EXPECT_EQ(_screen->GetPresentTimingStats().early, 0u);
}

/// The window copies frames outside the screen's lock while the emulation thread latches: a copy is never torn, and
/// the latch never writes a slot being copied
TEST_F(FramesByTime_Test, ReaderNeverSeesATornFrameWhileFramesAreLatched)
{
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> reads{0};
    std::atomic<uint64_t> torn{0};
    const size_t size = _size;
    const auto uniform = [size](const std::vector<uint8_t>& frame) {
        for (size_t i = 0; i < size; i += 509)
            if (frame[i] != frame[0])
                return false;
        return frame[size - 1] == frame[0];
    };
    std::atomic<int> stamped{0};
    LatchAndStamp();
    stamped = _frames;
    TestThread reader(
        [&] {
            std::vector<uint8_t> out(size), next(size);
            for (uint64_t n = 0; !stop.load(std::memory_order_relaxed); n++)
            {
                // Halfway between a frame's target and the next one's: the oldest frame held (the one the latch
                // overwrites next) or a recent one, and the frame after it
                const int newest = stamped.load(std::memory_order_relaxed) - 1;
                const int k = std::max(0, newest - ((n & 1) ? 11 : 2));
                PresentFrameInfo info;
                if (!_screen->CopyFrameAt(Target(k) + kPeriod / 2, out.data(), out.size(), &info, next.data()))
                    continue;
                if (!uniform(out) || (info.nextSerial != 0 && !uniform(next)))
                    torn.fetch_add(1, std::memory_order_relaxed);
                reads.fetch_add(1, std::memory_order_relaxed);
            }
        },
        [&] { stop = true; });
    ASSERT_TRUE(TestWait::For([&] { return reads.load(std::memory_order_relaxed) > 0; }));

    uint64_t latched = 0;
    const uint64_t readsBefore = reads.load();
    const auto deadline = std::chrono::steady_clock::now() + TestWait::Scaled(TestWait::kDefaultTimeout);
    while ((latched < 300 || reads.load(std::memory_order_relaxed) - readsBefore < 300) &&
           std::chrono::steady_clock::now() < deadline)
    {
        LatchAndStamp();
        stamped.store(_frames, std::memory_order_relaxed);
        latched++;
    }
    reader.Join();
    EXPECT_EQ(torn.load(), 0u) << "of " << reads.load() << " reads beside " << latched << " latches";
    EXPECT_GE(reads.load() - readsBefore, 300u) << "the reader barely ran beside the latches";
    EXPECT_GE(latched, 300u);
}

/// The temporal effect's worker a frame or two behind (it may be up to TemporalEffects::kMaxQueuedJobs): the read
/// ahead (smooth motion) waits for a frame whose output is still to come. Handed out raw, it was kept by its serial for
/// good: the flicker drawn, and counted nowhere (shownRaw stayed 0)
TEST_F(FramesByTime_Test, NextFrameWaitsForTheTemporalOutputToCome)
{
    StartTemporalEffect();
    // 10 frames in step with the worker: the outputs of frames 1..4 are in (each 6 frames of look-ahead later)
    for (int k = 0; k < 10; k++)
        LatchProcessed();
    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    uint64_t serial = 0;
    for (int k = 0; k < 4; k++)  // the frames whose output is in are served
    {
        ASSERT_TRUE(_screen->CopyNextFrame(serial, out.data(), out.size(), &info, false));
        EXPECT_NE(out[0], Marker(k)) << "frame " << k << " processed";
        serial = info.serial;
    }
    EXPECT_FALSE(_screen->CopyNextFrame(serial, out.data(), out.size(), &info, false)) << "frame 5 not served yet";

    // The worker held writing frame 5's output (pushed with frame 11), frame 12 queued behind it: frame 5's output
    // still to come
    _gate->Set(false);
    LatchAndStamp();
    ASSERT_TRUE(TestWait::For([&] { return _gate->Waiting() != 0; }));
    const uint64_t held = _gate->Waiting();
    LatchAndStamp();
    EXPECT_EQ(held, serial + 1) << "the worker holds the frame after the last one served";
    EXPECT_FALSE(_screen->CopyNextFrame(serial, out.data(), out.size(), &info, false))
        << "frame 5 waits for its output, not handed out raw";

    // The output in: the frame processed
    const uint64_t processed = _screen->GetTemporalStats().processed;
    _gate->Set(true);
    ASSERT_TRUE(TestWait::For([&] { return _screen->GetTemporalStats().processed >= processed + 2; }));
    ASSERT_TRUE(_screen->CopyNextFrame(serial, out.data(), out.size(), &info, false));
    EXPECT_EQ(info.serial, held);
    EXPECT_NE(out[0], Marker(4)) << "frame 5 processed";
    EXPECT_EQ(_screen->GetTemporalStats().restarts, 0u);

    // A restart (another algorithm, a seek, the worker too far behind): the output of the frames queued before never
    // comes, they are served raw at once
    serial = info.serial;
    _gate->Set(false);
    LatchAndStamp();  // frame 7's output at the gate
    ASSERT_TRUE(TestWait::For([&] { return _gate->Waiting() != 0; }));
    LatchAndStamp();
    ASSERT_TRUE(_screen->CopyNextFrame(serial, out.data(), out.size(), &info, false));
    serial = info.serial;  // frame 6
    EXPECT_FALSE(_screen->CopyNextFrame(serial, out.data(), out.size(), &info, false)) << "frame 7 waits";
    ASSERT_TRUE(_screen->SetTemporalAlgorithm("mod-tpgwa"));
    ASSERT_TRUE(_screen->CopyNextFrame(serial, out.data(), out.size(), &info, false)) << "nothing to wait for";
    EXPECT_EQ(out[0], Marker(6)) << "frame 7 raw";
    _gate->Set(true);
}

/// The read ahead (smooth motion) gets a ZX DLSS frame as soon as its output is in, not a whole delay (look-ahead + 1
/// for the worker) after its latch: cut by the delay, the frame after the one due came only when that one was due,
/// and smooth motion had no next frame to go towards for about half the frames (live, 2026-11-27: starved 165 of 339)
TEST_F(FramesByTime_Test, NextFrameServedOnceItsTemporalOutputIsIn)
{
    StartTemporalEffect();
    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    uint64_t serial = 0;
    int served = 0;
    // In step with the worker: frame k's output comes with frame k + 6 (6 frames of look-ahead)
    for (int k = 0; k < 20; k++)
    {
        LatchProcessed();
        while (_screen->CopyNextFrame(serial, out.data(), out.size(), &info, false))
        {
            EXPECT_NE(out[0], Marker(static_cast<int>(info.serial) - 1)) << "frame " << info.serial << " processed";
            serial = info.serial;
            served++;
        }
        if (k >= 6)
            EXPECT_EQ(static_cast<int>(serial), k - 5) << "latch " << k + 1 << ": every frame with its output in";
    }
    EXPECT_EQ(served, 14);
    EXPECT_EQ(_screen->GetTemporalStats().restarts, 0u);
}

/// ZX DLSS switched on or off mid-stream steps the video's delay by its look-ahead, the sound with it at the same
/// latch: the frames' targets take the new delay at once. Carried (0.2 ms a frame), the step of 6 frames was paid back
/// over 600 frames: frames due before their output could come (smooth motion starved for ~3 s live), the picture off
/// its sound meanwhile
TEST_F(FramesByTime_Test, TemporalEffectSwitchCutsTheFrameClock)
{
    for (int k = 0; k < 10; k++)
        LatchAndStamp();
    const uint64_t cutsBefore = _screen->GetPresentTimingStats().cuts;

    StartTemporalEffect();
    for (int k = 0; k < 20; k++)
        LatchProcessed();
    PresentTimingStats stats = _screen->GetPresentTimingStats();
    EXPECT_EQ(stats.carryNs, 0) << "the delay's step taken at once, not carried";
    EXPECT_EQ(stats.cuts, cutsBefore + 1);
    // The frames after the switch: each due its look-ahead (7 frames) after its end
    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    uint64_t serial = 0;
    while (_screen->CopyNextFrame(serial, out.data(), out.size(), &info, false))
        serial = info.serial;
    ASSERT_EQ(serial, 24u) << "the frames with their output in";
    EXPECT_EQ(info.targetNs, kBase + static_cast<int64_t>(serial + 7) * kPeriod);

    // Off again: back to one frame of delay at once
    ASSERT_TRUE(_screen->SetTemporalAlgorithm(""));
    for (int k = 0; k < 3; k++)
        LatchAndStamp();
    stats = _screen->GetPresentTimingStats();
    EXPECT_EQ(stats.carryNs, 0);
    EXPECT_EQ(stats.cuts, cutsBefore + 2);
    while (_screen->CopyNextFrame(serial, out.data(), out.size(), &info, false))
        serial = info.serial;
    EXPECT_EQ(serial, 33u);
    EXPECT_EQ(info.targetNs, Target(32));
}

/// ZX DLSS switched on mid-stream: its look-ahead (7 frames) is longer than the delay so far, but the picture never
/// goes back to frames already shown; the frame on screen is held until the look-ahead has passed the ones after it
TEST_F(FramesByTime_Test, TemporalEffectSwitchedOnNeverGoesBack)
{
    for (int k = 0; k < 10; k++)
        LatchAndStamp();
    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    ASSERT_TRUE(_screen->CopyFrameAt(Target(9), out.data(), out.size(), &info));
    EXPECT_EQ(out[0], Marker(9));
    uint64_t shown = info.serial;

    StartTemporalEffect();
    bool advanced = false;
    for (int k = 0; k < 8; k++)
    {
        LatchProcessed();
        ASSERT_TRUE(_screen->CopyFrameAt(INT64_MAX / 2, out.data(), out.size(), &info));
        ASSERT_GE(info.serial, shown) << "latch " << k << ": back from frame " << shown << " to " << info.serial;
        advanced |= info.serial > shown;
        shown = info.serial;
    }
    EXPECT_TRUE(advanced) << "the look-ahead passed the frame held: the frames go on";
}

/// The effect's readout (the dialog's LED) is of the frame the window shows: a screenshot or the shared-memory export
/// served another frame does not flip it
TEST_F(FramesByTime_Test, ScreenshotLeavesTheTemporalReadout)
{
    _base = SteadyNow() - 2'000'000'000;  // every target long past: a screenshot gets the newest frame served
    StartTemporalEffect();
    for (int k = 0; k < 10; k++)
        LatchProcessed();
    // Frame 5's output held back, frame 4's in
    _gate->Set(false);
    LatchAndStamp();
    ASSERT_TRUE(TestWait::For([&] { return _gate->Waiting() != 0; }));
    LatchAndStamp();

    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    uint64_t serial = 0;
    for (int k = 0; k < 4; k++)
    {
        ASSERT_TRUE(_screen->CopyNextFrame(serial, out.data(), out.size(), &info, false));
        serial = info.serial;
    }
    // The window shows frame 4 (processed)
    ASSERT_TRUE(_screen->CopyFrameAt(info.targetNs, out.data(), out.size(), &info));
    ASSERT_EQ(info.serial, serial);
    EXPECT_TRUE(_screen->GetTemporalStats().showingProcessed);
    // A screenshot now: frame 5, raw
    ASSERT_TRUE(_screen->CopyPresentedFramebuffer(out.data(), out.size()));
    EXPECT_EQ(out[0], Marker(4)) << "the screenshot got frame 5 raw";
    EXPECT_TRUE(_screen->GetTemporalStats().showingProcessed) << "the window's frame is processed";
    _gate->Set(true);
}

/// A refresh between two frames asks for the frame it holds already: not copied again (nor its slot pinned, the
/// latch never waits for it); another frame, or the same slot written again, is
TEST_F(FramesByTime_Test, FrameHeldAlreadyIsNotCopiedAgain)
{
    for (int k = 0; k < 3; k++)
        LatchAndStamp();
    std::vector<uint8_t> out(_size);
    PresentFrameInfo info;
    ASSERT_TRUE(_screen->CopyFrameAt(Target(2), out.data(), out.size(), &info));
    EXPECT_TRUE(info.copied);
    EXPECT_NE(info.content, 0u);
    const uint64_t content = info.content;

    std::fill(out.begin(), out.end(), uint8_t{0xEE});
    PresentFrameInfo again;
    again.content = content;
    ASSERT_TRUE(_screen->CopyFrameAt(Target(2) + kPeriod / 2, out.data(), out.size(), &again));
    EXPECT_FALSE(again.copied);
    EXPECT_EQ(again.content, content);
    EXPECT_EQ(again.serial, info.serial);
    EXPECT_EQ(out[0], 0xEE) << "left as it was";

    LatchAndStamp();
    PresentFrameInfo next;
    next.content = content;
    ASSERT_TRUE(_screen->CopyFrameAt(Target(3), out.data(), out.size(), &next));
    EXPECT_TRUE(next.copied);
    EXPECT_NE(next.content, content);
    EXPECT_EQ(out[0], Marker(3));
}
