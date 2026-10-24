/// @file linuxpads_test.cpp
/// @brief The Linux evdev backend's naming and identity as pure logic (platform/linuxpads.h), on every OS: the kinds
/// of pads, the canonical controls of hid-playstation's, hid-nintendo's and joycond's keys and axes (checked against
/// the names the shared HID decoders give the same pads, so a profile works on every OS), the device ids, the
/// Joy-Cons joycond took found in a sysfs tree under scratch/ and the pair id of its combined device. Then the two
/// outputs the backend adds beside the kernel drivers: a DualSense's trigger-only report and the Switch HD rumble
/// without the set-up. hid-generic's keys and axes named by the family's HID map, and the Steam pads the Steam
/// client holds (hid-steam).

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "devicedatabase.h"
#include "hid/drivers/dualsenseoutput.h"
#include "hid/drivers/sonypad.h"
#include "hid/drivers/switchpro.h"
#include "hid/hiddescriptor.h"
#include "hid/hidpad.h"
#include "joyconpairing.h"
#include "platform/linuxpads.h"

using namespace controllers;
namespace fs = std::filesystem;

namespace
{
/// linux/input-event-codes.h (UAPI), spelled out: the test runs where that header does not exist
enum : unsigned
{
    BtnSouth = 0x130,
    BtnEast = 0x131,
    BtnNorth = 0x133,
    BtnWest = 0x134,
    BtnZ = 0x135,
    BtnTl = 0x136,
    BtnTr = 0x137,
    BtnTl2 = 0x138,
    BtnTr2 = 0x139,
    BtnSelect = 0x13a,
    BtnStart = 0x13b,
    BtnMode = 0x13c,
    BtnThumbl = 0x13d,
    BtnThumbr = 0x13e,
    BtnDpadUp = 0x220,
    BtnDpadDown = 0x221,
    BtnDpadLeft = 0x222,
    BtnDpadRight = 0x223,
    BtnGripr = 0x225,
    BtnHappy1 = 0x2c0,
    BtnHappy2 = 0x2c1,
    BtnHappy3 = 0x2c2,
    BtnHappy4 = 0x2c3,
    BtnHappy5 = 0x2c4,
    AbsX = 0x00,
    AbsY = 0x01,
    AbsZ = 0x02,
    AbsRx = 0x03,
    AbsRy = 0x04,
    AbsRz = 0x05,
};

const std::vector<unsigned> kAllKeys = {BtnSouth,  BtnEast,     BtnNorth,    BtnWest,      BtnZ,       BtnTl,
                                        BtnTr,     BtnTl2,      BtnTr2,      BtnSelect,    BtnStart,   BtnMode,
                                        BtnThumbl, BtnThumbr,   BtnDpadUp,   BtnDpadDown,  BtnDpadLeft,
                                        BtnDpadRight, BtnGripr, BtnHappy1,   BtnHappy2,    BtnHappy3,  BtnHappy4,
                                        BtnHappy5};

std::string Button(LinuxPadKind kind, unsigned code, bool analogTriggers = false)
{
    const char* name = LinuxPadButton(kind, code, analogTriggers);
    return name != nullptr ? name : "";
}

/// Every name a kind gives its keys and axes
std::set<std::string> NamesOf(LinuxPadKind kind)
{
    std::set<std::string> names;
    for (const unsigned code : kAllKeys)
        if (const char* name = LinuxPadButton(kind, code))
            names.insert(name);
    for (unsigned axis = 0; axis < 6; ++axis)
        if (const char* name = LinuxPadAxisOf(kind, axis).control)
            names.insert(name);
    return names;
}
}  // namespace

TEST(LinuxPads_Test, Kind_ByIdsDriverAndStick)
{
    EXPECT_EQ(LinuxPadKindFor(0x045E, 0x0B13, false, false), LinuxPadKind::Standard);
    EXPECT_EQ(LinuxPadKindFor(0x054C, 0x0CE6, true, false), LinuxPadKind::Standard);
    EXPECT_EQ(LinuxPadKindFor(0x054C, 0x0DF2, true, false), LinuxPadKind::DualSenseEdge);
    EXPECT_EQ(LinuxPadKindFor(0x057E, 0x2009, true, true), LinuxPadKind::SwitchPro);
    EXPECT_EQ(LinuxPadKindFor(0x057E, 0x2006, false, true), LinuxPadKind::JoyConLeft);
    EXPECT_EQ(LinuxPadKindFor(0x057E, 0x2007, true, true), LinuxPadKind::JoyConRight);
    EXPECT_EQ(LinuxPadKindFor(0x057E, 0x200E, false, true), LinuxPadKind::JoyConLeft) << "a grip half: its stick";
    EXPECT_EQ(LinuxPadKindFor(0x057E, 0x200E, true, true), LinuxPadKind::JoyConRight);
    EXPECT_EQ(LinuxPadKindFor(0x057E, 0x2008, true, false), LinuxPadKind::JoycondCombined);
    EXPECT_EQ(LinuxPadKindFor(0x057E, 0x2009, true, false), LinuxPadKind::Standard)
        << "hid-generic's keys are positional or numbered, not hid-nintendo's";
    EXPECT_EQ(LinuxPadKindFor(0x057E, 0x2007, false, true, "NES Controller"), LinuxPadKind::NsoNes)
        << "an NES pad comes as a right Joy-Con: its name tells it";
    EXPECT_EQ(LinuxPadKindFor(0x057E, 0x2017, false, true), LinuxPadKind::Standard) << "the SNES pad: the Pro's keys";
    EXPECT_EQ(LinuxPadKindFor(0x057E, 0x2019, false, true), LinuxPadKind::NsoN64);
    EXPECT_EQ(LinuxPadKindFor(0x057E, 0x201E, false, true), LinuxPadKind::NsoGenesis);
    EXPECT_TRUE(IsJoyConKind(LinuxPadKind::PairRight));
    EXPECT_FALSE(IsJoyConKind(LinuxPadKind::JoycondCombined));
    EXPECT_TRUE(IsLeftJoyConKind(JoyConKind(true, true)));
    EXPECT_EQ(JoyConKind(false, false), LinuxPadKind::JoyConRight);
}

TEST(LinuxPads_Test, Standard_PositionalNamesAndAnalogTriggers)
{
    EXPECT_EQ(Button(LinuxPadKind::Standard, BtnSouth), "south");
    EXPECT_EQ(Button(LinuxPadKind::Standard, BtnSelect), "view");
    EXPECT_EQ(Button(LinuxPadKind::Standard, BtnGripr), "paddle.p1") << "xpad's Elite paddles";
    EXPECT_EQ(Button(LinuxPadKind::Standard, BtnHappy5), "paddle.p1") << "older xpad";
    EXPECT_EQ(Button(LinuxPadKind::Standard, BtnTl2), "lt");
    // hid-playstation: BTN_TL2 beside ABS_Z, which is `lt`: the key would make it jump between 0..1 and 1
    EXPECT_EQ(Button(LinuxPadKind::Standard, BtnTl2, true), "");
    EXPECT_EQ(Button(LinuxPadKind::Standard, BtnTr2, true), "");
    EXPECT_EQ(Button(LinuxPadKind::Standard, BtnSouth, true), "south");
    EXPECT_EQ(Button(LinuxPadKind::Standard, BtnZ), "") << "BTN_Z has no positional name";
    EXPECT_EQ(Button(LinuxPadKind::Standard, BtnHappy1), "");

    const LinuxPadAxis lt = LinuxPadAxisOf(LinuxPadKind::Standard, AbsZ);
    EXPECT_STREQ(lt.control, "lt");
    EXPECT_TRUE(lt.unit);
    EXPECT_STREQ(LinuxPadAxisOf(LinuxPadKind::Standard, AbsRz).control, "rt");
    const LinuxPadAxis y = LinuxPadAxisOf(LinuxPadKind::Standard, AbsY);
    EXPECT_STREQ(y.control, "lstick.y");
    EXPECT_FALSE(y.unit);
    EXPECT_FALSE(y.negate);
}

TEST(LinuxPads_Test, DualSenseEdge_TheFullReportsNames)
{
    // hid-playstation 7.2 (BTN_TRIGGER_HAPPY1-4): Fn1, Fn2, the left and the right paddle
    EXPECT_EQ(Button(LinuxPadKind::DualSenseEdge, BtnHappy1), "misc.2");
    EXPECT_EQ(Button(LinuxPadKind::DualSenseEdge, BtnHappy2), "misc.3");
    EXPECT_EQ(Button(LinuxPadKind::DualSenseEdge, BtnHappy3), "paddle.p3");
    EXPECT_EQ(Button(LinuxPadKind::DualSenseEdge, BtnHappy4), "paddle.p1");
    EXPECT_EQ(Button(LinuxPadKind::DualSenseEdge, BtnSouth), "south");
    EXPECT_EQ(Button(LinuxPadKind::DualSenseEdge, BtnTl2, true), "");
    const std::vector<std::string> sony = SonyPad::Controls(SonyModel::DualSenseEdge);
    for (const std::string& name : NamesOf(LinuxPadKind::DualSenseEdge))
    {
        if (name.rfind("paddle.p3", 0) == 0 || name.rfind("paddle.p4", 0) == 0)
            continue;  // xpad's Elite names, never sent by hid-playstation
        EXPECT_NE(std::find(sony.begin(), sony.end(), name), sony.end()) << name << ": SonyPad has it on macOS";
    }
}

TEST(LinuxPads_Test, SwitchPro_CaptureIsShare)
{
    EXPECT_EQ(Button(LinuxPadKind::SwitchPro, BtnZ), "share");
    // hid-nintendo: A = BTN_EAST, B = BTN_SOUTH: positional, as SDL's Pro controller
    EXPECT_EQ(Button(LinuxPadKind::SwitchPro, BtnEast), "east");
    EXPECT_EQ(Button(LinuxPadKind::SwitchPro, BtnTl2), "lt");
    EXPECT_EQ(Button(LinuxPadKind::SwitchPro, BtnMode), "guide");
    const std::vector<std::string> pro = SwitchProPad::Controls(SwitchLayout::Pro);
    for (const std::string& name : NamesOf(LinuxPadKind::SwitchPro))
    {
        if (name.rfind("paddle.", 0) == 0)
            continue;  // xpad's Elite names, never sent by hid-nintendo
        EXPECT_NE(std::find(pro.begin(), pro.end(), name), pro.end()) << name;
    }
}

TEST(LinuxPads_Test, JoyConsAlone_HeldSidewaysAsSdlAndMacos)
{
    // A left Joy-Con: its arrows the faces, SL / SR the shoulders, - the menu
    EXPECT_EQ(Button(LinuxPadKind::JoyConLeft, BtnDpadLeft), "south");
    EXPECT_EQ(Button(LinuxPadKind::JoyConLeft, BtnDpadDown), "east");
    EXPECT_EQ(Button(LinuxPadKind::JoyConLeft, BtnDpadUp), "west");
    EXPECT_EQ(Button(LinuxPadKind::JoyConLeft, BtnDpadRight), "north");
    EXPECT_EQ(Button(LinuxPadKind::JoyConLeft, BtnTr), "lb") << "SL (BTN_TR off the grip)";
    EXPECT_EQ(Button(LinuxPadKind::JoyConLeft, BtnTr2), "rb") << "SR";
    EXPECT_EQ(Button(LinuxPadKind::JoyConLeft, BtnSelect), "menu");
    EXPECT_EQ(Button(LinuxPadKind::JoyConLeft, BtnZ), "guide") << "Capture";
    EXPECT_EQ(Button(LinuxPadKind::JoyConLeft, BtnTl), "paddle.p3") << "L";
    EXPECT_EQ(Button(LinuxPadKind::JoyConLeft, BtnSouth), "");
    // A right Joy-Con: A B X Y turned a quarter
    EXPECT_EQ(Button(LinuxPadKind::JoyConRight, BtnEast), "south") << "A";
    EXPECT_EQ(Button(LinuxPadKind::JoyConRight, BtnNorth), "east") << "X";
    EXPECT_EQ(Button(LinuxPadKind::JoyConRight, BtnSouth), "west") << "B";
    EXPECT_EQ(Button(LinuxPadKind::JoyConRight, BtnWest), "north") << "Y";
    EXPECT_EQ(Button(LinuxPadKind::JoyConRight, BtnTl), "lb") << "SL (BTN_TL off the grip)";
    EXPECT_EQ(Button(LinuxPadKind::JoyConRight, BtnTl2), "rb") << "SR";
    EXPECT_EQ(Button(LinuxPadKind::JoyConRight, BtnTr), "paddle.p1") << "R";
    EXPECT_EQ(Button(LinuxPadKind::JoyConRight, BtnThumbr), "lstick.click") << "its one stick is the left one";

    // The stick a quarter turned: the left one's x is evdev's y, its y evdev's x negated
    LinuxPadAxis axis = LinuxPadAxisOf(LinuxPadKind::JoyConLeft, AbsY);
    EXPECT_STREQ(axis.control, "lstick.x");
    EXPECT_FALSE(axis.negate);
    axis = LinuxPadAxisOf(LinuxPadKind::JoyConLeft, AbsX);
    EXPECT_STREQ(axis.control, "lstick.y");
    EXPECT_TRUE(axis.negate);
    axis = LinuxPadAxisOf(LinuxPadKind::JoyConRight, AbsRy);
    EXPECT_STREQ(axis.control, "lstick.x");
    EXPECT_TRUE(axis.negate);
    axis = LinuxPadAxisOf(LinuxPadKind::JoyConRight, AbsRx);
    EXPECT_STREQ(axis.control, "lstick.y");
    EXPECT_FALSE(axis.negate);
    EXPECT_EQ(LinuxPadAxisOf(LinuxPadKind::JoyConRight, AbsX).control, nullptr);

    for (const auto& [kind, layout] : {std::pair{LinuxPadKind::JoyConLeft, SwitchLayout::JoyConLeft},
                                       std::pair{LinuxPadKind::JoyConRight, SwitchLayout::JoyConRight}})
    {
        const std::vector<std::string> controls = SwitchProPad::Controls(layout);
        for (const std::string& name : NamesOf(kind))
            EXPECT_NE(std::find(controls.begin(), controls.end(), name), controls.end())
                << name << ": SwitchProPad has it for the same Joy-Con";
    }
}

TEST(LinuxPads_Test, JoyConPair_HalvesOfOnePad)
{
    EXPECT_EQ(Button(LinuxPadKind::PairLeft, BtnDpadUp), "dpad.up");
    EXPECT_EQ(Button(LinuxPadKind::PairLeft, BtnTl), "lb");
    EXPECT_EQ(Button(LinuxPadKind::PairLeft, BtnTl2), "lt");
    EXPECT_EQ(Button(LinuxPadKind::PairLeft, BtnSelect), "view");
    EXPECT_EQ(Button(LinuxPadKind::PairLeft, BtnZ), "share");
    EXPECT_EQ(Button(LinuxPadKind::PairLeft, BtnTr), "paddle.p3") << "SL";
    EXPECT_EQ(Button(LinuxPadKind::PairLeft, BtnTr2), "paddle.p4") << "SR";
    EXPECT_EQ(Button(LinuxPadKind::PairRight, BtnSouth), "south") << "B, positional";
    EXPECT_EQ(Button(LinuxPadKind::PairRight, BtnEast), "east") << "A";
    EXPECT_EQ(Button(LinuxPadKind::PairRight, BtnTr2), "rt");
    EXPECT_EQ(Button(LinuxPadKind::PairRight, BtnTl2), "paddle.p1") << "SR";
    EXPECT_EQ(Button(LinuxPadKind::PairRight, BtnTl), "paddle.p2") << "SL";
    EXPECT_EQ(Button(LinuxPadKind::PairRight, BtnThumbr), "rstick.click");
    EXPECT_STREQ(LinuxPadAxisOf(LinuxPadKind::PairLeft, AbsX).control, "lstick.x");
    EXPECT_STREQ(LinuxPadAxisOf(LinuxPadKind::PairRight, AbsRy).control, "rstick.y");
    EXPECT_FALSE(LinuxPadAxisOf(LinuxPadKind::PairRight, AbsRy).negate);

    for (const auto& [kind, layout] : {std::pair{LinuxPadKind::PairLeft, SwitchLayout::PairLeft},
                                       std::pair{LinuxPadKind::PairRight, SwitchLayout::PairRight}})
    {
        const std::vector<std::string> controls = SwitchProPad::Controls(layout);
        for (const std::string& name : NamesOf(kind))
            EXPECT_NE(std::find(controls.begin(), controls.end(), name), controls.end()) << name;
    }
    // The two halves never give one name twice: one pad
    const std::set<std::string> left = NamesOf(LinuxPadKind::PairLeft);
    for (const std::string& name : NamesOf(LinuxPadKind::PairRight))
        EXPECT_EQ(left.count(name), 0u) << name;
}

TEST(LinuxPads_Test, JoycondCombined_SlSrAsThePairsPaddles)
{
    EXPECT_EQ(Button(LinuxPadKind::JoycondCombined, BtnZ), "share");
    EXPECT_EQ(Button(LinuxPadKind::JoycondCombined, BtnHappy1), "paddle.p3") << "the left SL";
    EXPECT_EQ(Button(LinuxPadKind::JoycondCombined, BtnHappy2), "paddle.p4") << "the left SR";
    EXPECT_EQ(Button(LinuxPadKind::JoycondCombined, BtnHappy3), "paddle.p2") << "the right SL";
    EXPECT_EQ(Button(LinuxPadKind::JoycondCombined, BtnHappy4), "paddle.p1") << "the right SR";
    EXPECT_EQ(Button(LinuxPadKind::JoycondCombined, BtnTl2), "lt");
    EXPECT_STREQ(LinuxPadAxisOf(LinuxPadKind::JoycondCombined, AbsRx).control, "rstick.x");
    // The same names a pair the backend joins gives the same buttons
    std::set<std::string> pair = NamesOf(LinuxPadKind::PairLeft);
    const std::set<std::string> right = NamesOf(LinuxPadKind::PairRight);
    pair.insert(right.begin(), right.end());
    for (const char* name : {"paddle.p1", "paddle.p2", "paddle.p3", "paddle.p4", "share"})
        EXPECT_EQ(pair.count(name), 1u) << name;
}

TEST(LinuxPads_Test, DeviceId_ByBusAsArchitectureSection5)
{
    EXPECT_EQ(LinuxDeviceId(0x05, 0x054C, 0x0CE6, 0x8100, "a0:b1:c2:d3:e4:f5", "00:11:22:33:44:55", "/dev/input/event3"),
              "bt:054c:0ce6:" + LinuxIdHash("a0:b1:c2:d3:e4:f5"));
    EXPECT_EQ(LinuxDeviceId(0x05, 0x057E, 0x2009, 0x8001, "", "00:11:22:33:44:55", "/dev/input/event3"),
              "bt:057e:2009:" + LinuxIdHash("00:11:22:33:44:55"));
    // hid-playstation sets uniq to the MAC over USB too
    EXPECT_EQ(LinuxDeviceId(0x03, 0x054C, 0x0CE6, 0x0100, "a0:b1:c2:d3:e4:f5", "usb-0000:00:14.0-2/input3", "x"),
              "usb:054c:0ce6:0100:a0:b1:c2:d3:e4:f5");
    EXPECT_EQ(LinuxDeviceId(0x03, 0x045E, 0x0B12, 0x0509, "", "usb-0000:00:14.0-2/input0", "x"),
              "usb:045e:0b12:0509@" + LinuxIdHash("usb-0000:00:14.0-2/input0"));
    EXPECT_EQ(LinuxDeviceId(0x06, 0x057E, 0x2008, 0x0000, "", "", "/dev/input/event20"),
              "virtual:057e:2008:0000@" + LinuxIdHash("/dev/input/event20"));
    EXPECT_EQ(LinuxIdHash("").size(), 8u);
}

namespace
{
/// One Joy-Con's input device in a sysfs tree: class/input/inputN/{id/*, name, uniq, phys, capabilities/abs, eventM}
void AddInput(const fs::path& root, int n, uint16_t product, const std::string& name, const std::string& uniq,
              bool rightStick, int event)
{
    const fs::path input = root / "class" / "input" / ("input" + std::to_string(n));
    fs::create_directories(input / "id");
    fs::create_directories(input / "capabilities");
    char hex[8];
    std::snprintf(hex, sizeof(hex), "%04x", product);
    std::ofstream(input / "id" / "bustype") << "0005\n";
    std::ofstream(input / "id" / "vendor") << "057e\n";
    std::ofstream(input / "id" / "product") << hex << "\n";
    std::ofstream(input / "id" / "version") << "8001\n";
    std::ofstream(input / "name") << name << "\n";
    std::ofstream(input / "uniq") << uniq << "\n";
    std::ofstream(input / "phys") << "00:11:22:33:44:55\n";
    // ABS_X | ABS_Y = 0x3; ABS_RX | ABS_RY = 0x18 (a 64-bit kernel prints longs: one word here)
    std::ofstream(input / "capabilities" / "abs") << (rightStick ? "18" : "3") << "\n";
    if (event >= 0)
        fs::create_directories(input / ("event" + std::to_string(event)));
}
}  // namespace

TEST(LinuxPads_Test, Joycond_FindsTheTakenHalvesAndNamesTheirPair)
{
    const fs::path root = TestPathHelper::GetUniqueTestScratchPath("linuxpads-sys");
    fs::remove_all(root);
    AddInput(root, 10, 0x2006, "Nintendo Switch Left Joy-Con", "aa:aa:aa:aa:aa:01", false, 20);
    AddInput(root, 11, 0x2006, "Nintendo Switch Left Joy-Con (IMU)", "aa:aa:aa:aa:aa:01", false, 21);
    AddInput(root, 12, 0x2007, "Nintendo Switch Right Joy-Con", "bb:bb:bb:bb:bb:02", true, 22);
    AddInput(root, 13, 0x2009, "Nintendo Switch Pro Controller", "cc:cc:cc:cc:cc:03", true, 23);

    std::set<std::string> taken = {"/dev/input/event20", "/dev/input/event22"};
    auto readable = [&taken](const std::string& node) { return taken.count(node) == 0; };
    std::vector<LinuxJoyConHalf> halves = FindJoyConHalves(root.string(), "/dev/input", readable);
    ASSERT_EQ(halves.size(), 2u) << "the IMU node and the Pro controller are no halves";
    EXPECT_TRUE(halves[0].left);
    EXPECT_FALSE(halves[1].left);
    EXPECT_TRUE(halves[0].taken);
    const std::string left = "bt:057e:2006:" + LinuxIdHash("aa:aa:aa:aa:aa:01");
    const std::string right = "bt:057e:2007:" + LinuxIdHash("bb:bb:bb:bb:bb:02");
    EXPECT_EQ(halves[0].id, left) << "the id the backend gives the same Joy-Con when it reads it";
    EXPECT_EQ(halves[1].id, right);
    EXPECT_EQ(JoycondPairId(halves), JoyConPairing::PairId(left, right));

    // One taken only: joycond has not joined them (or the user is root): no pair id
    taken = {"/dev/input/event20"};
    EXPECT_EQ(JoycondPairId(FindJoyConHalves(root.string(), "/dev/input", readable)), "");
    // Two taken left halves: which one joycond joined is not visible
    AddInput(root, 14, 0x2006, "Nintendo Switch Left Joy-Con", "aa:aa:aa:aa:aa:04", false, 24);
    taken = {"/dev/input/event20", "/dev/input/event22", "/dev/input/event24"};
    EXPECT_EQ(JoycondPairId(FindJoyConHalves(root.string(), "/dev/input", readable)), "");

    // A grip's halves (057E:200E): the side by the stick
    fs::remove_all(root);
    AddInput(root, 1, 0x200E, "Nintendo Switch Left Joy-Con", "", false, 5);
    AddInput(root, 2, 0x200E, "Nintendo Switch Right Joy-Con", "", true, 6);
    halves = FindJoyConHalves(root.string(), "/dev/input", [](const std::string&) { return false; });
    ASSERT_EQ(halves.size(), 2u);
    EXPECT_TRUE(halves[0].left);
    EXPECT_FALSE(halves[1].left);
    EXPECT_FALSE(JoycondPairId(halves).empty());
    EXPECT_TRUE(FindJoyConHalves((root / "none").string(), "/dev/input", readable).empty());
    fs::remove_all(root);
}

TEST(LinuxOutputs_Test, DualSenseTriggerReport_OnlyTheTriggerFlags)
{
    RumbleLevels levels;
    levels.low = 1.0f;  // ignored: the motors are hid-playstation's
    levels.leftTrigger = 0.5f;
    levels.rightTrigger = 1.0f;
    const std::vector<uint8_t> usb = DualSenseOutput::TriggerReport(levels, false, 0);
    ASSERT_GE(usb.size(), 48u);
    EXPECT_EQ(usb[0], 0x02);
    const uint8_t* common = usb.data() + 1;
    EXPECT_EQ(common[0], 0x0C) << "valid flag 0: the right (0x04) and the left (0x08) trigger only";
    EXPECT_EQ(common[1], 0x00) << "valid flag 1: no light, no player LEDs, no mute LED";
    EXPECT_EQ(common[2], 0x00) << "no motor level";
    EXPECT_EQ(common[3], 0x00);
    EXPECT_EQ(common[38], 0x00) << "valid flag 2: no compatible vibration 2";
    EXPECT_NE(common[10], 0x00) << "the right trigger's effect";
    EXPECT_NE(common[21], 0x00) << "the left trigger's effect";

    // The same with the motors set (DualSenseOutput::Report through rumble) carries them as well
    DualSenseOutputState full;
    full.rumble = levels;
    const std::vector<uint8_t> both = DualSenseOutput::Report(full, false, 0);
    EXPECT_EQ(both[1 + 10], common[10]) << "the same trigger bytes";
    EXPECT_EQ(both[1 + 21], common[21]);
    EXPECT_NE(both[1] & 0x03, 0) << "and the motor flags";

    RumbleLevels off;
    const std::vector<uint8_t> released = DualSenseOutput::TriggerReport(off, false, 0);
    EXPECT_EQ(released[1], 0x0C) << "zero turns the resistance off: still both flags";

    const std::vector<uint8_t> bluetooth = DualSenseOutput::TriggerReport(levels, true, 5);
    EXPECT_EQ(bluetooth[0], 0x31);
    EXPECT_EQ(bluetooth[1], 0x50) << "the sequence in the high nibble";
    EXPECT_EQ(bluetooth[3], 0x0C) << "the common part from byte 3";
}

TEST(LinuxOutputs_Test, SwitchRumbleOnly_NoSetupNoPlayerLightsJustTheHdRumble)
{
    std::unique_ptr<SwitchProtocol> protocol = SwitchProtocol::RumbleOnly(true);
    EXPECT_TRUE(protocol->Ready()) << "hid-nintendo did the set-up";
    uint64_t wake = 0;
    EXPECT_FALSE(protocol->Next(0, wake).has_value()) << "nothing to write before an effect";
    protocol->SetPlayer(3);
    EXPECT_FALSE(protocol->Next(1, wake).has_value()) << "the kernel's player lights stay";

    RumbleLevels levels;
    levels.low = 0.5f;
    levels.high = 0.25f;
    levels.lowHz = 100;
    levels.highHz = 600;
    protocol->SetRumble(levels);
    // Past the rumble interval (SwitchProtocol::kRumbleIntervalMs) of the neutral state the first Next took as written
    const std::optional<PadOutReport> out = protocol->Next(100, wake);
    ASSERT_TRUE(out.has_value());
    ASSERT_GE(out->bytes.size(), 10u);
    EXPECT_EQ(out->bytes[0], 0x10) << "the rumble-only report";
    const std::array<uint8_t, 8> wanted = SwitchRumble::Pair(levels);
    EXPECT_TRUE(std::equal(wanted.begin(), wanted.end(), out->bytes.begin() + 2)) << "the effect's frequencies";
    // At the end the motors are stopped
    const std::optional<PadOutReport> last = protocol->Final();
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->bytes[0], 0x10);
}

/// hid-generic: hid-input's key for HID button n turned back into n, then named by the family's HID map, the names
/// HidPad gives the same buttons from the descriptor on macOS and Windows
TEST(LinuxPads_Test, HidGeneric_ButtonsByTheFamilysHidMapAsHidPadNamesThem)
{
    constexpr unsigned kBtnJoystick = 0x120;  // BTN_TRIGGER: Button 1 of a Joystick application
    constexpr unsigned kBtnGamepad = 0x130;   // BTN_SOUTH: Button 1 of a Game Pad application
    constexpr unsigned kBtnTriggerHappy1 = 0x2c0;
    EXPECT_EQ(LinuxGenericButtonIndex(kBtnJoystick), 0);
    EXPECT_EQ(LinuxGenericButtonIndex(kBtnJoystick + 15), 15);
    EXPECT_EQ(LinuxGenericButtonIndex(kBtnGamepad + 3), 3);
    EXPECT_EQ(LinuxGenericButtonIndex(kBtnTriggerHappy1), 16) << "Button 17 and above";
    EXPECT_EQ(LinuxGenericButtonIndex(0x220), -1) << "BTN_DPAD_UP is a usage of its own, not a button number";

    // An Xbox pad over Bluetooth under hid-generic / hid-microsoft: Button 4 (BTN_NORTH by its code) is X, the west one
    const HidLayout xbox = HidLayoutFor(std::string("xbox"));
    EXPECT_EQ(LinuxGenericButton(xbox, kBtnGamepad), "south");
    EXPECT_EQ(LinuxGenericButton(xbox, kBtnGamepad + 3), "west");
    EXPECT_EQ(LinuxGenericButton(xbox, kBtnGamepad + 4), "north");
    EXPECT_EQ(LinuxGenericButton(xbox, kBtnGamepad + 2), "button.2") << "a usage the layout leaves unnamed";
    EXPECT_EQ(LinuxGenericButton(xbox, kBtnGamepad + 12), "guide");
    EXPECT_EQ(LinuxGenericButton(xbox, 0x220), "");
    // Consumer usages in the pad's application: KEY_* on its node (hid-input), by the map's `consumer`
    EXPECT_EQ(LinuxConsumerUsage(172), 0x223) << "KEY_HOMEPAGE: AC Home";
    EXPECT_EQ(LinuxConsumerUsage(158), 0x224) << "KEY_BACK: AC Back";
    EXPECT_EQ(LinuxConsumerUsage(167), 0x0B2) << "KEY_RECORD: Record";
    EXPECT_EQ(LinuxConsumerUsage(0x130), -1);
    EXPECT_EQ(LinuxGenericButton(xbox, 167), "share") << "an Xbox pad's Record, as HidPad names it";
    EXPECT_EQ(LinuxGenericButton(xbox, 172), "");
    const DeviceMatch luna = DeviceDatabase::Builtin().Lookup({"bt", 0x0171, 0x0419, -1, "", "evdev"});
    ASSERT_EQ(luna.family, "luna");
    const HidLayout lunaLayout = HidLayoutFor(luna);
    EXPECT_EQ(LinuxGenericButton(lunaLayout, 172), "guide");
    EXPECT_EQ(LinuxGenericButton(lunaLayout, 158), "view");
    EXPECT_EQ(LinuxGenericButton(lunaLayout, 167), "misc.1");
    const HidLayout generic = HidLayoutFor(std::string("generic"));
    EXPECT_EQ(LinuxGenericButton(generic, kBtnJoystick + 5), "button.5");
    EXPECT_EQ(LinuxGenericButton(generic, kBtnTriggerHappy1 + 1), "button.17");

    // A family's own map from the database (the Mayflash GameCube adapter in its 1844 mode, a Joystick application)
    const DeviceMatch mayflash = DeviceDatabase::Builtin().Lookup({"usb", 0x0079, 0x1844, -1, "", "evdev"});
    ASSERT_EQ(mayflash.family, "gamecube-mayflash");
    const HidLayout mapped = HidLayoutFor(mayflash);
    EXPECT_EQ(LinuxGenericButton(mapped, kBtnJoystick), "west");
    EXPECT_EQ(LinuxGenericButton(mapped, kBtnJoystick + 12), "dpad.up");

    // The same names HidPad gives a 16-button Game Pad descriptor with each layout
    const uint8_t descriptor[] = {0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x10,
                                  0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02, 0xC0};
    HidDescriptor parsed;
    std::string error;
    ASSERT_TRUE(ParseHidDescriptor(descriptor, sizeof(descriptor), parsed, error)) << error;
    for (const HidLayout& layout : {xbox, generic, mapped, HidLayoutFor(std::string("playstation"))})
    {
        std::vector<std::string> fromHid = HidPad(parsed, layout).Controls();
        std::vector<std::string> fromEvdev;
        for (unsigned n = 0; n < 16; ++n)
            fromEvdev.push_back(LinuxGenericButton(layout, kBtnGamepad + n));
        std::sort(fromHid.begin(), fromHid.end());
        std::sort(fromEvdev.begin(), fromEvdev.end());
        EXPECT_EQ(fromEvdev, fromHid) << layout.family;
    }
}

TEST(LinuxPads_Test, HidGeneric_AxesByTheLayout)
{
    constexpr unsigned kAbsX = 0x00, kAbsZ = 0x02, kAbsRx = 0x03, kAbsRz = 0x05, kAbsGas = 0x09, kAbsBrake = 0x0a;
    const HidLayout playstation = HidLayoutFor(std::string("playstation"));
    EXPECT_STREQ(LinuxGenericAxis(playstation, kAbsZ).control, "rstick.x") << "Z / Rz the right stick, as HidPad";
    EXPECT_STREQ(LinuxGenericAxis(playstation, kAbsRz).control, "rstick.y");
    EXPECT_STREQ(LinuxGenericAxis(playstation, kAbsRx).control, "lt");
    EXPECT_TRUE(LinuxGenericAxis(playstation, kAbsRx).unit);
    const HidLayout xbox = HidLayoutFor(std::string("xbox"));
    EXPECT_EQ(LinuxGenericAxis(xbox, kAbsRx).control, nullptr) << "an extra axis: axis.<code>";
    EXPECT_STREQ(LinuxGenericAxis(xbox, kAbsBrake).control, "lt") << "Brake / Accelerator (the Stadia)";
    EXPECT_STREQ(LinuxGenericAxis(xbox, kAbsGas).control, "rt");
    HidLayout snes = HidLayoutFor(std::string("generic"));
    snes.dpadAxes = true;
    EXPECT_TRUE(LinuxGenericAxis(snes, kAbsX).dpad) << "the cross on X / Y";
    EXPECT_FALSE(LinuxGenericAxis(xbox, kAbsX).dpad);
    EXPECT_STREQ(LinuxGenericAxis(xbox, kAbsX).control, "lstick.x");
}

namespace
{
void AddSteamHid(const fs::path& root, const std::string& dir, const std::string& name, const std::string& phys,
                 bool hidraw, bool input)
{
    const fs::path device = root / "bus" / "hid" / "drivers" / "hid-steam" / dir;
    fs::create_directories(device);
    std::ofstream(device / "uevent") << "DRIVER=hid-steam\nHID_ID=0003:000028DE:00001102\nHID_NAME=" << name
                                     << "\nHID_PHYS=" << phys << "\nHID_UNIQ=\n";
    if (hidraw)
        fs::create_directories(device / "hidraw" / "hidraw3");
    if (input)
        fs::create_directories(device / "input" / "input30");
}
}  // namespace

/// hid-steam: the game pad interface (no hidraw node of its own) without its input device while Steam has it
TEST(LinuxPads_Test, HidSteam_ThePadsTheSteamClientHolds)
{
    const fs::path root = TestPathHelper::GetUniqueTestScratchPath("linuxpads-steam");
    fs::remove_all(root);
    const fs::path drivers = root / "bus" / "hid" / "drivers" / "hid-steam";
    fs::create_directories(drivers);
    std::ofstream(drivers / "bind") << "";
    // A wired 2015 controller: the lizard keyboard (hidraw and input) and the pad interface, taken
    AddSteamHid(root, "0003:28DE:1102.0001", "Valve Software Steam Controller", "usb-0000:00:14.0-1/input1", true,
                true);
    AddSteamHid(root, "0003:28DE:1102.0002", "Valve Software Steam Controller", "usb-0000:00:14.0-1/input2", false,
                false);
    // A Steam Deck whose pad hid-steam still offers (Steam not holding it): not listed
    AddSteamHid(root, "0003:28DE:1205.0003", "Valve Software Steam Deck Controller", "usb-0000:04:00.3-3/input2",
                false, true);
    // A 2015 wireless receiver: two slots without a pad, one entry
    AddSteamHid(root, "0003:28DE:1142.0004", "Valve Software Wireless Steam Controller", "usb-0000:00:14.0-2/input1",
                false, false);
    AddSteamHid(root, "0003:28DE:1142.0005", "Valve Software Wireless Steam Controller", "usb-0000:00:14.0-2/input2",
                false, false);

    const std::vector<LinuxSteamHeld> held = FindSteamHeldPads(root.string());
    ASSERT_EQ(held.size(), 2u);
    EXPECT_EQ(held[0].product, 0x1102);
    EXPECT_EQ(held[0].name, "Valve Software Steam Controller");
    EXPECT_EQ(held[0].id, "usb:28de:1102:0000@" + LinuxIdHash("usb-0000:00:14.0-1/input2"));
    EXPECT_EQ(held[1].product, 0x1142);
    EXPECT_EQ(held[1].id, "usb:28de:1142:0000@" + LinuxIdHash("usb-0000:00:14.0-2")) << "the receiver, not a slot";
    EXPECT_NE(SteamHeldNotice().find("Quit Steam"), std::string::npos);
    EXPECT_TRUE(FindSteamHeldPads((root / "none").string()).empty());

    // The client: a process whose comm is `steam` (steamwebhelper is not the one that holds the pads)
    const fs::path proc = root / "proc";
    fs::create_directories(proc / "1");
    std::ofstream(proc / "1" / "comm") << "systemd\n";
    fs::create_directories(proc / "40");
    std::ofstream(proc / "40" / "comm") << "steamwebhelper\n";
    fs::create_directories(proc / "self");
    EXPECT_FALSE(SteamClientRunning(proc.string()));
    fs::create_directories(proc / "41");
    std::ofstream(proc / "41" / "comm") << "steam\n";
    EXPECT_TRUE(SteamClientRunning(proc.string()));
    fs::remove_all(root);
}

/// hid-nintendo's NSO maps (6.8+, nescon / snescon / n64con / gencon_button_mappings) named as SwitchProPad names the
/// same buttons from the pad's report on macOS
TEST(LinuxPads_Test, NsoPads_KeysNamedAsSwitchProPadNamesThem)
{
    constexpr unsigned kSouth = 0x130, kEast = 0x131, kNorth = 0x133, kWest = 0x134, kZ = 0x135, kTl = 0x136,
                       kTr = 0x137, kTl2 = 0x138, kTr2 = 0x139, kSelect = 0x13a, kStart = 0x13b, kMode = 0x13c;
    constexpr unsigned kLeft = 0x110, kRight = 0x111, kForward = 0x115, kBack = 0x116;
    struct Case
    {
        LinuxPadKind kind;
        SwitchLayout layout;
        std::vector<std::pair<unsigned, std::string>> keys;  // the kernel's key of a button, the name it must get
    };
    const std::vector<Case> cases = {
        {LinuxPadKind::NsoNes,
         SwitchLayout::Nes,
         {{kSouth, "east"}, {kEast, "south"}, {kTl, "lb"}, {kTr, "rb"}, {kSelect, "view"}, {kStart, "menu"}}},
        {LinuxPadKind::Standard,
         SwitchLayout::Snes,
         {{kEast, "east"}, {kSouth, "south"}, {kNorth, "north"}, {kWest, "west"}, {kTl, "lb"}, {kTr, "rb"},
          {kTl2, "lt"}, {kTr2, "rt"}, {kSelect, "view"}, {kStart, "menu"}}},
        {LinuxPadKind::NsoN64,
         SwitchLayout::N64,
         {{kSouth, "south"}, {kEast, "west"}, {kTl2, "lt"}, {kTl, "lb"}, {kTr, "rb"}, {kTr2, "rt"}, {kStart, "menu"},
          {kForward, "misc.1"}, {kBack, "misc.2"}, {kLeft, "misc.3"}, {kRight, "misc.4"}, {kMode, "guide"},
          {kZ, "share"}}},
        {LinuxPadKind::NsoGenesis,
         SwitchLayout::Genesis,
         {{kSouth, "south"}, {kEast, "east"}, {kWest, "rb"}, {kSelect, "view"}, {kStart, "menu"}, {kMode, "guide"},
          {kZ, "share"}}},
    };
    for (const Case& test : cases)
    {
        const std::vector<std::string> controls = SwitchProPad::Controls(test.layout);
        for (const auto& [key, name] : test.keys)
        {
            const char* got = LinuxPadButton(test.kind, key);
            ASSERT_NE(got, nullptr) << static_cast<int>(test.layout) << " key " << key;
            EXPECT_EQ(std::string(got), name) << static_cast<int>(test.layout) << " key " << key;
            EXPECT_NE(std::find(controls.begin(), controls.end(), name), controls.end())
                << name << ": not a control SwitchProPad gives the layout";
        }
    }
}

/// The GameCube adapter has no input device: its hidraw node found by the HID device's ids
TEST(LinuxPads_Test, HidrawDevices_ByTheHidDevicesIds)
{
    const fs::path root = TestPathHelper::GetUniqueTestScratchPath("linuxpads-hidraw");
    fs::remove_all(root);
    const auto add = [&root](const std::string& node, const std::string& uevent) {
        const fs::path device = root / "class" / "hidraw" / node / "device";
        fs::create_directories(device);
        std::ofstream(device / "uevent") << uevent;
    };
    add("hidraw10", "DRIVER=hid-generic\nHID_ID=0003:0000057E:00000337\nHID_NAME=Nintendo Co., Ltd. WUP-028\n"
                    "HID_PHYS=usb-0000:00:14.0-3/input0\nHID_UNIQ=\n");
    add("hidraw2", "DRIVER=hid-generic\nHID_ID=0003:0000057E:00000337\nHID_NAME=Nintendo Co., Ltd. WUP-028\n"
                   "HID_PHYS=usb-0000:00:14.0-4/input0\nHID_UNIQ=\n");
    add("hidraw3", "DRIVER=playstation\nHID_ID=0003:0000054C:00000CE6\nHID_NAME=DualSense\nHID_PHYS=x\n");
    add("hidraw4", "MODALIAS=platform:x\n");
    const std::vector<LinuxHidrawDevice> found = FindHidrawDevices(root.string(), "/dev", 0x057E, 0x0337);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[0].node, "/dev/hidraw2") << "in node order";
    EXPECT_EQ(found[1].node, "/dev/hidraw10");
    EXPECT_EQ(found[0].name, "Nintendo Co., Ltd. WUP-028");
    EXPECT_EQ(found[0].id, "usb:057e:0337:0000@" + LinuxIdHash("usb-0000:00:14.0-4/input0"));
    EXPECT_NE(found[0].id, found[1].id);
    EXPECT_TRUE(FindHidrawDevices((root / "none").string(), "/dev", 0x057E, 0x0337).empty());
    fs::remove_all(root);
}
