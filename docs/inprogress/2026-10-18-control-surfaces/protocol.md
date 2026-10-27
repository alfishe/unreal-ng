# Control surfaces: the surface protocol

| | |
|---|---|
| **Date** | 2026-10-18 |
| **Status** | v1 built 2026-10-20 (S4): [§10](#10-as-built-s4-2026-10-20) |
| **Proposals** | P4, P5, P6, P7 in [README.md](README.md#proposals-for-the-owner-2026-10-18) |
| **Related** | [companion protocol](../2026-10-16-companion-client/protocol.md) (pairing, the events WebSocket), `core/automation/webapi/src/companion-event-hub.cpp` |

## Contents

- [1. Scope](#1-scope)
- [2. Why not the existing routes](#2-why-not-the-existing-routes)
- [3. Messages](#3-messages)
- [4. Transport](#4-transport)
- [5. Auth](#5-auth)
- [6. Coalescing](#6-coalescing)
- [7. Errors](#7-errors)
- [8. Versioning](#8-versioning)
- [9. Example](#9-example)
- [10. As built (S4, 2026-10-20)](#10-as-built-s4-2026-10-20)

## 1. Scope

| Who | How |
|:--|:--|
| A **physical device** on a host bus (USB, Bluetooth) | A controllers-library backend. HID is the transport-neutral device protocol (USB, BLE, I²C) and has the usages (Dial `0x01:0x37`, Haptics page `0x0E`). MIDI later. |
| A **process** (`streamdeck-bridge`, an app, a test) | This protocol, on a WebSocket of the WebAPI, as a paired companion ([CC D5](../2026-10-16-companion-client/README.md#decisions-2026-10-16): companions act on the WebAPI and the CLI). |

v1 is **actions mode**: the process decides what its controls do (the Stream Deck scenario does) and calls action
ids with values. Raw controls from a process, bound by a profile in the core, are later ([TODO.md](TODO.md#later)):
they need a backend for sessions and a trust rule for the `model` a client claims.

## 2. Why not the existing routes

| Route | Missing |
|:--|:--|
| The REST routes (today's bridge) | One request per step; no push: the bridge polls `/ttd/position` at 50 Hz; no shared context. |
| `/api/v1/companions/events` | Server to client only, status (`hello`, `emulator`, `snapshot`, ...), no calls. |

The surface protocol is the `surface` topic of the one WebSocket `/api/v1/ws` (Stream Deck integration D7,
[protocol.md §0](../2026-10-19-streamdeck-integration/protocol.md#0-transport-one-websocket); erratum 2026-10-20: no
own route): same auth and error shape as the companion events socket, whose contract stays.

## 3. Messages

JSON objects with `op`, one per WebSocket text frame.

### 3.1 From the client

| op | Fields | Meaning |
|:--|:--|:--|
| `auth` | `token` | First message when the upgrade carried no `Authorization` header (as the events socket). |
| `join` | `v`, `name`, `model`, `serial`?, `target`? | Who I am; the target to bind (`follow`, a view or an instance, [targeting.md §4](../2026-10-19-streamdeck-integration/targeting.md#4-pins); else follow). Required before anything else but `auth`. (Not `hello`: the events socket's server already sends an op of that name.) |
| `action` | `id`, `value`?, `rid`? | Call an action. `value`: `{"p": true/false}`, `{"d": n, "rate": r, …params}`, `{"l": x}`, or a string. `rid` asks for a `result`. |
| `subscribe` | `readouts[]`, `max_hz` (≤ 50) | |
| `unsubscribe` | `readouts[]` | |
| `bye` | | Clean close. |

### 3.2 From the core

| op | Fields | Meaning |
|:--|:--|:--|
| `welcome` | `v`, `session`, `instance`, `context`, `scopes[]` | Accepted. |
| `result` | `rid`, `ok`, `error`?, `message`?, `body`? | The action's result ([actions.md](actions.md)); `ttd.seek_by` adds `marks`. |
| `ctx` | `instance`, `context`, `by` | The bound instance's context changed. |
| `readout` | `instance`, `values{}` | Changed readouts only. |
| `target` | `instance`, `index`, `model`, `view`, `mode` | The binding's target changed (replaces the earlier `instance` op, erratum 2026-10-20; [Stream Deck protocol §2](../2026-10-19-streamdeck-integration/protocol.md#2-target)). |
| `revoked` | | The companion was revoked; the core closes the socket (as the events socket). |
| `error` | `error`, `message`, `rid`? | [§7](#7-errors). |

## 4. Transport

- The `surface` topic of `GET /api/v1/ws`, the one WebSocket upgrade on the WebAPI port (D7; erratum 2026-10-20:
  not an own route `/api/v1/surfaces/ws`). Its calls (`{"op":"call"}`, every REST route) are implemented since S1
  (`api-websocket.cpp`); `SurfaceHub` becomes the topic's handler in the shared hub (S4).
- JSON through `StateNode` and jsoncpp (`emulator/state/statenodejson.h`).
- Ping / pong are WebSocket control frames.

## 5. Auth

The companion flow unchanged: pair once (`POST /api/v1/companions/pairings`, approval at the GUI,
[CC protocol §3](../2026-10-16-companion-client/protocol.md#3-pairing)); headless: `UNREAL_COMPANION_TOKENS`. The
socket takes the bearer token in the header or in `auth`. Each `action` goes through the registry with the
companion as origin, so `CheckAction` decides it with the action's scope, exactly as for REST. Revoking the
companion closes the socket.

## 6. Coalescing

- An `action` with a delta merges into the last queued call when it has the same id and params from the same
  session ([architecture §3](architecture.md#3-the-core-action-registry)); every merged `rid` gets the merged
  call's `result`. A client need not throttle.
- `readout` is sent at most at `max_hz`; when the socket is not writable, values are replaced, not queued.

## 7. Errors

The codes of the companion socket and of the routes, no new vocabulary:

| error | When |
|:--|:--|
| `bad_request` | Not a JSON object, unknown `op`, wrong field, `join` missing. Session goes on. |
| `companion_required` | No token yet. |
| `companion_unknown` | Token unknown or revoked; close. |
| `companion_not_for_agents` | The token is an agent's; agents never act as companions (CC D5). |
| `companion_forbidden` | The action's scope is not granted. |
| `held_by_session` | An agent holds the machine. |
| `not_now` | The action is not available now (`message` is the registry's reason). |
| `Not Found` | Unknown action id or instance (as REST answers 404). |
| the route's phrase | An executor's refusal as its route answers it: `TTDControlError` phrases (`Conflict`, `Bad Request`, ...), `Run-control held`. |

## 8. Versioning

`join.v` is the highest version the client speaks, `welcome.v` the one chosen. Additions are ignored by peers that
do not know them; a breaking change is `v: 2`, with `v: 1` kept for a release cycle.

## 9. Example

```json
→ {"op":"auth","token":"unc_…"}
→ {"op":"join","v":1,"name":"streamdeck-bridge","model":"elgato.streamdeck.plusxl"}
← {"op":"welcome","v":1,"session":"s7","instance":"0f3a…","context":"play","scopes":["run","input","media","machine","rewind","takeback","debug"]}
→ {"op":"subscribe","readouts":["ttd.position","run.state"],"max_hz":50}
→ {"op":"action","id":"ctx.set","value":"ttd"}
← {"op":"ctx","instance":"0f3a…","context":"ttd","by":"companion:c-3f9a01b2c4d5"}
→ {"op":"action","id":"ttd.seek_by","value":{"d":-3,"rate":18.5,"unit":"frames","min":1,"max":100},"rid":41}
← {"op":"result","rid":41,"ok":true,"body":{"reached":true,"arrived_at":{"frame":1834,"tinframe":0},"halt_reason":"target","state":"stopped","marks":[{"kind":"second","frame":1806}]}}
← {"op":"readout","instance":"0f3a…","values":{"ttd.position":{"frame":1834,"tinframe":0,"first":0,"end":9000,"state":"stopped","fps":48.83}}}
```

## 10. As built (S4, 2026-10-20)

`WsHub` (`core/automation/webapi/src/ws-hub.*`) is the shared hub of `/api/v1/ws`: connections, the one auth, the
topic mechanism and the readout timer; `SurfaceHub` (`surface-hub.*`) is the `surface` topic's handler in it. Calls
(`{"op":"call"}`) stay in `ApiWebSocket`. The plane's reference:
[websocket-interface.md §3.1](../../emulator/design/control-interfaces/websocket-interface.md#31-the-surface-topic).

| Part | As built |
|:--|:--|
| Messages | All of §3 but `target` from the client (`pin`, SD4). Every server message carries `t: "surface"`. `welcome` adds `target` and `readouts[]`. |
| Auth | The token of the handshake or of `auth`, checked at `join` (`companion_required`, `companion_unknown` + close, `companion_not_for_agents` for an `X-Unreal-Source: mcp` handshake). Revoking the companion (`NC_COMPANION_CHANGED`) sends `revoked` and closes. |
| `join.target` | `"follow"` (default): the WebAPI's rule, the selected instance else the only one, re-evaluated when instances come and go or the selection changes (then `target`, the new instance's `ctx` and a full `readout`); `{"instance": id or index}`: pinned, `target` with `instance: null` when it goes. `{"view": …}`: `not_now`. Views, `TargetState`, `pin`, `features`, `activity`, `catalogue`: Stream Deck SD4 / SD5. |
| `action` | The value forms of §3.1 and the REST ones; any other member of a value object is a parameter. `ActionRegistry::Submit` with origin Companion (identity: the companion's id), so the registry's companion gate decides exactly as for REST; coalescing is the registry's (§6). `result` only with a `rid`; a failure without one is an `error`. A value of the wrong kind is `bad_request` (with the `rid`). |
| `ctx` | On `NC_SURFACE_CONTEXT` for the bound instance, and after a rebind. The default flip of Q-7 is derived now: the TTD backends tell `ContextState::HistoryChanged` when their published cursor's `history` flips (`ttd::NoteHistoryChange`), and while no context is set it posts `NC_SURFACE_CONTEXT` with `by` `default`. |
| Readouts | `ttd.position` `{frame, tinframe, first, end, end_tinframe, state, history, paused, fps}` (the published cursor) and `run.state` `{state: running/paused/stopped, speed, turbo}`; names or prefixes; unknown: `Not Found` once. Full on `subscribe` and on a rebind, then changed values only, at most the session's `max_hz` (1 … 50, default 10). |
| Threads | No thread of its own: messages on the WebAPI's I/O loops, results and notifications posted to its main loop, one `runEvery` timer at the highest `max_hz`, armed only while somebody subscribes; each value read once per tick from published state and serialised once. |
| Hub topics | `run.state`, `ttd.position`, `surfaces.context` for clients that do not join (the planes guideline: state a client would poll is a topic). |
| Errors | §7's codes. `Not Found` also for an unknown readout or topic, `not_now` for a view target. |

Not built: the byte high-water mark and `slow_consumer` (drogon's `WebSocketConnection` does not expose its TCP
connection's queue; the rate is bounded by `max_hz` and the hub keeps latest values only), and the `instances` /
`debug.*` adapters of the older sockets. Tests: `WsHub_Test` (core-tests, the protocol at the unit level),
`tools/verification/mcp/test_ws_surface.py` (live, the conformance suite against a launched instance, with the 50 Hz
subscribers beside a running machine).

