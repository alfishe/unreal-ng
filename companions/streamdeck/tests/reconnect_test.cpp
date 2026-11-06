// Hot plug of a USB deck (sdb/reconnect.h) with fake decks on a fake bus: an unplug found by a failed write or read,
// the picture kept while the deck is away and replayed to it when it is back, brightness included; a reconnect on
// purpose; another deck after the lost one stayed away; a deck that goes again while it is drawn.
#include "sdb/reconnect.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "sdb/device.h"
#include "sdb/protocol.h"

using namespace sdb;
using namespace std::chrono_literals;

namespace
{

const Model& PlusXl()
{
    return *FindModelByName("plusxl");
}

// The USB bus: which decks are plugged in, and what each was sent since it was last opened.
struct Bus
{
    struct Deck
    {
        bool present = true;
        int failWritesAfter = -1;  // the deck goes after that many more writes (-1 = never)
        int opens = 0;
        bool open = false;
        std::vector<std::string> writes, features;
        std::deque<std::string> input;
    };
    std::map<std::string, Deck> decks;
    std::vector<bool> asked;  // anyDeck of every reopen attempt
    std::vector<std::chrono::steady_clock::time_point> askedAt;
};

struct FakeDeck final : Transport
{
    Bus& bus;
    std::string serial;
    FakeDeck(Bus& b, std::string s) : bus(b), serial(std::move(s))
    {
        Bus::Deck& d = bus.decks[serial];
        ++d.opens;
        d.open = true;
        d.writes.clear();
        d.features.clear();
    }
    ~FakeDeck() override
    {
        bus.decks[serial].open = false;
    }
    Bus::Deck& D()
    {
        return bus.decks[serial];
    }
    bool Gone()
    {
        Bus::Deck& d = D();
        if (d.failWritesAfter == 0)
            d.present = false;
        return !d.present;
    }
    int Write(std::span<const std::uint8_t> r) override
    {
        if (Gone())
            return -1;
        if (D().failWritesAfter > 0)
            --D().failWritesAfter;
        D().writes.emplace_back(r.begin(), r.end());
        return static_cast<int>(r.size());
    }
    int SendFeature(std::span<const std::uint8_t> r) override
    {
        if (Gone())
            return -1;
        D().features.emplace_back(r.begin(), r.end());
        return static_cast<int>(r.size());
    }
    int GetFeature(std::span<std::uint8_t> b) override
    {
        return Gone() ? -1 : static_cast<int>(b.size());
    }
    int Read(std::span<std::uint8_t> b, int) override
    {
        if (Gone())
            return -1;
        if (D().input.empty())
            return 0;
        const std::string r = D().input.front();
        D().input.pop_front();
        std::copy(r.begin(), r.end(), b.begin());
        return static_cast<int>(r.size());
    }
    std::string LastError() const override
    {
        return "device disconnected";
    }
};

// As deviceopen.cpp's reopen: the same serial when it is there, else another deck when that may be reported.
ReconnectingTransport::Reopen Reopen(Bus& bus, const std::string& serial)
{
    return [&bus, serial](bool anyDeck) {
        bus.asked.push_back(anyDeck);
        bus.askedAt.push_back(std::chrono::steady_clock::now());
        ReconnectingTransport::Probe p;
        for (auto& [s, d] : bus.decks)
            if (d.present && s == serial)
            {
                p.same = std::make_unique<FakeDeck>(bus, s);
                return p;
            }
        for (auto& [s, d] : bus.decks)
            if (d.present && anyDeck && s != serial)
                p.other = s;
        return p;
    };
}

struct Events
{
    std::vector<std::pair<bool, std::string>> changes;
    std::vector<std::string> others;
};

ReconnectOptions Options(Events& ev, std::chrono::milliseconds otherAfter = 1000ms)
{
    ReconnectOptions o;
    o.retry = 1ms;
    o.otherDeckAfter = otherAfter;
    o.onChange = [&ev](bool c, const std::string& why) { ev.changes.emplace_back(c, why); };
    o.onOtherDeck = [&ev](const std::string& s) { ev.others.push_back(s); };
    return o;
}

// The device thread's loop, bounded: reads until `done` holds.
bool ReadUntil(Transport& t, const std::function<bool()>& done, std::chrono::milliseconds limit = 500ms)
{
    std::uint8_t buf[64] = {};
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!done())
    {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        (void)t.Read(buf, 1);
    }
    return true;
}

std::string Str(const proto::Report& r)
{
    return std::string(r.begin(), r.end());
}

std::unique_ptr<ReconnectingTransport> Make(Bus& bus, const std::string& serial, Events& ev,
                                            std::chrono::milliseconds otherAfter = 1000ms)
{
    return std::make_unique<ReconnectingTransport>(std::make_unique<FakeDeck>(bus, serial), PlusXl(), serial,
                                                   Reopen(bus, serial), Options(ev, otherAfter));
}

void SendImage(Transport& t, int key, std::uint8_t fill, std::size_t size = 2500)
{
    const std::vector<std::uint8_t> img(size, fill);
    for (const auto& r : proto::KeyImage(PlusXl(), key, img))
        (void)t.Write(r);
}

bool Has(const std::vector<std::string>& v, const std::string& s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

void TestUnplugByWriteAndReplay()
{
    std::puts("reconnect: an unplug found by a write, the picture replayed");
    Bus bus;
    Events ev;
    const Model& m = PlusXl();
    auto t = Make(bus, "SER1", ev);
    SendImage(*t, 3, 0x33);
    CHECK_EQ(t->SendFeature(proto::Brightness(m, 40)), static_cast<int>(proto::Brightness(m, 40).size()));
    CHECK(t->Connected() && bus.decks["SER1"].writes.size() == 3);

    // Unplugged in the middle of an image: the image still goes on whole (to the cache), the handle is closed.
    bus.decks["SER1"].failWritesAfter = 1;
    const auto chain = proto::KeyImage(m, 5, std::vector<std::uint8_t>(2500, 0x55));
    for (const auto& r : chain)
        CHECK_EQ(t->Write(r), static_cast<int>(r.size()));
    CHECK(!t->Connected() && !bus.decks["SER1"].open);
    CHECK(ev.changes.size() == 1 && !ev.changes[0].first && ev.changes[0].second == "device disconnected");
    CHECK(t->LastError().find("unplugged") != std::string::npos);
    std::uint8_t buf[16];
    CHECK_EQ(t->GetFeature(buf), -1);

    // Drawn while away: kept for the deck. Nothing comes back while it is away.
    (void)t->SendFeature(proto::Brightness(m, 65));
    SendImage(*t, 7, 0x77);
    CHECK(ReadUntil(*t, [&] { return bus.asked.size() >= 2; }));
    CHECK(!t->Connected() && bus.decks["SER1"].opens == 1);

    // Plugged back: the same deck, its picture and the last brightness, nothing it does not need.
    bus.decks["SER1"].failWritesAfter = -1;
    bus.decks["SER1"].present = true;
    CHECK(ReadUntil(*t, [&] { return t->Connected(); }));
    const Bus::Deck& d = bus.decks["SER1"];
    CHECK(d.opens == 2 && d.open && t->Reconnects() == 1);
    CHECK(ev.changes.size() == 2 && ev.changes[1].first);
    CHECK(d.features.size() == 1 && d.features[0] == Str(proto::Brightness(m, 65)));
    CHECK_EQ(d.writes.size(), 9u);  // keys 3, 5 and 7, three reports each
    for (const auto& r : chain)
        CHECK(Has(d.writes, Str(r)));
    CHECK(ev.others.empty());

    // Live again: writes reach the deck.
    SendImage(*t, 1, 0x11);
    CHECK_EQ(bus.decks["SER1"].writes.size(), 12u);
}

void TestUnplugByReadAndRequest()
{
    std::puts("reconnect: an unplug found by a read; a reconnect on purpose");
    Bus bus;
    Events ev;
    auto t = Make(bus, "SER1", ev);
    SendImage(*t, 0, 0x10);
    bus.decks["SER1"].input.push_back(std::string("\x01\x00\x24\x00\x01", 5));
    std::uint8_t buf[64] = {};
    CHECK_EQ(t->Read(buf, 0), 5);

    bus.decks["SER1"].present = false;
    CHECK_EQ(t->Read(buf, 0), 0);  // an unplug is no error for the runtime above
    CHECK(!t->Connected() && ev.changes.size() == 1);
    bus.decks["SER1"].present = true;
    CHECK(ReadUntil(*t, [&] { return t->Connected(); }));
    CHECK_EQ(bus.decks["SER1"].writes.size(), 3u);

    // Reconnect(away) from another thread: the device thread closes on its next call and stays away that long.
    CHECK(t->Reconnect(20ms));
    CHECK(t->Connected());  // nothing happens off the device thread
    const auto asked = std::chrono::steady_clock::now();
    (void)t->Read(buf, 0);
    CHECK(!t->Connected() && !bus.decks["SER1"].open && ev.changes.back().second == "reconnect requested");
    CHECK(ReadUntil(*t, [&] { return t->Connected(); }));
    CHECK(std::chrono::steady_clock::now() - asked >= 20ms);
    CHECK(bus.decks["SER1"].opens == 3 && bus.decks["SER1"].writes.size() == 3u);
    CHECK_EQ(t->Reconnects(), 2);
}

void TestOtherDeck()
{
    std::puts("reconnect: another deck only after the lost one stayed away");
    Bus bus;
    Events ev;
    auto t = Make(bus, "SER1", ev, 15ms);
    bus.decks["SER2"].present = true;
    bus.decks["SER1"].present = false;
    std::uint8_t buf[64] = {};
    const auto lost = std::chrono::steady_clock::now();
    (void)t->Read(buf, 0);
    CHECK(!t->Connected());
    CHECK(ReadUntil(*t, [&] { return !ev.others.empty(); }));
    // the first attempts look for the lost deck only (a first attempt 15 ms late, the thread descheduled, may not)
    CHECK(!bus.asked.empty() && (!bus.asked.front() || bus.askedAt.front() - lost >= 15ms));
    CHECK(ev.others.size() == 1 && ev.others[0] == "SER2");
    CHECK(bus.decks["SER2"].opens == 0);  // the owner opens it, with a runtime of its own
    // Told once; the lost deck may still come back until the owner acts.
    CHECK(ReadUntil(*t, [&] { return bus.asked.size() >= 6; }));
    CHECK_EQ(ev.others.size(), 1u);
    bus.decks["SER1"].present = true;
    CHECK(ReadUntil(*t, [&] { return t->Connected(); }));
}

void TestGoneWhileReplayed()
{
    std::puts("reconnect: a deck that goes again while its picture is replayed");
    Bus bus;
    Events ev;
    auto t = Make(bus, "SER1", ev);
    for (int k = 0; k < 4; ++k)
        SendImage(*t, k, static_cast<std::uint8_t>(k));
    bus.decks["SER1"].present = false;
    std::uint8_t buf[64] = {};
    (void)t->Read(buf, 0);
    bus.decks["SER1"].present = true;
    bus.decks["SER1"].failWritesAfter = 4;  // opens, then fails in the middle of the replay
    CHECK(ReadUntil(*t, [&] { return bus.decks["SER1"].opens >= 2; }));
    CHECK(!t->Connected() && !bus.decks["SER1"].open && ev.changes.size() == 1);
    bus.decks["SER1"].failWritesAfter = -1;
    bus.decks["SER1"].present = true;
    CHECK(ReadUntil(*t, [&] { return t->Connected(); }));
    CHECK_EQ(bus.decks["SER1"].writes.size(), 12u);
}

void TestDeviceAndRuntimeView()
{
    std::puts("reconnect: Device and Found");
    Bus bus;
    Events ev;
    auto owned = Make(bus, "SER1", ev);
    ReconnectingTransport* t = owned.get();
    Device dev(PlusXl(), std::move(owned), "SER1");
    CHECK(dev.Connected());
    CHECK(dev.SetBrightness(30));
    bus.decks["SER1"].present = false;
    CHECK(dev.PollEvents(0).empty());
    CHECK(!dev.Connected());
    CHECK(dev.SetBrightness(45));  // the runtime's writes go on
    bus.decks["SER1"].present = true;
    CHECK(ReadUntil(*t, [&] { return dev.Connected(); }));
    CHECK(bus.decks["SER1"].features.size() == 1 &&
          bus.decks["SER1"].features[0] == Str(proto::Brightness(PlusXl(), 45)));
    // a plain transport does not reconnect
    struct Plain final : Transport
    {
        int Write(std::span<const std::uint8_t>) override
        {
            return 0;
        }
        int SendFeature(std::span<const std::uint8_t>) override
        {
            return 0;
        }
        int GetFeature(std::span<std::uint8_t>) override
        {
            return 0;
        }
        int Read(std::span<std::uint8_t>, int) override
        {
            return 0;
        }
    };
    Device plain(PlusXl(), std::make_unique<Plain>(), "P");
    CHECK(plain.Connected() && !plain.Reconnect(0ms));
}

}  // namespace

void TestHotplug()
{
    TestUnplugByWriteAndReplay();
    TestUnplugByReadAndRequest();
    TestOtherDeck();
    TestGoneWhileReplayed();
    TestDeviceAndRuntimeView();
}
