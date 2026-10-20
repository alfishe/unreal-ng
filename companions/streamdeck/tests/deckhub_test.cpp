// The device wire v2 and the multiplexer: frame codec, the picture cache's replay rules, and DeckHub over loopback
// (a mirror joining late gets the picture, input both ways, an own deck with GetFeature, a slow viewer, timeouts).
#include "sdb/deckhub.h"

#include "sdb/daemondecks.h"
#include "sdb/runtime.h"

#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "sdb/devicewire.h"
#include "sdb/eventloop.h"
#include "sdb/picturecache.h"
#include "sdb/protocol.h"

using namespace sdb;
using namespace std::chrono_literals;

namespace
{

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

std::string Str(const proto::Report& r)
{
    return std::string(r.begin(), r.end());
}

const Model& PlusXl()
{
    return *FindModelByName("plusxl");
}

// ------------------------------------------------------------------ the codec and the cache
void TestWireCodec()
{
    const std::string f = wire::EncodeFrame(wire::Join, "{}");
    CHECK(f == std::string("\x20\x02\x00\x00\x00{}", 7));
    wire::FrameReader reader;
    const std::string two = f + wire::EncodeFrame(wire::Input, std::string(70000, 'i'));
    wire::Frame out;
    std::vector<wire::Frame> frames;
    for (char c : two)
    {
        reader.Feed(std::string_view(&c, 1));
        while (reader.Next(out))
            frames.push_back(out);
    }
    CHECK_EQ(frames.size(), 2u);
    CHECK(frames.size() == 2 && frames[0].type == wire::Join && frames[0].payload == "{}");
    CHECK(frames.size() == 2 && frames[1].type == wire::Input && frames[1].payload.size() == 70000);
    CHECK_EQ(reader.Pending(), 0u);

    wire::FrameReader bad;
    bad.Feed(std::string("\x01\xFF\xFF\xFF\x7F", 5));
    CHECK(!bad.Next(out) && bad.Broken());
}

std::vector<std::string> Payloads(const PictureCache& c)
{
    std::vector<std::string> out;
    for (const auto& e : c.Replay())
        out.push_back(e.payload);
    return out;
}

void TestPictureCache()
{
    const Model& m = PlusXl();
    PictureCache c(m);
    const std::vector<std::uint8_t> small(500, 1), big(3000, 2), other(10, 3);

    // A two-report key chain counts once it ends; a newer chain for the key replaces it.
    const auto k0 = proto::KeyImage(m, 0, big);
    CHECK_EQ(k0.size(), 3u);
    c.Output(k0[0]);
    c.Output(k0[1]);
    CHECK_EQ(c.PictureCount(), 0u);
    c.Output(k0[2]);
    CHECK_EQ(c.PictureCount(), 1u);
    const auto k0b = proto::KeyImage(m, 0, small);
    for (const auto& r : k0b)
        c.Output(r);
    const auto k1 = proto::KeyImage(m, 1, small);
    for (const auto& r : k1)
        c.Output(r);
    CHECK_EQ(c.PictureCount(), 2u);
    auto p = Payloads(c);
    CHECK(p.size() == 2 && p[0] == Str(k0b[0]) && p[1] == Str(k1[0]));

    // A chain seen from the middle is ignored.
    const auto k2 = proto::KeyImage(m, 2, big);
    c.Output(k2[1]);
    c.Output(k2[2]);
    CHECK_EQ(c.PictureCount(), 2u);

    // Strip regions: an inner one is replaced by a later outer one, a disjoint one stays.
    // (device space: the + XL strip is portrait there)
    CHECK(m.lcd.w > 60 && m.lcd.h > 60);
    for (const auto& r : proto::LcdRegion(m, 5, 5, 20, 20, other))
        c.Output(r);
    for (const auto& r : proto::LcdRegion(m, m.lcd.w - 30, m.lcd.h - 30, 20, 20, other))
        c.Output(r);
    CHECK_EQ(c.PictureCount(), 4u);
    const auto outer = proto::LcdRegion(m, 0, 0, 40, 40, other);
    for (const auto& r : outer)
        c.Output(r);
    CHECK_EQ(c.PictureCount(), 4u);  // keys 0, 1, the far region, the outer one
    // The whole strip replaces every region.
    for (const auto& r : proto::LcdRegion(m, 0, 0, m.lcd.w, m.lcd.h, other))
        c.Output(r);
    CHECK_EQ(c.PictureCount(), 3u);

    // A colour fill on key 1 replaces its image; brightness is state, replayed first, its last value only.
    c.Feature(proto::FillKeyColor(m, 1, 255, 0, 0));
    CHECK_EQ(c.PictureCount(), 3u);
    c.Feature(proto::Brightness(m, 30));
    c.Feature(proto::Brightness(m, 70));
    const auto replay = c.Replay();
    CHECK(!replay.empty() && replay[0].wireType == wire::SetFeature &&
          replay[0].payload == Str(proto::Brightness(m, 70)));
    CHECK(replay.back().wireType == wire::SetFeature &&
          replay.back().payload == Str(proto::FillKeyColor(m, 1, 255, 0, 0)));

    // The full screen covers everything; a reset clears the pictures and keeps the brightness.
    for (const auto& r : proto::FullScreen(m, big))
        c.Output(r);
    CHECK_EQ(c.PictureCount(), 1u);
    c.Feature(proto::Reset(m));
    CHECK_EQ(c.PictureCount(), 0u);
    CHECK_EQ(c.Replay().size(), 1u);

    // Mini family (V1 reports).
    const Model& mini = *FindModelByName("mini");
    PictureCache v1(mini);
    for (const auto& r : proto::KeyImage(mini, 3, big))
        v1.Output(r);
    for (const auto& r : proto::KeyImage(mini, 3, small))
        v1.Output(r);
    CHECK_EQ(v1.PictureCount(), 1u);
}

// ------------------------------------------------------------------ DeckHub over loopback
struct FakeUsb final : Transport
{
    struct Shared
    {
        std::mutex mutex;
        std::vector<std::string> writes, features;
        std::deque<std::string> input;
    };
    std::shared_ptr<Shared> s = std::make_shared<Shared>();

    int Write(std::span<const std::uint8_t> r) override
    {
        std::lock_guard lock(s->mutex);
        s->writes.emplace_back(r.begin(), r.end());
        return static_cast<int>(r.size());
    }
    int SendFeature(std::span<const std::uint8_t> r) override
    {
        std::lock_guard lock(s->mutex);
        s->features.emplace_back(r.begin(), r.end());
        return static_cast<int>(r.size());
    }
    int GetFeature(std::span<std::uint8_t> buf) override
    {
        buf[1] = 0x42;
        return static_cast<int>(buf.size());
    }
    int Read(std::span<std::uint8_t> buf, int) override
    {
        std::lock_guard lock(s->mutex);
        if (s->input.empty())
            return 0;
        const std::string r = s->input.front();
        s->input.pop_front();
        std::copy(r.begin(), r.end(), buf.begin());
        return static_cast<int>(r.size());
    }
};

// A viewer on the test's loop: a non-blocking socket, the frames it got.
struct TestViewer
{
    net::EventLoop& loop;
    net::SocketHandle s = net::kInvalidSocket;
    wire::FrameReader reader;
    std::vector<wire::Frame> frames;
    std::string out;
    bool closed = false;
    bool reading = true;

    TestViewer(net::EventLoop& l, int port, int receiveBuffer = 0) : loop(l)
    {
        std::string error;
        s = net::ConnectStart("127.0.0.1", port, &error);
        if (receiveBuffer > 0)
            net::SetReceiveBuffer(s, receiveBuffer);
        loop.Watch(s, true, true, [this](bool r, bool w, bool e) {
            if (w)
                Flush();
            if ((r || e) && reading)
                Read();
        });
    }
    ~TestViewer()
    {
        if (s != net::kInvalidSocket)
        {
            loop.Unwatch(s);
            net::CloseSocket(s);
        }
    }
    void Send(std::uint8_t type, const std::string& payload)
    {
        out += wire::EncodeFrame(type, payload);
        Flush();
    }
    void Flush()
    {
        while (!out.empty())
        {
            const long n = net::Send(s, out.data(), out.size());
            if (n <= 0)
                break;
            out.erase(0, static_cast<std::size_t>(n));
        }
        loop.Modify(s, reading, !out.empty());
    }
    void PauseReading(bool pause)
    {
        reading = !pause;
        loop.Modify(s, reading, !out.empty());
    }
    void Read()
    {
        char buf[65536];
        for (;;)
        {
            const long n = net::Recv(s, buf, sizeof buf);
            if (n == 0)
                break;
            if (n < 0)
            {
                closed = true;
                loop.Unwatch(s);
                break;
            }
            reader.Feed(std::string_view(buf, static_cast<std::size_t>(n)));
        }
        wire::Frame f;
        while (reader.Next(f))
            frames.push_back(f);
    }
    [[nodiscard]] int Count(std::uint8_t type) const
    {
        int n = 0;
        for (const auto& f : frames)
            n += f.type == type;
        return n;
    }
    [[nodiscard]] nlohmann::json Welcome() const
    {
        for (const auto& f : frames)
            if (f.type == wire::Welcome)
                return nlohmann::json::parse(f.payload);
        return {};
    }
};

DeckHub::Options HubOptions()
{
    DeckHub::Options o;
    o.port = 0;
    o.joinTimeout = 200ms;
    return o;
}

void TestMirror()
{
    net::EventLoop loop;
    DeckHub hub(loop, HubOptions());
    std::string error;
    CHECK(hub.Listen(&error));
    const Model& m = PlusXl();
    auto usb = std::make_unique<FakeUsb>();
    auto shared = usb->s;
    std::unique_ptr<Transport> deck = hub.AttachDeck(std::move(usb), m, "SER1");

    // Drawn before anyone watches.
    const std::vector<std::uint8_t> img(1500, 7);
    for (const auto& r : proto::KeyImage(m, 4, img))
        (void)deck->Write(r);
    (void)deck->SendFeature(proto::Brightness(m, 55));
    deck->PublishPage(R"({"page":"home"})");
    CHECK(RunUntil(loop, [&] { return true; }));
    CHECK_EQ(shared->writes.size(), 2u);  // the real deck got it

    TestViewer v(loop, hub.Port());
    v.Send(wire::Join, R"({"v":2,"role":"mirror","deck":"first","name":"Studio"})");
    CHECK(RunUntil(loop, [&] { return v.Count(wire::ReplayEnd) == 1; }));
    const auto w = v.Welcome();
    CHECK(w.value("deck", "") == "SER1" && w.value("pid", 0) == m.pid && w.value("real", false));
    CHECK_EQ(v.Count(wire::Output), 2);      // the key's chain
    CHECK_EQ(v.Count(wire::SetFeature), 1);  // brightness
    CHECK_EQ(v.Count(wire::HostPage), 1);

    // Live: the next writes reach the viewer; its input reaches the runtime; the real deck's input is relayed.
    for (const auto& r : proto::KeyImage(m, 5, img))
        (void)deck->Write(r);
    CHECK(RunUntil(loop, [&] { return v.Count(wire::Output) == 4; }));
    v.Send(wire::Input, std::string("\x01\x00\x00\x00\x01", 5));
    std::uint8_t buf[64] = {};
    int n = 0;
    CHECK(RunUntil(loop, [&] { return (n = deck->Read(buf, 0)) > 0; }));
    CHECK(n == 5 && buf[4] == 1 && deck->LastSource() == InputSource::Emulator);
    shared->input.push_back(std::string("\x01\x00\x00\x00\x00\x01", 6));
    CHECK_EQ(deck->Read(buf, 10), 6);
    CHECK(RunUntil(loop, [&] { return v.Count(wire::HostInput) == 1; }));

    // Unplugged: the viewer is told and stays; plugged back: told again.
    deck.reset();
    CHECK(RunUntil(loop, [&] { return v.Count(wire::DeckState) == 1; }));
    CHECK(!v.closed && hub.ViewerCount() == 1);
    deck = hub.AttachDeck(std::make_unique<FakeUsb>(), m, "SER1");
    CHECK(RunUntil(loop, [&] { return v.Count(wire::DeckState) == 2; }));
    deck.reset();
    CHECK(RunUntil(loop, [&] { return v.Count(wire::DeckState) == 3; }));
}

void TestMirrorWaitsForDeck()
{
    net::EventLoop loop;
    DeckHub hub(loop, HubOptions());
    std::string error;
    CHECK(hub.Listen(&error));
    TestViewer v(loop, hub.Port());
    v.Send(wire::Join, R"({"v":2,"role":"mirror","deck":"SER9"})");
    CHECK(RunUntil(loop, [&] { return v.Count(wire::Welcome) == 1; }));
    CHECK(v.Welcome().value("error", "") == "no_deck");
    auto deck = hub.AttachDeck(std::make_unique<FakeUsb>(), PlusXl(), "SER9");
    CHECK(RunUntil(loop, [&] { return v.Count(wire::Welcome) == 2 && v.Count(wire::ReplayEnd) == 1; }));
}

void TestOwnDeck()
{
    net::EventLoop loop;
    DeckHub hub(loop, HubOptions());
    std::string error;
    CHECK(hub.Listen(&error));
    std::unique_ptr<Transport> own;
    std::string ownSerial;
    hub.onOwnDeck = [&](std::unique_ptr<Transport> t, const Model& m, const std::string& serial) {
        CHECK(m.pid == FindModelByName("neo")->pid);
        own = std::move(t);
        ownSerial = serial;
    };
    TestViewer v(loop, hub.Port());
    v.Send(wire::Join, R"({"v":2,"role":"own","model":"neo"})");
    CHECK(RunUntil(loop, [&] { return own != nullptr && v.Count(wire::Welcome) == 1; }));
    CHECK(!v.Welcome().value("real", true) && v.Welcome().value("deck", "") == ownSerial);

    (void)own->Write(proto::Report{0x02, 0x07, 0x00, 0x01});
    CHECK(RunUntil(loop, [&] { return v.Count(wire::Output) == 1; }));
    v.Send(wire::Input, std::string("\x01\x02", 2));
    std::uint8_t buf[16] = {};
    CHECK(RunUntil(loop, [&] { return own->Read(buf, 0) == 2; }));

    // GetFeature waits on the device thread while the viewer answers through the loop.
    int got = 0;
    std::uint8_t feature[32] = {0x06};
    std::thread device([&] { got = own->GetFeature(feature); });
    CHECK(RunUntil(loop, [&] { return v.Count(wire::GetFeature) == 1; }));
    v.Send(wire::FeatureReply, std::string("\x06\x0c"
                                           "EMU-SERIAL",
                                           12));
    CHECK(RunUntil(loop, [&] { return got != 0; }));
    device.join();
    CHECK(got == 12 && feature[1] == 0x0c);

    // The viewer leaves: the runtime's transport ends.
    v.Send(wire::Leave, {});
    CHECK(RunUntil(loop, [&] { return own->Read(buf, 0) < 0; }));
    CHECK_EQ(hub.ViewerCount(), 0u);
}

void TestSlowViewer()
{
    net::EventLoop loop;
    DeckHub::Options o = HubOptions();
    o.viewerQueueFrames = 4;
    o.viewerSendBuffer = 8 << 10;
    DeckHub hub(loop, o);
    std::string error;
    CHECK(hub.Listen(&error));
    const Model& m = PlusXl();
    auto deck = hub.AttachDeck(std::make_unique<FakeUsb>(), m, "SER1");
    TestViewer v(loop, hub.Port(), 8 << 10);
    v.Send(wire::Join, R"({"v":2,"role":"mirror"})");
    CHECK(RunUntil(loop, [&] { return v.Count(wire::ReplayEnd) == 1; }));

    // The viewer stops reading; the deck keeps drawing big images far past the socket buffers.
    v.PauseReading(true);
    const std::vector<std::uint8_t> img(20000, 9);
    for (int round = 0; round < 6; ++round)
        for (int key = 0; key < 4; ++key)
            for (const auto& r : proto::KeyImage(m, key, img))
                (void)deck->Write(r);
    for (int i = 0; i < 20; ++i)
        loop.RunOnce(0);  // the hub fills the kernel buffers and its queue
    v.PauseReading(false);
    // It catches up with a fresh replay of the last picture (four keys), not with every stale frame.
    CHECK(RunUntil(loop, [&] { return v.Count(wire::ReplayEnd) == 2; }, 3000ms));
    const auto perKey = static_cast<int>(proto::KeyImage(m, 0, img).size());
    CHECK(v.Count(wire::Output) < 6 * 4 * perKey);
}

void TestJoinRules()
{
    net::EventLoop loop;
    DeckHub::Options o = HubOptions();
    o.joinTimeout = 20ms;
    DeckHub hub(loop, o);
    std::string error;
    CHECK(hub.Listen(&error));
    TestViewer silent(loop, hub.Port());
    CHECK(RunUntil(loop, [&] { return hub.ViewerCount() == 1; }));
    CHECK(RunUntil(loop, [&] { return silent.closed; }));  // no Join within the timeout

    TestViewer old(loop, hub.Port());
    old.Send(wire::Join, R"({"v":1,"role":"mirror"})");
    CHECK(RunUntil(loop, [&] { return old.closed; }));
    CHECK(old.Welcome().value("error", "") == "version");

    TestViewer odd(loop, hub.Port());
    odd.Send(wire::Join, R"({"v":"2","role":"mirror"})");
    CHECK(RunUntil(loop, [&] { return odd.closed; }));
    CHECK(odd.Welcome().value("error", "") == "version");
}

// What the daemon's automation reads (GET /api/v1/decks) and the Control frames it sends.
void TestHubInfo()
{
    net::EventLoop loop;
    DeckHub hub(loop, HubOptions());
    std::string error;
    CHECK(hub.Listen(&error));
    std::unique_ptr<Transport> own;
    hub.onOwnDeck = [&](std::unique_ptr<Transport> t, const Model&, const std::string&) { own = std::move(t); };
    auto deck = hub.AttachDeck(std::make_unique<FakeUsb>(), PlusXl(), "SER1");
    TestViewer mirror(loop, hub.Port());
    mirror.Send(wire::Join, R"({"v":2,"role":"mirror","name":"Studio","pid":42})");
    TestViewer mine(loop, hub.Port());
    mine.Send(wire::Join, R"({"v":2,"role":"own","model":"neo","serial":"EMU7"})");
    TestViewer waiting(loop, hub.Port());
    waiting.Send(wire::Join, R"({"v":2,"role":"mirror","deck":"SER9"})");
    CHECK(RunUntil(loop, [&] {
        const auto i = hub.Info();
        return i.decks.size() == 2 && i.decks[0].viewers.size() == 1 && i.waiting == 1 && own != nullptr;
    }));
    const auto info = hub.Info();
    CHECK(info.decks.size() == 2 && info.decks[0].serial == "SER1" && info.decks[0].real && info.decks[0].connected);
    CHECK(info.decks.size() == 2 && info.decks[0].viewers.size() == 1 && info.decks[0].viewers[0].name == "Studio" &&
          info.decks[0].viewers[0].pid == 42 && info.decks[0].viewers[0].role == "mirror");
    CHECK(info.decks.size() == 2 && info.decks[1].serial == "EMU7" && !info.decks[1].real &&
          info.decks[1].model == FindModelByName("neo") && info.decks[1].viewers.size() == 1 &&
          info.decks[1].viewers[0].role == "own");

    // Control: to the deck's viewers only; an unknown deck reaches nobody.
    CHECK_EQ(hub.SendControl("SER1", R"({"op":"snapshot","path":"/tmp/a.png"})"), 1);
    CHECK_EQ(hub.SendControl("NOPE", "{}"), 0);
    CHECK(RunUntil(loop, [&] { return mirror.Count(wire::Control) == 1; }));
    CHECK_EQ(mine.Count(wire::Control), 0);

    // Unplugged and gone: the copy follows.
    deck.reset();
    CHECK(RunUntil(loop, [&] { return !hub.Info().decks.empty() && !hub.Info().decks[0].connected; }));
    mine.Send(wire::Leave, {});
    CHECK(RunUntil(loop, [&] { return hub.Info().decks.size() == 1; }));
}

// The daemon's routes over a hub: an own deck's viewer listed with its name, a snapshot reaching it as Control.
void TestHubWithDaemonDecks()
{
    net::EventLoop loop;
    DeckHub hub(loop, HubOptions());
    std::string error;
    CHECK(hub.Listen(&error));
    DaemonDecks decks(DaemonDecks::Options{});
    CHECK(decks.LoadStart(&error));
    decks.SetHub(&hub, hub.Port());
    automation::Api api("unreal-streamdeck", "1.0");
    decks.AddRoutes(api);
    std::unique_ptr<Device> dev;
    std::unique_ptr<scn::Runtime> rt;
    hub.onOwnDeck = [&](std::unique_ptr<Transport> t, const Model& m, const std::string& serial) {
        dev = std::make_unique<Device>(m, std::move(t), serial);
        rt = std::make_unique<scn::Runtime>(*dev, decks.StartScenario());
        decks.Add(serial, m, false, *rt);
    };
    hub.onOwnDeckGone = [&](const std::string& serial) { decks.Remove(serial); };
    TestViewer v(loop, hub.Port());
    v.Send(wire::Join, R"({"v":2,"role":"own","model":"mini","serial":"EMU9","name":"Studio"})");
    CHECK(RunUntil(loop, [&] { return v.Count(wire::Welcome) == 1; }));
    // the deck is known to the routes by the time its viewer is welcomed
    automation::Request q;
    q.method = "GET";
    q.path = "/api/v1/decks";
    const auto list = api.Dispatch(q).body["decks"];
    CHECK(list.size() == 1 && list[0]["serial"] == "EMU9" && list[0]["viewers"].size() == 1 &&
          list[0]["viewers"][0]["name"] == "Studio");
    q.path = "/api/v1/health";
    CHECK(api.Dispatch(q).body.value("wire_port", 0) == hub.Port());
    q.method = "POST";
    q.path = "/api/v1/decks/EMU9/snapshot";
    q.body = {{"path", "/tmp/emu9.png"}};
    const auto snap = api.Dispatch(q);
    CHECK(snap.status == 200 && snap.body.value("viewers", 0) == 1);
    CHECK(RunUntil(loop, [&] { return v.Count(wire::Control) == 1; }));
    for (const auto& f : v.frames)
        if (f.type == wire::Control)
            CHECK(nlohmann::json::parse(f.payload).value("path", "") == "/tmp/emu9.png");
    v.Send(wire::Leave, {});
    CHECK(RunUntil(loop, [&] { return decks.Count() == 0; }));
    rt.reset();
    dev.reset();
}

}  // namespace

void TestDeckHub()
{
    TestWireCodec();
    TestPictureCache();
    TestMirror();
    TestMirrorWaitsForDeck();
    TestOwnDeck();
    TestSlowViewer();
    TestJoinRules();
    TestHubInfo();
    TestHubWithDaemonDecks();
}
