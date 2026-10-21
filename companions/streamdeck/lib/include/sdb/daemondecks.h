// The logical decks a daemon plays (docs/inprogress/2026-10-19-streamdeck-integration/daemon.md §3, §10): the real
// deck and the viewers' own decks, each with its runtime, its scenario file and its brightness. Serves their
// automation routes (REST, and MCP through the same automation::Api) and watches their scenario files.
//
//   GET  /api/v1/health                       ready, version, decks, viewers
//   GET  /api/v1/decks                        every logical deck: serial, model, real / own, connected, scenario,
//                                             brightness, viewers
//   GET  /api/v1/decks/{serial}               one deck ("first" = the first real deck, else the first deck)
//   GET  /api/v1/decks/{serial}/brightness    PUT {value} | {delta}
//   GET  /api/v1/decks/{serial}/scenario      PUT {file, watch} plays another file on that deck only; DELETE gives
//                                             the deck the daemon's start scenario back
//   POST /api/v1/decks/{serial}/snapshot      {path}: the deck's viewers save a PNG there (device wire Control)
//   POST /api/v1/decks/{serial}/reconnect     {away_ms}: the real deck's USB handle closed and opened again
//   GET  /api/v1/state, /page, /nodes; POST /navigate, /press, /reload; GET /scenario   the runtime of one deck
//                                             (?deck=serial, default the first)
//
// Threads: Add / Remove come from the device and network threads, the routes run on the automation's thread, Pump
// on the daemon's watcher thread. A removed deck's runtime is never touched again (Remove waits for a route that
// uses it).
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "sdb/automation.h"
#include "sdb/models.h"
#include "sdb/scenario.h"

namespace sdb
{

class DeckHub;
namespace scn
{
class Runtime;
}

class DaemonDecks
{
public:
    struct Options
    {
        std::string startFile;       // the daemon's start scenario ("" = the built-in idle one)
        bool watchStart = false;     // reload decks playing it when it (or an include) changes (--watch)
        std::string version = "1.0";
        int applyTimeoutMs = 1000;  // /press, /navigate: the longest wait for the device thread's step (then 202)
    };
    using Log = std::function<void(const std::string& line)>;

    explicit DaemonDecks(Options options);
    DaemonDecks(const DaemonDecks&) = delete;
    DaemonDecks& operator=(const DaemonDecks&) = delete;

    // A scenario file, includes resolved and validated; warnings go to `log`, `files` gets every file read.
    [[nodiscard]] static std::optional<scn::Scenario> LoadFile(const std::string& file, std::string& error,
                                                               std::vector<std::string>* files = nullptr,
                                                               const Log& log = {});
    // The scenario played when none is given: the UnrealNG look laid out for `model` (null = the Plus XL) from the
    // data directory's theme, or a drawn title without one (sdb/idlescenario.h).
    [[nodiscard]] static nlohmann::json IdleDocument(const Model* model);
    [[nodiscard]] static scn::Scenario Idle(const Model* model = nullptr);

    // Loads the start scenario (once, before the decks come).
    bool LoadStart(std::string* error);
    // A copy of the start scenario for a new deck of `model` (the idle one is laid out for it).
    [[nodiscard]] scn::Scenario StartScenario(const Model* model = nullptr) const;
    [[nodiscard]] const std::string& StartFile() const
    {
        return _options.startFile;
    }

    // A deck's runtime: added once it exists, removed before it is destroyed. The deck starts on the start scenario.
    void Add(const std::string& serial, const Model& model, bool real, scn::Runtime& runtime);
    void Remove(const std::string& serial);
    // viewers, connected state, Control frames (null = none); port: where viewers join (GET /api/v1/health)
    void SetHub(DeckHub* hub, int port = 0);

    // Plays `file` on that deck ("" = the start scenario again). Thread-safe; the runtime applies it on its next
    // step with the menu stack kept. "first" names the first real deck.
    bool LoadScenario(const std::string& serial, const std::string& file, bool watch, std::string* error);
    // From a runtime (nav {to: scenario}): the load is done by Pump, off the runtime's thread.
    void RequestLoad(const std::string& serial, const std::string& file);
    // The watcher: waits up to `wait` for a requested load, does the requested loads, then reloads every watched
    // deck whose scenario (or an include of it) changed since it was loaded.
    void Pump(std::chrono::milliseconds wait);
    void Wake();  // ends a Pump that waits (shutdown)

    [[nodiscard]] nlohmann::json List() const;
    [[nodiscard]] std::size_t Count() const;
    // The serial "first" (or "") stands for: the first real deck, else the first deck; empty when there is none.
    [[nodiscard]] std::string Resolve(const std::string& serial) const;

    void AddRoutes(automation::Api& api);

    Log log;  // loads, reloads and their errors (the daemon prints them)

private:
    using Clock = std::filesystem::file_time_type;
    struct Entry
    {
        std::string serial;
        const Model* model = nullptr;
        bool real = true;
        scn::Runtime* runtime = nullptr;
        std::string file;  // "" = the built-in idle scenario
        bool watch = false;
        bool start = true;                     // plays the start scenario
        std::map<std::string, Clock> watched;  // the file and its includes, as last loaded
        std::uint64_t order = 0;
    };
    [[nodiscard]] std::string ResolveLocked(const std::string& serial) const;
    [[nodiscard]] nlohmann::json DescribeLocked(const Entry& e) const;
    void Say(const std::string& line) const;
    static std::map<std::string, Clock> Stamp(const std::vector<std::string>& files);
    void Restamp(const std::string& serial);
    // Runs fn(entry) under the lock for the deck the request names; 404 when there is none.
    automation::Reply WithDeck(const std::string& serial, const std::function<automation::Reply(Entry&)>& fn);

    Options _options;
    std::optional<scn::Scenario> _start;
    std::vector<std::string> _startFiles;
    mutable std::mutex _mutex;  // _decks, _hub, _nextOrder
    std::map<std::string, Entry> _decks;
    DeckHub* _hub = nullptr;
    int _wirePort = 0;
    std::uint64_t _nextOrder = 0;

    std::mutex _requestMutex;  // separate: a runtime asks while it holds its own lock
    std::condition_variable _requested;
    std::deque<std::pair<std::string, std::string>> _requests;
    bool _wake = false;
};

}  // namespace sdb
