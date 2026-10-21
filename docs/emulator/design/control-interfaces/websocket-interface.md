# WebSocket Interface

| | |
|---|---|
| **Status** | 🔧 In progress. Calls (§2) and auth (§1) are implemented (2026-10-20, `core/automation/webapi/src/api-websocket.cpp`); the topic mechanism with the state topics `run.state`, `ttd.position`, `surfaces.context` and the `surface` topic (§3) are implemented (2026-10-20, control surfaces S4, `ws-hub.*`, `surface-hub.*`); the `instances` and `debug.*` topics (§3.2, §3.3) and backpressure (§4.1) too (2026-10-21). Still to come: the Stream Deck SD4 additions of the `surface` topic (`activity`, `catalogue`, more readouts, views). |
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
  the implicit session for a call, the `result` carries it as `session`, for the next calls.
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
| `run.state` | per instance `{state: running \| paused \| stopped, speed, turbo}` | none (a read) | implemented |
| `ttd.position` | per instance the TTD published cursor `{frame, tinframe, first, end, end_tinframe, state, history, paused, fps}` (the `ttd position` verb's bounds) | none | implemented |
| `surfaces.context` | per instance the surface context `{context, set, by, default}` (`GET /api/v1/surfaces/context`) | none | implemented |
| `surface` | control surfaces: opened by `join` (not by `subscribe`): `action`, `result`, `ctx`, readouts, `target`, `revoked` ([below](#31-the-surface-topic)) | per action | implemented (S4); `activity`, `catalogue`, the readout vocabulary and views: Stream Deck SD4 |
| `instances` | the companion's live view of the instances (`hello`, `emulator`, `emulator_gone`, `snapshot`): `/api/v1/companions/events` on this socket ([§3.2](#32-the-instances-topic)) | none (a companion's own view) | implemented |
| `debug.events` | debugger events: `paused`, `resumed`, `step_done`, `breakpoints_changed` (the `debug` topic of `/api/v1/websocket`, [§3.3](#33-the-debug-topics)) | none; no token needed from this host | implemented |
| `debug.asm_sync` | the asm synchroniser's watch: `asm_sync_*` (the `asm_sync` topic of `/api/v1/websocket`) | none; no token needed from this host | implemented |

- `subscribe` takes names or prefixes (`ttd.*`, `*`) and adds to what the connection has; `unsubscribe` removes.
  Each is answered `subscribed` / `unsubscribed` with the connection's topics. An unknown name is answered
  `{"op":"error","t":"<name>","error":"Not Found"}` once and ignored; `surface` in `topics` is a `bad_request`
  (`join` opens it).
- A state topic sends each instance's value at once on `subscribe`, then only values that changed, one `state`
  message per instance, at most `max_hz` times a second (1 … 50, default 10, per connection). `{"op":"gone"}` tells an
  instance went.
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
| `join` | `v` (≥ 1), `name`, `model`, `serial`?, `target`? (`"follow"`, default: the selected instance, else the only one, followed; `{"instance": id or index}`: pinned; `{"view": …}`: `not_now` until Stream Deck SD4). One per connection. |
| `action` | `id`, `value`?, `rid`?. Value by the action's kind: press `{"p": bool}` / `true`; delta `{"d": n, "rate": r, …params}` / a number; level `{"l": x}` / a number; text a string. The REST spellings (`pressed`, `delta`, `level`, `params`) too. Runs on the instance's actions worker with the companion as origin: the same gate as `POST /api/v1/controllers/actions/{id}` with its token. |
| `subscribe` / `unsubscribe` | `readouts[]` (`ttd.position`, `run.state`, prefixes), `max_hz` (1 … 50). |
| `bye` | Ends the surface and closes the socket. |

| From the core | |
|:--|:--|
| `welcome` | `v`, `session`, `instance`, `context`, `scopes[]`, `target`, `readouts[]`. |
| `result` | `rid`, `ok`, `error`?, `message`?, `body` (the action's result; `ttd.seek_by` has `marks`). Only for an action with a `rid`; a failure without a `rid` is an `error`. Deltas queued behind a running one merge; every merged `rid` gets the merged result. |
| `ctx` | `instance`, `context`, `by`: the bound instance's context changed (a `ctx.set` anywhere: `by` names the setter; the default flipping when a TTD history appears or goes: `by` `default`), and after a rebind. |
| `readout` | `instance`, `values{}`: in full after `subscribe` and after a rebind, then only the values that changed, at most `max_hz`. |
| `target` | `instance` (null: none), `index`, `model`, `ram_kb`, `view` (null), `mode` (`follow` / `instance`): the binding changed (the followed instance changed, a pinned one went). |
| `revoked` | The companion was revoked; the socket closes. |
| `error` | `error`, `message`, `rid`?: `bad_request` (wrong field, `join` missing), `companion_required`, `companion_unknown`, `companion_not_for_agents`, `not_now`, `Not Found`, or an action's refusal (`companion_forbidden`, `held_by_session`, `not_now`, the route's phrase). |

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
