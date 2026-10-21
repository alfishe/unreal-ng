// The daemon's logical decks and their automation routes: the deck list, brightness per deck, a scenario per deck
// (the others keep theirs), the start scenario back, watched files, loads asked for by a runtime, MCP over the same
// routes. Runtimes on null transports, no hardware, no network.
#include "sdb/daemondecks.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "check.h"
#include "sdb/runtime.h"

using namespace sdb;
using namespace sdb::scn;
using automation::Reply;
using automation::Request;
namespace fs = std::filesystem;

namespace
{
struct NullTransport final : Transport
{
    int Write(std::span<const std::uint8_t> r) override
    {
        return static_cast<int>(r.size());
    }
    int SendFeature(std::span<const std::uint8_t> r) override
    {
        return static_cast<int>(r.size());
    }
    int GetFeature(std::span<std::uint8_t> b) override
    {
        return static_cast<int>(b.size());
    }
    int Read(std::span<std::uint8_t>, int) override
    {
        return 0;
    }
};

struct QuietHost final : ActionHost
{
    HttpResponse Webapi(const std::string&, const std::string&, const json&, bool) override
    {
        return {};
    }
    HttpResponse Http(const std::string&, const std::string&, const json&, const std::string&) override
    {
        return {};
    }
    int Shell(const std::string&, int, std::string&) override
    {
        return 0;
    }
    bool Open(const std::string&) override
    {
        return true;
    }
    bool Keys(const std::string&, const std::string&, std::string&) override
    {
        return true;
    }
    void Navigate(const json&) override {}
    void Brightness(const json&) override {}
    void SleepMs(int) override {}
    void Note(const std::string&) override {}
};

// A logical deck as the daemon has one: a device on a transport, its runtime (stepped by the test).
struct TestDeck
{
    Device dev;
    Runtime rt;
    TestDeck(const char* model, const std::string& serial, Scenario s)
        : dev(*FindModelByName(model), std::make_unique<NullTransport>(), serial),
          rt(dev, std::move(s), std::make_unique<QuietHost>(), RuntimeOptions{true})
    {
    }
    std::string Playing()
    {
        rt.Step(0);  // a reload is applied by the device thread's next step
        return rt.State().value("scenario", "");
    }
};

void WriteScenario(const fs::path& file, const std::string& name)
{
    std::ofstream(file) << R"({"name": ")" << name << R"(", "model": "neo",
      "root": {"id": "home", "type": "menu", "title": "Home", "items": [
        {"id": "a", "type": "action", "title": "A", "at": [0, 1], "do": {"kind": "webapi", "path": "/x"}}]}})";
}

void Touch(const fs::path& file)
{
    // a later mtime without waiting for the clock
    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(2));
}

Reply Call(const automation::Api& api, const std::string& method, const std::string& path, json body = nullptr)
{
    Request r;
    r.method = method;
    r.path = path;
    r.body = std::move(body);
    return api.Dispatch(r);
}
}  // namespace

void TestDaemonDecks()
{
    const fs::path dir = fs::temp_directory_path() / "sdb-daemondecks-test";
    fs::create_directories(dir);
    const fs::path mine = dir / "mine.deck.json", broken = dir / "broken.deck.json";
    WriteScenario(mine, "mine");
    std::ofstream(broken) << "{ not json";

    // No start file: the idle scenario (one empty menu).
    CHECK(DaemonDecks::Idle().name == "idle");
    DaemonDecks decks(DaemonDecks::Options{});
    std::vector<std::string> lines;
    decks.log = [&](const std::string& l) { lines.push_back(l); };
    std::string err;
    CHECK(decks.LoadStart(&err));
    automation::Api api("unreal-streamdeck", "1.0");
    decks.AddRoutes(api);

    // No deck yet: the runtime routes say so; the list is empty.
    CHECK_EQ(Call(api, "GET", "/api/v1/state").status, 404);
    CHECK(Call(api, "GET", "/api/v1/decks").body["decks"].empty());

    TestDeck own("neo", "EMU1", decks.StartScenario());
    TestDeck real("plusxl", "SER1", decks.StartScenario());
    decks.Add("EMU1", own.dev.GetModel(), false, own.rt);
    decks.Add("SER1", real.dev.GetModel(), true, real.rt);

    // The list: the real deck first, then the own ones; "first" is the real deck.
    Reply r = Call(api, "GET", "/api/v1/decks");
    CHECK_EQ(r.status, 200);
    const json& list = r.body["decks"];
    CHECK_EQ(list.size(), 2u);
    CHECK(list.size() == 2 && list[0]["serial"] == "SER1" && list[0]["real"] == true && list[1]["serial"] == "EMU1" &&
          list[1]["real"] == false);
    CHECK(list.size() == 2 && list[1]["pid"] == FindModelByName("neo")->pid && list[1]["connected"] == true &&
          list[1]["scenario"] == "" && list[1]["start_scenario"] == true);
    CHECK(Call(api, "GET", "/api/v1/decks/first").body.value("serial", "") == "SER1");
    CHECK_EQ(Call(api, "GET", "/api/v1/decks/NOPE").status, 404);
    CHECK(Call(api, "GET", "/api/v1/health").body.value("decks", 0) == 2);
    CHECK(Call(api, "GET", "/api/v1/state").body.value("deck", "") == "SER1");

    // Brightness per deck: a value, a change, clamped; the other deck keeps its own.
    const int realBefore = real.rt.Brightness();
    r = Call(api, "PUT", "/api/v1/decks/EMU1/brightness", {{"value", 30}});
    CHECK(r.status == 200 && r.body.value("brightness", -1) == 30 && own.rt.Brightness() == 30);
    CHECK_EQ(Call(api, "PUT", "/api/v1/decks/EMU1/brightness", {{"delta", -10}}).body.value("brightness", -1), 20);
    CHECK_EQ(Call(api, "PUT", "/api/v1/decks/EMU1/brightness", {{"value", 150}}).body.value("brightness", -1), 100);
    CHECK_EQ(Call(api, "PUT", "/api/v1/decks/EMU1/brightness", json::object()).status, 400);
    CHECK_EQ(Call(api, "GET", "/api/v1/decks/EMU1/brightness").body.value("brightness", -1), 100);
    CHECK_EQ(real.rt.Brightness(), realBefore);
    CHECK_EQ(Call(api, "PUT", "/api/v1/decks/NOPE/brightness", {{"value", 5}}).status, 404);

    // A scenario for one deck: the own deck plays it, the real one keeps the start scenario.
    r = Call(api, "PUT", "/api/v1/decks/EMU1/scenario", {{"file", mine.string()}});
    CHECK_EQ(r.status, 200);
    CHECK(r.body.value("scenario", "") == mine.string() && r.body.value("start_scenario", true) == false &&
          r.body.value("watch", false) == true);
    CHECK(own.Playing() == "mine");
    CHECK(real.Playing() == "idle");
    r = Call(api, "GET", "/api/v1/decks/EMU1/scenario");
    CHECK(r.body.value("file", "") == mine.string() && r.body.value("name", "") == "mine");
    {
        Request q;
        q.method = "GET";
        q.path = "/api/v1/scenario";
        q.query["deck"] = "EMU1";
        const Reply s = api.Dispatch(q);
        CHECK(s.status == 200 && s.body.value("file", "") == mine.string() && s.body["document"]["name"] == "mine");
    }

    // A broken file is refused and the deck keeps what it plays; a missing file parameter is a 400.
    r = Call(api, "PUT", "/api/v1/decks/EMU1/scenario", {{"file", broken.string()}});
    CHECK_EQ(r.status, 422);
    CHECK(own.Playing() == "mine");
    CHECK_EQ(Call(api, "PUT", "/api/v1/decks/EMU1/scenario", json::object()).status, 400);

    // Watched: an edit of the file reloads that deck at the next pump.
    WriteScenario(mine, "mine2");
    Touch(mine);
    decks.Pump(std::chrono::milliseconds(0));
    CHECK(own.Playing() == "mine2");
    decks.Pump(std::chrono::milliseconds(0));  // nothing changed since: no second load
    CHECK(own.Playing() == "mine2");

    // The start scenario back.
    r = Call(api, "DELETE", "/api/v1/decks/EMU1/scenario");
    CHECK(r.status == 200 && r.body.value("start_scenario", false) == true);
    CHECK(own.Playing() == "idle");

    // A runtime asks for another file (nav to scenario): done by the pump, off the runtime's thread.
    decks.RequestLoad("SER1", mine.string());
    decks.Pump(std::chrono::milliseconds(0));
    CHECK(real.Playing() == "mine2");
    CHECK(own.Playing() == "idle");

    // Reload of the deck's current file through the old route (?deck=, default the first).
    WriteScenario(mine, "mine3");
    r = Call(api, "POST", "/api/v1/reload");
    CHECK(r.status == 200 && r.body.value("deck", "") == "SER1");
    CHECK(real.Playing() == "mine3");

    // MCP: the same routes through invoke_api, answered in the text an agent reads.
    const json mcp = api.Mcp({{"jsonrpc", "2.0"},
                              {"id", 1},
                              {"method", "tools/call"},
                              {"params", {{"name", "invoke_api"}, {"arguments", {{"path", "/api/v1/decks"}}}}}});
    const std::string text = mcp["result"]["content"][0].value("text", "");
    CHECK(text.find("SER1") != std::string::npos && text.find("EMU1") != std::string::npos);
    const json args{{"method", "PUT"}, {"path", "/api/v1/decks/SER1/brightness"}, {"body", {{"value", 40}}}};
    const json put = api.Mcp({{"jsonrpc", "2.0"},
                              {"id", 2},
                              {"method", "tools/call"},
                              {"params", {{"name", "invoke_api"}, {"arguments", args}}}});
    CHECK(put["result"].value("isError", true) == false && real.rt.Brightness() == 40);
    const json found = api.Search("deck brightness");
    CHECK(found.value("total_matches", 0) >= 2);

    // Snapshot without a hub: refused, not crashed.
    CHECK_EQ(Call(api, "POST", "/api/v1/decks/SER1/snapshot", {{"path", "/tmp/x.png"}}).status, 409);

    // A removed deck is gone from every route.
    decks.Remove("EMU1");
    CHECK_EQ(Call(api, "GET", "/api/v1/decks/EMU1").status, 404);
    CHECK_EQ(Call(api, "GET", "/api/v1/decks").body["decks"].size(), 1u);
    decks.Remove("SER1");
    CHECK_EQ(Call(api, "GET", "/api/v1/page").status, 404);

    // A start file: decks begin on it; --watch reloads every deck that plays it.
    DaemonDecks::Options o;
    o.startFile = mine.string();
    o.watchStart = true;
    DaemonDecks started(o);
    CHECK(started.LoadStart(&err));
    TestDeck a("plusxl", "S1", started.StartScenario());
    started.Add("S1", a.dev.GetModel(), true, a.rt);
    CHECK(a.Playing() == "mine3");
    WriteScenario(mine, "mine4");
    Touch(mine);
    started.Pump(std::chrono::milliseconds(0));
    CHECK(a.Playing() == "mine4");
    DaemonDecks bad(DaemonDecks::Options{broken.string()});
    CHECK(!bad.LoadStart(&err) && !err.empty());
    started.Remove("S1");

    std::error_code ec;
    fs::remove_all(dir, ec);
}
