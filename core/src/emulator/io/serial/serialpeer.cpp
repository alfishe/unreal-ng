#include "emulator/io/serial/serialpeer.h"

#include "common/network/dnsmessage.h"
#include "emulator/io/network/virtualnetwork.h"

StreamPeer::StreamPeer(VirtualNetwork* network, const ComPortSpec& spec, bool modemLines)
    : _network(network), _spec(spec), _modemLines(modemLines)
{
    if (_spec.kind == ComPortSpec::Kind::Serial)
        _line.baud = _spec.baud;
    Open();
}

StreamPeer::StreamPeer(VirtualNetwork* network, const Dialer& dialer)
    : _network(network), _dialer(true), _owner(dialer.owner)
{
    _spec.kind = ComPortSpec::Kind::Tcp;
}

StreamPeer::~StreamPeer()
{
    CloseSockets();
}

std::string StreamPeer::Target() const
{
    if (_spec.kind == ComPortSpec::Kind::Serial)
        return _spec.device + "," + std::to_string(_line.baud);
    std::string target = _spec.host + ":" + std::to_string(_spec.port);
    if (_spec.addr == 0 && _resolvedAddr != 0)
        target += " (" + NetIpToString(_resolvedAddr) + ")";
    return target;
}

bool StreamPeer::Cts() const
{
    if (_modemLines && _spec.kind == ComPortSpec::Kind::Serial)
        return _phase == Phase::Connected && (_deviceLines & 0x10) != 0;
    return true;   // no line to watch: the other end always lets the ZX send
}

bool StreamPeer::Dsr() const
{
    if (_modemLines && _spec.kind == ComPortSpec::Kind::Serial)
        return _phase == Phase::Connected && (_deviceLines & 0x20) != 0;
    return _phase == Phase::Connected;
}

bool StreamPeer::Dcd() const
{
    if (_modemLines && _spec.kind == ComPortSpec::Kind::Serial)
        return _phase == Phase::Connected && (_deviceLines & 0x80) != 0;
    return _phase == Phase::Connected;
}

bool StreamPeer::Ri() const
{
    return _modemLines && _spec.kind == ComPortSpec::Kind::Serial && _phase == Phase::Connected &&
           (_deviceLines & 0x40) != 0;
}

void StreamPeer::CloseSockets()
{
    if (_network)
    {
        if (_socket)
            _network->Close(_socket);
        if (_dnsSocket)
            _network->Close(_dnsSocket);
    }
    _socket = 0;
    _dnsSocket = 0;
    _connectPending = false;
    _closePending = false;
}

void StreamPeer::Open()
{
    CloseSockets();
    _phase = Phase::Idle;
    _retryFrames = 0;
    _deviceLines = 0;
    if (!_network)
    {
        _lastError = "no network";
        return;
    }
    if (_spec.kind == ComPortSpec::Kind::Serial)
    {
        _socket = _network->Open(NetProto::Serial, Guest(), kCookieLink);
        if (!_socket)
            return Fail("out of network sockets");
        _phase = Phase::Connecting;
        _network->ConnectSerial(_socket, _spec.device, _line.baud);
        return;
    }
    if (_spec.addr != 0)
        return StartConnect(_spec.addr);

    // A host name: ask the virtual network's DNS (Hosts= first, then the host
    // resolver); the answer is a journaled datagram like any other
    _resolvedAddr = 0;
    _dnsSocket = _network->Open(NetProto::Udp, Guest(), kCookieDns);
    if (!_dnsSocket)
        return Fail("out of network sockets");
    _dnsSeq = static_cast<uint16_t>(_dnsSeq + 1);
    _dnsId = static_cast<uint16_t>(0x5A00 ^ _dnsSeq);
    const std::vector<uint8_t> query = dns::BuildQuery(_dnsId, _spec.host);
    if (query.empty())
        return Fail("invalid host name");
    _phase = Phase::Resolving;
    _waitFrames = kResolveFrames;
    _network->SendTo(_dnsSocket, static_cast<uint16_t>(0xC000 | (_dnsSeq & 0x3FFF)),
                     NetEndpoint{_network->Config().dnsServer, 53}, query.data(), static_cast<uint32_t>(query.size()));
}

void StreamPeer::StartConnect(uint32_t addr)
{
    _socket = _network->Open(NetProto::Tcp, Guest(), kCookieLink);
    if (!_socket)
        return Fail("out of network sockets");
    _phase = Phase::Connecting;
    _remote = NetEndpoint{addr, _spec.port};
    _network->Connect(_socket, _remote);
}

void StreamPeer::Fail(const std::string& why, NetEventStatus status)
{
    // Sockets close at the frame boundary, not inside the network's delivery
    _lastError = why;
    _phase = Phase::Idle;
    _closePending = true;
    _connectPending = false;
    _retryFrames = _dialer ? 0 : kRetryFrames;   // a dialed link is dialed again by its owner, if at all
    if (_dialer && onLinkChange)
        onLinkChange(_phase, status, why);
}

void StreamPeer::Dial(const ComPortSpec& spec)
{
    _spec = spec;
    _rx.clear();
    _tx.clear();
    _lastError.clear();
    _remote = NetEndpoint{};
    Open();
}

void StreamPeer::HangUp()
{
    // At the frame boundary, like a failure: this may run inside the network's delivery
    _phase = Phase::Idle;
    _closePending = _socket != 0 || _dnsSocket != 0;
    _connectPending = false;
    _retryFrames = 0;
    _waitFrames = 0;
    _rx.clear();
    _tx.clear();
}

void StreamPeer::Adopt(uint16_t socket, const NetEndpoint& peer)
{
    _socket = socket;
    _dnsSocket = 0;
    _closePending = false;
    _connectPending = false;
    _retryFrames = 0;
    _waitFrames = 0;
    _resolvedAddr = peer.addr;
    _remote = peer;
    _phase = Phase::Connected;
    _lastError.clear();
    _rx.clear();
    _tx.clear();
}

void StreamPeer::SendLineAndLines()
{
    if (!_network || _spec.kind != ComPortSpec::Kind::Serial || _phase != Phase::Connected)
        return;
    _network->ConfigureSerial(_socket, _line);
    if (_modemLines)
        _network->SerialModemLines(_socket, _rts, _dtr);
}

void StreamPeer::Transmit(uint8_t byte)
{
    // Kept while the link is down too: the first frame after the connect
    // sends it (a real modem would drop it; a harness wants it)
    if (_tx.size() < kMaxPending)
        _tx.push_back(byte);
}

uint8_t StreamPeer::TakeByte()
{
    const uint8_t b = _rx.front().value;
    _rx.pop_front();
    return b;
}

void StreamPeer::OnModemLines(bool rts, bool dtr)
{
    if (rts == _rts && dtr == _dtr)
        return;
    _rts = rts;
    _dtr = dtr;
    if (_modemLines && _spec.kind == ComPortSpec::Kind::Serial && _phase == Phase::Connected && _network)
        _network->SerialModemLines(_socket, _rts, _dtr);
}

void StreamPeer::OnLineSettings(const SerialLine& line)
{
    if (line == _line)
        return;
    _line = line;
    if (_spec.kind == ComPortSpec::Kind::Serial && _phase == Phase::Connected && _network)
        _network->ConfigureSerial(_socket, _line);
}

void StreamPeer::OnFrame()
{
    if (_closePending)
    {
        _closePending = false;
        if (_network)
        {
            if (_socket)
                _network->Close(_socket);
            if (_dnsSocket)
                _network->Close(_dnsSocket);
        }
        _socket = 0;
        _dnsSocket = 0;
    }
    if (_connectPending)
    {
        _connectPending = false;
        if (_network && _dnsSocket)
            _network->Close(_dnsSocket);
        _dnsSocket = 0;
        if (_network)
            StartConnect(_resolvedAddr);
    }
    if (_phase == Phase::Resolving && _waitFrames > 0 && --_waitFrames == 0)
        Fail(_spec.host + ": no DNS answer", NetEventStatus::Timeout);
    if (_phase == Phase::Connected && !_tx.empty() && _network)
    {
        _network->Send(_socket, _tx.data(), static_cast<uint32_t>(_tx.size()));
        _bytesOut += _tx.size();
        _tx.clear();
    }
    if (_phase == Phase::Idle && _retryFrames > 0 && --_retryFrames == 0)
        Open();
}

void StreamPeer::Reset()
{
    // A machine reset does not unplug the cable: the link stays, the bytes in
    // flight are gone
    _rx.clear();
    _tx.clear();
}

void StreamPeer::OnNetEvent(uint32_t cookie, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
                            const uint8_t* data, uint32_t length, uint32_t source)
{
    (void)peer;
    if (cookie == kCookieDns)
    {
        if (type != NetEventType::Datagram || _phase != Phase::Resolving)
            return;
        std::vector<uint32_t> addresses;
        uint8_t rcode = 0;
        if (!dns::ParseAnswer(data, length, _dnsId, addresses, rcode))
            return;   // not the answer to our query
        if (rcode == dns::kRcodeNxDomain)
            return Fail(_spec.host + ": no such host", NetEventStatus::Unreachable);
        if (rcode != dns::kRcodeNoError || addresses.empty())
            return Fail(_spec.host + ": the name did not resolve (DNS code " + std::to_string(rcode) + ")",
                        NetEventStatus::Unreachable);
        _resolvedAddr = addresses.front();
        _connectPending = true;   // at the frame boundary: no socket changes inside the delivery
        return;
    }

    switch (type)
    {
        case NetEventType::Connected:
            _phase = Phase::Connected;
            _lastError.clear();
            SendLineAndLines();
            if (_dialer && onLinkChange)
                onLinkChange(_phase, NetEventStatus::Ok, std::string());
            break;
        case NetEventType::Data:
            if (_dialer && _phase != Phase::Connected)
                break;   // a hung-up link: what was still in flight is gone with the call
            for (uint32_t i = 0; i < length && data; ++i)
            {
                if (_rx.size() >= kMaxPending)
                    break;
                _rx.push_back({data[i], source, i});
            }
            _bytesIn += length;
            if (onReceive && length)
                onReceive();
            break;
        case NetEventType::ModemLines:
            if (length >= 1 && data)
                _deviceLines = data[0];
            if (onReceive)
                onReceive();   // the UART updates MSR at this time
            break;
        case NetEventType::ConnectFailed:
            if (data && length)
                Fail(std::string(reinterpret_cast<const char*>(data), length), status);
            else
                Fail(std::string("connect failed: ") + NetStatusText(status), status);
            break;
        case NetEventType::PeerClosed:
            Fail("closed by the other end", NetEventStatus::Ok);
            break;
        case NetEventType::Reset:
            Fail(std::string("link lost: ") + NetStatusText(status), status);
            break;
        default:
            break;
    }
}

void StreamPeer::SaveLink(netstate::StreamLink& out) const
{
    out.phase = static_cast<uint8_t>(_phase);
    out.closePending = _closePending ? 1 : 0;
    out.connectPending = _connectPending ? 1 : 0;
    out.deviceLines = _deviceLines;
    out.rts = _rts ? 1 : 0;
    out.dtr = _dtr ? 1 : 0;
    out.lineDataBits = _line.dataBits;
    out.lineParity = static_cast<uint8_t>(_line.parity);
    out.lineStopBits = _line.stopBits;
    out.socket = _socket;
    out.dnsSocket = _dnsSocket;
    out.dnsId = _dnsId;
    out.dnsSeq = _dnsSeq;
    out.resolvedAddr = _resolvedAddr;
    out.retryFrames = _retryFrames;
    out.waitFrames = _waitFrames;
    out.lineBaud = _line.baud;
    out.bytesIn = _bytesIn;
    out.bytesOut = _bytesOut;
}

void StreamPeer::LoadLink(const netstate::StreamLink& in)
{
    _phase = in.phase <= static_cast<uint8_t>(Phase::Connected) ? static_cast<Phase>(in.phase) : Phase::Idle;
    _closePending = in.closePending != 0;
    _connectPending = in.connectPending != 0;
    _deviceLines = in.deviceLines;
    _rts = in.rts != 0;
    _dtr = in.dtr != 0;
    _line.dataBits = in.lineDataBits;
    _line.parity = static_cast<char>(in.lineParity);
    _line.stopBits = in.lineStopBits;
    _line.baud = in.lineBaud;
    _socket = in.socket;
    _dnsSocket = in.dnsSocket;
    _dnsId = in.dnsId;
    _dnsSeq = in.dnsSeq;
    _resolvedAddr = in.resolvedAddr;
    _retryFrames = in.retryFrames;
    _waitFrames = in.waitFrames;
    _bytesIn = in.bytesIn;
    _bytesOut = in.bytesOut;
}
