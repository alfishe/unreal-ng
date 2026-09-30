#include "emulator/io/network/vnet/dhcpserver.h"

namespace
{
constexpr size_t kBootpFixed = 236;  // op .. file, before the magic cookie
constexpr uint8_t kMagic[4] = {99, 130, 83, 99};

constexpr uint8_t kOptPad = 0;
constexpr uint8_t kOptMask = 1;
constexpr uint8_t kOptRouter = 3;
constexpr uint8_t kOptDns = 6;
constexpr uint8_t kOptRequestedIp = 50;
constexpr uint8_t kOptLeaseTime = 51;
constexpr uint8_t kOptMessageType = 53;
constexpr uint8_t kOptServerId = 54;
constexpr uint8_t kOptEnd = 255;

constexpr uint8_t kDiscover = 1;
constexpr uint8_t kOffer = 2;
constexpr uint8_t kRequest = 3;
constexpr uint8_t kAck = 5;
constexpr uint8_t kNak = 6;
constexpr uint8_t kInform = 8;

void Put32(std::vector<uint8_t>& out, size_t at, uint32_t v)
{
    out[at] = static_cast<uint8_t>(v >> 24);
    out[at + 1] = static_cast<uint8_t>(v >> 16);
    out[at + 2] = static_cast<uint8_t>(v >> 8);
    out[at + 3] = static_cast<uint8_t>(v);
}

void Option32(std::vector<uint8_t>& out, uint8_t code, uint32_t v)
{
    out.push_back(code);
    out.push_back(4);
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}
} // namespace

uint32_t DhcpServer::LeaseFor(const Mac& mac)
{
    auto it = _leases.find(mac);
    if (it != _leases.end())
        return it->second;
    if (_leases.size() >= _settings.leaseCount)
        return 0;
    const uint32_t addr = _settings.firstLease + static_cast<uint32_t>(_leases.size());
    _leases[mac] = addr;
    return addr;
}

std::vector<uint8_t> DhcpServer::Handle(const uint8_t* request, size_t length)
{
    std::vector<uint8_t> reply;
    if (!request || length < kBootpFixed || request[0] != 1 /* BOOTREQUEST */ || request[1] != 1 /* Ethernet */ ||
        request[2] != 6)
        return reply;

    // Message type and requested address from the options (Pad allowed on input)
    uint8_t type = 0;
    uint32_t requested = 0;
    if (length >= kBootpFixed + 4 && request[kBootpFixed] == kMagic[0] && request[kBootpFixed + 1] == kMagic[1] &&
        request[kBootpFixed + 2] == kMagic[2] && request[kBootpFixed + 3] == kMagic[3])
    {
        size_t pos = kBootpFixed + 4;
        while (pos < length)
        {
            const uint8_t code = request[pos++];
            if (code == kOptPad)
                continue;
            if (code == kOptEnd || pos >= length)
                break;
            const uint8_t len = request[pos++];
            if (pos + len > length)
                break;
            if (code == kOptMessageType && len >= 1)
                type = request[pos];
            else if (code == kOptRequestedIp && len == 4)
                requested = (static_cast<uint32_t>(request[pos]) << 24) | (request[pos + 1] << 16) |
                            (request[pos + 2] << 8) | request[pos + 3];
            pos += len;
        }
    }

    Mac mac{};
    for (size_t i = 0; i < mac.size(); ++i)
        mac[i] = request[28 + i];

    uint8_t answer = 0;
    uint32_t addr = LeaseFor(mac);
    switch (type)
    {
        case kDiscover:
            answer = kOffer;
            break;
        case kRequest:
            answer = (addr != 0 && (requested == 0 || requested == addr)) ? kAck : kNak;
            break;
        case kInform:
            answer = kAck;
            break;
        case 0:
            answer = kAck;  // plain BOOTP: give the address
            break;
        default:
            return reply;   // RELEASE, DECLINE: nothing to send
    }
    if (addr == 0)
        answer = kNak;

    // Fixed part: copy the request, then fill in the reply fields
    reply.assign(request, request + kBootpFixed);
    reply[0] = 2;       // BOOTREPLY
    reply[3] = 0;       // hops
    for (size_t i = 12; i < 24; ++i)
        reply[i] = 0;   // ciaddr (kept below for INFORM), yiaddr, siaddr
    if (type == kInform)
        std::copy(request + 12, request + 16, reply.begin() + 12);
    else if (answer != kNak)
        Put32(reply, 16, addr);                  // yiaddr
    Put32(reply, 20, _settings.serverAddr);      // siaddr
    for (size_t i = 44; i < kBootpFixed; ++i)
        reply[i] = 0;   // sname, file

    reply.insert(reply.end(), std::begin(kMagic), std::end(kMagic));
    reply.push_back(kOptMessageType);
    reply.push_back(1);
    reply.push_back(answer);
    Option32(reply, kOptServerId, _settings.serverAddr);
    if (answer != kNak)
    {
        if (type != kInform)
            Option32(reply, kOptLeaseTime, _settings.leaseSeconds);
        Option32(reply, kOptMask, _settings.mask);
        Option32(reply, kOptRouter, _settings.serverAddr);
        Option32(reply, kOptDns, _settings.dnsAddr);
    }
    reply.push_back(kOptEnd);

    // Minimum BOOTP size (300 bytes) with End-padding zeros after the End option
    while (reply.size() < 300)
        reply.push_back(0);
    return reply;
}
