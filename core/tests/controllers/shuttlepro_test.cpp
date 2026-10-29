/// @file shuttlepro_test.cpp
/// @brief A control surface defined by data alone (control surfaces devices.md §4): the Contour ShuttlePRO v2's
/// devices.json family (its `hid` map: `"application": "any"`, the shuttle read `split` over -7..7 against the
/// descriptor's relative -128..127, the jog wheel a wrapping `counter`, the two keys the descriptor leaves out as
/// `bits`), HidPad's `encoder` / `counter` / `split` reads on its real descriptor, the database's surface products the
/// backends open by id, the Linux hidraw use, the control names, the shipped debug profile through the router, and
/// a capture of the device (testdata/controllers/hid-captures/shuttle-pro/macos-usb/).

#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "devicedatabase.h"
#include "hid/hiddescriptor.h"
#include "hid/hidpad.h"
#include "hid/hidrawnode.h"
#include "hostcontrols.h"
#include "profiles/profile.h"
#include "profiles/profilestore.h"
#include "router/inputrouter.h"

using namespace controllers;

namespace
{
using Events = std::map<std::string, float>;

/// The ShuttlePRO v2's report descriptor as macOS reads it (0B33:0030): a Consumer Control with a Wheel (relative,
/// -128..127), a Dial (0..255), a constant byte and 13 buttons in 16 bits; 5-byte reports, no report id
const std::vector<uint8_t> kShuttleProDescriptor = {
    0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01, 0xa1, 0x02, 0x05, 0x01, 0x09, 0x38, 0x15, 0x80, 0x25, 0x7f, 0x66, 0x00, 0x00,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0x09, 0x37, 0x15, 0x00, 0x26, 0xff, 0x00, 0x66, 0x00, 0x00, 0x75, 0x08, 0x95,
    0x01, 0x81, 0x2e, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0d, 0x15, 0x00, 0x25, 0x01,
    0x35, 0x00, 0x45, 0x01, 0x75, 0x01, 0x95, 0x0d, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03, 0x81, 0x01, 0xc0, 0xc0};

HidDescriptor Parse(const std::vector<uint8_t>& bytes)
{
    HidDescriptor descriptor;
    std::string error;
    EXPECT_TRUE(ParseHidDescriptor(bytes.data(), bytes.size(), descriptor, error)) << error;
    return descriptor;
}

Events Decode(HidPad& pad, const std::vector<uint8_t>& report)
{
    Events events;
    pad.Decode(report.data(), report.size(), [&events](const std::string& control, float value) {
        events[control] += value;  // two of one control in a report would add up: none should come twice
    });
    return events;
}

/// One ShuttlePRO report: the shuttle -7..7, the jog counter, the keys K1..K15 as bits 0..14 of bytes 3-4
std::vector<uint8_t> Report(int shuttle, uint8_t jog, uint16_t keys = 0)
{
    return {static_cast<uint8_t>(static_cast<int8_t>(shuttle)), jog, 0x00, static_cast<uint8_t>(keys & 0xFF),
            static_cast<uint8_t>(keys >> 8)};
}

uint16_t Key(int k)
{
    return static_cast<uint16_t>(1u << (k - 1));
}

const DeviceMatch& ShuttlePro()
{
    static const DeviceMatch match = DeviceDatabase::Builtin().Lookup({"usb", 0x0B33, 0x0030, 0x0100, "ShuttlePRO v2", ""});
    return match;
}

bool Has(const std::vector<std::string>& controls, const std::string& control)
{
    return std::find(controls.begin(), controls.end(), control) != controls.end();
}

constexpr const char* kCapture = "controllers/hid-captures/shuttle-pro/macos-usb/2026-10-28-descriptor-and-reports.txt";

std::vector<uint8_t> Bytes(const std::string& hex)
{
    std::vector<uint8_t> out;
    std::istringstream in(hex);
    std::string byte;
    while (in >> byte)
        out.push_back(static_cast<uint8_t>(std::stoul(byte, nullptr, 16)));
    return out;
}

/// hiddump's log: `descriptor dev0 len=N : <bytes>`, `<ms> dev0 id=XX len=N : <bytes>`
void LoadCapture(std::vector<uint8_t>& descriptor, std::vector<std::vector<uint8_t>>& reports)
{
    std::ifstream in(TestPathHelper::GetTestDataPath(kCapture));
    std::string line;
    while (std::getline(in, line))
    {
        const size_t colon = line.find(" : ");
        if (colon == std::string::npos)
            continue;
        if (line.rfind("descriptor", 0) == 0)
            descriptor = Bytes(line.substr(colon + 3));
        else if (line.find(" id=") != std::string::npos)
            reports.push_back(Bytes(line.substr(colon + 3)));
    }
}
}  // namespace

TEST(ShuttlePro_Test, Database_AControlSurfaceFamilyWithItsHidMap)
{
    const DeviceMatch& match = ShuttlePro();
    EXPECT_EQ(match.family, "shuttle-pro-v2");
    EXPECT_FALSE(match.player);
    EXPECT_EQ(match.role, DeviceRole::DebuggerRemote);
    EXPECT_TRUE(match.hid.anyApplication);
    ASSERT_EQ(match.hid.usages.size(), 2u);
    EXPECT_EQ(match.hid.usages[0].page, 0x01);
    EXPECT_EQ(match.hid.usages[0].usage, 0x38);
    EXPECT_EQ(match.hid.usages[0].read, HidRead::Split);
    EXPECT_TRUE(match.hid.usages[0].range);
    EXPECT_EQ(match.hid.usages[0].min, -7);
    EXPECT_EQ(match.hid.usages[0].max, 7);
    EXPECT_EQ(match.hid.usages[1].read, HidRead::Counter);
    EXPECT_FALSE(match.hid.usages[1].range);
    EXPECT_EQ(DeviceDatabase::Builtin().Glyph(match.glyphs, "button.14"), "K14");
    EXPECT_EQ(match.images, "shuttle-pro-v2") << "its own picture (POC 033's generator, devices/contour.py)";
    EXPECT_EQ(DeviceDatabase::Builtin().Lookup({"usb", 0x0B33, 0x0020, -1, "ShuttleXpress", ""}).family,
              "shuttle-xpress");

    // The backends open the surfaces by id; pads are found by their collections, not listed
    const auto surfaces = DeviceDatabase::Builtin().SurfaceProducts();
    auto listed = [&surfaces](uint16_t vendor, uint16_t product) {
        return std::find(surfaces.begin(), surfaces.end(), std::pair{vendor, product}) != surfaces.end();
    };
    EXPECT_TRUE(listed(0x0B33, 0x0030));
    EXPECT_TRUE(listed(0x0B33, 0x0020));
    EXPECT_TRUE(listed(0x045E, 0x091B)) << "the Surface Dial is a surface too";
    EXPECT_FALSE(listed(0x045E, 0x0B13)) << "an Xbox pad is not";
}

TEST(ShuttlePro_Test, Database_MalformedUsagesAreRefused)
{
    auto merge = [](const std::string& hid) {
        DeviceDatabase db;
        std::string error;
        const bool ok = db.Merge(R"({"schema": 1, "families": [{"id": "s", "match": [{"vid": "1234", "pid": ["0001"]}],
            "hid": )" + hid + "}]}",
                                 error);
        EXPECT_EQ(ok, error.empty()) << error;
        return ok;
    };
    EXPECT_TRUE(merge(R"({"application": "any", "usages": [{"usage": "01:37", "control": "jog", "read": "counter"}]})"));
    EXPECT_FALSE(merge(R"({"usages": [{"usage": "01:37", "control": "jog", "read": "spin"}]})")) << "unknown read";
    EXPECT_FALSE(merge(R"({"usages": [{"usage": "0137", "control": "jog", "read": "counter"}]})")) << "no colon";
    EXPECT_FALSE(merge(R"({"usages": [{"usage": "01:37", "read": "counter"}]})")) << "no control";
    EXPECT_FALSE(merge(R"({"usages": [{"usage": "01:38", "control": "s", "read": "split", "min": 7, "max": -7}]})"));
    EXPECT_FALSE(merge(R"({"usages": [{"usage": "01:38", "control": "s", "read": "split", "min": -7}]})"))
        << "a range needs both ends";
    EXPECT_FALSE(merge(R"({"application": "consumer"})"));
    EXPECT_FALSE(merge(R"({"usages": {"01:37": "jog"}})"));
}

TEST(ShuttlePro_Test, HidPad_ItsControlsFromTheRealDescriptor)
{
    const HidDescriptor descriptor = Parse(kShuttleProDescriptor);
    EXPECT_TRUE(HidPad(descriptor, HidLayoutFor("generic")).Empty()) << "a Consumer Control is no pad by itself";

    HidPad pad(descriptor, HidLayoutFor(ShuttlePro()));
    ASSERT_FALSE(pad.Empty());
    const std::vector<std::string> controls = pad.Controls();
    for (const char* control : {"jog", "shuttle.left", "shuttle.right", "button.1", "button.13", "button.14",
                                "button.15"})
        EXPECT_TRUE(Has(controls, control)) << control;
    EXPECT_FALSE(Has(controls, "axis.4")) << "the wheel and the dial are the family's, not raw axes";
    EXPECT_FALSE(Has(controls, "button.0"));
    // Every control the family lists is one it decodes, and every one it decodes has a name in the vocabulary
    for (const std::string& control : ShuttlePro().controls)
        EXPECT_TRUE(Has(controls, control)) << control;
    for (const std::string& control : controls)
    {
        ControlInfo info;
        EXPECT_TRUE(ParseControl(control, info)) << control;
    }
}

TEST(ShuttlePro_Test, HidPad_ShuttleJogAndKeys)
{
    HidPad pad(Parse(kShuttleProDescriptor), HidLayoutFor(ShuttlePro()));
    Events events = Decode(pad, Report(0, 0xFE));
    EXPECT_EQ(events.count("jog"), 0u) << "the first count is the baseline";
    EXPECT_EQ(events["shuttle.left"], 0.0f);
    EXPECT_EQ(events["shuttle.right"], 0.0f);

    // The shuttle: its own -7..7, not the descriptor's -128..127; back to the centre is a zero (hid-input drops it)
    events = Decode(pad, Report(7, 0xFE));
    EXPECT_EQ(events["shuttle.right"], 1.0f);
    EXPECT_EQ(events.count("shuttle.left"), 0u);
    events = Decode(pad, Report(-3, 0xFE));
    EXPECT_EQ(events["shuttle.right"], 0.0f);
    EXPECT_FLOAT_EQ(events["shuttle.left"], 3.0f / 7.0f);
    events = Decode(pad, Report(0, 0xFE));
    EXPECT_EQ(events.count("shuttle.left"), 1u);
    EXPECT_EQ(events["shuttle.left"], 0.0f);
    EXPECT_TRUE(Decode(pad, Report(0, 0xFE)).empty()) << "nothing changed";

    // The jog wheel: the change of its counter, the shorter way round the wrap
    events = Decode(pad, Report(0, 0x01));
    EXPECT_EQ(events["jog"], 3.0f);
    events = Decode(pad, Report(0, 0xFF));
    EXPECT_EQ(events["jog"], -2.0f);

    // K1 and K13 are descriptor buttons, K14 and K15 the bits it leaves out
    events = Decode(pad, Report(0, 0xFF, Key(1) | Key(13) | Key(14)));
    EXPECT_EQ(events["button.1"], 1.0f);
    EXPECT_EQ(events["button.13"], 1.0f);
    EXPECT_EQ(events["button.14"], 1.0f);
    EXPECT_EQ(events.count("button.15"), 0u);
    events = Decode(pad, Report(0, 0xFF, Key(15)));
    EXPECT_EQ(events["button.1"], 0.0f);
    EXPECT_EQ(events["button.14"], 0.0f);
    EXPECT_EQ(events["button.15"], 1.0f);
}

TEST(ShuttlePro_Test, HidPad_AnEncoderCountsEveryReport)
{
    // A relative wheel read as an encoder: the same count twice is two turns, a zero is none
    HidLayout layout = HidLayoutFor("generic");
    layout.anyApplication = true;
    layout.usages = {{0x01, 0x38, "jog", HidRead::Encoder}};
    HidPad pad(Parse(kShuttleProDescriptor), layout);
    EXPECT_EQ(Decode(pad, Report(2, 0))["jog"], 2.0f);
    EXPECT_EQ(Decode(pad, Report(2, 0))["jog"], 2.0f);
    EXPECT_EQ(Decode(pad, Report(-1, 0))["jog"], -1.0f);
    EXPECT_EQ(Decode(pad, Report(0, 0)).count("jog"), 0u);
}

TEST(ShuttlePro_Test, Linux_ReadThroughHidraw)
{
    LinuxPadFacts facts;
    facts.vendor = 0x0B33;
    facts.product = 0x0030;
    facts.family = "shuttle-pro-v2";
    facts.driver = LinuxHidDriver::Generic;
    EXPECT_EQ(HidrawUseFor(facts), HidrawUse::None);
    facts.surface = true;
    EXPECT_EQ(HidrawUseFor(facts), HidrawUse::Input);
    const std::string denied =
        HidrawUnavailable(HidrawUse::Input, "/dev/hidraw4", EACCES, "Permission denied", facts.family, true);
    EXPECT_NE(denied.find("control surface"), std::string::npos) << denied;
    EXPECT_NE(denied.find("udevadm"), std::string::npos) << denied;
}

TEST(ShuttlePro_Test, Vocabulary_JogAndShuttle)
{
    ControlInfo info;
    ASSERT_TRUE(ParseControl("jog", info));
    EXPECT_EQ(info.type, ControlType::Encoder);
    ASSERT_TRUE(ParseControl("shuttle.left", info));
    EXPECT_EQ(info.type, ControlType::Trigger);
    ASSERT_TRUE(ParseControl("shuttle.right", info));
    EXPECT_EQ(info.type, ControlType::Trigger);
}

TEST(ShuttlePro_Test, Profile_ShippedAndRoutedByContext)
{
    ProfileStore store;
    const Profile* profile = store.Find("builtin/debug/shuttle-pro");
    ASSERT_NE(profile, nullptr) << "compiled in";
    Profile again;
    std::string error;
    ASSERT_TRUE(ParseProfile(ProfileStore::Dump(*profile), again, error)) << error;

    const HostDeviceInfo shuttle = {"usb:0b33:0030:0100@1", "ShuttlePRO v2", "shuttle-pro-v2", "generic", "fake"};
    InputRouter router;
    uint64_t now = 1'000'000'000;
    std::vector<ActionEvent> actions;
    router.SetClock([&now] { return now; });
    router.SetActionSink([&actions](const ActionEvent& event) { actions.push_back(event); });
    router.SetDefaultRole(shuttle.id, DeviceRole::DebuggerRemote);
    router.OnDeviceAdded(shuttle);
    router.SetFocus("emu-1");
    ProfileContext context;
    context.debugLayer = false;  // a debugger remote needs no debug layer switch
    context.devices = {shuttle};
    const ResolvedProfiles resolved = store.Apply(router, context);
    ASSERT_TRUE(resolved.warnings.empty()) << resolved.warnings.front();
    auto control = [&](const char* name, float value) {
        now += 25'000'000;
        router.OnControl({shuttle.id, name, value, now});
    };

    // No context (play): the jog wheel steps an instruction forward / back, by frames with K14 held
    control("jog", 1);
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].action, "run.step_by");
    EXPECT_TRUE(actions[0].params.IsNull()) << "instructions";
    actions.clear();
    control("button.14", 1);
    control("jog", -1);
    control("button.14", 0);
    ASSERT_EQ(actions.size(), 1u) << "held while turning: its own action (follow PC) does not fire";
    EXPECT_EQ(actions[0].action, "run.step_by");
    ASSERT_NE(actions[0].params.Find("unit"), nullptr);
    EXPECT_EQ(actions[0].params.Find("unit")->AsString(), "frames");
    actions.clear();
    control("button.7", 1);
    control("button.7", 0);
    ASSERT_FALSE(actions.empty());
    EXPECT_EQ(actions[0].action, "run.pause_toggle");

    actions.clear();
    router.SetContext("emu-1", "ttd");
    control("jog", -2);
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].action, "ttd.seek_by");
    EXPECT_EQ(actions[0].value, -2.0f);
    EXPECT_EQ(actions[0].params.Find("unit")->AsString(), "frames");

    // K14 held while turning: instructions, and its own action (follow PC) does not fire
    actions.clear();
    control("button.14", 1);
    control("jog", 1);
    control("button.14", 0);
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].action, "ttd.jog_instructions");

    // The shuttle: the speed control, right (CW) slower, left (CCW) faster; a level, back to 0 at the centre
    actions.clear();
    control("shuttle.right", 0.5f);
    control("shuttle.right", 0.0f);
    ASSERT_GE(actions.size(), 2u);
    EXPECT_EQ(actions.front().action, "run.pace_shuttle_slower");
    EXPECT_FLOAT_EQ(actions.front().value, 0.5f);
    EXPECT_EQ(actions.back().value, 0.0f);
    actions.clear();
    control("shuttle.left", 0.25f);
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].action, "run.pace_shuttle_faster");
    control("shuttle.left", 0.0f);

    actions.clear();
    router.SetContext("emu-1", "debug.disasm");
    control("jog", 3);
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].action, "run.step_by");
}

TEST(ShuttlePro_Test, Capture_TheOwnersDevice)
{
    std::vector<uint8_t> descriptor;
    std::vector<std::vector<uint8_t>> reports;
    LoadCapture(descriptor, reports);
    ASSERT_EQ(descriptor, kShuttleProDescriptor) << "the descriptor this test file decodes is the device's";
    ASSERT_GT(reports.size(), 50u);

    HidPad pad(Parse(descriptor), HidLayoutFor(ShuttlePro()));
    float jog = 0;
    float left = 0;
    float right = 0;
    float maxLeft = 0;
    float maxRight = 0;
    std::map<std::string, int> presses;
    for (const std::vector<uint8_t>& report : reports)
        pad.Decode(report.data(), report.size(), [&](const std::string& control, float value) {
            if (control == "jog")
                jog += value;
            else if (control == "shuttle.left")
                maxLeft = std::max(maxLeft, left = value);
            else if (control == "shuttle.right")
                maxRight = std::max(maxRight, right = value);
            else if (value > 0)
                ++presses[control];
        });
    // The counter went 0x75 .. 0x82 one detent at a time: the first report is the baseline
    EXPECT_EQ(jog, static_cast<float>(0x82 - 0x75));
    // The ring to -6 and +5, back at rest at the end
    EXPECT_FLOAT_EQ(maxLeft, 6.0f / 7.0f);
    EXPECT_FLOAT_EQ(maxRight, 5.0f / 7.0f);
    EXPECT_EQ(left, 0.0f);
    EXPECT_EQ(right, 0.0f);
    // Every key once, the two beside the jog wheel included (bits the descriptor does not declare)
    for (int k = 1; k <= 15; ++k)
        EXPECT_EQ(presses["button." + std::to_string(k)], 1) << k;
    EXPECT_EQ(presses.size(), 15u);
}
