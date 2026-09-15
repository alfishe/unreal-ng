/// @file cli-mouse-format-test.cpp
/// @brief Unit tests for the Kempston Mouse CLI argument parsing and output formatting
/// (automation-interfaces §4.5, §5.6). Pure strings: no CLI socket, no emulator.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../../automation/cli/src/commands/cli-mouse-format.h"

namespace
{
MouseStateSnapshot MakeState()
{
    MouseStateSnapshot state;
    state.available = true;
    state.present = true;
    state.wheelEnabled = true;
    state.x = 41;
    state.y = 80;
    state.buttonMask = 0xFE;  // left down
    state.wheel = 2;
    state.portButtons = 0x2E;
    state.portX = 0x29;
    state.portY = 0x50;
    return state;
}
}  // namespace

TEST(CliMouseFormat_test, ParseStrictInt_AcceptsWholeIntegers)
{
    long long value = 0;
    EXPECT_TRUE(CliMouse::ParseStrictInt("10", value));
    EXPECT_EQ(value, 10);
    EXPECT_TRUE(CliMouse::ParseStrictInt("-127", value));
    EXPECT_EQ(value, -127);
    EXPECT_TRUE(CliMouse::ParseStrictInt("+5", value));
    EXPECT_EQ(value, 5);
}

TEST(CliMouseFormat_test, ParseStrictInt_RejectsJunk)
{
    long long value = 123;
    EXPECT_FALSE(CliMouse::ParseStrictInt("", value));
    EXPECT_FALSE(CliMouse::ParseStrictInt("10px", value));
    EXPECT_FALSE(CliMouse::ParseStrictInt("px", value));
    EXPECT_FALSE(CliMouse::ParseStrictInt("1.5", value));
    EXPECT_FALSE(CliMouse::ParseStrictInt(" 10", value));
    EXPECT_FALSE(CliMouse::ParseStrictInt("10 ", value));
    EXPECT_FALSE(CliMouse::ParseStrictInt("99999999999999999999999", value));
    EXPECT_EQ(value, 123);  // untouched on failure
}

TEST(CliMouseFormat_test, ParseIntArg_ReportsNameAndToken)
{
    int value = 0;
    std::string error;
    EXPECT_TRUE(CliMouse::ParseIntArg("300", "dx", value, error));  // range is the manager's job
    EXPECT_EQ(value, 300);

    EXPECT_FALSE(CliMouse::ParseIntArg("10px", "dx", value, error));
    EXPECT_EQ(error, "Invalid dx '10px': expected an integer");

    EXPECT_FALSE(CliMouse::ParseIntArg("4294967296", "steps", value, error));  // does not fit int
}

TEST(CliMouseFormat_test, ParseFramesArg_NoUnsignedWrap)
{
    uint32_t frames = 0;
    std::string error;
    EXPECT_TRUE(CliMouse::ParseFramesArg("70000", frames, error));
    EXPECT_EQ(frames, 70000u);  // reaches the manager as 70000, not wrapped by uint16

    EXPECT_FALSE(CliMouse::ParseFramesArg("-1", frames, error));
    EXPECT_EQ(error, "frames=-1 out of range 1..65535");

    EXPECT_FALSE(CliMouse::ParseFramesArg("3x", frames, error));
    EXPECT_EQ(error, "Invalid frame count '3x': expected an integer");
}

TEST(CliMouseFormat_test, ParseButtonList_NamesAliasesAndNone)
{
    uint8_t bits = 0xFF;
    std::string error;

    EXPECT_TRUE(CliMouse::ParseButtonList({"none"}, bits, error));
    EXPECT_EQ(bits, 0x00);

    EXPECT_TRUE(CliMouse::ParseButtonList({"left,middle"}, bits, error));
    EXPECT_EQ(bits, 0x05);

    EXPECT_TRUE(CliMouse::ParseButtonList({"R", "l,l"}, bits, error));  // tokens + duplicates
    EXPECT_EQ(bits, 0x03);

    EXPECT_FALSE(CliMouse::ParseButtonList({"left,foo"}, bits, error));
    EXPECT_NE(error.find("'foo'"), std::string::npos);

    EXPECT_FALSE(CliMouse::ParseButtonList({}, bits, error));
}

TEST(CliMouseFormat_test, FormatLines_MatchDocumentedOutput)
{
    MouseStateSnapshot state = MakeState();

    EXPECT_EQ(CliMouse::FormatMoveLine(10, -5, state), "Moved: dx=+10 dy=-5 -> X=41 Y=80");

    state.portButtons = 0x0E;
    EXPECT_EQ(CliMouse::FormatButtonLine("Pressed", "left", state), "Pressed: left -> buttons=L-- (#FADF=0x0E)");

    state.buttonMask = 0xFA;  // left + middle
    EXPECT_EQ(CliMouse::FormatButtonsShort(state), "L-M");
    state.buttonMask = 0xFF;
    EXPECT_EQ(CliMouse::FormatButtonsShort(state), "---");

    EXPECT_EQ(CliMouse::FormatPressedNames(0x06), "right,middle");
    EXPECT_EQ(CliMouse::FormatPressedNames(0x00), "none");
    EXPECT_EQ(CliMouse::Hex2(0x0E), "0x0E");
}

TEST(CliMouseFormat_test, FormatStatus_AllFields)
{
    MouseStateSnapshot state = MakeState();
    state.pendingClickButton = MouseButton::Left;
    state.pendingClickFramesLeft = 3;

    const std::string expected = "Kempston Mouse [present]\n"
                                 "  X=41 (0x29)  Y=80 (0x50)\n"
                                 "  Buttons: left=down right=up middle=up  (mask 0xFE)\n"
                                 "  Wheel: 2\n"
                                 "  Ports: #FADF=0x2E #FBDF=0x29 #FFDF=0x50\n"
                                 "  Pending click: left, 3 frame(s) left\n"
                                 "  TTD journal: supported\n";
    EXPECT_EQ(CliMouse::FormatStatus(state), expected);
}

TEST(CliMouseFormat_test, FormatStatus_AbsentAndUnavailable)
{
    MouseStateSnapshot state = MakeState();
    state.present = false;
    state.wheelEnabled = false;
    const std::string text = CliMouse::FormatStatus(state, "\r\n");
    EXPECT_EQ(text.rfind("Kempston Mouse [absent]\r\n", 0), 0u);
    EXPECT_NE(text.find("  Wheel: 2 (no wheel fitted)\r\n"), std::string::npos);
    EXPECT_NE(text.find("  Pending click: none\r\n"), std::string::npos);

    EXPECT_EQ(CliMouse::FormatStatus(MouseStateSnapshot{}), "Kempston Mouse [not available]\n");
}
