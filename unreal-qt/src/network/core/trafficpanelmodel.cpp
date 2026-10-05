#include "network/core/trafficpanelmodel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace
{
std::string Text(const StateNode* n) { return n && n->kind == StateNode::Kind::String ? n->s : std::string(); }
uint64_t Num(const StateNode* n) { return n ? static_cast<uint64_t>(n->i) : 0; }

std::vector<uint8_t> FromHex(const std::string& hex)
{
    std::vector<uint8_t> out;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        const int hi = nibble(hex[i]), lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0)
            break;
        out.push_back(static_cast<uint8_t>(hi << 4 | lo));
    }
    return out;
}

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

uint16_t G16(const std::vector<uint8_t>& b, size_t at) { return static_cast<uint16_t>((b[at] << 8) | b[at + 1]); }
uint32_t G32(const std::vector<uint8_t>& b, size_t at) { return (static_cast<uint32_t>(G16(b, at)) << 16) | G16(b, at + 2); }

std::string Mac(const std::vector<uint8_t>& b, size_t at)
{
    char t[18];
    std::snprintf(t, sizeof(t), "%02X:%02X:%02X:%02X:%02X:%02X", b[at], b[at + 1], b[at + 2], b[at + 3], b[at + 4], b[at + 5]);
    return t;
}
std::string Ip(uint32_t a)
{
    return std::to_string(a >> 24) + "." + std::to_string((a >> 16) & 255) + "." + std::to_string((a >> 8) & 255) + "." +
           std::to_string(a & 255);
}

/// A DNS name at `at` in `msg` (labels; one level of compression)
std::string DnsName(const std::vector<uint8_t>& msg, size_t at)
{
    std::string name;
    for (int guard = 0; at < msg.size() && guard < 64; ++guard)
    {
        const uint8_t len = msg[at];
        if (len == 0)
            break;
        if ((len & 0xC0) == 0xC0)
        {
            if (at + 1 >= msg.size())
                break;
            at = static_cast<size_t>(((len & 0x3F) << 8) | msg[at + 1]);
            continue;
        }
        if (at + 1 + len > msg.size())
            break;
        if (!name.empty())
            name += '.';
        name.append(reinterpret_cast<const char*>(msg.data() + at + 1), len);
        at += 1 + len;
    }
    return name;
}

void DecodeUdpPayload(TrafficDecodeNode& udp, uint16_t sport, uint16_t dport, const std::vector<uint8_t>& p)
{
    if ((sport == 67 || sport == 68 || dport == 67 || dport == 68) && p.size() >= 240)
    {
        TrafficDecodeNode d{"DHCP", {}};
        d.children.push_back({std::string("op ") + (p[0] == 1 ? "request" : "reply"), {}});
        d.children.push_back({"client MAC " + Mac(p, 28), {}});
        d.children.push_back({"your address " + Ip(G32(p, 16)), {}});
        // option 53: the message type
        static const char* const kTypes[] = {"?", "DISCOVER", "OFFER", "REQUEST", "DECLINE", "ACK", "NAK", "RELEASE", "INFORM"};
        for (size_t at = 240; at + 2 <= p.size() && p[at] != 255;)
        {
            if (p[at] == 0)
            {
                ++at;
                continue;
            }
            if (p[at] == 53 && at + 2 < p.size() && p[at + 2] <= 8)
                d.children.push_back({std::string("message ") + kTypes[p[at + 2]], {}});
            at += 2u + p[at + 1];
        }
        udp.children.push_back(d);
    }
    else if ((sport == 53 || dport == 53) && p.size() >= 12)
    {
        TrafficDecodeNode d{"DNS", {}};
        d.children.push_back({std::string((p[2] & 0x80) ? "response" : "query") + ", id " + std::to_string(G16(p, 0)), {}});
        if (G16(p, 4) > 0)
            d.children.push_back({"question " + DnsName(p, 12), {}});
        d.children.push_back({"answers " + std::to_string(G16(p, 6)), {}});
        udp.children.push_back(d);
    }
}
}  // namespace

std::vector<TrafficRow> TrafficRows(const StateNode& report)
{
    std::vector<TrafficRow> rows;
    const StateNode* records = report.find("records");
    if (!records)
        return rows;
    for (const StateNode& r : records->items)
    {
        TrafficRow row;
        row.index = Num(r.find("index"));
        row.frame = Num(r.find("frame"));
        row.tInFrame = Num(r.find("t_in_frame"));
        row.timeUs = Num(r.find("time_us"));
        row.frameKind = Text(r.find("kind")) != "socket";
        row.out = Text(r.find("direction")) == "out";
        row.adapter = Text(r.find("adapter"));
        row.summary = Text(r.find("summary"));
        row.op = Text(r.find("op"));
        row.peer = Text(r.find("peer"));
        row.proto = Text(r.find("proto"));
        row.socket = Num(r.find("socket"));
        row.bytes = FromHex(Text(r.find("hex")));
        rows.push_back(std::move(row));
    }
    return rows;
}

bool TrafficRowMatches(const TrafficRow& row, const std::string& words, int kind)
{
    if (kind == 1 && !row.frameKind)
        return false;
    if (kind == 2 && row.frameKind)
        return false;
    const std::string hay = Lower(row.adapter + " " + (row.out ? "out" : "in") + " " + row.op + " " + row.summary);
    std::istringstream in(Lower(words));
    std::string word;
    while (in >> word)
    {
        if (hay.find(word) == std::string::npos)
            return false;
    }
    return true;
}

std::string TrafficTimeText(const TrafficRow& row, uint64_t previousUs)
{
    std::string s = "f " + std::to_string(row.frame);
    if (previousUs && row.timeUs >= previousUs)
    {
        char t[32];
        std::snprintf(t, sizeof(t), "  +%.2f ms", static_cast<double>(row.timeUs - previousUs) / 1000.0);
        s += t;
    }
    return s;
}

std::vector<TrafficDecodeNode> TrafficDecode(const TrafficRow& row)
{
    std::vector<TrafficDecodeNode> out;
    TrafficDecodeNode where{"Record #" + std::to_string(row.index) + ": " + row.adapter + (row.out ? " -> network" : " <- network"), {}};
    where.children.push_back({"TTD position: frame " + std::to_string(row.frame) + ", " + std::to_string(row.tInFrame) + " units in", {}});
    where.children.push_back({"emulated time " + std::to_string(row.timeUs) + " us", {}});
    out.push_back(where);
    const std::vector<uint8_t>& b = row.bytes;
    if (!row.frameKind)
    {
        TrafficDecodeNode s{"Socket operation: " + row.proto + " " + row.op, {}};
        s.children.push_back({"socket " + std::to_string(row.socket), {}});
        if (!row.peer.empty())
            s.children.push_back({"peer " + row.peer, {}});
        s.children.push_back({std::to_string(b.size()) + " bytes", {}});
        out.push_back(s);
        return out;
    }
    if (b.size() < 14)
        return out;
    const uint16_t type = G16(b, 12);
    char typeText[8];
    std::snprintf(typeText, sizeof(typeText), "%04X", type);
    out.push_back({"Ethernet", {{"destination " + Mac(b, 0), {}}, {"source " + Mac(b, 6), {}}, {std::string("type #") + typeText, {}}}});
    if (type == 0x0806 && b.size() >= 42)
    {
        const uint16_t op = G16(b, 20);
        out.push_back({std::string("ARP ") + (op == 1 ? "request" : op == 2 ? "reply" : "op " + std::to_string(op)),
                       {{"sender " + Ip(G32(b, 28)) + " at " + Mac(b, 22), {}}, {"target " + Ip(G32(b, 38)) + " at " + Mac(b, 32), {}}}});
        return out;
    }
    if (type != 0x0800 || b.size() < 34)
        return out;
    const size_t ihl = static_cast<size_t>(b[14] & 0x0F) * 4;
    const uint8_t proto = b[23];
    out.push_back({"IPv4 " + Ip(G32(b, 26)) + " > " + Ip(G32(b, 30)),
                   {{"protocol " + std::to_string(proto), {}}, {"TTL " + std::to_string(b[22]), {}}, {"length " + std::to_string(G16(b, 16)), {}}}});
    const size_t at = 14 + ihl;
    if (proto == 1 && b.size() >= at + 4)
        out.push_back({"ICMP type " + std::to_string(b[at]) + (b[at] == 8 ? " (echo request)" : b[at] == 0 ? " (echo reply)" : ""), {}});
    else if (proto == 17 && b.size() >= at + 8)
    {
        const uint16_t sport = G16(b, at), dport = G16(b, at + 2);
        TrafficDecodeNode udp{"UDP " + std::to_string(sport) + " > " + std::to_string(dport), {{"length " + std::to_string(G16(b, at + 4)), {}}}};
        const size_t end = std::min(b.size(), at + G16(b, at + 4));
        if (end > at + 8)
            DecodeUdpPayload(udp, sport, dport, std::vector<uint8_t>(b.begin() + static_cast<std::ptrdiff_t>(at + 8), b.begin() + static_cast<std::ptrdiff_t>(end)));
        out.push_back(udp);
    }
    else if (proto == 6 && b.size() >= at + 20)
    {
        const uint8_t f = b[at + 13];
        std::string flags;
        const char* names[] = {"FIN", "SYN", "RST", "PSH", "ACK", "URG"};
        for (int i = 0; i < 6; ++i)
        {
            if (f & (1 << i))
                flags += (flags.empty() ? "" : " ") + std::string(names[i]);
        }
        const size_t offset = static_cast<size_t>(b[at + 12] >> 4) * 4;
        const size_t ipEnd = std::min(b.size(), static_cast<size_t>(14 + G16(b, 16)));
        TrafficDecodeNode tcp{"TCP " + std::to_string(G16(b, at)) + " > " + std::to_string(G16(b, at + 2)) + " [" + flags + "]",
                              {{"seq " + std::to_string(G32(b, at + 4)), {}}, {"ack " + std::to_string(G32(b, at + 8)), {}},
                               {"window " + std::to_string(G16(b, at + 14)), {}}}};
        if (ipEnd > at + offset)
            tcp.children.push_back({std::to_string(ipEnd - at - offset) + " bytes of data", {}});
        out.push_back(tcp);
    }
    return out;
}

std::string TrafficHexDump(const std::vector<uint8_t>& bytes)
{
    std::string out;
    char line[100];
    for (size_t at = 0; at < bytes.size(); at += 16)
    {
        int n = std::snprintf(line, sizeof(line), "%04zX  ", at);
        std::string ascii;
        for (size_t i = 0; i < 16; ++i)
        {
            if (at + i < bytes.size())
            {
                n += std::snprintf(line + n, sizeof(line) - static_cast<size_t>(n), "%02X ", bytes[at + i]);
                const uint8_t c = bytes[at + i];
                ascii.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.');
            }
            else
                n += std::snprintf(line + n, sizeof(line) - static_cast<size_t>(n), "   ");
            if (i == 7)
                n += std::snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " ");
        }
        out += line;
        out += " " + ascii + "\n";
    }
    return out;
}
