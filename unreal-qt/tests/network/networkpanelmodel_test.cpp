// The Network window's Qt-free model: the state report into the form, the
// form back into settings, and what the machine allows

#include <gtest/gtest.h>

#include "emulator/io/serial/uart16550.h"
#include "network/core/networkpanelmodel.h"

namespace
{
StateNode State(const char* serialPort, bool zxBus, const char* card, const char* comPort, const char* zxWifi)
{
    StateNode net = StateNode::Object();
    net["machine"]["zx_bus"] = zxBus;
    net["machine"]["serial_port"] = serialPort;
    StateNode& set = net["settings"];
    set["card"] = card;
    set["com_port"] = comPort;
    set["zx_wifi"] = zxWifi;
    set["esp_chip"] = "ESP32";
    set["com_modem_lines"] = false;
    set["avr_firmware"] = "BASE2023";
    set["host_access"] = true;
    set["dns_mode"] = "HOST";
    set["hosts"] = "";
    set["forwards"] = "";
    set["connect_timeout_ms"] = 10000u;
    return net;
}
}  // namespace

TEST(NetworkPanelModel_Test, TheFormFollowsTheState)
{
    const NetworkForm f = NetworkFormFromState(State("evo-avr", true, "ZXNETUSB", "TCP:bbs.example.org:23", "AT"));
    EXPECT_EQ(f.serialPort, "evo-avr");
    EXPECT_TRUE(f.zxNetUsb);
    EXPECT_FALSE(f.zxWifi);
    EXPECT_EQ(f.comPort.kind, ComPortSpec::Kind::Tcp);
    EXPECT_EQ(f.comPort.host, "bbs.example.org");
    EXPECT_EQ(f.comPort.port, 23);
    EXPECT_EQ(f.zxWifiPeer.kind, ComPortSpec::Kind::At);
    EXPECT_EQ(f.connectTimeoutMs, 10000u);
}

TEST(NetworkPanelModel_Test, OnlyTheChangedSettingsAreSent)
{
    const NetworkForm before = NetworkFormFromState(State("none", true, "NONE", "NONE", "AT"));
    EXPECT_TRUE(NetworkFormChanges(before, before).empty());

    NetworkForm after = before;
    after.zxNetUsb = true;
    after.zxWifi = true;
    after.zxWifiPeer.kind = ComPortSpec::Kind::Serial;
    after.zxWifiPeer.device = "/dev/cu.usbserial-0001";
    after.zxWifiPeer.baud = 115200;
    after.espChip = "ESP8266";
    after.hostAccess = false;
    after.connectTimeoutMs = 5000;
    const auto changes = NetworkFormChanges(before, after);
    const std::vector<std::pair<std::string, std::string>> expected = {
        {"card", "zxnetusb,zxwifi"},
        {"zx_wifi", "SERIAL:/dev/cu.usbserial-0001,115200"},
        {"esp_chip", "esp8266"},
        {"host_access", "off"},
        {"connect_timeout_ms", "5000"},
    };
    EXPECT_EQ(changes, expected);
}

TEST(NetworkPanelModel_Test, TheZxEvoOffersItsOwnPortAndTheAvr)
{
    const NetworkAvailability a = NetworkFormAvailability(NetworkFormFromState(State("evo-avr", true, "NONE", "NONE", "AT")));
    EXPECT_TRUE(a.cards);
    EXPECT_TRUE(a.comPort);
    EXPECT_TRUE(a.avrFirmware);
    EXPECT_FALSE(a.zxWifi) << "#xxEF is the AVR's";
    EXPECT_NE(a.zxWifiWhy.find("AVR"), std::string::npos);
}

TEST(NetworkPanelModel_Test, APentagonTakesCardsOnly)
{
    const NetworkAvailability a = NetworkFormAvailability(NetworkFormFromState(State("none", true, "NONE", "NONE", "AT")));
    EXPECT_TRUE(a.cards);
    EXPECT_TRUE(a.zxWifi);
    EXPECT_FALSE(a.comPort);
    EXPECT_NE(a.comPortWhy.find("ZX-WiFi"), std::string::npos);
    EXPECT_FALSE(a.avrFirmware);
}

TEST(NetworkPanelModel_Test, TsConfKeepsItsZiFi)
{
    const NetworkAvailability a = NetworkFormAvailability(NetworkFormFromState(State("zifi", true, "NONE", "NONE", "AT")));
    EXPECT_TRUE(a.cards);
    EXPECT_FALSE(a.zxWifi);
    EXPECT_FALSE(a.comPort);
    EXPECT_NE(a.comPortWhy.find("ZiFi"), std::string::npos);
}

TEST(NetworkPanelModel_Test, NoZxBusNoCards)
{
    const NetworkAvailability a = NetworkFormAvailability(NetworkFormFromState(State("none", false, "NONE", "NONE", "AT")));
    EXPECT_FALSE(a.cards);
    EXPECT_FALSE(a.zxWifi);
}

TEST(NetworkPanelModel_Test, EveryAvrPresetParses)
{
    const auto choices = NetworkAvrFirmwareChoices();
    ASSERT_EQ(choices.size(), 9u);
    EXPECT_EQ(choices.front().first, "BASE2010");
    EXPECT_EQ(choices.back().first, "TS2016-04");
    for (const auto& [name, text] : choices)
    {
        Uart16550::AvrFirmware f;
        EXPECT_TRUE(Uart16550::ParseAvrFirmware(name.c_str(), f)) << name;
        EXPECT_FALSE(text.empty());
    }
}
