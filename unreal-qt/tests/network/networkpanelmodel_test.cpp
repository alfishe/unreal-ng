// The Network window's Qt-free model: the state report into the form, the
// form back into settings, and what the machine allows

#include <gtest/gtest.h>

#include "emulator/io/keyboard/atm2kbc.h"
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

TEST(NetworkPanelModel_Test, TheAtm2OffersItsControllersPort)
{
    StateNode state = State("atm2-kbc", true, "NONE", "LOOPBACK", "AT");
    state["settings"]["kbc_firmware"] = "V41";
    NetworkForm form = NetworkFormFromState(state);
    EXPECT_EQ(form.kbcFirmware, "V41");
    NetworkAvailability a = NetworkFormAvailability(form);
    EXPECT_TRUE(a.comPort) << a.comPortWhy;
    EXPECT_TRUE(a.kbcFirmware);
    EXPECT_TRUE(a.zxWifi) << "the controller is not on #xxEF: a ZX-WiFi card fits beside it";
    EXPECT_FALSE(a.avrFirmware);

    // A firmware without RS-232 chosen in the window: the port goes grey with the reason
    NetworkForm edited = form;
    edited.kbcFirmware = "V22-11";
    a = NetworkFormAvailability(edited);
    EXPECT_FALSE(a.comPort);
    EXPECT_NE(a.comPortWhy.find("RS-232"), std::string::npos) << a.comPortWhy;
    const auto changes = NetworkFormChanges(form, edited);
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].first, "kbc_firmware");
    EXPECT_EQ(changes[0].second, "V22-11");
}

TEST(NetworkPanelModel_Test, EveryKbcPresetParses)
{
    const auto choices = NetworkKbcFirmwareChoices();
    ASSERT_EQ(choices.size(), 10u);
    for (const auto& [name, text] : choices)
    {
        Atm2Kbc::Firmware firmware = Atm2Kbc::Firmware::V41;
        EXPECT_TRUE(Atm2Kbc::ParseFirmware(name.c_str(), firmware)) << name;
        EXPECT_FALSE(text.empty()) << name;
    }
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

TEST(NetworkPanelModel_Test, AnEspModuleKeepsItsRate)
{
    const NetworkForm form = NetworkFormFromState(State("atm2-kbc", true, "NONE", "ESPNET,38400", "AT"));
    EXPECT_EQ(form.comPort.kind, ComPortSpec::Kind::Espnet);
    EXPECT_EQ(form.comPort.baud, 38400u);

    NetworkForm edited = form;
    edited.comPort.baud = 0;   // back to the port's default
    const auto changes = NetworkFormChanges(form, edited);
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].first, "com_port");
    EXPECT_EQ(changes[0].second, "ESPNET");
}

TEST(NetworkPanelModel_Test, TheAtm2IoEspCardNeedsTheInternalIoConnector)
{
    StateNode state = State("atm2-kbc", true, "ATM2IOESP", "NONE", "AT");
    state["machine"]["internal_io"] = true;
    state["settings"]["atm2ioesp"] = "ESPNET";
    state["settings"]["atm2ioesp_address"] = "0xF8";
    NetworkForm form = NetworkFormFromState(state);
    EXPECT_TRUE(form.atm2IoEsp);
    EXPECT_EQ(form.atm2IoEspPeer.kind, ComPortSpec::Kind::Espnet);
    EXPECT_EQ(form.atm2IoEspAddress, 0xF8u);
    EXPECT_TRUE(NetworkFormAvailability(form).atm2IoEsp);

    NetworkForm edited = form;
    edited.atm2IoEspAddress = 0xF0;
    edited.zxWifi = true;
    const auto changes = NetworkFormChanges(form, edited);
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].first, "card");
    EXPECT_EQ(changes[0].second, "zxwifi,atm2ioesp");
    EXPECT_EQ(changes[1].first, "atm2ioesp_address");
    EXPECT_EQ(changes[1].second, "0xF0");

    form.internalIo = false;
    EXPECT_FALSE(NetworkFormAvailability(form).atm2IoEsp);
}
