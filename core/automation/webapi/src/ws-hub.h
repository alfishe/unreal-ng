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
/// `surfaces.context`, the monitoring ones of an instance (`emulator.stats`, `debug.breakpoints`, `media.slots`,
/// `recording.state`, `debug.labels`, `ttd.status`, `debug.analyzers`, `controllers.state`,
/// `game.events`: monitoringapi.h), one
/// message per instance whose value changed; `{"op":"gone"}` when the instance went. Topics of the process, one
/// message without `instance`: `streamdeck.status`, `host.stats`, `host.clients`. A
/// topic and a surface readout need a companion token (handshake or `auth`), no scope: they read what the REST GETs
/// read, which need none. An unknown topic is answered `Not Found` once and ignored.
///
/// `instances`: the companion's live view of /api/v1/companions/events in the hub (`hello`, `emulator`,
/// `emulator_gone`, `snapshot` on {"op":"snapshot"}), latest value too: each instance entry is compared with the one
/// last sent. `debug.events` / `debug.asm_sync`: DebugEventHub's events (the `debug` / `asm_sync` topics of
/// /api/v1/websocket, the same numbering), an event stream: a bounded queue per connection that drops the oldest and
/// says how many (`dropped`). They need no token from a loopback peer (as /api/v1/websocket).
///
/// Backpressure (WsFlow): the transport tells the hub when the bytes queued on a connection's socket are above the
/// high-water mark (trantor's high-water mark callback; a probe, a ping frame, measures again). Above it the hub sends
/// only control messages (acks, errors, `result`, `ctx`, `target`, `revoked`): state topics, readouts and `instances`
/// wait and send their latest values once it drains; events queue and drop the oldest. Above it for longer than the
/// slow-consumer limit: `error slow_consumer`, then the socket is closed.
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
#include <deque>
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
struct ActivityEntry;
}

/// How full a connection's socket is, as its transport measured it (the I/O loop writes, the hub reads)
struct WsFlow
{
    std::atomic<bool> congested{false};  ///< the bytes queued were above the high-water mark at the last measure
    std::atomic<uint64_t> queued{0};     ///< the bytes queued at that measure (0 below the mark)
};

/// One connection of /api/v1/ws as the hub knows it
struct WsConnection
{
    uint64_t id = 0;
    std::function<void(const std::string& text)> send;
    std::function<void()> close;
    std::shared_ptr<WsFlow> flow;  ///< null: the transport cannot tell (never congested)
    std::function<void()> probe;   ///< measures `flow` again (the transport: a ping frame through the same queue)
    std::optional<std::chrono::steady_clock::time_point> congestedAt;  ///< since when it is above the mark
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

    /// region <instances>
    bool instancesFull = false;      ///< send the whole view at the next refresh (`instancesOp`)
    std::string instancesOp;         ///< "hello" (after subscribe) or "snapshot" (asked for)
    uint64_t instancesSeq = 0;       ///< counts this connection's `instances` messages from 1
    std::map<std::string, std::string> instancesSent;  ///< instance id -> its entry as last sent
    /// endregion </instances>

    /// region <Event topics>
    std::deque<std::pair<std::string, std::string>> events;  ///< topic, message: waiting while congested
    uint64_t eventsDropped = 0;                              ///< dropped since the last event sent
    /// endregion </Event topics>

    /// After `join`: session id -> the surface (several per connection: the Stream Deck daemon joins once per deck)
    std::map<std::string, std::unique_ptr<SurfaceSession>> surfaces;
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
    /// Backpressure defaults (UNREAL_WS_HIGH_WATER_BYTES, UNREAL_WS_SLOW_CONSUMER_MS override them in the WebAPI)
    static constexpr uint64_t kHighWaterBytes = 256 * 1024;
    static constexpr int kSlowConsumerMs = 10000;
    /// Events waiting per connection (and from the producers to the loop) before the oldest are dropped
    static constexpr size_t kEventQueue = 256;
    static constexpr size_t kPendingEvents = 1024;
    /// The `me` body of a companion: {companion, emulators: [{id, state, control, actions, ...}]} (the WebAPI's
    /// CompanionMeJson; tests their fake)
    using Snapshot = std::function<Json::Value(const companion::CompanionInfo& companion)>;

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
    /// How the `instances` topic builds a companion's view
    void SetSnapshot(Snapshot snapshot);
    /// How long a connection may stay above the high-water mark before it is closed as a slow consumer
    void SetSlowConsumerLimit(std::chrono::milliseconds limit);
    /// The high-water mark the transport measures each socket's queue against (read when a connection opens)
    void SetHighWaterMark(uint64_t bytes);
    uint64_t HighWaterMark() const;
    /// Follow the core's notifications (idempotent): contexts, companions, instances, the selection, run state
    void Start();
    void Stop();
    /// endregion </Set-up>

    /// region <Connections (ApiWebSocket)>
    /// `flow` and `probe`: the transport's measure of its socket's queue (backpressure); none, never congested
    void Open(const void* client, Peer peer, std::function<void(const std::string&)> send, std::function<void()> close,
              std::shared_ptr<WsFlow> flow = nullptr, std::function<void()> probe = nullptr);
    /// {"op":"auth"} carried a token: topics and join authenticate with it
    void SetToken(const void* client, const std::string& token);
    /// One message whose op is not `call` / `auth`: subscribe, unsubscribe, snapshot, join, action, bye. False for an
    /// op the hub does not know (the caller answers bad_request)
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
    /// The `instances` topic: compares every subscriber's view with what it was sent and sends the changes (the
    /// notifications that can change an entry and, once a second, the tick)
    void RefreshInstances();
    /// A debugger event (DebugEventHub's sink, any thread): queued for the loop, never sent from the producer.
    /// `topic`: DebugEventHub's ("debug", "asm_sync"); `event` carries its seq and emulator
    void OnDebugEvent(const std::string& topic, const Json::Value& event);
    /// The events queued by OnDebugEvent to the subscribers (the loop)
    void DrainEvents();
    /// An action the registry ran (ActivityFeed's listener posts it): `activity` to the surfaces that asked for it
    void OnActivity(const actions::ActivityEntry& entry);
    /// NC_ACTION_CATALOGUE: `catalogue` {rev} to the surfaces that asked for it
    void OnCatalogue(uint64_t revision);
    /// endregion </Driven by the loop>

    /// region <Introspection (tests, docs)>
    size_t Connections() const;
    /// The timer's rate; 0 when it is not armed
    double TimerHz() const;
    /// Every topic `subscribe` takes (state, `instances`, events) and the state topics alone
    static const std::vector<std::string>& TopicNames();
    static const std::vector<std::string>& StateTopicNames();
    /// The event topics: `debug.events`, `debug.asm_sync`
    static const std::vector<std::string>& EventTopicNames();
    /// The state topics' values (`ttd.position`, `run.state`, `surfaces.context`): the surface readouts are the whole
    /// vocabulary of actions::ReadoutNames (readouts.h), these two among them
    static const std::vector<std::string>& ReadoutNames();
    /// The value of a topic / readout of one instance (published state only); null when it has none. The state
    /// topics' names read here, every other surface readout through actions::ReadReadout
    static Json::Value Read(const std::string& name, const std::string& instanceId, Emulator& emulator);
    /// The instance a following surface takes: actions::TargetState's followed instance (the front view's active
    /// instance, else the selected one, else the only one); "" none
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
    /// The state topics and readouts of the connections due (the tick, the hub's lock held)
    using SurfaceDue = std::vector<std::pair<WsConnection*, SurfaceSession*>>;
    void DeliverValues(const std::vector<WsConnection*>& topicDue, const std::set<std::string>& topicNames,
                       const SurfaceDue& surfaceDue, std::chrono::steady_clock::time_point now, WsOutbox& out);
    /// Posts RefreshInstances once while somebody subscribes `instances` (coalesced)
    void WakeInstances();
    /// Posts one Tick while somebody subscribes (coalesced: a burst of monitoring notifications is one tick)
    void WakeTick();
    /// The queued events of a connection that is not congested, the first with `dropped` when some were
    static void FlushEvents(WsConnection& connection, WsOutbox& out);
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
    Snapshot _snapshot;
    std::chrono::milliseconds _slowAfter{kSlowConsumerMs};
    std::atomic<uint64_t> _highWater{kHighWaterBytes};
    std::chrono::steady_clock::time_point _instancesAt{};  ///< the last refresh of `instances`

    mutable std::mutex _postMutex;  ///< `_post` alone: producers post without the hub's lock

    std::mutex _instancesMutex;  ///< one refresh of `instances` at a time
    std::atomic<bool> _instancesSubscribed{false};
    std::atomic<bool> _instancesPending{false};
    std::atomic<bool> _tickPending{false};

    std::mutex _eventsMutex;  ///< the events from the producers, waiting for the loop
    std::deque<std::pair<std::string, Json::Value>> _pendingEvents;  ///< hub topic, event
    std::map<std::string, uint64_t> _pendingDropped;                 ///< hub topic -> events dropped before the loop
    bool _eventsPosted = false;
    std::atomic<bool> _eventsSubscribed{false};
    uint64_t _debugSink = 0;
    uint64_t _activityListener = 0;
    std::atomic<bool> _activitySubscribed{false};  ///< some surface asked for `activity`

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
