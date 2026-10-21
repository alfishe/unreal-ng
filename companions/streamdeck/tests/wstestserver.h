// A WebSocket server on the client's loop for the tests (websocket_test.cpp, surfaceclient_test.cpp): handshake,
// echo, fragments, ping, close, refusal, drops; or a server of its own through onText (the fake surface server).
#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "sdb/eventloop.h"
#include "sdb/websocket.h"

namespace test
{

using namespace sdb;

inline std::string Bytes(std::initializer_list<int> list)
{
    std::string s;
    for (int b : list)
        s += static_cast<char>(b);
    return s;
}

// Runs the loop until done() or the deadline; true when done() came true.
inline bool RunUntil(net::EventLoop& loop, const std::function<bool()>& done,
                     std::chrono::milliseconds limit = std::chrono::milliseconds(2000))
{
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!done())
    {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        loop.RunOnce(5);
    }
    return true;
}

// A WebSocket server for the tests, on the client's loop. Behaviour per connection is chosen by `mode`.
struct TestServer
{
    enum class Mode
    {
        Echo,           // echoes every message, as fragments when asked
        Refuse,         // answers 401
        DropAfterOpen,  // upgrades, then closes the TCP connection without a close frame
        SilentClose     // answers our close frame by closing the connection (as Drogon does)
    };

    struct Conn
    {
        net::SocketHandle s = net::kInvalidSocket;
        std::string in, out;
        bool upgraded = false;
        std::string request;
    };

    net::EventLoop& loop;
    Mode mode = Mode::Echo;
    net::SocketHandle listener = net::kInvalidSocket;
    int port = 0;
    std::vector<std::unique_ptr<Conn>> conns;
    std::vector<std::string> received;  // text messages
    int pings = 0;
    int closes = 0;
    int unmasked = 0;  // frames from the client without a mask (checked once, at the end: the frame count is timing's)
    int accepted = 0;
    // A server of its own (the fake surface server): every text message goes here instead of the echo
    std::function<void(Conn*, const std::string&)> onText;

    explicit TestServer(net::EventLoop& l) : loop(l)
    {
        std::string error;
        listener = net::Listen("127.0.0.1", 0, &port, &error);
        loop.Watch(listener, true, false, [this](bool, bool, bool) {
            for (net::SocketHandle c = net::Accept(listener); c != net::kInvalidSocket; c = net::Accept(listener))
            {
                ++accepted;
                auto conn = std::make_unique<Conn>();
                conn->s = c;
                Conn* raw = conn.get();
                conns.push_back(std::move(conn));
                loop.Watch(c, true, false, [this, raw](bool readable, bool writable, bool error) {
                    if (writable)
                        Flush(raw);
                    if (readable || error)
                        OnRead(raw);
                });
            }
        });
    }
    ~TestServer()
    {
        CHECK_EQ(unmasked, 0);  // a client masks every frame
        for (auto& c : conns)
            Kill(c.get());
        loop.Unwatch(listener);
        net::CloseSocket(listener);
    }

    std::string Url(const std::string& path = "/api/v1/ws") const
    {
        return "ws://127.0.0.1:" + std::to_string(port) + path;
    }

    void Kill(Conn* c)
    {
        if (c->s == net::kInvalidSocket)
            return;
        loop.Unwatch(c->s);
        net::CloseSocket(c->s);
        c->s = net::kInvalidSocket;
    }

    // A text message to the client (unmasked, as a server sends)
    void SendText(Conn* c, const std::string& text)
    {
        Write(c, ws::EncodeFrame(ws::Opcode::Text, text));
    }
    // The open connections, newest last
    Conn* Last()
    {
        for (auto it = conns.rbegin(); it != conns.rend(); ++it)
            if ((*it)->s != net::kInvalidSocket && (*it)->upgraded)
                return it->get();
        return nullptr;
    }

    void Write(Conn* c, const std::string& bytes)
    {
        c->out += bytes;
        Flush(c);
    }

    void Flush(Conn* c)
    {
        while (c->s != net::kInvalidSocket && !c->out.empty())
        {
            const long n = net::Send(c->s, c->out.data(), c->out.size());
            if (n <= 0)
                break;
            c->out.erase(0, static_cast<std::size_t>(n));
        }
        if (c->s != net::kInvalidSocket)
            loop.Modify(c->s, true, !c->out.empty());
    }

    void OnRead(Conn* c)
    {
        char buf[4096];
        for (;;)
        {
            const long n = net::Recv(c->s, buf, sizeof buf);
            if (n == 0)
                break;
            if (n < 0)
            {
                Kill(c);
                return;
            }
            c->in.append(buf, static_cast<std::size_t>(n));
        }
        if (!c->upgraded)
        {
            const auto end = c->in.find("\r\n\r\n");
            if (end == std::string::npos)
                return;
            c->request = c->in.substr(0, end);
            c->in.erase(0, end + 4);
            if (mode == Mode::Refuse)
            {
                Write(c, "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                Kill(c);
                return;
            }
            const auto at = c->request.find("Sec-WebSocket-Key: ");
            const std::string key = c->request.substr(at + 19, c->request.find("\r\n", at) - at - 19);
            Write(c,
                  "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                  "Sec-WebSocket-Accept: " +
                      ws::AcceptKey(key) + "\r\n\r\n");
            c->upgraded = true;
            if (mode == Mode::DropAfterOpen)
            {
                Kill(c);
                return;
            }
        }
        for (;;)
        {
            ws::Frame f;
            int code = 0;
            const auto r = ws::DecodeFrame(c->in, f, 1u << 20, &code);
            if (r != ws::DecodeResult::Frame)
                break;
            if (!f.masked)
                ++unmasked;
            if (f.op == ws::Opcode::Ping)
            {
                ++pings;
                Write(c, ws::EncodeFrame(ws::Opcode::Pong, f.payload));
            }
            else if (f.op == ws::Opcode::Close)
            {
                ++closes;
                if (mode != Mode::SilentClose)
                    Write(c, ws::EncodeFrame(ws::Opcode::Close, f.payload.substr(0, 2)));
                Kill(c);
                return;
            }
            else if (f.op == ws::Opcode::Text)
            {
                received.push_back(f.payload);
                if (onText)
                {
                    onText(c, f.payload);
                    if (c->s == net::kInvalidSocket)
                        return;
                }
                else if (f.payload == "fragments")
                {
                    // Three fragments with a ping in the middle: the client assembles one message, answers the ping.
                    Write(c, ws::EncodeFrame(ws::Opcode::Text, "frag", false));
                    Write(c, ws::EncodeFrame(ws::Opcode::Ping, "p"));
                    Write(c, ws::EncodeFrame(ws::Opcode::Continuation, "men", false));
                    Write(c, ws::EncodeFrame(ws::Opcode::Continuation, "ts", true));
                }
                else if (f.payload == "bye")
                {
                    Write(c, ws::EncodeFrame(ws::Opcode::Close, Bytes({0x03, 0xE9}) + "going"));  // 1001
                }
                else
                {
                    Write(c, ws::EncodeFrame(ws::Opcode::Text, f.payload));
                }
            }
        }
    }
};

}  // namespace test
