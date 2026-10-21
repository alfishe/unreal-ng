# Stream Deck integration: surface protocol additions

| | |
|---|---|
| **Date** | 2026-10-19 |
| **Status** | Design, for review |
| **Base** | control surfaces `protocol.md` (v1, served on `/api/v1/ws` as the `surface` topic, §0: `auth`, `join`, `action`, `subscribe`, `welcome`, `result`, `ctx`, `readout`, `instance`, `revoked`, `error`) |

Everything here is an **addition** to protocol v1: peers that do not know a field or an op ignore it (base §8). The
base `instance` message is replaced by `target`.

## Contents

- [0. Transport: one WebSocket](#0-transport-one-websocket)
- [1. Join](#1-join)
- [2. Target](#2-target)
- [3. Readouts](#3-readouts)
- [4. Activity](#4-activity)
- [5. The action catalogue](#5-the-action-catalogue)
- [6. Machine lifecycle without REST](#6-machine-lifecycle-without-rest)
- [7. Errors](#7-errors)
- [8. Example](#8-example)
- [9. As built in the core (SD4, 2026-10-20)](#9-as-built-in-the-core-sd4-2026-10-20)

## 0. Transport: one WebSocket

Decision D7 (owner, 2026-10-20). The WebAPI has one WebSocket for every client instead of one route per stream:

| Route | Today | With this design |
|:--|:--|:--|
| `/api/v1/websocket` | debugger events (`DebugEventHub`), no auth | adapter: the `debug.*` topics of the hub, contract unchanged |
| `/api/v1/companions/events` | instances status (`CompanionEventHub`), server to client | adapter: the `instances` topic, contract unchanged |
| `/api/v1/surfaces/ws` (control surfaces design) | not built | not built: the `surface` topic of `/api/v1/ws` |
| **`/api/v1/ws`** | | the hub: one auth, topics, two-way |

- **WebSocket adds to REST, it replaces nothing.** REST stays a first-class API (scripts, `curl`, MCP, the CLI); the
  socket is for clients that want push and low latency. Whatever an action does over the socket, it does over REST
  (`POST /api/v1/controllers/actions/{id}`), and the other way round where it makes sense.
- **One auth per socket.** The companion token on the handshake (`Authorization: Bearer`) or the first message
  `{"op":"auth"}`. Without a token: localhost only, and only the `debug.*` topics, as `/api/v1/websocket` today.
- **Every REST route is a call.** `{"op":"call","rid":…,"method":…,"path":…,"body":…}` runs the same handler as
  REST and answers `result` ([websocket-interface.md](../../emulator/design/control-interfaces/websocket-interface.md),
  [automation planes guidelines](../../guidelines/automation-planes-guidelines.md)).
- **Topics.** `{"op":"subscribe","topics":["instances","debug.breakpoints"]}`; `join` opens the `surface` topic (the
  base protocol and §1-§7 below, unchanged). Each topic needs its companion scope; a missing one is answered
  `error forbidden` for that topic only.
- **Every server message carries its topic** (`"t":"surface"`, `"t":"instances"`, `"t":"debug.events"`); ops keep
  their names inside a topic, so the `hello` of `instances` and the surface ops never clash.
- **Control traffic is never queued behind streams.** Each topic has its own outgoing queue; `result`, `ctx`,
  `target` and `error` go first; readouts are latest-value (coalesced, `max_hz`); bulk debugger events drop oldest
  with a `dropped` count, as `activity` (§4).
- The daemon needs both `surface` and `instances`: one connection, one reconnect loop, one token.

### 0.1 Asynchronous I/O, a fixed number of threads

The hub adds **no thread**: not per connection, not per topic, not per hub.

- **Sockets.** They live on the WebAPI's existing event loops: Drogon / trantor, non-blocking, kqueue / epoll / wepoll,
  `setThreadNum(2)` in `automation-webapi.cpp`. A thousand clients are still two I/O threads.
- **Producers never touch a socket and never wait.** The emulation thread, the message centre and the actions worker
  only do two things:
  - store into the hub under a short lock: a readout's latest value replaces the previous one; an event is
    appended to a bounded ring;
  - wake the connection's loop once (`queueInLoop`) while nothing is pending.

  The loop drains its queues when the socket is writable.
- **Serialised once, on the loop.** A readout tick builds each instance's JSON once and every subscriber gets the
  same bytes. JSON is never built on the emulation thread, and never once per client.
- **One timer for every readout.** A single `runEvery` on the loop, at most 50 Hz, armed only while some connection
  subscribes, like `CompanionEventHub::Refresh` today. No ticker thread.
- **Calls go to the instance's actions worker.** `action` is handed to that worker (control surfaces §3) and never
  run on an I/O thread; its `result` comes back by `queueInLoop`.
- **Backpressure per connection.** Use trantor's high-water mark on the bytes the connection has queued. Above
  it:
  - readouts keep only the latest value;
  - events drop the oldest, and the next message says how many were dropped (`dropped`);
  - the producer never blocks.

  A client that stays above it for 10 s is closed with `error slow_consumer`.
- **Today's hubs serialise on the producer thread.** `DebugEventHub::Publish` and `CompanionEventHub` build their
  JSON there and send to each connection with no limit. They move to the same scheme when they become topics of the
  hub.
- **Acceptance.** The host's thread count is the same with 0, 1 and 50 connected clients. 50 clients with 50 Hz
  readouts add no measurable time per frame to the emulation thread (NFR-2, NFR-8).

## 1. Join

| Field | Meaning |
|:--|:--|
| `name`, `model`, `serial` | As base. The daemon joins **once per deck** (one session per serial) so each deck has its own target and context page. |
| `target` | `"follow"` (default), `{"view": "<id>"}`, `{"instance": "<id>"}` ([targeting.md §4](targeting.md#4-pins)). Replaces the base `instance`. |
| `features` | What the client wants pushed: `["target", "activity", "catalogue"]`. |

`welcome` adds `target` (the [§2](#2-target) object), `views` (in focus order) and `catalogue_rev` (§5).

## 2. Target

```json
{"op":"target","instance":"0f3a…","index":3,"model":"PENTAGON","ram_kb":128,
 "view":{"id":"v2","name":"Window 2"},"mode":"follow"}
```

- Sent on every change of the session's target ([targeting.md §3](targeting.md#3-rules)), before the new instance's
  `ctx` and a full `readout` (all subscribed values, not only changed ones).
- `instance: null` when there is no machine.
- Client to core: `{"op":"pin","target":"follow"|{"view":…}|{"instance":…}}`; answered by `target`.

## 3. Readouts

Base v1 has `ttd.position` and `run.state`. A deck draws more state, so readouts become a small vocabulary in the
core, one producer each, all from published values (never a read that holds the emulation thread):

| Readout | Value | Source | Rate |
|:--|:--|:--|:--|
| `ttd.position` | `{frame, tinframe, first, end, state, fps}` | TTD published cursor | ≤ 50 Hz |
| `run.state` | `{state: running\|paused\|stopped, speed, turbo}` | run control | on change |
| `machine` | `{model, ram_kb, index}` | instance | on change |
| `media.tape` | `{loaded, playing, block, blocks, progress}` | tape manager | ≤ 10 Hz |
| `media.disk` | `{drives: [{letter, loaded, name, busy}]}` | disk manager | on change |
| `audio.mute` | `{muted, master}` | mixer | on change |
| `debug.mode` | `{enabled}` | debugger | on change |
| `toggle.<action id>` | `{on}` | the action's `State()` (§5) | on change |

- `subscribe` takes names or prefixes (`media.*`). Unknown names are answered once by `error not_found` and ignored.
- `toggle.*` exists for every catalogue action whose kind is a toggle: a deck key bound to `mach.turbo_toggle` shows
  its face from `toggle.mach.turbo_toggle`, never from a count of its own presses (today the runtime flips a local
  boolean on success, wrong when another surface toggles).

## 4. Activity

Subscribed by `features: ["activity"]` ([architecture §5](architecture.md#5-the-activity-feed)):

```json
{"op":"activity","instance":"0f3a…","seq":812,"id":"run.pause_toggle","value":{"p":true},
 "origin":"host_device","surface":"Xbox Wireless Controller","ok":true,"t_ms":1760870000123}
```

- For the session's target instance only.
- Own actions are not echoed.
- Merged per id within 100 ms (deltas summed), at most 20 per second; when the socket is not writable the oldest are
  dropped and the next message carries `"dropped": n`.

## 5. The action catalogue

The deck's palette, the studio's validation and the daemon's "is this id known" all need the catalogue.

| Where | What |
|:--|:--|
| `GET /api/v1/controllers/actions` (control surfaces planes) | `{rev, actions: [{id, label, group, kind, params?, toggle?, scope, gui?, description}]}` |
| `catalogue` op (pushed when `features` has it) | `{rev}` after a change: the Qt client registered or dropped its GUI actions |
| `action` on the socket | unchanged |

Additions to the registry's `ActionInfo`:

| Field | Why |
|:--|:--|
| `description` | One sentence for the palette's tooltip and the MCP tool. |
| `params` | Schema of the value's parameters (`ttd.seek_by`: `unit`, `min`, `max`), so the inspector draws a form. |
| `toggle` | The action has an on / off state; `State()` feeds `toggle.<id>`. |
| `scope` | The companion scope it needs ([companion protocol](../2026-10-16-companion-client/protocol.md)). |

## 6. Machine lifecycle without REST

Today's scenario replaces machines with `DELETE` + `POST /api/v1/emulator/start {"replace": true}` and loads software
with three REST steps (replace, delay 600 ms, `/snapshot/load`). As actions, with values:

| Action | Value | Does |
|:--|:--|:--|
| `mach.start` | `{model, ram_kb?, config?, replace?: true}` | Start a machine; with `replace` the target's instance is replaced in place (same view, the old one destroyed when the new one runs), `symbolic_id` kept. |
| `mach.cold_reset` | `{ram_power_on?: zero\|random}` | `$model`-style restart of the same model. |
| `media.open` | `{path, kind?: auto\|snapshot\|tape\|disk, drive?, autostart?: true}` | Open software on the target, with the fast-load settings; replaces the "replace + delay + load" chain. |
| `media.tape` | `{op: play\|stop\|rewind\|eject}` | |
| `snapshot.quick` | `{op: save\|load, slot?}` | |

- `mach.start` with `replace` answers when the new instance is the target's active (no delay steps).
- `media.open` paths are resolved by the **host** ([scenarios.md §3](scenarios.md#3-paths)): `$data/…`,
  `$testdata/…`, `$user/…`, or an absolute path.
- Everything else in today's 140 REST paths maps either to an existing action or to a registry action added with this
  work; the mapping table is in [migration.md §4](migration.md#4-rest-paths-to-action-ids).

## 7. Errors

No new codes. `Not Found` covers an unknown readout name (§3) or an unknown view in a pin; `not_now` covers a pin on
a headless host (no views).

## 8. Example

```json
→ {"op":"join","v":1,"name":"Stream Deck + XL","model":"elgato.streamdeck.plusxl","serial":"A1B2C3",
   "target":"follow","features":["target","activity","catalogue"]}
← {"op":"welcome","v":1,"session":"s7","target":{"instance":"0f3a…","index":1,"model":"PENTAGON","ram_kb":128,
   "view":{"id":"v1","name":"Main window"},"mode":"follow"},"context":"play","catalogue_rev":4,
   "views":[{"id":"v1","name":"Main window","active":"0f3a…"}],"scopes":["run","input","media","machine","rewind","takeback","debug"]}
→ {"op":"subscribe","readouts":["run.state","ttd.position","toggle.*","media.*"],"max_hz":50}
← {"op":"readout","instance":"0f3a…","values":{"run.state":{"state":"running","speed":1,"turbo":false}, "...":"..."}}
← {"op":"activity","instance":"0f3a…","seq":3,"id":"run.pause_toggle","origin":"host_device",
   "surface":"Xbox Wireless Controller","ok":true}
← {"op":"readout","instance":"0f3a…","values":{"run.state":{"state":"paused","speed":1,"turbo":false}}}
← {"op":"target","instance":"7c21…","index":2,"model":"SPRINTER","view":{"id":"v2","name":"Window 2"},"mode":"follow"}
← {"op":"ctx","instance":"7c21…","context":"debug","by":"gui"}
```

## 9. As built in the core (SD4, 2026-10-20)

The core side of §1-§5; the socket messages themselves are the `surface` topic (control surfaces S4), which reads
these (the hook: [targeting.md §10.1](targeting.md#101-the-hook-for-the-surface-topic-s4)). Every value here is also
a REST / CLI / MCP / script verb ([command-interface.md §16.1](../../emulator/design/control-interfaces/command-interface.md#161-surface-targets-readouts-activity)).

| Part | As built |
|:--|:--|
| §2 target | `actions::surfaces::TargetNode`: `instance`, `index`, `model`, `ram_kb`, `view` {`id`, `name`} or null, `mode` (`follow`, `view`, `instance`), plus `pin` (`follow`, `view:<id>`, `instance:<id>`) and `source` (`view`, `selected`, `sole`, `none`) |
| §3 readouts | `core/src/emulator/actions/readouts.h`. `ttd.position` is the TTD `position` verb's body (S2's cursor: `frame`, `tinframe`, the bounds, `state`, `history`, `paused`, `fps`) plus `available`. `run.state` `{state, speed (the host multiplier 1..16), turbo}`. `machine` `{model, ram_kb, index}` from the config (not `GetMachineIdentity`, heavy on ZX-Poly). `media.tape` `{loaded, name, playing, block, blocks, progress}`: progress is blocks done / blocks (within a block needs the tape's position, which only a paused machine may read). `media.disk` `{drives: [{letter, loaded, name, protect, busy}]}`: busy is the drive's motor. `audio.mute` `{muted}`: the host has no master volume (the mixer's master mutes only). `debug.mode` `{enabled}` (the feature, not `Emulator::IsDebug`). `toggle.<id>` `{on}` (null when not known), `toggle.feature.<name>` for every feature, `toggle.set.<name>` by name. Matching: exact, `prefix.*`, `*` |
| §4 activity | `actions::ActivityFeed` (`activityfeed.h`): the actions worker records each job it ran (after the result, before the callers learn it); ring of 256; a delta of the same action, instance, origin, identity and session within 100 ms of the entry's first job is merged (summed, the higher rate, a new seq). Entry: `seq`, `instance`, `id`, `value` (`{p}`, `{d, rate}`, `{level}`, `{text}`, `params`), `origin` (`gui`, `host_device`, `companion`, `webapi`, `mcp`, `cli`, `lua`, `python`), `surface` (the client name, `GUI`, else the identity), `ok`, `error`, `message`, `t_ms`, `merged`. The session filter (own actions), the target-instance filter, 20 per second and `dropped` per socket are the topic's |
| §4 look (Q-3) | The daemon's (SD5): the scenario's `settings.activity` defaults to the key flash only, `strip: "none"`, `ignore: ["ttd.seek_by"]`; the core sends everything |
| §5 catalogue | `ActionInfo.description` (every core and GUI action: `catalogue.cpp`), `ActionInfo.toggle` (from `ActionRules::state`: `run.pause_toggle`, `ttd.record_toggle`, `mach.turbo_toggle`, `audio.mute_toggle`, `video.overscan_toggle`, `video.temporal`, `dbg.mode_toggle`, `feature.toggle`, `set.toggle`), `scope` (the rule's scope now, as S1 built it), `params` (SD6's one-line form: a typed schema is open), `rev` (`ActionRegistry::Revision()`, bumped by every registration and unregistration; `NC_ACTION_CATALOGUE`) |
| §7 errors | `not_now` only for a **view** pin on a host without views (an instance pin works headless); `Not Found` for an unknown session, view or instance |

