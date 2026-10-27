// Device wire v2 conformance (device-wire.md) and the daemon's automation routes over a live multiplexer: a scripted
// peer on loopback against the real DeckHub and DaemonDecks, fake USB decks, no hardware. The hardware-free part of
// the conformance suite (testing.md §3); deckhub_test.cpp has the multiplexer's own cases (late join, unplug and
// replug, reconnect, the first deck followed, the slow viewer's replay), this file the protocol rules a viewer may
// rely on. The coverage table is in testing.md §3.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "hubpeer.h"
#include "sdb/automation.h"
#include "sdb/daemondecks.h"
#include "sdb/deckhub.h"
#include "sdb/device.h"
#include "sdb/devicewire.h"
#include "sdb/eventloop.h"
#include "sdb/protocol.h"
#include "sdb/runtime.h"

using namespace sdb;
using namespace sdbtest;
using namespace std::chrono_literals;
using nlohmann::json;

namespace
{

// The frame types a viewer got, in order.
std::vector<std::uint8_t> Types(const TestViewer& v)
{
    std::vector<std::uint8_t> t;
    for (const auto& f : v.frames)
        t.push_back(f.type);
    return t;
}

// A frame header announcing more than kMaxFrame bytes (the payload never follows).
std::string OversizeHeader(std::uint8_t type)
{
    const auto n = static_cast<std::uint32_t>(wire::kMaxFrame + 1);
    std::string h(1, static_cast<char>(type));
    for (int i = 0; i < 4; ++i)
        h += static_cast<char>((n >> (i * 8)) & 0xFF);
    return h;
}

// ------------------------------------------------------------------ frames
// 32-bit little-endian lengths, the kMaxFrame boundary, a stream cut anywhere.
void TestFrameBoundaries()
{
    // exactly kMaxFrame is a frame; the header alone waits for the payload
    std::string h = OversizeHeader(wire::Output);
    h[1] = static_cast<char>(h[1] - 1);  // kMaxFrame + 1 - 1
    wire::FrameReader atLimit;
    atLimit.Feed(h);
    wire::Frame f;
    CHECK(!atLimit.Next(f) && !atLimit.Broken());
    wire::FrameReader over;
    over.Feed(OversizeHeader(wire::Output));
    CHECK(!over.Next(f) && over.Broken());
    // broken stays broken, whatever follows
    over.Feed(wire::EncodeFrame(wire::Leave, {}));
    CHECK(!over.Next(f) && over.Broken());

    // a 100 KB HostPage (v1's 16-bit length dropped these) and an empty Leave, fed in odd slices
    const std::string page(100000, 'p');
    const std::string stream = wire::EncodeFrame(wire::HostPage, page) + wire::EncodeFrame(wire::Leave, {});
    CHECK_EQ(static_cast<unsigned char>(stream[3]), 0x01u);  // 100000 = 0x0186A0: the third length byte is used
    wire::FrameReader r;
    std::vector<wire::Frame> got;
    for (std::size_t at = 0; at < stream.size(); at += 4097)
    {
        r.Feed(std::string_view(stream).substr(at, 4097));
        while (r.Next(f))
            got.push_back(f);
    }
    CHECK(got.size() == 2 && got[0].type == wire::HostPage && got[0].payload == page && got[1].type == wire::Leave &&
          got[1].payload.empty());
}

// ------------------------------------------------------------------ Join and Welcome
// Every role: the Welcome's fields; serials of own decks; unknown frame types ignored after Join.
void TestJoinRoles()
{
    net::EventLoop loop;
    DeckHub::Options o = HubOptions();
    o.daemonVersion = "conformance-1";
    DeckHub hub(loop, o);
    std::string error;
    CHECK(hub.Listen(&error));
    std::vector<std::unique_ptr<Transport>> owns;
    std::vector<std::string> ownSerials;
    hub.onOwnDeck = [&](std::unique_ptr<Transport> t, const Model&, const std::string& serial) {
        owns.push_back(std::move(t));
        ownSerials.push_back(serial);
    };
    const Model& neo = *FindModelByName("neo");
    auto a = hub.AttachDeck(std::make_unique<FakeUsb>(), PlusXl(), "SERA");
    auto b = hub.AttachDeck(std::make_unique<FakeUsb>(), neo, "SERB");

    TestViewer first(loop, hub.Port()), bySerial(loop, hub.Port()), noDeckField(loop, hub.Port());
    TestViewer own(loop, hub.Port()), sameSerial(loop, hub.Port()), defaultModel(loop, hub.Port());
    first.Send(wire::Join, R"({"v":2,"role":"mirror","deck":"first"})");
    bySerial.Send(wire::Join, R"({"v":2,"role":"mirror","deck":"SERB","name":"Studio","pid":7})");
    noDeckField.Send(wire::Join, R"({"v":2,"role":"mirror"})");
    own.Send(wire::Join, R"({"v":2,"role":"own","model":"neo","serial":"EMU7"})");
    CHECK(RunUntil(loop, [&] { return own.Count(wire::Welcome) == 1; }));
    sameSerial.Send(wire::Join, R"({"v":2,"role":"own","model":"mini","serial":"EMU7"})");
    defaultModel.Send(wire::Join, R"({"v":2,"role":"own"})");
    CHECK(RunUntil(loop, [&] {
        return first.Count(wire::ReplayEnd) == 1 && bySerial.Count(wire::ReplayEnd) == 1 &&
               noDeckField.Count(wire::ReplayEnd) == 1 && sameSerial.Count(wire::Welcome) == 1 &&
               defaultModel.Count(wire::Welcome) == 1;
    }));

    // A mirror's Welcome: the deck it got, its model ("model" is the token Join takes, "model_name" the display name,
    // "pid"), real; "first" (or no deck at all) is the first attached deck.
    const json wf = first.Welcome();
    CHECK(wf.value("v", 0) == 2 && wf.value("deck", "") == "SERA" && wf.value("model", "") == "plusxl" &&
          wf.value("model_name", "") == PlusXl().name && wf.value("pid", 0) == PlusXl().pid &&
          wf.value("real", false) && wf.value("daemon", "") == "conformance-1" && !wf.contains("error"));
    CHECK(noDeckField.Welcome().value("deck", "") == "SERA");
    const json wb = bySerial.Welcome();
    CHECK(wb.value("deck", "") == "SERB" && wb.value("model", "") == "neo" && wb.value("model_name", "") == neo.name &&
          wb.value("pid", 0) == neo.pid);
    // every model's token reads back as that model: a viewer can Join "own" with the Welcome's "model"
    for (const auto& m : AllModels())
        CHECK(FindModelByName(ModelToken(m)) == &m);

    // An own deck: the serial asked for, not real; a taken serial is replaced; the model defaults to the + XL.
    const json wo = own.Welcome();
    CHECK(wo.value("deck", "") == "EMU7" && wo.value("model", "") == "neo" && !wo.value("real", true) &&
          wo.value("daemon", "") == "conformance-1");
    const std::string renamed = sameSerial.Welcome().value("deck", "");
    CHECK(renamed != "EMU7" && renamed.rfind("EMU", 0) == 0 && sameSerial.Welcome().value("model", "") == "mini" &&
          sameSerial.Welcome().value("model_name", "") == FindModelByName("mini")->name);
    CHECK(defaultModel.Welcome().value("model", "") == "plusxl");
    CHECK(owns.size() == 3 && ownSerials.size() == 3 && ownSerials[0] == "EMU7" &&
          std::count(ownSerials.begin(), ownSerials.end(), renamed) == 1);
    // An own deck has no replay: nothing was drawn on it yet.
    CHECK_EQ(own.Count(wire::ReplayBegin), 0);

    // A type the daemon does not know (a newer viewer) is ignored: the session goes on.
    first.Send(0x7F, "future");
    first.Send(wire::Input, std::string("\x01\x00\x00\x00\x01", 5));
    std::uint8_t buf[64] = {};
    CHECK(RunUntil(loop, [&] { return a->Read(buf, 0) == 5; }));
    CHECK(!first.closed && hub.ViewerCount() == 6);
    owns.clear();
}

// Every refused Join: the error in a Welcome ("v":2), then the connection closes; only a mirror that waits for its
// deck stays. Anything before a Join closes the connection without a Welcome.
void TestWelcomeErrors()
{
    net::EventLoop loop;
    DeckHub hub(loop, HubOptions());
    std::string error;
    CHECK(hub.Listen(&error));
    bool ownCalled = false;
    hub.onOwnDeck = [&](std::unique_ptr<Transport>, const Model&, const std::string&) { ownCalled = true; };

    struct Case
    {
        const char* join;
        const char* error;
    };
    const Case cases[] = {
        {"not json", "bad_request"},
        {"[2]", "bad_request"},
        {R"({"v":2,"role":"mirror","deck":5})", "bad_request"},  // a field of the wrong type
        {R"({"role":"mirror"})", "version"},
        {R"({"v":3,"role":"mirror"})", "version"},
        {R"({"v":2})", "bad_role"},
        {R"({"v":2,"role":"spy"})", "bad_role"},
        {R"({"v":2,"role":"own","model":"nosuch"})", "bad_model"},
    };
    std::vector<std::unique_ptr<TestViewer>> peers;
    for (const Case& c : cases)
    {
        peers.push_back(std::make_unique<TestViewer>(loop, hub.Port()));
        peers.back()->Send(wire::Join, c.join);
    }
    TestViewer early(loop, hub.Port());
    early.Send(wire::Input, std::string("\x01\x00", 2));  // input before Join
    TestViewer waiting(loop, hub.Port());
    waiting.Send(wire::Join, R"({"v":2,"role":"mirror","deck":"SER9"})");

    CHECK(RunUntil(loop, [&] {
        return early.closed && waiting.Count(wire::Welcome) == 1 &&
               std::all_of(peers.begin(), peers.end(), [](const auto& p) { return p->closed; });
    }));
    for (std::size_t i = 0; i < peers.size(); ++i)
    {
        const json w = peers[i]->Welcome();
        const bool ok = w.value("error", "") == cases[i].error && w.value("v", 0) == 2 && !w.contains("deck") &&
                        peers[i]->Count(wire::Welcome) == 1 && peers[i]->frames.size() == 1;
        CHECK(ok);
        if (!ok)
            std::printf("    join %s: got %s\n", cases[i].join, w.dump().c_str());
    }
    CHECK(early.frames.empty());
    CHECK(!ownCalled);
    const json w = waiting.Welcome();
    CHECK(w.value("error", "") == "no_deck" && w.value("waiting", false) && !waiting.closed);
    CHECK(RunUntil(loop, [&] { return hub.ViewerCount() == 1 && hub.Info().waiting == 1; }));
}

// ------------------------------------------------------------------ replay framing and fan-out
// A late mirror: Welcome, ReplayBegin, the deck state, the key chains, the page, ReplayEnd, nothing in between; the
// replayed reports are the very reports the deck got; live frames only after ReplayEnd. Two mirrors get the same
// bytes, a page past 64 KB included.
void TestReplayFraming()
{
    net::EventLoop loop;
    DeckHub hub(loop, HubOptions());
    std::string error;
    CHECK(hub.Listen(&error));
    const Model& m = PlusXl();
    auto usb = std::make_unique<FakeUsb>();
    auto shared = usb->s;
    auto deck = hub.AttachDeck(std::move(usb), m, "SER1");

    const std::vector<std::uint8_t> img(1500, 5);
    std::vector<std::string> drawn;
    for (int key = 0; key < 3; ++key)
        for (const auto& r : proto::KeyImage(m, key, img))
        {
            (void)deck->Write(r);
            drawn.push_back(Str(r));
        }
    (void)deck->SendFeature(proto::Brightness(m, 42));
    const std::string page = json{{"page", "home"}, {"pad", std::string(70000, 'x')}}.dump();
    deck->PublishPage(page);
    CHECK(RunUntil(loop, [&] { return hub.Info().decks.size() == 1; }));

    TestViewer a(loop, hub.Port()), b(loop, hub.Port());
    a.keepRaw = b.keepRaw = true;
    a.Send(wire::Join, R"({"v":2,"role":"mirror"})");
    b.Send(wire::Join, R"({"v":2,"role":"mirror"})");
    CHECK(RunUntil(loop, [&] { return a.Count(wire::ReplayEnd) == 1 && b.Count(wire::ReplayEnd) == 1; }));

    const auto t = Types(a);
    CHECK(t.size() >= 4 && t[0] == wire::Welcome && t[1] == wire::ReplayBegin && t.back() == wire::ReplayEnd);
    std::vector<std::string> replayed;
    int features = 0, pages = 0, other = 0;
    for (std::size_t i = 2; i + 1 < a.frames.size(); ++i)
    {
        const auto& f = a.frames[i];
        if (f.type == wire::Output)
            replayed.push_back(f.payload);
        else if (f.type == wire::SetFeature)
            features += f.payload == Str(proto::Brightness(m, 42));
        else if (f.type == wire::HostPage)
            pages += f.payload == page;
        else
            ++other;
    }
    std::sort(replayed.begin(), replayed.end());
    std::sort(drawn.begin(), drawn.end());
    CHECK(replayed == drawn);
    CHECK(features == 1 && pages == 1 && other == 0);
    CHECK(page.size() > 65535);
    // the state comes before the pictures, so a viewer shows them at the right brightness
    CHECK(t.size() > 2 && t[2] == wire::SetFeature);

    // Live: after the replay, in the deck's order, byte for byte the same to both mirrors.
    const std::size_t before = a.frames.size();
    for (const auto& r : proto::KeyImage(m, 7, img))
        (void)deck->Write(r);
    deck->Note("pressed 7");
    (void)deck->SendFeature(proto::Brightness(m, 60));
    const std::string page2 = json{{"page", "sub"}, {"pad", std::string(80000, 'y')}}.dump();
    deck->PublishPage(page2);
    const auto chain = proto::KeyImage(m, 7, img);
    const std::size_t expect = before + chain.size() + 3;
    CHECK(RunUntil(loop, [&] { return a.frames.size() == expect && b.frames.size() == expect; }));
    CHECK(a.frames.size() == expect && a.frames[before].type == wire::Output &&
          a.frames[before].payload == Str(chain[0]) && a.frames[expect - 3].type == wire::HostNote &&
          a.frames[expect - 3].payload == "pressed 7" && a.frames[expect - 2].type == wire::SetFeature &&
          a.frames[expect - 1].type == wire::HostPage && a.frames[expect - 1].payload == page2);
    CHECK(!a.raw.empty() && a.raw == b.raw);
    {
        std::lock_guard lock(shared->mutex);
        CHECK_EQ(shared->writes.size(), drawn.size() + chain.size());  // the deck got every report exactly once
    }
}

// ------------------------------------------------------------------ input, features, leaving
// Input from several mirrors merges into the deck's runtime in each viewer's order; a mirror's FeatureReply is
// ignored and its deck's GetFeature is answered by the deck itself.
void TestInputMerge()
{
    net::EventLoop loop;
    DeckHub hub(loop, HubOptions());
    std::string error;
    CHECK(hub.Listen(&error));
    auto deck = hub.AttachDeck(std::make_unique<FakeUsb>(), PlusXl(), "SER1");
    TestViewer a(loop, hub.Port()), b(loop, hub.Port());
    a.Send(wire::Join, R"({"v":2,"role":"mirror"})");
    b.Send(wire::Join, R"({"v":2,"role":"mirror"})");
    CHECK(RunUntil(loop, [&] { return a.Count(wire::ReplayEnd) == 1 && b.Count(wire::ReplayEnd) == 1; }));
    for (char i = 1; i <= 3; ++i)
    {
        a.Send(wire::Input, std::string{'\x01', 'a', i});
        b.Send(wire::Input, std::string{'\x01', 'b', i});
    }
    a.Send(wire::FeatureReply, std::string("\x06\x99", 2));
    std::vector<std::string> got;
    CHECK(RunUntil(loop, [&] {
        std::uint8_t buf[16] = {};
        while (deck->Read(buf, 0) > 0)
            got.emplace_back(reinterpret_cast<const char*>(buf), 3);
        return got.size() == 6;
    }));
    std::vector<std::string> fromA, fromB;
    for (const auto& r : got)
        (r[1] == 'a' ? fromA : fromB).push_back(r);
    CHECK(fromA == (std::vector<std::string>{"\x01"
                                             "a\x01",
                                             "\x01"
                                             "a\x02",
                                             "\x01"
                                             "a\x03"}));
    CHECK(fromB == (std::vector<std::string>{"\x01"
                                             "b\x01",
                                             "\x01"
                                             "b\x02",
                                             "\x01"
                                             "b\x03"}));
    CHECK(deck->LastSource() == InputSource::Emulator);

    std::uint8_t feature[32] = {0x06};
    CHECK(deck->GetFeature(feature) == 32 && feature[1] == 0x42);  // FakeUsb's answer, not the viewer's 0x99
    loop.RunOnce(0);
    CHECK(a.Count(wire::GetFeature) == 0 && b.Count(wire::GetFeature) == 0 && !a.closed);
}

// An own deck's GetFeature unanswered: -1 after the feature timeout; the request went out with the full requested
// length, report ID first.
void TestOwnFeatureTimeout()
{
    net::EventLoop loop;
    DeckHub::Options o = HubOptions();
    o.featureTimeout = 5ms;
    DeckHub hub(loop, o);
    std::string error;
    CHECK(hub.Listen(&error));
    std::unique_ptr<Transport> own;
    hub.onOwnDeck = [&](std::unique_ptr<Transport> t, const Model&, const std::string&) { own = std::move(t); };
    TestViewer v(loop, hub.Port());
    v.Send(wire::Join, R"({"v":2,"role":"own","model":"neo","serial":"EMU3"})");
    CHECK(RunUntil(loop, [&] { return own != nullptr && v.Count(wire::Welcome) == 1; }));
    std::uint8_t feature[32] = {0x06};
    std::atomic<int> got{0};
    std::thread device([&] {
        got = own->GetFeature(feature);
        loop.Post([] {});  // wakes the loop's wait: the test sees the result at once
    });
    CHECK(RunUntil(loop, [&] { return got != 0; }));
    device.join();
    CHECK_EQ(got.load(), -1);
    CHECK(RunUntil(loop, [&] { return v.Count(wire::GetFeature) == 1; }));
    for (const auto& f : v.frames)
        if (f.type == wire::GetFeature)
            CHECK(f.payload.size() == sizeof feature && f.payload[0] == '\x06');
}

// An own deck's GetFeature answered: the viewer's FeatureReply comes back; a reply nobody asked for is not taken for
// the next request; a viewer that leaves while a request waits ends it at once (long before the 1 s timeout);
// afterwards the transport is closed for good.
void TestOwnFeatureReplyAndLeave()
{
    net::EventLoop loop;
    DeckHub hub(loop, HubOptions());
    std::string error;
    CHECK(hub.Listen(&error));
    std::unique_ptr<Transport> own;
    std::vector<std::string> gone;
    hub.onOwnDeck = [&](std::unique_ptr<Transport> t, const Model&, const std::string&) { own = std::move(t); };
    hub.onOwnDeckGone = [&](const std::string& serial) { gone.push_back(serial); };
    TestViewer v(loop, hub.Port());
    v.Send(wire::Join, R"({"v":2,"role":"own","model":"neo","serial":"EMU3"})");
    CHECK(RunUntil(loop, [&] { return own != nullptr && v.Count(wire::Welcome) == 1; }));

    std::uint8_t feature[32] = {0x06};
    std::uint8_t buf[8] = {};
    std::atomic<int> got{0};
    // the viewer answers each request it sees with the given bytes
    auto ask = [&](int request, const std::string& answer) {
        got = 0;
        std::thread device([&] {
            got = own->GetFeature(feature);
            loop.Post([] {});
        });
        const bool asked = RunUntil(loop, [&] { return v.Count(wire::GetFeature) == request; });
        if (asked)
            v.Send(wire::FeatureReply, answer);
        const bool answered = RunUntil(loop, [&] { return got != 0; });
        device.join();
        return asked && answered;
    };
    CHECK(ask(1, std::string("\x06"
                             "first!",
                             7)));
    CHECK(got.load() == 7 && feature[1] == 'f');

    // A stray reply (handled: the input sent after it arrived), then a request: it gets the new answer only.
    v.Send(wire::FeatureReply, std::string("\x06stale", 6));
    v.Send(wire::Input, std::string("\x01\x01", 2));
    CHECK(RunUntil(loop, [&] { return own->Read(buf, 0) == 2; }));
    CHECK(ask(2, std::string("\x06"
                             "second!!",
                             9)));
    CHECK(got.load() == 9 && feature[1] == 's' && feature[2] == 'e');

    // Leave while a request waits.
    got = 0;
    const auto t0 = std::chrono::steady_clock::now();
    std::thread waiter([&] {
        got = own->GetFeature(feature);
        loop.Post([] {});
    });
    CHECK(RunUntil(loop, [&] { return v.Count(wire::GetFeature) == 3; }));
    v.Send(wire::Leave, {});
    CHECK(RunUntil(loop, [&] { return got != 0; }));
    waiter.join();
    CHECK(got.load() == -1 && std::chrono::steady_clock::now() - t0 < 500ms);
    CHECK(gone == std::vector<std::string>{"EMU3"});
    CHECK(own->Read(buf, 0) < 0 && own->Write(proto::Report{0x02, 0x07}) < 0 && own->GetFeature(feature) < 0);
}

// Leaving: a mirror's Leave closes its connection only (the deck and the other mirror go on); an own viewer that
// vanishes without a Leave ends its deck the same way a Leave does.
void TestLeave()
{
    net::EventLoop loop;
    DeckHub hub(loop, HubOptions());
    std::string error;
    CHECK(hub.Listen(&error));
    std::unique_ptr<Transport> own;
    std::vector<std::string> gone;
    hub.onOwnDeck = [&](std::unique_ptr<Transport> t, const Model&, const std::string&) { own = std::move(t); };
    hub.onOwnDeckGone = [&](const std::string& serial) { gone.push_back(serial); };
    const Model& m = PlusXl();
    auto deck = hub.AttachDeck(std::make_unique<FakeUsb>(), m, "SER1");
    TestViewer stays(loop, hub.Port()), leaves(loop, hub.Port()), crashes(loop, hub.Port());
    stays.Send(wire::Join, R"({"v":2,"role":"mirror"})");
    leaves.Send(wire::Join, R"({"v":2,"role":"mirror"})");
    crashes.Send(wire::Join, R"({"v":2,"role":"own","serial":"EMU4"})");
    CHECK(RunUntil(loop, [&] {
        return stays.Count(wire::ReplayEnd) == 1 && leaves.Count(wire::ReplayEnd) == 1 && own != nullptr;
    }));

    leaves.Send(wire::Leave, {});
    CHECK(RunUntil(loop, [&] { return leaves.closed && hub.ViewerCount() == 2; }));
    CHECK(RunUntil(loop, [&] { return !hub.Info().decks.empty() && hub.Info().decks[0].viewers.size() == 1; }));
    (void)deck->Write(proto::KeyImage(m, 1, std::vector<std::uint8_t>(100, 1))[0]);
    CHECK(RunUntil(loop, [&] { return stays.Count(wire::Output) == 1; }));
    CHECK(!stays.closed && gone.empty());

    crashes.Drop();
    CHECK(RunUntil(loop, [&] { return gone.size() == 1; }));
    CHECK(gone[0] == "EMU4" && hub.ViewerCount() == 1);
    std::uint8_t buf[8] = {};
    CHECK(own->Read(buf, 0) < 0);
    CHECK(RunUntil(loop, [&] { return hub.Info().decks.size() == 1; }));
}

// A length over kMaxFrame is a broken peer: the connection closes, whatever its role; the deck goes on.
void TestOversizeFrame()
{
    net::EventLoop loop;
    DeckHub hub(loop, HubOptions());
    std::string error;
    CHECK(hub.Listen(&error));
    std::unique_ptr<Transport> own;
    std::vector<std::string> gone;
    hub.onOwnDeck = [&](std::unique_ptr<Transport> t, const Model&, const std::string&) { own = std::move(t); };
    hub.onOwnDeckGone = [&](const std::string& serial) { gone.push_back(serial); };
    auto deck = hub.AttachDeck(std::make_unique<FakeUsb>(), PlusXl(), "SER1");
    TestViewer mirror(loop, hub.Port()), mine(loop, hub.Port()), unjoined(loop, hub.Port()), fine(loop, hub.Port());
    mirror.Send(wire::Join, R"({"v":2,"role":"mirror"})");
    mine.Send(wire::Join, R"({"v":2,"role":"own","serial":"EMU5"})");
    fine.Send(wire::Join, R"({"v":2,"role":"mirror"})");
    CHECK(RunUntil(loop, [&] {
        return mirror.Count(wire::ReplayEnd) == 1 && fine.Count(wire::ReplayEnd) == 1 && own != nullptr;
    }));
    mirror.SendRaw(OversizeHeader(wire::Input));
    mine.SendRaw(OversizeHeader(wire::FeatureReply));
    unjoined.SendRaw(OversizeHeader(wire::Join));
    CHECK(RunUntil(loop, [&] { return mirror.closed && mine.closed && unjoined.closed && gone.size() == 1; }));
    CHECK(gone[0] == "EMU5" && unjoined.frames.empty() && hub.ViewerCount() == 1 && !fine.closed);
    // the other mirror and the deck are unaffected
    fine.Send(wire::Input, std::string("\x01\x00\x00\x00\x01", 5));
    std::uint8_t buf[16] = {};
    CHECK(RunUntil(loop, [&] { return deck->Read(buf, 0) == 5; }));
}

// ------------------------------------------------------------------ flow control
// A slow mirror next to a fast one: the fast one gets every frame and no extra replay, the slow one loses images
// but keeps the feature reports and catches up with a replay; its own input is never dropped; the deck never waits.
void TestSlowViewerBesideFast()
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
    TestViewer slow(loop, hub.Port(), 8 << 10), fast(loop, hub.Port());
    slow.Send(wire::Join, R"({"v":2,"role":"mirror"})");
    fast.Send(wire::Join, R"({"v":2,"role":"mirror"})");
    CHECK(RunUntil(loop, [&] { return slow.Count(wire::ReplayEnd) == 1 && fast.Count(wire::ReplayEnd) == 1; }));

    slow.PauseReading(true);
    slow.Send(wire::Input, std::string("\x01\x00\x00\x00\x01", 5));  // sent while it does not read
    const std::vector<std::uint8_t> img(3000, 9);
    const int perKey = static_cast<int>(proto::KeyImage(m, 0, img).size());
    const int chains = 48;
    int expect = 0;
    std::chrono::steady_clock::duration writing{};
    for (int i = 0; i < chains; ++i)
    {
        const auto t0 = std::chrono::steady_clock::now();
        for (const auto& r : proto::KeyImage(m, i % m.keys, img))
            (void)deck->Write(r);
        if (i == chains / 2)
            (void)deck->SendFeature(proto::Brightness(m, 77));
        writing += std::chrono::steady_clock::now() - t0;
        expect += perKey;
        if (!RunUntil(loop, [&] { return fast.Count(wire::Output) == expect; }, 500ms))
        {
            CHECK(fast.Count(wire::Output) == expect);  // the fast mirror missed frames
            break;
        }
    }
    CHECK(fast.Count(wire::ReplayBegin) == 1 && fast.Count(wire::SetFeature) == 1);
    CHECK(writing < 50ms);  // posts only: a stalled viewer never holds the device thread

    std::uint8_t buf[16] = {};
    CHECK(RunUntil(loop, [&] { return deck->Read(buf, 0) == 5; }));  // the slow viewer's input arrived
    slow.PauseReading(false);
    CHECK(RunUntil(loop, [&] { return slow.Count(wire::ReplayEnd) == 2; }, 1000ms));
    CHECK(slow.Count(wire::Output) < chains * perKey);
    bool brightness = false;
    for (const auto& f : slow.frames)
        brightness = brightness || (f.type == wire::SetFeature && f.payload == Str(proto::Brightness(m, 77)));
    CHECK(brightness);
    CHECK(!slow.closed && !fast.closed);
}

// ------------------------------------------------------------------ the daemon's routes over a live hub
automation::Reply Call(const automation::Api& api, const std::string& method, const std::string& path,
                       json body = nullptr, const std::string& deck = {})
{
    automation::Request r;
    r.method = method;
    r.path = path;
    r.body = std::move(body);
    if (!deck.empty())
        r.query["deck"] = deck;
    return api.Dispatch(r);
}

// GET /health and /decks with a real deck and its mirror, an own deck, a deck the hub has without a runtime here and a
// waiting mirror; per-deck routes; snapshot and reconnect rules; the runtime routes by ?deck=; MCP over the same.
void TestDaemonRoutesOverHub()
{
    net::EventLoop loop;
    DeckHub hub(loop, HubOptions());
    std::string error;
    CHECK(hub.Listen(&error));
    DaemonDecks::Options dopt;
    dopt.version = "9.8";
    DaemonDecks decks(dopt);
    CHECK(decks.LoadStart(&error));
    decks.SetHub(&hub, hub.Port());
    automation::Api api("unreal-streamdeck", "9.8");
    decks.AddRoutes(api);

    const Model& m = PlusXl();
    auto realDev = std::make_unique<Device>(m, hub.AttachDeck(std::make_unique<FakeUsb>(), m, "SER1"), "SER1");
    auto realRt = std::make_unique<scn::Runtime>(*realDev, decks.StartScenario());
    decks.Add("SER1", m, true, *realRt);
    realRt->Step(0);
    auto bare = hub.AttachDeck(std::make_unique<FakeUsb>(), *FindModelByName("neo"), "SER2");  // no runtime here

    std::unique_ptr<Device> ownDev;
    std::unique_ptr<scn::Runtime> ownRt;
    hub.onOwnDeck = [&](std::unique_ptr<Transport> t, const Model& om, const std::string& serial) {
        ownDev = std::make_unique<Device>(om, std::move(t), serial);
        ownRt = std::make_unique<scn::Runtime>(*ownDev, decks.StartScenario());
        decks.Add(serial, om, false, *ownRt);
    };
    hub.onOwnDeckGone = [&](const std::string& serial) { decks.Remove(serial); };

    TestViewer mirror(loop, hub.Port()), own(loop, hub.Port()), waiting(loop, hub.Port());
    mirror.Send(wire::Join, R"({"v":2,"role":"mirror","deck":"SER1","name":"Studio","pid":7})");
    own.Send(wire::Join, R"({"v":2,"role":"own","model":"mini","serial":"EMU5","name":"Mine"})");
    waiting.Send(wire::Join, R"({"v":2,"role":"mirror","deck":"SER9"})");
    CHECK(RunUntil(loop, [&] {
        return mirror.Count(wire::ReplayEnd) == 1 && own.Count(wire::Welcome) == 1 &&
               waiting.Count(wire::Welcome) == 1 && hub.Info().waiting == 1;
    }));

    // health
    json h = Call(api, "GET", "/api/v1/health").body;
    CHECK(h.value("ok", false) && h.value("name", "") == "unreal-streamdeck" && h.value("version", "") == "9.8");
    CHECK(h.value("decks", 0) == 3 && h.value("viewers", 0) == 2 && h.value("waiting_mirrors", 0) == 1 &&
          h.value("wire_port", 0) == hub.Port());

    // the list: real decks with a runtime, own decks, then decks only the hub has
    const json l = Call(api, "GET", "/api/v1/decks").body;
    const json& list = l["decks"];
    CHECK(list.size() == 3 && l.value("waiting_mirrors", 0) == 1);
    if (list.size() == 3)
    {
        CHECK(list[0]["serial"] == "SER1" && list[0]["real"] == true && list[0]["connected"] == true &&
              list[0]["viewers"].size() == 1 && list[0]["viewers"][0]["name"] == "Studio" &&
              list[0]["viewers"][0]["pid"] == 7 && list[0]["viewers"][0]["role"] == "mirror");
        CHECK(list[1]["serial"] == "EMU5" && list[1]["real"] == false &&
              list[1]["model"] == std::string(FindModelByName("mini")->name) && list[1]["viewers"].size() == 1 &&
              list[1]["viewers"][0]["role"] == "own");
        CHECK(list[2]["serial"] == "SER2" && list[2]["runtime"] == false && list[2]["connected"] == true &&
              list[2]["model"] == std::string(FindModelByName("neo")->name) && list[2]["viewers"].empty());
    }
    CHECK(Call(api, "GET", "/api/v1/decks/first").body.value("serial", "") == "SER1");
    CHECK(Call(api, "GET", "/api/v1/decks/EMU5").body.value("serial", "") == "EMU5");

    // snapshot: to the deck's viewers only, as a Control frame
    automation::Reply r = Call(api, "POST", "/api/v1/decks/EMU5/snapshot", {{"path", "/tmp/emu5.png"}});
    CHECK(r.status == 200 && r.body.value("viewers", 0) == 1 && r.body.value("path", "") == "/tmp/emu5.png");
    r = Call(api, "POST", "/api/v1/decks/first/snapshot", {{"path", "/tmp/ser1.png"}});
    CHECK(r.status == 200 && r.body.value("serial", "") == "SER1" && r.body.value("viewers", 0) == 1);
    CHECK(RunUntil(loop, [&] { return own.Count(wire::Control) == 1 && mirror.Count(wire::Control) == 1; }));
    for (const TestViewer* v : {&own, &mirror})
        for (const auto& f : v->frames)
            if (f.type == wire::Control)
                CHECK(json::parse(f.payload).value("op", "") == "snapshot");
    CHECK_EQ(waiting.Count(wire::Control), 0);
    CHECK_EQ(Call(api, "POST", "/api/v1/decks/NOPE/snapshot", {{"path", "/tmp/x.png"}}).status, 404);

    // reconnect: an own deck has no USB handle; the range of away_ms
    CHECK_EQ(Call(api, "POST", "/api/v1/decks/EMU5/reconnect", {{"away_ms", 0}}).status, 409);
    CHECK_EQ(Call(api, "POST", "/api/v1/decks/SER1/reconnect", {{"away_ms", 60001}}).status, 400);
    CHECK_EQ(Call(api, "POST", "/api/v1/decks/NOPE/reconnect", {{"away_ms", 0}}).status, 404);

    // the runtime routes: ?deck= picks the deck, the first real deck by default
    ownRt->Step(0);
    CHECK(Call(api, "GET", "/api/v1/state").body.value("deck", "") == "SER1");
    CHECK(Call(api, "GET", "/api/v1/state", nullptr, "EMU5").body.value("deck", "") == "EMU5");
    r = Call(api, "GET", "/api/v1/page", nullptr, "EMU5");
    CHECK(r.status == 200 && r.body.is_object());
    r = Call(api, "GET", "/api/v1/nodes", nullptr, "EMU5");
    CHECK(r.status == 200 && r.body["nodes"].is_array());
    CHECK_EQ(Call(api, "GET", "/api/v1/page", nullptr, "NOPE").status, 404);
    CHECK_EQ(Call(api, "POST", "/api/v1/press", {{"node", "nosuch"}}, "EMU5").status, 404);  // refused before it waits
    CHECK_EQ(Call(api, "POST", "/api/v1/navigate", {{"to", "home"}}, "NOPE").status, 404);
    r = Call(api, "GET", "/api/v1/scenario", nullptr, "EMU5");
    CHECK(r.status == 200 && r.body.value("deck", "") == "EMU5" && r.body["document"].is_object());

    // MCP: the same routes, the answer in the text an agent reads
    const json mcp = api.Mcp({{"jsonrpc", "2.0"},
                              {"id", 1},
                              {"method", "tools/call"},
                              {"params", {{"name", "invoke_api"}, {"arguments", {{"path", "/api/v1/health"}}}}}});
    const std::string text = mcp["result"]["content"][0].value("text", "");
    CHECK(text.find("wire_port") != std::string::npos && text.find("waiting_mirrors") != std::string::npos);

    // The own viewer leaves: its deck is gone from the routes. The mirror leaves: nobody views SER1 any more.
    own.Send(wire::Leave, {});
    CHECK(RunUntil(loop, [&] { return decks.Count() == 1; }));
    CHECK_EQ(Call(api, "GET", "/api/v1/decks/EMU5").status, 404);
    mirror.Send(wire::Leave, {});
    CHECK(RunUntil(loop, [&] { return hub.Info().decks.size() == 2 && hub.Info().decks[0].viewers.empty(); }));
    CHECK_EQ(Call(api, "POST", "/api/v1/decks/SER1/snapshot", {{"path", "/tmp/x.png"}}).status, 409);

    // The bare deck unplugged: still listed, not connected.
    bare.reset();
    CHECK(RunUntil(loop, [&] {
        const json d = Call(api, "GET", "/api/v1/decks").body["decks"];
        return d.size() == 2 && d[1]["serial"] == "SER2" && d[1]["connected"] == false;
    }));
    CHECK(RunUntil(loop, [&] { return hub.Info().waiting == 1; }));
    CHECK_EQ(Call(api, "GET", "/api/v1/health").body.value("waiting_mirrors", 0), 1);

    decks.Remove("SER1");
    realRt.reset();
    realDev.reset();
    ownRt.reset();
    ownDev.reset();
}

// A deck with nothing to read: Read waits its timeout like hidapi's, so the device thread does not hold the runtime's
// lock back to back (a FakeUsb read returns at once: the stepping loop would starve the routes).
struct IdleUsb final : Transport
{
    std::mutex mutex;
    std::condition_variable never;
    int Write(std::span<const std::uint8_t> r) override
    {
        return static_cast<int>(r.size());
    }
    int SendFeature(std::span<const std::uint8_t> r) override
    {
        return static_cast<int>(r.size());
    }
    int GetFeature(std::span<std::uint8_t> buf) override
    {
        return static_cast<int>(buf.size());
    }
    int Read(std::span<std::uint8_t>, int timeoutMs) override
    {
        std::unique_lock lock(mutex);
        never.wait_for(lock, std::chrono::milliseconds(timeoutMs));
        return 0;
    }
};

// POST /press and /navigate that succeed: the answer is the state after the device thread's step (no fixed wait),
// key or node; a device thread that does not come round answers 202 within the bound, the request applied later.
void TestPressAndNavigate()
{
    // a Mini: 6 small keys, so each page change the device thread draws stays cheap (the 50 ms budget)
    const Model& m = *FindModelByName("mini");
    std::vector<scn::Issue> issues;
    auto scenario = scn::Parse(json::parse(R"({ "name": "routes", "model": "mini",
      "root": { "id": "home", "type": "menu", "title": "Home", "items": [
        { "id": "pause", "type": "action", "title": "Pause", "at": [0, 1], "do": { "kind": "webapi", "path": "/pause" } },
        { "id": "debug", "type": "menu", "title": "Debug", "at": [0, 2], "items": [
          { "id": "bp", "type": "menu", "title": "Breakpoints", "at": [1, 1], "items": [
            { "id": "bp.add", "type": "action", "title": "Add", "at": [1, 2], "do": { "kind": "webapi", "path": "/bp" } } ] } ] } ] } })"),
                               issues);
    for (const auto& i : issues)
        std::printf("    %s: %s\n", i.node.c_str(), i.message.c_str());
    CHECK(scenario.has_value());
    if (!scenario)
        return;
    Device dev(m, std::make_unique<IdleUsb>(), "SER1");
    scn::RuntimeOptions ro;
    ro.dryRun = true;  // no emulator here: actions are logged
    scn::Runtime rt(dev, *scenario, nullptr, ro);
    DaemonDecks decks(DaemonDecks::Options{});
    decks.Add("SER1", m, true, rt);
    automation::Api api("unreal-streamdeck", "1.0");
    decks.AddRoutes(api);
    DaemonDecks::Options stuckOptions;
    stuckOptions.applyTimeoutMs = 5;
    DaemonDecks stuck(stuckOptions);  // the same runtime behind a short bound, for the 202 case
    stuck.Add("SER1", m, true, rt);
    automation::Api stuckApi("unreal-streamdeck", "1.0");
    stuck.AddRoutes(stuckApi);

    auto stack = [](const automation::Reply& r) {
        std::vector<std::string> ids;
        for (const auto& n : r.body.value("stack", json::array()))
            ids.push_back(n.value("id", ""));
        return ids;
    };
    using V = std::vector<std::string>;
    {
        std::atomic<bool> stop{false};
        std::thread device([&] {
            while (!stop)
                rt.Step(1);
        });
        automation::Reply r = Call(api, "POST", "/api/v1/press", {{"node", "debug"}});
        CHECK(r.status == 200 && r.body.value("applied", false) && r.body.value("deck", "") == "SER1");
        CHECK((stack(r) == V{"home", "debug"}));
        const int back = r.body["page"].value("back_key", -1);
        r = Call(api, "POST", "/api/v1/navigate", {{"to", "node"}, {"node", "bp"}}, "SER1");
        CHECK(r.status == 200 && (stack(r) == V{"home", "debug", "bp"}));
        r = Call(api, "POST", "/api/v1/navigate", {{"to", "back"}});
        CHECK(r.status == 200 && (stack(r) == V{"home", "debug"}));
        CHECK(back >= 0);
        r = Call(api, "POST", "/api/v1/press", {{"key", back}});  // down and up in one step: back, not hold
        CHECK(r.status == 200 && r.body.value("applied", false) && (stack(r) == V{"home"}));
        r = Call(api, "POST", "/api/v1/press", {{"node", "pause"}});  // an action: the page stays
        CHECK(r.status == 200 && (stack(r) == V{"home"}));
        r = Call(api, "POST", "/api/v1/navigate", {{"to", "node"}, {"node", "bp"}});
        CHECK(r.status == 200 && (stack(r) == V{"home", "debug", "bp"}));
        r = Call(api, "POST", "/api/v1/navigate", {{"to", "home"}});
        CHECK(r.status == 200 && (stack(r) == V{"home"}));
        stop = true;
        device.join();
    }

    // nobody steps: 202 within the bound, the state before; the next step applies it
    automation::Reply r = Call(stuckApi, "POST", "/api/v1/press", {{"node", "debug"}});
    CHECK(r.status == 202 && !r.body.value("applied", true) && (stack(r) == V{"home"}));
    rt.Step(0);
    CHECK((stack(Call(api, "GET", "/api/v1/state")) == V{"home", "debug"}));
    CHECK_EQ(Call(api, "POST", "/api/v1/press", {{"node", "nosuch"}}).status, 404);  // refused before it waits

    stuck.Remove("SER1");
    decks.Remove("SER1");
}

// Each case runs alone; one over the 50 ms budget is named (not failed: a loaded CI runner may be slow).
void Timed(const char* name, void (*fn)())
{
    const auto t0 = std::chrono::steady_clock::now();
    fn();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    if (ms > 50ms)
        std::printf("  SLOW conformance %s: %lld ms (budget 50 ms)\n", name, static_cast<long long>(ms.count()));
}

}  // namespace

void TestConformance()
{
    Timed("FrameBoundaries", TestFrameBoundaries);
    Timed("JoinRoles", TestJoinRoles);
    Timed("WelcomeErrors", TestWelcomeErrors);
    Timed("ReplayFraming", TestReplayFraming);
    Timed("InputMerge", TestInputMerge);
    Timed("OwnFeatureTimeout", TestOwnFeatureTimeout);
    Timed("OwnFeatureReplyAndLeave", TestOwnFeatureReplyAndLeave);
    Timed("Leave", TestLeave);
    Timed("OversizeFrame", TestOversizeFrame);
    Timed("SlowViewerBesideFast", TestSlowViewerBesideFast);
    Timed("DaemonRoutesOverHub", TestDaemonRoutesOverHub);
    Timed("PressAndNavigate", TestPressAndNavigate);
}
