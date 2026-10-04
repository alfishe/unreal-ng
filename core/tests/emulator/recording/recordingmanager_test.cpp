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

#ifdef __APPLE__
/// The automation surfaces' "audio":"aac" reaches the recorder: a native h264 + aac recording of a running
/// machine carries the emulated sound (every frame's samples), and the next video-only session has no audio
/// track again (the default every surface keeps). The writer holds the audio input back until the picture
/// catches up; an audio chunk dropped there used to shorten the sound track (and pull it ahead of the picture).
/// ~0.7 s: two real AVAssetWriter sessions, one with an AAC track
TEST(RecordingManager_Test, AacAudioTrack_SamplesFollowTheFramesAndVideoOnlyStaysDefault)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    RecordingManager* rm = context->pRecordingManager;
    ASSERT_NE(rm, nullptr);
    context->pFeatureManager->setFeature(Features::kRecording, true);
    rm->SetEncoderBackend(EncoderBackend::Native);

    const std::string file = TestPathHelper::GetUniqueTestScratchPath("rm-aac") + ".mp4";
    ASSERT_TRUE(rm->StartRecording(file, "h264", "aac", 0, 128)) << rm->GetLastRecordingError();
    EXPECT_TRUE(rm->HasAudio());
    EXPECT_EQ(rm->GetAudioCodec(), "aac");
    EXPECT_EQ(rm->GetAudioChannels(), 2u);
    EXPECT_GT(rm->GetAudioSampleRate(), 0u);

    constexpr int kFrames = 25;
    emulator->RunNFrames(kFrames);
    const RecordingManager::RecordingStats live = rm->GetStats();
    rm->StopRecording();

    const double expectedSeconds = kFrames * static_cast<double>(context->config.frame) / CPU_CLOCK_RATE;
    EXPECT_GE(live.framesRecorded, static_cast<uint64_t>(kFrames));
    EXPECT_NEAR(rm->GetAudioDuration(), expectedSeconds, 0.021)  // within one frame
        << rm->GetStats().audioSamplesRecorded << " samples at " << rm->GetAudioSampleRate() << " Hz";
    std::error_code ec;
    EXPECT_GT(std::filesystem::file_size(file, ec), 0u) << file;

    // Video only again: no audio codec, no audio track
    const std::string videoOnly = TestPathHelper::GetUniqueTestScratchPath("rm-video-only") + ".mp4";
    ASSERT_TRUE(rm->StartRecording(videoOnly, "h264", "")) << rm->GetLastRecordingError();
    EXPECT_FALSE(rm->HasAudio());
    EXPECT_EQ(rm->GetAudioChannels(), 0u);
    emulator->RunNFrames(2);
    rm->StopRecording();
    EXPECT_EQ(rm->GetStats().audioSamplesRecorded, 0u);
    EXPECT_EQ(rm->GetAudioDuration(), 0.0);

    std::filesystem::remove(file, ec);
    std::filesystem::remove(videoOnly, ec);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
#endif  // __APPLE__

namespace
{
/// Keeps the size and a copy of every frame the recording manager hands to its encoder
class FrameSinkEncoder : public EncoderBase
{
public:
    struct Frame
    {
        uint16_t width = 0;
        uint16_t height = 0;
        std::vector<uint8_t> pixels;
    };
    explicit FrameSinkEncoder(std::vector<Frame>& frames) : _frames(frames) {}
    bool Start(const std::string&, const EncoderConfig&) override
    {
        _recording = true;
        return true;
    }
    void Stop() override { _recording = false; }
    bool IsRecording() const override { return _recording; }
    std::string GetType() const override { return "frames"; }
    std::string GetDisplayName() const override { return "frame sink"; }
    bool SupportsVideo() const override { return true; }
    bool SupportsAudio() const override { return false; }
    void OnVideoFrame(const FramebufferDescriptor& fb, double) override
    {
        Frame frame;
        frame.width = fb.width;
        frame.height = fb.height;
        frame.pixels.assign(fb.memoryBuffer, fb.memoryBuffer + static_cast<size_t>(fb.width) * fb.height * 4);
        _frames.push_back(std::move(frame));
    }

private:
    std::vector<Frame>& _frames;
    bool _recording = false;
};

/// A frame whose pixel (x, y) holds x in bytes 0-1 and y in bytes 2-3
FramebufferDescriptor CoordinateFrame(std::vector<uint8_t>& storage, uint16_t width, uint16_t height, VideoModeEnum mode)
{
    storage.assign(static_cast<size_t>(width) * height * 4, 0);
    for (uint16_t y = 0; y < height; y++)
        for (uint16_t x = 0; x < width; x++)
        {
            uint8_t* p = storage.data() + (static_cast<size_t>(y) * width + x) * 4;
            p[0] = static_cast<uint8_t>(x);
            p[1] = static_cast<uint8_t>(x >> 8);
            p[2] = static_cast<uint8_t>(y);
            p[3] = static_cast<uint8_t>(y >> 8);
        }
    FramebufferDescriptor fb;
    fb.videoMode = mode;
    fb.width = width;
    fb.height = height;
    fb.memoryBuffer = storage.data();
    fb.memoryBufferSize = storage.size();
    return fb;
}

uint16_t SourceX(const FrameSinkEncoder::Frame& frame, uint32_t x, uint32_t y)
{
    const uint8_t* p = frame.pixels.data() + (static_cast<size_t>(y) * frame.width + x) * 4;
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint16_t SourceY(const FrameSinkEncoder::Frame& frame, uint32_t x, uint32_t y)
{
    const uint8_t* p = frame.pixels.data() + (static_cast<size_t>(y) * frame.width + x) * 4;
    return static_cast<uint16_t>(p[2] | (p[3] << 8));
}
}  // namespace

/// The ZX Profi shows its 352x288 Spectrum frame and its 608x288 hi-res frame in the same 352:288 window: a
/// full-frame recording is that window at 704x576 for both, every frame scaled into it the way the screen does
/// (owner report 2026-10-04: a hi-res recording came out 1216x576, stretched horizontally, and the frames of the
/// other mode were dropped after a switch)
TEST(RecordingManager_Test, ProfiFramesKeepTheScreensWindowInBothModes)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PROFI", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    RecordingManager* rm = context->pRecordingManager;
    ASSERT_NE(rm, nullptr);

    std::vector<FrameSinkEncoder::Frame> frames;
    rm->SetScaleFactor(2);   // "Native 2x": the screen's window at 704x576
    ASSERT_TRUE(rm->StartRecordingWithEncoder(TestPathHelper::GetUniqueTestScratchPath("rm-profi") + ".mp4",
                                              std::make_unique<FrameSinkEncoder>(frames)));

    std::vector<uint8_t> normal;
    std::vector<uint8_t> hires;
    rm->CaptureFrame(CoordinateFrame(normal, 352, 288, M_ZX48));
    rm->CaptureFrame(CoordinateFrame(hires, 608, 288, M_PROFIHR));
    rm->CaptureFrame(CoordinateFrame(normal, 352, 288, M_ZX48));
    ASSERT_EQ(frames.size(), 3u) << "a mode switch keeps recording";
    for (const auto& frame : frames)
    {
        EXPECT_EQ(frame.width, 704u);
        EXPECT_EQ(frame.height, 576u);
    }

    // Spectrum frame: every pixel twice in each direction
    EXPECT_EQ(SourceX(frames[0], 0, 0), 0u);
    EXPECT_EQ(SourceX(frames[0], 703, 575), 351u);
    EXPECT_EQ(SourceY(frames[0], 703, 575), 287u);
    EXPECT_EQ(SourceX(frames[0], 351, 0), 175u);
    // Hi-res frame: 608 columns across the same 704, each line twice
    EXPECT_EQ(SourceX(frames[1], 0, 0), 0u);
    EXPECT_EQ(SourceX(frames[1], 703, 0), 607u);
    EXPECT_EQ(SourceX(frames[1], 352, 0), 304u) << "the middle stays the middle";
    EXPECT_EQ(SourceY(frames[1], 0, 575), 287u);
    EXPECT_EQ(SourceY(frames[1], 0, 1), 0u);

    rm->StopRecording();
    rm->SetScaleFactor(1);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

namespace
{
/// Width and height from the GIF header (logical screen descriptor, little endian)
bool GifSize(const std::string& path, uint32_t& width, uint32_t& height)
{
    FILE* f = FileHelper::OpenFile(path, "rb");
    if (!f)
        return false;
    uint8_t header[10] = {};
    const size_t got = std::fread(header, 1, sizeof(header), f);
    FileHelper::CloseFile(f);
    if (got != sizeof(header))
        return false;
    width = header[6] | (header[7] << 8);
    height = header[8] | (header[9] << 8);
    return true;
}
}  // namespace

/// The picture size of a recording follows its own capture region: a second recording on the same emulator
/// must not inherit the size of the first (it did: the frames of the new size were dropped, an empty or
/// black file). Only a size the caller sets explicitly is kept
TEST(RecordingManager_Test, VideoSize_EachRecordingDerivesItsOwn)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    RecordingManager* rm = context->pRecordingManager;
    ASSERT_NE(rm, nullptr);
    context->pFeatureManager->setFeature(Features::kRecording, true);
    emulator->RunNFrames(2);

    const PictureGeometry g = context->pScreen->DescribeCurrentFrame();
    ASSERT_NE(g.screenWindow.width, g.width) << "the Pentagon window is smaller than its frame";

    auto record = [&](VideoCaptureRegion region, const char* name, uint32_t& w, uint32_t& h) {
        rm->SetCaptureRegion(region);
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(name) + ".gif";
        ASSERT_TRUE(rm->StartRecording(path, "gif"));
        rm->CaptureFrame(context->pScreen->GetFramebufferDescriptor());
        rm->StopRecording();
        ASSERT_TRUE(GifSize(path, w, h)) << path;
        std::remove(path.c_str());
    };

    uint32_t w = 0, h = 0;
    record(VideoCaptureRegion::MainScreen, "rm-size-screen", w, h);
    EXPECT_EQ(w, g.screenWindow.width);
    EXPECT_EQ(h, g.screenWindow.height);

    record(VideoCaptureRegion::FullFrame, "rm-size-full", w, h);
    EXPECT_EQ(w, g.width) << "the second recording takes the full frame, not the first one's size";
    EXPECT_EQ(h, g.height);

    // An explicit size stays until it is cleared
    rm->SetVideoResolution(100, 80);
    record(VideoCaptureRegion::MainScreen, "rm-size-explicit", w, h);
    EXPECT_EQ(w, 100u);
    EXPECT_EQ(h, 80u);

    EmulatorTestHelper::CleanupEmulator(emulator);
}
