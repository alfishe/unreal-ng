// Stream Deck Studio without a window: the device wire v2 client (Join, Welcome and its errors, replay, input both
// ways, own-role feature reports, Leave, reconnect) against a small fake daemon in this process, and the --listen v1
// server. Qt offscreen, one thread; waits spin the event loop until a condition holds (no sleeps).
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "check.h"
#include "deckclient.h"
#include "listenserver.h"
#include "sdb/devicewire.h"
#include "sdb/emuwire.h"
#include "virtualdeck.h"

using nlohmann::json;
using namespace sdb;

namespace
{
// Spins the event loop until `done` or the timeout; true when done.
bool WaitFor(const std::function<bool()>& done, int timeoutMs = 2000)
{
    QElapsedTimer t;
    t.start();
    while (!done())
    {
        if (t.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

// Spins the event loop for a while (to see that something does NOT happen).
void SpinFor(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

// The daemon's side of the wire, as small as the tests need.
struct FakeDaemon
{
    QTcpServer server;
    QTcpSocket* peer = nullptr;
    wire::FrameReader reader;
    std::vector<wire::Frame> frames;
    int connections = 0;

    explicit FakeDaemon(quint16 port = 0)
    {
        Listen(port);
        QObject::connect(&server, &QTcpServer::newConnection, [this] {
            while (QTcpSocket* s = server.nextPendingConnection())
            {
                peer = s;
                reader = {};
                ++connections;
                QObject::connect(s, &QTcpSocket::readyRead, [this, s] {
                    if (s != peer)
                        return;
                    const QByteArray b = s->readAll();
                    reader.Feed(std::string_view(b.constData(), static_cast<std::size_t>(b.size())));
                    wire::Frame f;
                    while (reader.Next(f))
                        frames.push_back(f);
                });
            }
        });
    }
    bool Listen(quint16 port)
    {
        return server.listen(QHostAddress::LocalHost, port);
    }
    [[nodiscard]] quint16 Port() const
    {
        return server.serverPort();
    }
    void Send(std::uint8_t type, const std::string& payload)
    {
        if (peer)
        {
            peer->write(QByteArray::fromStdString(wire::EncodeFrame(type, payload)));
            peer->flush();
        }
    }
    void SendJson(std::uint8_t type, const json& j)
    {
        Send(type, j.dump());
    }
    // The first frame of a type, or null.
    const wire::Frame* Find(std::uint8_t type) const
    {
        for (const auto& f : frames)
            if (f.type == type)
                return &f;
        return nullptr;
    }
    bool WaitFrame(std::uint8_t type, int timeoutMs = 2000)
    {
        return WaitFor([&] { return Find(type) != nullptr; }, timeoutMs);
    }
};

json Welcome(const char* serial, int pid, bool real)
{
    return {{"v", 2}, {"deck", serial}, {"model", "whatever"}, {"pid", pid}, {"real", real}, {"daemon", "test"}};
}

std::string KeyReport(int keys, int down)
{
    std::string r(512, '\0');
    r[0] = 1;
    r[2] = static_cast<char>(keys);
    if (down >= 0)
        r[4 + static_cast<std::size_t>(down)] = 1;
    return r;
}

void TestJoinPayload()
{
    std::printf("join payload\n");
    DeckClient::Role mirror;
    const json m = json::parse(DeckClient::JoinPayload(mirror, QStringLiteral("EMU00C6")).toStdString());
    CHECK(m.value("v", 0) == 2);
    CHECK(m.value("role", "") == "mirror");
    CHECK(m.value("deck", "") == "first");
    CHECK(!m.contains("model"));
    DeckClient::Role own;
    own.own = true;
    own.model = QStringLiteral("neo");
    const json o = json::parse(DeckClient::JoinPayload(own, QStringLiteral("EMU009A")).toStdString());
    CHECK(o.value("role", "") == "own");
    CHECK(o.value("model", "") == "neo");
    CHECK(o.value("serial", "") == "EMU009A");
    CHECK(o.contains("name") && o.contains("pid"));
    CHECK(DeckClient::ModelToken(*FindModel(kElgatoVid, 0x00C6)) == QStringLiteral("plusxl"));
    CHECK(DeckClient::ModelToken(*FindModel(kElgatoVid, 0x009A)) == QStringLiteral("neo"));
    for (const auto& model : AllModels())  // every token reads back as its model
        CHECK(FindModelByName(DeckClient::ModelToken(model).toStdString()) == &model);
}

void TestMirrorSession()
{
    std::printf("mirror session\n");
    FakeDaemon d;
    VirtualDeck deck;
    DeckClient c(&deck);
    c.SetAddress(QStringLiteral("127.0.0.1"), d.Port());
    c.Start({});
    CHECK(d.WaitFrame(wire::Join));
    CHECK(c.GetState() == DeckClient::State::Joining);
    const json join = json::parse(d.Find(wire::Join)->payload);
    CHECK(join.value("role", "") == "mirror");

    d.SendJson(wire::Welcome, Welcome("A1B2C3", 0x0084, true));  // a Stream Deck +
    CHECK(WaitFor([&] { return c.GetState() == DeckClient::State::Joined; }));
    CHECK_EQ(deck.GetModel().pid, 0x0084);  // a mirror follows the deck's model
    CHECK(deck.Serial() == QStringLiteral("A1B2C3"));
    CHECK(c.Real());
    CHECK(c.Banner().isEmpty());

    // the replay: brightness 0x40, bracketed
    d.Send(wire::ReplayBegin, {});
    d.Send(wire::SetFeature, std::string("\x03\x08\x40", 3));
    CHECK(WaitFor([&] { return deck.Brightness() == 0x40; }));
    CHECK(c.InReplay());
    d.Send(wire::ReplayEnd, {});
    CHECK(WaitFor([&] { return !c.InReplay(); }));

    // a press on the real deck lights here; a page means a runtime draws its own presses
    d.Send(wire::HostInput, KeyReport(8, 2));
    CHECK(WaitFor([&] { return deck.RemoteKeyDown(2); }));
    d.Send(wire::HostInput, KeyReport(8, -1));
    CHECK(WaitFor([&] { return !deck.RemoteKeyDown(2); }));
    QString note;
    QObject::connect(&c, &DeckLink::HostNote, [&](const QString& s) { note = s; });
    d.Send(wire::HostNote, "pressed pause");
    d.Send(wire::HostPage, R"({"menu":"home"})");
    CHECK(WaitFor([&] { return deck.HostDrawsPresses() && note == QStringLiteral("pressed pause"); }));

    // a click here goes to the daemon as Input
    deck.PressButton(1, true);
    CHECK(d.WaitFrame(wire::Input));
    const auto& in = d.Find(wire::Input)->payload;
    CHECK(in.size() >= 6 && in[0] == 1 && in[5] == 1);

    // the deck unplugged, then back
    d.SendJson(wire::DeckState, {{"connected", false}});
    CHECK(WaitFor([&] { return !c.DeckConnected(); }));
    CHECK(!c.Banner().isEmpty());
    d.SendJson(wire::DeckState, {{"connected", true}, {"target", "W2 #3"}});
    CHECK(WaitFor([&] { return c.DeckConnected(); }));
    CHECK(c.Target() == QStringLiteral("W2 #3"));

    // an own-role-only request is not answered by a mirror
    d.Send(wire::GetFeature, std::string(32, '\x06'));
    c.Stop();
    CHECK(d.WaitFrame(wire::Leave));
    CHECK(d.Find(wire::FeatureReply) == nullptr);
    CHECK(c.GetState() == DeckClient::State::Stopped);
}

void TestWaitingForDeck()
{
    std::printf("no deck, waiting\n");
    FakeDaemon d;
    VirtualDeck deck;
    DeckClient c(&deck);
    c.SetAddress(QStringLiteral("127.0.0.1"), d.Port());
    c.Start({});
    CHECK(d.WaitFrame(wire::Join));
    d.SendJson(wire::Welcome, {{"v", 2}, {"error", "no_deck"}, {"waiting", true}});
    CHECK(WaitFor([&] { return c.GetState() == DeckClient::State::Waiting; }));
    CHECK(c.Banner().contains(QStringLiteral("waiting for a Stream Deck")));
    d.SendJson(wire::Welcome, Welcome("XL0001", 0x006C, true));  // a deck attached
    CHECK(WaitFor([&] { return c.GetState() == DeckClient::State::Joined; }));
    CHECK_EQ(deck.GetModel().pid, 0x006C);
    CHECK_EQ(d.connections, 1);
}

void TestRefused()
{
    std::printf("refused joins: no retry\n");
    for (const char* error : {"version", "bad_role", "bad_model"})
    {
        FakeDaemon d;
        VirtualDeck deck;
        DeckClient c(&deck);
        c.SetAddress(QStringLiteral("127.0.0.1"), d.Port());
        c.SetRetryDelays(10, 20);
        c.Start({});
        CHECK(d.WaitFrame(wire::Join));
        d.SendJson(wire::Welcome, {{"v", 2}, {"error", error}});
        d.peer->disconnectFromHost();  // the daemon closes after an error
        CHECK(WaitFor([&] { return c.GetState() == DeckClient::State::Failed; }));
        SpinFor(80);  // several retry periods
        CHECK_EQ(d.connections, 1);
        CHECK(c.GetState() == DeckClient::State::Failed);
        CHECK(!c.Error().isEmpty());
    }
}

void TestOwnRole()
{
    std::printf("own role\n");
    FakeDaemon d;
    VirtualDeck deck;
    DeckClient c(&deck);
    c.SetAddress(QStringLiteral("127.0.0.1"), d.Port());
    DeckClient::Role role;
    role.own = true;
    role.model = QStringLiteral("neo");
    c.Start(role);
    CHECK_EQ(deck.GetModel().pid, 0x009A);  // shown while it joins
    CHECK(d.WaitFrame(wire::Join));
    CHECK(json::parse(d.Find(wire::Join)->payload).value("model", "") == "neo");
    d.SendJson(wire::Welcome, Welcome("EMU7", 0x009A, false));
    CHECK(WaitFor([&] { return c.GetState() == DeckClient::State::Joined; }));
    CHECK(!c.Real());
    std::string req(32, '\0');
    req[0] = 0x06;  // the serial
    d.Send(wire::GetFeature, req);
    CHECK(d.WaitFrame(wire::FeatureReply));
    const std::string& rep = d.Find(wire::FeatureReply)->payload;
    CHECK(rep.size() >= 32 && rep[0] == 0x06 && rep.substr(2, 4) == "EMU7");
}

void TestReconnect()
{
    std::printf("reconnect with backoff\n");
    quint16 port = 0;
    {
        QTcpServer probe;  // a free port, then nobody there
        probe.listen(QHostAddress::LocalHost, 0);
        port = probe.serverPort();
    }
    VirtualDeck deck;
    DeckClient c(&deck);
    c.SetAddress(QStringLiteral("127.0.0.1"), port);
    c.SetRetryDelays(10, 40);
    c.Start({});
    SpinFor(60);
    CHECK(c.GetState() == DeckClient::State::Connecting);
    CHECK(c.Banner().contains(QStringLiteral("waiting for unreal-streamdeck")));
    FakeDaemon d(port);  // the daemon comes up
    CHECK(d.WaitFrame(wire::Join));
    d.SendJson(wire::Welcome, Welcome("A1", 0x00C6, true));
    CHECK(WaitFor([&] { return c.GetState() == DeckClient::State::Joined; }));

    // the daemon goes away and comes back: a new connection and Join
    d.frames.clear();
    d.peer->abort();
    CHECK(WaitFor([&] { return c.GetState() == DeckClient::State::Connecting; }));
    CHECK(d.WaitFrame(wire::Join));
    CHECK_EQ(d.connections, 2);

    // a broken frame (a length over the limit) drops the connection; the client comes back
    d.frames.clear();
    std::string bad(5, '\xFF');
    bad[0] = static_cast<char>(wire::Output);
    d.peer->write(bad.data(), static_cast<qint64>(bad.size()));
    CHECK(d.WaitFrame(wire::Join));
    CHECK_EQ(d.connections, 3);
}

void TestListenServer()
{
    std::printf("--listen (v1 server)\n");
    VirtualDeck deck;
    ListenServer s(&deck);
    QString err;
    CHECK(s.Listen(0, false, &err));
    quint16 asked = 0;
    QObject::connect(&s, &ListenServer::ModelRequested, [&](quint16 pid) { asked = pid; });
    QTcpSocket host;
    host.connectToHost(QHostAddress::LocalHost, s.Port());
    QByteArray rx;
    QObject::connect(&host, &QTcpSocket::readyRead, [&] { rx += host.readAll(); });
    // Hello: [0x10][u16 len][u8 version][u16 vid][u16 pid][u8 n][serial]
    CHECK(WaitFor([&] { return rx.size() >= 9; }));
    CHECK_EQ(static_cast<quint8>(rx[0]), emuwire::Hello);
    CHECK_EQ(qFromLittleEndian<quint16>(rx.constData() + 6), deck.GetModel().pid);
    rx.clear();
    auto send = [&](quint8 type, const QByteArray& payload) {
        QByteArray f(3, '\0');
        f[0] = static_cast<char>(type);
        qToLittleEndian<quint16>(static_cast<quint16>(payload.size()), f.data() + 1);
        host.write(f + payload);
    };
    QByteArray pid(2, '\0');
    qToLittleEndian<quint16>(0x009A, pid.data());
    send(emuwire::SetModel, pid);
    CHECK(WaitFor([&] { return asked == 0x009A; }));
    QByteArray req(32, '\0');
    req[0] = 0x06;
    send(emuwire::GetFeature, req);
    CHECK(WaitFor([&] { return rx.size() >= 3 + 32; }));
    CHECK_EQ(static_cast<quint8>(rx[0]), emuwire::FeatureReply);
    CHECK(s.Live());
}
}  // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestJoinPayload();
    TestMirrorSession();
    TestWaitingForDeck();
    TestRefused();
    TestOwnRole();
    TestReconnect();
    TestListenServer();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
