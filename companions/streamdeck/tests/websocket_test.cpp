// The WebSocket client and the event loop: the frame codec on RFC 6455's own examples, then whole connections to a
// small server on the same loop over loopback (handshake, echo, fragments, ping, close, refusal, reconnect).
#include "sdb/websocket.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "sdb/eventloop.h"

using namespace sdb;
using namespace std::chrono_literals;

namespace
{

std::string Bytes(std::initializer_list<int> list)
{
    std::string s;
    for (int b : list)
        s += static_cast<char>(b);
    return s;
}

// Runs the loop until done() or the deadline; true when done() came true.
bool RunUntil(net::EventLoop& loop, const std::function<bool()>& done, std::chrono::milliseconds limit = 2000ms)
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
    int accepted = 0;

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
            CHECK(f.masked);  // a client masks every frame
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
                if (f.payload == "fragments")
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

void TestCodec()
{
    // RFC 6455 §1.3 handshake example.
    CHECK(ws::AcceptKey("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    CHECK(ws::Base64("") == "");
    CHECK(ws::Base64("f") == "Zg==");
    CHECK(ws::Base64("fo") == "Zm8=");
    CHECK(ws::Base64("foobar") == "Zm9vYmFy");

    // RFC 6455 §5.7: an unmasked and a masked "Hello".
    CHECK(ws::EncodeFrame(ws::Opcode::Text, "Hello") == Bytes({0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f}));
    const std::uint8_t mask[4] = {0x37, 0xfa, 0x21, 0x3d};
    const std::string masked = Bytes({0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58});
    CHECK(ws::EncodeFrame(ws::Opcode::Text, "Hello", true, mask) == masked);

    std::string buf = masked;
    ws::Frame f;
    CHECK(ws::DecodeFrame(buf, f, 1024, nullptr) == ws::DecodeResult::Frame);
    CHECK(f.payload == "Hello" && f.masked && f.fin && f.op == ws::Opcode::Text && buf.empty());

    // A fragmented unmasked "Hel" + "lo", fed byte by byte: NeedMore until each frame is whole.
    std::string stream = Bytes({0x01, 0x03, 0x48, 0x65, 0x6c, 0x80, 0x02, 0x6c, 0x6f});
    std::string in;
    std::vector<ws::Frame> frames;
    for (char c : stream)
    {
        in += c;
        ws::Frame one;
        if (ws::DecodeFrame(in, one, 1024, nullptr) == ws::DecodeResult::Frame)
            frames.push_back(one);
    }
    CHECK_EQ(frames.size(), 2u);
    CHECK(frames.size() == 2 && !frames[0].fin && frames[0].op == ws::Opcode::Text && frames[0].payload == "Hel");
    CHECK(frames.size() == 2 && frames[1].fin && frames[1].op == ws::Opcode::Continuation && frames[1].payload == "lo");

    // 16-bit and 64-bit lengths round-trip.
    for (std::size_t n :
         {std::size_t{125}, std::size_t{126}, std::size_t{65535}, std::size_t{65536}, std::size_t{70000}})
    {
        const std::string payload(n, 'x');
        std::string b = ws::EncodeFrame(ws::Opcode::Binary, payload, true, mask);
        CHECK_EQ(b.size(), n + 4 + (n < 126 ? 2 : n <= 0xFFFF ? 4 : 10));
        ws::Frame g;
        CHECK(ws::DecodeFrame(b, g, 1u << 20, nullptr) == ws::DecodeResult::Frame && g.payload == payload);
    }

    // Errors: reserved bits, unknown opcode, fragmented or long control frame, too big.
    int code = 0;
    std::string bad = Bytes({0xC1, 0x00});
    CHECK(ws::DecodeFrame(bad, f, 1024, &code) == ws::DecodeResult::Error && code == ws::kCloseProtocolError);
    bad = Bytes({0x83, 0x00});
    CHECK(ws::DecodeFrame(bad, f, 1024, &code) == ws::DecodeResult::Error);
    bad = Bytes({0x09, 0x00});
    CHECK(ws::DecodeFrame(bad, f, 1024, &code) == ws::DecodeResult::Error);
    bad = Bytes({0x89, 0x7E, 0x00, 0x80});
    CHECK(ws::DecodeFrame(bad, f, 1024, &code) == ws::DecodeResult::Error);
    bad = ws::EncodeFrame(ws::Opcode::Text, std::string(2000, 'y'));
    CHECK(ws::DecodeFrame(bad, f, 1024, &code) == ws::DecodeResult::Error && code == ws::kCloseTooBig);

    ws::ParsedUrl u;
    std::string error;
    CHECK(ws::ParseUrl("ws://127.0.0.1:8090/api/v1/ws", &u, &error) && u.host == "127.0.0.1" && u.port == 8090 &&
          u.path == "/api/v1/ws");
    CHECK(ws::ParseUrl("ws://localhost", &u, &error) && u.port == 80 && u.path == "/");
    CHECK(ws::ParseUrl("ws://[::1]:9/x?y=1", &u, &error) && u.host == "::1" && u.port == 9 && u.path == "/x?y=1");
    CHECK(!ws::ParseUrl("wss://h/", &u, &error) && error.find("wss") != std::string::npos);
    CHECK(!ws::ParseUrl("http://h/", &u, &error));
    CHECK(!ws::ParseUrl("ws://h:99999/", &u, &error));
}

void TestLoop()
{
    net::EventLoop loop;
    std::vector<int> order;
    loop.After(2ms, [&] { order.push_back(2); });
    loop.After(1ms, [&] { order.push_back(1); });
    const auto cancelled = loop.After(1ms, [&] { order.push_back(99); });
    loop.Cancel(cancelled);
    int ticks = 0;
    const auto every = loop.Every(1ms, [&] { ++ticks; });
    CHECK(RunUntil(loop, [&] { return order.size() == 2 && ticks >= 3; }));
    loop.Cancel(every);
    CHECK(order == std::vector<int>({1, 2}));

    // Post from another thread wakes a loop that waits without a timer.
    bool posted = false;
    std::thread other([&] { loop.Post([&] { posted = true; }); });
    const auto t0 = std::chrono::steady_clock::now();
    while (!posted && std::chrono::steady_clock::now() - t0 < 2s)
        loop.RunOnce(1000);
    other.join();
    CHECK(posted);
    CHECK(std::chrono::steady_clock::now() - t0 < 500ms);  // woken, not timed out

    loop.Stop();
    CHECK(!loop.RunOnce(0));
}

ws::ClientOptions Options(const std::string& url)
{
    ws::ClientOptions o;
    o.url = url;
    o.headers = {{"Authorization", "Bearer unc_test"}};
    o.reconnectMin = 1ms;
    o.reconnectMax = 4ms;
    o.pingInterval = 0ms;
    return o;
}

void TestConnection()
{
    net::EventLoop loop;
    TestServer server(loop);
    ws::Client client(loop, Options(server.Url()));
    std::vector<std::string> texts;
    std::vector<int> closeCodes;
    int opens = 0;
    client.onOpen = [&] {
        ++opens;
        CHECK(client.SendText("hello"));
    };
    client.onText = [&](std::string_view t) { texts.emplace_back(t); };
    client.onClose = [&](int code, std::string_view) { closeCodes.push_back(code); };
    client.Start();

    CHECK(RunUntil(loop, [&] { return !texts.empty(); }));
    CHECK(opens == 1 && texts.size() == 1 && texts[0] == "hello");
    CHECK(!server.conns.empty() && server.conns[0]->request.find("GET /api/v1/ws HTTP/1.1") == 0);
    CHECK(!server.conns.empty() &&
          server.conns[0]->request.find("Authorization: Bearer unc_test") != std::string::npos);

    // A big message crosses many frames of the socket buffer and comes back whole.
    const std::string big(300000, 'z');
    CHECK(client.SendText(big));
    CHECK(RunUntil(loop, [&] { return texts.size() == 2; }));
    CHECK(texts.size() == 2 && texts[1] == big);

    // Fragments with a ping between them.
    CHECK(client.SendText("fragments"));
    CHECK(RunUntil(loop, [&] { return texts.size() == 3; }));
    CHECK(texts.size() == 3 && texts[2] == "fragments");  // the echo of the request itself is not sent
    CHECK(RunUntil(loop, [&] { return texts.size() == 3 && client.Buffered() == 0; }));

    // Our close: the server answers, the client reports 1000 and stops.
    client.Close(ws::kCloseNormal, "done");
    CHECK(RunUntil(loop, [&] { return client.GetState() == ws::Client::State::Stopped; }));
    CHECK(server.closes == 1);
    CHECK(closeCodes == std::vector<int>({ws::kCloseNormal}));
    CHECK(!client.SendText("late"));
    CHECK_EQ(client.Connections(), 1);
}

void TestFragmentsAndPing()
{
    net::EventLoop loop;
    TestServer server(loop);
    ws::Client client(loop, Options(server.Url()));
    std::vector<std::string> texts;
    client.onOpen = [&] { (void)client.SendText("fragments"); };
    client.onText = [&](std::string_view t) { texts.emplace_back(t); };
    client.Start();
    CHECK(RunUntil(loop, [&] { return !texts.empty(); }));
    CHECK(texts.size() == 1 && texts[0] == "fragments");
    // The pong to the server's ping is a masked control frame; the server counts pings only from the client, so
    // check the round trip the other way: the client pings on its timer and the server answers.
    ws::ClientOptions o = Options(server.Url());
    o.pingInterval = 2ms;
    ws::Client pinger(loop, o);
    pinger.Start();
    CHECK(RunUntil(loop, [&] { return server.pings >= 2; }));
    CHECK(pinger.IsOpen());
}

void TestSilentClose()
{
    net::EventLoop loop;
    TestServer server(loop);
    server.mode = TestServer::Mode::SilentClose;
    ws::Client client(loop, Options(server.Url()));
    int code = 0;
    client.onOpen = [&] { client.Close(ws::kCloseGoingAway, "bye"); };
    client.onClose = [&](int c, std::string_view) { code = c; };
    client.Start();
    CHECK(RunUntil(loop, [&] { return code != 0; }));
    CHECK(code == ws::kCloseGoingAway && server.closes == 1);
    CHECK(client.GetState() == ws::Client::State::Stopped);
}

void TestServerClose()
{
    net::EventLoop loop;
    TestServer server(loop);
    ws::ClientOptions o = Options(server.Url());
    o.reconnect = false;
    ws::Client client(loop, o);
    int code = 0;
    std::string reason;
    client.onOpen = [&] { (void)client.SendText("bye"); };
    client.onClose = [&](int c, std::string_view r) {
        code = c;
        reason = r;
    };
    client.Start();
    CHECK(RunUntil(loop, [&] { return code != 0; }));
    CHECK(code == ws::kCloseGoingAway && reason == "going");
    CHECK(client.GetState() == ws::Client::State::Stopped);
}

void TestRefusedAndReconnect()
{
    net::EventLoop loop;
    TestServer server(loop);
    server.mode = TestServer::Mode::Refuse;
    ws::Client client(loop, Options(server.Url()));
    std::vector<std::string> reasons;
    int opens = 0;
    client.onOpen = [&] { ++opens; };
    client.onClose = [&](int code, std::string_view r) {
        CHECK(code == ws::kCloseAbnormal);
        reasons.emplace_back(r);
    };
    client.Start();
    // Refused twice (with backoff), then the server lets it in.
    CHECK(RunUntil(loop, [&] { return reasons.size() >= 2; }));
    CHECK(!reasons.empty() && reasons[0].find("401") != std::string::npos);
    CHECK(opens == 0);
    server.mode = TestServer::Mode::Echo;
    CHECK(RunUntil(loop, [&] { return opens == 1; }));

    // A dropped connection (no close frame) comes back by itself.
    server.mode = TestServer::Mode::DropAfterOpen;
    const auto before = reasons.size();
    for (auto& c : server.conns)
        server.Kill(c.get());
    CHECK(RunUntil(loop, [&] { return reasons.size() > before; }));
    server.mode = TestServer::Mode::Echo;
    CHECK(RunUntil(loop, [&] { return client.IsOpen() && opens >= 2; }));
    CHECK(client.Connections() >= 2);
}

void TestNoServer()
{
    net::EventLoop loop;
    int port = 0;
    {
        // A port that was free a moment ago: nothing listens there now.
        std::string error;
        const auto s = net::Listen("127.0.0.1", 0, &port, &error);
        net::CloseSocket(s);
    }
    ws::ClientOptions o = Options("ws://127.0.0.1:" + std::to_string(port) + "/api/v1/ws");
    o.reconnect = false;
    ws::Client client(loop, o);
    int code = 0;
    client.onClose = [&](int c, std::string_view) { code = c; };
    client.Start();
    // 5 s: Windows retries a refused loopback connect for about 2 s before it reports the failure
    CHECK(RunUntil(loop, [&] { return code != 0; }, 5000ms));
    CHECK(code == ws::kCloseAbnormal && client.GetState() == ws::Client::State::Stopped);
    CHECK_EQ(loop.WatchCount(), 0u);

    ws::ClientOptions secure;
    secure.url = "wss://127.0.0.1/";
    ws::Client wss(loop, secure);
    std::string why;
    wss.onClose = [&](int, std::string_view r) { why = r; };
    wss.Start();
    CHECK(why.find("wss") != std::string::npos);
}

}  // namespace

void TestWebSocket()
{
    TestCodec();
    TestLoop();
    TestConnection();
    TestFragmentsAndPing();
    TestServerClose();
    TestSilentClose();
    TestRefusedAndReconnect();
    TestNoServer();
}
