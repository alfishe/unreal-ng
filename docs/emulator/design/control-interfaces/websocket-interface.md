# WebSocket Interface

| | |
|---|---|
| **Status** | 🔧 In progress. Calls (§2) and auth (§1) are implemented (2026-10-20, `core/automation/webapi/src/api-websocket.cpp`); the topic mechanism with the state topics `run.state`, `ttd.position`, `surfaces.context` and the `surface` topic (§3) are implemented (2026-10-20, control surfaces S4, `ws-hub.*`, `surface-hub.*`); the `instances` and `debug.*` topics (§3.2, §3.3) and backpressure (§4.1) too (2026-10-21); the `surface` topic's Stream Deck additions (several surfaces per connection, `features` with `activity` and `catalogue`, the whole readout vocabulary) too (2026-10-21, SD5). Still to come: views and `pin` on the `surface` topic (`TargetState`). |
| **Endpoint** | `GET /api/v1/ws` (WebSocket upgrade on the WebAPI port) |
| **Rules** | [automation-planes-guidelines.md](../../../guidelines/automation-planes-guidelines.md) |
| **Design** | [Stream Deck integration protocol §0](../../../inprogress/2026-10-19-streamdeck-integration/protocol.md#0-transport-one-websocket) |

One socket for every client. It **adds to REST** and replaces nothing. Every REST route can also be called on the
socket, and the socket also pushes state changes, so clients never poll.

## 1. Connection and auth

- The companion token on the handshake (`Authorization: Bearer unc_…`), or the first message `{"op":"auth","token":…}`
  for clients that cannot set headers.
- Without a token: calls (each passes the REST gates, as a request without a token does) and, from this host (a
  loopback peer), the `debug.*` topics, as `/api/v1/websocket`. The other topics and the `surface` topic need the
  token (`companion_required` otherwise; the `debug.*` topics from another host too).
- A token on the handshake passes the HTTP gates like any request with it: an unknown or revoked one is refused
  before the upgrade (401 `companion_unknown`), one with `X-Unreal-Source: mcp` too (403 `companion_not_for_agents`).
  A token sent by `auth` is checked when a topic or `join` first needs it: unknown or revoked is answered
  `companion_unknown` and the socket is closed (as the events socket); on an agent's socket (`X-Unreal-Source: mcp`)
  `companion_not_for_agents`. When the user revokes the companion, the socket is told `revoked` and closed.
- JSON objects, one per text frame. Every message has an `op`. Messages from the server also carry their topic `t`.
- Ping and pong are WebSocket control frames.

## 2. Calls

```json
→ {"op":"call","rid":7,"method":"POST","path":"/api/v1/emulator/{id}/tape/play"}
← {"op":"result","rid":7,"status":200,"body":{…}}
```

The same handler as the REST route: same scopes, body and errors. Calls with different `rid` may be answered out of
order.

As implemented:

- `method`: `GET`, `POST`, `PUT`, `DELETE`, `PATCH`, `HEAD`; `path` a WebAPI route `/api/v1/...` with an optional
  query string; `body` any JSON (sent as the JSON body). The sockets themselves (`/api/v1/ws`, `/api/v1/websocket`,
  `/api/v1/companions/events`) are not calls.
- `"session": "<id>"` carries an automation session (the `X-Unreal-Session` header). When the automation gate opens
  the implicit session for a call, the `result` carries it as `session`, for the next calls. A `session` that does not
  hold the instance the call changes is no pass: the gate applies as for a call without one.
- The call passes the checks every HTTP request passes before routing (CORS, companion token and scopes, session
  revocation, the automation gate), then drogon runs the route's handler; the socket's peer is the call's peer, so the
  implicit session of a socket client is the client's own.
- `result`: `{"op":"result","t":"call","rid":…,"status":…,"body":…}` (a body that is not JSON comes as `{"text":…}`).
  Errors of the socket itself: `{"op":"error","t":"call","rid":…,"error":"bad_request" | "busy","message":…}`;
  `busy` when 64 calls of the connection are in flight.
- `{"op":"auth","token":"unc_…"}` is answered `{"op":"auth","t":"call","ok":true}`; every later call carries the
  token, so the companion gate decides per call (a revoked token is refused at the next call).

Example, a host action ([command-interface.md §16](./command-interface.md#16-host-actions)):

```json
→ {"op":"call","rid":1,"method":"POST","path":"/api/v1/controllers/actions/run.pause_toggle","body":{}}
← {"op":"result","t":"call","rid":1,"status":200,"body":{"ok":true,"action":"run.pause_toggle","message":"Emulator paused","result":{…}}}
```

A dial's seek and the surface context (control surfaces S2) are calls too; their pushes (`ctx`, the `ttd.position`
readout) come with the `surface` topic (S4):

```json
→ {"op":"call","rid":2,"method":"POST","path":"/api/v1/controllers/actions/ttd.seek_by","body":{"value":{"delta":-3,"rate":18.5}}}
← {"op":"result","t":"call","rid":2,"status":200,"body":{"ok":true,"action":"ttd.seek_by","result":{"arrived_at":{"frame":1834,"tinframe":0},…,"from":1852,"frame":1834,"frames":-18,"marks":[]}}}
→ {"op":"call","rid":3,"method":"GET","path":"/api/v1/surfaces/context"}
← {"op":"result","t":"call","rid":3,"status":200,"body":{"ok":true,"instance":"0f3a…","context":"ttd","set":false,"by":null,"default":"ttd"}}
```

The surface targets, readouts and activity (Stream Deck SD4: `GET /api/v1/surfaces/views`, `target`, `sessions`,
`PUT …/sessions/{id}/pin`, `readouts`, `activity`) are calls as well; the `surface` topic (S4) pushes `target` on
`NC_SURFACE_TARGET`, the readouts and `activity` from the core's `ActivityFeed`.

## 3. Topics

```json
→ {"op":"subscribe","topics":["run.state","ttd.*"],"max_hz":20}
← {"op":"subscribed","t":"ws","topics":["run.state","ttd.position"],"max_hz":20}
← {"t":"run.state","op":"state","instance":"0f3a…","value":{"state":"paused","speed":1,"turbo":false}}
← {"t":"ttd.position","op":"state","instance":"0f3a…","value":{"frame":1834,"tinframe":0,"first":0,"end":9000,"end_tinframe":0,"state":"idle","history":true,"paused":true,"fps":48.828125}}
→ {"op":"unsubscribe","topics":["ttd.position"]}
← {"op":"unsubscribed","t":"ws","topics":["run.state"]}
```

| Topic | Pushes | Scope | Status |
|:--|:--|:--|:--|
| `run.state` | per instance `{state: running \| paused \| stopped, speed, turbo, pace: {preset, label, unit, count, interval_ms, effective_ms, running, stop_reason}}` (`speed`: the host setting 1..16, as the `run.state` readout; the composed clock with the machine's own turbo is `emulator.stats` cpu.multiplier; `pace`: the paced run, `GET /run_pace` without its tick count) | none (a read) | implemented (`pace` 2026-10-21) |
| `ttd.position` | per instance the TTD published cursor `{frame, tinframe, first, end, end_tinframe, state, history, paused, fps, unit, unit_count}` (the `ttd position` verb's bounds; `unit`: the scrub unit of `ttd.seek_by`, `ttd.scrub_unit`) | none | implemented (`unit` 2026-10-21) |
| `surfaces.context` | per instance the surface context `{context, set, by, default}` (`GET /api/v1/surfaces/context`) | none | implemented |
| `streamdeck.status` | the Stream Deck daemon as unreal-qt supervises it, once for the process (no `instance`): `{attached, state, mode, activity, own, pid, deck_attached, restarts, error, version, decks, viewers, surface, pairing: {state, code, paired, companion_id, error}, pack: {serial: a pack or scenario}}` (the fields of `GET /api/v1/streamdeck` that change with events, [command-interface.md §16.2](./command-interface.md#162-the-stream-deck-daemon)) | none | implemented (SD7) |
| `audio.mixer` | per instance the mixer's levels and mute / solo: `{master: {muted}, solo, devices: [{source, muted, solo, audible, volume, peak, active, channels?: [{key, muted, solo, audible, volume?, peak?}]}]}` (`GET /audio/levels`, the readout `audio.mixer`; [mixer channels](../audio/mixer-channels-and-settings.md)); a meter that moves sends at `max_hz` | none | implemented |
| `media.recent` | the files opened lately, the same for every instance: `{revision, kinds: {snapshot: [...], tape: [...], ...}, all: [{index, kind, name, path, time}]}` (`GET /api/v1/recent`, [media.md → Recent files](../../../features/media.md#recent-files)) | none | implemented (2026-10-21) |
| `media.rzx` | per instance the RZX playback `{loaded, active, name, state, frame, total_frames, progress, desyncs}` (`GET /rzx/status`) | none | implemented (2026-10-21) |
| `cap.record` | per instance the video recording `{available, recording, paused, frames, seconds, bytes, output}` (`GET /video/record/status`) | none | implemented (2026-10-21) |
| `machine.switches` | per instance the front-panel switches `{switches: [{name, on}]}` (`GET /switches`) | none | implemented (2026-10-21) |
| `input.connectors` | per instance the joystick connectors `{connectors: [{id, kind, device, fixed, port?}]}` (`GET /controllers/connectors`) | none | implemented (2026-10-21) |
| `surface` | control surfaces: opened by `join` (not by `subscribe`): `action`, `result`, `ctx`, readouts, `target`, `revoked` ([below](#31-the-surface-topic)) | per action | implemented (S4); `activity`, `catalogue`, the readout vocabulary and views: Stream Deck SD4 |
| `instances` | the companion's live view of the instances (`hello`, `emulator`, `emulator_gone`, `snapshot`): `/api/v1/companions/events` on this socket ([§3.2](#32-the-instances-topic)) | none (a companion's own view) | implemented |
| `debug.events` | debugger events: `paused`, `resumed`, `step_done`, `breakpoints_changed` (the `debug` topic of `/api/v1/websocket`, [§3.3](#33-the-debug-topics)) | none; no token needed from this host | implemented |
| `host.stats` | the host process, once for the process (no `instance`): `{cpu_percent, cpu_percent_host, cores, rss_bytes, peak_rss_bytes, virtual_bytes, threads, cpu_time_s, uptime_s, …}`, sampled at most twice a second (`GET /api/v1/monitor/host.stats`, [command-interface.md §17](./command-interface.md#17-monitoring)) | none | implemented |
| `host.clients` | the process: per plane (`rest`, `ws`, `events`, `debug_ws`, `mcp`, `cli`) `{connections, opened, requests}`, `companions`, `instances` (`GET /api/v1/monitor/host.clients`) | none | implemented |
| `emulator.stats` | per instance the measured pace `{state, measuring, fps, nominal_fps, speed_percent, frame_time_us {avg, max}, frame_budget_percent, halted_percent, interrupts_per_frame, frame, t_states, turbo, cpu {mhz, base_mhz, multiplier, host_multiplier, hw_turbo_ratio}}`: a new value every 0.5 s while it runs (`GET /api/v1/monitor/emulator.stats`) | none | implemented |
| `debug.breakpoints` | per instance `{total, active, inactive, owned, by_kind {…}, groups [{name, total, active}], last_hit}` on every change of the set and on a new last hit (`GET /api/v1/monitor/debug.breakpoints`; the events are `breakpoints_changed` on `debug.events`) | none | implemented |
| `media.slots` | per instance every media slot `{slots [{id, kind, present, pending, name, format, dirty, changed_units, protect}], loaded, dirty}` (`GET /api/v1/monitor/media.slots`) | none | implemented |
| `recording.state` | per instance `{available, enabled, recording, paused, mode, file, frames, duration_s, emulated_s, file_bytes, fps}` (`GET /api/v1/monitor/recording.state`) | none | implemented |
| `debug.labels` | per instance `{total, sets [{id, title, enabled, symbols}]}` (`GET /api/v1/monitor/debug.labels`) | none | implemented |
| `ttd.status` | per instance `{state, recording, history, frame, first, end, frames, checkpoints, memory_bytes, history_bytes, page_store_bytes, bookmarks, write_journal_records, input_events, evicted_checkpoints}`: the cursor's fields at the rate, the sizes refreshed every 2 s (`GET /api/v1/monitor/ttd.status`; the full status is `GET …/ttd/status`) | none | implemented |
| `debug.analyzers` | per instance the profilers' session states `{opcode, memory, calltrace, coverage {available, recording, instructions}, pchist}` (`GET /api/v1/monitor/debug.analyzers`) | none | implemented |
| `controllers.state` | per instance `{connectors [{id, kind, device, plugged, fixed}], plugged}` (`GET /api/v1/monitor/controllers.state`) | none | implemented |
| `game.events` | per instance the game's events `{available, profile, frame, seq, events {<name>: {kind, fires, last_frame, level, value}}, recent [{seq, frame, name}]}` (`GET /api/v1/monitor/game.events`); a new value when an event fires or a watched value changes, coalesced at `max_hz`: `recent` holds the fired ones between two messages | none | implemented |
| `debug.asm_sync` | the asm synchroniser's watch: `asm_sync_*` (the `asm_sync` topic of `/api/v1/websocket`) | none; no token needed from this host | implemented |

- `subscribe` takes names or prefixes (`ttd.*`, `*`) and adds to what the connection has; `unsubscribe` removes.
  Each is answered `subscribed` / `unsubscribed` with the connection's topics. An unknown name is answered
  `{"op":"error","t":"<name>","error":"Not Found"}` once and ignored; `surface` in `topics` is a `bad_request`
  (`join` opens it).
- A state topic sends each instance's value at once on `subscribe`, then only values that changed, one `state`
  message per instance, at most `max_hz` times a second (1 … 50, default 10, per connection). `{"op":"gone"}` tells an
  instance went.
- A prefix such as `debug.*` from a peer without a token (a loopback debugger) takes the event topics and skips the
  state topics it may not have, without an error; a name it may not have is still `companion_required`.
- A topic of the process (`streamdeck.status`, `host.stats`, `host.clients`) sends one `state` message without `instance`:
  `{"t":"streamdeck.status","op":"state","value":{…}}`, at once on `subscribe`, then on change.
- Messages of the hub itself (`subscribed`, `unsubscribed`, their errors, `revoked` before a `join`, `slow_consumer`)
  carry `t: "ws"`; those of the calls (`result`, `auth`) `t: "call"`.

Each module adds its topics here (automation planes guidelines §3).

### 3.1 The surface topic

The control surfaces protocol ([protocol.md](../../../inprogress/2026-10-18-control-surfaces/protocol.md)): a process
(a Stream Deck daemon, a test) joins as one surface and acts by action id.

```json
→ {"op":"join","v":1,"name":"streamdeck-bridge","model":"elgato.streamdeck.plusxl","target":"follow"}
← {"op":"welcome","t":"surface","v":1,"session":"s7","instance":"0f3a…","context":"play","scopes":["run","input",…],"target":{"instance":"0f3a…","index":0,"model":"PENTAGON","ram_kb":128,"view":null,"mode":"follow"},"readouts":["ttd.position","run.state"]}
→ {"op":"subscribe","readouts":["ttd.position","run.state"],"max_hz":50}
← {"op":"readout","t":"surface","instance":"0f3a…","values":{"ttd.position":{…},"run.state":{…}}}
→ {"op":"action","id":"ctx.set","value":"ttd","rid":40}
← {"op":"ctx","t":"surface","instance":"0f3a…","context":"ttd","by":"companion:c-3f9a01b2c4d5"}
← {"op":"result","t":"surface","rid":40,"ok":true,"body":{"context":"ttd",…}}
→ {"op":"action","id":"ttd.seek_by","value":{"d":-3,"rate":18.5,"unit":"frames"},"rid":41}
← {"op":"result","t":"surface","rid":41,"ok":true,"body":{"arrived_at":{…},"marks":[…],…}}
→ {"op":"bye"}
```

| From the client | |
|:--|:--|
| `join` | `v` (≥ 1), `name`, `model`, `serial`?, `target`? (`"follow"`, default: the followed instance of `TargetState` (the front view's active instance, else the selected one, else the only one: the same as `instances` `followed` and `GET /api/v1/surfaces/target`), followed; `{"instance": id or index}`: pinned; `{"view": …}`: `not_now` until views come to the topic), `features`? (`activity`, `catalogue`; `target` is always pushed), `rid`? (echoed by `welcome` or the error). Several per connection, one per `serial` (the Stream Deck daemon joins once per deck on one socket); a second join with a serial the connection joined already: `bad_request`. |
| `action` | `id`, `value`?, `rid`?, `session`?. Value by the action's kind: press `{"p": bool}` / `true`; delta `{"d": n, "rate": r, …params}` / a number; level `{"l": x}` / a number; text a string. The REST spellings (`pressed`, `delta`, `level`, `params`) too. Runs on the instance's actions worker with the companion as origin: the same gate as `POST /api/v1/controllers/actions/{id}` with its token. |
| `subscribe` / `unsubscribe` | `readouts[]`, `max_hz` (1 … 50), `session`?. The names of the readout vocabulary ([Stream Deck protocol §3](../../../inprogress/2026-10-19-streamdeck-integration/protocol.md#3-readouts), `readouts.h`): `ttd.position`, `run.state`, `machine`, `media.tape`, `media.disk`, `audio.mute`, `debug.mode`, the monitoring names of the topic table (`host.stats`, `emulator.stats`, `debug.breakpoints`, …), `video.window` (the GUI window's display settings as it published them: GPU, CRT, blending, full screen, scale, HUD, keyboard, ...; `available: false` headless), `video.crt_params` (the active CRT profile's parameter values), `video.viewport` (overscan and its crop), `toggle.<id>` (every toggle of the catalogue; `toggle.feature.<name>` per feature, `toggle.set.<name>`, `toggle.hud.notice.<id>`, `toggle.view.next_layer.<name>` by name), and prefixes (`media.*`, `toggle.*`, `*`); expanded again against the instance after a rebind. |
| `bye` | `session`?: with one, that surface ends and the socket stays; without, every surface of the connection ends and the socket closes. |

`session` names the surface an op is for (the id its `welcome` gave); it may be left out while the connection has one
surface (`bad_request` with several). Every message of a surface carries its `session`.

| From the core | |
|:--|:--|
| `welcome` | `v`, `session`, `rid` (the join's), `serial`, `instance`, `context`, `scopes[]`, `target`, `readouts[]` (every name the bound instance has), `features[]` (those taken), `catalogue_rev`. |
| `result` | `rid`, `ok`, `error`?, `message`?, `body` (the action's result; `ttd.seek_by` has `marks`). Only for an action with a `rid`; a failure without a `rid` is an `error`. Deltas queued behind a running one merge; every merged `rid` gets the merged result. |
| `ctx` | `instance`, `context`, `by`: the bound instance's context changed (a `ctx.set` anywhere: `by` names the setter; the default flipping when a TTD history appears or goes: `by` `default`), and after a rebind. |
| `readout` | `instance`, `values{}`: in full after `subscribe` and after a rebind, then only the values that changed, at most `max_hz`. |
| `target` | `instance` (null: none), `index`, `model`, `ram_kb`, `view` (null), `mode` (`follow` / `instance`): the binding changed (the followed instance changed: a view got the focus or changed its active instance, `NC_SURFACE_TARGET`, or the selection; a pinned one went). |
| `activity` | With `features: ["activity"]`: an action another surface (or the GUI, a pad, a script) ran on the bound instance, as `GET /api/v1/surfaces/activity` lists it: `seq`, `instance`, `id`, `value`, `origin`, `surface` (who), `ok`, `error`?, `message`?, `t_ms`, `merged`. Never the surface's own actions (`ActionOrigin::surface`, the session that called), at most 20 a second per surface (`dropped`: how many were not sent before this one, over the rate or while the connection was congested). |
| `catalogue` | With `features: ["catalogue"]`: `rev`, the action catalogue changed (`NC_ACTION_CATALOGUE`: the Qt client registered or dropped its GUI actions). |
| `revoked` | The companion was revoked; the socket closes. |
| `error` | `error`, `message`, `rid`?, `session`?: `bad_request` (wrong field, `join` missing, a surface not named while there are several), `companion_required`, `companion_unknown`, `companion_not_for_agents`, `not_now`, `Not Found` (also an unknown `session`), or an action's refusal (`companion_forbidden`, `held_by_session`, `not_now`, the route's phrase). |

### 3.2 The instances topic

What `/api/v1/companions/events` sends ([companion protocol §4.1](../../../inprogress/2026-10-16-companion-client/protocol.md#41-events)),
as a topic: the Stream Deck daemon needs it beside the `surface` topic on one connection with one token.

```json
→ {"op":"subscribe","topics":["instances"]}
← {"op":"subscribed","t":"ws","topics":["instances"],"max_hz":10}
← {"t":"instances","op":"hello","seq":1,"companion":{…},"emulators":[{"id":"0f3a…","state":"paused","control":{…},"actions":{…},…}]}
← {"t":"instances","op":"emulator","seq":2,"emulator":{"id":"0f3a…","state":"running",…}}
← {"t":"instances","op":"emulator_gone","seq":3,"id":"4d7f…"}
→ {"op":"snapshot"}
← {"t":"instances","op":"snapshot","seq":4,"companion":{…},"emulators":[…]}
```

- `hello` (after `subscribe`) and `snapshot` (after `{"op":"snapshot"}`) are the whole view, the body of
  `GET /api/v1/companions/me`; `emulator` an instance entry that changed (state, run control, actions, model,
  selected) or a new instance; `emulator_gone` an instance closed. `seq` counts the connection's `instances`
  messages from 1. A revoked companion is told `revoked` (`t: "ws"`) and closed.
- An entry is sent only when it differs from the one last sent on this connection. The hub compares on every
  notification that can change one (state, run control, features, companions, instances, the selection) and once a
  second for what changes with time alone (a re-take cooldown, the first recorded frame), as the events socket.
- `snapshot` without the topic: `bad_request`.

### 3.3 The debug topics

DebugEventHub's events ([debugger protocol §5](../../../inprogress/2026-09-28-debugger-model/protocol.md)), the
`debug` and `asm_sync` topics of `/api/v1/websocket`, as topics `debug.events` and `debug.asm_sync`:

```json
→ {"op":"subscribe","topics":["debug.*"]}
← {"op":"subscribed","t":"ws","topics":["debug.asm_sync","debug.events"],"max_hz":10}
← {"t":"debug.events","op":"event","event":"paused","reason":"breakpoint","breakpoint_id":3,"address":32773,"access":"execute","seq":12,"instance":"0f3a…","emulator":"0f3a…","cpu":"main"}
```

- The fields of the old socket's `event`, with `t` instead of `topic` and `instance` beside `emulator`. `seq` is the
  same number the old socket sends (per instance, one numbering for both sockets).
- Every instance's events; a client that wants one filters by `instance` (the old socket's `emulator` filter is
  not taken).
- An event stream, not a value: queued per connection (256 at most), the oldest dropped first; the next event sent
  carries `"dropped": n` ([§4](#4-delivery-and-threads)).

## 4. Delivery and threads

- State topics, `instances` and readouts keep the latest value, coalesced at `max_hz` (≤ 50 Hz). Event topics
  (`debug.*`; `activity` with SD4) are bounded: when a client falls behind, the oldest events are dropped and the
  next event says how many (`dropped`).
- Served on the WebAPI's event loops with asynchronous I/O: no thread per connection, per topic or for the hub.
  Producers (the message centre, the actions workers, DebugEventHub) post a task to the WebAPI's main loop (a
  debugger event is queued, at most 1024 waiting, and serialised once on the loop); one timer on it, armed
  only while somebody subscribes and at the highest `max_hz` asked for, reads published state only (the TTD cursor's
  seqlock, the run state, the context: never a read that holds the emulation thread), builds each value's JSON once
  per tick, and sends each connection what changed when its own interval passed. A state topic's message of one
  instance is the same bytes for every subscriber.
- `result`, `ctx`, `target` and `error` are sent as they happen, never behind a readout tick.

### 4.1 Backpressure

Per connection, on the bytes its socket has queued (the TCP connection's own buffer, behind what the kernel takes):

- **Above the high-water mark** (256 KiB; `UNREAL_WS_HIGH_WATER_BYTES`) the hub sends control messages only:
  `subscribed` / `unsubscribed`, `error`, `result`, `ctx`, `target`, `revoked`. State topics, readouts and
  `instances` are held and send their latest values once it drains (a value is replaced, never queued); events
  queue (256 per connection) and drop the oldest, the first one sent after says `dropped`. The producers never block.
- **Above it longer than the limit** (10 s; `UNREAL_WS_SLOW_CONSUMER_MS`):
  `{"op":"error","t":"ws","error":"slow_consumer","message":"… bytes queued …"}`, then the socket is closed (a close
  frame; dropped 2 s later when the client does not answer it).
- **How it is measured.** Drogon's `WebSocketConnection` does not expose its queue, but the upgraded request still
  holds the trantor `TcpConnection` (`HttpRequest::getConnectionPtr()`), and trantor calls its high-water mark
  callback on a send that leaves more than the mark queued. That sets the connection congested. trantor's
  `TcpConnectionImpl` never calls the write-complete callback it declares and has no queue size getter, so while congested the hub's tick asks for a
  probe: a ping frame (2 bytes) sent on the connection's own loop behind everything queued; when that send does not
  fire the callback the queue is below the mark and the connection drains. The measure runs on the connection's
  I/O loop; the hub reads two atomics (`WsFlow`).
- `max_hz` bounds the rate before that; the hub keeps one value per topic and instance, not a queue.

## 5. The older sockets

`/api/v1/websocket` and `/api/v1/companions/events` keep their contracts and keep serving: they are not deprecated.
The `debug.*` topics take DebugEventHub's events through a sink (one numbering for both sockets); the `instances`
topic builds the same view as the events socket (`CompanionMeJson`). Their clients (the recipes, `index.html`,
`tools/verification/mcp/test_companion.py`) may move to `/api/v1/ws` at their own pace.
