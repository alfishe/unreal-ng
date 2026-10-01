#include "networkpanelmodel.h"

#include "emulator/io/serial/uart16550.h"

namespace
{
std::string Text(const StateNode* node, const std::string& fallback = std::string())
{
    return (node && node->kind == StateNode::Kind::String) ? node->s : fallback;
}

bool Flag(const StateNode* node, bool fallback)
{
    return (node && node->kind == StateNode::Kind::Bool) ? node->b : fallback;
}

ComPortSpec Peer(const std::string& text, const char* fallback)
{
    ComPortSpec spec;
    std::string error;
    if (text.empty() || !ComPortSpec::Parse(text, spec, error))
        ComPortSpec::Parse(fallback, spec, error);
    return spec;
}

std::string Upper(std::string text)
{
    for (char& c : text)
    {
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    }
    return text;
}

std::string Cards(const NetworkForm& form)
{
    if (form.zxNetUsb && form.zxWifi)
        return "zxnetusb,zxwifi";
    if (form.zxNetUsb)
        return "zxnetusb";
    if (form.zxWifi)
        return "zxwifi";
    return "none";
}
}  // namespace

NetworkForm NetworkFormFromState(const StateNode& network)
{
    NetworkForm form;
    if (const StateNode* machine = network.find("machine"))
    {
        form.zxBus = Flag(machine->find("zx_bus"), true);
        form.serialPort = Text(machine->find("serial_port"), "none");
    }
    const StateNode* set = network.find("settings");
    if (!set)
        return form;
    const std::string cards = Upper(Text(set->find("card"), "NONE"));
    form.zxNetUsb = cards.find("ZXNETUSB") != std::string::npos;
    form.zxWifi = cards.find("ZXWIFI") != std::string::npos;
    form.comPort = Peer(Text(set->find("com_port")), "NONE");
    form.zxWifiPeer = Peer(Text(set->find("zx_wifi")), "AT");
    form.espChip = Upper(Text(set->find("esp_chip"), "ESP32"));
    form.modemLines = Flag(set->find("com_modem_lines"), false);
    form.avrFirmware = Upper(Text(set->find("avr_firmware"), "BASE2023"));
    form.hostAccess = Flag(set->find("host_access"), true);
    form.dnsMode = Upper(Text(set->find("dns_mode"), "HOST"));
    form.hosts = Text(set->find("hosts"));
    form.forwards = Text(set->find("forwards"));
    if (const StateNode* timeout = set->find("connect_timeout_ms"); timeout && timeout->kind == StateNode::Kind::Int)
        form.connectTimeoutMs = static_cast<unsigned>(timeout->i);
    return form;
}

std::vector<std::pair<std::string, std::string>> NetworkFormChanges(const NetworkForm& before, const NetworkForm& after)
{
    std::vector<std::pair<std::string, std::string>> out;
    if (before.zxNetUsb != after.zxNetUsb || before.zxWifi != after.zxWifi)
        out.emplace_back("card", Cards(after));
    if (before.comPort.ToString() != after.comPort.ToString())
        out.emplace_back("com_port", after.comPort.ToString());
    if (before.zxWifiPeer.ToString() != after.zxWifiPeer.ToString())
        out.emplace_back("zx_wifi", after.zxWifiPeer.ToString());
    if (Upper(before.espChip) != Upper(after.espChip))
        out.emplace_back("esp_chip", Upper(after.espChip) == "ESP8266" ? "esp8266" : "esp32");
    if (before.modemLines != after.modemLines)
        out.emplace_back("com_modem_lines", after.modemLines ? "on" : "off");
    if (Upper(before.avrFirmware) != Upper(after.avrFirmware))
        out.emplace_back("avr_firmware", after.avrFirmware);
    if (before.hostAccess != after.hostAccess)
        out.emplace_back("host_access", after.hostAccess ? "on" : "off");
    if (Upper(before.dnsMode) != Upper(after.dnsMode))
        out.emplace_back("dns_mode", Upper(after.dnsMode) == "PASS" ? "pass" : "host");
    if (before.hosts != after.hosts)
        out.emplace_back("hosts", after.hosts);
    if (before.forwards != after.forwards)
        out.emplace_back("forwards", after.forwards);
    if (before.connectTimeoutMs != after.connectTimeoutMs)
        out.emplace_back("connect_timeout_ms", std::to_string(after.connectTimeoutMs));
    return out;
}

NetworkAvailability NetworkFormAvailability(const NetworkForm& form)
{
    NetworkAvailability a;
    if (!form.zxBus)
    {
        a.cards = false;
        a.cardsWhy = "This machine has no ZX-Bus.";
    }
    if (!a.cards)
    {
        a.zxWifi = false;
        a.zxWifiWhy = a.cardsWhy;
    }
    else if (form.serialPort == "evo-avr")
    {
        a.zxWifi = false;
        a.zxWifiWhy = "Ports #F8EF..#FFEF are the ZX-Evo AVR's COM port: use the machine's serial port below.";
    }
    else if (form.serialPort == "zifi")
    {
        a.zxWifi = false;
        a.zxWifiWhy = "Ports #xxEF are TS-Conf's ZiFi.";
    }

    if (form.serialPort == "zifi")
    {
        a.comPort = false;
        a.comPortWhy = "TS-Conf's serial port (ZiFi) is not emulated yet.";
    }
    else if (form.serialPort != "evo-avr")
    {
        a.comPort = false;
        a.comPortWhy = "This machine has no serial port of its own: a ZX-WiFi card adds one.";
    }

    if (form.serialPort != "evo-avr")
    {
        a.avrFirmware = false;
        a.avrFirmwareWhy = "Only the ZX-Evo has the AVR.";
    }
    return a;
}

bool NetworkPeerIsEsp(const ComPortSpec& peer)
{
    return peer.kind == ComPortSpec::Kind::Espnet || peer.kind == ComPortSpec::Kind::At;
}

std::vector<std::pair<std::string, std::string>> NetworkAvrFirmwareChoices()
{
    using F = Uart16550::AvrFirmware;
    const std::pair<F, const char*> rows[] = {
        {F::Base2010, "NedoPC 2010-10 .. 2011-04: a register file, no transfer"},
        {F::Base2011Apr, "NedoPC 2011-04: first working UART, divisor not reset, RTS inverted"},
        {F::Base2011May, "NedoPC 2011-05: DLM bit 7 = the AVR's own divisor"},
        {F::Base2011Sep, "NedoPC 2011-09: RTS fixed"},
        {F::Base2013, "NedoPC 2013-11: an FCR RX reset clears OE"},
        {F::Base2023, "NedoPC 2023 .. now (default): LSR bit 7 = RX half full"},
        {F::Ts2013, "TS-Labs 2013-05 .. 2016-02: 256-byte FIFOs"},
        {F::Ts2016Feb, "TS-Labs 2016-02 .. 04: needs the TS-Conf FPGA, misses the registers on BaseConf"},
        {F::Ts2016Apr, "TS-Labs 2016-04 .. now: 511 / 255-byte rings; on BaseConf indexed by the clock address"},
    };
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [firmware, text] : rows)
        out.emplace_back(Uart16550::AvrFirmwareName(firmware), text);
    return out;
}

std::vector<uint32_t> NetworkSerialBaudChoices()
{
    return {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
}
