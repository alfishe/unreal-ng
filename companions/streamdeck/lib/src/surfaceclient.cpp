#include "sdb/surfaceclient.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <future>
#include <set>

#include "sdb/actions.h"
#include "sdb/catalogue.h"
#include "sdb/format.h"
#include "sdb/runtime.h"
#include "sdb/websocket.h"

namespace sdb::surface
{

namespace
{
constexpr const char* kJoinRid = "join:";

// An action call as the socket takes it (websocket-interface.md §3.1): the REST spellings inside a value object,
// parameters beside them. Without params the value goes as it is (null: a press).
json SocketValue(const scn::ActionCall& call)
{
    const bool params = call.params.is_object() && !call.params.empty();
    const json& v = call.value;
    if (!params)
        return v;
    json out = v.is_object() ? v : json::object();  // {delta, rate}: kept
    if (v.is_boolean())
        out["p"] = v;
    else if (v.is_string())
        out["text"] = v;
    else if (v.is_number())
    {
        // the kind is the host's: a delta reads d, a level l (the other is ignored)
        out["d"] = v;
        out["l"] = v;
    }
    out["params"] = call.params;
    return out;
}

std::string Upper(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

std::optional<Companion> LoadCompanion(const std::string& file, std::string* error)
{
    std::ifstream in(file);
    if (!in)
    {
        if (error)
            *error = "no companion file " + file + " (run `unreal-streamdeck pair`)";
        return std::nullopt;
    }
    try
    {
        const json j = json::parse(in);
        Companion c{j.value("webapi", ""), j.value("token", ""), j.value("companion_id", "")};
        if (c.token.empty())
        {
            if (error)
                *error = file + " has no token (run `unreal-streamdeck pair`)";
            return std::nullopt;
        }
        return c;
    }
    catch (const std::exception& e)
    {
        if (error)
            *error = file + ": " + e.what();
        return std::nullopt;
    }
}

std::string WsUrlOf(const std::string& webapi)
{
    std::string base = webapi;
    if (base.rfind("http://", 0) != 0)
        return {};
    base = "ws://" + base.substr(7);
    while (!base.empty() && base.back() == '/')
        base.pop_back();
    return base + "/api/v1/ws";
}

// ---------------------------------------------------------------- the runtime as a sink

namespace
{
class RuntimeDeckSink final : public DeckSink
{
public:
    explicit RuntimeDeckSink(scn::Runtime& rt) : _rt(rt) {}
    void SetLive(bool live) override
    {
        _rt.SetLive(live);
    }
    void SetReadout(const std::string& path, const json& value) override
    {
        _rt.SetReadout(path, value);
    }
    void SetStripReadout(const std::string& name, const json& value) override
    {
        _rt.SetStripReadout(name, value);
    }
    void SetTarget(const json& target) override
    {
        _rt.SetTarget(target);
    }
    void OnContext(const std::string& context) override
    {
        _rt.OnContext(context);
    }
    void ShowActivity(const std::string& id, const std::string& text) override
    {
        _rt.ShowActivity(id, text);
    }
    void SetNotice(const std::string& text) override
    {
        _rt.SetNotice(text);
    }
    [[nodiscard]] std::string Context() const override
    {
        return _rt.Context();
    }
    [[nodiscard]] scn::SurfaceNeeds Needs() const override
    {
        return _rt.Needs();
    }

private:
    scn::Runtime& _rt;
};
}  // namespace

std::shared_ptr<DeckSink> RuntimeSink(scn::Runtime& runtime)
{
    return std::make_shared<RuntimeDeckSink>(runtime);
}

// ---------------------------------------------------------------- the executor

struct Client::Alive
{
    std::mutex m;
    Client* client = nullptr;
};

class ClientExecutor final : public scn::ActionExecutor
{
public:
    ClientExecutor(std::shared_ptr<Client::Alive> alive, net::EventLoop& loop, std::string serial,
                   std::chrono::milliseconds timeout)
        : _alive(std::move(alive)), _loop(loop), _serial(std::move(serial)), _timeout(timeout)
    {
    }

    scn::ActionResult Run(const scn::ActionCall& call) override
    {
        scn::ActionResult notSent{false, "no socket", {}, true};
        auto promise = std::make_shared<std::promise<scn::ActionResult>>();
        std::future<scn::ActionResult> future = promise->get_future();
        {
            std::lock_guard l(_alive->m);
            if (!_alive->client || !_alive->client->Joined(_serial))
            {
                // the deck's context before its join: the join gives it to the instance (no REST call to a host
                // that may not listen yet)
                if (_alive->client && _alive->client->Paired() && call.id == "ctx.set")
                    return {true, "the join carries it"};
                return notSent;
            }
        }
        _loop.Post([alive = _alive, serial = _serial, call, promise] {
            std::lock_guard l(alive->m);
            if (!alive->client)
            {
                promise->set_value({false, "the surface client stopped", {}, true});
                return;
            }
            alive->client->Act(serial, call,
                               [promise](const scn::ActionResult& r, const json&) { promise->set_value(r); });
        });
        // the client's own timer answers first (actionTimeout); this bound only covers a loop that stopped
        if (future.wait_for(_timeout + std::chrono::seconds(2)) != std::future_status::ready)
            return {false, "action " + call.id + ": no answer from the surface client"};
        try
        {
            return future.get();
        }
        catch (const std::exception&)
        {
            return {false, "action " + call.id + ": the surface client went away"};
        }
    }

private:
    std::shared_ptr<Client::Alive> _alive;
    net::EventLoop& _loop;
    std::string _serial;
    std::chrono::milliseconds _timeout;
};

// ---------------------------------------------------------------- the client

Client::Client(net::EventLoop& loop, Options options)
    : _loop(loop), _options(std::move(options)), _alive(std::make_shared<Alive>())
{
    _alive->client = this;
    _paired = !_options.token.empty();
    const std::filesystem::path file =
        _options.catalogueFile.empty() ? cat::DefaultFile() : std::filesystem::path(_options.catalogueFile);
    if (!file.empty())
        if (auto c = cat::Load(file))
            for (const auto& e : c->actions)
                _labels[e.id] = e.label;
    Publish();
}

Client::~Client()
{
    {
        std::lock_guard l(_alive->m);
        _alive->client = nullptr;
    }
    if (_fileTimer)
        _loop.Cancel(_fileTimer);
    for (auto& [rid, p] : _pending)
        if (p.timer)
            _loop.Cancel(p.timer);
    _ws.reset();
}

void Client::Say(const std::string& line)
{
    if (log)
        log("surface: " + line);
}

void Client::Start()
{
    _loop.Post([this] {
        _stopped = false;
        if (_options.token.empty())
        {
            _error = "not paired: run `unreal-streamdeck pair` (the decks use the REST fallback)";
            Say(_error);
            for (auto& [serial, deck] : _decks)
                deck.sink->SetNotice(kNotPairedNotice);
            CheckFile();
            Publish();
            return;
        }
        Connect();
    });
}

void Client::Stop()
{
    auto stop = [this] {
        _stopped = true;
        if (_ws && _ws->IsOpen())
            for (auto& [serial, deck] : _decks)
                if (!deck.session.empty())
                    Send({{"op", "bye"}, {"session", deck.session}});
        if (_ws)
            _ws->Close(ws::kCloseGoingAway, "daemon stops");
        if (_fileTimer)
        {
            _loop.Cancel(_fileTimer);
            _fileTimer = 0;
        }
    };
    if (_loop.InLoopThread())
        stop();
    else
        _loop.Post(stop);
}

void Client::Connect()
{
    ws::ClientOptions o;
    o.url = _options.url;
    o.headers = {{"Authorization", "Bearer " + _options.token}};
    o.reconnectMin = _options.reconnectMin;
    o.reconnectMax = _options.reconnectMax;
    o.pingInterval = _options.pingInterval;
    _ws = std::make_unique<ws::Client>(_loop, o);
    _ws->onOpen = [this] { OnOpen(); };
    _ws->onText = [this](std::string_view text) { OnText(text); };
    _ws->onClose = [this](int code, std::string_view reason) { OnClose(code, reason); };
    _error.clear();
    Say("connecting to " + _options.url);
    _ws->Start();
}

void Client::Send(const json& message)
{
    if (_ws && _ws->IsOpen())
        (void)_ws->SendText(message.dump());
}

void Client::OnOpen()
{
    _open = true;
    _error.clear();
    Say(sdb::Format("connected to {} ({} deck{})", _options.url, _decks.size(), _decks.size() == 1 ? "" : "s"));
    Send({{"op", "subscribe"}, {"topics", {"instances"}}});
    for (auto& [serial, deck] : _decks)
        Join(deck);
    Publish();
}

void Client::Join(Deck& deck)
{
    deck.needs = deck.sink->Needs();
    deck.joining = true;
    deck.session.clear();
    deck.subscribed.clear();
    const json target = deck.following ? json("follow") : deck.needs.target;
    Send({{"op", "join"},
          {"v", 1},
          {"name", deck.spec.name},
          {"model", deck.spec.model},
          {"serial", deck.spec.serial},
          {"target", target},
          {"features", {"target", "activity", "catalogue"}},
          {"rid", kJoinRid + deck.spec.serial}});
}

void Client::Subscribe(Deck& deck)
{
    if (deck.session.empty())
        return;
    std::vector<std::string> gone;
    for (const auto& name : deck.subscribed)
        if (std::find(deck.needs.readouts.begin(), deck.needs.readouts.end(), name) == deck.needs.readouts.end())
            gone.push_back(name);
    if (!gone.empty())
        Send({{"op", "unsubscribe"}, {"session", deck.session}, {"readouts", gone}});
    if (!deck.needs.readouts.empty())
        Send({{"op", "subscribe"},
              {"session", deck.session},
              {"readouts", deck.needs.readouts},
              {"max_hz", deck.needs.timeline ? _options.timelineHz : _options.readoutHz}});
    deck.subscribed = deck.needs.readouts;
}

Client::Deck* Client::BySession(const std::string& session)
{
    for (auto& [serial, deck] : _decks)
        if (!session.empty() && deck.session == session)
            return &deck;
    return nullptr;
}

std::string Client::Label(const std::string& id) const
{
    auto it = _labels.find(id);
    return it != _labels.end() && !it->second.empty() ? it->second : id;
}

void Client::OnText(std::string_view text)
{
    json j;
    try
    {
        j = json::parse(text);
    }
    catch (...)
    {
        Say("a message that is not JSON: ignored");
        return;
    }
    if (!j.is_object())
        return;
    const std::string t = j.value("t", ""), op = j.value("op", "");
    if (t == "instances")
    {
        if (op == "hello" || op == "snapshot")
            _instances = j.value("emulators", json::array());
        else if (op == "emulator" && j.contains("emulator") && j["emulator"].is_object())
        {
            const json& e = j["emulator"];
            const std::string id = e.value("id", "");
            auto it = std::find_if(_instances.begin(), _instances.end(),
                                   [&](const json& x) { return x.value("id", "") == id; });
            if (it != _instances.end())
                *it = e;
            else
                _instances.push_back(e);
        }
        else if (op == "emulator_gone")
        {
            const std::string id = j.value("id", "");
            _instances.erase(std::remove_if(_instances.begin(), _instances.end(),
                                            [&](const json& x) { return x.value("id", "") == id; }),
                             _instances.end());
        }
        Publish();
        return;
    }
    if (op == "revoked")
    {
        Revoked(j.value("message", "the companion was revoked"));
        return;
    }
    if (t != "surface")
    {
        if (op == "error")
            Say("host: " + j.value("error", "") + ": " + j.value("message", ""));
        return;
    }
    const json rid = j.contains("rid") ? j["rid"] : json();
    if (op == "welcome")
    {
        const std::string r = rid.is_string() ? rid.get<std::string>() : std::string();
        auto it = r.rfind(kJoinRid, 0) == 0 ? _decks.find(r.substr(5)) : _decks.end();
        if (it == _decks.end())
        {
            // a deck removed while it joined: end that session
            if (j.contains("session") && j["session"].is_string())
                Send({{"op", "bye"}, {"session", j["session"]}});
            return;
        }
        Deck& deck = it->second;
        deck.joining = false;
        deck.session = j.value("session", "");
        deck.instance = j["instance"].is_string() ? j["instance"].get<std::string>() : std::string();
        deck.target = j.value("target", json());
        {
            std::lock_guard l(_statusMutex);
            _sessions[deck.spec.serial] = deck.session;
        }
        deck.sink->SetNotice("");
        deck.sink->SetLive(true);
        deck.sink->SetTarget(deck.target);
        Subscribe(deck);
        // the deck's page again on this instance (a reconnect, a new instance after a restart): the deck's context
        const std::string own = deck.sink->Context();
        const std::string core = j["context"].is_string() ? j["context"].get<std::string>() : std::string();
        if (!own.empty() && own != core && !deck.instance.empty())
            Send({{"op", "action"}, {"session", deck.session}, {"id", "ctx.set"}, {"value", own}});
        Say(sdb::Format("deck {} joined as {} on {}", deck.spec.serial, deck.session,
                        deck.instance.empty() ? std::string("no machine") : deck.instance));
        Publish();
        return;
    }
    if (op == "result" || (op == "error" && rid.is_number_integer()))
    {
        if (rid.is_number_integer())
            Finish(rid.get<std::int64_t>(), op == "result" && j.value("ok", false), j.value("error", ""),
                   j.value("message", ""), j.value("body", json()));
        return;
    }
    if (op == "error")
    {
        const std::string r = rid.is_string() ? rid.get<std::string>() : std::string();
        const std::string error = j.value("error", ""), message = j.value("message", "");
        if (r.rfind(kJoinRid, 0) == 0)
        {
            auto it = _decks.find(r.substr(5));
            if (it == _decks.end())
                return;
            Deck& deck = it->second;
            deck.joining = false;
            if ((error == "not_now" || error == "Not Found") && !deck.following)
            {
                // the scenario's target cannot be bound (views on this host, a pinned instance that is gone): follow
                Say("deck " + deck.spec.serial + ": target " + deck.needs.target.dump() + " refused (" + error + ": " +
                    message + "), following the host's focus");
                deck.following = true;
                Join(deck);
                return;
            }
            Say("deck " + deck.spec.serial + " not joined: " + error + ": " + message);
            _error = error + ": " + message;
            Publish();
            return;
        }
        Say("host: " + error + ": " + message);
        return;
    }
    Deck* deck = BySession(j.value("session", ""));
    if (!deck)
        return;
    if (op == "readout")
    {
        const json values = j.value("values", json::object());
        for (const auto& [name, value] : values.items())
        {
            deck->sink->SetStripReadout(name, value);
            for (const auto& path : deck->needs.paths)
            {
                const scn::ReadoutRef ref = scn::SplitReadoutPath(path);
                if (ref.name == name)
                    deck->sink->SetReadout(path, scn::ReadoutField(ref, value));
            }
        }
    }
    else if (op == "ctx")
    {
        const std::string context = j.value("context", ""), by = j.value("by", "");
        const std::string own = deck->sink->Context();
        if (deck->retarget)
        {
            // a new instance starts on its default: the deck keeps its page and tells the new instance so
            deck->retarget = false;
            if (by == "default" && !own.empty() && own != context)
            {
                Send({{"op", "action"}, {"session", deck->session}, {"id", "ctx.set"}, {"value", own}});
                return;
            }
        }
        deck->sink->OnContext(context);
    }
    else if (op == "target")
    {
        const std::string instance = j["instance"].is_string() ? j["instance"].get<std::string>() : std::string();
        deck->retarget = !instance.empty() && instance != deck->instance;
        deck->instance = instance;
        deck->target = j;
        deck->target.erase("op");
        deck->target.erase("t");
        deck->target.erase("session");
        deck->sink->SetTarget(deck->target);
        Publish();
    }
    else if (op == "activity")
    {
        const std::string id = j.value("id", "");
        std::string text = Upper(Label(id));
        if (const std::string who = j.value("surface", ""); !who.empty())
            text += " - " + who;
        deck->sink->ShowActivity(id, text);
    }
    else if (op == "catalogue")
    {
        Say(sdb::Format("the host's action catalogue changed (rev {})", j.value("rev", 0)));
    }
}

void Client::OnClose(int code, std::string_view reason)
{
    const bool was = _open.exchange(false);
    for (auto& [serial, deck] : _decks)
    {
        deck.session.clear();
        deck.joining = false;
        deck.subscribed.clear();
        if (was)
        {
            deck.sink->SetLive(false);
            deck.sink->SetTarget(nullptr);
        }
    }
    {
        std::lock_guard l(_statusMutex);
        _sessions.clear();
    }
    FailPending("", "the connection to the host closed");
    if (_revoked || _stopped)
    {
        Publish();
        return;
    }
    _error = sdb::Format("{} ({})", std::string(reason), code);
    // a token the host does not know (revoked while we were away): the handshake is refused
    if (reason.find("401") != std::string_view::npos)
    {
        Revoked("the host does not know this companion token");
        return;
    }
    if (was)
        Say("disconnected (" + _error + "): reconnecting");
    Publish();
}

void Client::Revoked(const std::string& why)
{
    if (_revoked)
        return;
    _revoked = true;
    _open = false;
    _error = "revoked: " + why;
    Say("the companion was revoked (" + why + "): stopped; pair again (`unreal-streamdeck pair`)");
    for (auto& [serial, deck] : _decks)
    {
        deck.session.clear();
        deck.subscribed.clear();
        deck.sink->SetLive(false);
        deck.sink->SetTarget(nullptr);
        deck.sink->SetNotice(kRevokedNotice);
    }
    {
        std::lock_guard l(_statusMutex);
        _sessions.clear();
    }
    FailPending("", "the companion was revoked");
    if (_ws)
        _ws->Close(ws::kCloseNormal, "revoked");
    CheckFile();
    Publish();
}

void Client::CheckFile()
{
    if (_fileTimer)
    {
        _loop.Cancel(_fileTimer);
        _fileTimer = 0;
    }
    if (_options.companionFile.empty() || _stopped)
        return;
    if (auto c = LoadCompanion(_options.companionFile); c && c->token != _options.token)
    {
        Say("a new companion token in " + _options.companionFile + ": connecting again");
        _options.token = c->token;
        _paired = !_options.token.empty();
        _revoked = false;
        for (auto& [serial, deck] : _decks)
            deck.sink->SetNotice("");
        _ws.reset();
        Connect();
        Publish();
        return;
    }
    _fileTimer = _loop.After(_options.fileCheck, [this] {
        _fileTimer = 0;
        CheckFile();
    });
}

void Client::AddDeck(const DeckSpec& spec, std::shared_ptr<DeckSink> sink)
{
    auto add = [this, spec, sink] {
        Deck& deck = _decks[spec.serial];
        deck = Deck{};
        deck.spec = spec;
        deck.sink = sink;
        deck.needs = sink->Needs();
        if (_revoked)
            sink->SetNotice(kRevokedNotice);
        else if (_options.token.empty() && !_stopped)
            sink->SetNotice(kNotPairedNotice);
        if (_open)
            Join(deck);
        Publish();
    };
    if (_loop.InLoopThread())
        add();
    else
        _loop.Post(add);
}

void Client::RemoveDeck(const std::string& serial)
{
    auto remove = [this, serial] {
        auto it = _decks.find(serial);
        if (it == _decks.end())
            return;
        if (!it->second.session.empty())
            Send({{"op", "bye"}, {"session", it->second.session}});
        FailPending(serial, "the deck went");
        _decks.erase(it);
        {
            std::lock_guard l(_statusMutex);
            _sessions.erase(serial);
        }
        Publish();
    };
    if (_loop.InLoopThread())
    {
        remove();
        return;
    }
    std::uint64_t seq = 0;
    {
        std::lock_guard l(_removeMutex);
        seq = ++_removeSeq;
    }
    {
        // the executor sees no session from here on: its next action goes the REST way
        std::lock_guard l(_statusMutex);
        _sessions.erase(serial);
    }
    _loop.Post([this, remove, seq] {
        remove();
        {
            std::lock_guard l(_removeMutex);
            _removeDone = std::max(_removeDone, seq);
        }
        _removed.notify_all();
    });
    std::unique_lock l(_removeMutex);
    // bounded: a loop that no longer runs (shutdown) never answers
    _removed.wait_for(l, std::chrono::seconds(2), [&] { return _removeDone >= seq; });
}

void Client::UpdateDeck(const std::string& serial)
{
    _loop.Post([this, serial] {
        auto it = _decks.find(serial);
        if (it == _decks.end())
            return;
        Deck& deck = it->second;
        const scn::SurfaceNeeds needs = deck.sink->Needs();
        const bool retarget = needs.target != deck.needs.target;
        deck.needs = needs;
        if (deck.session.empty())
            return;
        if (retarget)
        {
            // another target: a new session
            Send({{"op", "bye"}, {"session", deck.session}});
            deck.following = false;
            {
                std::lock_guard l(_statusMutex);
                _sessions.erase(serial);
            }
            Join(deck);
            return;
        }
        Subscribe(deck);
    });
}

bool Client::Joined(const std::string& serial) const
{
    std::lock_guard l(_statusMutex);
    auto it = _sessions.find(serial);
    return it != _sessions.end() && !it->second.empty();
}

std::shared_ptr<scn::ActionExecutor> Client::Executor(const std::string& serial)
{
    return std::make_shared<ClientExecutor>(_alive, _loop, serial, _options.actionTimeout);
}

void Client::Act(const std::string& serial, const scn::ActionCall& call,
                 std::function<void(const scn::ActionResult&, const json& body)> done)
{
    auto it = _decks.find(serial);
    if (it == _decks.end() || it->second.session.empty() || !_open)
    {
        done({false, "no socket", {}, true}, json());
        return;
    }
    if (call.id.empty())
    {
        done({false, "action: no id"}, json());
        return;
    }
    const std::int64_t rid = _nextRid++;
    Pending p;
    p.serial = serial;
    p.id = call.id;
    p.done = std::move(done);
    p.at = std::chrono::steady_clock::now();
    p.timer = _loop.After(_options.actionTimeout, [this, rid] {
        if (auto pending = _pending.find(rid); pending != _pending.end())
            pending->second.timer = 0;
        Finish(rid, false, "Timeout",
               sdb::Format("no result in {} ms (the host may still run it)", _options.actionTimeout.count()), json());
    });
    _pending.emplace(rid, std::move(p));
    json message{{"op", "action"}, {"session", it->second.session}, {"id", call.id}, {"rid", rid}};
    if (json value = SocketValue(call); !value.is_null())
        message["value"] = std::move(value);
    Send(message);
}

void Client::Finish(std::int64_t rid, bool ok, const std::string& error, const std::string& message, const json& body)
{
    auto it = _pending.find(rid);
    if (it == _pending.end())
        return;
    Pending p = std::move(it->second);
    _pending.erase(it);
    if (p.timer)
        _loop.Cancel(p.timer);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p.at).count();
    std::string text = sdb::Format("action {} -> {} ({:.0f} ms, socket)", p.id, ok ? "ok" : error, ms);
    if (!message.empty())
        text += ": " + message;
    // a seek's marks belong to the timeline on the strip
    if (ok && p.id == "ttd.seek_by" && body.is_object() && body.contains("marks"))
        if (auto deck = _decks.find(p.serial); deck != _decks.end())
            deck->second.sink->SetStripReadout("ttd.marks", body["marks"]);
    if (p.done)
        p.done({ok, text}, body);
}

void Client::FailPending(const std::string& serial, const std::string& why)
{
    std::vector<std::int64_t> rids;
    for (const auto& [rid, p] : _pending)
        if (serial.empty() || p.serial == serial)
            rids.push_back(rid);
    for (std::int64_t rid : rids)
        Finish(rid, false, "Unavailable", why, json());
}

void Client::Publish()
{
    json decks = json::array();
    for (const auto& [serial, deck] : _decks)
        decks.push_back({{"serial", serial},
                         {"session", deck.session.empty() ? json() : json(deck.session)},
                         {"instance", deck.instance.empty() ? json() : json(deck.instance)},
                         {"target", deck.target},
                         {"readouts", deck.subscribed}});
    std::lock_guard l(_statusMutex);
    _status = {{"connected", _open.load()},
               {"revoked", _revoked},
               {"paired", !_options.token.empty()},
               {"url", _options.url},
               {"connections", _ws ? _ws->Connections() : 0},
               {"decks", decks},
               {"instances", _instances.size()},
               {"error", _error.empty() ? json() : json(_error)}};
    _instancesCopy = _instances;
}

json Client::Status() const
{
    std::lock_guard l(_statusMutex);
    return _status;
}

json Client::Instances() const
{
    std::lock_guard l(_statusMutex);
    return _instancesCopy;
}

}  // namespace sdb::surface
