#include "common/network/hostnetbridge.h"

#include <chrono>

#include "common/network/dnsmessage.h"
#include "common/serial/hostserialport.h"

HostNetBridge::HostNetBridge() : HostNetBridge(Options())
{
}

HostNetBridge::HostNetBridge(const Options& options) : _options(options)
{
    netsock::Startup();
    _resolver = [](const std::string& name, std::vector<uint32_t>& out) { return netsock::ResolveIpv4(name, out); };
    _worker = std::thread(&HostNetBridge::Run, this);
    _resolverThread = std::thread(&HostNetBridge::RunResolver, this);
    _pingThread = std::thread(&HostNetBridge::RunPinger, this);
}

HostNetBridge::~HostNetBridge()
{
    CloseAllSerial();
    _stop = true;
    _dnsCv.notify_all();
    _pingCv.notify_all();
    if (_worker.joinable())
        _worker.join();
    if (_resolverThread.joinable())
        _resolverThread.join();
    if (_pingThread.joinable())
        _pingThread.join();
    for (auto& [id, s] : _sockets)
        netsock::Close(s.handle);
}

uint64_t HostNetBridge::NowMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// ---------------------------------------------------------------------------
// Commands (emulation thread)
// ---------------------------------------------------------------------------

void HostNetBridge::TcpConnect(uint16_t socket, const NetEndpoint& to)
{
    std::lock_guard<std::mutex> lock(_commandMutex);
    _commands.push_back({CommandType::Connect, socket, to, {}});
}

void HostNetBridge::TcpSend(uint16_t socket, const uint8_t* data, uint32_t length)
{
    if (!data || length == 0)
        return;
    if (SerialSend(socket, data, length))
        return;
    std::lock_guard<std::mutex> lock(_commandMutex);
    _commands.push_back({CommandType::Send, socket, {}, std::vector<uint8_t>(data, data + length)});
}

void HostNetBridge::TcpShutdownWrite(uint16_t socket)
{
    std::lock_guard<std::mutex> lock(_commandMutex);
    _commands.push_back({CommandType::ShutdownWrite, socket, {}, {}});
}

void HostNetBridge::TcpListen(uint16_t socket, uint16_t hostPort)
{
    std::lock_guard<std::mutex> lock(_commandMutex);
    _commands.push_back({CommandType::Listen, socket, NetEndpoint{NetIp(127, 0, 0, 1), hostPort}, {}});
}

void HostNetBridge::UdpSend(uint16_t socket, const NetEndpoint& to, const uint8_t* data, uint32_t length)
{
    std::lock_guard<std::mutex> lock(_commandMutex);
    Command cmd{CommandType::UdpSend, socket, to, {}};
    if (data && length)
        cmd.data.assign(data, data + length);
    _commands.push_back(std::move(cmd));
}

void HostNetBridge::DnsQuery(uint16_t socket, const NetEndpoint& server, const uint8_t* query, uint32_t length)
{
    if (!query || length == 0)
        return;
    {
        std::lock_guard<std::mutex> lock(_dnsMutex);
        _dnsJobs.push_back({socket, server, std::vector<uint8_t>(query, query + length)});
    }
    _dnsCv.notify_one();
}

void HostNetBridge::IcmpEcho(uint16_t socket, const NetEndpoint& to, const uint8_t* data, uint32_t length)
{
    if (!data || length < 8)
        return;
    {
        std::lock_guard<std::mutex> lock(_pingMutex);
        if (_pingJobs.size() >= kMaxQueuedPings)
            return;   // a flood: the extra requests get no answer, as on a busy link
        _pingJobs.push_back({socket, to, std::vector<uint8_t>(data, data + length)});
    }
    _pingCv.notify_one();
}

void HostNetBridge::Close(uint16_t socket)
{
    CloseSerial(socket);
    std::lock_guard<std::mutex> lock(_commandMutex);
    _commands.push_back({CommandType::Close, socket, {}, {}});
}

void HostNetBridge::CloseAll()
{
    CloseAllSerial();
    {
        std::lock_guard<std::mutex> lock(_commandMutex);
        _commands.push_back({CommandType::CloseAll, 0, {}, {}});
    }
    {
        std::lock_guard<std::mutex> lock(_dnsMutex);
        _dnsJobs.clear();
    }
    {
        std::lock_guard<std::mutex> lock(_pingMutex);
        _pingJobs.clear();
    }
}

bool HostNetBridge::PollEvent(HostNetEvent& out)
{
    std::lock_guard<std::mutex> lock(_eventMutex);
    if (_events.empty())
        return false;
    out = std::move(_events.front());
    _events.pop_front();
    return true;
}

void HostNetBridge::SetResolver(Resolver resolver)
{
    std::lock_guard<std::mutex> lock(_dnsMutex);
    _resolver = std::move(resolver);
}

uint16_t HostNetBridge::ListenerHostPort(uint16_t socket) const
{
    std::lock_guard<std::mutex> lock(_listenerPortMutex);
    auto it = _listenerPorts.find(socket);
    return it == _listenerPorts.end() ? 0 : it->second;
}

// ---------------------------------------------------------------------------
// Serial devices
// ---------------------------------------------------------------------------

void HostNetBridge::SerialOpen(uint16_t socket, const std::string& device, uint32_t baud)
{
    CloseSerial(socket);
    auto link = std::make_unique<SerialLink>();
    SerialLink* raw = link.get();
    std::lock_guard<std::mutex> lock(_serialMutex);
    link->thread = std::thread(&HostNetBridge::RunSerial, this, socket, device, baud, raw);
    _serial[socket] = std::move(link);
}

void HostNetBridge::SerialConfigure(uint16_t socket, const SerialLine& line)
{
    std::lock_guard<std::mutex> lock(_serialMutex);
    auto it = _serial.find(socket);
    if (it == _serial.end())
        return;
    std::lock_guard<std::mutex> linkLock(it->second->sendMutex);
    it->second->line = line;
    it->second->lineChanged = true;
}

void HostNetBridge::SerialModemLines(uint16_t socket, bool rts, bool dtr)
{
    std::lock_guard<std::mutex> lock(_serialMutex);
    auto it = _serial.find(socket);
    if (it == _serial.end())
        return;
    std::lock_guard<std::mutex> linkLock(it->second->sendMutex);
    it->second->rts = rts;
    it->second->dtr = dtr;
    it->second->linesChanged = true;
    it->second->reportLines = true;
}

bool HostNetBridge::SerialSend(uint16_t socket, const uint8_t* data, uint32_t length)
{
    std::lock_guard<std::mutex> lock(_serialMutex);
    auto it = _serial.find(socket);
    if (it == _serial.end())
        return false;
    std::lock_guard<std::mutex> sendLock(it->second->sendMutex);
    if (it->second->sendQueue.size() + length <= _options.maxQueuedSendBytes)
        it->second->sendQueue.insert(it->second->sendQueue.end(), data, data + length);
    return true;
}

void HostNetBridge::CloseSerial(uint16_t socket)
{
    std::unique_ptr<SerialLink> link;
    {
        std::lock_guard<std::mutex> lock(_serialMutex);
        auto it = _serial.find(socket);
        if (it == _serial.end())
            return;
        link = std::move(it->second);
        _serial.erase(it);
    }
    link->stop = true;
    if (link->thread.joinable())
        link->thread.join();
}

void HostNetBridge::CloseAllSerial()
{
    std::map<uint16_t, std::unique_ptr<SerialLink>> links;
    {
        std::lock_guard<std::mutex> lock(_serialMutex);
        links.swap(_serial);
    }
    for (auto& [id, link] : links)
    {
        (void)id;
        link->stop = true;
        if (link->thread.joinable())
            link->thread.join();
    }
}

void HostNetBridge::RunSerial(uint16_t socket, std::string device, uint32_t baud, SerialLink* link)
{
    HostSerialPort port;
    std::string error;
    HostNetEvent opened;
    opened.socket = socket;
    if (!port.Open(device, baud, error))
    {
        opened.type = NetEventType::ConnectFailed;
        opened.status = NetEventStatus::Unreachable;
        opened.data.assign(error.begin(), error.end());   // the reason, for the log
        Emit(std::move(opened));
        return;
    }
    opened.type = NetEventType::Connected;
    Emit(std::move(opened));

    std::vector<uint8_t> buffer(512);
    std::vector<uint8_t> out;
    int lastLines = -1;
    while (!link->stop)
    {
        // What the machine asked for since the last pass
        bool applyLine = false, applyLines = false, report = false;
        SerialLine line;
        bool rts = false, dtr = false;
        {
            std::lock_guard<std::mutex> lock(link->sendMutex);
            applyLine = link->lineChanged;
            link->lineChanged = false;
            line = link->line;
            applyLines = link->linesChanged;
            link->linesChanged = false;
            rts = link->rts;
            dtr = link->dtr;
            report = link->reportLines;
        }
        if (applyLine)
        {
            std::string lineError;
            (void)port.Configure(line, lineError);   // a rate the adapter refuses keeps the previous one
        }
        if (applyLines)
            port.SetModemLines(rts, dtr);
        if (report)
        {
            const int lines = port.ModemStatus();
            if (lines >= 0 && lines != lastLines)
            {
                lastLines = lines;
                HostNetEvent changed;
                changed.type = NetEventType::ModemLines;
                changed.socket = socket;
                changed.data.push_back(static_cast<uint8_t>(lines));
                Emit(std::move(changed));
            }
        }

        const int n = port.Read(buffer.data(), buffer.size(), 10);
        if (n < 0)
        {
            HostNetEvent gone;
            gone.type = NetEventType::Reset;
            gone.socket = socket;
            Emit(std::move(gone));
            return;
        }
        if (n > 0)
        {
            HostNetEvent data;
            data.type = NetEventType::Data;
            data.socket = socket;
            data.data.assign(buffer.begin(), buffer.begin() + n);
            Emit(std::move(data));
        }
        {
            std::lock_guard<std::mutex> lock(link->sendMutex);
            out.assign(link->sendQueue.begin(), link->sendQueue.end());
            link->sendQueue.clear();
        }
        if (!out.empty() && !port.Write(out.data(), out.size()))
        {
            HostNetEvent gone;
            gone.type = NetEventType::Reset;
            gone.socket = socket;
            Emit(std::move(gone));
            return;
        }
    }
}

void HostNetBridge::Emit(HostNetEvent ev)
{
    std::lock_guard<std::mutex> lock(_eventMutex);
    _events.push_back(std::move(ev));
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

void HostNetBridge::Run()
{
    std::vector<netsock::PollItem> items;
    std::vector<uint16_t> ids;

    while (!_stop)
    {
        std::vector<Command> commands;
        {
            std::lock_guard<std::mutex> lock(_commandMutex);
            commands.swap(_commands);
        }
        for (Command& cmd : commands)
            Execute(cmd);

        // Backpressure towards the guest: while the virtual network has not
        // taken the events, stop reading (TCP flow control does the rest)
        bool eventsFull;
        {
            std::lock_guard<std::mutex> lock(_eventMutex);
            eventsFull = _events.size() >= _options.maxQueuedEvents;
        }

        items.clear();
        ids.clear();
        const uint64_t now = NowMs();
        for (auto& [id, s] : _sockets)
        {
            if (s.state == State::Connecting && now >= s.connectDeadlineMs)
            {
                Emit({NetEventType::ConnectFailed, NetEventStatus::Timeout, id, {}, {}});
                netsock::Close(s.handle);
                s.handle = netsock::kInvalid;
                continue;
            }
            if (s.handle == netsock::kInvalid)
                continue;
            netsock::PollItem item;
            item.handle = s.handle;
            item.wantRead = !eventsFull && !s.readClosed && s.state != State::Connecting;
            item.wantWrite = s.state == State::Connecting || !s.sendQueue.empty();
            items.push_back(item);
            ids.push_back(id);
        }
        // Sockets whose connect timed out: forget them
        for (auto it = _sockets.begin(); it != _sockets.end();)
        {
            if (it->second.handle == netsock::kInvalid)
                it = _sockets.erase(it);
            else
                ++it;
        }

        if (items.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (netsock::Poll(items, 10) <= 0)
            continue;

        for (size_t i = 0; i < items.size(); ++i)
        {
            auto it = _sockets.find(ids[i]);
            if (it != _sockets.end())
                Service(ids[i], it->second, items[i]);
        }
    }
}

void HostNetBridge::Execute(Command& cmd)
{
    switch (cmd.type)
    {
        case CommandType::Connect:
        {
            Drop(cmd.socket);
            HostSocket s;
            s.handle = netsock::OpenTcp();
            if (s.handle == netsock::kInvalid)
            {
                Emit({NetEventType::ConnectFailed, NetEventStatus::Error, cmd.socket, cmd.endpoint, {}});
                return;
            }
            const netsock::Result r = netsock::Connect(s.handle, cmd.endpoint);
            if (r == netsock::Result::Ok)
            {
                s.state = State::Connected;
                Emit({NetEventType::Connected, NetEventStatus::Ok, cmd.socket, cmd.endpoint, {}});
            }
            else if (r == netsock::Result::WouldBlock)
            {
                s.state = State::Connecting;
                s.connectDeadlineMs = NowMs() + _options.connectTimeoutMs;
            }
            else
            {
                netsock::Close(s.handle);
                const NetEventStatus status = r == netsock::Result::Refused       ? NetEventStatus::Refused
                                              : r == netsock::Result::Unreachable ? NetEventStatus::Unreachable
                                              : r == netsock::Result::Timeout     ? NetEventStatus::Timeout
                                                                                  : NetEventStatus::Error;
                Emit({NetEventType::ConnectFailed, status, cmd.socket, cmd.endpoint, {}});
                return;
            }
            _sockets[cmd.socket] = std::move(s);
            break;
        }

        case CommandType::Send:
        {
            auto it = _sockets.find(cmd.socket);
            if (it == _sockets.end() || it->second.state != State::Connected)
                return;
            HostSocket& s = it->second;
            if (s.sendQueue.size() + cmd.data.size() > _options.maxQueuedSendBytes)
            {
                Emit({NetEventType::Reset, NetEventStatus::Error, cmd.socket, {}, {}});
                Drop(cmd.socket);
                return;
            }
            s.sendQueue.insert(s.sendQueue.end(), cmd.data.begin(), cmd.data.end());
            FlushSend(cmd.socket, s);
            break;
        }

        case CommandType::ShutdownWrite:
        {
            auto it = _sockets.find(cmd.socket);
            if (it == _sockets.end())
                return;
            it->second.shutdownPending = true;
            FlushSend(cmd.socket, it->second);
            break;
        }

        case CommandType::Listen:
        {
            Drop(cmd.socket);
            HostSocket s;
            s.handle = netsock::OpenTcp();
            if (s.handle == netsock::kInvalid ||
                netsock::Bind(s.handle, cmd.endpoint.addr, cmd.endpoint.port, true) != netsock::Result::Ok ||
                netsock::Listen(s.handle, 4) != netsock::Result::Ok)
            {
                netsock::Close(s.handle);
                Emit({NetEventType::ListenFailed, NetEventStatus::AddressInUse, cmd.socket, cmd.endpoint, {}});
                return;
            }
            s.state = State::Listening;
            {
                std::lock_guard<std::mutex> lock(_listenerPortMutex);
                _listenerPorts[cmd.socket] = netsock::LocalPort(s.handle);
            }
            _sockets[cmd.socket] = std::move(s);
            break;
        }

        case CommandType::UdpSend:
        {
            auto it = _sockets.find(cmd.socket);
            if (it == _sockets.end())
            {
                HostSocket s;
                s.handle = netsock::OpenUdp();
                if (s.handle == netsock::kInvalid ||
                    netsock::Bind(s.handle, 0, 0, false) != netsock::Result::Ok)
                {
                    netsock::Close(s.handle);
                    return;
                }
                s.state = State::Udp;
                it = _sockets.emplace(cmd.socket, std::move(s)).first;
            }
            if (it->second.state == State::Udp)
                netsock::SendTo(it->second.handle, cmd.endpoint, cmd.data.data(), cmd.data.size());
            break;
        }

        case CommandType::Close:
            Drop(cmd.socket);
            break;

        case CommandType::CloseAll:
            for (auto& [id, s] : _sockets)
                netsock::Close(s.handle);
            _sockets.clear();
            {
                std::lock_guard<std::mutex> lock(_listenerPortMutex);
                _listenerPorts.clear();
            }
            {
                std::lock_guard<std::mutex> lock(_eventMutex);
                _events.clear();
            }
            break;
    }
}

void HostNetBridge::Drop(uint16_t id)
{
    auto it = _sockets.find(id);
    if (it == _sockets.end())
        return;
    netsock::Close(it->second.handle);
    _sockets.erase(it);
    std::lock_guard<std::mutex> lock(_listenerPortMutex);
    _listenerPorts.erase(id);
}

void HostNetBridge::FlushSend(uint16_t id, HostSocket& s)
{
    if (s.state != State::Connected)
        return;
    uint8_t chunk[16 * 1024];
    while (!s.sendQueue.empty())
    {
        const size_t n = s.sendQueue.size() < sizeof(chunk) ? s.sendQueue.size() : sizeof(chunk);
        std::copy(s.sendQueue.begin(), s.sendQueue.begin() + static_cast<std::ptrdiff_t>(n), chunk);
        size_t done = 0;
        const netsock::Result r = netsock::Send(s.handle, chunk, n, done);
        if (r == netsock::Result::WouldBlock)
            return;
        if (r != netsock::Result::Ok)
        {
            Emit({NetEventType::Reset, NetEventStatus::Error, id, {}, {}});
            netsock::Close(s.handle);
            s.handle = netsock::kInvalid;
            s.sendQueue.clear();
            return;
        }
        s.sendQueue.erase(s.sendQueue.begin(), s.sendQueue.begin() + static_cast<std::ptrdiff_t>(done));
        if (done < n)
            return;
    }
    if (s.shutdownPending)
    {
        netsock::ShutdownWrite(s.handle);
        s.shutdownPending = false;
    }
}

void HostNetBridge::Service(uint16_t id, HostSocket& s, const netsock::PollItem& item)
{
    uint8_t buffer[16 * 1024];

    switch (s.state)
    {
        case State::Connecting:
            if (!item.writable && !item.failed)
                return;
            if (const netsock::Result r = netsock::ConnectResult(s.handle); r == netsock::Result::Ok)
            {
                s.state = State::Connected;
                Emit({NetEventType::Connected, NetEventStatus::Ok, id, {}, {}});
                FlushSend(id, s);
            }
            else
            {
                const NetEventStatus status = r == netsock::Result::Refused       ? NetEventStatus::Refused
                                              : r == netsock::Result::Unreachable ? NetEventStatus::Unreachable
                                              : r == netsock::Result::Timeout     ? NetEventStatus::Timeout
                                                                                  : NetEventStatus::Refused;
                Emit({NetEventType::ConnectFailed, status, id, {}, {}});
                netsock::Close(s.handle);
                s.handle = netsock::kInvalid;
            }
            return;

        case State::Connected:
            if (item.writable)
                FlushSend(id, s);
            if (s.handle == netsock::kInvalid)
                return;
            if (item.readable || item.failed)
            {
                size_t done = 0;
                const netsock::Result r = netsock::Recv(s.handle, buffer, sizeof(buffer), done);
                if (r == netsock::Result::Ok && done)
                {
                    Emit({NetEventType::Data, NetEventStatus::Ok, id, {}, std::vector<uint8_t>(buffer, buffer + done)});
                }
                else if (r == netsock::Result::Closed)
                {
                    s.readClosed = true;
                    Emit({NetEventType::PeerClosed, NetEventStatus::Ok, id, {}, {}});
                }
                else if (r != netsock::Result::WouldBlock)
                {
                    Emit({NetEventType::Reset, NetEventStatus::Error, id, {}, {}});
                    netsock::Close(s.handle);
                    s.handle = netsock::kInvalid;
                }
            }
            return;

        case State::Listening:
            if (item.readable)
            {
                NetEndpoint peer;
                netsock::Handle h = netsock::Accept(s.handle, peer);
                if (h == netsock::kInvalid)
                    return;
                uint16_t newId = _nextAcceptedId++;
                if (_nextAcceptedId == 0)
                    _nextAcceptedId = IHostNet::kFirstAcceptedId;
                HostSocket conn;
                conn.handle = h;
                conn.state = State::Connected;
                _sockets[newId] = std::move(conn);
                HostNetEvent ev{NetEventType::Accepted, NetEventStatus::Ok, id, peer, {}};
                ev.data = {static_cast<uint8_t>(newId & 0xFF), static_cast<uint8_t>(newId >> 8)};
                Emit(std::move(ev));
            }
            return;

        case State::Udp:
            if (item.readable)
            {
                size_t done = 0;
                NetEndpoint from;
                if (netsock::RecvFrom(s.handle, buffer, sizeof(buffer), done, from) == netsock::Result::Ok)
                    Emit({NetEventType::Datagram, NetEventStatus::Ok, id, from,
                          std::vector<uint8_t>(buffer, buffer + done)});
            }
            return;
    }
}

// ---------------------------------------------------------------------------
// Resolver thread: answers DNS queries from the host resolver
// ---------------------------------------------------------------------------

void HostNetBridge::RunResolver()
{
    while (!_stop)
    {
        DnsJob job;
        Resolver resolver;
        {
            std::unique_lock<std::mutex> lock(_dnsMutex);
            _dnsCv.wait_for(lock, std::chrono::milliseconds(100), [this] { return _stop || !_dnsJobs.empty(); });
            if (_stop || _dnsJobs.empty())
                continue;
            job = std::move(_dnsJobs.front());
            _dnsJobs.pop_front();
            resolver = _resolver;
        }

        dns::Question q;
        if (!dns::ParseQuery(job.query.data(), job.query.size(), q))
            continue;  // not a query we can answer: a real server would drop it too

        std::vector<uint32_t> addresses;
        uint8_t rcode = dns::kRcodeNoError;
        if (q.qclass != dns::kClassIn || q.qtype != dns::kTypeA)
        {
            // Only A records: an empty NOERROR answer lets the program fall back
        }
        else if (!resolver || !resolver(q.name, addresses))
        {
            rcode = dns::kRcodeNxDomain;
        }

        HostNetEvent ev{NetEventType::Datagram, NetEventStatus::Ok, job.socket, job.server, {}};
        ev.data = dns::BuildAnswer(job.query.data(), job.query.size(), q, addresses, rcode);
        if (!ev.data.empty())
            Emit(std::move(ev));
    }
}

// ---------------------------------------------------------------------------
// Ping thread: ICMP echo through the host (netsock::Ping)
// ---------------------------------------------------------------------------

void HostNetBridge::RunPinger()
{
    while (!_stop)
    {
        DnsJob job;
        {
            std::unique_lock<std::mutex> lock(_pingMutex);
            _pingCv.wait_for(lock, std::chrono::milliseconds(100), [this] { return _stop || !_pingJobs.empty(); });
            if (_stop || _pingJobs.empty())
                continue;
            job = std::move(_pingJobs.front());
            _pingJobs.pop_front();
        }
        std::vector<uint8_t> reply;
        if (netsock::Ping(job.server, job.query.data(), job.query.size(), kPingTimeoutMs, reply))
            Emit({NetEventType::EchoReply, NetEventStatus::Ok, job.socket, NetEndpoint{job.server.addr, 0}, std::move(reply)});
    }
}
