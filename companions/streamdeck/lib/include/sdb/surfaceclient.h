// The daemon's surface client (docs/inprogress/2026-10-19-streamdeck-integration/protocol.md, daemon.md; the
// `surface` and `instances` topics of unreal-ng's /api/v1/ws, websocket-interface.md §3.1, §3.2): one WebSocket to
// the host for every deck, on the daemon's network loop (no thread of its own, NFR-8), with the companion token of
// `unreal-streamdeck pair`.
//
//   - one `join` per deck runtime (name, model token, serial; the scenario's target; features activity, catalogue),
//     each deck its own session; `bye` with that session when the deck goes
//   - the decks' `action` steps go over the socket (Executor: rid, result); while there is no socket they go the REST
//     way (ActionResult::notSent: the system host's fallback)
//   - readouts (`subscribe` with what the scenario names, sdb/surfacereadouts.h) into the runtime: the faces' state
//     paths (DeckSink::SetReadout) and the strip's readouts (SetStripReadout); a connected deck polls nothing
//   - `ctx` -> DeckSink::OnContext; `target` -> SetTarget; `activity` -> ShowActivity (the scenario's look, Q-3)
//   - `instances`: the host's instance list (Instances())
//   - reconnects with backoff (unreal-qt restarts) and joins every deck again; `revoked`: stops, says so on the strip
//     (SetNotice), and waits for a new token in the companion file
//
// Every call but the constructor and the destructor may come from any thread: they post to the loop. Callbacks into
// a DeckSink run on the loop thread.
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "sdb/actionexec.h"
#include "sdb/eventloop.h"
#include "sdb/surfacereadouts.h"

namespace sdb::ws
{
class Client;
}
namespace sdb::scn
{
class Runtime;
}

namespace sdb::surface
{

using json = nlohmann::json;

// The strip's lasting lines without a link (a tap on the strip then pairs: sdb/pairing.h)
inline constexpr const char* kRevokedNotice = "COMPANION REVOKED - TOUCH TO PAIR";
inline constexpr const char* kNotPairedNotice = "NOT PAIRED - TOUCH TO PAIR";

// The companion file `unreal-streamdeck pair` writes: {webapi, token, companion_id, scopes}
struct Companion
{
    std::string webapi, token, id;
};
[[nodiscard]] std::optional<Companion> LoadCompanion(const std::string& file, std::string* error = nullptr);
// http://host:port -> ws://host:port/api/v1/ws ("" for anything but http://)
[[nodiscard]] std::string WsUrlOf(const std::string& webapi);

// Where a deck's pushes land (the runtime; tests: a fake). Called on the loop thread.
class DeckSink
{
public:
    virtual ~DeckSink() = default;
    virtual void SetLive(bool live) = 0;                                           // readouts pushed: nothing polled
    virtual void SetReadout(const std::string& path, const json& value) = 0;       // a face's state path
    virtual void SetStripReadout(const std::string& name, const json& value) = 0;  // ttd.position, run.state, ...
    virtual void SetTarget(const json& target) = 0;                                // the target op (null: none)
    virtual void OnContext(const std::string& context) = 0;
    virtual void ShowActivity(const std::string& id, const std::string& text) = 0;
    virtual void SetNotice(const std::string& text) = 0;    // a lasting strip line ("" clears)
    [[nodiscard]] virtual std::string Context() const = 0;  // the context the deck asked for last
    [[nodiscard]] virtual scn::SurfaceNeeds Needs() const = 0;
};
// The runtime as a sink
[[nodiscard]] std::shared_ptr<DeckSink> RuntimeSink(scn::Runtime& runtime);

struct Options
{
    std::string url;            // ws://127.0.0.1:8090/api/v1/ws
    std::string token;          // the companion token ("" = not paired: nothing connects)
    std::string companionFile;  // watched while revoked / not paired: a new token connects again ("" = none)
    std::chrono::milliseconds reconnectMin{250}, reconnectMax{5000};
    std::chrono::milliseconds pingInterval{15000};
    std::chrono::milliseconds actionTimeout{5000};  // an action's result; then it fails (the host may still run it)
    std::chrono::milliseconds fileCheck{2000};      // how often the companion file is looked at while revoked
    double timelineHz = 50, readoutHz = 10;         // max_hz with / without the TTD timeline on a strip
    std::string catalogueFile;                      // labels for the activity line ("" = $data's)
};

struct DeckSpec
{
    std::string serial;
    std::string model;  // the model's token (plusxl)
    std::string name;   // Stream Deck + XL
};

class ClientExecutor;

class Client
{
public:
    Client(net::EventLoop& loop, Options options);
    ~Client();  // on the loop thread, or after the loop stopped
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void Start();
    // bye, close, no more attempts; the decks keep their REST fallback
    void Stop();

    // A deck runtime came / went. Remove waits until the loop has let go of the sink (call it off the loop thread,
    // before the runtime is destroyed; on the loop thread it is immediate).
    void AddDeck(const DeckSpec& spec, std::shared_ptr<DeckSink> sink);
    void RemoveDeck(const std::string& serial);
    // The deck's scenario changed (a reload): its readouts and target are asked again
    void UpdateDeck(const std::string& serial);

    // The `action` kind of a deck over the socket; while the deck has no session, notSent (the caller's fallback)
    [[nodiscard]] std::shared_ptr<scn::ActionExecutor> Executor(const std::string& serial);
    // Asynchronous form, on the loop thread: done(result, body) once the result came, or failed / timed out
    void Act(const std::string& serial, const scn::ActionCall& call,
             std::function<void(const scn::ActionResult&, const json& body)> done);

    // {connected, revoked, paired, url, connections, decks: [{serial, session, instance, context}], instances,
    // error}; thread-safe
    [[nodiscard]] json Status() const;
    // The host's instances as the `instances` topic last told ([{id, state, model, ...}]); thread-safe
    [[nodiscard]] json Instances() const;
    [[nodiscard]] bool Connected() const
    {
        return _open.load();
    }
    // The deck has a session now (its actions go over the socket); thread-safe
    [[nodiscard]] bool Joined(const std::string& serial) const;
    // A companion token to join with (not the REST fallback): a deck's join will carry its context; thread-safe
    [[nodiscard]] bool Paired() const { return _paired; }

    std::function<void(const std::string& line)> log;

private:
    friend class ClientExecutor;
    struct Deck
    {
        DeckSpec spec;
        std::shared_ptr<DeckSink> sink;
        std::string session;  // "" until welcome
        std::string instance;
        json target;
        bool joining = false;
        bool following = false;  // the target was refused: joined with "follow"
        bool retarget = false;   // a new instance: its first default ctx gets the deck's context instead
        scn::SurfaceNeeds needs;
        std::vector<std::string> subscribed;
    };
    struct Pending
    {
        std::string serial;
        std::string id;
        std::function<void(const scn::ActionResult&, const json&)> done;
        std::chrono::steady_clock::time_point at;
        net::EventLoop::TimerId timer = 0;
    };

    void Connect();
    void OnOpen();
    void OnText(std::string_view text);
    void OnClose(int code, std::string_view reason);
    void Join(Deck& deck);
    void Subscribe(Deck& deck);
    void Send(const json& message);
    void Say(const std::string& line);
    void Revoked(const std::string& why);
    void CheckFile();
    void FailPending(const std::string& serial, const std::string& why);
    void Finish(std::int64_t rid, bool ok, const std::string& error, const std::string& message, const json& body);
    void Publish();  // the status copy the other threads read
    Deck* BySession(const std::string& session);
    std::string Label(const std::string& id) const;

    struct Alive;  // what an Executor holds: the client while it lives

    net::EventLoop& _loop;
    Options _options;
    std::shared_ptr<Alive> _alive;
    std::unique_ptr<ws::Client> _ws;
    std::map<std::string, Deck> _decks;  // by serial (the loop's)
    std::map<std::int64_t, Pending> _pending;
    std::int64_t _nextRid = 1;
    json _instances = json::array();
    bool _revoked = false, _stopped = false;
    std::string _error;
    net::EventLoop::TimerId _fileTimer = 0;
    std::map<std::string, std::string> _labels;  // action id -> catalogue label

    std::atomic<bool> _open{false};
    std::atomic<bool> _paired{false};
    mutable std::mutex _statusMutex;
    std::map<std::string, std::string> _sessions;  // serial -> session, for the executors (under _statusMutex)
    json _status = json::object();
    json _instancesCopy = json::array();

    // RemoveDeck's handshake with the loop
    std::mutex _removeMutex;
    std::condition_variable _removed;
    std::uint64_t _removeSeq = 0, _removeDone = 0;
};

}  // namespace sdb::surface
