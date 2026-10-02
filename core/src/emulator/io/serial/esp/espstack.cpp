#include "emulator/io/serial/esp/espstack.h"

#include <algorithm>
#include <cstring>

#include "common/network/dnsmessage.h"
#include "emulator/io/network/virtualnetwork.h"

EspStack::EspStack(VirtualNetwork* network, int slots) : _network(network), _slots(static_cast<size_t>(slots))
{
}

EspStack::~EspStack()
{
    Close(-1);
    for (uint16_t id : {_dnsSocket, _pingSocket, _querySocket})
    {
        if (_network && id)
            _network->Close(id);
    }
    for (uint16_t id : _closeLater)
    {
        if (_network)
            _network->Close(id);
    }
}

uint8_t EspStack::SlotMask() const
{
    uint8_t mask = 0;
    for (int i = 0; i < SlotCount() && i < 8; ++i)
    {
        if (_slots[static_cast<size_t>(i)].state != State::Free)
            mask = static_cast<uint8_t>(mask | (1u << i));
    }
    return mask;
}

uint16_t EspStack::OpenVnet(NetProto proto)
{
    if (!_network)
        return 0;
    const uint16_t id = _network->Open(proto, this, 0);
    if (id)
        _network->Rebind(id, this, id);   // the cookie is the socket id: one lookup for every kind
    return id;
}

void EspStack::CloseVnet(uint16_t id)
{
    if (id)
        _closeLater.push_back(id);
}

void EspStack::Reap()
{
    // The firmware frees TCP slots that are closed and fully read when a new
    // socket needs one (sockets.cpp: reap_dead_tcp)
    for (Slot& s : _slots)
    {
        if (s.state == State::Tcp && s.finSeen && s.rx.empty())
        {
            CloseVnet(s.vnetId);
            s = Slot();
        }
    }
}

int EspStack::Open(bool tcp)
{
    auto freeSlot = [&]() {
        for (int i = 0; i < SlotCount(); ++i)
        {
            if (_slots[static_cast<size_t>(i)].state == State::Free)
                return i;
        }
        return -1;
    };
    int i = freeSlot();
    if (i < 0)
    {
        Reap();
        i = freeSlot();
    }
    if (i < 0)
        return -1;
    Slot& s = _slots[static_cast<size_t>(i)];
    s = Slot();
    if (tcp)
    {
        s.state = State::TcpIdle;
        return i;
    }
    s.vnetId = OpenVnet(NetProto::Udp);
    if (!s.vnetId)
        return -1;
    s.state = State::Udp;
    return i;
}

int EspStack::OpenAt(int slot, bool tcp)
{
    if (slot < 0 || slot >= SlotCount() || _slots[static_cast<size_t>(slot)].state != State::Free)
        return -1;
    Slot& s = _slots[static_cast<size_t>(slot)];
    s = Slot();
    if (tcp)
    {
        s.state = State::TcpIdle;
        return slot;
    }
    s.vnetId = OpenVnet(NetProto::Udp);
    if (!s.vnetId)
        return -1;
    s.state = State::Udp;
    return slot;
}

void EspStack::Ping(uint32_t to, const std::vector<uint8_t>& request)
{
    if (!_network)
        return;
    if (_pingSocket)
        CloseVnet(_pingSocket);
    _pingSocket = OpenVnet(NetProto::Icmp);
    if (_pingSocket)
        _network->SendTo(_pingSocket, 0, NetEndpoint{to, 0}, request.data(), static_cast<uint32_t>(request.size()));
}

void EspStack::Query(const NetEndpoint& to, const std::vector<uint8_t>& request)
{
    if (!_network)
        return;
    if (_querySocket)
        CloseVnet(_querySocket);
    _querySocket = OpenVnet(NetProto::Udp);
    if (_querySocket)
        _network->SendTo(_querySocket, 0xC3F0, to, request.data(), static_cast<uint32_t>(request.size()));
}

void EspStack::Connect(int slot, const NetEndpoint& to)
{
    Slot& s = _slots[static_cast<size_t>(slot)];
    if (s.vnetId)
        CloseVnet(s.vnetId);
    s.vnetId = OpenVnet(NetProto::Tcp);
    s.rx.clear();
    s.finSeen = false;
    s.remote = to;
    s.connecting = true;
    s.state = State::TcpIdle;
    if (!s.vnetId)
    {
        s.connecting = false;
        if (onDone)
            onDone({Done::Kind::Connect, slot, NetEventStatus::Error, 0, {}});
        return;
    }
    _network->Connect(s.vnetId, to);
}

void EspStack::ArmListener(int slot)
{
    Slot& s = _slots[static_cast<size_t>(slot)];
    if (s.state != State::Listen || s.waitingId)
        return;
    // Backlog: as many queued clients as the module has slots
    if (static_cast<int>(s.pending.size()) >= SlotCount())
        return;
    s.waitingId = OpenVnet(NetProto::Tcp);
    if (s.waitingId)
        _network->Listen(s.waitingId, s.localPort);
}

void EspStack::Listen(int slot)
{
    Slot& s = _slots[static_cast<size_t>(slot)];
    // A server on the same port in another slot is dropped (a stale one after
    // a ZX reboot: the firmware "steals" the port)
    for (int i = 0; i < SlotCount(); ++i)
    {
        Slot& other = _slots[static_cast<size_t>(i)];
        if (i != slot && other.state == State::Listen && other.localPort == s.localPort)
            Close(i);
    }
    s.state = State::Listen;
    ArmListener(slot);
}

int EspStack::Accept(int listenSlot)
{
    Slot& l = _slots[static_cast<size_t>(listenSlot)];
    if (l.pending.empty())
        return -1;
    int target = -1;
    for (int i = 0; i < SlotCount(); ++i)
    {
        if (_slots[static_cast<size_t>(i)].state == State::Free)
        {
            target = i;
            break;
        }
    }
    if (target < 0)
        return -1;   // the client stays queued
    Pending client = std::move(l.pending.front());
    l.pending.pop_front();
    Slot& s = _slots[static_cast<size_t>(target)];
    s = Slot();
    s.state = State::Tcp;
    s.vnetId = client.vnetId;
    s.remote = client.peer;
    s.finSeen = client.finSeen;
    s.rx = std::move(client.rx);
    // The queue has room again
    _rearm.push_back(listenSlot);
    return target;
}

void EspStack::Send(int slot, const uint8_t* data, uint32_t length)
{
    Slot& s = _slots[static_cast<size_t>(slot)];
    if (_network && s.vnetId && length)
        _network->Send(s.vnetId, data, length);
}

void EspStack::SendTo(int slot, const NetEndpoint& to, const uint8_t* data, uint32_t length)
{
    Slot& s = _slots[static_cast<size_t>(slot)];
    if (_network && s.vnetId)
        _network->SendTo(s.vnetId, s.localPort ? s.localPort : static_cast<uint16_t>(0xC100 + slot), to, data, length);
}

std::vector<uint8_t> EspStack::Read(int slot, uint32_t max)
{
    Slot& s = _slots[static_cast<size_t>(slot)];
    std::vector<uint8_t> out;
    const size_t n = std::min<size_t>(max, s.rx.size());
    out.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        out.push_back(s.rx.front().value);
        s.rx.pop_front();
    }
    return out;
}

bool EspStack::PopDatagram(int slot, Datagram& out)
{
    Slot& s = _slots[static_cast<size_t>(slot)];
    if (s.datagrams.empty())
        return false;
    out = std::move(s.datagrams.front());
    s.datagrams.pop_front();
    return true;
}

void EspStack::Close(int slot)
{
    if (slot < 0)
    {
        for (int i = 0; i < SlotCount(); ++i)
            Close(i);
        return;
    }
    Slot& s = _slots[static_cast<size_t>(slot)];
    CloseVnet(s.vnetId);
    CloseVnet(s.waitingId);
    for (const Pending& p : s.pending)
        CloseVnet(p.vnetId);
    s = Slot();
}

void EspStack::StopAll()
{
    for (Slot& s : _slots)
    {
        if (s.state == State::Tcp || s.state == State::TcpIdle)
        {
            CloseVnet(s.vnetId);
            s.vnetId = 0;
            s.finSeen = true;
            s.connecting = false;
            if (s.state == State::TcpIdle)
                s.state = State::Tcp;
        }
    }
}

bool EspStack::Established(int slot) const
{
    const Slot& s = _slots[static_cast<size_t>(slot)];
    return s.state == State::Tcp && s.vnetId != 0 && !s.finSeen;
}

void EspStack::Resolve(const std::string& name)
{
    if (!_network)
    {
        if (onDone)
            onDone({Done::Kind::Resolve, -1, NetEventStatus::Unreachable, 0, {}});
        return;
    }
    if (_dnsSocket)
        CloseVnet(_dnsSocket);
    _dnsSocket = OpenVnet(NetProto::Udp);
    _dnsSeq = static_cast<uint16_t>(_dnsSeq + 1);
    _dnsId = static_cast<uint16_t>(0xE500 ^ _dnsSeq);
    _resolveName = name;
    const std::vector<uint8_t> query = dns::BuildQuery(_dnsId, name);
    if (!_dnsSocket || query.empty())
    {
        _resolving = false;
        if (onDone)
            onDone({Done::Kind::Resolve, -1, NetEventStatus::Unreachable, 0, {}});
        return;
    }
    _resolving = true;
    _network->SendTo(_dnsSocket, static_cast<uint16_t>(0xC000 | (_dnsSeq & 0x3FFF)),
                     NetEndpoint{_network->Config().dnsServer, 53}, query.data(), static_cast<uint32_t>(query.size()));
}

void EspStack::OnNetEvent(uint32_t cookie, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
                          const uint8_t* data, uint32_t length, uint32_t source)
{
    const uint16_t id = static_cast<uint16_t>(cookie);

    if ((id == _pingSocket && _pingSocket && type == NetEventType::EchoReply) ||
        (id == _querySocket && _querySocket && type == NetEventType::Datagram))
    {
        const bool ping = id == _pingSocket;
        CloseVnet(id);
        (ping ? _pingSocket : _querySocket) = 0;
        Done done{ping ? Done::Kind::Ping : Done::Kind::Query, -1, NetEventStatus::Ok, peer.addr, {}};
        done.data.assign(data, data + length);
        if (onDone)
            onDone(done);
        return;
    }

    if (id == _dnsSocket && _dnsSocket)
    {
        if (type != NetEventType::Datagram || !_resolving)
            return;
        std::vector<uint32_t> addresses;
        uint8_t rcode = 0;
        if (!dns::ParseAnswer(data, length, _dnsId, addresses, rcode))
            return;
        _resolving = false;
        CloseVnet(_dnsSocket);
        _dnsSocket = 0;
        if (onDone)
            onDone({Done::Kind::Resolve, -1, addresses.empty() ? NetEventStatus::Unreachable : NetEventStatus::Ok,
                    addresses.empty() ? 0u : addresses.front(), {}});
        return;
    }

    auto append = [&](std::deque<RxByte>& rx) {
        for (uint32_t i = 0; i < length && data; ++i)
            rx.push_back({data[i], source, i});
    };

    for (int i = 0; i < SlotCount(); ++i)
    {
        Slot& s = _slots[static_cast<size_t>(i)];
        if (s.state == State::Free)
            continue;

        // A client for a server: the waiting socket is the connection now
        if (s.state == State::Listen && id == s.waitingId && s.waitingId)
        {
            if (type == NetEventType::Accepted)
            {
                Pending p;
                p.vnetId = s.waitingId;
                p.peer = peer;
                s.pending.push_back(std::move(p));
                s.waitingId = 0;
                _rearm.push_back(i);
                if (onData)
                    onData(i);
            }
            return;
        }
        if (s.state == State::Listen)
        {
            for (Pending& p : s.pending)
            {
                if (p.vnetId != id)
                    continue;
                if (type == NetEventType::Data)
                    append(p.rx);
                else if (type == NetEventType::PeerClosed || type == NetEventType::Reset)
                    p.finSeen = true;
                return;
            }
            continue;
        }

        if (id != s.vnetId || !s.vnetId)
            continue;
        switch (type)
        {
            case NetEventType::Connected:
                if (s.connecting)
                {
                    s.connecting = false;
                    s.state = State::Tcp;
                    if (onDone)
                        onDone({Done::Kind::Connect, i, NetEventStatus::Ok, 0, {}});
                }
                break;
            case NetEventType::ConnectFailed:
                if (s.connecting)
                {
                    s.connecting = false;
                    CloseVnet(s.vnetId);
                    s.vnetId = 0;
                    if (onDone)
                        onDone({Done::Kind::Connect, i, status == NetEventStatus::Ok ? NetEventStatus::Error : status, 0, {}});
                }
                break;
            case NetEventType::Data:
                append(s.rx);
                if (onData)
                    onData(i);
                break;
            case NetEventType::Datagram:
            {
                Datagram d;
                d.from = peer;
                for (uint32_t b = 0; b < length && data; ++b)
                    d.data.push_back({data[b], source, b});
                // The firmware's lwIP keeps a few datagrams per socket
                if (s.datagrams.size() < 16)
                    s.datagrams.push_back(std::move(d));
                if (onData)
                    onData(i);
                break;
            }
            case NetEventType::PeerClosed:
            case NetEventType::Reset:
                s.finSeen = true;
                if (onData)
                    onData(i);
                break;
            default:
                break;
        }
        return;
    }
}

void EspStack::OnFrame()
{
    std::vector<uint16_t> closing;
    closing.swap(_closeLater);
    for (uint16_t id : closing)
    {
        if (_network)
            _network->Close(id);
    }
    std::vector<int> rearm;
    rearm.swap(_rearm);
    for (int slot : rearm)
        ArmListener(slot);
}

void EspStack::RebindAll()
{
    if (!_network)
        return;
    auto rebind = [&](uint16_t id) {
        if (id)
            _network->Rebind(id, this, id);
    };
    for (const Slot& s : _slots)
    {
        rebind(s.vnetId);
        rebind(s.waitingId);
        for (const Pending& p : s.pending)
            rebind(p.vnetId);
    }
    rebind(_dnsSocket);
    rebind(_pingSocket);
    rebind(_querySocket);
}

namespace
{
/// Runs of consecutive bytes of one journal record
template <typename It>
bool SaveRuns(It begin, It end, netstate::Reference* runs, uint16_t max, uint16_t& count)
{
    count = 0;
    for (It it = begin; it != end; ++it)
    {
        netstate::Reference* last = count ? &runs[count - 1] : nullptr;
        if (last && it->source && last->source == it->source && last->sourceOffset + last->length == it->offset)
        {
            ++last->length;
            continue;
        }
        if (count >= max || it->source == 0)
            return false;
        runs[count++] = {it->source, it->offset, 1};
    }
    return true;
}

template <typename Container>
bool LoadRuns(const netstate::Reference* runs, uint16_t count, const EspStack::ByteSource& bytes, Container& out)
{
    std::vector<uint8_t> chunk;
    bool complete = true;
    for (uint16_t r = 0; r < count; ++r)
    {
        if (!bytes || !bytes(runs[r].source, runs[r].sourceOffset, runs[r].length, chunk) || chunk.size() != runs[r].length)
        {
            complete = false;
            continue;
        }
        for (uint32_t i = 0; i < runs[r].length; ++i)
            out.push_back({chunk[i], runs[r].source, runs[r].sourceOffset + i});
    }
    return complete;
}
}  // namespace

bool EspStack::SaveState(netstate::EspStackState& out) const
{
    std::memset(&out, 0, sizeof(out));
    bool complete = true;
    out.slotCount = static_cast<uint8_t>(std::min(SlotCount(), netstate::kEspSlots));
    out.dnsSocket = _dnsSocket;
    out.pingSocket = _pingSocket;
    out.querySocket = _querySocket;
    out.dnsId = _dnsId;
    out.dnsSeq = _dnsSeq;
    out.resolving = _resolving ? 1 : 0;
    std::strncpy(out.resolveName, _resolveName.c_str(), sizeof(out.resolveName) - 1);
    for (int i = 0; i < out.slotCount; ++i)
    {
        const Slot& s = _slots[static_cast<size_t>(i)];
        netstate::EspSlot& o = out.slotStates[i];
        o.state = static_cast<uint8_t>(s.state);
        o.connecting = s.connecting ? 1 : 0;
        o.finSeen = s.finSeen ? 1 : 0;
        o.vnetId = s.vnetId;
        o.localPort = s.localPort;
        o.remoteAddr = s.remote.addr;
        o.remotePort = s.remote.port;
        o.waitingId = s.waitingId;
        complete = SaveRuns(s.rx.begin(), s.rx.end(), o.rx, netstate::kEspRuns, o.rxRuns) && complete;
        o.datagramCount = static_cast<uint8_t>(std::min<size_t>(s.datagrams.size(), netstate::kEspDatagrams));
        complete = complete && s.datagrams.size() <= netstate::kEspDatagrams;
        for (int d = 0; d < o.datagramCount; ++d)
        {
            const Datagram& dg = s.datagrams[static_cast<size_t>(d)];
            o.datagrams[d].fromAddr = dg.from.addr;
            o.datagrams[d].fromPort = dg.from.port;
            uint16_t runs = 0;
            complete = SaveRuns(dg.data.begin(), dg.data.end(), &o.datagrams[d].data, 1, runs) && complete;
        }
        o.pendingCount = static_cast<uint8_t>(std::min<size_t>(s.pending.size(), netstate::kEspPending));
        complete = complete && s.pending.size() <= netstate::kEspPending;
        for (int p = 0; p < o.pendingCount; ++p)
        {
            const Pending& pd = s.pending[static_cast<size_t>(p)];
            netstate::EspPending& op = o.pending[p];
            op.vnetId = pd.vnetId;
            op.peerAddr = pd.peer.addr;
            op.peerPort = pd.peer.port;
            op.finSeen = pd.finSeen ? 1 : 0;
            complete = SaveRuns(pd.rx.begin(), pd.rx.end(), op.rx, netstate::kEspPendingRuns, op.rxRuns) && complete;
        }
    }
    out.closeCount = static_cast<uint8_t>(std::min<size_t>(_closeLater.size(), netstate::kEspClose));
    for (int c = 0; c < out.closeCount; ++c)
        out.closeLater[c] = _closeLater[static_cast<size_t>(c)];
    out.rearmMask = 0;
    for (int slot : _rearm)
        out.rearmMask = static_cast<uint8_t>(out.rearmMask | (1u << (slot & 7)));
    return complete;
}

bool EspStack::LoadState(const netstate::EspStackState& in, const ByteSource& bytes)
{
    bool complete = true;
    for (Slot& s : _slots)
        s = Slot();
    for (int i = 0; i < in.slotCount && i < SlotCount(); ++i)
    {
        const netstate::EspSlot& o = in.slotStates[i];
        Slot& s = _slots[static_cast<size_t>(i)];
        s.state = o.state <= static_cast<uint8_t>(State::Listen) ? static_cast<State>(o.state) : State::Free;
        s.connecting = o.connecting != 0;
        s.finSeen = o.finSeen != 0;
        s.vnetId = o.vnetId;
        s.localPort = o.localPort;
        s.remote = {o.remoteAddr, o.remotePort};
        s.waitingId = o.waitingId;
        complete = LoadRuns(o.rx, std::min<uint16_t>(o.rxRuns, netstate::kEspRuns), bytes, s.rx) && complete;
        for (int d = 0; d < o.datagramCount && d < netstate::kEspDatagrams; ++d)
        {
            Datagram dg;
            dg.from = {o.datagrams[d].fromAddr, o.datagrams[d].fromPort};
            if (o.datagrams[d].data.length)
                complete = LoadRuns(&o.datagrams[d].data, 1, bytes, dg.data) && complete;
            s.datagrams.push_back(std::move(dg));
        }
        for (int p = 0; p < o.pendingCount && p < netstate::kEspPending; ++p)
        {
            const netstate::EspPending& op = o.pending[p];
            Pending pd;
            pd.vnetId = op.vnetId;
            pd.peer = {op.peerAddr, op.peerPort};
            pd.finSeen = op.finSeen != 0;
            complete = LoadRuns(op.rx, std::min<uint16_t>(op.rxRuns, netstate::kEspPendingRuns), bytes, pd.rx) && complete;
            s.pending.push_back(std::move(pd));
        }
    }
    _dnsSocket = in.dnsSocket;
    _pingSocket = in.pingSocket;
    _querySocket = in.querySocket;
    _dnsId = in.dnsId;
    _dnsSeq = in.dnsSeq;
    _resolving = in.resolving != 0;
    _resolveName.assign(in.resolveName, strnlen(in.resolveName, sizeof(in.resolveName)));
    _closeLater.assign(in.closeLater, in.closeLater + std::min<int>(in.closeCount, netstate::kEspClose));
    _rearm.clear();
    for (int i = 0; i < 8; ++i)
    {
        if (in.rearmMask & (1u << i))
            _rearm.push_back(i);
    }
    return complete;
}
