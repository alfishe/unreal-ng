#include "sdb/daemondecks.h"

#include <algorithm>
#include <thread>

#include "sdb/deckhub.h"
#include "sdb/runtime.h"
#include "sdb/scenarioio.h"

namespace sdb
{

using json = nlohmann::json;
using automation::Reply;
using automation::Request;

namespace
{
constexpr const char* kIdle = R"({"name": "idle", "model": "plusxl",
  "root": {"id": "home", "type": "menu", "title": "unreal-streamdeck", "items": []}})";

std::string ModelName(const Model* m)
{
    return m ? std::string(m->name) : std::string();
}
}  // namespace

DaemonDecks::DaemonDecks(Options options) : _options(std::move(options)) {}

std::optional<scn::Scenario> DaemonDecks::LoadFile(const std::string& file, std::string& error,
                                                   std::vector<std::string>* files, const Log& log)
{
    std::vector<std::string> read;
    auto doc = scn::io::LoadResolved(file, &error, &read);  // .json .yaml .yml .csv .tsv, includes resolved
    if (files)
        *files = read;
    if (!doc)
        return std::nullopt;
    std::vector<scn::Issue> issues;
    auto s = scn::Parse(*doc, issues);
    for (const auto& i : issues)
        if (!i.error && log)
            log("warning: " + file + ": " + i.node + ": " + i.message);
    if (!s)
    {
        error.clear();
        for (const auto& i : issues)
            if (i.error)
                error += (error.empty() ? "" : "; ") + (i.node.empty() ? i.message : i.node + ": " + i.message);
    }
    return s;
}

scn::Scenario DaemonDecks::Idle()
{
    std::vector<scn::Issue> issues;
    return *scn::Parse(json::parse(kIdle), issues);
}

bool DaemonDecks::LoadStart(std::string* error)
{
    if (_options.startFile.empty())
    {
        _start = Idle();
        return true;
    }
    std::string e;
    _start = LoadFile(_options.startFile, e, &_startFiles, log);
    if (!_start && error)
        *error = e;
    return _start.has_value();
}

scn::Scenario DaemonDecks::StartScenario() const
{
    std::lock_guard lock(_mutex);
    return _start ? *_start : Idle();
}

std::map<std::string, DaemonDecks::Clock> DaemonDecks::Stamp(const std::vector<std::string>& files)
{
    std::map<std::string, Clock> out;
    for (const auto& f : files)
    {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(f, ec);
        out[f] = ec ? Clock{} : t;
    }
    return out;
}

void DaemonDecks::Say(const std::string& line) const
{
    if (log)
        log(line);
}

void DaemonDecks::Add(const std::string& serial, const Model& model, bool real, scn::Runtime& runtime)
{
    std::lock_guard lock(_mutex);
    Entry& e = _decks[serial];
    e.serial = serial;
    e.model = &model;
    e.real = real;
    e.runtime = &runtime;
    e.file = _options.startFile;
    e.watch = _options.watchStart && !_options.startFile.empty();
    e.start = true;
    e.watched = Stamp(_startFiles);
    e.order = _nextOrder++;
}

void DaemonDecks::Remove(const std::string& serial)
{
    std::lock_guard lock(_mutex);
    _decks.erase(serial);
}

void DaemonDecks::SetHub(DeckHub* hub, int port)
{
    std::lock_guard lock(_mutex);
    _hub = hub;
    _wirePort = hub ? port : 0;
}

std::size_t DaemonDecks::Count() const
{
    std::lock_guard lock(_mutex);
    return _decks.size();
}

std::string DaemonDecks::Resolve(const std::string& serial) const
{
    std::lock_guard lock(_mutex);
    return ResolveLocked(serial);
}

std::string DaemonDecks::ResolveLocked(const std::string& serial) const
{
    if (!serial.empty() && serial != "first")
        return _decks.count(serial) ? serial : std::string();
    const Entry* best = nullptr;
    for (const auto& [s, e] : _decks)
        if (!best || (e.real && !best->real) || (e.real == best->real && e.order < best->order))
            best = &e;
    return best ? best->serial : std::string();
}

bool DaemonDecks::LoadScenario(const std::string& serial, const std::string& file, bool watch, std::string* error)
{
    const std::string target = Resolve(serial);
    if (target.empty())
    {
        if (error)
            *error = "no deck '" + serial + "'";
        return false;
    }
    const bool start = file.empty() || file == _options.startFile;
    std::optional<scn::Scenario> s;
    std::vector<std::string> files;
    std::string e;
    if (start)
    {  // the start scenario as the daemon loaded it (its file may have changed since: then read it again)
        s = StartScenario();
        files = _startFiles;
    }
    else
        s = LoadFile(file, e, &files, log);
    if (!s)
    {
        Say("scenario error (deck " + target + " keeps the previous one): " + e);
        if (error)
            *error = e;
        return false;
    }
    std::lock_guard lock(_mutex);
    const auto it = _decks.find(target);
    if (it == _decks.end())
    {
        if (error)
            *error = "deck '" + target + "' went away";
        return false;
    }
    Entry& d = it->second;
    d.file = start ? _options.startFile : file;
    d.start = start;
    d.watch = start ? _options.watchStart && !_options.startFile.empty() : watch;
    d.watched = Stamp(files);
    d.runtime->Reload(std::move(*s));
    Say("deck " + target + " plays " + (d.file.empty() ? std::string("the idle scenario") : d.file));
    return true;
}

void DaemonDecks::RequestLoad(const std::string& serial, const std::string& file)
{
    {
        std::lock_guard lock(_requestMutex);
        _requests.emplace_back(serial, file);
    }
    _requested.notify_all();
}

void DaemonDecks::Wake()
{
    {
        std::lock_guard lock(_requestMutex);
        _wake = true;
    }
    _requested.notify_all();
}

void DaemonDecks::Pump(std::chrono::milliseconds wait)
{
    std::deque<std::pair<std::string, std::string>> requests;
    {
        std::unique_lock lock(_requestMutex);
        _requested.wait_for(lock, wait, [&] { return !_requests.empty() || _wake; });
        requests.swap(_requests);
        _wake = false;
    }
    for (const auto& [serial, file] : requests)
        LoadScenario(serial, file, true, nullptr);

    // Changed files: the start scenario once for every deck that plays it, a deck's own file for that deck.
    struct Due
    {
        std::string serial, file;
        bool start;
        bool watch;
    };
    std::vector<Due> due;
    {
        std::lock_guard lock(_mutex);
        for (const auto& [serial, e] : _decks)
        {
            if (!e.watch)
                continue;
            std::vector<std::string> files;
            for (const auto& [f, t] : e.watched)
                files.push_back(f);
            if (files.empty() && !e.file.empty())
                files.push_back(e.file);
            if (Stamp(files) != e.watched)
                due.push_back({serial, e.file, e.start, e.watch});
        }
    }
    if (due.empty())
        return;
    bool startOk = true;
    if (std::any_of(due.begin(), due.end(), [](const Due& d) { return d.start; }))
    {
        std::string e;
        std::vector<std::string> files;
        if (auto s = LoadFile(_options.startFile, e, &files, log))
        {
            std::lock_guard lock(_mutex);
            _start = std::move(s);
            _startFiles = files;
        }
        else
        {
            Say("scenario error (kept the previous one): " + e);
            startOk = false;
            for (const auto& d : due)
                if (d.start)
                    Restamp(d.serial);
        }
    }
    for (const auto& d : due)
        if ((!d.start || startOk) && !LoadScenario(d.serial, d.start ? std::string() : d.file, d.watch, nullptr))
            Restamp(d.serial);
}

// A broken edit: wait for the next change before trying again.
void DaemonDecks::Restamp(const std::string& serial)
{
    std::lock_guard lock(_mutex);
    const auto it = _decks.find(serial);
    if (it == _decks.end())
        return;
    std::vector<std::string> files;
    for (const auto& [f, t] : it->second.watched)
        files.push_back(f);
    it->second.watched = Stamp(files);
}

json DaemonDecks::DescribeLocked(const Entry& e) const
{
    json j{{"serial", e.serial},
           {"model", ModelName(e.model)},
           {"pid", e.model ? e.model->pid : 0},
           {"real", e.real},
           {"connected", true},
           {"scenario", e.file},
           {"start_scenario", e.start},
           {"watch", e.watch},
           {"brightness", e.runtime->Brightness()},
           {"viewers", json::array()}};
    if (_hub)
        for (const auto& d : _hub->Info().decks)
            if (d.serial == e.serial)
            {
                j["connected"] = d.connected;
                for (const auto& v : d.viewers)
                    j["viewers"].push_back({{"id", v.id}, {"role", v.role}, {"name", v.name}, {"pid", v.pid}});
            }
    return j;
}

json DaemonDecks::List() const
{
    std::lock_guard lock(_mutex);
    std::vector<const Entry*> order;
    for (const auto& [s, e] : _decks)
        order.push_back(&e);
    std::sort(order.begin(), order.end(), [](const Entry* a, const Entry* b) {
        return a->real != b->real ? a->real : a->order < b->order;
    });
    json decks = json::array();
    for (const Entry* e : order)
        decks.push_back(DescribeLocked(*e));
    // decks the hub knows without a runtime here (a real deck between unplug and replug)
    int waiting = 0;
    if (_hub)
    {
        const auto info = _hub->Info();
        waiting = info.waiting;
        for (const auto& d : info.decks)
            if (!_decks.count(d.serial))
            {
                json v = json::array();
                for (const auto& w : d.viewers)
                    v.push_back({{"id", w.id}, {"role", w.role}, {"name", w.name}, {"pid", w.pid}});
                decks.push_back({{"serial", d.serial},
                                 {"model", ModelName(d.model)},
                                 {"pid", d.model ? d.model->pid : 0},
                                 {"real", d.real},
                                 {"connected", d.connected},
                                 {"runtime", false},
                                 {"viewers", v}});
            }
    }
    return {{"decks", decks}, {"waiting_mirrors", waiting}};
}

Reply DaemonDecks::WithDeck(const std::string& serial, const std::function<Reply(Entry&)>& fn)
{
    std::lock_guard lock(_mutex);
    const std::string target = ResolveLocked(serial);
    if (target.empty())
        return Reply::Error(404, serial.empty() || serial == "first" ? "no deck yet" : "no deck '" + serial + "'",
                            "GET /api/v1/decks lists them");
    return fn(_decks.at(target));
}

void DaemonDecks::AddRoutes(automation::Api& api)
{
    const automation::Param deckQuery{"deck", "query", "string", "deck serial (default: the first real deck)"};
    const automation::Param serialPath{"serial", "path", "string", "deck serial, or \"first\"", true};

    api.Add({"GET",
             "/api/v1/health",
             "Ready, version, decks, viewers",
             "",
             {"daemon", "health"},
             {},
             nullptr,
             [this](const Request&) {
                 const json l = List();
                 int port = 0;
                 {
                     std::lock_guard lock(_mutex);
                     port = _wirePort;
                 }
                 int viewers = 0;
                 for (const auto& d : l["decks"])
                     viewers += static_cast<int>(d["viewers"].size());
                 return Reply::Ok({{"ok", true},
                                   {"name", "unreal-streamdeck"},
                                   {"version", _options.version},
                                   {"decks", l["decks"].size()},
                                   {"viewers", viewers},
                                   {"waiting_mirrors", l["waiting_mirrors"]},
                                   {"wire_port", port}});
             }});
    api.Add({"GET",
             "/api/v1/decks",
             "Every logical deck: the real one and the viewers' own decks",
             "serial, model, real / own, connected, scenario file, brightness, viewers",
             {"decks", "devices"},
             {},
             nullptr,
             [this](const Request&) { return Reply::Ok(List()); }});
    api.Add({"GET",
             "/api/v1/decks/{serial}",
             "One deck",
             "",
             {"decks"},
             {serialPath},
             nullptr,
             [this](const Request& r) {
                 return WithDeck(r.Str("serial"), [this](Entry& e) { return Reply::Ok(DescribeLocked(e)); });
             }});
    api.Add({"GET",
             "/api/v1/decks/{serial}/brightness",
             "A deck's brightness (percent)",
             "",
             {"decks", "brightness"},
             {serialPath},
             nullptr,
             [this](const Request& r) {
                 return WithDeck(r.Str("serial"), [](Entry& e) {
                     return Reply::Ok({{"serial", e.serial}, {"brightness", e.runtime->Brightness()}});
                 });
             }});
    api.Add({"PUT",
             "/api/v1/decks/{serial}/brightness",
             "Set a deck's brightness: {value} 0..100, or {delta}",
             "",
             {"decks", "brightness"},
             {serialPath,
              {"value", "body", "integer", "percent, 0..100"},
              {"delta", "body", "integer", "change from the current value"}},
             json{{"value", 60}},
             [this](const Request& r) {
                 if (!r.Has("value") && !r.Has("delta"))
                     return Reply::Error(400, "give 'value' (0..100) or 'delta'");
                 return WithDeck(r.Str("serial"), [&](Entry& e) {
                     const int v = r.Has("value") ? static_cast<int>(r.Num("value"))
                                                  : e.runtime->Brightness() + static_cast<int>(r.Num("delta"));
                     e.runtime->SetBrightness(v);
                     return Reply::Ok({{"serial", e.serial}, {"brightness", e.runtime->Brightness()}});
                 });
             }});
    api.Add({"GET",
             "/api/v1/decks/{serial}/scenario",
             "The scenario a deck plays",
             "file (\"\" = the built-in idle scenario), whether it is the daemon's start scenario, watched; "
             "document=true adds the document",
             {"decks", "scenario"},
             {serialPath, {"document", "query", "boolean", "include the document"}},
             nullptr,
             [this](const Request& r) {
                 return WithDeck(r.Str("serial"), [&](Entry& e) {
                     json j{{"serial", e.serial},
                            {"file", e.file},
                            {"start_scenario", e.start},
                            {"start_file", _options.startFile},
                            {"watch", e.watch},
                            {"name", e.runtime->State().value("scenario", "")}};
                     if (r.Flag("document"))
                     {
                         std::string err;
                         auto doc = e.file.empty() ? std::optional<json>(json::parse(kIdle))
                                                   : scn::io::LoadFile(e.file, &err);
                         j["document"] = doc ? *doc : json();
                         if (!doc)
                             j["error"] = err;
                     }
                     return Reply::Ok(j);
                 });
             }});
    api.Add({"PUT",
             "/api/v1/decks/{serial}/scenario",
             "Play another scenario file on this deck only (the menu stack is kept)",
             "The other decks keep theirs. watch (default true): edits of the file or its includes reload it",
             {"decks", "scenario", "reload"},
             {serialPath,
              {"file", "body", "string", "scenario path (.json .yaml .csv .tsv)", true},
              {"watch", "body", "boolean", "reload on changes (default true)"}},
             json{{"file", "/path/to/my.deck.json"}, {"watch", true}},
             [this](const Request& r) {
                 const std::string serial = Resolve(r.Str("serial"));
                 if (serial.empty())
                     return Reply::Error(404, "no deck '" + r.Str("serial") + "'", "GET /api/v1/decks lists them");
                 std::string err;
                 if (!LoadScenario(serial, r.Str("file"), r.Flag("watch", true), &err))
                     return Reply::Error(422, err);
                 return WithDeck(serial, [this](Entry& e) { return Reply::Ok(DescribeLocked(e)); });
             }});
    api.Add({"DELETE",
             "/api/v1/decks/{serial}/scenario",
             "Give a deck the daemon's start scenario back",
             "",
             {"decks", "scenario"},
             {serialPath},
             nullptr,
             [this](const Request& r) {
                 const std::string serial = Resolve(r.Str("serial"));
                 if (serial.empty())
                     return Reply::Error(404, "no deck '" + r.Str("serial") + "'", "GET /api/v1/decks lists them");
                 std::string err;
                 if (!LoadScenario(serial, {}, false, &err))
                     return Reply::Error(422, err);
                 return WithDeck(serial, [this](Entry& e) { return Reply::Ok(DescribeLocked(e)); });
             }});
    api.Add({"POST",
             "/api/v1/decks/{serial}/snapshot",
             "The deck's viewers (Stream Deck Studio windows) save a PNG of the deck",
             "Sent as a device wire Control frame; answers how many viewers it went to (they save asynchronously)",
             {"decks", "screenshot", "snapshot", "viewers"},
             {serialPath, {"path", "body", "string", "PNG file the viewers write", true}},
             json{{"path", "/tmp/deck.png"}},
             [this](const Request& r) {
                 return WithDeck(r.Str("serial"), [&](Entry& e) {
                     if (!_hub)
                         return Reply::Error(409, "no viewers: the daemon runs with --no-viewers");
                     const int n =
                         _hub->SendControl(e.serial, json{{"op", "snapshot"}, {"path", r.Str("path")}}.dump());
                     return n > 0 ? Reply::Ok({{"serial", e.serial}, {"viewers", n}, {"path", r.Str("path")}})
                                  : Reply::Error(409, "nobody views deck " + e.serial);
                 });
             }});

    // The runtime of one deck (the first by default)
    api.Add({"GET",
             "/api/v1/state",
             "Runtime state: menu stack, page (key -> node), breadcrumbs, toggles, last actions",
             "",
             {"runtime"},
             {deckQuery},
             nullptr,
             [this](const Request& r) {
                 return WithDeck(r.Str("deck"), [](Entry& e) {
                     json s = e.runtime->State();
                     s["deck"] = e.serial;
                     return Reply::Ok(s);
                 });
             }});
    api.Add({"GET",
             "/api/v1/page",
             "What every key, dial and the strip mean now (JSON, no screenshot needed)",
             "",
             {"runtime", "page", "keys"},
             {deckQuery},
             nullptr,
             [this](const Request& r) {
                 return WithDeck(r.Str("deck"), [](Entry& e) { return Reply::Ok(e.runtime->CurrentPage()); });
             }});
    api.Add({"GET",
             "/api/v1/nodes",
             "Nodes of the scenario, filtered by tag / type / kind / menu / text",
             "",
             {"scenario", "nodes", "tags"},
             {deckQuery,
              {"tag", "query", "string", "only nodes with this tag"},
              {"type", "query", "string", "menu | action", false, {"menu", "action"}},
              {"kind", "query", "string", "action kind (webapi, multi, nav, ...)"},
              {"menu", "query", "string", "direct children of this menu"},
              {"text", "query", "string", "substring of id / title"}},
             nullptr,
             [this](const Request& r) {
                 json f = json::object();
                 for (const char* k : {"tag", "type", "kind", "menu", "text"})
                     if (r.Has(k))
                         f[k] = r.Str(k);
                 return WithDeck(r.Str("deck"),
                                 [&](Entry& e) { return Reply::Ok({{"nodes", e.runtime->Nodes(f)}}); });
             }});
    api.Add({"POST",
             "/api/v1/navigate",
             "Navigate: back, home, or to a node (its menu path is pushed)",
             "",
             {"runtime", "navigation"},
             {deckQuery,
              {"to", "body", "string", "back | home | node", true, {"back", "home", "node"}},
              {"node", "body", "string", "node id for to=node"}},
             json{{"to", "node"}, {"node", "debug"}},
             [this](const Request& r) {
                 const std::string deck = r.Str("deck");
                 Reply first = WithDeck(deck, [&](Entry& e) {
                     e.runtime->Navigate(r.body);
                     return Reply::Ok();
                 });
                 if (first.IsError())
                     return first;
                 std::this_thread::sleep_for(std::chrono::milliseconds(40));  // applied by the device thread
                 return WithDeck(deck, [](Entry& e) { return Reply::Ok(e.runtime->State()); });
             }});
    api.Add({"POST",
             "/api/v1/press",
             "Press a key on the current page, by node id or key index (a menu opens, an action runs)",
             "",
             {"runtime", "input"},
             {deckQuery,
              {"node", "body", "string", "node id on the current page"},
              {"key", "body", "integer", "key index"}},
             json{{"node", "pause"}},
             [this](const Request& r) {
                 const std::string deck = r.Str("deck");
                 Reply first = WithDeck(deck, [&](Entry& e) {
                     if (r.Has("key"))
                     {
                         e.runtime->Inject({EventType::KeyDown, static_cast<int>(r.Num("key"))});
                         e.runtime->Inject({EventType::KeyUp, static_cast<int>(r.Num("key"))});
                         return Reply::Ok();
                     }
                     std::string err;
                     return e.runtime->PressNode(r.Str("node"), &err) ? Reply::Ok() : Reply::Error(404, err);
                 });
                 if (first.IsError())
                     return first;
                 std::this_thread::sleep_for(std::chrono::milliseconds(60));
                 return WithDeck(deck, [](Entry& e) { return Reply::Ok(e.runtime->State()); });
             }});
    api.Add({"POST",
             "/api/v1/reload",
             "Reload a deck's scenario file (or play another file on it)",
             "",
             {"runtime", "scenario"},
             {deckQuery, {"file", "body", "string", "path; default the deck's current file"}},
             nullptr,
             [this](const Request& r) {
                 const std::string serial = Resolve(r.Str("deck"));
                 if (serial.empty())
                     return Reply::Error(404, "no deck yet", "GET /api/v1/decks lists them");
                 std::string file;
                 bool watch = true;
                 WithDeck(serial, [&](Entry& e) {
                     file = e.file;
                     watch = e.watch;
                     return Reply::Ok();
                 });
                 const bool other = r.Has("file") && r.Str("file") != file;
                 if (other)
                     file = r.Str("file");
                 else if (file == _options.startFile && !file.empty())
                 {  // the start file itself: read it again (a reload asks for the file as it is now)
                     std::string e;
                     std::vector<std::string> files;
                     auto s = LoadFile(file, e, &files, log);
                     if (!s)
                         return Reply::Error(422, e);
                     std::lock_guard lock(_mutex);
                     _start = std::move(s);
                     _startFiles = files;
                 }
                 std::string err;
                 if (!LoadScenario(serial, file, other ? true : watch, &err))
                     return Reply::Error(422, err);
                 return Reply::Ok({{"loaded", file}, {"deck", serial}});
             }});
    api.Add({"GET",
             "/api/v1/scenario",
             "The scenario file a deck plays, with its document",
             "",
             {"scenario"},
             {deckQuery},
             nullptr,
             [this](const Request& r) {
                 std::string file;
                 Reply rep = WithDeck(r.Str("deck"), [&](Entry& e) {
                     file = e.file;
                     return Reply::Ok({{"deck", e.serial}});
                 });
                 if (rep.IsError())
                     return rep;
                 std::string e;
                 auto doc = file.empty() ? std::optional<json>(json::parse(kIdle)) : scn::io::LoadFile(file, &e);
                 if (!doc)
                     return Reply::Error(500, e);
                 rep.body["file"] = file;
                 rep.body["document"] = *doc;
                 return rep;
             }});
}

}  // namespace sdb
