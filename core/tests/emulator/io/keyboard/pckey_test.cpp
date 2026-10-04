#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "emulator/io/keyboard/pckey.h"

using Bytes = std::vector<uint8_t>;

/// PS2-1: set-2 make / break bytes (checked against the ZX-Evo AVR kbmap.c rows)
TEST(PcKey_Test, Ps2Set2Bytes)
{
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::A, true), (Bytes{0x1C}));
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::A, false), (Bytes{0xF0, 0x1C}));
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::Up, true), (Bytes{0xE0, 0x75}));
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::Up, false), (Bytes{0xE0, 0xF0, 0x75}));
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::LeftShift, true), (Bytes{0x12})) << "kbmap: Caps Shift";
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::LeftCtrl, true), (Bytes{0x14})) << "kbmap: Symbol Shift";
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::RightCtrl, true), (Bytes{0xE0, 0x14}));
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::Function1, true), (Bytes{0x05}));
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::Function7, true), (Bytes{0x83}));
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::Function12, true), (Bytes{0x07}));
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::Enter, true), (Bytes{0x5A}));
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::KeypadEnter, true), (Bytes{0xE0, 0x5A}));
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::Backspace, true), (Bytes{0x66}));
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::Escape, true), (Bytes{0x76}));
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::PrintScreen, true), (Bytes{0xE0, 0x12, 0xE0, 0x7C}));
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::PrintScreen, false), (Bytes{0xE0, 0xF0, 0x7C, 0xE0, 0xF0, 0x12}));
    EXPECT_EQ(pckey::Ps2Set2Bytes(PcKey::Pause, true), (Bytes{0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77}));
    EXPECT_TRUE(pckey::Ps2Set2Bytes(PcKey::Pause, false).empty()) << "Pause has no break";
    EXPECT_TRUE(pckey::Ps2Set2Bytes(PcKey::None, true).empty());
}

/// Set 1 (PC/XT) make / break bytes, as the PROFI-XT controller receives them (IBM XT technical reference)
TEST(PcKey_Test, XtSet1Bytes)
{
    EXPECT_EQ(pckey::XtSet1Bytes(PcKey::A, true), (Bytes{0x1E}));
    EXPECT_EQ(pckey::XtSet1Bytes(PcKey::A, false), (Bytes{0x9E})) << "break = make | 80h";
    EXPECT_EQ(pckey::XtSet1Bytes(PcKey::Escape, true), (Bytes{0x01}));
    EXPECT_EQ(pckey::XtSet1Bytes(PcKey::Function1, true), (Bytes{0x3B}));
    EXPECT_EQ(pckey::XtSet1Bytes(PcKey::Function11, true), (Bytes{0x57}));
    EXPECT_EQ(pckey::XtSet1Bytes(PcKey::LeftShift, true), (Bytes{0x2A}));
    EXPECT_EQ(pckey::XtSet1Bytes(PcKey::Up, true), (Bytes{0xE0, 0x48}));
    EXPECT_EQ(pckey::XtSet1Bytes(PcKey::Up, false), (Bytes{0xE0, 0xC8}));
    EXPECT_EQ(pckey::XtSet1Bytes(PcKey::Delete, true), (Bytes{0xE0, 0x53}));
    EXPECT_EQ(pckey::XtSet1Bytes(PcKey::KeypadDecimal, true), (Bytes{0x53}));
    EXPECT_EQ(pckey::XtSet1Bytes(PcKey::RightAlt, true), (Bytes{0xE0, 0x38}));
    EXPECT_EQ(pckey::XtSet1Bytes(PcKey::NumLock, true), (Bytes{0x45})) << "not the E0 45 Windows reports";
    EXPECT_EQ(pckey::XtSet1Bytes(PcKey::PrintScreen, true), (Bytes{0xE0, 0x2A, 0xE0, 0x37}));
    EXPECT_EQ(pckey::XtSet1Bytes(PcKey::PrintScreen, false), (Bytes{0xE0, 0xB7, 0xE0, 0xAA}));
    EXPECT_EQ(pckey::XtSet1Bytes(PcKey::Pause, true), (Bytes{0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5}));
    EXPECT_TRUE(pckey::XtSet1Bytes(PcKey::Pause, false).empty()) << "Pause has no break";
    EXPECT_TRUE(pckey::XtSet1Bytes(PcKey::None, true).empty());
    // Every key has a set-1 code
    for (int i = 1; i < static_cast<int>(PcKey::Count); i++)
        EXPECT_FALSE(pckey::XtSet1Bytes(static_cast<PcKey>(i), true).empty()) << pckey::Name(static_cast<PcKey>(i));
}

/// Every key has a code and a unique name that maps back to it
TEST(PcKey_Test, EveryKeyHasANameAndACode)
{
    std::set<std::string> names;
    std::set<std::pair<bool, uint8_t>> codes;
    for (int i = 1; i < static_cast<int>(PcKey::Count); i++)
    {
        const auto key = static_cast<PcKey>(i);
        const std::string name = pckey::Name(key);
        ASSERT_FALSE(name.empty()) << i;
        EXPECT_TRUE(names.insert(name).second) << "duplicate name " << name;
        EXPECT_EQ(pckey::FromName(name), key) << name;
        EXPECT_FALSE(pckey::Ps2Set2Bytes(key, true).empty()) << name;
        if (key != PcKey::PrintScreen && key != PcKey::Pause)
        {
            const Bytes make = pckey::Ps2Set2Bytes(key, true);
            EXPECT_TRUE(codes.insert({make.size() == 2, make.back()}).second) << "duplicate code for " << name;
        }
    }
    EXPECT_EQ(pckey::FromName("PC.Home"), PcKey::Home);
    EXPECT_EQ(pckey::FromName("pc:F1"), PcKey::Function1);
    EXPECT_EQ(pckey::FromName("nope"), PcKey::None);
}

/// Host code tables: the same physical key from each platform
TEST(PcKey_Test, PlatformCodesAreLayoutIndependentPositions)
{
    // macOS kVK_*
    EXPECT_EQ(pckey::FromMacVirtualKey(0x00), PcKey::A);
    EXPECT_EQ(pckey::FromMacVirtualKey(0x7E), PcKey::Up);
    EXPECT_EQ(pckey::FromMacVirtualKey(0x7A), PcKey::Function1);
    EXPECT_EQ(pckey::FromMacVirtualKey(0x38), PcKey::LeftShift);
    EXPECT_EQ(pckey::FromMacVirtualKey(0x3B), PcKey::LeftCtrl);
    EXPECT_EQ(pckey::FromMacVirtualKey(0x33), PcKey::Backspace);
    EXPECT_EQ(pckey::FromMacVirtualKey(0x75), PcKey::Delete);
    EXPECT_EQ(pckey::FromMacVirtualKey(0x3F), PcKey::None) << "Fn is not a PC key";

    // Windows set 1, bit 8 = E0
    EXPECT_EQ(pckey::FromWindowsScanCode(0x1E), PcKey::A);
    EXPECT_EQ(pckey::FromWindowsScanCode(0x148), PcKey::Up);
    EXPECT_EQ(pckey::FromWindowsScanCode(0x48), PcKey::Keypad8) << "same code without E0 is the keypad";
    EXPECT_EQ(pckey::FromWindowsScanCode(0x11D), PcKey::RightCtrl);
    EXPECT_EQ(pckey::FromWindowsScanCode(0x145), PcKey::NumLock);
    EXPECT_EQ(pckey::FromWindowsScanCode(0x45), PcKey::Pause);

    // Linux evdev
    EXPECT_EQ(pckey::FromLinuxEvdev(30), PcKey::A);
    EXPECT_EQ(pckey::FromLinuxEvdev(103), PcKey::Up);
    EXPECT_EQ(pckey::FromLinuxEvdev(97), PcKey::RightCtrl);
    EXPECT_EQ(pckey::FromLinuxEvdev(59), PcKey::Function1);
}

/// Automation: a ZX key before decomposition -> the PC keys it stands for
TEST(PcKey_Test, FromZxKey)
{
    using Keys = std::vector<PcKey>;
    EXPECT_EQ(pckey::FromZxKey(ZXKEY_H), (Keys{PcKey::H})) << "by name: ZXKEY_H is 0x49";
    EXPECT_EQ(pckey::FromZxKey(ZXKEY_I), (Keys{PcKey::I}));
    EXPECT_EQ(pckey::FromZxKey(ZXKEY_0), (Keys{PcKey::Digit0}));
    EXPECT_EQ(pckey::FromZxKey(ZXKEY_CAPS_SHIFT), (Keys{PcKey::LeftShift}));
    EXPECT_EQ(pckey::FromZxKey(ZXKEY_SYM_SHIFT), (Keys{PcKey::LeftCtrl}));
    EXPECT_EQ(pckey::FromZxKey(ZXKEY_EXT_UP), (Keys{PcKey::Up})) << "not Shift + 7";
    EXPECT_EQ(pckey::FromZxKey(ZXKEY_EXT_DELETE), (Keys{PcKey::Backspace}));
    EXPECT_EQ(pckey::FromZxKey(ZXKEY_EXT_PLUS), (Keys{PcKey::LeftShift, PcKey::Equal}));
    EXPECT_TRUE(pckey::FromZxKey(ZXKEY_NONE).empty());
}

/// Automation typing, US layout
TEST(PcKey_Test, FromCharacter)
{
    using Keys = std::vector<PcKey>;
    EXPECT_EQ(pckey::FromCharacter('a'), (Keys{PcKey::A}));
    EXPECT_EQ(pckey::FromCharacter('A'), (Keys{PcKey::LeftShift, PcKey::A}));
    EXPECT_EQ(pckey::FromCharacter('&'), (Keys{PcKey::LeftShift, PcKey::Digit7}));
    EXPECT_EQ(pckey::FromCharacter('.'), (Keys{PcKey::Period}));
    EXPECT_EQ(pckey::FromCharacter(':'), (Keys{PcKey::LeftShift, PcKey::Semicolon}));
    EXPECT_EQ(pckey::FromCharacter(' '), (Keys{PcKey::Space}));
    EXPECT_EQ(pckey::FromCharacter('\n'), (Keys{PcKey::Enter}));
    EXPECT_TRUE(pckey::FromCharacter('\x01').empty());
}
