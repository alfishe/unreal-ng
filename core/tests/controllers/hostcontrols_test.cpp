/// @file hostcontrols_test.cpp
/// @brief The controllers library's control vocabulary and retro control names (architecture.md §6), and the
/// analog rules of the router (mapping-and-profiles.md §3). Pure functions: no machine.

#include "hostcontrols.h"

#include <gtest/gtest.h>

#include "emulator/io/controllers/controllertypes.h"
#include "router/analog.h"
#include "router/binding.h"

using namespace controllers;

TEST(HostControls_Test, ParseControl_FixedRawDerived)
{
    ControlInfo info;
    ASSERT_TRUE(ParseControl("South", info));
    EXPECT_EQ(info.name, "south");
    EXPECT_EQ(info.type, ControlType::Button);
    ASSERT_TRUE(ParseControl("rt", info));
    EXPECT_EQ(info.type, ControlType::Trigger);
    ASSERT_TRUE(ParseControl("lstick.y", info));
    EXPECT_EQ(info.type, ControlType::Axis);

    ASSERT_TRUE(ParseControl("lstick.left", info));
    EXPECT_EQ(info.base, "lstick");
    EXPECT_EQ(info.direction, kLeft);
    ASSERT_TRUE(ParseControl("rt.click", info));
    EXPECT_EQ(info.base, "rt");

    EXPECT_TRUE(ParseControl("button.12", info));
    EXPECT_TRUE(ParseControl("axis.3", info));
    EXPECT_EQ(info.type, ControlType::Axis);
    ASSERT_TRUE(ParseControl("hat.0.up", info));
    EXPECT_EQ(info.direction, kUp);
    EXPECT_TRUE(ParseControl("key.kp8", info));

    EXPECT_FALSE(ParseControl("button.x", info));
    EXPECT_FALSE(ParseControl("hat.0.north", info));
    EXPECT_FALSE(ParseControl("key.", info));
    EXPECT_FALSE(ParseControl("southpaw", info));
    EXPECT_FALSE(ParseControl("lstick.sideways", info));
}

/// The window keyboard's shortcuts: the modifiers in one order, '.' between them (debug-and-ttd.md §4.3)
TEST(HostControls_Test, KeyControl_ModifiersInAFixedOrder)
{
    EXPECT_EQ(KeyControl("f10", 0), "key.f10");
    EXPECT_EQ(KeyControl("F10", kKeyCtrl), "key.ctrl.f10");
    EXPECT_EQ(KeyControl("f11", kKeyShift | kKeyCtrl | kKeyMeta | kKeyAlt), "key.ctrl.alt.shift.meta.f11");
    ControlInfo info;
    ASSERT_TRUE(ParseControl(KeyControl("left", kKeyCtrl), info));
    EXPECT_EQ(info.type, ControlType::Button);
    EXPECT_EQ(info.name, "key.ctrl.left") << "a key, not a stick direction";
}

TEST(HostControls_Test, RetroControls_KempstonAndMdPadShareTheBits)
{
    uint16_t mask = 0;
    ASSERT_TRUE(RetroControlMask("fire", mask));
    EXPECT_EQ(mask, kFire);
    ASSERT_TRUE(RetroControlMask("B", mask));
    EXPECT_EQ(mask, kFire) << "MD B is fire 1";
    ASSERT_TRUE(RetroControlMask("start", mask));
    EXPECT_EQ(mask, kFire4);
    ASSERT_TRUE(RetroControlMask("mode", mask));
    EXPECT_EQ(mask, 0x0800) << "PadState bit 11";
    ASSERT_TRUE(RetroControlMask("dpad", mask));
    EXPECT_EQ(mask, kRight | kLeft | kDown | kUp);
    EXPECT_FALSE(RetroControlMask("hyperspace", mask));
}

TEST(HostControls_Test, ParseRetroTarget_ListsTheKnownControls)
{
    RetroTarget target;
    std::string error;
    ASSERT_TRUE(ParseRetroTarget("Next.Left:c", target, error)) << error;
    EXPECT_EQ(target.connector, "next.left");
    EXPECT_EQ(target.mask, kFire2);
    ASSERT_TRUE(ParseRetroTarget("auto:dpad", target, error)) << error;
    EXPECT_EQ(target.connector, "auto");

    EXPECT_FALSE(ParseRetroTarget("kempston", target, error));
    EXPECT_FALSE(ParseRetroTarget("kempston:jump", target, error));
    EXPECT_NE(error.find("fire2"), std::string::npos) << error;
}

/// mapping-and-profiles.md §3 and testing.md Analog_Hysteresis: the thresholds are of the rescaled deflection,
/// (r - 0.15) / 0.8 with the default dead zone and saturation
TEST(HostControls_Test, Analog_Hysteresis)
{
    const AnalogOptions options;
    auto at = [](float rescaled) { return 0.15f + rescaled * 0.8f; };
    EXPECT_EQ(StickDirections(at(0.44f), 0, 0, options), 0) << "below 0.45: not yet";
    const uint16_t on = StickDirections(at(0.46f), 0, 0, options);
    EXPECT_EQ(on, kRight) << "crossing 0.45 presses";
    EXPECT_EQ(StickDirections(at(0.40f), 0, on, options), kRight) << "falling to 0.40 holds";
    EXPECT_EQ(StickDirections(at(0.34f), 0, on, options), 0) << "below 0.35 releases";
    EXPECT_EQ(RadialDeflection(0.1f, 0.1f, options), 0.0f) << "inside the radial dead zone";
    EXPECT_EQ(RadialDeflection(0.97f, 0, options), 1.0f) << "past the saturation";
}

TEST(HostControls_Test, Analog_EightAndFourWays)
{
    AnalogOptions options;
    EXPECT_EQ(StickDirections(0, -1, 0, options), kUp) << "y -1 is up";
    EXPECT_EQ(StickDirections(0.7f, -0.7f, 0, options), kUp | kRight) << "8-way diagonal";
    EXPECT_EQ(StickDirections(-0.7f, 0.7f, 0, options), kDown | kLeft);

    options.ways = 4;
    // 40 degrees above the right axis: still right while it was right, up when it came from nowhere nearer
    const float x = 0.766f, y = -0.643f;
    EXPECT_EQ(StickDirections(x, y, kRight, options), kRight) << "the previous axis holds on a diagonal";
    EXPECT_EQ(StickDirections(0.5f, -0.866f, kRight, options), kUp) << "60 degrees: past the 55-degree hold";
    EXPECT_EQ(StickDirections(x, y, 0, options), kRight) << "no previous: the nearest axis";
}

TEST(HostControls_Test, TriggerClick_Hysteresis)
{
    const AnalogOptions options;
    EXPECT_FALSE(TriggerClick(0.49f, false, options));
    EXPECT_TRUE(TriggerClick(0.5f, false, options));
    EXPECT_TRUE(TriggerClick(0.42f, true, options));
    EXPECT_FALSE(TriggerClick(0.39f, true, options));
}
