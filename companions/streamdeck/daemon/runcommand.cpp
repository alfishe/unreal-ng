// unreal-streamdeck run <scenario: .json .yaml .csv .tsv> [--watch] [--api PORT] [--dry-run] [--seconds N] [--device S
// | emu[:addr]] [--mirror [addr]] [--viewers [PORT] | --no-viewers] Plays a scenario (menus + actions,
// docs/inprogress/2026-10-19-streamdeck-integration/scenarios.md) on the deck.
//   --viewers   Stream Deck Studio windows join on 127.0.0.1:PORT (device wire v2, default 37050, on by default):
//               mirrors of the deck, or decks of their own with their own runtime
//   --watch     reload the file when it changes (the menu stack is kept)
//   --api PORT  automation planes on 127.0.0.1:PORT: WebAPI (/api/v1/state, navigate, press, reload, scenario) + MCP
//   (/mcp)
//   --dry-run   log actions instead of running them
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "cli.h"
#include "sdb/automation.h"
#include "sdb/deckhub.h"
#include "sdb/devicewire.h"
#include "sdb/eventloop.h"
#include "sdb/runtime.h"
#include "sdb/scenarioio.h"
#include "sdb/scenarioops.h"

using namespace sdb;
using namespace sdb::scn;
namespace fs = std::filesystem;

namespace cli
{

namespace
{
std::atomic<bool> runStop{false};
void OnRunSig(int)
{
    runStop = true;
}

std::vector<std::string> watchedFiles;  // the scenario file and every file it includes (watched)
std::mutex watchedFilesMutex;

// A viewer's deck of its own: a device on the viewer, a runtime with the same scenario, its thread.
struct OwnDeck
{
    std::unique_ptr<Device> dev;
    std::unique_ptr<Runtime> rt;
    std::atomic<bool> stop{false};
    std::thread thread;
};

std::optional<Scenario> LoadScenario(const std::string& file, std::string& err)
{
    std::vector<std::string> files;
    auto doc = io::LoadResolved(file, &err, &files);  // .json .yaml .yml .csv .tsv, includes resolved
    if (doc)
    {
        std::lock_guard l(watchedFilesMutex);
        watchedFiles = files;
    }
    if (!doc)
        return std::nullopt;
    std::vector<Issue> issues;
    auto s = Parse(*doc, issues);
    for (const auto& i : issues)
        if (!i.error)
            std::fprintf(stderr, "warning: %s: %s\n", i.node.c_str(), i.message.c_str());
    if (!s)
    {
        err.clear();
        for (const auto& i : issues)
            if (i.error)
                err += (err.empty() ? "" : "; ") + (i.node.empty() ? i.message : i.node + ": " + i.message);
    }
    return s;
}

fs::file_time_type Mtime(const std::string& f)
{
    std::error_code ec;
    auto t = fs::last_write_time(f, ec);
    return ec ? fs::file_time_type{} : t;
}
}  // namespace

int CmdRun(const Args& a)
{
    if (a.pos.size() < 2)
    {
        std::fprintf(stderr,
                     "usage: unreal-streamdeck run <scenario: .json .yaml .csv .tsv> [--watch] [--api PORT] "
                     "[--dry-run] [--seconds N] [--device S|emu] [--mirror] [--viewers [PORT] | --no-viewers]\n");
        return kBadUsage;
    }
    std::string file = a.pos[1], err;
    auto scenario = LoadScenario(file, err);
    if (!scenario)
    {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return kBadContent;
    }
    auto dev = OpenOrExplain(a);
    if (!dev)
        return kNoDevice;
    if (&dev->GetModel() != scenario->model)
        std::printf("note: scenario made for %s, playing on %s (keys outside the grid are not shown)\n",
                    std::string(scenario->model->name).c_str(), std::string(dev->GetModel().name).c_str());

    RuntimeOptions opt;
    opt.dryRun = a.Has("dry-run");
    const Scenario forOwnDecks = *scenario;

    // The device wire v2 on its network loop: viewers mirror the deck or get a deck of their own
    // (device-wire.md). A port taken by an older Stream Deck Studio (which listens itself) leaves it off.
    net::EventLoop netLoop;
    std::unique_ptr<DeckHub> hub;
    std::thread netThread;
    std::mutex ownMu;
    std::map<std::string, std::unique_ptr<OwnDeck>> owns;
    std::vector<std::thread> reapers;  // each takes an ended own deck apart (its runtime joins its threads)
    const bool emulatorDevice = a.Str("device").rfind("emu", 0) == 0;
    if (!a.Has("no-viewers") && !emulatorDevice)
    {
        DeckHub::Options ho;
        ho.port =
            a.Has("viewers") && a.Str("viewers") != "1" ? std::atoi(a.Str("viewers").c_str()) : wire::kDefaultPort;
        ho.daemonVersion = "1.0";
        hub = std::make_unique<DeckHub>(netLoop, ho);
        hub->onOwnDeck = [&](std::unique_ptr<Transport> t, const Model& m, const std::string& serial) {
            auto own = std::make_unique<OwnDeck>();
            own->dev = std::make_unique<Device>(m, std::move(t), serial);
            own->rt = std::make_unique<Runtime>(*own->dev, forOwnDecks, nullptr, opt);
            OwnDeck* p = own.get();
            own->thread = std::thread([p] { p->rt->Run(p->stop); });
            std::printf("viewer %s: own %s\n", serial.c_str(), std::string(m.name).c_str());
            std::lock_guard l(ownMu);
            owns[serial] = std::move(own);
        };
        hub->onOwnDeckGone = [&](const std::string& serial) {
            std::lock_guard l(ownMu);
            if (auto it = owns.find(serial); it != owns.end())
            {
                it->second->stop = true;  // its runtime stops within a step; taken apart off the network loop
                reapers.emplace_back([own = std::move(it->second)]() mutable {
                    if (own->thread.joinable())
                        own->thread.join();
                    own.reset();
                });
                owns.erase(it);
            }
        };
        std::string herr;
        if (hub->Listen(&herr))
        {
            dev->WrapTransport([&](std::unique_ptr<Transport> t) {
                return hub->AttachDeck(std::move(t), dev->GetModel(), dev->Serial());
            });
            std::printf("viewers: 127.0.0.1:%d (device wire v2)\n", hub->Port());
            netThread = std::thread([&] { netLoop.Run(); });
        }
        else
        {
            std::printf("note: no viewers (%s; an older Stream Deck Studio listens there?)\n", herr.c_str());
            hub.reset();
        }
    }

    Runtime rt(*dev, std::move(*scenario), nullptr, opt);
    std::mutex fileMu;
    auto reloadFrom = [&](const std::string& f) {
        std::string e;
        if (auto s = LoadScenario(f, e))
        {
            {
                std::lock_guard l(fileMu);
                file = f;
            }
            rt.Reload(std::move(*s));
            std::printf("loaded %s\n", f.c_str());
            return std::string();
        }
        std::fprintf(stderr, "scenario error (kept the previous one): %s\n", e.c_str());
        dev->Note("scenario error: " + e);
        return e;
    };
    rt.onSwitchScenario = [&](const std::string& f) { std::thread([&, f] { reloadFrom(f); }).detach(); };

    std::signal(SIGINT, OnRunSig);
    std::signal(SIGTERM, OnRunSig);

    // automation planes
    automation::Api api(
        "unreal-streamdeck", "1.0",
        "A Stream Deck playing a scenario (menus and actions). GET /api/v1/state shows the menu stack and what "
        "each key does; press keys by node id, navigate, reload.");
    using automation::Reply;
    using automation::Request;
    api.Add({"GET",
             "/api/v1/state",
             "Runtime state: menu stack, page (key -> node), breadcrumbs, toggles, last actions",
             "",
             {"runtime"},
             {},
             nullptr,
             [&](const Request&) { return Reply::Ok(rt.State()); }});
    api.Add({"GET",
             "/api/v1/page",
             "What every key, dial and the strip mean now (JSON, no screenshot needed)",
             "",
             {"runtime", "page", "keys"},
             {},
             nullptr,
             [&](const Request&) { return Reply::Ok(rt.CurrentPage()); }});
    api.Add({"GET",
             "/api/v1/nodes",
             "Nodes of the scenario, filtered by tag / type / kind / menu / text",
             "",
             {"scenario", "nodes", "tags"},
             {{"tag", "query", "string", "only nodes with this tag"},
              {"type", "query", "string", "menu | action", false, {"menu", "action"}},
              {"kind", "query", "string", "action kind (webapi, multi, nav, ...)"},
              {"menu", "query", "string", "direct children of this menu"},
              {"text", "query", "string", "substring of id / title"}},
             nullptr,
             [&](const Request& r) {
                 json f = json::object();
                 for (const char* k : {"tag", "type", "kind", "menu", "text"})
                     if (r.Has(k))
                         f[k] = r.Str(k);
                 return Reply::Ok({{"nodes", rt.Nodes(f)}});
             }});
    api.Add({"POST",
             "/api/v1/navigate",
             "Navigate: back, home, or to a node (its menu path is pushed)",
             "",
             {"runtime", "navigation"},
             {{"to", "body", "string", "back | home | node", true, {"back", "home", "node"}},
              {"node", "body", "string", "node id for to=node"}},
             json{{"to", "node"}, {"node", "debug"}},
             [&](const Request& r) {
                 rt.Navigate(r.body);
                 std::this_thread::sleep_for(std::chrono::milliseconds(40));  // applied by the device thread
                 return Reply::Ok(rt.State());
             }});
    api.Add({"POST",
             "/api/v1/press",
             "Press a key on the current page, by node id or key index (a menu opens, an action runs)",
             "",
             {"runtime", "input"},
             {{"node", "body", "string", "node id on the current page"}, {"key", "body", "integer", "key index"}},
             json{{"node", "pause"}},
             [&](const Request& r) {
                 if (r.Has("key"))
                 {
                     rt.Inject({EventType::KeyDown, static_cast<int>(r.Num("key"))});
                     rt.Inject({EventType::KeyUp, static_cast<int>(r.Num("key"))});
                 }
                 else
                 {
                     std::string e;
                     if (!rt.PressNode(r.Str("node"), &e))
                         return Reply::Error(404, e);
                 }
                 std::this_thread::sleep_for(std::chrono::milliseconds(60));
                 return Reply::Ok(rt.State());
             }});
    api.Add({"POST",
             "/api/v1/reload",
             "Reload the scenario file (or another file)",
             "",
             {"runtime", "scenario"},
             {{"file", "body", "string", "path; default the current file"}},
             nullptr,
             [&](const Request& r) {
                 std::string f;
                 {
                     std::lock_guard l(fileMu);
                     f = r.Str("file", file);
                 }
                 const std::string e = reloadFrom(f);
                 return e.empty() ? Reply::Ok({{"loaded", f}}) : Reply::Error(422, e);
             }});
    api.Add({"GET",
             "/api/v1/scenario",
             "The scenario file being played",
             "",
             {"scenario"},
             {},
             nullptr,
             [&](const Request&) {
                 std::string f;
                 {
                     std::lock_guard l(fileMu);
                     f = file;
                 }
                 std::string e;
                 auto doc = io::LoadFile(f, &e);
                 return !doc ? Reply::Error(500, e) : Reply::Ok({{"file", f}, {"document", *doc}});
             }});
    automation::Server server;
    if (a.Has("api"))
    {
        const int port = a.Num("api", 37053);
        if (!server.Start(api, "127.0.0.1", port, &err))
        {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return kDeviceError;
        }
        std::printf("automation: http://127.0.0.1:%d/api/v1/state, MCP http://127.0.0.1:%d/mcp\n", server.Port(),
                    server.Port());
    }

    // file watcher
    std::thread watcher;
    if (a.Has("watch"))
    {
        watcher = std::thread([&] {
            // the scenario and its includes: any of them changing reloads the whole tree
            std::map<std::string, fs::file_time_type> seen;
            auto snapshot = [&] {
                std::map<std::string, fs::file_time_type> m;
                std::vector<std::string> fs;
                {
                    std::lock_guard l(watchedFilesMutex);
                    fs = watchedFiles;
                }
                {
                    std::lock_guard l(fileMu);
                    fs.push_back(file);
                }
                for (const auto& f : fs)
                    m[f] = Mtime(f);
                return m;
            };
            seen = snapshot();
            while (!runStop)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                if (auto now = snapshot(); now != seen)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(60));  // let the writer finish
                    std::string f;
                    {
                        std::lock_guard l(fileMu);
                        f = file;
                    }
                    reloadFrom(f);
                    seen = snapshot();
                }
            }
        });
    }
    if (const double secs = a.Dbl("seconds", 0); secs > 0)
        std::thread([secs] {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(secs * 1000)));
            runStop = true;
        }).detach();
#ifndef _WIN32
    // --with-parent: started by an app (Stream Deck Studio, unreal-qt); when it goes away, so does this run, not
    // holding the deck
    if (a.Has("with-parent"))
        std::thread([parent = getppid()] {
            while (!runStop && getppid() == parent)
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            runStop = true;
        }).detach();
#endif

    std::printf("playing %s on %s%s (Ctrl-C to stop)\n", file.c_str(), std::string(dev->GetModel().name).c_str(),
                opt.dryRun ? ", dry run" : "");
    std::fflush(stdout);
    rt.Run(runStop);
    if (watcher.joinable())
        watcher.join();
    server.Stop();
    if (netThread.joinable())
    {
        netLoop.Stop();
        netThread.join();
    }
    hub.reset();
    std::lock_guard l(ownMu);
    for (auto& [serial, own] : owns)
    {
        own->stop = true;
        if (own->thread.joinable())
            own->thread.join();
    }
    owns.clear();
    for (auto& reaper : reapers)
        reaper.join();
    std::printf("stopped\n");
    return kOk;
}

// sdctl scenario check <file> | convert <in> <out> | extract <in> <node> <out> | resolve <in> <out>
int CmdScenario(const Args& a)
{
    const std::string sub = a.pos.size() > 1 ? a.pos[1] : "";
    const size_t need = sub == "check" ? 3 : sub == "extract" ? 5 : 4;
    if ((sub != "check" && sub != "convert" && sub != "extract" && sub != "resolve") || a.pos.size() < need)
    {
        std::fprintf(stderr,
                     "usage: sdctl scenario check <file> | convert <in> <out> | extract <in> <node-id> <out> | resolve "
                     "<in> <out>\n"
                     "  (.json .yaml .yml .csv .tsv; resolve = inline every include)\n");
        return kBadUsage;
    }
    std::string err;
    if (sub == "extract")
    {
        auto doc = io::LoadFile(a.pos[2], &err);
        if (!doc)
        {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return kBadContent;
        }
        std::vector<std::string> outside;
        const json frag = ops::ExtractSubtree(*doc, a.pos[3], &outside);
        if (frag.is_null())
        {
            std::fprintf(stderr, "error: no node '%s'\n", a.pos[3].c_str());
            return kBadContent;
        }
        for (const auto& o : outside)
            std::printf("warning: links to '%s' outside the subtree\n", o.c_str());
        if (!io::SaveFile(a.pos[4], frag, &err))
        {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return kDeviceError;
        }
        std::printf("wrote %s\n", a.pos[4].c_str());
        return kOk;
    }
    auto doc = sub == "convert" ? io::LoadFile(a.pos[2], &err)
                                : io::LoadResolved(a.pos[2], &err);  // convert keeps includes as links
    if (!doc)
    {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return kBadContent;
    }
    if (sub == "convert")
    {  // as written: includes stay links
        if (!io::SaveFile(a.pos[3], *doc, &err))
        {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return kDeviceError;
        }
        std::printf("wrote %s\n", a.pos[3].c_str());
        return kOk;
    }
    std::vector<Issue> issues;
    auto s = Parse(*doc, issues);
    for (const auto& i : issues)
        std::printf("%s %s: %s\n", i.error ? "error" : "warning", i.node.empty() ? "(scenario)" : i.node.c_str(),
                    i.message.c_str());
    if (sub == "check")
    {
        if (s)
            std::printf("ok: %s, %s\n", s->name.c_str(), std::string(s->model->name).c_str());
        return s ? kOk : kBadContent;
    }
    if (!io::SaveFile(a.pos[3], *doc, &err))
    {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return kDeviceError;
    }
    std::printf("wrote %s\n", a.pos[3].c_str());
    return kOk;
}

}  // namespace cli
