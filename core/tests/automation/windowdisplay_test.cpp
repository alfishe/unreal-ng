/// @file windowdisplay_test.cpp
/// @brief The GUI window's display settings every automation surface reads and changes (WindowDisplay: WebAPI
/// /video/window, CLI `video window`, Lua / Python window_display(), MCP capture_media window_display): no window
/// headless, the words a change takes, the validation against the window's offer, the report.
///
/// No machine: the front end is a fake provider.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

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
        settings.crtProfiles = {"none", "basic", "crt-lottes", "shader:mine"};
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
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "crt_profile", "CRT-Lottes", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "blending", "1", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "blending_frames", "5", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "blending_weights", "exponential", error));
    EXPECT_EQ(change.gpu, true);
    EXPECT_EQ(change.crt, false);
    EXPECT_EQ(change.crtProfile, "crt-lottes") << "profile words in any case";
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
    change.crtProfile = "crt-lottes";
    change.blending = true;
    change.blendingFrames = 3;
    const WindowDisplay::Result r = WindowDisplay::Set(change);
    ASSERT_TRUE(r.Ok()) << r.error;
    EXPECT_EQ(fake->applied, 1);
    EXPECT_TRUE(r.settings.crt);
    EXPECT_EQ(r.settings.crtProfile, "crt-lottes");
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
    profile.crtProfile = "crt-royale";  // not in this window's list
    const WindowDisplay::Result badProfile = WindowDisplay::Set(profile);
    EXPECT_EQ(badProfile.status, WindowDisplay::Status::BadParameter);
    EXPECT_NE(badProfile.error.find("none, basic, crt-lottes"), std::string::npos) << badProfile.error;

    WindowDisplayChange shader;
    shader.crtProfile = "shader:mine";  // offered, but runs on the GPU only
    const WindowDisplay::Result noGpuShader = WindowDisplay::Set(shader);
    EXPECT_EQ(noGpuShader.status, WindowDisplay::Status::Unavailable);
    EXPECT_NE(noGpuShader.error.find("gpu=on"), std::string::npos) << noGpuShader.error;

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
                    "blending_decay emulator fullscreen scale toolbar status_bar hud hud_theme hud_themes hud_notices "
                    "osd_keyboard osd_keyboard_picture osd_keyboard_pictures osd_keyboard_style osd_keyboard_styles next_layers "
                    "next_layers_mode "
                    "next_layers_shown ");

    EXPECT_EQ(WindowDisplay::Summary(fake.settings),
              "gpu off (unavailable), crt basic, blending off (4 frames, equal), scale free, hud off");
    fake.settings.blendingWeights = "exponential";
    fake.settings.blendingDecay = 0.3;
    fake.settings.scale = 2;
    fake.settings.hud = true;
    fake.settings.hudTheme = "retro_zx";
    EXPECT_EQ(WindowDisplay::Summary(fake.settings),
              "gpu off (unavailable), crt basic, blending off (4 frames, exponential 0.3), scale 2x, hud on (retro_zx)");
}

TEST(WindowDisplay_Test, TheWindowsOtherWordsMakeAChange)
{
    WindowDisplayChange change;
    std::string error;
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "blending_decay", "0.3", error)) << error;
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "fullscreen", "on", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "scale", "3", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "toolbar", "off", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "status_bar", "false", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "hud", "yes", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "hud_theme", "Retro_ZX", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "hud_notice.ram-page", "off", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "osd_keyboard", "1", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "osd_keyboard_picture", "pentagon2plus", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "osd_keyboard_style", "Hybrid", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "next_layers", "on", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "next_layers_mode", "Winner", error));
    EXPECT_TRUE(WindowDisplay::ApplyWord(change, "next_layer.sprites", "off", error));
    EXPECT_DOUBLE_EQ(*change.blendingDecay, 0.3);
    EXPECT_EQ(change.fullscreen, true);
    EXPECT_EQ(change.scale, 3);
    EXPECT_EQ(change.toolbar, false);
    EXPECT_EQ(change.statusBar, false);
    EXPECT_EQ(change.hud, true);
    EXPECT_EQ(change.hudTheme, "retro_zx");
    EXPECT_EQ(change.hudNotices.at("ram-page"), false);
    EXPECT_EQ(change.osdKeyboard, true);
    EXPECT_EQ(change.osdKeyboardPicture, "pentagon2plus");
    EXPECT_EQ(change.osdKeyboardStyle, "hybrid");
    EXPECT_EQ(change.nextLayers, true);
    EXPECT_EQ(change.nextLayersMode, "winner");
    EXPECT_EQ(change.nextLayersShown.at("sprites"), false);
    EXPECT_FALSE(change.Empty());

    EXPECT_FALSE(WindowDisplay::ApplyWord(change, "blending_decay", "0.95", error));
    EXPECT_FALSE(WindowDisplay::ApplyWord(change, "blending_decay", "0,3", error)) << "a locale's comma is no number";
    EXPECT_FALSE(WindowDisplay::ApplyWord(change, "scale", "5", error));
    EXPECT_FALSE(WindowDisplay::ApplyWord(change, "scale", "2.5", error));
    EXPECT_FALSE(WindowDisplay::ApplyWord(change, "next_layers_mode", "stacked", error));
    EXPECT_FALSE(WindowDisplay::ApplyWord(change, "next_layer.copper", "on", error));
    EXPECT_FALSE(WindowDisplay::ApplyWord(change, "hud_notice.ram-page", "maybe", error));
}

TEST(WindowDisplay_Test, NumbersInWordsIgnoreTheLocale)
{
    double value = 0;
    EXPECT_TRUE(WindowDisplay::ParseNumber("0.3", value));
    EXPECT_DOUBLE_EQ(value, 0.3);
    EXPECT_TRUE(WindowDisplay::ParseNumber("-2", value));
    EXPECT_DOUBLE_EQ(value, -2.0);
    EXPECT_TRUE(WindowDisplay::ParseNumber(".5", value));
    EXPECT_DOUBLE_EQ(value, 0.5);
    for (const char* bad : {"", "-", ".", "1e3", "0,5", "1.2.3", "x"})
        EXPECT_FALSE(WindowDisplay::ParseNumber(bad, value)) << bad;
    EXPECT_EQ(WindowDisplay::NumberWord(0.3), "0.3");
    EXPECT_EQ(WindowDisplay::NumberWord(2.0), "2");
    EXPECT_EQ(WindowDisplay::NumberWord(-1.25), "-1.25");
    EXPECT_EQ(WindowDisplay::NumberWord(0.30000001192092896), "0.3") << "a float's tail is not shown";
}

TEST(WindowDisplay_Test, SetRefusesAThemeANoticeOrAPictureTheWindowDoesNotOffer)
{
    auto fake = std::make_shared<FakeDisplay>();
    fake->settings.hudThemes = {"dark_glass", "retro_zx"};
    fake->settings.hudNotices = {{"ram-page", true}};
    ScopedDisplay registered(fake);

    WindowDisplayChange theme;
    theme.hudTheme = "neon";
    const WindowDisplay::Result badTheme = WindowDisplay::Set(theme);
    EXPECT_EQ(badTheme.status, WindowDisplay::Status::BadParameter);
    EXPECT_NE(badTheme.error.find("dark_glass, retro_zx"), std::string::npos) << badTheme.error;

    WindowDisplayChange notice;
    notice.hudNotices["no-such"] = true;
    EXPECT_EQ(WindowDisplay::Set(notice).status, WindowDisplay::Status::BadParameter);

    WindowDisplayChange picture;
    picture.osdKeyboardPicture = "zx48k";
    EXPECT_EQ(WindowDisplay::Set(picture).status, WindowDisplay::Status::Unavailable) << "no pictures for this machine";
    fake->settings.osdKeyboardPictures = {"pentagon"};
    EXPECT_EQ(WindowDisplay::Set(picture).status, WindowDisplay::Status::BadParameter);
    EXPECT_EQ(fake->applied, 0);
}

TEST(WindowDisplay_Test, CrtParametersHeadlessWordsAndValidation)
{
    WindowView::SetProvider(nullptr);
    EXPECT_EQ(WindowDisplay::GetCrtParameters().status, WindowDisplay::Status::NoWindow);

    CrtParameterChange change;
    std::string error;
    EXPECT_TRUE(WindowDisplay::ApplyCrtParameterWord(change, "curvature=0.1", error)) << error;
    EXPECT_TRUE(WindowDisplay::ApplyCrtParameterWord(change, "gamma+=2", error));
    EXPECT_TRUE(WindowDisplay::ApplyCrtParameterWord(change, "gamma-=0.5", error));
    EXPECT_TRUE(WindowDisplay::ApplyCrtParameterWord(change, "reset=bloom_radius", error));
    EXPECT_TRUE(WindowDisplay::ApplyCrtParameterWord(change, "reset", error));
    ASSERT_EQ(change.values.size(), 1u);
    EXPECT_DOUBLE_EQ(change.values[0].second, 0.1);
    ASSERT_EQ(change.steps.size(), 2u);
    EXPECT_DOUBLE_EQ(change.steps[0].second, 2.0);
    EXPECT_DOUBLE_EQ(change.steps[1].second, -0.5);
    EXPECT_EQ(change.reset, std::vector<std::string>{"bloom_radius"});
    EXPECT_TRUE(change.resetAll);
    EXPECT_FALSE(WindowDisplay::ApplyCrtParameterWord(change, "curvature", error));
    EXPECT_FALSE(WindowDisplay::ApplyCrtParameterWord(change, "curvature=high", error));

    // A window without the parameters' verbs (the default of the interface) answers so
    auto fake = std::make_shared<FakeDisplay>();
    ScopedDisplay registered(fake);
    const WindowDisplay::ParametersResult none = WindowDisplay::GetCrtParameters();
    EXPECT_EQ(none.status, WindowDisplay::Status::NoWindow);
    EXPECT_NE(none.error.find("no CRT parameters"), std::string::npos) << none.error;
}

TEST(WindowDisplay_Test, ThePublishedCopyEndsWithTheWindow)
{
    auto fake = std::make_shared<FakeDisplay>();
    WindowView::SetProvider(fake);
    WindowDisplaySettings published = fake->settings;
    published.hud = true;
    WindowView::Publish(published, CrtParameters());
    ASSERT_TRUE(WindowView::PublishedSettings());
    EXPECT_TRUE(WindowView::PublishedSettings()->hud);
    ASSERT_TRUE(WindowView::PublishedCrtParameters());
    WindowView::SetProvider(nullptr);
    EXPECT_FALSE(WindowView::PublishedSettings()) << "no window: nothing published stays";
    EXPECT_FALSE(WindowView::PublishedCrtParameters());
}
