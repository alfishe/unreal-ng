/// @file protocolpads_test.cpp
/// @brief ProtocolPads, the OS-neutral part of a HID backend for the pads with a protocol of their own (the Windows Raw
/// Input backend's): devices and ids as the sink sees them, the PlayStation reports as Windows hands them (padded to the
/// collection's longest input report), a Joy-Con pair and the charging grip's halves with a fake Switch device that
/// answers the setup, the pair's rumble split with its HD frequencies, the player numbers.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "_helpers/testwaithelper.h"
#include "hid/drivers/dualsenseoutput.h"
#include "protocolpads.h"

using namespace controllers;

namespace
{
/// The sink: the device events in order ("+id", "-id") and the last value of each control per id
class Sink : public IBackendSink
{
public:
    void OnDeviceAdded(const HostDeviceInfo& device) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back("+" + device.id);
        names[device.id] = device.name;
    }
    void OnDeviceRemoved(const HostDeviceId& device) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back("-" + device);
    }
    void OnControl(const ControlEvent& event) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        controls[event.device][event.control] = event.value;
    }
    std::vector<std::string> Events()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return std::exchange(events, {});
    }
    std::map<std::string, float> Controls(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return controls[id];
    }

    std::mutex mutex;
    std::vector<std::string> events;
    std::map<std::string, std::string> names;
    std::map<std::string, std::map<std::string, float>> controls;
};

ProtocolPads::Options Fast()
{
    ProtocolPads::Options options;
    options.switchStartDelayMs = 0;
    return options;
}

ProtocolPads::Endpoint MakeEndpoint(const std::string& key, const std::string& id, uint16_t product, bool bluetooth)
{
    ProtocolPads::Endpoint endpoint;
    endpoint.key = key;
    endpoint.info.id = id;
    endpoint.info.name = "pad " + key;
    endpoint.info.backend = "rawinput";
    endpoint.vendor = product == 0x0CE6 || product == 0x05C4 ? 0x054C : 0x057E;
    endpoint.product = product;
    endpoint.bluetooth = bluetooth;
    return endpoint;
}

void PutStick(uint8_t* at, int x, int y)
{
    at[0] = static_cast<uint8_t>(x & 0xFF);
    at[1] = static_cast<uint8_t>(((x >> 8) & 0x0F) | ((y & 0x0F) << 4));
    at[2] = static_cast<uint8_t>(y >> 4);
}

/// A Switch report (0x30, or a 0x21 reply) with both sticks at rest and a full battery
std::vector<uint8_t> SwitchReport(uint8_t id)
{
    std::vector<uint8_t> report(64, 0);
    report[0] = id;
    report[2] = 0x80;
    PutStick(report.data() + 6, 2048, 2048);
    PutStick(report.data() + 9, 2048, 2048);
    return report;
}

/// A Switch pad that answers its setup as SDL expects (SwitchProtocol): the USB commands with 0x81, the device info
/// with its type, an SPI read with its address and unset flash, vibration and the input mode with an acknowledgement.
/// The answers go back through ProtocolPads::Input, as the backend's reading thread passes them
class FakeSwitch
{
public:
    FakeSwitch(ProtocolPads& pads, std::string key, uint8_t type = 0) : _pads(pads), _key(std::move(key)), _type(type)
    {
    }

    void Give(ProtocolPads::Endpoint& endpoint)
    {
        endpoint.write = [this](const std::vector<uint8_t>& report) { return Write(report); };
        endpoint.getFeature = [](uint8_t, size_t, std::vector<uint8_t>&) { return false; };
    }

    /// The rumble reports written (0x10), their 8 rumble bytes
    std::vector<std::array<uint8_t, 8>> Rumbles()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::vector<std::array<uint8_t, 8>> out;
        for (const auto& report : _writes)
            if (report[0] == 0x10)
            {
                std::array<uint8_t, 8> rumble{};
                std::copy(report.begin() + 2, report.begin() + 10, rumble.begin());
                out.push_back(rumble);
            }
        return out;
    }
    bool HasRumble(const std::array<uint8_t, 8>& rumble)
    {
        const auto all = Rumbles();
        return std::find(all.begin(), all.end(), rumble) != all.end();
    }

    std::atomic<bool> setUp{false};
    /// The device answers nothing (its writes wait) until this is cleared
    std::atomic<bool> hold{false};

private:
    bool Write(const std::vector<uint8_t>& report)
    {
        if (!TestWait::For([this] { return !hold.load(); }))
            return false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _writes.push_back(report);
        }
        std::vector<uint8_t> reply;
        if (report[0] == 0x80 && report[1] != 0x04)
            reply = {0x81, report[1]};
        else if (report[0] == 0x01)
        {
            const uint8_t id = report[10];
            reply = SwitchReport(0x21);
            reply[13] = 0x80;
            reply[14] = id;
            if (id == 0x02)
            {
                reply[15] = 0x04;
                reply[16] = 0x21;
                reply[17] = _type;
            }
            else if (id == 0x10)
            {
                std::copy(report.begin() + 11, report.begin() + 16, reply.begin() + 15);
                std::fill(reply.begin() + 20, reply.begin() + 20 + report[15], uint8_t{0xFF});
            }
            else if (id != 0x48 && id != 0x03)
                reply.clear();  // the player lights: no answer needed
        }
        if (!reply.empty())
            _pads.Input(_key, reply.data(), reply.size());
        if (report[0] == 0x01 && report[10] == 0x03)
            setUp = true;
        return true;
    }

    ProtocolPads& _pads;
    std::string _key;
    uint8_t _type;
    std::mutex _mutex;
    std::vector<std::vector<uint8_t>> _writes;
};

/// Declared after the fakes: the pads go (their pumps' final writes) before the fakes do, even when an ASSERT returns
struct ClearOnExit
{
    ProtocolPads& pads;
    ~ClearOnExit()
    {
        pads.Clear();
    }
};

void PutCrc(std::vector<uint8_t>& report, size_t size, uint8_t seed)
{
    const uint32_t crc = PlayStationCrc32(seed, report.data(), size - 4);
    for (size_t index = 0; index < 4; index++)
        report[size - 4 + index] = static_cast<uint8_t>(crc >> (8 * index));
}
}  // namespace

TEST(ProtocolPads_Test, HandlesAndReportSizes)
{
    EXPECT_TRUE(ProtocolPads::Handles(0x054C, 0x0CE6));
    EXPECT_TRUE(ProtocolPads::Handles(0x054C, 0x0DF2));
    EXPECT_TRUE(ProtocolPads::Handles(0x054C, 0x09CC));
    EXPECT_TRUE(ProtocolPads::Handles(0x057E, 0x2009));
    EXPECT_TRUE(ProtocolPads::Handles(0x057E, 0x2006));
    EXPECT_TRUE(ProtocolPads::Handles(0x057E, 0x200E)) << "the charging grip";
    EXPECT_FALSE(ProtocolPads::Handles(0x045E, 0x0B12));

    std::vector<uint8_t> report(547, 0);
    report[0] = 0x31;
    EXPECT_EQ(ProtocolPads::ReportSize(0x054C, 0x0CE6, true, report.data(), report.size()), 78u);
    EXPECT_EQ(ProtocolPads::ReportSize(0x054C, 0x09CC, true, report.data(), report.size()), 547u)
        << "a DualShock 4's full report is 0x11";
    report[0] = 0x11;
    EXPECT_EQ(ProtocolPads::ReportSize(0x054C, 0x09CC, true, report.data(), report.size()), 78u);
    report[0] = 0x01;
    EXPECT_EQ(ProtocolPads::ReportSize(0x054C, 0x0CE6, true, report.data(), report.size()), 547u)
        << "the Bluetooth short report stays HidPad's, whatever its padding";
    EXPECT_EQ(ProtocolPads::ReportSize(0x054C, 0x0CE6, false, report.data(), 64), 64u);
    EXPECT_EQ(ProtocolPads::ReportSize(0x057E, 0x2009, true, report.data(), 362), 362u) << "Switch reports by >=";
}

TEST(ProtocolPads_Test, DualSenseOverBluetoothAsWindowsHandsItsReports)
{
    Sink sink;
    std::mutex mutex;
    std::vector<std::vector<uint8_t>> writes;
    std::atomic<int> features{0};
    {
        ProtocolPads pads(sink, Fast());
        ProtocolPads::Endpoint endpoint = MakeEndpoint("1", "bt:054c:0ce6:aaaa", 0x0CE6, true);
        endpoint.write = [&](const std::vector<uint8_t>& report) {
            std::lock_guard<std::mutex> lock(mutex);
            writes.push_back(report);
            return true;
        };
        endpoint.getFeature = [&](uint8_t id, size_t size, std::vector<uint8_t>& data) {
            data.assign(size, 0);
            data[0] = id;
            PutCrc(data, size, SonyPad::kFeatureCrcSeed);
            ++features;
            return true;
        };
        ASSERT_TRUE(pads.Add(std::move(endpoint)));
        EXPECT_EQ(sink.Events(), (std::vector<std::string>{"+bt:054c:0ce6:aaaa"}));
        EXPECT_EQ(pads.DeviceCount(), 1u);
        EXPECT_EQ(pads.Player("bt:054c:0ce6:aaaa"), 1);
        EXPECT_EQ(pads.RumbleActuators("bt:054c:0ce6:aaaa"),
                  kRumbleLow | kRumbleHigh | kRumbleLeftTrigger | kRumbleRightTrigger);
        EXPECT_TRUE(pads.HasLight("bt:054c:0ce6:aaaa"));
        EXPECT_FALSE(pads.Battery("bt:054c:0ce6:aaaa").known);

        // The calibration read first (it switches the pad to its full report), then the light with its CRC
        ASSERT_TRUE(TestWait::For([&] { return features.load() >= 1; }));
        ASSERT_TRUE(pads.SetLight("bt:054c:0ce6:aaaa", {10, 20, 30}));
        ASSERT_TRUE(TestWait::For([&] {
            std::lock_guard<std::mutex> lock(mutex);
            return std::any_of(writes.begin(), writes.end(),
                               [](const std::vector<uint8_t>& report) { return report[3 + 44] == 10; });
        }));
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (const auto& report : writes)
            {
                ASSERT_EQ(report.size(), 78u);
                EXPECT_EQ(report[0], 0x31);
                EXPECT_TRUE(PlayStationCrcValid(0xA2, report.data(), report.size()));
            }
        }

        // The full report 0x31 as Raw Input passes it: padded to 547 bytes, the CRC over its own 78
        std::vector<uint8_t> report(547, 0);
        report[0] = 0x31;
        report[1] = 0x10;
        uint8_t* common = report.data() + 2;
        common[0] = common[1] = common[2] = common[3] = 0x80;
        common[7] = 0x08 | 0x20;  // hat none, cross
        common[32] = common[36] = 0x80;
        common[52] = 0x15;  // charging, 50-59 %
        PutCrc(report, 78, SonyPad::kInputCrcSeed);
        pads.Input("1", report.data(), report.size());
        EXPECT_FLOAT_EQ(sink.Controls("bt:054c:0ce6:aaaa").at("south"), 1.0f);
        const BatteryState battery = pads.Battery("bt:054c:0ce6:aaaa");
        EXPECT_TRUE(battery.known);
        EXPECT_TRUE(battery.charging);

        pads.Remove("1");
        EXPECT_EQ(sink.Events(), (std::vector<std::string>{"-bt:054c:0ce6:aaaa"}));
        EXPECT_FALSE(pads.Has("bt:054c:0ce6:aaaa"));
        EXPECT_FALSE(pads.SetRumble("bt:054c:0ce6:aaaa", {}));
    }
}

TEST(ProtocolPads_Test, PlayersTakeTheLowestFreeNumber)
{
    Sink sink;
    ProtocolPads pads(sink, Fast());
    auto add = [&](const std::string& key) {
        ProtocolPads::Endpoint endpoint = MakeEndpoint(key, "usb:054c:0ce6:" + key, 0x0CE6, false);
        endpoint.write = [](const std::vector<uint8_t>&) { return true; };
        endpoint.getFeature = [](uint8_t, size_t, std::vector<uint8_t>&) { return false; };
        ASSERT_TRUE(pads.Add(std::move(endpoint)));
    };
    add("a");
    add("b");
    EXPECT_EQ(pads.Player("usb:054c:0ce6:a"), 1);
    EXPECT_EQ(pads.Player("usb:054c:0ce6:b"), 2);
    pads.Remove("a");
    add("c");
    EXPECT_EQ(pads.Player("usb:054c:0ce6:c"), 1);
    EXPECT_TRUE(pads.IdTaken("usb:054c:0ce6:b"));
    EXPECT_FALSE(pads.IdTaken("usb:054c:0ce6:a"));
    pads.Clear();
    std::vector<std::string> events = sink.Events();
    std::sort(events.begin() + 3, events.end());
    EXPECT_EQ(events, (std::vector<std::string>{"+usb:054c:0ce6:a", "+usb:054c:0ce6:b", "-usb:054c:0ce6:a",
                                                "+usb:054c:0ce6:c", "-usb:054c:0ce6:b", "-usb:054c:0ce6:c"}));
    EXPECT_EQ(pads.DeviceCount(), 0u);
}

TEST(ProtocolPads_Test, NoWritesNoOutputsAndNoGrip)
{
    Sink sink;
    ProtocolPads pads(sink, Fast());
    ProtocolPads::Endpoint endpoint = MakeEndpoint("1", "usb:054c:05c4:x", 0x05C4, false);
    endpoint.outputsUnavailable = "Windows refused to open the pad for writing";
    ASSERT_TRUE(pads.Add(std::move(endpoint)));
    EXPECT_TRUE(pads.Has("usb:054c:05c4:x"));
    EXPECT_EQ(pads.RumbleActuators("usb:054c:05c4:x"), 0);
    EXPECT_EQ(pads.RumbleUnavailable("usb:054c:05c4:x"), "Windows refused to open the pad for writing");
    EXPECT_FALSE(pads.SetRumble("usb:054c:05c4:x", {1, 1, 0, 0}));
    EXPECT_FALSE(pads.HasLight("usb:054c:05c4:x"));
    EXPECT_EQ(pads.Player("usb:054c:05c4:x"), 0);

    // A grip half says its side only through the setup's device info
    EXPECT_FALSE(pads.Add(MakeEndpoint("2", "usb:057e:200e:y", 0x200E, false)));
    EXPECT_EQ(sink.Events(), (std::vector<std::string>{"+usb:054c:05c4:x"}));
    EXPECT_FALSE(ProtocolPads(sink).Add(MakeEndpoint("3", "usb:045e:0b12:z", 0x0B12, false)));
}

TEST(ProtocolPads_Test, JoyConsPairAndRumbleWithTheirSidesHdFrequencies)
{
    Sink sink;
    ProtocolPads pads(sink, Fast());
    FakeSwitch left(pads, "L");
    FakeSwitch right(pads, "R");
    ClearOnExit clearOnExit{pads};
    ProtocolPads::Endpoint first = MakeEndpoint("L", "bt:057e:2006:l", 0x2006, true);
    left.Give(first);
    ASSERT_TRUE(pads.Add(std::move(first)));
    EXPECT_EQ(sink.Events(), (std::vector<std::string>{"+bt:057e:2006:l"}));
    ASSERT_TRUE(TestWait::For([&] { return left.setUp.load(); }));

    ProtocolPads::Endpoint second = MakeEndpoint("R", "bt:057e:2007:r", 0x2007, true);
    right.Give(second);
    ASSERT_TRUE(pads.Add(std::move(second)));
    const std::string pair = "bt:057e:2006:l+bt:057e:2007:r";
    EXPECT_EQ(sink.Events(), (std::vector<std::string>{"-bt:057e:2006:l", "+" + pair}));
    EXPECT_EQ(sink.names[pair], "Joy-Con (L/R)");
    EXPECT_EQ(pads.DeviceCount(), 1u);
    EXPECT_TRUE(pads.Has(pair));
    EXPECT_FALSE(pads.Has("bt:057e:2006:l"));
    EXPECT_EQ(pads.Player(pair), 1);
    ASSERT_TRUE(TestWait::For([&] { return right.setUp.load(); }));

    // The left half's buttons go out under the pair's id, as the Pro layout's left side (its setup's replies, 0x21
    // reports with the sticks, went out under its own id while it was alone)
    {
        std::lock_guard<std::mutex> lock(sink.mutex);
        sink.controls.clear();
    }
    std::vector<uint8_t> report = SwitchReport(0x30);
    report[5] = 0x02;  // up
    pads.Input("L", report.data(), report.size());
    EXPECT_FLOAT_EQ(sink.Controls(pair).at("dpad.up"), 1.0f);
    EXPECT_TRUE(sink.Controls("bt:057e:2006:l").empty());
    EXPECT_TRUE(pads.Battery(pair).known);

    // The rumble: the left half the low motor at its frequency, the right half the high one at its own
    RumbleLevels levels;
    levels.low = 0.5f;
    levels.high = 0.25f;
    levels.lowHz = 80;
    levels.highHz = 640;
    ASSERT_TRUE(pads.SetRumble(pair, levels));
    EXPECT_EQ(pads.RumbleActuators(pair), kRumbleLow | kRumbleHigh);
    const std::array<uint8_t, 8> leftBytes = SwitchRumble::Pair(SwitchProPad::HalfRumble(levels, true));
    const std::array<uint8_t, 8> rightBytes = SwitchRumble::Pair(SwitchProPad::HalfRumble(levels, false));
    EXPECT_NE(leftBytes, SwitchRumble::Pair({0.5f, 0, 0, 0, 0, 0})) << "the frequency is in the bytes";
    EXPECT_TRUE(TestWait::For([&] { return left.HasRumble(leftBytes); }));
    EXPECT_TRUE(TestWait::For([&] { return right.HasRumble(rightBytes); }));
    EXPECT_FALSE(pads.HasLight(pair));
    EXPECT_FALSE(pads.LightUnavailable(pair).empty());

    // The right one goes: the pair with it, the left one alone again
    pads.Remove("R");
    EXPECT_EQ(sink.Events(), (std::vector<std::string>{"-" + pair, "+bt:057e:2006:l"}));
    EXPECT_EQ(pads.DeviceCount(), 1u);
    pads.Input("L", report.data(), report.size());
    EXPECT_FALSE(sink.Controls("bt:057e:2006:l").empty());
}

TEST(ProtocolPads_Test, ChargingGripHalvesBecomeAPairByTheirDeviceInfo)
{
    Sink sink;
    ProtocolPads pads(sink, Fast());
    FakeSwitch left(pads, "1", SwitchProtocol::kTypeJoyConLeft);
    FakeSwitch right(pads, "2", SwitchProtocol::kTypeJoyConRight);
    ClearOnExit clearOnExit{pads};
    ProtocolPads::Endpoint first = MakeEndpoint("1", "usb:057e:200e:0100:g", 0x200E, false);
    left.Give(first);
    left.hold = true;
    ASSERT_TRUE(pads.Add(std::move(first)));
    EXPECT_TRUE(sink.Events().empty()) << "hidden until it says its side";
    EXPECT_EQ(pads.DeviceCount(), 0u);
    std::vector<uint8_t> report = SwitchReport(0x30);
    report[3] = 0x08;  // A
    pads.Input("1", report.data(), report.size());
    EXPECT_TRUE(sink.controls.empty()) << "its input is dropped while its side is unknown";
    left.hold = false;
    ASSERT_TRUE(TestWait::For([&] { return left.setUp.load(); }));
    EXPECT_EQ(sink.Events(), (std::vector<std::string>{"+usb:057e:200e:0100:g"}));
    EXPECT_EQ(sink.names["usb:057e:200e:0100:g"], "Joy-Con (L)");

    ProtocolPads::Endpoint second = MakeEndpoint("2", "usb:057e:200e:0100:g#2", 0x200E, false);
    right.Give(second);
    ASSERT_TRUE(pads.Add(std::move(second)));
    ASSERT_TRUE(TestWait::For([&] { return right.setUp.load(); }));
    const std::string pair = "usb:057e:200e:0100:g+usb:057e:200e:0100:g#2";
    EXPECT_EQ(sink.Events(), (std::vector<std::string>{"-usb:057e:200e:0100:g", "+" + pair}));
    EXPECT_EQ(pads.DeviceCount(), 1u);

    pads.Clear();
    EXPECT_EQ(sink.Events(), (std::vector<std::string>{"-" + pair}));
}
