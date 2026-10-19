/// @file windowdisplay_test.cpp
/// @brief The GUI window's display settings every automation surface reads and changes (WindowDisplay: WebAPI
/// /video/window, CLI `video window`, Lua / Python window_display(), MCP capture_media window_display): no window
/// headless, the words a change takes, the validation against the window's offer, the report.
///
/// No machine: the front end is a fake provider.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "../../automation/windowdisplay.h"

namespace
{
/// A front end that keeps the settings and counts what it was asked to apply
class FakeDisplay : public IWindowViewProvider
{
public:
    WindowDisplaySettings settings;
    int applied = 0;

    FakeDisplay()
    {
        settings.backend = "software";
        settings.gpuAvailable = false;
        settings.crtProfile = "none";
        settings.crtProfiles = {"none", "basic", "shadow-mask"};
        settings.blendingWeights = "equal";
        settings.emulatorId = "fake-id";
    }

    bool GrabWindowView(const std::string&, WindowViewGrab&, std::string& error) override
    {
        error = "not in this test";
        return false;
    }
    bool GetWindowDisplay(WindowDisplaySettings& out, std::string&) override
    {
        out = settings;
        return true;
    }
    bool SetWindowDisplay(const WindowDisplayChange& change, WindowDisplaySettings& out, std::string&) override
    {
        ++applied;
        if (change.gpu)
        {
            settings.gpu = *change.gpu;
            settings.backend = settings.gpu ? "gpu" : "software";
        }
        if (change.crtProfile)
        {
            settings.crtProfile = *change.crtProfile;
            settings.crt = *change.crtProfile != "none";
        }
        if (change.crt)
            settings.crt = *change.crt;
        if (change.blending)
            settings.blending = *change.blending;
        if (change.blendingFrames)
            settings.blendingFrames = *change.blendingFrames;
        if (change.blendingWeights)
            settings.blendingWeights = *change.blendingWeights;
        out = settings;
        return true;
    }
};

/// Registers a provider for one test, and always takes it away again: the registry is process-wide
struct ScopedDisplay
{
    explicit ScopedDisplay(std::shared_ptr<IWindowViewProvider> provider) { WindowView::SetProvider(std::move(provider)); }
    ~ScopedDisplay() { WindowView::SetProvider(nullptr); }
};
}  // namespace

TEST(WindowDisplay_Test, HeadlessThereIsNoWindow)
{
    WindowView::SetProvider(nullptr);
    const WindowDisplay::Result get = WindowDisplay::Get();
    EXPECT_EQ(get.status, WindowDisplay::Status::NoWindow);
    EXPECT_STREQ(WindowDisplay::StatusName(get.status), "no-window");

    WindowDisplayChange change;
    change.crt = true;
    EXPECT_EQ(WindowDisplay::Set(change).status, WindowDisplay::Status::NoWindow);
}

TEST(WindowDisplay_Test, WordsMakeAChange)
{
    WindowDisplayChange change;
    std::string error;
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "gpu", "on", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "CRT", "False", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "crt_profile", "Shadow-Mask", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "blending", "1", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "blending_frames", "5", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "blending_weights", "exponential", error));
    EXPECT_EQ(change.gpu, true);
    EXPECT_EQ(change.crt, false);
    EXPECT_EQ(change.crtProfile, "shadow-mask") << "profile words in any case";
    EXPECT_EQ(change.blending, true);
    EXPECT_EQ(change.blendingFrames, 5);
    EXPECT_EQ(change.blendingWeights, "exponential");

    EXPECT_FALSE(WindowDisplay::ApplyWord(change, "gpu", "maybe", error));
    EXPECT_NE(error.find("on or off"), std::string::npos) << error;
    EXPECT_FALSE(WindowDisplay::ApplyWord(change, "blending_frames", "6", error));
    EXPECT_FALSE(WindowDisplay::ApplyWord(change, "blending_frames", "3x", error));
    EXPECT_FALSE(WindowDisplay::ApplyWord(change, "blending_weights", "linear", error));
    EXPECT_FALSE(WindowDisplay::ApplyWord(change, "shader", "x", error));
    EXPECT_NE(error.find(WindowDisplay::kKeys), std::string::npos) << "an unknown key lists the keys: " << error;
}

TEST(WindowDisplay_Test, SetAppliesAndAnswersTheNewSettings)
{
    auto fake = std::make_shared<FakeDisplay>();
    ScopedDisplay registered(fake);

    WindowDisplayChange change;
    change.crtProfile = "shadow-mask";
    change.blending = true;
    change.blendingFrames = 3;
    const WindowDisplay::Result r = WindowDisplay::Set(change);
    ASSERT_TRUE(r.Ok()) << r.error;
    EXPECT_EQ(fake->applied, 1);
    EXPECT_TRUE(r.settings.crt);
    EXPECT_EQ(r.settings.crtProfile, "shadow-mask");
    EXPECT_TRUE(r.settings.blending);
    EXPECT_EQ(r.settings.blendingFrames, 3);

    // An empty change only reads
    const WindowDisplay::Result read = WindowDisplay::Set(WindowDisplayChange());
    ASSERT_TRUE(read.Ok());
    EXPECT_EQ(fake->applied, 1);
}

TEST(WindowDisplay_Test, SetRefusesWhatTheWindowDoesNotOffer)
{
    auto fake = std::make_shared<FakeDisplay>();
    ScopedDisplay registered(fake);

    WindowDisplayChange profile;
    profile.crtProfile = "megatron";  // not in this window's list
    const WindowDisplay::Result badProfile = WindowDisplay::Set(profile);
    EXPECT_EQ(badProfile.status, WindowDisplay::Status::BadParameter);
    EXPECT_NE(badProfile.error.find("none, basic, shadow-mask"), std::string::npos) << badProfile.error;

    WindowDisplayChange gpu;
    gpu.gpu = true;
    const WindowDisplay::Result noGpu = WindowDisplay::Set(gpu);
    EXPECT_EQ(noGpu.status, WindowDisplay::Status::Unavailable);
    EXPECT_STREQ(WindowDisplay::StatusName(noGpu.status), "unavailable");
    EXPECT_EQ(fake->applied, 0) << "nothing applied after a refusal";

    gpu.gpu = false;  // off is always possible
    EXPECT_TRUE(WindowDisplay::Set(gpu).Ok());
}

TEST(WindowDisplay_Test, ReportAndSummary)
{
    FakeDisplay fake;
    fake.settings.crt = true;
    fake.settings.crtProfile = "basic";
    fake.settings.blendingFrames = 4;

    const StateNode report = WindowDisplay::Report(fake.settings);
    ASSERT_EQ(report.kind, StateNode::Kind::Object);
    std::string keys;
    for (const auto& member : report.members)
        keys += member.first + " ";
    EXPECT_EQ(keys, "backend gpu_available gpu crt crt_profile crt_profiles blending blending_frames blending_weights "
                    "emulator ");

    EXPECT_EQ(WindowDisplay::Summary(fake.settings),
              "gpu off (unavailable), crt basic, blending off (4 frames, equal)");
}
