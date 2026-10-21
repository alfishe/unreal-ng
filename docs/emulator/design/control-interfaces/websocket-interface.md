# WebSocket Interface

| | |
|---|---|
| **Status** | 🔧 In progress. Calls (§2) and auth (§1) are implemented (2026-10-20, `core/automation/webapi/src/api-websocket.cpp`); topics (§3) are planned. Two older sockets are implemented and become adapters. |
| **Endpoint** | `GET /api/v1/ws` (WebSocket upgrade on the WebAPI port) |
| **Rules** | [automation-planes-guidelines.md](../../../guidelines/automation-planes-guidelines.md) |
| **Design** | [Stream Deck integration protocol §0](../../../inprogress/2026-10-19-streamdeck-integration/protocol.md#0-transport-one-websocket) |

One socket for every client. It **adds to REST** and replaces nothing. Every REST route can also be called on the
socket, and the socket also pushes state changes, so clients never poll.

## 1. Connection and auth

- The companion token on the handshake (`Authorization: Bearer unc_…`), or the first message `{"op":"auth","token":…}`
  for clients that cannot set headers.
- Without a token: localhost only, and only the `debug.*` topics, as `/api/v1/websocket` today.
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

The surface targets, readouts and activity (Stream Deck SD4: `GET /api/v1/surfaces/views`, `target`, `sessions`,
`PUT …/sessions/{id}/pin`, `readouts`, `activity`) are calls as well; the `surface` topic (S4) pushes `target` on
`NC_SURFACE_TARGET`, the readouts and `activity` from the core's `ActivityFeed`.

## 3. Topics

```json
→ {"op":"subscribe","topics":["instances","run.state","media.*"],"max_hz":20}
← {"t":"run.state","op":"state","instance":"0f3a…","value":{"state":"paused","speed":1,"turbo":false}}
```

| Topic | Pushes | Today |
|:--|:--|:--|
| `instances` | the companion's live view of instances (`hello`, `emulator`, `emulator_gone`, `revoked`) | `/api/v1/companions/events` |
| `debug.*` | debugger events: state change, CPU step, breakpoints, asm sync | `/api/v1/websocket` |
| `surface` | control surfaces: `join`, `action`, `ctx`, readouts, `target`, `activity` | designed (control surfaces, Stream Deck integration) |

Each module adds its topics here (automation planes guidelines §3).

## 4. Delivery and threads

- State topics keep the latest value, coalesced at `max_hz` (≤ 50 Hz).
- Event topics are bounded. When a client falls behind, the oldest events are dropped and the next message says how
  many (`dropped`).
- `result`, `ctx`, `target` and `error` are never queued behind streams.
- Served on the WebAPI's event loops with asynchronous I/O: no thread per connection or topic. A client that stays
  over the per-connection high-water mark is closed with `slow_consumer`.

## 5. The older sockets

`/api/v1/websocket` and `/api/v1/companions/events` keep their contracts as adapters over the same hub. Their
clients (the recipes, `index.html`, `tools/verification/mcp/test_companion.py`) may move to `/api/v1/ws` at their
own pace.
