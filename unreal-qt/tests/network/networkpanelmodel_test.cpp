// The Network window's Qt-free model: the state report into the form, the
// form back into settings, and what the machine allows

#include <gtest/gtest.h>

#include <algorithm>

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

TEST(NetworkPanelModel_Test, TheProfiOffersIts8251)
{
    const NetworkAvailability a = NetworkFormAvailability(NetworkFormFromState(State("profi-8251", true, "NONE", "PLUG", "AT")));
    EXPECT_TRUE(a.comPort) << a.comPortWhy;
    EXPECT_TRUE(a.zxWifi) << "the 8251 is not on #xxEF";
    EXPECT_FALSE(a.avrFirmware);
    EXPECT_FALSE(a.kbcFirmware);
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

TEST(NetworkPanelModel_Test, TsConfHasItsComPortAndZiFi)
{
    StateNode state = State("zifi", true, "NONE", "NONE", "AT");
    state["machine"]["zifi"] = true;
    state["settings"]["zifi"] = "AT";
    const NetworkForm form = NetworkFormFromState(state);
    EXPECT_TRUE(form.zifiMachine);
    EXPECT_EQ(form.zifiPeer.kind, ComPortSpec::Kind::At);
    const NetworkAvailability a = NetworkFormAvailability(form);
    EXPECT_TRUE(a.cards);
    EXPECT_FALSE(a.zxWifi) << "#xxEF is the AVR's";
    EXPECT_TRUE(a.comPort) << "the TS AVR's 16550";
    EXPECT_TRUE(a.zifi);
    EXPECT_FALSE(a.avrFirmware);

    NetworkForm after = form;
    after.zifiPeer.kind = ComPortSpec::Kind::Loopback;
    const auto changes = NetworkFormChanges(form, after);
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].first, "zifi");
    EXPECT_EQ(changes[0].second, "LOOPBACK");
}

TEST(NetworkPanelModel_Test, ZiFiOnlyWithTheTsFirmware)
{
    const NetworkAvailability a = NetworkFormAvailability(NetworkFormFromState(State("evo-avr", true, "NONE", "NONE", "AT")));
    EXPECT_FALSE(a.zifi);
    EXPECT_NE(a.zifiWhy.find("TS2016"), std::string::npos);
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

// Expansion slots (the Sprinter's ISA slots): one line per slot - what is plugged and what it uses, or why not
TEST(NetworkPanelModel_Test, SlotRowsSayWhatIsPluggedAndWhatItUses)
{
    StateNode network = StateNode::Object();
    StateNode slots = StateNode::Array();
    StateNode empty = StateNode::Object();
    empty["id"] = "isa1";
    empty["label"] = "ISA slot 1 (J6), page #D4";
    empty["configured"] = "zxbus";
    empty["card"] = "none";
    empty["not_fitted"] = "not built yet";
    slots.push(empty);
    StateNode ne = StateNode::Object();
    ne["id"] = "isa2";
    ne["label"] = "ISA slot 2 (J7), page #D6";
    ne["configured"] = "ne2000";
    ne["card"] = "ne2000";
    ne["chip"] = "RTL8019AS";
    ne["base"] = "#300";
    ne["irq"] = 3;
    ne["mac"] = "02:53:50:00:00:02";
    ne["link"] = "ethernet-gateway";
    slots.push(ne);
    network["slots"] = slots;

    const std::vector<NetworkSlotRow> rows = NetworkSlotRows(network);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].line, "zxbus not fitted: not built yet");
    EXPECT_EQ(rows[1].line, "NE2000 RTL8019AS, I/O #300-#31F, IRQ 3, MAC 02:53:50:00:00:02, cable: ethernet-gateway");
    EXPECT_TRUE(NetworkSlotRows(StateNode::Object()).empty()) << "machines without slots show no group";

    // ISA I2: slot 1 holds the ZX-bus adapter (not a network card): with the ISA report the row is the adapter's line
    StateNode isa = StateNode::Object();
    StateNode isaSlots = StateNode::Array();
    StateNode adapter = StateNode::Object();
    adapter["card"] = "zxbus";
    adapter["summary_line"] = "zxbus I/O #033-#0BB -> NeoGS on the ZX-bus: #B3 / #BB / #33, RESET from ISA RESET DRV";
    isaSlots.push(adapter);
    StateNode isaNe = StateNode::Object();
    isaNe["card"] = "ne2000";
    isaSlots.push(isaNe);
    isa["slots"] = isaSlots;
    const std::vector<NetworkSlotRow> withIsa = NetworkSlotRows(network, &isa);
    ASSERT_EQ(withIsa.size(), 2u);
    EXPECT_EQ(withIsa[0].line, "zxbus I/O #033-#0BB -> NeoGS on the ZX-bus: #B3 / #BB / #33, RESET from ISA RESET DRV");
    EXPECT_EQ(withIsa[1].line.rfind("NE2000 RTL8019AS", 0), 0u);
}

// The SprinterESP in a slot: its UART line and the ESP's session in the row, its line editable (isaN_peer)
TEST(NetworkPanelModel_Test, SprinterEspRowAndItsLine)
{
    StateNode network = StateNode::Object();
    StateNode slots = StateNode::Array();
    StateNode esp = StateNode::Object();
    esp["id"] = "isa1";
    esp["label"] = "ISA slot 1 (J6), page #D4";
    esp["configured"] = "sprinteresp";
    esp["card"] = "sprinteresp";
    esp["chip"] = "TL16C550C";
    esp["base"] = "#3E8";
    esp["irq"] = 3;
    esp["peer_spec"] = "AT";
    StateNode uart = StateNode::Object();
    uart["baud"] = 115200;
    uart["mcr"] = "#22";
    esp["uart"] = uart;
    StateNode module = StateNode::Object();
    module["firmware"] = "ESP8266-AT222";
    module["state"] = "running";
    module["wifi"] = "got_ip";
    module["ip"] = "10.0.2.15";
    module["mac"] = "5C:CF:7F:5A:00:01";
    StateNode session = StateNode::Object();
    session["links"] = StateNode::Array();
    module["at_session"] = session;
    esp["esp"] = module;
    slots.push(esp);
    network["slots"] = slots;

    const std::vector<NetworkSlotRow> rows = NetworkSlotRows(network);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].line, "SPRINTERESP TL16C550C, I/O #3E8-#3EF, IRQ 3, UART 115200 baud, MCR #22; ESP ESP8266-AT222 "
                            "(running), Wi-Fi got_ip 10.0.2.15, 0 link(s), MAC 5C:CF:7F:5A:00:01");

    // With the ISA report the row ends with the slot's IRQ line (ISA I4): level, PIO port B bit, whether it interrupts
    StateNode isa = StateNode::Object();
    StateNode isaSlots = StateNode::Array();
    StateNode isaSlot = StateNode::Object();
    StateNode irqLine = StateNode::Object();
    irqLine["line"] = "low";
    irqLine["pio_bit"] = "PB0";
    irqLine["reaches_cpu"] = "yes: the line going high makes the PIO request IM 2 vector #00 ...";
    isaSlot["irq_line"] = irqLine;
    StateNode counters = StateNode::Object();
    counters["irq_acknowledged"] = static_cast<uint64_t>(6);
    isaSlot["counters"] = counters;
    isaSlots.push(isaSlot);
    isa["slots"] = isaSlots;
    const std::vector<NetworkSlotRow> withIrq = NetworkSlotRows(network, &isa);
    ASSERT_EQ(withIrq.size(), 1u);
    EXPECT_EQ(withIrq[0].line, rows[0].line + "; IRQ line low -> PB0, interrupts the CPU, 6 acknowledged");

    const NetworkForm before = NetworkFormFromState(network);
    EXPECT_TRUE(before.slotUart[0]);
    EXPECT_FALSE(before.slotUart[1]);
    EXPECT_EQ(before.slotPeer[0].ToString(), "AT");
    NetworkForm after = before;
    std::string error;
    ASSERT_TRUE(ComPortSpec::Parse("loopback", after.slotPeer[0], error));
    after.espChip = "ESP8266-AT221";
    const auto changes = NetworkFormChanges(before, after);
    EXPECT_NE(std::find(changes.begin(), changes.end(), std::make_pair(std::string("isa1_peer"), std::string("LOOPBACK"))),
              changes.end());
    EXPECT_NE(std::find(changes.begin(), changes.end(), std::make_pair(std::string("esp_chip"), std::string("esp8266-at221"))),
              changes.end());
}

// The 3C509B in a slot (network phase SN5): its 16 registers, the ID port and isolation state, window, FIFOs, link
TEST(NetworkPanelModel_Test, El3c509bRow)
{
    StateNode network = StateNode::Object();
    StateNode slots = StateNode::Array();
    StateNode el3 = StateNode::Object();
    el3["id"] = "isa2";
    el3["label"] = "ISA slot 2 (J7), page #D6";
    el3["configured"] = "el3c509b";
    el3["card"] = "el3c509b";
    el3["chip"] = "3C509B-TPO";
    el3["base"] = "#300";
    el3["id_port"] = "#110";
    el3["ids"] = "ID_WAIT, sequence 0/255, tag 0";
    el3["activated"] = true;
    el3["irq"] = 3;
    el3["mac"] = "02:53:50:00:00:02";
    el3["link"] = "ethernet-gateway";
    el3["link_state"] = "link pass";
    el3["window"] = 1;
    StateNode fifo = StateNode::Object();
    fifo["tx_packets"] = static_cast<uint64_t>(0);
    fifo["tx_free"] = 3068;
    fifo["rx_packets"] = static_cast<uint64_t>(1);
    fifo["rx_free"] = 5048;
    el3["fifo"] = fifo;
    slots.push(el3);
    network["slots"] = slots;

    const std::vector<NetworkSlotRow> rows = NetworkSlotRows(network);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].line, "EL3C509B 3C509B-TPO, I/O #300-#30F, IRQ 3, MAC 02:53:50:00:00:02, cable: ethernet-gateway, "
                            "ID port #110 (ID_WAIT, sequence 0/255, tag 0), active, window 1, TX FIFO 0 pkt / 3068 free, "
                            "RX FIFO 1 pkt / 5048 free, link pass");
}

// SprinterSerial in a slot (network SN4): both lines in the row - COM1's peer, COM2's Hayes modem with its call and
// DCD - and both editable (isaN_peer, isaN_peer_b); the modem phone book is a setting of its own
TEST(NetworkPanelModel_Test, SprinterSerialRowWithAModemOnCom2)
{
    StateNode network = StateNode::Object();
    StateNode slots = StateNode::Array();
    StateNode card = StateNode::Object();
    card["id"] = "isa1";
    card["label"] = "ISA slot 1 (J6), page #D4";
    card["configured"] = "dual16552";
    card["card"] = "dual16552";
    card["chip"] = "PC16552D";
    card["base"] = "#3F8";
    card["irq"] = 3;
    card["peer_spec"] = "TCP:localhost:2323";
    StateNode uart = StateNode::Object();
    uart["baud"] = 57600;
    uart["mcr"] = "#0B";
    card["uart"] = uart;
    StateNode peer = StateNode::Object();
    peer["kind"] = "tcp";
    peer["target"] = "localhost:2323";
    card["peer"] = peer;
    StateNode b = StateNode::Object();
    b["base"] = "#2F8";
    b["peer_spec"] = "MODEM";
    b["uart"] = uart;
    StateNode peerB = StateNode::Object();
    peerB["kind"] = "modem";
    b["peer"] = peerB;
    StateNode modem = StateNode::Object();
    modem["mode"] = "online";
    StateNode call = StateNode::Object();
    call["dialed"] = "bbs.test:23";
    modem["call"] = call;
    StateNode lines = StateNode::Object();
    lines["dcd"] = true;
    lines["ri"] = false;
    modem["lines"] = lines;
    modem["last_result"] = "CONNECT 57600";
    b["modem"] = modem;
    card["channel_b"] = b;
    slots.push(card);
    network["slots"] = slots;
    StateNode settings = StateNode::Object();
    settings["modem_phonebook"] = "5551234=bbs.test:23";
    network["settings"] = settings;

    const std::vector<NetworkSlotRow> rows = NetworkSlotRows(network);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].line, "DUAL16552 PC16552D, I/O #3F8-#3FF, IRQ 3; COM1: 57600 baud, MCR #0B, tcp localhost:2323; "
                            "COM2 #2F8: 57600 baud, MCR #0B, modem online bbs.test:23, DCD on, last CONNECT 57600");

    const NetworkForm before = NetworkFormFromState(network);
    EXPECT_TRUE(before.slotUart[0]);
    EXPECT_TRUE(before.slotUartB[0]);
    EXPECT_EQ(before.slotCard[0], "dual16552");
    EXPECT_EQ(before.slotPeerB[0].ToString(), "MODEM");
    EXPECT_EQ(before.modemPhonebook, "5551234=bbs.test:23");
    EXPECT_TRUE(NetworkPeerIsModem(before.slotPeerB[0]));
    NetworkForm after = before;
    std::string error;
    ASSERT_TRUE(ComPortSpec::Parse("modem,2323", after.slotPeerB[0], error));
    after.modemPhonebook = "7=10.0.2.2:2323";
    const auto changes = NetworkFormChanges(before, after);
    EXPECT_NE(std::find(changes.begin(), changes.end(), std::make_pair(std::string("isa1_peer_b"), std::string("MODEM,2323"))),
              changes.end());
    EXPECT_NE(std::find(changes.begin(), changes.end(),
                        std::make_pair(std::string("modem_phonebook"), std::string("7=10.0.2.2:2323"))),
              changes.end());
}
