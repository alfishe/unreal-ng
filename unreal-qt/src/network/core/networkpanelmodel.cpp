#include "networkpanelmodel.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "emulator/io/keyboard/atm2kbc.h"
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
    std::string out;
    auto add = [&out](const char* name) { out += (out.empty() ? "" : ",") + std::string(name); };
    if (form.zxNetUsb)
        add("zxnetusb");
    if (form.zxWifi)
        add("zxwifi");
    if (form.atm2IoEsp)
        add("atm2ioesp");
    return out.empty() ? "none" : out;
}
}  // namespace

NetworkForm NetworkFormFromState(const StateNode& network)
{
    NetworkForm form;
    if (const StateNode* machine = network.find("machine"))
    {
        form.zxBus = Flag(machine->find("zx_bus"), true);
        form.internalIo = Flag(machine->find("internal_io"), false);
        form.serialPort = Text(machine->find("serial_port"), "none");
        form.zifiMachine = Flag(machine->find("zifi"), false);
    }
    if (const StateNode* slots = network.find("slots"))
    {
        for (const StateNode& slot : slots->items)
        {
            const std::string id = Text(slot.find("id"));
            const int n = id == "isa1" ? 0 : id == "isa2" ? 1 : -1;
            if (n < 0 || Text(slot.find("card")) != "sprinteresp")
                continue;
            form.slotUart[n] = true;
            form.slotPeer[n] = Peer(Text(slot.find("peer_spec")), "AT");
        }
    }
    const StateNode* set = network.find("settings");
    if (!set)
        return form;
    const std::string cards = Upper(Text(set->find("card"), "NONE"));
    form.zxNetUsb = cards.find("ZXNETUSB") != std::string::npos;
    form.zxWifi = cards.find("ZXWIFI") != std::string::npos;
    form.atm2IoEsp = cards.find("ATM2IOESP") != std::string::npos;
    form.atm2IoEspPeer = Peer(Text(set->find("atm2ioesp")), "AT");
    {
        const std::string address = Text(set->find("atm2ioesp_address"), "0xF0");
        form.atm2IoEspAddress = static_cast<unsigned>(std::strtoul(address.c_str(), nullptr, 0));
    }
    form.zifiPeer = Peer(Text(set->find("zifi")), "NONE");
    form.comPort = Peer(Text(set->find("com_port")), "NONE");
    form.zxWifiPeer = Peer(Text(set->find("zx_wifi")), "AT");
    form.espChip = Upper(Text(set->find("esp_chip"), "ESP32"));
    form.modemLines = Flag(set->find("com_modem_lines"), false);
    form.avrFirmware = Upper(Text(set->find("avr_firmware"), "BASE2023"));
    form.kbcFirmware = Upper(Text(set->find("kbc_firmware")));
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
    if (before.zxNetUsb != after.zxNetUsb || before.zxWifi != after.zxWifi || before.atm2IoEsp != after.atm2IoEsp)
        out.emplace_back("card", Cards(after));
    if (before.atm2IoEspPeer.ToString() != after.atm2IoEspPeer.ToString())
        out.emplace_back("atm2ioesp", after.atm2IoEspPeer.ToString());
    if (before.atm2IoEspAddress != after.atm2IoEspAddress)
    {
        char text[8];
        std::snprintf(text, sizeof text, "0x%02X", after.atm2IoEspAddress);
        out.emplace_back("atm2ioesp_address", text);
    }
    if (before.zifiPeer.ToString() != after.zifiPeer.ToString())
        out.emplace_back("zifi", after.zifiPeer.ToString());
    for (int n = 0; n < 2; ++n)
    {
        if (after.slotUart[n] && before.slotPeer[n].ToString() != after.slotPeer[n].ToString())
            out.emplace_back(n == 0 ? "isa1_peer" : "isa2_peer", after.slotPeer[n].ToString());
    }
    if (before.comPort.ToString() != after.comPort.ToString())
        out.emplace_back("com_port", after.comPort.ToString());
    if (before.zxWifiPeer.ToString() != after.zxWifiPeer.ToString())
        out.emplace_back("zx_wifi", after.zxWifiPeer.ToString());
    if (Upper(before.espChip) != Upper(after.espChip))
    {
        std::string chip = after.espChip;
        for (char& c : chip)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        out.emplace_back("esp_chip", chip.empty() ? std::string("esp32") : chip);
    }
    if (before.modemLines != after.modemLines)
        out.emplace_back("com_modem_lines", after.modemLines ? "on" : "off");
    if (Upper(before.avrFirmware) != Upper(after.avrFirmware))
        out.emplace_back("avr_firmware", after.avrFirmware);
    if (!after.kbcFirmware.empty() && Upper(before.kbcFirmware) != Upper(after.kbcFirmware))
        out.emplace_back("kbc_firmware", after.kbcFirmware);
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
        a.zxWifiWhy = "Ports #xxEF are the TS-Conf AVR's (its COM port and ZiFi): use the machine's serial port and ZiFi below.";
    }

    // ATM Turbo 2+: the port follows the controller firmware chosen (V31 on has RS-232)
    Atm2Kbc::Firmware kbc = Atm2Kbc::Firmware::None;
    const bool kbcSocket = !form.kbcFirmware.empty();
    const Atm2Kbc::FirmwareInfo* kbcInfo =
        kbcSocket && Atm2Kbc::ParseFirmware(form.kbcFirmware.c_str(), kbc) ? Atm2Kbc::Info(kbc) : nullptr;

    if (form.serialPort == "zifi")
    {
        // TS-Conf: the TS AVR firmware's 16550 at #F8EF..#FFEF takes a peer like the ZX-Evo's
    }
    else if (kbcSocket)
    {
        if (!kbcInfo || !kbcInfo->serialPort)
        {
            a.comPort = false;
            a.comPortWhy = kbc == Atm2Kbc::Firmware::None
                               ? "No keyboard controller fitted: its RS-232 is the machine's serial port."
                               : "This keyboard controller firmware has no RS-232: choose V31 or later.";
        }
    }
    else if (form.serialPort != "evo-avr")
    {
        a.comPort = false;
        a.comPortWhy = "This machine has no serial port of its own: a ZX-WiFi card adds one.";
    }

    if (form.serialPort == "zifi")
    {
        a.avrFirmware = false;
        a.avrFirmwareWhy = "TS-Conf runs the TS-Labs AVR firmware (2016-04 and later).";
    }
    else if (form.serialPort != "evo-avr")
    {
        a.avrFirmware = false;
        a.avrFirmwareWhy = "Only the ZX-Evo has the AVR.";
    }
    if (!form.zifiMachine)
    {
        a.zifi = false;
        a.zifiWhy = "ZiFi is the TS-Labs AVR firmware's: TS-Conf, or a ZX-Evo with the AVR firmware TS2016-02 / TS2016-04.";
    }
    if (!kbcSocket)
    {
        a.kbcFirmware = false;
        a.kbcFirmwareWhy = "Only the ATM Turbo 2+ (v7.xx) has the keyboard controller.";
    }
    if (!form.internalIo)
    {
        a.atm2IoEsp = false;
        a.atm2IoEspWhy = "ATM2IOESP plugs into the ATM Turbo 2+ INTERNAL I/O connector (#FB / #FA); this machine has none.";
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

std::vector<std::pair<std::string, std::string>> NetworkKbcFirmwareChoices()
{
    using F = Atm2Kbc::Firmware;
    std::vector<std::pair<std::string, std::string>> out;
    out.emplace_back(Atm2Kbc::FirmwareName(F::None), "no controller: #FE is the plain matrix port");
    for (F firmware : {F::V22At7, F::V22At11, F::V22At12, F::V31At7, F::V31At11, F::V32At7, F::V32At11, F::V40, F::V41})
    {
        const Atm2Kbc::FirmwareInfo* info = Atm2Kbc::Info(firmware);
        out.emplace_back(Atm2Kbc::FirmwareName(firmware), info ? info->description : "");
    }
    return out;
}

std::vector<uint32_t> NetworkSerialBaudChoices()
{
    return {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
}

std::vector<NetworkSlotRow> NetworkSlotRows(const StateNode& network, const StateNode* isa)
{
    std::vector<NetworkSlotRow> rows;
    const StateNode* slots = network.find("slots");
    if (!slots)
        return rows;
    auto text = [](const StateNode& node, const char* key) {
        const StateNode* v = node.find(key);
        if (!v)
            return std::string();
        if (v->kind == StateNode::Kind::String)
            return v->s;
        if (v->kind == StateNode::Kind::Int)
            return std::to_string(v->i);
        return std::string();
    };
    for (const StateNode& slot : slots->items)
    {
        NetworkSlotRow row;
        row.id = text(slot, "id");
        row.label = text(slot, "label");
        const std::string card = text(slot, "card");
        if (card.empty() || card == "none")
        {
            const std::string configured = text(slot, "configured");
            const std::string why = text(slot, "not_fitted");
            row.line = configured.empty() || configured == "none" ? "empty"
                                                                  : configured + " not fitted" + (why.empty() ? "" : ": " + why);
        }
        else
        {
            std::string kind = card;
            for (char& c : kind)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            row.line = kind;
            const std::string chip = text(slot, "chip");
            if (!chip.empty())
                row.line += " " + chip;
            const std::string base = text(slot, "base");
            if (!base.empty() && base.size() > 1)
            {
                const unsigned first = static_cast<unsigned>(std::strtoul(base.c_str() + 1, nullptr, 16));
                char range[32];
                const unsigned size = card == "sprinteresp" ? 0x07 : 0x1F;   // a 16550's 8 registers / the NE2000's 32
                std::snprintf(range, sizeof(range), ", I/O #%03X-#%03X", first, first + size);
                row.line += range;
            }
            const std::string irq = text(slot, "irq");
            if (!irq.empty())
                row.line += ", IRQ " + irq;
            const std::string mac = text(slot, "mac");
            if (!mac.empty())
                row.line += ", MAC " + mac;
            const std::string link = text(slot, "link");
            if (!link.empty())
                row.line += ", cable: " + link;
            if (const StateNode* esp = slot.find("esp"))
            {
                // The SprinterESP: its UART line and the ESP's session at a glance
                if (const StateNode* uart = slot.find("uart"))
                    row.line += ", UART " + text(*uart, "baud") + " baud, MCR " + text(*uart, "mcr");
                row.line += "; ESP " + text(*esp, "firmware") + " (" + text(*esp, "state") + "), Wi-Fi " + text(*esp, "wifi");
                const std::string ip = text(*esp, "ip");
                if (!ip.empty() && ip != "0.0.0.0")
                    row.line += " " + ip;
                if (const StateNode* session = esp->find("at_session"))
                {
                    if (const StateNode* links = session->find("links"))
                        row.line += ", " + std::to_string(links->items.size()) + " link(s)";
                }
                row.line += ", MAC " + text(*esp, "mac");
            }
            if (!text(slot, "stalled").empty())
                row.line += " - STALLED (the ISA cycle hangs until RESET)";
            // The slot's IRQ line from the ISA report: level, PIO port B bit, whether it interrupts the CPU
            const StateNode* isaSlots = isa ? isa->find("slots") : nullptr;
            const size_t index = row.id == "isa1" ? 0 : row.id == "isa2" ? 1 : 2;
            if (isaSlots && index < isaSlots->items.size())
            {
                const StateNode& isaSlot = isaSlots->items[index];
                if (const StateNode* irq = isaSlot.find("irq_line"))
                {
                    const std::string reach = text(*irq, "reaches_cpu");
                    row.line += "; IRQ line " + text(*irq, "line") + " -> " + text(*irq, "pio_bit") + ", " +
                                (reach.rfind("yes", 0) == 0 ? std::string("interrupts the CPU")
                                                            : reach.size() > 4 ? reach.substr(4) : std::string("-"));
                    if (const StateNode* counters = isaSlot.find("counters"))
                    {
                        const StateNode* acks = counters->find("irq_acknowledged");
                        if (acks && acks->kind == StateNode::Kind::Int && acks->i > 0)
                            row.line += ", " + std::to_string(acks->i) + " acknowledged";
                    }
                }
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}
