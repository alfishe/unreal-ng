/// @file windowrecording_test.cpp
/// @brief The GUI window recording every automation surface starts, steps and stops (WindowRecording: WebAPI
/// /video/window/record*, CLI `video window record`, Lua / Python window_record*(), MCP capture_media window_record_*):
/// no window headless, the words a start takes, the ranges refused before the window is asked, the report.
///
/// The front end is a fake provider; two tests make a machine for the frames' checks (one not started, one paused).

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "emulator/emulator.h"
#include "emulator/video/windowrecording.h"

namespace
{
/// A front end that keeps the request it was asked to record and answers a status from it
class FakeRecorder : public IWindowViewProvider
{
public:
    WindowRecordingRequest request;
    WindowRecordingStatus status;
    std::string refuse;  ///< non-empty: start answers this error
    int starts = 0;

    bool GrabWindowView(const std::string&, WindowViewGrab&, std::string& error) override
    {
        error = "not in this test";
        return false;
    }
    bool GetWindowDisplay(WindowDisplaySettings&, std::string&) override { return true; }
    bool SetWindowDisplay(const WindowDisplayChange&, WindowDisplaySettings&, std::string&) override { return true; }

    bool StartWindowRecording(const std::string&, const WindowRecordingRequest& r, WindowRecordingStatus& out,
                              std::string& error) override
    {
        ++starts;
        if (!refuse.empty())
        {
            error = refuse;
            return false;
        }
        request = r;
        status.recording = true;
        status.path = r.path;
        status.keyboard = r.keyboard == "default" ? "pentagon" : r.keyboard;
        status.width = 1406;
        status.height = 1828;
        status.fps = 48.828125;
        status.audio = r.audio;
        status.sampleRate = r.audio ? 44100 : 0;
        out = status;
        return true;
    }
    bool StopWindowRecording(WindowRecordingStatus& out, std::string&) override
    {
        status.recording = false;
        out = status;
        return true;
    }
    bool GetWindowRecording(WindowRecordingStatus& out, std::string&) override
    {
        if (failAtRead > 0 && ++reads >= failAtRead)  // the writer failed: the recorder stops itself
        {
            status.recording = false;
            status.error = "ffmpeg stopped taking frames";
        }
        out = status;
        return true;
    }
    int failAtRead = 0;  ///< > 0: the recording stops at this status read
    int reads = 0;
};

/// Registers a provider for one test, and always takes it away again: the registry is process-wide
struct ScopedProvider
{
    explicit ScopedProvider(std::shared_ptr<IWindowViewProvider> provider) { WindowView::SetProvider(std::move(provider)); }
    ~ScopedProvider() { WindowView::SetProvider(nullptr); }
};

WindowRecordingRequest Valid()
{
    WindowRecordingRequest request;
    request.path = "/tmp/never-written.mov";
    return request;
}
}  // namespace

TEST(WindowRecording_Test, HeadlessThereIsNoWindow)
{
    WindowView::SetProvider(nullptr);
    EXPECT_EQ(WindowRecording::Get().status, WindowRecording::Status::NoWindow);
    EXPECT_EQ(WindowRecording::Stop().status, WindowRecording::Status::NoWindow);
    const WindowRecording::Result start = WindowRecording::Start("id", Valid());
    EXPECT_EQ(start.status, WindowRecording::Status::NoWindow);
    EXPECT_STREQ(WindowRecording::StatusName(start.status), "no-window");

    WindowRecordingRequest bad = Valid();
    bad.scale = 9;
    EXPECT_EQ(WindowRecording::Start("id", bad).status, WindowRecording::Status::BadParameter)
        << "a value out of range is refused alike with a window or without";
}

TEST(WindowRecording_Test, WordsMakeARequest)
{
    WindowRecordingRequest request;
    std::string error;
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "path", "/x/demo.mp4", error));
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "keyboard", "zxnext", error));
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "codec", "H264", error));
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "audio", "off", error));
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "fps", "25", error));
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "scale", "2", error));
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "picture_height", "1080", error));
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "width", "3840", error));
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "height", "2160", error));
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "layout", "side", error));
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "hardware", "on", error));
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "fit", "true", error));
    EXPECT_EQ(request.path, "/x/demo.mp4");
    EXPECT_EQ(request.keyboard, "zxnext");
    EXPECT_EQ(request.codec, "h264") << "codec words in any case";
    EXPECT_FALSE(request.audio);
    EXPECT_EQ(request.fps, 25);
    EXPECT_EQ(request.scale, 2);
    EXPECT_EQ(request.pictureHeight, 1080);
    EXPECT_EQ(request.frameWidth, 3840);
    EXPECT_EQ(request.frameHeight, 2160);
    EXPECT_EQ(request.layout, "side");
    EXPECT_TRUE(request.hardware);
    EXPECT_TRUE(request.fit);
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "hardware", "off", error));
    EXPECT_FALSE(request.hardware);
    EXPECT_FALSE(WindowRecording::ApplyWord(request, "hardware", "fast", error)) << "a boolean word only";
    EXPECT_FALSE(error.empty());
    error.clear();
    EXPECT_FALSE(WindowRecording::ApplyWord(request, "fit", "2x", error));
    error.clear();
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "codec", "H265", error));
    EXPECT_EQ(request.codec, "h265");

    // The keyboard: on = the window's, off / none = the picture only; audio none = off
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "keyboard", "on", error));
    EXPECT_EQ(request.keyboard, "default");
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "keyboard", "none", error));
    EXPECT_EQ(request.keyboard, "");
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "audio", "on", error));
    EXPECT_TRUE(WindowRecording::ApplyWord(request, "audio", "none", error));
    EXPECT_FALSE(request.audio);

    EXPECT_FALSE(WindowRecording::ApplyWord(request, "audio", "loud", error));
    EXPECT_FALSE(WindowRecording::ApplyWord(request, "scale", "2x", error));
    EXPECT_FALSE(WindowRecording::ApplyWord(request, "width", "-1", error));
    EXPECT_FALSE(WindowRecording::ApplyWord(request, "bitrate", "9", error));
    EXPECT_NE(error.find(WindowRecording::kKeys), std::string::npos) << "an unknown key lists the keys: " << error;
}

TEST(WindowRecording_Test, StartRefusesRangesBeforeTheWindowIsAsked)
{
    auto fake = std::make_shared<FakeRecorder>();
    ScopedProvider registered(fake);
    auto refused = [](WindowRecordingRequest request, const char* what) {
        const WindowRecording::Result r = WindowRecording::Start("id", request);
        EXPECT_EQ(r.status, WindowRecording::Status::BadParameter) << what;
        EXPECT_NE(r.error.find(what), std::string::npos) << r.error;
    };
    WindowRecordingRequest r = Valid();
    r.path.clear();
    refused(r, "path");
    r = Valid();
    r.codec = "vp9";
    refused(r, "codec");
    r = Valid();
    r.layout = "above";
    refused(r, "layout");
    r = Valid();
    r.scale = 5;
    refused(r, "scale");
    r = Valid();
    r.pictureHeight = 5000;
    refused(r, "picture_height");
    r = Valid();
    r.audio = false;
    r.fps = 101;
    refused(r, "fps");
    r = Valid();
    r.fps = 25;  // with the sound track on
    refused(r, "fps 0");
    EXPECT_EQ(fake->starts, 0) << "nothing reached the window";

    r = Valid();
    r.audio = false;
    r.fps = 25;
    EXPECT_TRUE(WindowRecording::Start("id", r).Ok()) << "a picture rate of its own without sound";
}

TEST(WindowRecording_Test, StartReportAndStop)
{
    auto fake = std::make_shared<FakeRecorder>();
    ScopedProvider registered(fake);
    WindowRecordingRequest request = Valid();
    request.keyboard = "default";
    const WindowRecording::Result started = WindowRecording::Start("id", request);
    ASSERT_TRUE(started.Ok()) << started.error;
    EXPECT_EQ(fake->request.path, request.path);

    StateNode report = WindowRecording::Report(started.recording);
    EXPECT_TRUE(report["recording"].b);
    EXPECT_EQ(report["path"].s, request.path);
    EXPECT_EQ(report["keyboard"].s, "pentagon");
    EXPECT_EQ(report["width"].i, 1406);
    EXPECT_EQ(report["sample_rate"].i, 44100);
    EXPECT_EQ(WindowRecording::Summary(started.recording),
              "window recording to /tmp/never-written.mov: 1406x1828 at 48.828 fps, sound 44100 Hz, 0 frames, "
              "keyboard pentagon");

    const WindowRecording::Result stopped = WindowRecording::Stop();
    ASSERT_TRUE(stopped.Ok());
    EXPECT_EQ(WindowRecording::Summary(stopped.recording).rfind("window recording off, last ", 0), 0u);

    fake->refuse = "No ffmpeg on the host";
    const WindowRecording::Result refused = WindowRecording::Start("id", request);
    EXPECT_EQ(refused.status, WindowRecording::Status::Unavailable);
    StateNode error = WindowRecording::ErrorNode(refused);
    EXPECT_FALSE(error["ok"].b);
    EXPECT_EQ(error["kind"].s, "unavailable");
    EXPECT_EQ(error["error"].s, "No ffmpeg on the host");
}

TEST(WindowRecording_Test, FramesNeedARangeAndAPausedMachine)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    auto fake = std::make_shared<FakeRecorder>();
    ScopedProvider registered(fake);
    EXPECT_EQ(WindowRecording::RunFrames(*emulator, 0).status, WindowRecording::Status::BadParameter);
    EXPECT_EQ(WindowRecording::RunFrames(*emulator, WindowRecording::kMaxFrames + 1).status,
              WindowRecording::Status::BadParameter);
    const WindowRecording::Result running = WindowRecording::RunFrames(*emulator, 5);
    EXPECT_EQ(running.status, WindowRecording::Status::NotPaused) << "a machine not paused runs no recorded frames";
    EXPECT_STREQ(WindowRecording::StatusName(running.status), "not-paused");
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(WindowRecording_Test, FramesEndAtOnceWhenTheRecordingStops)
{
    // 2026-11 on Windows the sound file was never opened: the recorder stopped at the first frame and the step
    // waited out its 10 s, reported as "recorded nothing" instead of the recorder's error
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    emulator->StartAsync();  // a recorded step needs a paused machine, and only a started one is paused
    ASSERT_TRUE(TestWait::For([emulator] { return emulator->GetState() == StateRun; }));
    emulator->Pause();
    ASSERT_TRUE(emulator->IsPaused());
    auto fake = std::make_shared<FakeRecorder>();
    ScopedProvider registered(fake);
    ASSERT_TRUE(WindowRecording::Start("id", Valid()).status == WindowRecording::Status::Ok);
    fake->failAtRead = 3;  // the first wait for the frame's picture
    const auto started = std::chrono::steady_clock::now();
    const WindowRecording::Result stopped = WindowRecording::RunFrames(*emulator, 5);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(1)) << "no wait for a dead recorder";
    EXPECT_EQ(stopped.status, WindowRecording::Status::Unavailable);
    EXPECT_EQ(stopped.error, "The recording stopped: ffmpeg stopped taking frames");
    EmulatorTestHelper::CleanupEmulator(emulator);
}
