/// @file linuxpresent_test.cpp
/// @brief The Linux presenter's decisions (platform/linuxpresent.h), on every platform: the window system, the VRR
/// decision from what X11 / Wayland / NVIDIA tell, the swap mode of each policy, the timed swap's steps, the vblank grid
/// from GLX_OML_sync_control's counter, the VRR check, and the RandR mode for the machine's rate.

#include <gtest/gtest.h>

#include "emulator/video/displayrate.h"
#include "platform/linuxpresent.h"

using namespace linuxpresent;

namespace
{
/// A mode with a VESA-like timing at `hz` (the pixel clock rounded to 10 kHz, as EDID gives it)
XMode Mode(uint64_t id, uint32_t w, uint32_t h, double hz, uint64_t flags = 0)
{
    XMode m;
    m.id = id;
    m.width = w;
    m.height = h;
    m.hTotal = w + 160;
    m.vTotal = h + 45;
    m.dotClock = static_cast<uint64_t>(std::llround(hz * m.hTotal * m.vTotal / 10000.0)) * 10000;
    m.flags = flags;
    return m;
}
}  // namespace

TEST(LinuxPresent_Test, TheWindowSystemComesFromQtsPlatformAndWaylandDisplay)
{
    EXPECT_EQ(WindowSystemOf("xcb", false), WindowSystem::X11);
    EXPECT_EQ(WindowSystemOf("xcb", true), WindowSystem::XWayland);
    EXPECT_EQ(WindowSystemOf("wayland", true), WindowSystem::Wayland);
    EXPECT_EQ(WindowSystemOf("wayland-egl", false), WindowSystem::Wayland);
    EXPECT_EQ(WindowSystemOf("offscreen", false), WindowSystem::Other);
}

TEST(LinuxPresent_Test, X11VrrNeedsTheCapableOutputAndAFullscreenWindow)
{
    VrrInputs in;
    in.system = WindowSystem::X11;
    in.vrrCapable = 1;
    in.coversScreen = true;
    EXPECT_EQ(DecideVrr(in).support, VrrSupport::Yes);
    EXPECT_TRUE(DecideVrr(in).reason.empty());

    in.coversScreen = false;
    VrrDecision d = DecideVrr(in);
    EXPECT_EQ(d.support, VrrSupport::No);
    EXPECT_NE(d.reason.find("fullscreen"), std::string::npos);

    in.coversScreen = true;
    in.vrrCapable = 0;
    d = DecideVrr(in);
    EXPECT_EQ(d.support, VrrSupport::No);
    EXPECT_NE(d.reason.find("vrr_capable 0"), std::string::npos);

    in.vrrCapable = -1;
    d = DecideVrr(in);
    EXPECT_EQ(d.support, VrrSupport::Unknown);
    EXPECT_NE(d.reason.find("no RandR vrr_capable"), std::string::npos);
}

TEST(LinuxPresent_Test, NvidiaIsUnknownUnlessItsEnvironmentSwitchesVrrOff)
{
    VrrInputs in;
    in.system = WindowSystem::X11;
    in.nvidia = true;
    in.coversScreen = true;
    VrrDecision d = DecideVrr(in);
    EXPECT_EQ(d.support, VrrSupport::Unknown);
    EXPECT_NE(d.reason.find("NVIDIA"), std::string::npos);

    in.gsyncAllowed = "1";
    EXPECT_EQ(DecideVrr(in).support, VrrSupport::Unknown);
    in.gsyncAllowed = "0";
    d = DecideVrr(in);
    EXPECT_EQ(d.support, VrrSupport::No);
    EXPECT_NE(d.reason.find("__GL_GSYNC_ALLOWED"), std::string::npos);

    in.gsyncAllowed = nullptr;
    in.vrrAllowed = "false";
    d = DecideVrr(in);
    EXPECT_EQ(d.support, VrrSupport::No);
    EXPECT_NE(d.reason.find("__GL_VRR_ALLOWED"), std::string::npos);

    // A driver that exposes the property is believed
    in.vrrAllowed = nullptr;
    in.vrrCapable = 1;
    EXPECT_EQ(DecideVrr(in).support, VrrSupport::Yes);
}

TEST(LinuxPresent_Test, WaylandAndXWaylandAreUnknownWithTheWayOut)
{
    for (WindowSystem system : {WindowSystem::Wayland, WindowSystem::XWayland})
    {
        VrrInputs in;
        in.system = system;
        in.vrrCapable = 1;  // XWayland's emulated RandR: not the compositor's word
        in.coversScreen = true;
        const VrrDecision d = DecideVrr(in);
        EXPECT_EQ(d.support, VrrSupport::Unknown);
        EXPECT_NE(d.reason.find("compositor decides"), std::string::npos);
        EXPECT_NE(d.reason.find("present_policy=vrr"), std::string::npos);
    }
    EXPECT_STREQ(VrrSupportName(VrrSupport::Unknown), "unknown");
    EXPECT_STREQ(VrrSupportName(VrrSupport::Yes), "yes");
    EXPECT_STREQ(VrrSupportName(VrrSupport::No), "no");
}

TEST(LinuxPresent_Test, ThePolicyMeansWhatItMeansOnMacOsAndWindows)
{
    // vrr: each frame at its time, swap interval 0, whatever the decision (forced)
    for (VrrSupport s : {VrrSupport::Yes, VrrSupport::No, VrrSupport::Unknown})
    {
        const SwapMode vrr = PickSwapMode(PresentPolicy::Vrr, s);
        EXPECT_TRUE(vrr.vrr);
        EXPECT_EQ(vrr.interval, 0);
        const SwapMode vsync = PickSwapMode(PresentPolicy::Vsync, s);
        EXPECT_FALSE(vsync.vrr);
        EXPECT_EQ(vsync.interval, 1);
    }
    // auto: VRR only where the display can and it engaged
    EXPECT_TRUE(PickSwapMode(PresentPolicy::Auto, VrrSupport::Yes).vrr);
    EXPECT_FALSE(PickSwapMode(PresentPolicy::Auto, VrrSupport::Yes, false).vrr);
    EXPECT_FALSE(PickSwapMode(PresentPolicy::Auto, VrrSupport::Unknown).vrr);
    EXPECT_FALSE(PickSwapMode(PresentPolicy::Auto, VrrSupport::No).vrr);
    EXPECT_EQ(PickSwapMode(PresentPolicy::Auto, VrrSupport::No).interval, 1);
}

TEST(LinuxPresent_Test, TheTimedSwapNeedsScreenTimesUnlessEachFrameGoesAtItsTime)
{
    EXPECT_TRUE(UseTimedSwap(PresentPolicy::Auto, true));
    EXPECT_TRUE(UseTimedSwap(PresentPolicy::Vsync, true));
    EXPECT_TRUE(UseTimedSwap(PresentPolicy::Vrr, true));
    EXPECT_TRUE(UseTimedSwap(PresentPolicy::Vrr, false));
    EXPECT_FALSE(UseTimedSwap(PresentPolicy::Auto, false));
    EXPECT_FALSE(UseTimedSwap(PresentPolicy::Vsync, false));
}

TEST(LinuxPresent_Test, TheSwapWaitsForItsTimeAndGivesWayToALaterFrameDue)
{
    int64_t wake = 0;
    // Ahead of its time: the wake is the swap's cost before it
    EXPECT_EQ(PlanSwap(10'000'000, 0, 1'000'000, 300'000, wake), SwapStep::Wait);
    EXPECT_EQ(wake, 9'700'000);
    // Within the cost, or past: at once
    EXPECT_EQ(PlanSwap(10'000'000, 0, 9'800'000, 300'000, wake), SwapStep::Swap);
    EXPECT_EQ(PlanSwap(10'000'000, 0, 12'000'000, 300'000, wake), SwapStep::Swap);
    // A forced repaint (time 0) goes at once
    EXPECT_EQ(PlanSwap(0, 0, 5'000'000, 300'000, wake), SwapStep::Swap);
    // The next frame due already: this one would never be seen
    EXPECT_EQ(PlanSwap(10'000'000, 30'000'000, 31'000'000, 300'000, wake), SwapStep::Drop);
    // The next one not due yet: this one goes
    EXPECT_EQ(PlanSwap(10'000'000, 30'000'000, 12'000'000, 300'000, wake), SwapStep::Swap);
}

TEST(LinuxPresent_Test, TheUstIsUsedOnlyOnTheSteadyClock)
{
    EXPECT_TRUE(UstOnSteadyClock(5'000'000'000, 5'010'000'000));
    EXPECT_FALSE(UstOnSteadyClock(5'000'000'000, 7'000'000'000));
    EXPECT_FALSE(UstOnSteadyClock(0, 5'000'000));
}

TEST(LinuxPresent_Test, TheVBlankClockGivesTheGridFromUstAndMsc)
{
    VBlankClock clock;
    constexpr int64_t kPeriod = 16'666'667;
    int64_t ust = 1'000'000'000;
    int64_t msc = 100;
    clock.Add(ust, msc);
    EXPECT_EQ(clock.PeriodNs(), 0);
    // Swaps one, two or three refreshes apart: the period is per refresh
    for (int i = 0; i < 20; i++)
    {
        const int step = 1 + i % 3;
        msc += step;
        ust += step * kPeriod;
        clock.Add(ust, msc);
    }
    EXPECT_EQ(clock.LastNs(), ust);
    EXPECT_NEAR(static_cast<double>(clock.PeriodNs()), static_cast<double>(kPeriod), 1.0);
    // A pause (the display asleep) is no refresh; the same vblank again changes nothing
    clock.Add(ust + 2'000'000'000, msc + 1);
    clock.Add(ust + 2'000'000'000, msc + 1);
    EXPECT_NEAR(static_cast<double>(clock.PeriodNs()), static_cast<double>(kPeriod), 1.0);
    EXPECT_EQ(clock.LastNs(), ust + 2'000'000'000);
}

TEST(LinuxPresent_Test, VrrEngagedWhenTheVBlanksFollowTheFrames)
{
    bool known = false;
    // Pentagon 48.83 Hz on a 60 Hz mode: vblanks every frame (20.48 ms) -> engaged
    EXPECT_TRUE(VrrEngaged(20.48, 20.48, 16.67, known));
    EXPECT_TRUE(known);
    // Vblanks on the mode's grid while the frames are off it -> not engaged
    EXPECT_FALSE(VrrEngaged(16.67, 20.48, 16.67, known));
    EXPECT_TRUE(known);
    // Frames at the mode's rate: cannot tell
    EXPECT_TRUE(VrrEngaged(16.67, 16.70, 16.67, known));
    EXPECT_FALSE(known);
    EXPECT_TRUE(VrrEngaged(0.0, 20.0, 16.67, known));
    EXPECT_FALSE(known);
}

TEST(LinuxPresent_Test, CoversScreenAllowsAPixelOfRounding)
{
    EXPECT_TRUE(CoversScreen(0, 0, 1280, 800, 0, 0, 1280, 800));
    EXPECT_TRUE(CoversScreen(1921, 0, 1919, 1080, 1920, 0, 1920, 1080));
    EXPECT_FALSE(CoversScreen(0, 30, 1280, 770, 0, 0, 1280, 800));
    EXPECT_FALSE(CoversScreen(0, 0, 0, 0, 0, 0, 0, 0));
}

TEST(LinuxPresent_Test, ModeRateFollowsXrandrsFormula)
{
    XMode m;
    m.dotClock = 148'500'000;
    m.hTotal = 2200;
    m.vTotal = 1125;
    EXPECT_NEAR(ModeRateHz(m), 60.0, 1e-9);
    m.flags = kModeInterlace;
    EXPECT_NEAR(ModeRateHz(m), 120.0, 1e-9);
    m.flags = kModeDoubleScan;
    EXPECT_NEAR(ModeRateHz(m), 30.0, 1e-9);
    m.hTotal = 0;
    EXPECT_EQ(ModeRateHz(m), 0.0);
}

TEST(LinuxPresent_Test, TheMachinesRateFindsTheModeOfTheSameResolution)
{
    // A Steam Deck LCD-like list: 800x1280 at 40..60 Hz, another resolution at 50 Hz, an interlaced one
    const XMode current = Mode(1, 1280, 800, 60.0);
    const std::vector<XMode> modes = {current,
                                      Mode(2, 1280, 800, 50.0),
                                      Mode(3, 1280, 800, 40.0),
                                      Mode(4, 1280, 800, 50.0),  // a second 50 Hz timing: one rate
                                      Mode(5, 1024, 768, 50.0),
                                      Mode(6, 1280, 800, 100.0, kModeInterlace)};
    const std::vector<double> rates = RatesAtResolution(modes, current);
    ASSERT_EQ(rates.size(), 3u);
    EXPECT_NEAR(rates[0], 60.0, 0.01);
    EXPECT_NEAR(rates[1], 50.0, 0.01);
    EXPECT_NEAR(rates[2], 40.0, 0.01);

    // Pentagon 48.83 Hz on the 60 Hz mode: 50 Hz is the pick, the first such mode of that resolution
    DisplayRateFit pick;
    ASSERT_TRUE(DisplayRatePick(48.83, rates[0], rates, pick));
    EXPECT_NEAR(pick.hz, 50.0, 0.01);
    const XMode* mode = ModeForRate(modes, current, pick.hz);
    ASSERT_NE(mode, nullptr);
    EXPECT_EQ(mode->id, 2u);
    EXPECT_EQ(ModeForRate(modes, current, 75.0), nullptr);
}

TEST(LinuxPresent_Test, TheModeWithTheCurrentFlagsIsPreferred)
{
    const XMode current = Mode(1, 1920, 1080, 60.0, 0x2 | 0x8);  // -hsync -vsync
    const std::vector<XMode> modes = {current, Mode(2, 1920, 1080, 50.0, 0x1 | 0x4), Mode(3, 1920, 1080, 50.0, 0x2 | 0x8)};
    const XMode* mode = ModeForRate(modes, current, 50.0);
    ASSERT_NE(mode, nullptr);
    EXPECT_EQ(mode->id, 3u);
}

TEST(LinuxPresent_Test, OnlyX11SwitchesTheMode)
{
    EXPECT_TRUE(NoModeSwitchReason(WindowSystem::X11).empty());
    EXPECT_NE(NoModeSwitchReason(WindowSystem::Wayland).find("compositor"), std::string::npos);
    EXPECT_NE(NoModeSwitchReason(WindowSystem::XWayland).find("XWayland"), std::string::npos);
    EXPECT_FALSE(NoModeSwitchReason(WindowSystem::Other).empty());
}

TEST(LinuxPresent_Test, AFixedDisplayReportsTheMeasuredVblankRate)
{
    // gamescope switched the panel to 49 Hz; XWayland's RandR (Qt) still says 59.81
    EXPECT_NEAR(FixedRefreshHz(59.81, 20'408'163, false), 49.0, 0.001);
    EXPECT_DOUBLE_EQ(FixedRefreshHz(59.81, 0, false), 59.81);         // no counter yet
    EXPECT_DOUBLE_EQ(FixedRefreshHz(144.0, 20'408'163, true), 144.0);  // VRR: the vblanks follow the frames
}

TEST(LinuxPresent_Test, GamescopeRatesAndTheRefreshRequest)
{
    // The Deck LCD: 40..60 whole Hz, unordered with a repeat and a zero
    const uint32_t valid[] = {60, 40, 49, 50, 0, 49};
    const std::vector<double> rates = GamescopeRates(valid, sizeof valid / sizeof valid[0]);
    ASSERT_EQ(rates.size(), 4u);
    EXPECT_DOUBLE_EQ(rates.front(), 40.0);
    EXPECT_DOUBLE_EQ(rates.back(), 60.0);
    EXPECT_TRUE(GamescopeRates(nullptr, 0).empty());

    // Pentagon 48.83 Hz from 60: 49 Hz (0.35 % off), asked as 49 fps
    DisplayRateFit pick;
    ASSERT_TRUE(DisplayRatePick(48.83, 60.0, rates, pick));
    EXPECT_DOUBLE_EQ(pick.hz, 49.0);
    EXPECT_EQ(GamescopeFps(pick.hz), 49u);
    EXPECT_EQ(GamescopeFps(0.0), 0u);

    // The built-in panel, the mode switched, no frame limiter at that fps
    EXPECT_EQ(GamescopeRefreshFlags(true), 0x7u);
    EXPECT_EQ(GamescopeRefreshFlags(false), 0x6u);
}
