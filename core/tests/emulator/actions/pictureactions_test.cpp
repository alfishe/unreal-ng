// The catalogue's picture and window actions (Stream Deck use case C9, pictureactions.h): headless they are listed and
// unavailable; with a front end (a fake provider here) each one is a WindowDisplay change, its toggle reads the
// published copy; the overscan viewport on a real Pentagon (not started: nothing runs the CPU), and the feature
// `overscan` that switches the same frame.

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/readouts.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"
#include "emulator/video/viewportcontrol.h"
#include "emulator/video/windowdisplay.h"
#include "pch.h"

using namespace actions;

namespace
{
ActionValue Press(std::initializer_list<std::pair<const char*, StateNode>> params = {})
{
    ActionValue value;
    for (const auto& [name, node] : params)
        value.params[name] = node;
    return value;
}

ActionValue Delta(double delta, std::initializer_list<std::pair<const char*, StateNode>> params = {})
{
    ActionValue value = Press(params);
    value.kind = ValueKind::Delta;
    value.delta = delta;
    return value;
}

ActionOrigin Hands()
{
    ActionOrigin origin;
    origin.origin = execution::Origin::HostDevice;
    origin.identity = "deck-picture";
    origin.client = "deck-picture";
    return origin;
}

/// A front end that keeps the settings, applies a change the way the window does and publishes after it
class FakeWindow : public IWindowViewProvider
{
public:
    WindowDisplaySettings settings;
    CrtParameters parameters;
    CrtParameterChange lastParameters;

    FakeWindow()
    {
        settings.backend = "gpu";
        settings.gpuAvailable = true;
        settings.gpu = true;
        settings.crtProfile = "none";
        settings.crtProfiles = {"none", "basic", "shader:crt-geom"};
        settings.blendingWeights = "equal";
        settings.scale = 2;
        settings.hudTheme = "dark_glass";
        settings.hudThemes = {"dark_glass", "retro_zx", "cyberpunk"};
        settings.hudNotices = {{"ram-page", true}, {"tape-events", false}};
        settings.osdKeyboardPictures = {"pentagon", "pentagon2plus"};
        settings.osdKeyboardPicture = "pentagon";
        settings.osdKeyboardStyles = {"photo", "schematic", "hybrid"};
        settings.osdKeyboardStyle = "photo";
        settings.nextLayersMode = "layers";
        settings.nextLayersShown = {{"ula", true}, {"layer2", true}, {"tilemap", true}, {"sprites", true}};
        parameters.profile = "basic";
        parameters.kind = "profile";
        CrtParameter curvature;
        curvature.name = "curvature";
        curvature.value = 0.02;
        curvature.initial = 0.02;
        curvature.maximum = 0.5;
        curvature.step = 0.01;
        parameters.parameters.push_back(curvature);
        WindowView::Publish(settings, parameters);
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
        auto set = [](auto& field, const auto& value) {
            if (value)
                field = *value;
        };
        set(settings.gpu, change.gpu);
        set(settings.crt, change.crt);
        if (change.crtProfile)
        {
            settings.crtProfile = *change.crtProfile;
            settings.crt = *change.crtProfile != "none";
        }
        set(settings.blending, change.blending);
        set(settings.blendingFrames, change.blendingFrames);
        set(settings.blendingWeights, change.blendingWeights);
        set(settings.blendingDecay, change.blendingDecay);
        set(settings.fullscreen, change.fullscreen);
        set(settings.scale, change.scale);
        set(settings.toolbar, change.toolbar);
        set(settings.statusBar, change.statusBar);
        set(settings.hud, change.hud);
        set(settings.hudTheme, change.hudTheme);
        for (const auto& [id, on] : change.hudNotices)
            settings.hudNotices[id] = on;
        set(settings.osdKeyboard, change.osdKeyboard);
        set(settings.osdKeyboardPicture, change.osdKeyboardPicture);
        set(settings.osdKeyboardStyle, change.osdKeyboardStyle);
        set(settings.nextLayers, change.nextLayers);
        set(settings.nextLayersMode, change.nextLayersMode);
        for (const auto& [name, on] : change.nextLayersShown)
            settings.nextLayersShown[name] = on;
        out = settings;
        WindowView::Publish(settings, parameters);
        return true;
    }
    bool GetCrtParameters(CrtParameters& out, std::string&) override
    {
        out = parameters;
        return true;
    }
    bool SetCrtParameters(const CrtParameterChange& change, CrtParameters& out, std::string&) override
    {
        lastParameters = change;
        for (CrtParameter& p : parameters.parameters)
        {
            if (change.resetAll)
                p.value = p.initial;
            for (const auto& [name, steps] : change.steps)
                if (name == p.name)
                    p.value += steps * p.step;
        }
        out = parameters;
        WindowView::Publish(settings, parameters);
        return true;
    }
};

struct ScopedWindow
{
    explicit ScopedWindow(std::shared_ptr<IWindowViewProvider> provider) { WindowView::SetProvider(std::move(provider)); }
    ~ScopedWindow() { WindowView::SetProvider(nullptr); }
};
}  // namespace

class PictureActions_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        WindowView::SetProvider(nullptr);
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _id = _emulator->GetId();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    ActionResult Run(const char* id, ActionValue value)
    {
        return ActionRegistry::Instance().Run(id, std::move(value), _id, Hands());
    }

    std::optional<bool> Toggle(const char* id, StateNode params = StateNode::Object())
    {
        return ActionRegistry::Instance().ToggleState(id, _id, params);
    }

    Emulator* _emulator = nullptr;
    std::string _id;
};

TEST_F(PictureActions_Test, HeadlessTheWindowActionsAreListedAndSayThereIsNoWindow)
{
    ActionRegistry& registry = ActionRegistry::Instance();
    for (const char* id : {"video.gpu_toggle", "video.crt_toggle", "video.crt_profile", "video.crt_param_by",
                           "video.crt_params_reset", "video.blending", "video.blending_decay_by", "video.temporal_toggle",
                           "video.temporal_effect", "video.viewport",
                           "view.fullscreen", "view.scale", "view.toolbar", "view.status_bar", "view.next_layers",
                           "view.next_layers_mode", "view.next_layer", "hud.toggle", "hud.theme", "hud.notice",
                           "osd.keyboard_picture", "osd.keyboard_style"})
    {
        const std::optional<ActionInfo> info = registry.Find(id);
        ASSERT_TRUE(info) << id;
        EXPECT_FALSE(info->gui) << id << ": a core action";
        EXPECT_FALSE(info->description.empty()) << id;
    }
    EXPECT_EQ(registry.Find("video.crt_param_by")->kind, ValueKind::Delta);
    EXPECT_TRUE(registry.Find("hud.toggle")->toggle);
    EXPECT_TRUE(registry.Find("hud.notice")->toggle);
    EXPECT_FALSE(registry.Find("hud.theme")->toggle);

    EXPECT_NE(registry.Unavailable("video.gpu_toggle", _id, Hands(), Press()).find("no window"), std::string::npos);
    EXPECT_EQ(Run("hud.toggle", Press()).error, "not_now");
    EXPECT_EQ(Toggle("hud.toggle"), std::nullopt) << "nothing published headless";
    EXPECT_FALSE(ReadReadout(*_emulator, "video.window").find("available")->b);
    EXPECT_TRUE(registry.Unavailable("video.viewport", _id, Hands(), Press()).empty()) << "the viewport is the machine's";
}

TEST_F(PictureActions_Test, AWindowActionChangesTheWindowAndItsToggleReadsThePublishedCopy)
{
    auto window = std::make_shared<FakeWindow>();
    ScopedWindow registered(window);

    EXPECT_EQ(Toggle("hud.toggle"), false);
    ASSERT_TRUE(Run("hud.toggle", Press()).ok) << "flips";
    EXPECT_TRUE(window->settings.hud);
    EXPECT_EQ(Toggle("hud.toggle"), true);
    ASSERT_TRUE(Run("hud.toggle", Press({{"on", true}})).ok);
    EXPECT_TRUE(window->settings.hud) << "on stays on";

    ASSERT_TRUE(Run("view.fullscreen", Press()).ok);
    EXPECT_TRUE(window->settings.fullscreen);
    ASSERT_TRUE(Run("view.toolbar", Press({{"on", false}})).ok);
    EXPECT_FALSE(window->settings.toolbar);
    EXPECT_EQ(Toggle("view.toolbar"), false);

    // Notices and layers by name: `toggle.hud.notice.<id>`
    StateNode tape = StateNode::Object();
    tape["name"] = "tape-events";
    EXPECT_EQ(Toggle("hud.notice", tape), false);
    ASSERT_TRUE(Run("hud.notice", Press({{"name", "tape-events"}})).ok);
    EXPECT_TRUE(window->settings.hudNotices["tape-events"]);
    EXPECT_TRUE(ReadReadout(*_emulator, "toggle.hud.notice.tape-events").find("on")->b);
    EXPECT_EQ(Run("hud.notice", Press({{"name", "no-such"}})).error, "Bad Request");
    ASSERT_TRUE(Run("view.next_layer", Press({{"name", "sprites"}, {"on", false}})).ok);
    EXPECT_FALSE(window->settings.nextLayersShown["sprites"]);

    const StateNode readout = ReadReadout(*_emulator, "video.window");
    EXPECT_TRUE(readout.find("available")->b);
    EXPECT_TRUE(readout.find("hud")->b);
}

TEST_F(PictureActions_Test, CyclingActionsGoToANameOrTheNextAndRefuseWhatIsNotOffered)
{
    auto window = std::make_shared<FakeWindow>();
    ScopedWindow registered(window);

    ASSERT_TRUE(Run("video.crt_profile", Press()).ok);
    EXPECT_EQ(window->settings.crtProfile, "basic") << "next after none";
    ASSERT_TRUE(Run("video.crt_profile", Press({{"profile", "prev"}})).ok);
    EXPECT_EQ(window->settings.crtProfile, "none");
    EXPECT_EQ(Run("video.crt_profile", Press({{"profile", "crt-royale"}})).error, "Bad Request");

    window->settings.gpu = false;  // a shader is skipped without the GPU
    ASSERT_TRUE(Run("video.crt_profile", Press({{"profile", "prev"}})).ok);
    EXPECT_EQ(window->settings.crtProfile, "basic");

    ASSERT_TRUE(Run("hud.theme", Press({{"theme", "prev"}})).ok);
    EXPECT_EQ(window->settings.hudTheme, "cyberpunk") << "prev wraps around";
    ASSERT_TRUE(Run("view.scale", Press()).ok);
    EXPECT_EQ(window->settings.scale, 3);
    EXPECT_EQ(Run("view.scale", Press({{"scale", 7}})).error, "Bad Request");
    ASSERT_TRUE(Run("osd.keyboard_picture", Press()).ok);
    EXPECT_EQ(window->settings.osdKeyboardPicture, "pentagon2plus");
    ASSERT_TRUE(Run("osd.keyboard_style", Press()).ok);
    EXPECT_EQ(window->settings.osdKeyboardStyle, "schematic");
    ASSERT_TRUE(Run("osd.keyboard_style", Press({{"style", "prev"}})).ok);
    EXPECT_EQ(window->settings.osdKeyboardStyle, "photo");
    EXPECT_FALSE(Run("osd.keyboard_style", Press({{"style", "sketch"}})).ok) << "not a style";
    ASSERT_TRUE(Run("view.next_layers_mode", Press({{"mode", "winner"}})).ok);
    EXPECT_EQ(window->settings.nextLayersMode, "winner");
}

// As the GUI's Temporal Effects dialog: one switch, then one effect at a time (frame blending or the ZX DLSS
// de-flicker); the choice is kept while the switch is off
TEST_F(PictureActions_Test, TemporalEffectsAreOneSwitchAndOneEffectAtATime)
{
    auto window = std::make_shared<FakeWindow>();
    ScopedWindow registered(window);
    Screen* screen = _emulator->GetContext()->pScreen;
    ASSERT_NE(screen, nullptr);
    ASSERT_TRUE(Run("video.temporal_effect", Press({{"effect", "blend"}})).ok);  // the choice is process-wide

    EXPECT_EQ(Toggle("video.temporal_toggle"), false);
    ASSERT_TRUE(Run("video.temporal_toggle", Press()).ok);
    EXPECT_TRUE(window->settings.blending) << "on: the chosen effect";
    EXPECT_TRUE(screen->GetTemporalAlgorithm().empty());
    EXPECT_EQ(Toggle("video.temporal_toggle"), true);

    // The other effect while on: it switches at once and the first stops
    ASSERT_TRUE(Run("video.temporal_effect", Press()).ok);
    EXPECT_FALSE(window->settings.blending);
    EXPECT_EQ(screen->GetTemporalAlgorithm(), "mod-tpgwafsd");
    EXPECT_TRUE(ReadReadout(*_emulator, "video.temporal").find("dlss")->b);

    // Off stops it and keeps the choice; on again runs the de-flicker, not the blending
    ASSERT_TRUE(Run("video.temporal_toggle", Press()).ok);
    EXPECT_FALSE(window->settings.blending);
    EXPECT_TRUE(screen->GetTemporalAlgorithm().empty());
    EXPECT_EQ(Toggle("video.temporal_toggle"), false);
    EXPECT_EQ(ReadReadout(*_emulator, "video.temporal").find("effect")->s, "dlss");
    ASSERT_TRUE(Run("video.temporal_toggle", Press({{"on", true}})).ok);
    EXPECT_EQ(screen->GetTemporalAlgorithm(), "mod-tpgwafsd");
    EXPECT_FALSE(window->settings.blending);

    // A choice while off runs nothing until the switch
    ASSERT_TRUE(Run("video.temporal_toggle", Press({{"on", false}})).ok);
    ASSERT_TRUE(Run("video.temporal_effect", Press({{"effect", "blend"}})).ok);
    EXPECT_FALSE(window->settings.blending);
    EXPECT_TRUE(screen->GetTemporalAlgorithm().empty());
    EXPECT_FALSE(ReadReadout(*_emulator, "video.temporal").find("dlss")->b);
    EXPECT_EQ(Run("video.temporal_effect", Press({{"effect", "sepia"}})).error, "Bad Request");
}

TEST_F(PictureActions_Test, BlendingAndTheDialsMoveTheirValues)
{
    auto window = std::make_shared<FakeWindow>();
    ScopedWindow registered(window);

    ASSERT_TRUE(Run("video.blending", Press({{"frames", 4}})).ok);
    EXPECT_FALSE(window->settings.blending) << "only tuning: the switch stays";
    EXPECT_EQ(window->settings.blendingFrames, 4);
    ASSERT_TRUE(Run("video.blending", Press({{"decay", 0.3}})).ok);
    EXPECT_DOUBLE_EQ(window->settings.blendingDecay, 0.3);
    ASSERT_TRUE(Run("video.blending", Press()).ok);
    EXPECT_TRUE(window->settings.blending);

    ASSERT_TRUE(Run("video.blending_decay_by", Delta(2)).ok);
    EXPECT_DOUBLE_EQ(window->settings.blendingDecay, 0.5);
    EXPECT_EQ(window->settings.blendingWeights, "exponential");
    ASSERT_TRUE(Run("video.blending_decay_by", Delta(20)).ok);
    EXPECT_DOUBLE_EQ(window->settings.blendingDecay, 0.9) << "clamped";

    const ActionResult nudged = Run("video.crt_param_by", Delta(3, {{"name", "curvature"}}));
    ASSERT_TRUE(nudged.ok) << nudged.message;
    ASSERT_EQ(window->lastParameters.steps.size(), 1u);
    EXPECT_DOUBLE_EQ(window->lastParameters.steps[0].second, 3.0);
    EXPECT_NEAR(nudged.body.find("value")->d, 0.05, 1e-9);
    EXPECT_NEAR(ReadReadout(*_emulator, "video.crt_params").find("values")->find("curvature")->d, 0.05, 1e-9);
    EXPECT_EQ(Run("video.crt_param_by", Delta(1, {{"name", "no-such"}})).error, "Bad Request");
    ASSERT_TRUE(Run("video.crt_params_reset", Press()).ok);
    EXPECT_TRUE(window->lastParameters.resetAll);
}

TEST_F(PictureActions_Test, TheViewportCropsTheOverscanFrameOnly)
{
    EXPECT_EQ(Run("video.viewport", Press({{"viewport", "standard"}})).error, "Conflict") << "no overscan yet";
    ASSERT_TRUE(Run("video.overscan_toggle", Press({{"on", true}})).ok);
    EXPECT_EQ(VideoViewport::NameOf(_emulator->GetDisplayViewport()), "symmetric") << "the GUI's default";
    ASSERT_TRUE(Run("video.viewport", Press()).ok);
    EXPECT_EQ(VideoViewport::NameOf(_emulator->GetDisplayViewport()), "standard") << "next";
    ASSERT_TRUE(Run("video.viewport", Press({{"viewport", "screen-only"}})).ok);
    const StateNode readout = ReadReadout(*_emulator, "video.viewport");
    EXPECT_EQ(readout.find("viewport")->s, "screen_only");
    EXPECT_EQ(readout.find("display")->s, "256x192");
    EXPECT_EQ(Run("video.viewport", Press({{"viewport", "huge"}})).error, "Bad Request");
    ASSERT_TRUE(Run("video.overscan_toggle", Press({{"on", false}})).ok);
    EXPECT_EQ(VideoViewport::NameOf(_emulator->GetDisplayViewport()), "full");
}

TEST_F(PictureActions_Test, TheOverscanFeatureSwitchesTheFrameAndFollowsIt)
{
    FeatureManager* features = _emulator->GetFeatureManager();
    ASSERT_NE(features, nullptr);
    ASSERT_TRUE(features->setFeature(Features::kOverscan, true));
    EXPECT_TRUE(_emulator->IsOverscanMode()) << "the feature switches the frame";
    EXPECT_EQ(VideoViewport::NameOf(_emulator->GetDisplayViewport()), "symmetric");

    ASSERT_TRUE(VideoViewport::SetOverscan(*_emulator, false).Ok());
    EXPECT_FALSE(features->isEnabled(Features::kOverscan)) << "the frame switched elsewhere: the feature follows";
}

TEST(PictureActionsNoFrame_Test, AMachineWithoutTheFrameKeepsTheFeatureOff)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    FeatureManager* features = emulator->GetFeatureManager();
    features->setFeature(Features::kOverscan, true);
    EXPECT_FALSE(emulator->IsOverscanMode());
    EXPECT_FALSE(features->isEnabled(Features::kOverscan)) << "no frame on a 48K: the switch says so";
    const VideoViewport::Result r = VideoViewport::SetOverscan(*emulator, true);
    EXPECT_EQ(r.status, 409);
    EXPECT_EQ(VideoViewport::SetViewport(*emulator, "full").status, 200) << "full is always the picture without overscan";
    EmulatorTestHelper::CleanupEmulator(emulator);
}
