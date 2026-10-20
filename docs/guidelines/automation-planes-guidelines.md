# Automation planes guidelines

Every emulator capability is reachable from **every automation plane**: CLI, Python, Lua, MCP, REST (WebAPI) and
WebSocket. A module never adds a verb to one plane only. The WebSocket plane comes with the REST one by
construction, and the module adds only what changes on its own.

## 1. The planes

| Plane | Transport | Plane document |
|:--|:--|:--|
| CLI | TCP text | [cli-interface.md](../emulator/design/control-interfaces/cli-interface.md) |
| Python | in process (pybind11) | [python-interface.md](../emulator/design/control-interfaces/python-interface.md) |
| Lua | in process (sol2) | [lua-interface.md](../emulator/design/control-interfaces/lua-interface.md) |
| MCP | Streamable HTTP, smart tools | [docs/features/mcp/README.md](../features/mcp/README.md) |
| REST | HTTP/1.1 JSON | [webapi-interface.md](../emulator/design/control-interfaces/webapi-interface.md) |
| WebSocket | `/api/v1/ws` on the WebAPI port | [websocket-interface.md](../emulator/design/control-interfaces/websocket-interface.md) |

**WebSocket adds to REST; it does not replace or deprecate it.** REST stays a first-class API. A client uses whatever
suits it: request and answer over REST, or the same calls plus push over one socket.

## 2. The documentation chain

A capability is documented top-down, in this order, in the same change as the code:

1. **[command-interface.md](../emulator/design/control-interfaces/command-interface.md)**: the verb, its arguments,
   its semantics and its status, protocol-agnostic. One row per verb.
2. **The module's document**: the design or reference of the subsystem (for example
   `docs/emulator/design/<area>/…`, or the `surfaces` / `automation` page of an in-progress design). It shows
   the verb on **every** plane: the CLI line, the Python and Lua calls, the MCP tool, the REST route, and the WebSocket
   `call` plus the topic that pushes its changes.
3. **Each plane's document** (§1): the plane-specific details, such as argument encoding, errors, examples and limits.

A module page without the WebSocket line is as incomplete as one without the REST line.

## 3. What a module provides on WebSocket

| What | How | Module work |
|:--|:--|:--|
| **Calls** | `{"op":"call","rid":…,"method":"GET","path":"/api/v1/…","body":…}` runs the **same handler** as REST: same scopes, same answer, same errors, in a `result` | none: every REST route is a WebSocket call automatically |
| **Changes** | a **topic** (`<module>.<what>`, e.g. `media.tape`, `debug.breakpoints`) that pushes the state, or the event, that a client would otherwise poll | one publisher per topic, plus a row in the topic table of websocket-interface.md |
| **Actions** | the `surface` topic (`join`, `action`, readouts) for control surfaces | register the action in the `ActionRegistry`; nothing per plane |

Rules for topics:

- **State that a UI or a companion shows gets a topic.** Examples: run state, media progress, the TTD cursor,
  breakpoints, slots, controllers. If a REST GET would be polled, the module publishes the change instead. Clients
  never have to poll.
- **Publishing is cheap and never waits.** The producer, which may be the emulation thread, stores the latest value,
  or appends an event to a bounded ring, and wakes the hub. It does not serialise JSON per client, does not touch a
  socket and never blocks (§4).
- **Rate.** State is latest-value and coalesced, limited by the subscriber's `max_hz` (≤ 50 Hz). Events are
  bounded; when a client falls behind, the oldest are dropped and the next message says how many (`dropped`).
- **Scopes.** A topic needs the same companion scope as the REST route that reads the same state.

## 4. Threads and I/O

The WebSocket plane runs on the WebAPI's existing event loops: Drogon / trantor, non-blocking, two threads
(`setThreadNum(2)`).

- No thread per connection, per topic, per hub or per module; the thread count does not grow with clients.
- A `call` whose handler is slow, or touches an emulator, goes to the work the REST handler already uses. It never
  runs long on an I/O thread.
- One timer on the loop drives every readout. It is armed only while someone subscribes.
- Backpressure per connection uses the high-water mark of queued bytes. A client that stays over it is closed with
  `slow_consumer`.

Out-of-process companions follow the same rule on their side: all networking on one event-loop thread.

## 5. Checklist for a new verb or module

- [ ] `command-interface.md` row (status per plane)
- [ ] Module document: CLI, Python, Lua, MCP, REST, WebSocket (`call` + topic) side by side
- [ ] Plane documents updated
- [ ] A topic for every piece of state a client would poll, added to the topic table
- [ ] Tests:
  - per plane, as each plane's guidelines ask;
  - a WebSocket `call` of the route answers the same as REST (the generic conformance test covers every route);
  - a topic test: change, then the push arrives, coalesced at the rate;
  - MCP by [mcp-testing-guidelines.md](mcp-testing-guidelines.md).

Related: [new-platform-guidelines.md §4](new-platform-guidelines.md#4-every-plane-sees-the-new-thing-for-free) (no
private endpoints for media and cards), [performance-guidelines.md](performance-guidelines.md) (reads never park
the emulation thread).
