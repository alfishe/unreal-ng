#include "emulator/io/network/traffic/trafficstream.h"

#include <vector>

#include "common/network/netsockets.h"
#include "emulator/io/network/traffic/networktraffictap.h"

TrafficStream::~TrafficStream()
{
    Stop();
}

bool TrafficStream::Start(uint32_t address, uint16_t port, std::string& error)
{
    Stop();
    netsock::Startup();
    netsock::Handle listener = netsock::OpenTcp();
    if (listener == netsock::kInvalid)
    {
        error = "cannot open a socket";
        return false;
    }
    if (netsock::Bind(listener, address, port, true) != netsock::Result::Ok ||
        netsock::Listen(listener, 4) != netsock::Result::Ok)
    {
        netsock::Close(listener);
        error = "cannot listen on port " + std::to_string(port) + " (taken?)";
        return false;
    }
    _listener = listener;
    _port = netsock::LocalPort(listener);
    _running = true;
    _thread = std::thread(&TrafficStream::Loop, this);
    return true;
}

void TrafficStream::Stop()
{
    _running = false;
    if (_thread.joinable())
        _thread.join();
    if (_listener != netsock::kInvalid)
        netsock::Close(_listener);
    _listener = netsock::kInvalid;
    _port = 0;
    _clients = 0;
}

void TrafficStream::Loop()
{
    struct Client
    {
        netsock::Handle socket = netsock::kInvalid;
        uint32_t reader = 0;
        std::vector<uint8_t> out;   ///< taken from the tap, not yet sent
        size_t sent = 0;
    };
    std::vector<Client> clients;
    std::vector<uint8_t> chunk;
    while (_running)
    {
        std::vector<netsock::PollItem> items;
        netsock::PollItem l;
        l.handle = _listener;
        items.push_back(l);
        for (Client& c : clients)
        {
            // New bytes from the tap; a reader the tap dropped (too slow) is closed
            chunk.clear();
            if (!_tap.TakeLive(c.reader, chunk))
            {
                netsock::Close(c.socket);
                c.socket = netsock::kInvalid;
                continue;
            }
            c.out.insert(c.out.end(), chunk.begin(), chunk.end());
            netsock::PollItem p;
            p.handle = c.socket;
            p.wantWrite = c.sent < c.out.size();
            items.push_back(p);
        }
        netsock::Poll(items, 20);
        if (items[0].readable)
        {
            NetEndpoint peer;
            const netsock::Handle s = netsock::Accept(_listener, peer);
            if (s != netsock::kInvalid)
                clients.push_back(Client{s, _tap.AttachLive(), {}, 0});
        }
        size_t at = 1;
        for (Client& c : clients)
        {
            if (c.socket == netsock::kInvalid)
                continue;
            const netsock::PollItem& p = items[at++];
            if (p.failed || p.readable)
            {
                // A reader sends nothing: readable means it closed (or an error)
                uint8_t sink[256];
                size_t got = 0;
                const netsock::Result r = netsock::Recv(c.socket, sink, sizeof(sink), got);
                if (p.failed || r != netsock::Result::Ok || got == 0)
                {
                    netsock::Close(c.socket);
                    c.socket = netsock::kInvalid;
                    continue;
                }
            }
            if (p.writable && c.sent < c.out.size())
            {
                size_t done = 0;
                const netsock::Result r = netsock::Send(c.socket, c.out.data() + c.sent, c.out.size() - c.sent, done);
                if (r != netsock::Result::Ok && r != netsock::Result::WouldBlock)
                {
                    netsock::Close(c.socket);
                    c.socket = netsock::kInvalid;
                    continue;
                }
                c.sent += done;
                if (c.sent == c.out.size())
                {
                    c.out.clear();
                    c.sent = 0;
                }
            }
        }
        // Forget closed clients
        for (auto it = clients.begin(); it != clients.end();)
        {
            if (it->socket == netsock::kInvalid)
            {
                _tap.DetachLive(it->reader);
                it = clients.erase(it);
            }
            else
                ++it;
        }
        _clients = static_cast<unsigned>(clients.size());
    }
    for (Client& c : clients)
    {
        netsock::Close(c.socket);
        _tap.DetachLive(c.reader);
    }
}
