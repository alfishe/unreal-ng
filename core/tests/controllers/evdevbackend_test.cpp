/// @file evdevbackend_test.cpp
/// @brief The Linux evdev backend's key table (Linux only: the codes come from linux/input-event-codes.h): xpad's
/// Elite 2 paddles under both code sets, BTN_GRIP* (current kernels) and BTN_TRIGGER_HAPPY5-8 (6.1+), in the bit order
/// of drivers/input/joystick/xpad.c's xpad_btn_paddles[] (upper right, lower right, upper left, lower left), the same
/// names the Bluetooth report's paddle nibble gets through HidPad on macOS.

#ifdef __linux__

#include <gtest/gtest.h>

#include <linux/input-event-codes.h>

#include <string>

#include "platform/linux/evdevbackend.h"

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

#endif  // __linux__
