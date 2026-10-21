// unreal-streamdeck run [scenario: .json .yaml .csv .tsv] [--watch] [--api PORT] [--host URL] [--wait] [--dry-run]
// [--seconds N] [--device S | emu[:addr]] [--mirror [addr]] [--viewers [PORT] | --no-viewers] [--with-parent]
// Plays a scenario (menus + actions, docs/inprogress/2026-10-19-streamdeck-integration/scenarios.md) on the deck.
//   (no scenario) the built-in idle scenario: the UnrealNG look for the deck's model (sdb/idlescenario.h), until a
//               scenario is loaded for a deck
//   --viewers   Stream Deck Studio windows join on 127.0.0.1:PORT (device wire v2, default 37050, on by default):
//               mirrors of the deck, or decks of their own with their own runtime
//   --wait      no deck attached: serve the viewers (own decks work, mirrors wait) and take the first USB deck that
//               appears
//   The USB deck may be unplugged and plugged back in: the runtime and the viewers stay, the deck gets its picture
//   back (sdb/reconnect.h). Another deck plugged in instead (after 3 s) gets a runtime of its own.
//   --watch     reload the file when it changes (the menu stack is kept)
//   --api PORT  automation planes on 127.0.0.1:PORT: WebAPI (/api/v1/decks, state, navigate, press, reload,
//               scenario, brightness) + MCP (/mcp); sdb/daemondecks.h
//   --host URL  the unreal-ng WebAPI the scenarios' actions go to (default http://127.0.0.1:8090; a v1 scenario's
//               settings.webapi still wins for that scenario)
//   --dry-run   log actions instead of running them
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "cli.h"
#include "sdb/automation.h"
#include "sdb/catalogue.h"
#include "sdb/daemondecks.h"
#include "sdb/datadir.h"
#include "sdb/deckhub.h"
#include "sdb/devicewire.h"
#include "sdb/eventloop.h"
#include "sdb/runtime.h"
#include "sdb/scenarioio.h"
#include "sdb/scenarioops.h"

using namespace sdb;
using namespace sdb::scn;

namespace cli
{

namespace
{
std::atomic<bool> runStop{false};
std::atomic<bool> deckStop{false};  // the real deck's runtime ends: the run ends, or another deck takes its place
void StopRun()
{
    runStop = true;
    deckStop = true;
}
void OnRunSig(int)
{
    StopRun();
}

// A viewer's deck of its own: a device on the viewer, a runtime with the same scenario, its thread.
struct OwnDeck
{
    std::unique_ptr<Device> dev;
    std::unique_ptr<Runtime> rt;
    std::atomic<bool> stop{false};
    std::thread thread;
};

// The deck to play on, or null. quiet: no error lines (--wait polls). mirror: an older Stream Deck Studio listening
// on the wire port may get a copy (only when this daemon's hub does not listen there itself). A USB deck reconnects
// after an unplug (sdb/reconnect.h); another deck there instead ends this one's runtime (deckStop).
std::unique_ptr<Device> TryOpen(const Args& a, bool quiet, bool mirror)
{
    Args sel = a;
    if (!mirror && !sel.Has("mirror"))
        sel.opt["no-mirror"] = "1";
    const bool emulator = sel.Str("device").rfind("emu", 0) == 0;
    if (quiet && (emulator || (sel.Has("emulate") && !FindModelByName(sel.Str("emulate")))))
        return nullptr;  // a studio address is not polled; a bad --emulate was said once already
    std::string err = "no matching device";
    std::unique_ptr<Device> dev;
    if (auto f = FindTarget(sel))
    {
        if (!emulator)
        {
            ReconnectOptions ro;
            ro.filter = sel.Str("device");
            ro.onChange = [serial = f->info.serial](bool connected, const std::string& why) {
                if (connected)
                    std::printf("deck %s is back: picture restored\n", serial.c_str());
                else
                    std::printf("deck %s unplugged (%s): the runtime and the viewers wait for it\n", serial.c_str(),
                                why.c_str());
                std::fflush(stdout);
            };
            ro.onOtherDeck = [serial = f->info.serial](const std::string& other) {
                std::printf("deck %s stays away, %s is here: it gets a runtime of its own\n", serial.c_str(),
                            other.c_str());
                std::fflush(stdout);
                deckStop = true;
            };
            f->reconnect = std::move(ro);
        }
        dev = OpenDevice(*f, &err);
    }
    if (!dev && !quiet)
    {
        std::fprintf(stderr, "error: cannot open device (%s)\n", err.c_str());
        std::fprintf(
            stderr,
            "hint: quit other Stream Deck software (the Elgato app) first; it holds the device. Run `sdctl info`.\n");
    }
    return dev;
}
}  // namespace

int CmdRun(const Args& a)
{
    if (a.Has("help"))
    {
        std::fprintf(stderr,
                     "usage: unreal-streamdeck run [scenario: .json .yaml .csv .tsv] [--watch] [--api PORT] "
                     "[--host URL] [--wait] [--dry-run] [--seconds N] [--device S|emu] [--mirror] "
                     "[--viewers [PORT] | --no-viewers] [--with-parent]\n");
        return kBadUsage;
    }
    if (a.Has("host"))
    {  // before any scenario is parsed: a document without settings.webapi takes it
        std::string host = a.Str("host");
        if (host.find("://") == std::string::npos)
            host = "http://" + host;
        while (host.size() > 8 && host.back() == '/')
            host.pop_back();
        SetDefaultHost(host);
        std::printf("host: %s\n", host.c_str());
    }
    DaemonDecks::Options dopt;
    dopt.startFile = a.pos.size() >= 2 ? a.pos[1] : std::string();
    dopt.watchStart = a.Has("watch");
    DaemonDecks decks(dopt);
    decks.log = [](const std::string& line) {
        std::fprintf(stderr, "%s\n", line.c_str());
        std::fflush(stderr);
    };
    std::string err;
    if (!decks.LoadStart(&err))
    {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return kBadContent;
    }
    // $data of the scenarios and the idle screen's theme
    if (const auto root = data::Root(); !root.empty())
        std::printf("data: %s\n", root.string().c_str());
    else
        std::printf("note: no data directory (streamdeck/ not found; %s names one): $data paths do not load\n",
                    data::kEnv);
    const bool emulatorDevice = a.Str("device").rfind("emu", 0) == 0;
    const bool wait = a.Has("wait") && !emulatorDevice;  // a studio address is opened once, not polled
    const bool viewersWanted = !a.Has("no-viewers") && !emulatorDevice;
    // Without --wait a missing deck ends the run at once (before anything listens). The default mirror is off when
    // this daemon will serve the wire port itself.
    std::unique_ptr<Device> dev = TryOpen(a, wait, !viewersWanted);
    if (!dev && !wait)
        return kNoDevice;

    RuntimeOptions opt;
    opt.dryRun = a.Has("dry-run");

    // The device wire v2 on its network loop: viewers mirror the deck or get a deck of their own
    // (device-wire.md). A port taken by an older Stream Deck Studio (which listens itself) leaves it off.
    net::EventLoop netLoop;
    std::unique_ptr<DeckHub> hub;
    std::thread netThread;
    std::mutex ownMu;
    std::map<std::string, std::unique_ptr<OwnDeck>> owns;
    std::vector<std::thread> reapers;  // each takes an ended own deck apart (its runtime joins its threads)
    if (viewersWanted)
    {
        DeckHub::Options ho;
        ho.port =
            a.Has("viewers") && a.Str("viewers") != "1" ? std::atoi(a.Str("viewers").c_str()) : wire::kDefaultPort;
        ho.daemonVersion = "1.0";
        hub = std::make_unique<DeckHub>(netLoop, ho);
        hub->onOwnDeck = [&](std::unique_ptr<Transport> t, const Model& m, const std::string& serial) {
            auto own = std::make_unique<OwnDeck>();
            own->dev = std::make_unique<Device>(m, std::move(t), serial);
            own->rt = std::make_unique<Runtime>(*own->dev, decks.StartScenario(&m), nullptr, opt);
            own->rt->onSwitchScenario = [&decks, serial](const std::string& f) { decks.RequestLoad(serial, f); };
            decks.Add(serial, m, false, *own->rt);
            OwnDeck* p = own.get();
            own->thread = std::thread([p] { p->rt->Run(p->stop); });
            std::printf("viewer %s: own %s\n", serial.c_str(), std::string(m.name).c_str());
            std::fflush(stdout);
            std::lock_guard l(ownMu);
            owns[serial] = std::move(own);
        };
        hub->onOwnDeckGone = [&](const std::string& serial) {
            decks.Remove(serial);  // no route reaches its runtime from here on
            std::printf("viewer %s: own deck gone\n", serial.c_str());
            std::fflush(stdout);
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
            std::printf("viewers: 127.0.0.1:%d (device wire v2)\n", hub->Port());
            decks.SetHub(hub.get(), hub->Port());
            netThread = std::thread([&] { netLoop.Run(); });
        }
        else
        {
            std::printf("note: no viewers (%s; an older Stream Deck Studio listens there?)\n", herr.c_str());
            hub.reset();
        }
    }

    std::signal(SIGINT, OnRunSig);
    std::signal(SIGTERM, OnRunSig);

    // automation planes: the decks' routes (sdb/daemondecks.h)
    automation::Api api(
        "unreal-streamdeck", "1.0",
        "The Stream Deck daemon: it plays a scenario (menus and actions) on every logical deck (the real one, and "
        "decks of their own for Stream Deck Studio windows). GET /api/v1/decks lists them; per deck: brightness, "
        "scenario; GET /api/v1/state shows the menu stack and what each key does; press keys by node id, navigate, "
        "reload (?deck=serial, default the first deck).");
    decks.AddRoutes(api);
    automation::Server server;
    int rc = kOk;
    if (a.Has("api"))
    {
        const int port = a.Num("api", 37053);
        if (!server.Start(api, "127.0.0.1", port, &err))
        {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            rc = kDeviceError;
            StopRun();
        }
        else
            std::printf("automation: http://127.0.0.1:%d/api/v1/decks, MCP http://127.0.0.1:%d/mcp\n", server.Port(),
                        server.Port());
    }

    // the watcher: requested loads (nav to a scenario) and changed files of watched decks
    std::thread watcher([&] {
        while (!runStop)
            decks.Pump(std::chrono::milliseconds(250));
    });
    if (const double secs = a.Dbl("seconds", 0); secs > 0)
        std::thread([secs] {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(secs * 1000)));
            StopRun();
        }).detach();
#ifndef _WIN32
    // --with-parent: started by an app (Stream Deck Studio, unreal-qt); when it goes away, so does this run, not
    // holding the deck
    if (a.Has("with-parent"))
        std::thread([parent = getppid()] {
            while (!runStop && getppid() == parent)
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            StopRun();
        }).detach();
#endif

    // The real deck: one runtime per deck. An unplug is handled underneath (the runtime goes on); the loop turns only
    // when another deck takes the place of the one that stayed away.
    while (!runStop)
    {
        if (!dev)
        {
            dev = TryOpen(a, true, !hub);
            if (!dev)
            {
                std::printf("waiting for a Stream Deck (viewers%s are served meanwhile)\n", hub ? "" : " are not");
                std::fflush(stdout);
            }
            for (int tick = 0; !dev && !runStop; ++tick)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (tick % 10 == 9)
                    dev = TryOpen(a, true, !hub);
            }
            if (!dev)
                break;
        }
        const Scenario start = decks.StartScenario(&dev->GetModel());
        if (!decks.StartFile().empty() && &dev->GetModel() != start.model)
            std::printf("note: scenario made for %s, playing on %s (keys outside the grid are not shown)\n",
                        std::string(start.model->name).c_str(), std::string(dev->GetModel().name).c_str());
        if (hub)
            dev->WrapTransport([&](std::unique_ptr<Transport> t) {
                return hub->AttachDeck(std::move(t), dev->GetModel(), dev->Serial());
            });
        const std::string serial = dev->Serial();
        {
            Runtime rt(*dev, start, nullptr, opt);
            rt.onSwitchScenario = [&decks, serial](const std::string& f) { decks.RequestLoad(serial, f); };
            decks.Add(serial, dev->GetModel(), true, rt);
            std::printf("playing %s on %s %s%s (Ctrl-C to stop)\n",
                        decks.StartFile().empty() ? "the idle scenario" : decks.StartFile().c_str(),
                        std::string(dev->GetModel().name).c_str(), serial.c_str(), opt.dryRun ? ", dry run" : "");
            std::fflush(stdout);
            deckStop = false;
            if (runStop)
                deckStop = true;  // a stop between the loop's check and the reset above
            rt.Run(deckStop);
            decks.Remove(serial);
        }
        dev.reset();  // its viewers are told; mirrors of the first deck follow the next one
    }
    decks.Wake();
    watcher.join();
    server.Stop();
    if (netThread.joinable())
    {
        netLoop.Stop();
        netThread.join();
    }
    hub.reset();
    {
        std::lock_guard l(ownMu);
        for (auto& [serial, own] : owns)
        {
            decks.Remove(serial);
            own->stop = true;
            if (own->thread.joinable())
                own->thread.join();
        }
        owns.clear();
    }
    for (auto& reaper : reapers)
        reaper.join();
    dev.reset();
    std::printf("stopped\n");
    return rc;
}

// unreal-streamdeck scenario check <file> [--catalogue FILE] | convert <in> <out> | upgrade <in> <out>
// | extract <in> <node> <out> | resolve <in> <out>
int CmdScenario(const Args& a)
{
    const std::string sub = a.pos.size() > 1 ? a.pos[1] : "";
    const size_t need = sub == "check" ? 3 : sub == "extract" ? 5 : 4;
    if ((sub != "check" && sub != "convert" && sub != "upgrade" && sub != "extract" && sub != "resolve") ||
        a.pos.size() < need)
    {
        std::fprintf(
            stderr,
            "usage: unreal-streamdeck scenario check <file> [--catalogue FILE] | convert <in> <out> | upgrade <in> "
            "<out> | extract <in> <node-id> <out> | resolve <in> <out>\n"
            "  (.json .yaml .yml .csv .tsv; convert = another file format, upgrade = format v1 -> v2, resolve = "
            "inline every include; check warns about action ids the catalogue lacks, default "
            "$data/streamdeck/catalogue.json)\n");
        return kBadUsage;
    }
    std::string err;
    if (sub == "upgrade")
    {
        auto doc = io::LoadFile(a.pos[2], &err);
        if (!doc)
        {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return kBadContent;
        }
        std::vector<std::string> notes;
        const json v2 = ConvertToV2(*doc, &notes);
        for (const auto& n : notes)
            std::printf("%s\n", n.c_str());
        if (!io::SaveFile(a.pos[3], v2, &err))
        {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return kDeviceError;
        }
        std::printf("wrote %s (version 2)\n", a.pos[3].c_str());
        return kOk;
    }
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
    if (s && sub == "check")
    {
        const std::string file = a.Has("catalogue") ? a.Str("catalogue") : cat::DefaultFile().string();
        if (auto c = file.empty() ? std::nullopt : cat::Load(file))
            for (auto& i : cat::Check(*s, *c))
                issues.push_back(std::move(i));
    }
    for (const auto& i : issues)
        std::printf("%s %s: %s\n", i.error ? "error" : "warning", i.node.empty() ? "(scenario)" : i.node.c_str(),
                    i.message.c_str());
    if (sub == "check")
    {
        if (s)
            std::printf("ok: %s, %s\n", s->name.c_str(), std::string(s->model->name).c_str());
        return s ? kOk : kBadContent;
    }
    if (doc->contains("root"))
        io::StripIncludeMarks((*doc)["root"]);  // a document of its own: no absolute paths of the files it came from
    if (!io::SaveFile(a.pos[3], *doc, &err))
    {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return kDeviceError;
    }
    std::printf("wrote %s\n", a.pos[3].c_str());
    return kOk;
}

}  // namespace cli
