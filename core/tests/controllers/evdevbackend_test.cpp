/// @file evdevbackend_test.cpp
/// @brief The Linux evdev backend's key table (Linux only: the codes come from linux/input-event-codes.h): xpad's
/// Elite 2 paddles under both code sets, BTN_GRIP* (current kernels) and BTN_TRIGGER_HAPPY5-8 (6.1+), in the bit order
/// of drivers/input/joystick/xpad.c's xpad_btn_paddles[] (upper right, lower right, upper left, lower left), the same
/// names the Bluetooth report's paddle nibble gets through HidPad on macOS.

#ifdef __linux__

#include <gtest/gtest.h>

#include <linux/input.h>

#include <string>

#include "platform/linux/evdevbackend.h"
#include "platform/linuxpads.h"

using namespace controllers;

namespace
{
std::string Name(unsigned code)
{
    const char* name = EvdevPadButton(code);
    return name != nullptr ? name : "";
}
}  // namespace

TEST(EvdevBackend_Test, PadButton_XpadPaddlesUnderBothCodeSets)
{
    // BTN_GRIPL / R / L2 / R2: their UAPI values, which the backend also uses with headers that predate them
    EXPECT_EQ(Name(0x225), "paddle.p1") << "BTN_GRIPR, upper right";
    EXPECT_EQ(Name(0x227), "paddle.p2") << "BTN_GRIPR2, lower right";
    EXPECT_EQ(Name(0x224), "paddle.p3") << "BTN_GRIPL, upper left";
    EXPECT_EQ(Name(0x226), "paddle.p4") << "BTN_GRIPL2, lower left";
    EXPECT_EQ(Name(BTN_TRIGGER_HAPPY5), "paddle.p1");
    EXPECT_EQ(Name(BTN_TRIGGER_HAPPY6), "paddle.p2");
    EXPECT_EQ(Name(BTN_TRIGGER_HAPPY7), "paddle.p3");
    EXPECT_EQ(Name(BTN_TRIGGER_HAPPY8), "paddle.p4");
    EXPECT_EQ(Name(BTN_TRIGGER_HAPPY1), "") << "not a paddle";
    EXPECT_EQ(Name(BTN_SOUTH), "south");
    EXPECT_EQ(Name(BTN_MODE), "guide");
}

/// linuxpads.cpp spells the UAPI codes out (it builds on every OS): they are the kernel header's
TEST(EvdevBackend_Test, LinuxPads_CodesAreTheKernelHeaders)
{
    auto name = [](LinuxPadKind kind, unsigned code) {
        const char* control = LinuxPadButton(kind, code);
        return std::string(control != nullptr ? control : "");
    };
    EXPECT_EQ(name(LinuxPadKind::SwitchPro, BTN_Z), "share");
    EXPECT_EQ(name(LinuxPadKind::JoyConLeft, BTN_DPAD_LEFT), "south");
    EXPECT_EQ(name(LinuxPadKind::JoyConRight, BTN_EAST), "south");
    EXPECT_EQ(name(LinuxPadKind::PairRight, BTN_TR2), "rt");
    EXPECT_EQ(name(LinuxPadKind::DualSenseEdge, BTN_TRIGGER_HAPPY3), "paddle.p3");
    EXPECT_EQ(name(LinuxPadKind::DualSenseEdge, BTN_TRIGGER_HAPPY4), "paddle.p1");
    EXPECT_EQ(name(LinuxPadKind::NsoN64, BTN_FORWARD), "misc.1");
    EXPECT_EQ(name(LinuxPadKind::NsoN64, BTN_BACK), "misc.2");
    EXPECT_EQ(name(LinuxPadKind::NsoN64, BTN_LEFT), "misc.3");
    EXPECT_EQ(name(LinuxPadKind::NsoN64, BTN_RIGHT), "misc.4");
    EXPECT_EQ(name(LinuxPadKind::NsoN64, BTN_B), "west");
    EXPECT_EQ(name(LinuxPadKind::NsoNes, BTN_SOUTH), "east");
    EXPECT_EQ(name(LinuxPadKind::NsoGenesis, BTN_WEST), "rb");
    EXPECT_EQ(LinuxGenericButtonIndex(BTN_JOYSTICK), 0);
    EXPECT_EQ(LinuxGenericButtonIndex(BTN_GAMEPAD + 15), 15);
    EXPECT_EQ(LinuxGenericButtonIndex(BTN_TRIGGER_HAPPY1), 16);
    EXPECT_EQ(LinuxConsumerUsage(KEY_HOMEPAGE), 0x223);
    EXPECT_EQ(LinuxConsumerUsage(KEY_BACK), 0x224);
    EXPECT_EQ(LinuxConsumerUsage(KEY_FORWARD), 0x225);
    EXPECT_EQ(LinuxConsumerUsage(KEY_RECORD), 0x0B2);
    EXPECT_EQ(LinuxConsumerUsage(KEY_SEARCH), 0x221);
    const HidLayout xbox = HidLayoutFor(std::string("xbox"));
    EXPECT_STREQ(LinuxGenericAxis(xbox, ABS_BRAKE).control, "lt");
    EXPECT_STREQ(LinuxGenericAxis(xbox, ABS_GAS).control, "rt");
    EXPECT_EQ(name(LinuxPadKind::JoycondCombined, BTN_TRIGGER_HAPPY4), "paddle.p1");
    EXPECT_EQ(name(LinuxPadKind::Standard, BTN_THUMBR), "rstick.click");
    EXPECT_EQ(LinuxPadButton(LinuxPadKind::Standard, BTN_TL2, true), nullptr);
    EXPECT_STREQ(LinuxPadAxisOf(LinuxPadKind::Standard, ABS_RZ).control, "rt");
    EXPECT_STREQ(LinuxPadAxisOf(LinuxPadKind::JoyConRight, ABS_RY).control, "lstick.x");
    EXPECT_EQ(LinuxDeviceId(BUS_BLUETOOTH, 0x057E, 0x2009, 0, "", "p", "n").rfind("bt:", 0), 0u);
    EXPECT_EQ(LinuxDeviceId(BUS_VIRTUAL, 0x057E, 0x2008, 0, "", "", "n").rfind("virtual:", 0), 0u);
}

#endif  // __linux__
