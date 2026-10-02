#include "emulator/io/network/virtualnetwork.h"

#include <algorithm>

#include "common/network/dnsmessage.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdinputjournal.h"
#include "emulator/emulatorcontext.h"

using ttd::TTDInputEvent;
using ttd::TTDInputKind;
using ttd::TTDNetInput;

namespace
{
constexpr uint16_t kDhcpServerPort = 67;
constexpr uint16_t kDnsPort = 53;
constexpr uint32_t kBroadcast = 0xFFFFFFFFu;

DhcpServer::Settings DhcpSettings(const VirtualNetworkConfig& c)
{
    DhcpServer::Settings s;
    s.serverAddr = c.gateway;
    s.mask = c.mask;
    s.dnsAddr = c.dnsServer;
    s.firstLease = c.firstLease;
    return s;
}

uint16_t IcmpChecksum(const uint8_t* data, size_t length)
{
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < length; i += 2)
        sum += static_cast<uint32_t>((data[i] << 8) | data[i + 1]);
    if (length & 1)
        sum += static_cast<uint32_t>(data[length - 1] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}
} // namespace

VirtualNetwork::VirtualNetwork(EmulatorContext* context, std::unique_ptr<IHostNet> host,
                               const VirtualNetworkConfig& config)
    : _context(context), _host(std::move(host)), _config(config), _dhcp(DhcpSettings(config))
{
}

VirtualNetwork::~VirtualNetwork()
{
    if (_host)
        _host->CloseAll();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool VirtualNetwork::IsReplaying() const
{
    return _context && _context->pTimeTravelManager && _context->pTimeTravelManager->OwnsInput();
}

bool VirtualNetwork::IsInternal(uint32_t addr) const
{
    return (addr & _config.mask) == (_config.network & _config.mask);
}

uint16_t VirtualNetwork::AllocateId()
{
    // Ids below IHostNet::kFirstAcceptedId; the host numbers accepted connections above
    for (uint32_t tries = 0; tries < IHostNet::kFirstAcceptedId; ++tries)
    {
        const uint16_t id = _nextId;
        _nextId = static_cast<uint16_t>(_nextId + 1);
        if (_nextId >= IHostNet::kFirstAcceptedId)
            _nextId = 1;
        if (_sockets.find(id) == _sockets.end() && FindByHostId(id) == nullptr)
            return id;
    }
    return kNoSocket;
}

VirtualNetwork::Socket* VirtualNetwork::Find(uint16_t id)
{
    auto it = _sockets.find(id);
    return it == _sockets.end() ? nullptr : &it->second;
}

VirtualNetwork::Socket* VirtualNetwork::FindByHostId(uint16_t hostId)
{
    for (auto& [id, s] : _sockets)
    {
        if (s.hostId == hostId)
            return &s;
    }
    return nullptr;
}

uint16_t VirtualNetwork::HostPortFor(uint16_t guestPort) const
{
    auto rule = _config.forwards.find(guestPort);
    if (rule != _config.forwards.end())
        return rule->second;
    return guestPort >= 1024 ? guestPort : 0;
}

void VirtualNetwork::Defer(uint16_t id, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
                           std::vector<uint8_t> data)
{
    // The virtual network's own answers are journaled like the host's (their
    // bytes are what a checkpoint refers to): while TTD replays they come
    // from the journal, so none is made here
    if (IsReplaying())
        return;
    _deferred.push_back({id, type, status, peer, std::move(data)});
}

void VirtualNetwork::Note(uint16_t id, NetProto proto, const char* action, const NetEndpoint& remote,
                          NetEventStatus status, uint32_t bytes)
{
    Activity a;
    a.frame = _context ? _context->emulatorState.frame_counter : 0;
    a.socket = id;
    a.proto = proto;
    a.action = action;
    a.remote = remote;
    a.status = status;
    a.bytes = bytes;
    _activity.push_back(std::move(a));
    while (_activity.size() > kActivityLength)
        _activity.pop_front();
}

void VirtualNetwork::Deliver(Socket& s, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
                             const uint8_t* data, uint32_t length, uint32_t source)
{
    switch (type)
    {
        case NetEventType::Connected: Note(s.id, s.proto, "connected", s.remote); break;
        case NetEventType::ConnectFailed: Note(s.id, s.proto, "connect-failed", s.remote, status); break;
        case NetEventType::PeerClosed: Note(s.id, s.proto, "peer-closed", s.remote); break;
        case NetEventType::Reset: Note(s.id, s.proto, "reset", s.remote, status); break;
        case NetEventType::Accepted: Note(s.id, s.proto, "accepted", peer); break;
        default: break;
    }
    switch (type)
    {
        case NetEventType::Connected:
            s.connected = true;
            break;
        case NetEventType::Data:
        case NetEventType::Datagram:
        case NetEventType::EchoReply:
            s.bytesIn += length;
            break;
        case NetEventType::ConnectFailed:
        case NetEventType::Reset:
            s.connected = false;
            break;
        default:
            break;
    }
    if (s.guest)
        s.guest->OnNetEvent(s.cookie, type, status, peer, data, length, source);
}

// ---------------------------------------------------------------------------
// Guest socket API
// ---------------------------------------------------------------------------

uint16_t VirtualNetwork::Open(NetProto proto, INetGuest* guest, uint32_t cookie)
{
    const uint16_t id = AllocateId();
    if (id == kNoSocket)
        return kNoSocket;
    Socket s;
    s.id = id;
    s.hostId = id;
    s.proto = proto;
    s.guest = guest;
    s.cookie = cookie;
    _sockets[id] = s;
    return id;
}

void VirtualNetwork::Rebind(uint16_t id, INetGuest* guest, uint32_t cookie)
{
    if (Socket* s = Find(id))
    {
        s->guest = guest;
        s->cookie = cookie;
    }
}

void VirtualNetwork::Connect(uint16_t id, const NetEndpoint& to)
{
    Socket* s = Find(id);
    if (!s || s->proto != NetProto::Tcp)
        return;
    s->remote = to;
    s->connected = false;

    if (IsInternal(to.addr) || to.addr == 0 || to.addr == kBroadcast)
    {
        // Nothing listens on the router's own addresses
        Defer(id, NetEventType::ConnectFailed, NetEventStatus::Refused, to);
        return;
    }
    Note(id, s->proto, "connect", to);
    if (!_host)
    {
        Defer(id, NetEventType::ConnectFailed, NetEventStatus::Unreachable, to);
        return;
    }
    if (!IsReplaying())
        _host->TcpConnect(s->hostId, to);
}

void VirtualNetwork::ConnectSerial(uint16_t id, const std::string& device, uint32_t baud)
{
    Socket* s = Find(id);
    if (!s || s->proto != NetProto::Serial)
        return;
    s->connected = false;
    Note(id, s->proto, "serial-open", s->remote);
    if (!_host)
    {
        Defer(id, NetEventType::ConnectFailed, NetEventStatus::Unreachable, s->remote);
        return;
    }
    if (!IsReplaying())
        _host->SerialOpen(s->hostId, device, baud);
}

void VirtualNetwork::ConfigureSerial(uint16_t id, const SerialLine& line)
{
    Socket* s = Find(id);
    if (s && s->proto == NetProto::Serial && _host && !IsReplaying())
        _host->SerialConfigure(s->hostId, line);
}

void VirtualNetwork::SerialModemLines(uint16_t id, bool rts, bool dtr)
{
    Socket* s = Find(id);
    if (s && s->proto == NetProto::Serial && _host && !IsReplaying())
        _host->SerialModemLines(s->hostId, rts, dtr);
}

void VirtualNetwork::Send(uint16_t id, const uint8_t* data, uint32_t length)
{
    Socket* s = Find(id);
    if (!s || (s->proto != NetProto::Tcp && s->proto != NetProto::Serial) || !data || length == 0)
        return;
    s->bytesOut += length;
    if (_host && s->connected && !IsReplaying())
        _host->TcpSend(s->hostId, data, length);
}

void VirtualNetwork::SendTo(uint16_t id, uint16_t localPort, const NetEndpoint& to, const uint8_t* data,
                            uint32_t length)
{
    Socket* s = Find(id);
    if (!s || !data)
        return;
    s->bytesOut += length;

    if (s->proto == NetProto::Icmp)
    {
        AnswerIcmpEcho(id, to, data, length);
        return;
    }
    if (s->proto != NetProto::Udp)
        return;

    // DHCP: broadcast (or unicast to the server) from the client port
    if (to.port == kDhcpServerPort && (to.addr == kBroadcast || to.addr == _config.gateway))
    {
        std::vector<uint8_t> reply = _dhcp.Handle(data, length);
        if (!reply.empty())
        {
            ++_counters.dhcpReplies;
            Note(id, s->proto, "dhcp", to, NetEventStatus::Ok, length);
            Defer(id, NetEventType::Datagram, NetEventStatus::Ok, NetEndpoint{_config.gateway, kDhcpServerPort},
                  std::move(reply));
        }
        return;
    }

    // DNS: the hosts table first, then the host resolver
    if (to.port == kDnsPort && (_config.dnsMode == VirtualNetworkConfig::DnsMode::Host || to.addr == _config.dnsServer))
    {
        dns::Question q;
        if (dns::ParseQuery(data, length, q))
        {
            auto pinned = _config.hosts.find(q.name);
            if (pinned != _config.hosts.end())
            {
                ++_counters.dnsLocalAnswers;
                Defer(id, NetEventType::Datagram, NetEventStatus::Ok, to,
                      dns::BuildAnswer(data, length, q, {pinned->second}, dns::kRcodeNoError));
                return;
            }
            Note(id, s->proto, "dns", to, NetEventStatus::Ok, length);
            if (_host && !IsReplaying())
            {
                ++_counters.dnsHostQueries;
                _host->DnsQuery(s->hostId, to, data, length);
            }
            else if (!_host)
                Defer(id, NetEventType::Datagram, NetEventStatus::Ok, to,
                      dns::BuildAnswer(data, length, q, {}, dns::kRcodeServFail));
        }
        return;
    }

    // Anything else for the virtual LAN or a broadcast: nobody answers
    if (IsInternal(to.addr) || to.addr == kBroadcast || (to.addr >> 28) == 0xE)
        return;

    (void)localPort;  // the host picks its own source port (NAT)
    if (_host && !IsReplaying())
        _host->UdpSend(s->hostId, to, data, length);
}

bool VirtualNetwork::AnswerIcmpEcho(uint16_t id, const NetEndpoint& to, const uint8_t* data, uint32_t length)
{
    // Echo request (type 8) to the gateway or the DNS server: echo reply
    // (type 0), same identifier, sequence and data, new checksum. Other
    // addresses: the host pings (its answer is a journaled NetEvent)
    if (length < 8 || data[0] != 8)
        return false;
    if (to.addr != _config.gateway && to.addr != _config.dnsServer)
    {
        if (IsInternal(to.addr) || to.addr == kBroadcast)
            return false;   // nobody else lives on the virtual LAN
        Socket* s = Find(id);
        if (_host && s && !IsReplaying())
            _host->IcmpEcho(s->hostId, to, data, length);
        return true;
    }
    std::vector<uint8_t> reply(data, data + length);
    reply[0] = 0;
    reply[2] = reply[3] = 0;
    const uint16_t sum = IcmpChecksum(reply.data(), reply.size());
    reply[2] = static_cast<uint8_t>(sum >> 8);
    reply[3] = static_cast<uint8_t>(sum & 0xFF);
    ++_counters.echoReplies;
    Defer(id, NetEventType::EchoReply, NetEventStatus::Ok, NetEndpoint{to.addr, 0}, std::move(reply));
    return true;
}

void VirtualNetwork::ShutdownWrite(uint16_t id)
{
    Socket* s = Find(id);
    if (s && s->proto == NetProto::Tcp && _host && !IsReplaying())
        _host->TcpShutdownWrite(s->hostId);
}

void VirtualNetwork::Listen(uint16_t id, uint16_t guestPort)
{
    Socket* s = Find(id);
    if (!s || s->proto != NetProto::Tcp)
        return;
    s->listenPort = guestPort;

    auto it = _listeners.find(guestPort);
    if (it == _listeners.end())
    {
        Listener l;
        l.hostPort = HostPortFor(guestPort);
        if (l.hostPort != 0)
        {
            l.hostListenerId = AllocateId();
            // Reserve the id so no socket takes it
            Socket placeholder;
            placeholder.id = l.hostListenerId;
            placeholder.hostId = l.hostListenerId;
            placeholder.listenPort = guestPort;
            _sockets[l.hostListenerId] = placeholder;
            if (_host && !IsReplaying())
                _host->TcpListen(l.hostListenerId, l.hostPort);
        }
        it = _listeners.emplace(guestPort, std::move(l)).first;
    }
    it->second.waiting.push_back(id);
    HandOver(guestPort, it->second);
}

void VirtualNetwork::HandOver(uint16_t guestPort, Listener& l)
{
    (void)guestPort;
    while (!l.waiting.empty() && !l.pending.empty())
    {
        const uint16_t sid = l.waiting.front();
        l.waiting.pop_front();
        Socket* s = Find(sid);
        if (!s)
            continue;
        const Listener::Pending client = l.pending.front();
        l.pending.pop_front();
        s->hostId = client.hostId;
        s->remote = client.peer;
        s->connected = true;
        s->listenPort = 0;
        Deliver(*s, NetEventType::Accepted, NetEventStatus::Ok, client.peer, nullptr, 0);
    }
}

void VirtualNetwork::Close(uint16_t id)
{
    auto it = _sockets.find(id);
    if (it == _sockets.end())
        return;
    const Socket s = it->second;
    _sockets.erase(it);
    if (s.guest)
        Note(id, s.proto, "close", s.remote);

    if (s.listenPort)
    {
        auto l = _listeners.find(s.listenPort);
        if (l != _listeners.end())
        {
            auto& w = l->second.waiting;
            w.erase(std::remove(w.begin(), w.end(), id), w.end());
        }
    }
    _deferred.erase(std::remove_if(_deferred.begin(), _deferred.end(), [id](const Deferred& d) { return d.id == id; }),
                    _deferred.end());
    if (_host && !IsReplaying())
        _host->Close(s.hostId);
}

void VirtualNetwork::ReplaceHost(std::unique_ptr<IHostNet> host)
{
    if (_host)
        _host->CloseAll();
    _host = std::move(host);
}

void VirtualNetwork::Reset(const SerialGuests& keep)
{
    std::map<uint16_t, Socket> kept;
    for (const auto& [id, s] : _sockets)
    {
        if (s.guest && (s.guest == keep.com || s.guest == keep.machine || s.guest == keep.atmIo || s.guest == keep.zifi))
            kept[id] = s;
    }
    if (_host)
    {
        if (kept.empty())
            _host->CloseAll();
        else
        {
            for (const auto& [id, s] : _sockets)
            {
                if (!kept.count(id))
                    _host->Close(s.hostId);
            }
        }
    }
    _sockets.swap(kept);
    _listeners.clear();
    // Answers still queued for the kept sockets stay; the others are gone
    std::deque<Deferred> deferred;
    for (Deferred& d : _deferred)
    {
        if (_sockets.count(d.id))
            deferred.push_back(std::move(d));
    }
    _deferred.swap(deferred);
    _dhcp.Clear();
    if (_sockets.empty())
        _nextId = 1;   // ids of kept sockets must not be handed out again
    _counters = Counters();
    _activity.clear();
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

void VirtualNetwork::Pump()
{
    // While TTD replays, every answer comes from the journal
    const bool replaying = IsReplaying();
    if (replaying)
    {
        _wasReplaying = true;
        return;
    }
    if (_wasReplaying)
    {
        // Back to live from the recorded past: the connections of back then are gone
        _wasReplaying = false;
        TTDInputEvent ev;
        ev.kind = TTDInputKind::NetLinkReset;
        if (_context && _context->pTimeTravelManager)
            _context->pTimeTravelManager->SubmitLiveInput(ev);
        else
            ApplyLinkReset();
    }

    // 1. The virtual network's own answers, then 2. the host's: both are TTD
    // inputs (journaled with their bytes)
    while (!_deferred.empty())
    {
        Deferred d = std::move(_deferred.front());
        _deferred.pop_front();
        Socket* s = Find(d.id);
        if (!s)
            continue;
        HostNetEvent hev;
        hev.type = d.type;
        hev.status = d.status;
        hev.socket = s->hostId;
        hev.peer = d.peer;
        hev.data = std::move(d.data);
        SubmitHostEvent(hev);
    }

    if (!_host)
        return;
    HostNetEvent hev;
    while (_host->PollEvent(hev))
        SubmitHostEvent(hev);
}

void VirtualNetwork::SubmitHostEvent(const HostNetEvent& hev)
{
    TTDInputEvent ev;
    ev.kind = TTDInputKind::NetEvent;
    TTDNetInput net;
    net.socket = hev.socket;
    net.event = static_cast<uint8_t>(hev.type);
    net.status = static_cast<uint8_t>(hev.status);
    net.addr = hev.peer.addr;
    net.port = hev.peer.port;
    const uint8_t* payload = hev.data.empty() ? nullptr : hev.data.data();
    const auto length = static_cast<uint32_t>(hev.data.size());
    net.payloadLength = length;

    if (_context && _context->pTimeTravelManager)
        _context->pTimeTravelManager->SubmitLiveInput(ev, net, payload, length);
    else
        ApplyHostEvent(net, payload);
}

// ---------------------------------------------------------------------------
// TTD input
// ---------------------------------------------------------------------------

void VirtualNetwork::ApplyHostEvent(const TTDNetInput& net, const uint8_t* payload)
{
    ++_counters.hostEvents;
    const auto type = static_cast<NetEventType>(net.event);
    const auto status = static_cast<NetEventStatus>(net.status);
    const NetEndpoint peer{net.addr, net.port};
    const uint32_t length = payload ? net.payloadLength : 0;

    if (type == NetEventType::Accepted)
    {
        // A host client for a guest server: netSocket is the listener id
        if (length < 2)
            return;
        const uint16_t clientId = static_cast<uint16_t>(payload[0] | (payload[1] << 8));
        for (auto& [port, l] : _listeners)
        {
            if (l.hostListenerId != net.socket)
                continue;
            if (l.pending.size() >= 8)
            {
                if (_host && !IsReplaying())
                    _host->Close(clientId);
                return;
            }
            l.pending.push_back({clientId, peer});
            HandOver(port, l);
            return;
        }
        if (_host && !IsReplaying())
            _host->Close(clientId);
        return;
    }

    if (type == NetEventType::ListenFailed)
        return;  // the guest keeps LISTEN; no client will ever arrive (reported in the listener view)

    Socket* s = FindByHostId(net.socket);
    if (!s)
        return;
    Deliver(*s, type, status, peer, payload, length, net.journalIndex);
}

void VirtualNetwork::ApplyLinkReset()
{
    ++_counters.linkResets;
    // A guest may close its socket while handling the reset: collect first
    std::vector<uint16_t> affected;
    for (const auto& [id, s] : _sockets)
    {
        const bool stream = s.proto == NetProto::Tcp || s.proto == NetProto::Serial;
        if (s.guest && stream && (s.connected || s.remote.addr != 0 || s.proto == NetProto::Serial))
            affected.push_back(id);
    }
    for (uint16_t id : affected)
    {
        if (Socket* s = Find(id))
        {
            s->connected = false;
            const NetEndpoint remote = s->remote;
            Deliver(*s, NetEventType::Reset, NetEventStatus::Timeout, remote, nullptr, 0);
        }
    }
    for (auto& [port, l] : _listeners)
    {
        l.pending.clear();
        // Listen again on the host: the old host listener is gone too
        if (l.hostListenerId && _host && !IsReplaying())
            _host->TcpListen(l.hostListenerId, l.hostPort);
    }
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

std::vector<VirtualNetwork::SocketInfo> VirtualNetwork::Sockets() const
{
    std::vector<SocketInfo> out;
    for (const auto& [id, s] : _sockets)
    {
        if (!s.guest)
            continue;  // listener placeholders
        SocketInfo i;
        i.id = s.id;
        i.hostId = s.hostId;
        i.proto = s.proto;
        i.connected = s.connected;
        i.remote = s.remote;
        i.listenPort = s.listenPort;
        i.bytesIn = s.bytesIn;
        i.bytesOut = s.bytesOut;
        out.push_back(i);
    }
    return out;
}

std::vector<VirtualNetwork::ListenerInfo> VirtualNetwork::Listeners() const
{
    std::vector<ListenerInfo> out;
    for (const auto& [port, l] : _listeners)
        out.push_back({port, l.hostPort, l.waiting.size(), l.pending.size()});
    return out;
}

// ---------------------------------------------------------------------------
// TTD state
// ---------------------------------------------------------------------------

bool VirtualNetwork::SaveState(netstate::VirtualNetwork& out, const SerialGuests& serial) const
{
    bool complete = true;
    out.nextId = _nextId;

    uint16_t n = 0;
    for (const auto& [id, s] : _sockets)
    {
        if (n >= netstate::kMaxNetSockets)
        {
            complete = false;
            break;
        }
        netstate::NetSocket& o = out.sockets[n++];
        o.id = s.id;
        o.hostId = s.hostId;
        o.proto = static_cast<uint8_t>(s.proto);
        o.connected = s.connected ? 1 : 0;
        o.hasGuest = !s.guest                        ? 0
                     : s.guest == serial.com     ? 2
                     : s.guest == serial.machine ? 3
                     : s.guest == serial.atmIo   ? 4
                     : s.guest == serial.zifi    ? 5
                                                 : 1;
        o.cookie = s.cookie;
        o.remoteAddr = s.remote.addr;
        o.remotePort = s.remote.port;
        o.listenPort = s.listenPort;
        o.bytesIn = s.bytesIn;
        o.bytesOut = s.bytesOut;
    }
    out.socketCount = n;

    n = 0;
    for (const auto& [port, l] : _listeners)
    {
        if (n >= netstate::kMaxListeners)
        {
            complete = false;
            break;
        }
        netstate::Listener& o = out.listeners[n++];
        o.guestPort = port;
        o.hostListenerId = l.hostListenerId;
        o.hostPort = l.hostPort;
        uint8_t w = 0;
        for (uint16_t id : l.waiting)
        {
            if (w >= netstate::kMaxWaiting)
            {
                complete = false;
                break;
            }
            o.waiting[w++] = id;
        }
        o.waitingCount = w;
        uint8_t p = 0;
        for (const Listener::Pending& client : l.pending)
        {
            if (p >= netstate::kMaxPending)
            {
                complete = false;
                break;
            }
            o.pending[p].hostId = client.hostId;
            o.pending[p].addr = client.peer.addr;
            o.pending[p].port = client.peer.port;
            ++p;
        }
        o.pendingCount = p;
    }
    out.listenerCount = n;

    n = 0;
    for (const auto& [mac, addr] : _dhcp.Leases())
    {
        if (n >= netstate::kMaxLeases)
        {
            complete = false;
            break;
        }
        for (size_t i = 0; i < 6; ++i)
            out.leases[n].mac[i] = mac[i];
        out.leases[n].addr = addr;
        ++n;
    }
    out.leaseCount = n;

    out.counters[0] = _counters.dhcpReplies;
    out.counters[1] = _counters.dnsLocalAnswers;
    out.counters[2] = _counters.dnsHostQueries;
    out.counters[3] = _counters.echoReplies;
    out.counters[4] = _counters.hostEvents;
    out.counters[5] = _counters.linkResets;
    return complete;
}

void VirtualNetwork::LoadState(const netstate::VirtualNetwork& in, INetGuest* guest, const SerialGuests& serial)
{
    _sockets.clear();
    _listeners.clear();
    _deferred.clear();
    _nextId = in.nextId ? in.nextId : 1;

    for (uint16_t i = 0; i < in.socketCount && i < netstate::kMaxNetSockets; ++i)
    {
        const netstate::NetSocket& o = in.sockets[i];
        Socket s;
        s.id = o.id;
        s.hostId = o.hostId;
        s.proto = static_cast<NetProto>(o.proto);
        s.connected = o.connected != 0;
        s.guest = o.hasGuest == 2   ? serial.com
                  : o.hasGuest == 3 ? serial.machine
                  : o.hasGuest == 4 ? serial.atmIo
                  : o.hasGuest == 5 ? serial.zifi
                  : o.hasGuest      ? guest
                                    : nullptr;
        s.cookie = o.cookie;
        s.remote = {o.remoteAddr, o.remotePort};
        s.listenPort = o.listenPort;
        s.bytesIn = o.bytesIn;
        s.bytesOut = o.bytesOut;
        _sockets[s.id] = s;
    }
    for (uint16_t i = 0; i < in.listenerCount && i < netstate::kMaxListeners; ++i)
    {
        const netstate::Listener& o = in.listeners[i];
        Listener l;
        l.hostListenerId = o.hostListenerId;
        l.hostPort = o.hostPort;
        for (uint8_t w = 0; w < o.waitingCount && w < netstate::kMaxWaiting; ++w)
            l.waiting.push_back(o.waiting[w]);
        for (uint8_t p = 0; p < o.pendingCount && p < netstate::kMaxPending; ++p)
            l.pending.push_back({o.pending[p].hostId, NetEndpoint{o.pending[p].addr, o.pending[p].port}});
        _listeners[o.guestPort] = std::move(l);
    }
    std::map<DhcpServer::Mac, uint32_t> leases;
    for (uint16_t i = 0; i < in.leaseCount && i < netstate::kMaxLeases; ++i)
    {
        DhcpServer::Mac mac{};
        for (size_t b = 0; b < mac.size(); ++b)
            mac[b] = in.leases[i].mac[b];
        leases[mac] = in.leases[i].addr;
    }
    _dhcp.SetLeases(leases);

    _counters.dhcpReplies = in.counters[0];
    _counters.dnsLocalAnswers = in.counters[1];
    _counters.dnsHostQueries = in.counters[2];
    _counters.echoReplies = in.counters[3];
    _counters.hostEvents = in.counters[4];
    _counters.linkResets = in.counters[5];
}
