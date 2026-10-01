#include "emulator/io/serial/esp/espnetmodule.h"

#include <algorithm>
#include <cstring>

#include "emulator/io/network/virtualnetwork.h"

namespace
{
constexpr size_t kReqHeader = 6;   // cmd, sock, arg, seq, len16
constexpr size_t kSockaddr = 15;   // family, port BE, ip[4], zero[8]
constexpr uint64_t kScanUs = 1200000;          // a scan takes about a second
constexpr uint64_t kUartSwitchUs = 50000;      // UART SET: the new rate 50 ms after the reply
constexpr uint8_t kChipEsp32 = 32, kChipEsp8266 = 86;

bool ParseIpv4(const std::string& s, uint32_t& out)
{
    uint32_t addr = 0;
    int parts = 0;
    size_t i = 0;
    while (parts < 4)
    {
        if (i >= s.size() || s[i] < '0' || s[i] > '9')
            return false;
        uint32_t v = 0;
        size_t digits = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && digits < 4)
        {
            v = v * 10 + static_cast<uint32_t>(s[i] - '0');
            ++i;
            ++digits;
        }
        if (v > 255)
            return false;
        addr = (addr << 8) | v;
        ++parts;
        if (parts < 4)
        {
            if (i >= s.size() || s[i] != '.')
                return false;
            ++i;
        }
    }
    if (i != s.size())
        return false;
    out = addr;
    return true;
}

void Put16(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

void Put32Be(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void PutSsid(std::vector<uint8_t>& out, const std::string& ssid)
{
    // 33 bytes: up to 32 characters, NUL padded
    for (size_t i = 0; i < 33; ++i)
        out.push_back(i < ssid.size() && i < 32 ? static_cast<uint8_t>(ssid[i]) : 0);
}
}  // namespace

EspnetModule::EspnetModule(VirtualNetwork* network, Chip chip)
    : EspModule(network, chip, chip == Chip::Esp8266 ? 4 : 8)
{
}

NetEndpoint EspnetModule::ParseSockaddr(const uint8_t* p)
{
    // The family byte is ignored (the firmware does too); port big-endian
    return NetEndpoint{NetIp(p[3], p[4], p[5], p[6]), static_cast<uint16_t>((p[1] << 8) | p[2])};
}

void EspnetModule::PutSockaddr(std::vector<uint8_t>& out, const NetEndpoint& e)
{
    out.push_back(2);   // AF_INET
    out.push_back(static_cast<uint8_t>(e.port >> 8));
    out.push_back(static_cast<uint8_t>(e.port));
    Put32Be(out, e.addr);
    out.insert(out.end(), 8, 0);
}

bool EspnetModule::NextRequest(Request& out)
{
    while (true)
    {
        // Outside a frame every byte but SOF is ignored
        while (!_rx.empty() && _rx.front() != kSof)
            _rx.pop_front();
        if (_rx.size() < 1 + kReqHeader)
            return false;
        const uint16_t len = static_cast<uint16_t>(_rx[5] | (_rx[6] << 8));
        if (len > kMaxPayload)
        {
            // An impossible length: drop the SOF and look for the next one
            _rx.pop_front();
            continue;
        }
        const bool crc = (_rx[1] & kCrcFlag) != 0;
        const size_t total = 1 + kReqHeader + len + (crc ? 1 : 0);
        if (_rx.size() < total)
            return false;   // the rest of the frame is still on its way (no inter-byte timeout)
        out.cmd = _rx[1];
        out.sock = _rx[2];
        out.arg = _rx[3];
        out.seq = _rx[4];
        out.crc = crc;
        out.payload.assign(_rx.begin() + 1 + kReqHeader, _rx.begin() + 1 + kReqHeader + len);
        bool good = true;
        if (crc)
        {
            uint8_t x = 0;
            for (size_t i = 1; i < 1 + kReqHeader + len; ++i)
                x = static_cast<uint8_t>(x ^ _rx[i]);
            good = x == _rx[total - 1];
        }
        _rx.erase(_rx.begin(), _rx.begin() + static_cast<std::ptrdiff_t>(total));
        if (good)
            return true;
        // A bad CRC: the frame is dropped, no reply
    }
}

namespace
{
const char* CommandName(uint8_t cmd)
{
    switch (cmd & 0x7F)
    {
        case EspnetModule::kSocket: return "SOCKET";
        case EspnetModule::kShutdown: return "SHUTDOWN";
        case EspnetModule::kConnect: return "CONNECT";
        case EspnetModule::kAccept: return "ACCEPT";
        case EspnetModule::kBind: return "BIND";
        case EspnetModule::kListen: return "LISTEN";
        case EspnetModule::kRead: return "READ";
        case EspnetModule::kWrite: return "WRITE";
        case EspnetModule::kGetDns: return "GETDNS";
        case EspnetModule::kDnsResolve: return "DNSRESOLVE";
        case EspnetModule::kInfo: return "INFO";
        case EspnetModule::kWifiScan: return "WIFI_SCAN";
        case EspnetModule::kWifiConnect: return "WIFI_CONNECT";
        case EspnetModule::kWifiDisc: return "WIFI_DISC";
        case EspnetModule::kWifiStatus: return "WIFI_STATUS";
        case EspnetModule::kUart: return "UART";
        case EspnetModule::kEcho: return "ECHO";
        default: return "?";
    }
}
}  // namespace

void EspnetModule::Process()
{
    Request r;
    while (!_busy && NextRequest(r))
    {
        ++_requests;
        LogRequest(std::string(CommandName(r.cmd)) + " sock " + std::to_string(r.sock) + " arg " + std::to_string(r.arg) +
                   " seq " + std::to_string(r.seq) + " len " + std::to_string(r.payload.size()));
        Handle(r);
    }
}

void EspnetModule::Reply(const Request& r, uint8_t sock, uint8_t status, uint16_t result,
                         const std::vector<uint8_t>& payload, uint64_t turnaroundUs)
{
    std::vector<uint8_t> f;
    f.reserve(9 + payload.size() + 1);
    f.push_back(kSof);
    f.push_back(r.crc ? static_cast<uint8_t>(r.cmd | kCrcFlag) : static_cast<uint8_t>(r.cmd & 0x7F));
    f.push_back(sock);
    f.push_back(status);
    f.push_back(r.seq);
    Put16(f, result);
    Put16(f, static_cast<uint16_t>(payload.size()));
    f.insert(f.end(), payload.begin(), payload.end());
    LogReply("sock " + std::to_string(sock) + " status " + std::to_string(status) + " result " + std::to_string(result) +
             " len " + std::to_string(payload.size()));
    if (r.crc)
    {
        uint8_t x = 0;
        for (size_t i = 1; i < f.size(); ++i)
            x = static_cast<uint8_t>(x ^ f[i]);
        f.push_back(x);
    }
    Send(f, turnaroundUs);
}

void EspnetModule::Handle(const Request& r)
{
    switch (r.cmd & 0x7F)
    {
        case kSocket: DoSocket(r); break;
        case kShutdown: DoShutdown(r); break;
        case kConnect: DoConnect(r); break;
        case kAccept: DoAccept(r); break;
        case kBind: DoBind(r); break;
        case kListen: DoListen(r); break;
        case kRead: DoRead(r); break;
        case kWrite: DoWrite(r); break;
        case kGetDns: DoGetDns(r); break;
        case kDnsResolve: DoDnsResolve(r); break;
        case kInfo: DoInfo(r); break;
        case kWifiScan: DoWifiScan(r); break;
        case kWifiConnect: DoWifiConnect(r); break;
        case kWifiDisc:
            _held = true;
            Leave();
            Reply(r, kSockNone, kOk, 0);
            break;
        case kWifiStatus: DoWifiStatus(r); break;
        case kUart: DoUart(r); break;
        case kEcho:
            Reply(r, r.sock, kOk, static_cast<uint16_t>(r.payload.size()), r.payload);
            break;
        default:
            Error(r, r.sock, kProtoType);
            break;
    }
}

void EspnetModule::DoSocket(const Request& r)
{
    if (!r.payload.empty() && r.payload[0] != 2)
        return Error(r, kSockNone, kAfNoSupport);
    if (r.arg != 1 && r.arg != 3)
        return Error(r, kSockNone, kProtoType);   // ICMP (2) and anything else
    const int slot = Stack().Open(r.arg == 1);
    if (slot < 0)
        return Error(r, kSockNone, kNfile);
    Reply(r, static_cast<uint8_t>(slot), kOk, 0);
}

void EspnetModule::DoShutdown(const Request& r)
{
    if (r.sock == kSockNone)
    {
        Stack().Close(-1);
        return Reply(r, r.sock, kOk, 0);
    }
    if (!Stack().Valid(r.sock))
        return Error(r, r.sock, kNotSock);
    Stack().Close(r.sock);
    Reply(r, r.sock, kOk, 0);
}

void EspnetModule::DoConnect(const Request& r)
{
    if (!Stack().Valid(r.sock))
        return Error(r, r.sock, kNotSock);
    const EspStack::Slot& s = Stack().GetSlot(r.sock);
    if (s.state != EspStack::State::TcpIdle && s.state != EspStack::State::Tcp)
        return Error(r, r.sock, kNotSock);
    if (Stack().Established(r.sock))
        return Error(r, r.sock, kAlready);
    if (r.payload.size() < kSockaddr)
        return Error(r, r.sock, kIntr);
    if (GetWifi() != Wifi::GotIp)
        return Error(r, r.sock, kHostUnreach);
    _busy = true;
    _pending = r;
    _pending.payload.clear();
    Stack().Connect(r.sock, ParseSockaddr(r.payload.data()));
}

void EspnetModule::DoBind(const Request& r)
{
    if (!Stack().Valid(r.sock))
        return Error(r, r.sock, kNotSock);
    if (r.payload.size() < kSockaddr)
        return Error(r, r.sock, kIntr);
    Stack().Bind(r.sock, ParseSockaddr(r.payload.data()).port);
    Reply(r, r.sock, kOk, 0);
}

void EspnetModule::DoListen(const Request& r)
{
    if (!Stack().Valid(r.sock))
        return Error(r, r.sock, kNotSock);
    const EspStack::Slot& s = Stack().GetSlot(r.sock);
    if (s.state != EspStack::State::TcpIdle && s.state != EspStack::State::Tcp)
        return Error(r, r.sock, kNotSock);
    if (Stack().Established(r.sock))
        return Error(r, r.sock, kAlready);
    if (s.localPort == 0)
        return Error(r, r.sock, kNotSock);
    Stack().Listen(r.sock);
    Reply(r, r.sock, kOk, 0);
}

void EspnetModule::DoAccept(const Request& r)
{
    if (!Stack().Valid(r.sock) || Stack().GetSlot(r.sock).state != EspStack::State::Listen)
        return Error(r, r.sock, kNotSock);
    const int slot = Stack().Accept(r.sock);
    if (slot < 0)
        return Error(r, r.sock, kAgain);
    Reply(r, static_cast<uint8_t>(slot), kOk, 0);
}

void EspnetModule::DoRead(const Request& r)
{
    if (!Stack().Valid(r.sock))
        return Error(r, r.sock, kNotSock);
    uint32_t maxlen = r.payload.size() >= 2 ? static_cast<uint32_t>(r.payload[0] | (r.payload[1] << 8)) : kMaxPayload;
    maxlen = std::min<uint32_t>(maxlen, kMaxPayload);
    const EspStack::Slot& s = Stack().GetSlot(r.sock);
    if (s.state == EspStack::State::Udp)
    {
        EspStack::Datagram d;
        if (maxlen == 0 || !Stack().PopDatagram(r.sock, d))
            return Error(r, r.sock, kAgain);
        // One datagram per READ: source sockaddr, then what fits; the rest is gone
        const uint32_t n = std::min<uint32_t>({static_cast<uint32_t>(d.data.size()), maxlen, kMaxPayload - kSockaddr});
        std::vector<uint8_t> payload;
        PutSockaddr(payload, d.from);
        for (uint32_t i = 0; i < n; ++i)
            payload.push_back(d.data[i].value);
        return Reply(r, r.sock, kOk, static_cast<uint16_t>(n), payload);
    }
    if (maxlen == 0)
        return Error(r, r.sock, kAgain);
    if (s.rx.empty())
        return Error(r, r.sock, Stack().Established(r.sock) ? kAgain : kNotConn);
    const std::vector<uint8_t> data = Stack().Read(r.sock, maxlen);
    Reply(r, r.sock, kOk, static_cast<uint16_t>(data.size()), data);
}

void EspnetModule::DoWrite(const Request& r)
{
    if (!Stack().Valid(r.sock))
        return Error(r, r.sock, kNotSock);
    const EspStack::Slot& s = Stack().GetSlot(r.sock);
    if (s.state == EspStack::State::Udp)
    {
        if (r.payload.size() < kSockaddr)
            return Error(r, r.sock, kMsgSize);
        const NetEndpoint to = ParseSockaddr(r.payload.data());
        const uint32_t n = static_cast<uint32_t>(r.payload.size() - kSockaddr);
        Stack().SendTo(r.sock, to, r.payload.data() + kSockaddr, n);
        if (n == 0)
            return Error(r, r.sock, kMsgSize);   // the empty datagram went out, the status says EMSGSIZE
        return Reply(r, r.sock, kOk, static_cast<uint16_t>(n));
    }
    if (r.payload.empty())
        return Error(r, r.sock, kMsgSize);
    if (!Stack().Established(r.sock))
        return Error(r, r.sock, kNotConn);
    Stack().Send(r.sock, r.payload.data(), static_cast<uint32_t>(r.payload.size()));
    Reply(r, r.sock, kOk, static_cast<uint16_t>(r.payload.size()));
}

void EspnetModule::DoGetDns(const Request& r)
{
    std::vector<uint8_t> payload;
    const uint32_t dns = GetWifi() == Wifi::GotIp ? DnsServer() : 0;
    Put32Be(payload, dns);
    Reply(r, kSockNone, kOk, 0, payload);
}

void EspnetModule::DoDnsResolve(const Request& r)
{
    if (r.payload.empty() || r.payload.size() > 64 || GetWifi() != Wifi::GotIp)
        return Error(r, kSockNone, kHostUnreach);
    const std::string name(r.payload.begin(), r.payload.end());
    uint32_t addr = 0;
    if (ParseIpv4(name, addr))
    {
        // hostByName answers an address without asking
        std::vector<uint8_t> payload;
        Put32Be(payload, addr);
        return Reply(r, kSockNone, kOk, 0, payload);
    }
    _busy = true;
    _pending = r;
    _pending.payload.clear();
    Stack().Resolve(name);
}

void EspnetModule::OnStackDone(const EspStack::Done& done)
{
    if (!_busy)
        return;
    const Request r = _pending;
    _busy = false;
    if (done.kind == EspStack::Done::Kind::Connect)
    {
        if (done.status == NetEventStatus::Ok)
            Reply(r, r.sock, kOk, 0);
        else
            Error(r, r.sock, kHostUnreach);
        return;
    }
    if (done.status != NetEventStatus::Ok || done.addr == 0)
        return Error(r, kSockNone, kHostUnreach);
    std::vector<uint8_t> payload;
    Put32Be(payload, done.addr);
    Reply(r, kSockNone, kOk, 0, payload);
}

void EspnetModule::DoInfo(const Request& r)
{
    std::vector<uint8_t> p;
    p.push_back(kVersionMajor);
    p.push_back(kVersionMinor);
    p.push_back(GetChip() == Chip::Esp8266 ? kChipEsp8266 : kChipEsp32);
    p.push_back(static_cast<uint8_t>(Stack().SlotCount()));
    p.push_back(static_cast<uint8_t>(GetWifi()));
    p.push_back(GetWifi() == Wifi::GotIp ? static_cast<uint8_t>(kVirtualRssi) : 0);
    p.push_back(Stack().SlotMask());
    p.push_back(0x01);   // caps: CRC capable
    Put32Be(p, Ip());
    p.insert(p.end(), Mac().begin(), Mac().end());
    PutSsid(p, Ssid());
    Put16(p, GetChip() == Chip::Esp8266 ? 34464 : 65535);   // free heap, clamped
    Reply(r, kSockNone, kOk, 0, p);
}

void EspnetModule::DoWifiScan(const Request& r)
{
    // One access point: the virtual one. A scan drops the station and joins
    // the saved access point again afterwards (unless WIFI_DISC holds it down)
    std::vector<uint8_t> p;
    PutSsid(p, kVirtualSsid);
    p.push_back(static_cast<uint8_t>(kVirtualRssi));
    p.push_back(3);   // WPA2_PSK
    const std::array<uint8_t, 6> bssid{0x52, 0x54, 0x00, 0x12, 0x35, 0x02};
    p.insert(p.end(), bssid.begin(), bssid.end());
    p.push_back(kVirtualChannel);
    const std::string saved = Ssid();
    Leave();
    if (!_held)
        Join(saved);
    Reply(r, kSockNone, kOk, 1, p, kScanUs);
}

void EspnetModule::DoWifiConnect(const Request& r)
{
    if (r.payload.size() < 98)
        return Error(r, kSockNone, kMsgSize);
    std::string ssid(reinterpret_cast<const char*>(r.payload.data()), strnlen(reinterpret_cast<const char*>(r.payload.data()), 32));
    _held = false;
    Join(ssid);
    Reply(r, kSockNone, kOk, 0);   // acknowledged at once; the join goes on in the background
}

void EspnetModule::DoWifiStatus(const Request& r)
{
    std::vector<uint8_t> p;
    const bool up = GetWifi() == Wifi::GotIp;
    Put32Be(p, Ip());
    p.insert(p.end(), Mac().begin(), Mac().end());
    PutSsid(p, Ssid());
    p.push_back(up ? static_cast<uint8_t>(kVirtualRssi) : 0);
    p.push_back(static_cast<uint8_t>((up ? 0x01 : 0) | (Ip() != 0 ? 0x02 : 0)));
    Reply(r, kSockNone, kOk, 0, p);
}

void EspnetModule::DoUart(const Request& r)
{
    auto answer = [&](uint32_t baud, uint8_t flags) {
        std::vector<uint8_t> p;
        Put16(p, static_cast<uint16_t>(baud));
        Put16(p, static_cast<uint16_t>(baud >> 16));
        p.push_back(flags);
        p.insert(p.end(), 3, 0);
        Reply(r, kSockNone, kOk, 0, p);
    };
    if (r.arg == 0 && r.payload.empty())
        return answer(Baud(), 0x01);
    if (r.arg != 1 || r.payload.size() != 8)
        return Error(r, kSockNone, kMsgSize);
    const uint32_t baud = static_cast<uint32_t>(r.payload[0] | (r.payload[1] << 8) | (r.payload[2] << 16) |
                                                (static_cast<uint32_t>(r.payload[3]) << 24));
    if (baud != 9600 && baud != 19200 && baud != 38400 && baud != 57600 && baud != 115200)
        return Error(r, kSockNone, kMsgSize);
    // The reply goes out at the old rate, then the module switches
    answer(baud, static_cast<uint8_t>(r.payload[4] & 0x01));
    SetBaud(baud, Now() + MicrosToT(kTurnaroundUs + kUartSwitchUs));
}

void EspnetModule::SaveFirmware(netstate::EspModuleState& out) const
{
    out.firmware[0] = _busy ? 1 : 0;
    out.firmware[1] = _pending.cmd;
    out.firmware[2] = _pending.sock;
    out.firmware[3] = _pending.arg;
    out.firmware[4] = _pending.seq;
    out.firmware[5] = _pending.crc ? 1 : 0;
    out.firmware[6] = _held ? 1 : 0;
}

void EspnetModule::LoadFirmware(const netstate::EspModuleState& in)
{
    _busy = in.firmware[0] != 0;
    _pending = Request();
    _pending.cmd = in.firmware[1];
    _pending.sock = in.firmware[2];
    _pending.arg = in.firmware[3];
    _pending.seq = in.firmware[4];
    _pending.crc = in.firmware[5] != 0;
    _held = in.firmware[6] != 0;
}
