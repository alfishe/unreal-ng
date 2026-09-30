#include "emulator/io/network/w5300.h"

#include <algorithm>
#include <cstring>

#include "emulator/io/network/virtualnetwork.h"

namespace
{
// Common register offsets (reference-w5300-model.md §2)
constexpr uint16_t kMr0 = 0x000;
constexpr uint16_t kMr1 = 0x001;
constexpr uint16_t kIr0 = 0x002;
constexpr uint16_t kIr1 = 0x003;
constexpr uint16_t kImr1 = 0x005;
constexpr uint16_t kRtr0 = 0x01C;
constexpr uint16_t kRtr1 = 0x01D;
constexpr uint16_t kRcr = 0x01F;
constexpr uint16_t kTmsr = 0x020;
constexpr uint16_t kRmsr = 0x028;
constexpr uint16_t kMtyper0 = 0x030;
constexpr uint16_t kMtyper1 = 0x031;
constexpr uint16_t kPtimer = 0x037;
constexpr uint16_t kIdr0 = 0x0FE;
constexpr uint16_t kIdr1 = 0x0FF;

constexpr uint8_t kMrRst = 0x80;

// Socket register offsets (§3)
constexpr uint16_t kSnMr0 = 0x00;
constexpr uint16_t kSnMr1 = 0x01;
constexpr uint16_t kSnCr = 0x03;
constexpr uint16_t kSnImr = 0x05;
constexpr uint16_t kSnIr = 0x07;
constexpr uint16_t kSnSsr = 0x09;
constexpr uint16_t kSnPortr = 0x0A;
constexpr uint16_t kSnDhar = 0x0C;
constexpr uint16_t kSnDportr = 0x12;
constexpr uint16_t kSnDipr = 0x14;
constexpr uint16_t kSnMssr = 0x18;
constexpr uint16_t kSnKpalvtr = 0x1A;
constexpr uint16_t kSnProtor = 0x1B;
constexpr uint16_t kSnTosr = 0x1D;
constexpr uint16_t kSnTtlr = 0x1F;
constexpr uint16_t kSnWrsr = 0x20;
constexpr uint16_t kSnFsr = 0x24;
constexpr uint16_t kSnRsr = 0x28;
constexpr uint16_t kSnFragr = 0x2D;
constexpr uint16_t kSnTxFifo0 = 0x2E;
constexpr uint16_t kSnTxFifo1 = 0x2F;
constexpr uint16_t kSnRxFifo0 = 0x30;
constexpr uint16_t kSnRxFifo1 = 0x31;

/// Byte `index` (0 = at the register's lowest address) of a 32-bit field
/// holding a 17-bit count: #x0 = 0, #x1 = bit 16, #x2 = bits 15..8, #x3 = bits 7..0
uint8_t CountByte(uint32_t value, uint16_t index)
{
    switch (index)
    {
        case 1: return static_cast<uint8_t>((value >> 16) & 0x01);
        case 2: return static_cast<uint8_t>(value >> 8);
        case 3: return static_cast<uint8_t>(value);
        default: return 0;
    }
}
} // namespace

W5300::W5300(VirtualNetwork* network) : _network(network)
{
    Reset();
}

W5300::~W5300()
{
    for (int n = 0; n < kSockets; ++n)
        Release(n);
}

void W5300::SetNetwork(VirtualNetwork* network)
{
    for (int n = 0; n < kSockets; ++n)
        Release(n);
    _network = network;
    Reset();
}

void W5300::Reset()
{
    for (int n = 0; n < kSockets; ++n)
        Release(n);

    _common.fill(0);
    _common[kMr0] = 0x38;   // MR = #3800: WDF / RDH defaults, 8-bit bus
    _common[kRtr0] = 0x07;  // RTR = #07D0 (200 ms)
    _common[kRtr1] = 0xD0;
    _common[kRcr] = 0x08;
    for (int i = 0; i < 8; ++i)
    {
        _common[kTmsr + i] = 8;  // 8 KB TX and RX per socket
        _common[kRmsr + i] = 8;
    }
    _common[kMtyper0] = 0x00;
    _common[kMtyper1] = 0xFF;
    _common[kPtimer] = 0x28;
    _common[kIdr0] = 0x53;   // IDR = #5300
    _common[kIdr1] = 0x00;

    for (Socket& s : _sockets)
        s = Socket();
}

bool W5300::InterruptActive() const
{
    const uint8_t imr = _common[kImr1];
    for (int n = 0; n < kSockets; ++n)
    {
        if ((_sockets[n].ir & _sockets[n].imr) && (imr & (1u << n)))
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Bus
// ---------------------------------------------------------------------------

uint8_t W5300::Read(uint16_t address)
{
    address &= 0x3FF;
    if (address < 0x100)
        return ReadCommon(address);
    if (address < 0x200)
        return 0;  // reserved
    return ReadSocket((address - 0x200) >> 6, address & 0x3F);
}

void W5300::Write(uint16_t address, uint8_t value)
{
    address &= 0x3FF;
    if (address < 0x100)
        WriteCommon(address, value);
    else if (address >= 0x200)
        WriteSocket((address - 0x200) >> 6, address & 0x3F, value);
}

uint8_t W5300::ReadCommon(uint16_t offset) const
{
    if (offset == kIr1)
    {
        uint8_t bits = 0;
        for (int n = 0; n < kSockets; ++n)
        {
            if (_sockets[n].ir)
                bits |= static_cast<uint8_t>(1u << n);
        }
        return bits;
    }
    return _common[offset];
}

void W5300::WriteCommon(uint16_t offset, uint8_t value)
{
    switch (offset)
    {
        case kMr1:
            if (value & kMrRst)
            {
                Reset();   // software reset; RST reads back 0 when done (at once here)
                return;
            }
            _common[kMr1] = value;
            return;
        case kMr0:
            _common[kMr0] = static_cast<uint8_t>((value & 0x3F) | (_common[kMr0] & 0xC0));  // DBW, MPF read-only
            return;
        case kIr0:
            _common[kIr0] = static_cast<uint8_t>(_common[kIr0] & ~value);  // write 1 to clear
            return;
        case kIr1:
            return;    // socket bits follow Sn_IR
        case kIdr0:
        case kIdr1:
            return;    // read-only
        default:
            _common[offset] = value;
            return;
    }
}

uint32_t W5300::TxFree(const Socket& s) const
{
    const size_t used = s.tx.size() + (s.txHalf ? 1 : 0);
    return used >= s.txSize ? 0 : static_cast<uint32_t>(s.txSize - used);
}

NetEndpoint W5300::Destination(const Socket& s) const
{
    NetEndpoint e;
    e.addr = (static_cast<uint32_t>(s.dipr[0]) << 24) | (static_cast<uint32_t>(s.dipr[1]) << 16) |
             (static_cast<uint32_t>(s.dipr[2]) << 8) | s.dipr[3];
    e.port = static_cast<uint16_t>((s.dportr[0] << 8) | s.dportr[1]);
    return e;
}

void W5300::SetDestination(Socket& s, const NetEndpoint& peer)
{
    s.dipr[0] = static_cast<uint8_t>(peer.addr >> 24);
    s.dipr[1] = static_cast<uint8_t>(peer.addr >> 16);
    s.dipr[2] = static_cast<uint8_t>(peer.addr >> 8);
    s.dipr[3] = static_cast<uint8_t>(peer.addr);
    s.dportr[0] = static_cast<uint8_t>(peer.port >> 8);
    s.dportr[1] = static_cast<uint8_t>(peer.port);
}

uint8_t W5300::ReadSocket(int n, uint16_t offset)
{
    Socket& s = _sockets[n];
    switch (offset)
    {
        case kSnMr0: return s.mr[0];
        case kSnMr1: return s.mr[1];
        case kSnCr: return 0;   // every command is taken at once
        case kSnImr: return s.imr;
        case kSnIr: return s.ir;
        case kSnSsr: return s.ssr;
        case kSnPortr: return s.portr[0];
        case kSnPortr + 1: return s.portr[1];
        case kSnDportr: return s.dportr[0];
        case kSnDportr + 1: return s.dportr[1];
        case kSnMssr: return s.mssr[0];
        case kSnMssr + 1: return s.mssr[1];
        case kSnKpalvtr: return s.kpalvtr;
        case kSnProtor: return s.protor;
        case kSnTosr: return s.tosr;
        case kSnTtlr: return s.ttlr;
        case kSnFragr: return s.fragr;

        case kSnRxFifo0:
            // Earlier stream byte of the next word; the pointer moves on FIFOR1
            return s.rxBytes ? RxByteAt(s, 0) : 0;
        case kSnRxFifo1:
        {
            if (s.rxBytes < 2)
            {
                ClearRx(s);
                return 0;
            }
            const uint8_t value = RxByteAt(s, 1);
            RxConsume(s, 2);
            return value;
        }

        case kSnTxFifo0:
        case kSnTxFifo1:
            return 0;   // write-only unless MR.MT
        default:
            break;
    }

    if (offset >= kSnDhar && offset < kSnDhar + 6)
        return s.dhar[offset - kSnDhar];
    if (offset >= kSnDipr && offset < kSnDipr + 4)
        return s.dipr[offset - kSnDipr];
    if (offset >= kSnWrsr && offset < kSnWrsr + 4)
        return CountByte(s.wrsr, static_cast<uint16_t>(offset - kSnWrsr));
    if (offset >= kSnFsr && offset < kSnFsr + 4)
        return CountByte(TxFree(s), static_cast<uint16_t>(offset - kSnFsr));
    if (offset >= kSnRsr && offset < kSnRsr + 4)
        return CountByte(s.rxBytes, static_cast<uint16_t>(offset - kSnRsr));
    return 0;
}

void W5300::WriteSocket(int n, uint16_t offset, uint8_t value)
{
    Socket& s = _sockets[n];
    switch (offset)
    {
        case kSnMr0: s.mr[0] = value; return;
        case kSnMr1: s.mr[1] = value; return;
        case kSnCr: Command(n, value); return;
        case kSnImr: s.imr = value; return;
        case kSnIr: s.ir = static_cast<uint8_t>(s.ir & ~value); return;   // write 1 to clear
        case kSnPortr: s.portr[0] = value; return;
        case kSnPortr + 1: s.portr[1] = value; return;
        case kSnDportr: s.dportr[0] = value; return;
        case kSnDportr + 1: s.dportr[1] = value; return;
        case kSnMssr: s.mssr[0] = value; return;
        case kSnMssr + 1: s.mssr[1] = value; return;
        case kSnKpalvtr: s.kpalvtr = value; return;
        case kSnProtor: s.protor = value; return;
        case kSnTosr: s.tosr = value; return;
        case kSnTtlr: s.ttlr = value; return;
        case kSnFragr: s.fragr = value; return;

        case kSnTxFifo0:
            s.txLatch = value;
            s.txHalf = true;
            return;
        case kSnTxFifo1:
            // The word is stored with its second byte; a full memory drops it
            // (the real chip would wrap over unsent data)
            if (s.tx.size() + 2 <= s.txSize)
            {
                s.tx.push_back(s.txHalf ? s.txLatch : 0);
                s.tx.push_back(value);
            }
            s.txHalf = false;
            return;
        default:
            break;
    }

    if (offset >= kSnDhar && offset < kSnDhar + 6)
    {
        s.dhar[offset - kSnDhar] = value;
        return;
    }
    if (offset >= kSnDipr && offset < kSnDipr + 4)
    {
        s.dipr[offset - kSnDipr] = value;
        return;
    }
    if (offset >= kSnWrsr && offset < kSnWrsr + 4)
    {
        switch (offset - kSnWrsr)
        {
            case 1: s.wrsr = (s.wrsr & 0x0FFFFu) | (static_cast<uint32_t>(value & 0x01) << 16); break;
            case 2: s.wrsr = (s.wrsr & 0x100FFu) | (static_cast<uint32_t>(value) << 8); break;
            case 3: s.wrsr = (s.wrsr & 0x1FF00u) | value; break;
            default: break;
        }
    }
    // FSR, RSR and SSR are read-only
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void W5300::SetIr(int n, uint8_t bits)
{
    // Sn_IMR gates the setting of Sn_IR bits (datasheet p.74)
    _sockets[n].ir |= static_cast<uint8_t>(bits & _sockets[n].imr);
}

void W5300::Release(int n)
{
    Socket& s = _sockets[n];
    if (s.net && _network)
        _network->Close(s.net);
    s.net = 0;
}

void W5300::Open(int n)
{
    Socket& s = _sockets[n];
    Release(n);
    s.tx.clear();
    s.txHalf = false;
    ClearRx(s);
    s.backlog.clear();
    s.backlogBytes = 0;
    s.wrsr = 0;
    s.txSize = std::min<uint32_t>(_common[kTmsr + n], 64) * 1024u;
    s.rxSize = std::min<uint32_t>(_common[kRmsr + n], 64) * 1024u;

    const uint8_t mode = s.mr[1] & 0x0F;
    NetProto proto;
    switch (mode)
    {
        case kModeTcp:
            proto = NetProto::Tcp;
            s.ssr = kSockInit;
            break;
        case kModeUdp:
            proto = NetProto::Udp;
            s.ssr = kSockUdp;
            break;
        case kModeIpRaw:
            proto = NetProto::Icmp;   // raw IP: ICMP (PROTOR 1) is the one protocol served
            s.ssr = kSockIpRaw;
            break;
        default:
            s.ssr = kSockClosed;      // closed / MACRAW / PPPoE: not served
            return;
    }
    if (_network)
        s.net = _network->Open(proto, this, static_cast<uint32_t>(n));
}

void W5300::Send(int n)
{
    Socket& s = _sockets[n];
    const size_t count = std::min<size_t>(s.wrsr, s.tx.size());
    std::vector<uint8_t> data(s.tx.begin(), s.tx.begin() + static_cast<std::ptrdiff_t>(count));
    // The pad byte of an odd count is never transmitted
    const size_t consumed = std::min(s.tx.size(), count + (count & 1));
    s.tx.erase(s.tx.begin(), s.tx.begin() + static_cast<std::ptrdiff_t>(consumed));

    switch (s.ssr)
    {
        case kSockEstablished:
        case kSockCloseWait:
            if (_network && s.net && !data.empty())
                _network->Send(s.net, data.data(), static_cast<uint32_t>(data.size()));
            break;
        case kSockUdp:
        case kSockIpRaw:
            if (_network && s.net)
                _network->SendTo(s.net, static_cast<uint16_t>((s.portr[0] << 8) | s.portr[1]), Destination(s),
                                 data.data(), static_cast<uint32_t>(data.size()));
            break;
        default:
            break;
    }
    SetIr(n, kIrSendOk);   // sent at once (the host keeps what it could not send yet)
}

void W5300::Command(int n, uint8_t cmd)
{
    Socket& s = _sockets[n];
    switch (cmd)
    {
        case kCmdOpen:
            if (s.ssr == kSockClosed)
                Open(n);
            break;

        case kCmdListen:
            if (s.ssr == kSockInit)
            {
                s.ssr = kSockListen;
                if (_network && s.net)
                    _network->Listen(s.net, static_cast<uint16_t>((s.portr[0] << 8) | s.portr[1]));
            }
            break;

        case kCmdConnect:
            if (s.ssr == kSockInit)
            {
                s.ssr = kSockSynSent;
                if (_network && s.net)
                    _network->Connect(s.net, Destination(s));
                else
                {
                    s.ssr = kSockClosed;   // no network: the chip's ARP / TCP timeout, early
                    SetIr(n, kIrTimeout);
                }
            }
            break;

        case kCmdDiscon:
            if (s.ssr == kSockEstablished)
            {
                s.ssr = kSockFinWait;   // CLOSED when the peer's FIN arrives
                if (_network && s.net)
                    _network->ShutdownWrite(s.net);
            }
            else if (s.ssr == kSockCloseWait)
            {
                // The peer already closed: our FIN completes the close
                if (_network && s.net)
                    _network->ShutdownWrite(s.net);
                Release(n);
                s.ssr = kSockClosed;
                SetIr(n, kIrDiscon);
            }
            break;

        case kCmdClose:
            Release(n);
            s.ssr = kSockClosed;
            s.tx.clear();
            s.txHalf = false;
            s.backlog.clear();
            s.backlogBytes = 0;
            break;

        case kCmdSend:
        case kCmdSendMac:
            Send(n);
            break;

        case kCmdRecv:
            // The program took a packet: room for more TCP data
            FillFromBacklog(n);
            break;

        default:
            break;   // SEND_KEEP, PPPoE commands, unknown: accepted, no effect
    }
}

// ---------------------------------------------------------------------------
// Receive path
// ---------------------------------------------------------------------------

uint8_t W5300::RxByteAt(const Socket& s, uint32_t position)
{
    position += s.rxRead;
    for (const Socket::RxPacket& p : s.rx)
    {
        const uint32_t size = p.Size();
        if (position < size)
        {
            if (position < p.headerLength)
                return p.header[position];
            position -= p.headerLength;
            return position < p.data.size() ? p.data[position] : 0;   // the pad byte
        }
        position -= size;
    }
    return 0;
}

void W5300::RxConsume(Socket& s, uint32_t count)
{
    count = std::min(count, s.rxBytes);
    s.rxBytes -= count;
    s.rxRead += count;
    while (!s.rx.empty() && s.rxRead >= s.rx.front().Size())
    {
        s.rxRead -= s.rx.front().Size();
        s.rx.pop_front();
    }
    if (s.rx.empty())
        s.rxRead = 0;
}

void W5300::ClearRx(Socket& s)
{
    s.rx.clear();
    s.rxRead = 0;
    s.rxBytes = 0;
}

void W5300::QueuePacket(int n, const uint8_t* header, size_t headerLength, const uint8_t* data, size_t length,
                        uint32_t source, uint32_t sourceOffset)
{
    Socket& s = _sockets[n];
    Socket::RxPacket p;
    std::memcpy(p.header, header, std::min<size_t>(headerLength, sizeof(p.header)));
    p.headerLength = static_cast<uint8_t>(headerLength);
    if (data && length)
        p.data.assign(data, data + length);
    p.source = source;
    p.sourceOffset = sourceOffset;
    s.rxBytes += p.Size();
    s.rx.push_back(std::move(p));
    SetIr(n, kIrRecv);
}

void W5300::FillFromBacklog(int n)
{
    // One packet never spans two arriving chunks (the chip keeps TCP
    // segments apart too), so each packet names a single source range
    Socket& s = _sockets[n];
    while (!s.backlog.empty())
    {
        Socket::Chunk& chunk = s.backlog.front();
        if (s.rxBytes + 2 + 2 > s.rxSize)
            return;
        size_t room = s.rxSize - s.rxBytes - 2;   // after the size header
        room &= ~static_cast<size_t>(1);          // keep space for the pad
        const size_t length = std::min<size_t>({chunk.data.size(), static_cast<size_t>(kTcpMss), room});
        if (length == 0)
            return;
        const uint8_t header[2] = {static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length)};
        QueuePacket(n, header, 2, chunk.data.data(), length, chunk.source, chunk.sourceOffset);
        s.backlogBytes -= length;
        if (length == chunk.data.size())
        {
            s.backlog.pop_front();
        }
        else
        {
            chunk.data.erase(chunk.data.begin(), chunk.data.begin() + static_cast<std::ptrdiff_t>(length));
            chunk.sourceOffset += static_cast<uint32_t>(length);
        }
    }
}

void W5300::OnNetEvent(uint32_t cookie, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
                       const uint8_t* data, uint32_t length, uint32_t source)
{
    TakeNetEvent(cookie, type, status, peer, data, length, source);
    if (onNetEvent)
        onNetEvent();
}

void W5300::TakeNetEvent(uint32_t cookie, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
                         const uint8_t* data, uint32_t length, uint32_t source)
{
    if (cookie >= kSockets)
        return;
    const int n = static_cast<int>(cookie);
    Socket& s = _sockets[n];

    switch (type)
    {
        case NetEventType::Connected:
            if (s.ssr == kSockSynSent)
            {
                s.ssr = kSockEstablished;
                SetIr(n, kIrCon);
            }
            break;

        case NetEventType::Accepted:
            if (s.ssr == kSockListen)
            {
                SetDestination(s, peer);
                s.ssr = kSockEstablished;
                SetIr(n, kIrCon);
            }
            break;

        case NetEventType::ConnectFailed:
            if (s.ssr == kSockSynSent)
            {
                Release(n);
                s.ssr = kSockClosed;
                // A refused connect is an RST: no interrupt bit; timeouts and
                // unreachable hosts are the chip's ARP / TCP timeout
                if (status != NetEventStatus::Refused)
                    SetIr(n, kIrTimeout);
            }
            break;

        case NetEventType::Data:
            if (s.ssr == kSockEstablished || s.ssr == kSockFinWait)
            {
                if (s.backlogBytes + length > kMaxTcpBacklog)
                {
                    Release(n);
                    s.ssr = kSockClosed;
                    s.backlog.clear();
                    s.backlogBytes = 0;
                    SetIr(n, kIrTimeout);
                    break;
                }
                if (data && length)
                {
                    Socket::Chunk chunk;
                    chunk.data.assign(data, data + length);
                    chunk.source = source;
                    s.backlog.push_back(std::move(chunk));
                    s.backlogBytes += length;
                }
                FillFromBacklog(n);
            }
            break;

        case NetEventType::PeerClosed:
            if (s.ssr == kSockEstablished)
            {
                s.ssr = kSockCloseWait;   // buffered data stays readable
                SetIr(n, kIrDiscon);
            }
            else if (s.ssr == kSockFinWait)
            {
                Release(n);
                s.ssr = kSockClosed;
                SetIr(n, kIrDiscon);
            }
            break;

        case NetEventType::Reset:
            if (s.ssr == kSockEstablished || s.ssr == kSockCloseWait || s.ssr == kSockFinWait ||
                s.ssr == kSockSynSent || s.ssr == kSockListen)
            {
                Release(n);
                s.ssr = kSockClosed;
                if (status == NetEventStatus::Timeout)
                    SetIr(n, kIrTimeout);
            }
            break;

        case NetEventType::Datagram:
            if (s.ssr == kSockUdp)
            {
                // PACKET-INFO: sender IP (4), sender port (2), size (2), MSB first
                const size_t need = 8 + length + (length & 1);
                if (s.rxBytes + need > s.rxSize)
                    break;   // no room: UDP drops it
                const uint8_t header[8] = {static_cast<uint8_t>(peer.addr >> 24), static_cast<uint8_t>(peer.addr >> 16),
                                           static_cast<uint8_t>(peer.addr >> 8),  static_cast<uint8_t>(peer.addr),
                                           static_cast<uint8_t>(peer.port >> 8),  static_cast<uint8_t>(peer.port),
                                           static_cast<uint8_t>(length >> 8),     static_cast<uint8_t>(length)};
                QueuePacket(n, header, sizeof(header), data, length, source, 0);
            }
            break;

        case NetEventType::EchoReply:
            if (s.ssr == kSockIpRaw)
            {
                // PACKET-INFO: sender IP (4), size (2)
                const size_t need = 6 + length + (length & 1);
                if (s.rxBytes + need > s.rxSize)
                    break;
                const uint8_t header[6] = {static_cast<uint8_t>(peer.addr >> 24), static_cast<uint8_t>(peer.addr >> 16),
                                           static_cast<uint8_t>(peer.addr >> 8),  static_cast<uint8_t>(peer.addr),
                                           static_cast<uint8_t>(length >> 8),     static_cast<uint8_t>(length)};
                QueuePacket(n, header, sizeof(header), data, length, source, 0);
            }
            break;

        default:
            break;
    }
}

W5300::SocketView W5300::GetSocket(int n) const
{
    SocketView v;
    if (n < 0 || n >= kSockets)
        return v;
    const Socket& s = _sockets[n];
    v.mode = s.mr[1] & 0x0F;
    v.state = s.ssr;
    v.ir = s.ir;
    v.sourcePort = static_cast<uint16_t>((s.portr[0] << 8) | s.portr[1]);
    v.destination = Destination(s);
    v.txFree = TxFree(s);
    v.rxReceived = s.rxBytes;
    v.tcpBacklog = s.backlogBytes;
    v.networkSocket = s.net;
    return v;
}

// ---------------------------------------------------------------------------
// TTD state
// ---------------------------------------------------------------------------

bool W5300::SaveState(netstate::Adapters& out) const
{
    bool complete = true;
    std::memcpy(out.common, _common.data(), sizeof(out.common));
    for (int n = 0; n < kSockets; ++n)
    {
        const Socket& s = _sockets[n];
        netstate::W5300Socket& o = out.sockets[n];
        std::memcpy(o.mr, s.mr, sizeof(o.mr));
        o.imr = s.imr;
        o.ir = s.ir;
        o.ssr = s.ssr;
        std::memcpy(o.portr, s.portr, sizeof(o.portr));
        std::memcpy(o.dhar, s.dhar, sizeof(o.dhar));
        std::memcpy(o.dportr, s.dportr, sizeof(o.dportr));
        std::memcpy(o.dipr, s.dipr, sizeof(o.dipr));
        std::memcpy(o.mssr, s.mssr, sizeof(o.mssr));
        o.kpalvtr = s.kpalvtr;
        o.protor = s.protor;
        o.tosr = s.tosr;
        o.ttlr = s.ttlr;
        o.fragr = s.fragr;
        o.txLatch = s.txLatch;
        o.txHalf = s.txHalf ? 1 : 0;
        o.wrsr = s.wrsr;
        o.txSize = s.txSize;
        o.rxSize = s.rxSize;
        o.net = s.net;

        const size_t tx = std::min<size_t>(s.tx.size(), netstate::kMaxTxBytes);
        complete = complete && tx == s.tx.size();
        o.txLength = static_cast<uint16_t>(tx);
        if (tx)
            std::memcpy(o.tx, s.tx.data(), tx);

        o.rxRead = s.rxRead;
        uint16_t p = 0;
        for (const Socket::RxPacket& packet : s.rx)
        {
            if (p >= netstate::kMaxPackets)
            {
                complete = false;
                break;
            }
            netstate::RxPacket& op = o.packets[p++];
            std::memcpy(op.header, packet.header, sizeof(op.header));
            op.headerLength = packet.headerLength;
            op.data.source = packet.source;
            op.data.sourceOffset = packet.sourceOffset;
            op.data.length = static_cast<uint32_t>(packet.data.size());
            complete = complete && (packet.source != 0 || packet.data.empty());
        }
        o.packetCount = p;

        uint16_t b = 0;
        for (const Socket::Chunk& chunk : s.backlog)
        {
            if (b >= netstate::kMaxBacklog)
            {
                complete = false;
                break;
            }
            o.backlog[b].source = chunk.source;
            o.backlog[b].sourceOffset = chunk.sourceOffset;
            o.backlog[b].length = static_cast<uint32_t>(chunk.data.size());
            complete = complete && chunk.source != 0;
            ++b;
        }
        o.backlogCount = b;
    }
    return complete;
}

bool W5300::LoadState(const netstate::Adapters& in, const ByteSource& bytes)
{
    bool found = true;
    auto fetch = [&](const netstate::Reference& ref, std::vector<uint8_t>& out) {
        out.clear();
        if (ref.length == 0)
            return;
        if (!bytes || !bytes(ref.source, ref.sourceOffset, ref.length, out) || out.size() != ref.length)
        {
            out.assign(ref.length, 0);
            found = false;
        }
    };

    std::memcpy(_common.data(), in.common, sizeof(in.common));
    for (int n = 0; n < kSockets; ++n)
    {
        Socket& s = _sockets[n];
        const netstate::W5300Socket& o = in.sockets[n];
        s = Socket();
        std::memcpy(s.mr, o.mr, sizeof(s.mr));
        s.imr = o.imr;
        s.ir = o.ir;
        s.ssr = o.ssr;
        std::memcpy(s.portr, o.portr, sizeof(s.portr));
        std::memcpy(s.dhar, o.dhar, sizeof(s.dhar));
        std::memcpy(s.dportr, o.dportr, sizeof(s.dportr));
        std::memcpy(s.dipr, o.dipr, sizeof(s.dipr));
        std::memcpy(s.mssr, o.mssr, sizeof(s.mssr));
        s.kpalvtr = o.kpalvtr;
        s.protor = o.protor;
        s.tosr = o.tosr;
        s.ttlr = o.ttlr;
        s.fragr = o.fragr;
        s.txLatch = o.txLatch;
        s.txHalf = o.txHalf != 0;
        s.wrsr = o.wrsr;
        s.txSize = o.txSize ? o.txSize : 8192;
        s.rxSize = o.rxSize ? o.rxSize : 8192;
        s.net = o.net;   // the virtual network restores its own table (the ids stay valid)
        const size_t tx = std::min<size_t>(o.txLength, netstate::kMaxTxBytes);
        s.tx.assign(o.tx, o.tx + tx);

        for (uint16_t p = 0; p < o.packetCount && p < netstate::kMaxPackets; ++p)
        {
            const netstate::RxPacket& op = o.packets[p];
            Socket::RxPacket packet;
            std::memcpy(packet.header, op.header, sizeof(packet.header));
            packet.headerLength = op.headerLength;
            packet.source = op.data.source;
            packet.sourceOffset = op.data.sourceOffset;
            fetch(op.data, packet.data);
            s.rxBytes += packet.Size();
            s.rx.push_back(std::move(packet));
        }
        s.rxRead = std::min(o.rxRead, s.rxBytes);
        s.rxBytes -= s.rxRead;

        for (uint16_t b = 0; b < o.backlogCount && b < netstate::kMaxBacklog; ++b)
        {
            Socket::Chunk chunk;
            chunk.source = o.backlog[b].source;
            chunk.sourceOffset = o.backlog[b].sourceOffset;
            fetch(o.backlog[b], chunk.data);
            s.backlogBytes += chunk.data.size();
            s.backlog.push_back(std::move(chunk));
        }
    }
    return found;
}
