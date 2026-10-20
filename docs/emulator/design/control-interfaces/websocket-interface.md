# WebSocket Interface

| | |
|---|---|
| **Status** | 🔮 Planned (decided 2026-10-20). Two older sockets are implemented and become adapters. |
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
