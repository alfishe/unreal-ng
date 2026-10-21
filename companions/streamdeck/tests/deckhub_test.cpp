// The device wire v2 and the multiplexer: frame codec, the picture cache's replay rules, and DeckHub over loopback
// (a mirror joining late gets the picture, input both ways, an own deck with GetFeature, a slow viewer, timeouts).
#include "sdb/deckhub.h"

#include "sdb/daemondecks.h"
#include "sdb/runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "hubpeer.h"
#include "sdb/devicewire.h"
#include "sdb/eventloop.h"
#include "sdb/picturecache.h"
#include "sdb/protocol.h"
#include "sdb/reconnect.h"

using namespace sdb;
using namespace sdbtest;
using namespace std::chrono_literals;

namespace
{

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

// A deck under a ReconnectingTransport (sdb/reconnect.h) that the test unplugs.
struct PluggableUsb final : Transport
{
    std::shared_ptr<std::atomic<bool>> present;
    std::shared_ptr<FakeUsb::Shared> s;
    PluggableUsb(std::shared_ptr<std::atomic<bool>> p, std::shared_ptr<FakeUsb::Shared> sh)
        : present(std::move(p)), s(std::move(sh))
    {
    }
    int Write(std::span<const std::uint8_t> r) override
    {
        if (!*present)
            return -1;
        std::lock_guard lock(s->mutex);
        s->writes.emplace_back(r.begin(), r.end());
        return static_cast<int>(r.size());
    }
    int SendFeature(std::span<const std::uint8_t> r) override
    {
        if (!*present)
            return -1;
        std::lock_guard lock(s->mutex);
        s->features.emplace_back(r.begin(), r.end());
        return static_cast<int>(r.size());
    }
    int GetFeature(std::span<std::uint8_t> buf) override
    {
        return *present ? static_cast<int>(buf.size()) : -1;
    }
    int Read(std::span<std::uint8_t>, int) override
    {
        return *present ? 0 : -1;
    }
};

// Unplug and replug under a running runtime: the viewers are told both ways, the routes show it, the runtime goes on,
// the deck gets the picture and the brightness set while it was away; POST .../reconnect does the same on purpose.
void TestHubReconnect()
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
    const Model& m = PlusXl();
    auto present = std::make_shared<std::atomic<bool>>(true);
    auto shared = std::make_shared<FakeUsb::Shared>();
    ReconnectOptions ro;
    ro.retry = 1ms;
    auto reopen = [present, shared](bool) {
        ReconnectingTransport::Probe p;
        if (*present)
            p.same = std::make_unique<PluggableUsb>(present, shared);
        return p;
    };
    auto dev = std::make_unique<Device>(
        m,
        std::make_unique<ReconnectingTransport>(std::make_unique<PluggableUsb>(present, shared), m, "SER1", reopen,
                                                ro),
        "SER1");
    dev->WrapTransport([&](std::unique_ptr<Transport> t) { return hub.AttachDeck(std::move(t), m, "SER1"); });
    auto rt = std::make_unique<scn::Runtime>(*dev, decks.StartScenario());
    decks.Add("SER1", m, true, *rt);
    rt->Step(0);
    TestViewer v(loop, hub.Port());
    v.Send(wire::Join, R"({"v":2,"role":"mirror","deck":"first"})");
    CHECK(RunUntil(loop, [&] { return v.Count(wire::ReplayEnd) == 1; }));
    automation::Request list;
    list.method = "GET";
    list.path = "/api/v1/decks/SER1";
    CHECK(api.Dispatch(list).body.value("connected", false));

    // Unplugged: found on the device thread's next read; told to the viewer; the route says so at once.
    *present = false;
    rt->Step(0);
    CHECK(!api.Dispatch(list).body.value("connected", true));
    CHECK(RunUntil(loop, [&] { return DeckStates(v).size() == 1; }));
    CHECK(!DeckStates(v)[0].value("connected", true) && !v.closed);
    CHECK(hub.Info().decks.size() == 1 && !hub.Info().decks[0].connected);

    // The runtime goes on while the deck is away.
    rt->SetBrightness(33);
    rt->Step(0);
    {
        std::lock_guard lock(shared->mutex);
        shared->writes.clear();
        shared->features.clear();
    }
    *present = true;
    CHECK(RunUntil(loop, [&] {
        rt->Step(1);
        return DeckStates(v).size() == 2;
    }));
    CHECK(DeckStates(v)[1].value("connected", false) && api.Dispatch(list).body.value("connected", false));
    {
        std::lock_guard lock(shared->mutex);
        CHECK(std::find(shared->features.begin(), shared->features.end(), Str(proto::Brightness(m, 33))) !=
              shared->features.end());
        CHECK(shared->writes.size() >= static_cast<std::size_t>(m.keys));  // every key drawn again
    }
    CHECK_EQ(v.Count(wire::Welcome), 1);  // the mirror stayed on its deck

    // On purpose.
    automation::Request q;
    q.method = "POST";
    q.path = "/api/v1/decks/first/reconnect";
    q.body = {{"away_ms", 0}};
    const auto r = api.Dispatch(q);
    CHECK(r.status == 200 && r.body.value("reconnecting", false));
    q.body = {{"away_ms", -1}};
    CHECK_EQ(api.Dispatch(q).status, 400);
    CHECK(RunUntil(loop, [&] {
        rt->Step(1);
        return DeckStates(v).size() == 4;
    }));
    CHECK(DeckStates(v)[3].value("connected", false) && dev->Connected());

    decks.Remove("SER1");
    rt.reset();
    dev.reset();
}

// The daemon takes another deck after its deck stayed away: mirrors of "the first deck" follow it (a new Welcome with
// the other model, its picture), a mirror of that very serial keeps waiting for it.
void TestMirrorFollowsFirstDeck()
{
    net::EventLoop loop;
    DeckHub hub(loop, HubOptions());
    std::string error;
    CHECK(hub.Listen(&error));
    auto a = hub.AttachDeck(std::make_unique<FakeUsb>(), PlusXl(), "SERA");
    TestViewer first(loop, hub.Port());
    first.Send(wire::Join, R"({"v":2,"role":"mirror","deck":"first"})");
    TestViewer pinned(loop, hub.Port());
    pinned.Send(wire::Join, R"({"v":2,"role":"mirror","deck":"SERA"})");
    CHECK(RunUntil(loop, [&] { return first.Count(wire::ReplayEnd) == 1 && pinned.Count(wire::ReplayEnd) == 1; }));
    a.reset();
    CHECK(RunUntil(loop, [&] { return DeckStates(first).size() == 1 && DeckStates(pinned).size() == 1; }));
    const Model& neo = *FindModelByName("neo");
    auto b = hub.AttachDeck(std::make_unique<FakeUsb>(), neo, "SERB");
    CHECK(RunUntil(loop, [&] { return first.Count(wire::Welcome) == 2 && first.Count(wire::ReplayEnd) == 2; }));
    nlohmann::json w;
    for (const auto& f : first.frames)
        if (f.type == wire::Welcome)
            w = nlohmann::json::parse(f.payload);
    CHECK(w.value("deck", "") == "SERB" && w.value("pid", 0) == neo.pid);
    CHECK(RunUntil(loop, [&] { return hub.Info().decks.size() == 2; }));
    CHECK_EQ(pinned.Count(wire::Welcome), 1);
    const auto info = hub.Info();
    CHECK(info.decks.size() == 2 && info.decks[0].serial == "SERA" && !info.decks[0].connected &&
          info.decks[0].viewers.size() == 1 && info.decks[1].serial == "SERB" && info.decks[1].viewers.size() == 1);

    // Nobody watches A any more: it is forgotten when the next deck comes.
    pinned.Send(wire::Leave, {});
    CHECK(RunUntil(loop, [&] { return hub.ViewerCount() == 1; }));
    b.reset();
    auto c = hub.AttachDeck(std::make_unique<FakeUsb>(), PlusXl(), "SERC");
    CHECK(RunUntil(loop, [&] { return first.Count(wire::Welcome) == 3; }));
    const auto after = hub.Info();
    CHECK(after.decks.size() == 1 && after.decks[0].serial == "SERC");
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
    TestHubReconnect();
    TestMirrorFollowsFirstDeck();
}
