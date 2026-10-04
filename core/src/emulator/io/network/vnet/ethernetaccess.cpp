#include "emulator/io/network/vnet/ethernetaccess.h"

#include <cctype>
#include <cstdio>

#include "common/network/hostframebridge.h"
#include "common/network/nettypes.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/network/vnet/ethernetgateway.h"

namespace
{
uint16_t G16(const std::vector<uint8_t>& f, size_t at) { return static_cast<uint16_t>((f[at] << 8) | f[at + 1]); }
uint32_t G32(const std::vector<uint8_t>& f, size_t at)
{
    return (static_cast<uint32_t>(f[at]) << 24) | (static_cast<uint32_t>(f[at + 1]) << 16) | (static_cast<uint32_t>(f[at + 2]) << 8) |
           f[at + 3];
}

EthernetGateway* GatewayOf(EmulatorContext* context, std::string& error)
{
    if (!context || !context->pEthernetGateway)
    {
        error = "no Ethernet card on a cable here (a frame-level card - the Sprinter's NE2000 - with the network on)";
        return nullptr;
    }
    return context->pEthernetGateway;
}
}  // namespace

namespace EthernetAccess
{

std::string Summary(const std::vector<uint8_t>& f)
{
    if (f.size() < 14)
        return "runt";
    char text[160];
    const uint16_t type = G16(f, 12);
    if (type == 0x0806 && f.size() >= 42)
    {
        const uint16_t op = G16(f, 20);
        if (op == 1)
            std::snprintf(text, sizeof(text), "ARP who-has %s tell %s", NetIpToString(G32(f, 38)).c_str(),
                          NetIpToString(G32(f, 28)).c_str());
        else
            std::snprintf(text, sizeof(text), "ARP %s is-at %02X:%02X:%02X:%02X:%02X:%02X", NetIpToString(G32(f, 28)).c_str(),
                          f[22], f[23], f[24], f[25], f[26], f[27]);
        return text;
    }
    if (type == 0x0800 && f.size() >= 34)
    {
        const size_t ihl = static_cast<size_t>(f[14] & 0x0F) * 4;
        const uint8_t proto = f[23];
        const std::string src = NetIpToString(G32(f, 26));
        const std::string dst = NetIpToString(G32(f, 30));
        const size_t l4 = 14 + ihl;
        if ((proto == 6 || proto == 17) && f.size() >= l4 + 4)
        {
            const unsigned sp = G16(f, l4), dp = G16(f, l4 + 2);
            if (proto == 17)
            {
                std::snprintf(text, sizeof(text), "IPv4 %s:%u > %s:%u UDP%s", src.c_str(), sp, dst.c_str(), dp,
                              (dp == 67 || dp == 68) ? " DHCP" : (dp == 53 || sp == 53) ? " DNS" : "");
                return text;
            }
            std::string flags;
            if (f.size() > l4 + 13)
            {
                const uint8_t fl = f[l4 + 13];
                flags += (fl & 0x02) ? "S" : "";
                flags += (fl & 0x01) ? "F" : "";
                flags += (fl & 0x04) ? "R" : "";
                flags += (fl & 0x08) ? "P" : "";
                flags += (fl & 0x10) ? "." : "";
            }
            const size_t total = G16(f, 16);
            const size_t offset = f.size() > l4 + 12 ? static_cast<size_t>(f[l4 + 12] >> 4) * 4 : 20;
            const size_t data = total > ihl + offset ? total - ihl - offset : 0;
            std::snprintf(text, sizeof(text), "IPv4 %s:%u > %s:%u TCP %s seq %u ack %u len %zu", src.c_str(), sp, dst.c_str(), dp,
                          flags.c_str(), G32(f, l4 + 4), G32(f, l4 + 8), data);
            return text;
        }
        if (proto == 1 && f.size() > l4)
        {
            std::snprintf(text, sizeof(text), "IPv4 %s > %s ICMP %s", src.c_str(), dst.c_str(),
                          f[l4] == 8 ? "echo request" : f[l4] == 0 ? "echo reply" : "type");
            return text;
        }
        std::snprintf(text, sizeof(text), "IPv4 %s > %s proto %u", src.c_str(), dst.c_str(), proto);
        return text;
    }
    std::snprintf(text, sizeof(text), "ethertype #%04X", type);
    return text;
}

StateNode Frames(EmulatorContext* context, const std::string& link, unsigned last)
{
    std::string error;
    EthernetGateway* gateway = GatewayOf(context, error);
    StateNode ret = StateNode::Object();
    if (!gateway)
    {
        ret["available"] = false;
        ret["description"] = error;
        return ret;
    }
    ret["available"] = true;
    ret["capacity"] = static_cast<uint64_t>(EthernetGateway::kCaptureLength);
    std::vector<const EthernetGateway::CapturedFrame*> chosen;
    for (const auto& f : gateway->Capture())
    {
        if (link.empty() || f.port == link)
            chosen.push_back(&f);
    }
    const size_t from = last && last < chosen.size() ? chosen.size() - last : 0;
    StateNode frames = StateNode::Array();
    for (size_t i = from; i < chosen.size(); ++i)
    {
        const auto& f = *chosen[i];
        StateNode n = StateNode::Object();
        n["index"] = f.index;
        n["frame"] = f.frame;
        // BRIDGE: lan_in = arrived from the host LAN (port "lan"), lan_out = a card's frame that went to the LAN
        n["direction"] = f.lan ? (f.toCard ? "lan_in" : "lan_out") : (f.toCard ? "to_card" : "from_card");
        n["port"] = f.port;
        n["length"] = static_cast<uint64_t>(f.bytes.size());
        n["summary"] = Summary(f.bytes);
        std::string hex;
        char b[4];
        for (uint8_t byte : f.bytes)
        {
            std::snprintf(b, sizeof(b), "%02X", byte);
            hex += b;
        }
        n["hex"] = hex;
        frames.push(std::move(n));
    }
    ret["frames"] = frames;
    return ret;
}

bool Pcap(EmulatorContext* context, const std::string& link, std::vector<uint8_t>& out, std::string& error)
{
    EthernetGateway* gateway = GatewayOf(context, error);
    if (!gateway)
        return false;
    out = gateway->CapturePcap(link);
    return true;
}

StateNode Adapters()
{
    StateNode ret = StateNode::Object();
    HostFrameBridge bridge;
    std::string error;
    const std::vector<HostAdapter> adapters = bridge.Adapters(error);
    ret["library"] = bridge.Library();
    ret["error"] = error;
    StateNode list = StateNode::Array();
    for (const HostAdapter& a : adapters)
    {
        StateNode n = StateNode::Object();
        n["name"] = a.name;
        n["description"] = a.description;
        StateNode ips = StateNode::Array();
        for (const std::string& ip : a.ipv4)
            ips.push(ip);
        n["ipv4"] = ips;
        n["loopback"] = a.loopback;
        n["wireless"] = a.wireless;
        n["up"] = a.up;
        n["running"] = a.running;
        // Wi-Fi bridges through MAC translation (its own MAC is needed); loopback has no LAN
        n["bridgeable"] = !a.loopback && (!a.wireless || a.hasMac);
        n["translation"] = a.wireless;
        if (a.hasMac)
        {
            char mac[18];
            std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", a.mac[0], a.mac[1], a.mac[2], a.mac[3], a.mac[4], a.mac[5]);
            n["mac"] = std::string(mac);
        }
        list.push(std::move(n));
    }
    ret["adapters"] = list;
    return ret;
}

bool Inject(EmulatorContext* context, const std::string& link, const std::string& hex, const char* source, std::string& error)
{
    EthernetGateway* gateway = GatewayOf(context, error);
    if (!gateway)
        return false;
    std::vector<uint8_t> frame;
    std::string digits;
    for (char c : hex)
    {
        if (std::isxdigit(static_cast<unsigned char>(c)))
            digits.push_back(c);
        else if (c != ' ' && c != ':' && c != '-')
        {
            error = "hex: hex digits only (spaces, ':' and '-' allowed between bytes)";
            return false;
        }
    }
    if (digits.size() % 2 || digits.size() < 28 || digits.size() > 2 * 1514)
    {
        error = "hex: a whole frame of 14..1514 bytes (no CRC)";
        return false;
    }
    for (size_t i = 0; i < digits.size(); i += 2)
        frame.push_back(static_cast<uint8_t>(std::stoul(digits.substr(i, 2), nullptr, 16)));
    bool ok = false;
    auto edit = [&]() { ok = gateway->Inject(link, frame); };
    if (context->pEmulator)
        context->pEmulator->EditMemoryFromTool(source ? source : "network frame", edit);
    else
        edit();
    if (!ok)
        error = "link: no card with that port key (see state/network ethernet_gateway.ports)";
    return ok;
}

}  // namespace EthernetAccess
