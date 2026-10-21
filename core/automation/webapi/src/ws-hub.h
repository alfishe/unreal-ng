#pragma once

/// @file ws-hub.h
/// @brief The shared hub of /api/v1/ws (docs/emulator/design/control-interfaces/websocket-interface.md §3, §4;
/// automation planes guidelines §3, §4; Stream Deck integration protocol.md §0): one auth per connection, topics a
/// client subscribes to, and the `surface` topic (SurfaceHub, surface-hub.h) that `join` opens. Calls
/// (`{"op":"call"}`) stay in ApiWebSocket: they run the REST handlers.
///
///   → {"op":"subscribe","topics":["run.state","ttd.position"],"max_hz":20}
///   ← {"op":"subscribed","t":"ws","topics":["run.state","ttd.position"],"max_hz":20}
///   ← {"t":"run.state","op":"state","instance":"0f3a…","value":{"state":"paused","speed":1,"turbo":false}}
///   → {"op":"unsubscribe","topics":["ttd.*"]}
///
/// State topics (latest value, coalesced at the connection's `max_hz`, at most 50): `run.state`, `ttd.position`,
/// `surfaces.context`, one message per instance whose value changed; `{"op":"gone"}` when the instance went. A
/// topic and a surface readout need a companion token (handshake or `auth`), no scope: they read what the REST GETs
/// read, which need none. An unknown topic is answered `Not Found` once and ignored.
///
/// Threads and I/O (no thread of its own, none per connection):
/// - Messages arrive on the WebAPI's I/O loops (ApiWebSocket); producers (the message centre, the actions workers)
///   never touch a socket: they post a task to the hub's loop (SetLoop: the WebAPI's main loop).
/// - One timer drives every readout and topic, armed only while somebody subscribes, at the highest `max_hz` asked
///   for; each tick reads published state only (the TTD cursor, the run state, the context: never a read that holds
///   the emulation thread), builds each value's JSON once, and sends each connection what changed since its last
///   message when its own interval has passed.
/// - Every message is built under the hub's lock and sent after it (WsOutbox): a send never runs under the lock.
///
/// Drogon-free (each connection is a send and a close function), so core-tests drive it directly with an inline
/// loop and a fake clock.

#include <json/json.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "emulator/execution/companionregistry.h"
#include "surface-hub.h"

class Emulator;

namespace actions
{
class ActionRegistry;
}

/// One connection of /api/v1/ws as the hub knows it
struct WsConnection
{
    uint64_t id = 0;
    std::function<void(const std::string& text)> send;
    std::function<void()> close;
    std::string token;
    std::string source;  ///< X-Unreal-Source of the handshake ("mcp": an agent, never a companion)
    std::string host;    ///< the peer's address
    std::optional<companion::CompanionInfo> companion;  ///< authenticated (topics, join)
    bool closing = false;  ///< revoked, bye, unknown token: nothing more is sent

    /// region <Topics>
    std::set<std::string> topics;
    double topicHz = 10;
    std::chrono::steady_clock::time_point topicSentAt{};
    bool topicFull = false;                         ///< send every value at the next tick (after a subscribe)
    std::map<std::string, std::string> topicSent;   ///< "topic|instance" -> value as last sent
    /// endregion </Topics>

    std::unique_ptr<SurfaceSession> surface;  ///< after `join`
};

/// What a handler produces under the hub's lock: sent, closed and run after the lock is released
struct WsOutbox
{
    std::vector<std::pair<std::function<void(const std::string&)>, std::string>> messages;
    std::vector<std::function<void()>> closes;
    std::vector<std::function<void()>> after;

    void Message(const WsConnection& to, std::string text);
    void Message(const WsConnection& to, const Json::Value& message);
    /// Error `error` / `message` in topic `t` (rid when not null)
    void Error(const WsConnection& to, const std::string& t, const std::string& error, const std::string& message,
               const Json::Value& rid = Json::Value());
    void Close(WsConnection& connection);
    void Flush();
};

class WsHub
{
public:
    using Task = std::function<void()>;
    /// Runs a task on the hub's loop (the WebAPI: its main loop's queueInLoop; tests: at once)
    using Post = std::function<void(Task task)>;
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    /// The readout timer (the WebAPI: runEvery / invalidateTimer on its main loop)
    struct Timer
    {
        std::function<uint64_t(double seconds, Task tick)> arm;
        std::function<void(uint64_t id)> disarm;
    };
    /// The handshake of a connection
    struct Peer
    {
        std::string token;   ///< Authorization: Bearer; "" when the client sends {"op":"auth"}
        std::string source;  ///< X-Unreal-Source
        std::string host;
    };

    static constexpr double kMaxHz = 50;
    static constexpr double kDefaultHz = 10;

    /// The process's hub (never destroyed: the WebAPI's loop may still run its tasks at exit)
    static WsHub& Instance();

    WsHub();
    ~WsHub();
    WsHub(const WsHub&) = delete;
    WsHub& operator=(const WsHub&) = delete;

    /// region <Set-up>
    void SetLoop(Post post, Timer timer);
    void SetClock(Clock clock);
    /// The registry actions go to (default: ActionRegistry::Instance(); tests their own)
    void SetRegistry(actions::ActionRegistry* registry);
    /// Follow the core's notifications (idempotent): contexts, companions, instances, the selection, run state
    void Start();
    void Stop();
    /// endregion </Set-up>

    /// region <Connections (ApiWebSocket)>
    void Open(const void* client, Peer peer, std::function<void(const std::string&)> send, std::function<void()> close);
    /// {"op":"auth"} carried a token: topics and join authenticate with it
    void SetToken(const void* client, const std::string& token);
    /// One message whose op is not `call` / `auth`: subscribe, unsubscribe, join, action, bye. False for an op the
    /// hub does not know (the caller answers bad_request)
    bool HandleMessage(const void* client, const Json::Value& message);
    void Remove(const void* client);
    /// endregion </Connections>

    /// region <Driven by the loop>
    /// The timer: changed values to every connection that is due
    void Tick();
    /// Instances came or went, the selection changed, a companion changed: rebinds following surfaces, tells and
    /// closes revoked companions
    void Refresh();
    /// NC_SURFACE_CONTEXT: `ctx` to the surfaces bound to the instance; the `surfaces.context` topic at the next tick
    void OnContext(const std::string& instanceId, const std::string& context, const std::string& by);
    /// endregion </Driven by the loop>

    /// region <Introspection (tests, docs)>
    size_t Connections() const;
    /// The timer's rate; 0 when it is not armed
    double TimerHz() const;
    /// The state topics and the surface readouts
    static const std::vector<std::string>& TopicNames();
    static const std::vector<std::string>& ReadoutNames();
    /// The value of a topic / readout of one instance (published state only); null when it has none
    static Json::Value Read(const std::string& name, const std::string& instanceId, Emulator& emulator);
    /// The instance a following surface takes: the selected one, else the only one; "" none
    static std::string FollowedInstance();
    /// `name` matches `pattern`: equal, "*", or a prefix pattern "ttd.*"
    static bool Matches(const std::string& pattern, const std::string& name);
    /// endregion </Introspection>

private:
    friend class SurfaceHub;

    WsConnection* Find(const void* client);
    WsConnection* FindById(uint64_t id);
    /// The connection's companion, authenticating its token on first use; false with the error in `out`
    bool Authenticated(WsConnection& connection, const std::string& t, WsOutbox& out);
    void SubscribeTopics(WsConnection& connection, const Json::Value& message, WsOutbox& out);
    void UnsubscribeTopics(WsConnection& connection, const Json::Value& message, WsOutbox& out);
    /// Re-arms the timer at the highest rate anyone asked for (disarms it when nobody subscribes)
    void UpdateTimer();
    void PostTask(Task task);
    std::chrono::steady_clock::time_point Now() const;
    actions::ActionRegistry& Registry() const;
    /// An action's result is on its way to the loop (the destructor waits for those in flight)
    void BeginDelivery();
    void EndDelivery();

    mutable std::mutex _mutex;
    std::map<const void*, std::unique_ptr<WsConnection>> _connections;
    uint64_t _nextId = 1;
    SurfaceHub _surface{*this};

    Post _post;
    Clock _clock;
    actions::ActionRegistry* _registry = nullptr;

    mutable std::mutex _timerMutex;
    Timer _timer;
    double _timerHz = 0;
    uint64_t _timerId = 0;
    bool _armed = false;
    std::atomic<bool> _subscribed{false};  ///< anyone subscribes (a state change then wakes a tick)

    std::mutex _flightMutex;
    std::condition_variable _flightDone;
    int _inFlight = 0;

    std::mutex _observersMutex;
    std::vector<std::pair<std::string, uint64_t>> _observers;  // topic, observer id
};
