#pragma once

/// @file surface-hub.h
/// @brief The `surface` topic of /api/v1/ws (control surfaces protocol.md, Stream Deck integration protocol.md §0,
/// D7): a companion process (streamdeck-bridge, a test) joins as a surface, calls actions by id with values, is told
/// its instance's context (`ctx`) and gets readouts (`ttd.position`, `run.state`) at the rate it asks for.
///
///   → {"op":"join","v":1,"name":"streamdeck-bridge","model":"elgato.streamdeck.plus","target":"follow"}
///   ← {"op":"welcome","t":"surface","v":1,"session":"s1","instance":"0f3a…","context":"play","scopes":[…],…}
///   → {"op":"subscribe","readouts":["ttd.position","run.state"],"max_hz":50}
///   → {"op":"action","id":"ttd.seek_by","value":{"d":-3,"rate":18.5,"unit":"frames"},"rid":41}
///   ← {"op":"result","t":"surface","rid":41,"ok":true,"body":{…,"marks":[…]}}
///   ← {"op":"readout","t":"surface","instance":"0f3a…","values":{"ttd.position":{…}}}
///
/// SurfaceHub is the topic's handler inside the shared hub (WsHub, ws-hub.h): the hub owns the connections, the one
/// lock, the readout timer and the delivery; it hands the surface ops to this class with its lock held, and the
/// outgoing messages are collected in a WsOutbox the hub sends after unlocking. Actions go to the instance's actions
/// worker (ActionRegistry::Submit, origin Companion); their results come back through the hub's loop.
///
/// The instance a session acts on (its binding): `target` "follow" (default) follows the WebAPI's rule - the selected
/// instance, else the only one - and is re-evaluated when instances come and go or the selection changes;
/// {"instance": id or index} pins one. Views ({"view": …}), the `pin` op and the focused-view rule are the Stream Deck
/// integration's TargetState (SD4, targeting.md): `{"view":…}` is answered `not_now` until then.

#include <json/json.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>

class WsHub;
struct WsConnection;
struct WsOutbox;

namespace actions
{
struct ActionResult;
}

/// What one joined surface keeps (one per connection)
struct SurfaceSession
{
    std::string session;  ///< "s7"
    int v = 1;            ///< the protocol version chosen
    std::string name;
    std::string model;
    std::string serial;
    bool pinned = false;   ///< target {"instance": …}; else it follows
    std::string instance;  ///< the bound instance; "" none (no machine, or the pinned one is gone)
    std::string context;   ///< the context as last sent in welcome / ctx
    /// Readouts
    std::set<std::string> readouts;
    double hz = 10;
    std::chrono::steady_clock::time_point sentAt{};
    bool full = false;                         ///< send every subscribed value at the next tick
    std::map<std::string, std::string> sent;   ///< readout -> its value as last sent (compact JSON)
};

/// The values one tick read: instance id -> readout / topic name -> its value as compact JSON
using WsReadings = std::map<std::string, std::map<std::string, std::string>>;

class SurfaceHub
{
public:
    static constexpr int kVersion = 1;

    explicit SurfaceHub(WsHub& hub) : _hub(hub) {}

    /// region <Ops (the hub's lock held)>
    void Join(WsConnection& connection, const Json::Value& message, WsOutbox& out);
    void Action(WsConnection& connection, const Json::Value& message, WsOutbox& out);
    void Subscribe(WsConnection& connection, const Json::Value& message, WsOutbox& out);
    void Unsubscribe(WsConnection& connection, const Json::Value& message, WsOutbox& out);
    void Bye(WsConnection& connection, WsOutbox& out);
    /// endregion </Ops>

    /// region <Pushes (the hub's lock held)>
    /// NC_SURFACE_CONTEXT: `ctx` to the sessions bound to `instance`
    void Context(WsConnection& connection, const std::string& instance, const std::string& context,
                 const std::string& by, WsOutbox& out);
    /// Instances came or went, the selection changed: a following session whose instance changed gets `target`, the
    /// new instance's `ctx` and a full readout at the next tick; a pinned one whose instance is gone, `target` null
    void Rebind(WsConnection& connection, const std::string& followed, WsOutbox& out);
    /// The tick: changed readouts of the bound instance when the session is due
    void Deliver(WsConnection& connection, const WsReadings& readings, std::chrono::steady_clock::time_point now,
                 WsOutbox& out);
    /// Due at `now` (has readouts and its interval passed, or a full send is pending)
    static bool Due(const SurfaceSession& session, std::chrono::steady_clock::time_point now);
    /// endregion </Pushes>

    /// An action's result, back on the hub's loop: `result` when the action carried a `rid`, else an `error` when it
    /// failed (a success without a rid is not answered). Takes the hub's lock; a closed connection gets nothing
    void DeliverResult(uint64_t connection, const Json::Value& rid, const actions::ActionResult& result);

    /// `target` as sent in welcome and on a change: {instance, index, model, ram_kb, view, mode}
    static Json::Value TargetOf(const SurfaceSession& session);

private:
    void Bind(SurfaceSession& session, const std::string& instance, WsConnection& connection, WsOutbox& out);

    WsHub& _hub;
    uint64_t _nextSession = 1;
};
