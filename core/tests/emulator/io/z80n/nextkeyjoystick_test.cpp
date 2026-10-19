/// @file nextkeyjoystick_test.cpp
/// @brief The Next's key joystick (membrane_stick.vhd, keyjoy_64_6.coe): the NR #05 modes press membrane keys, NR #28 /
/// #29 / #2B write the user-defined keys

#include <gtest/gtest.h>

#include "emulator/io/z80n/nextkeyjoystick.h"

namespace
{
constexpr uint16_t kRight = 0x01, kLeft = 0x02, kUp = 0x08, kFire = 0x10, kPadC = 0x20, kPadMode = 0x800;
constexpr unsigned kSinclair2 = 0, kKempston1 = 1, kCursor = 2, kSinclair1 = 3, kMd1 = 5, kUser = 7;
constexpr uint8_t kRow3 = 0xF7, kRow4 = 0xEF, kRow5 = 0xDF;  // #F7FE 1-5, #EFFE 0-6, #DFFE P-Y

uint8_t Pressed(unsigned column)
{
    return static_cast<uint8_t>(0x7F & ~(1u << column));
}
}  // namespace

TEST(NextKeyJoystick_Test, ModesPressTheirKeys)
{
    const NextKeyJoystick joy;
    // Sinclair 1 on row 4 (0 9 8 7 6): right = 7, up = 9, fire = 0
    EXPECT_EQ(joy.Columns(kRow4, kRight, kSinclair1, 0, kSinclair1), Pressed(3));
    EXPECT_EQ(joy.Columns(kRow4, kUp, kSinclair1, 0, kSinclair1), Pressed(1));
    EXPECT_EQ(joy.Columns(kRow4, kFire, kSinclair1, 0, kSinclair1), Pressed(0));
    EXPECT_EQ(joy.Columns(kRow3, kRight, kSinclair1, 0, kSinclair1), 0x7F) << "a row the read does not select";
    // Sinclair 2 on the right connector, row 3 (1 2 3 4 5): left = 1, fire = 5
    EXPECT_EQ(joy.Columns(kRow3, 0, kKempston1, kLeft, kSinclair2), Pressed(0));
    EXPECT_EQ(joy.Columns(kRow3, 0, kKempston1, kFire, kSinclair2), Pressed(4));
    // Cursor: left = 5 (row 3), right = 8 (row 4)
    EXPECT_EQ(joy.Columns(kRow3, kLeft, kCursor, 0, kCursor), Pressed(4));
    EXPECT_EQ(joy.Columns(kRow4, kRight, kCursor, 0, kCursor), Pressed(2));
    // Kempston and MD: the port carries the stick; the spare buttons have no key until the program maps one
    EXPECT_EQ(joy.Columns(0x00, kRight | kPadC, kKempston1, kPadMode, kMd1), 0x7F);
}

TEST(NextKeyJoystick_Test, UserDefinedKeysAreWrittenThroughTheRegisters)
{
    NextKeyJoystick joy;
    joy.WriteAddressHigh(0x80);  // the joystick map
    joy.WriteAddressLow(0x00);   // left, bit 0 (right)
    joy.WriteData(0x28);         // row 5, column 0: P
    joy.WriteData(0x29);         // bit 1 (left), the address stepped: O
    EXPECT_EQ(joy.Columns(kRow5, kRight, kUser, 0, kUser), Pressed(0));
    EXPECT_EQ(joy.Columns(kRow5, kLeft, kUser, 0, kUser), Pressed(1));
    EXPECT_EQ(joy.Columns(kRow5, 0, kUser, kRight, kUser), 0x7F) << "the right side keeps its own map";

    joy.WriteAddressLow(0x15);   // right, bit 5 (C)
    joy.WriteData(0x28);
    EXPECT_EQ(joy.Columns(kRow5, 0, kKempston1, kPadC, kKempston1), Pressed(0)) << "Kempston mode: C presses its key";
    EXPECT_EQ(joy.Entry(32 + 16 + 5), 0x28);

    joy.WriteAddressHigh(0x00);  // the PS/2 keymap: not the joystick map
    joy.WriteAddressLow(0x00);
    joy.WriteData(0x3F);
    EXPECT_EQ(joy.Entry(16), 0x28);
    EXPECT_EQ(joy.Entry(0), 0x23) << "the Sinclair entries are not writable";
}
