#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/platform.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/ports/models/sprinter/sprinterpldstate.h"
#include "emulator/video/screen.h"
#include "recordingmanager.h"

/// RecordingManager's video timestamps (emulated time) when the frame length
/// changes while recording.
///
/// The timestamp was frame count x the CURRENT frame duration: a machine that
/// switches its frame length (the Sprinter's codes #2C / #2D: 320 lines =
/// 20.48 ms, 312 lines = 19.968 ms) stepped the timestamps backwards - frame
/// 100 at 312 lines is 1.9968 s after frame 99 at 2.0275 s. A native encoder
/// fails the file on that (AVAssetWriter), and the failed writer was the way
/// into the Sprinter heap overflow (videotoolbox_encoder_test.cpp,
/// docs/inprogress/2026-09-28-sprinter/crash-fb-overflow.md). Each frame now
/// adds its own duration.

namespace
{
/// Keeps the timestamps the recording manager hands to its encoder
class TimestampSinkEncoder : public EncoderBase
{
public:
    explicit TimestampSinkEncoder(std::vector<double>& timestamps) : _timestamps(timestamps) {}
    bool Start(const std::string&, const EncoderConfig&) override
    {
        _recording = true;
        return true;
    }
    void Stop() override { _recording = false; }
    bool IsRecording() const override { return _recording; }
    std::string GetType() const override { return "timestamps"; }
    std::string GetDisplayName() const override { return "timestamp sink"; }
    bool SupportsVideo() const override { return true; }
    bool SupportsAudio() const override { return false; }
    void OnVideoFrame(const FramebufferDescriptor&, double timestampSec) override { _timestamps.push_back(timestampSec); }

private:
    std::vector<double>& _timestamps;
    bool _recording = false;
};

constexpr double kFrame320 = 71680.0 / CPU_CLOCK_RATE;  // 20.48 ms
constexpr double kFrame312 = 69888.0 / CPU_CLOCK_RATE;  // 19.968 ms
}  // namespace

/// The frame length alternates between frames: every timestamp is the sum of
/// the durations of the frames before it, so they only grow
TEST(RecordingManager_Test, FrameLengthChange_TimestampsAreTheSumOfTheFrames)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    RecordingManager* rm = context->pRecordingManager;
    ASSERT_NE(rm, nullptr);

    std::vector<double> timestamps;
    rm->SetVideoResolution(context->pScreen->GetFramebufferDescriptor().width,
                           context->pScreen->GetFramebufferDescriptor().height);
    ASSERT_TRUE(rm->StartRecordingWithEncoder(TestPathHelper::GetUniqueTestScratchPath("rm-timestamps") + ".mp4",
                                              std::make_unique<TimestampSinkEncoder>(timestamps)));

    const uint32_t savedFrame = context->config.frame;
    const FramebufferDescriptor fb = context->pScreen->GetFramebufferDescriptor();
    double expected = 0.0;
    for (int i = 0; i < 200; i++)
    {
        // 100 frames of 320 lines, then 312 / 320 alternating
        const bool short312 = i >= 100 && (i & 1);
        context->config.frame = short312 ? 69888 : 71680;
        rm->CaptureFrame(fb);
        ASSERT_EQ(timestamps.size(), static_cast<size_t>(i + 1));
        EXPECT_NEAR(timestamps[i], expected, 1e-9) << "frame " << i;
        if (i > 0)
            ASSERT_GT(timestamps[i], timestamps[i - 1]) << "frame " << i << ": the timestamps only grow";
        expected += short312 ? kFrame312 : kFrame320;
    }
    context->config.frame = savedFrame;

    rm->StopRecording();
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// The same on a running Sprinter: the frame length codes #2D / #2C switch the
/// frame at the next frame start, the recording follows each frame's length.
/// ~0.3 s: 60 rendered Sprinter frames (the BIOS runs from the fast start)
TEST(RecordingManager_Test, SprinterFrameCodes_TimestampsFollowEachFrame)
{
    const std::filesystem::path rom = TestPathHelper::FindProjectRoot() / "data" / "rom" / "sprinter" / "sp2k-3.04.rom";
    if (!FileHelper::FileExists(rom.string()))
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";

    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator =
        manager->CreateEmulatorWithModelAndRAM("sprinter-rec-timestamps", "SPRINTER", 4096, LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoder_Sprinter*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    context->config.sprinter.fast_start = 1;
    emulator->Reset();

    RecordingManager* rm = context->pRecordingManager;
    std::vector<double> timestamps;
    rm->SetVideoResolution(context->pScreen->GetFramebufferDescriptor().width,
                           context->pScreen->GetFramebufferDescriptor().height);
    ASSERT_TRUE(rm->StartRecordingWithEncoder(TestPathHelper::GetUniqueTestScratchPath("sprinter-timestamps") + ".mp4",
                                              std::make_unique<TimestampSinkEncoder>(timestamps)));

    for (int i = 0; i < 20; i++)
    {
        decoder->StandardWriteCode((i & 1) ? SprinterCode::Frame312 : SprinterCode::Frame320, 0, 0);
        emulator->RunNFrames(3);
    }
    rm->StopRecording();

    ASSERT_GE(timestamps.size(), 50u);
    unsigned short312 = 0;
    for (size_t i = 1; i < timestamps.size(); i++)
    {
        const double step = timestamps[i] - timestamps[i - 1];
        ASSERT_GT(step, 0.0) << "frame " << i << ": the timestamps only grow";
        const bool is312 = std::abs(step - kFrame312) < 1e-9;
        EXPECT_TRUE(is312 || std::abs(step - kFrame320) < 1e-9) << "frame " << i << ": " << step;
        short312 += is312 ? 1u : 0u;
    }
    EXPECT_GT(short312, 10u) << "the 312-line frames are in the recording";

    const std::string id = emulator->GetId();
    emulator.reset();
    manager->RemoveEmulator(id);
}
