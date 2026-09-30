# MCP Bridge Multi-Instance Support — Proposal

## Problem

An agent driving unreal-ng today can only address **one running emulator process**
at a time. `unreal-mcp-bridge` (the stdio↔HTTP adapter Claude Desktop/other MCP
clients spawn) is hardcoded to a single URL
(`core/automation/mcp/bridge/src/main.cpp:22`, default
`http://127.0.0.1:8092/mcp`, overridable only via `--url` / `UNREAL_MCP_URL` at
process launch). The in-process MCP server it forwards to talks to its
co-located WebAPI over loopback only
(`core/automation/mcp/src/webapi-client.cpp:3-7,38-45`, hardcoded port 8090,
overridable via `UNREAL_WEBAPI_PORT`) — by design, so a second instance's MCP
never talks to the instance that owns 8090.

The existing `target` parameter on every MCP tool
(`core/automation/mcp/src/target-resolver.h`) resolves **emulator objects within
one process** (WebAPI `id`s from `POST /api/v1/emulator/start`), not separate OS
processes. It already refuses to guess when more than one emulator object
exists in-process, forcing disambiguation — the same pattern this proposal
extends one level up.

Running a second `unreal-qt` side by side (e.g. for A/B testing a fix, or
running two different machine families at once) already works at the process
level today via `UNREAL_WEBAPI_PORT=8191` (see
`docs/inprogress/2026-09-29-neogs-bringup/neogs-bringup.md:937`,
`docs/inprogress/2026-09-27-zxpoly/platform-bringup.md:666`), but nothing in
the MCP layer can address that second process — an agent has to know to
restart the bridge with a different `--url`/env var, and cannot list what is
even running.

Two smaller gaps make this worse:
- The MCP server's own listen port (8092,
  `core/automation/mcp/src/automation-mcp.cpp:107`) has **no env-var override**
  at all, unlike the WebAPI (8090) and CLI (8765) ports, which both follow the
  `getenv("UNREAL_..._PORT")` + range-checked `strtol` pattern
  (`core/automation/webapi/src/automation-webapi.cpp:190-198`,
  `core/automation/cli/src/automation-cli.cpp:78-85`). A second process today
  cannot be given its own MCP port without this fix — it can only be reached
  via its WebAPI port (`/mcp` also rides on 8090, since it is the same drogon
  app instance), which conflates the two ports' semantics.
- `tools/python/emulator_discovery.py` already implements list+select UX for
  emulator objects within one process (`discover_emulators`, `select_emulator`,
  `--host`/`--port`/`-e/--emulator`) — a template worth mirroring rather than
  inventing new conventions.

## Goals

1. An agent can ask the MCP bridge **"what emulator processes are currently
   reachable"** and get back a list (host, port, reachability, basic identity
   — e.g. git commit / model — pulled from each candidate's
   `GET /api/v1/emulator/status`).
2. An agent can **select** which process subsequent tool calls target, without
   restarting the bridge or losing MCP session state.
3. The existing in-process `target`/emulator-id mechanism is preserved
   unchanged and stays scoped *within* whichever process is currently
   selected — this proposal adds a layer above it, not a replacement.
4. The MCP server's dedicated listen port becomes configurable
   (`UNREAL_MCP_PORT`), closing the prerequisite gap that prevents two
   full processes from having independently addressable MCP endpoints.
5. Naming/UX matches existing MCP conventions: a single verb-dispatch tool
   (cf. `emulator_manage`, `manage_symbols`) rather than two new top-level
   tools, and list/select semantics mirroring
   `tools/python/emulator_discovery.py`.

## Non-Goals

- Auto-discovery of arbitrary processes on the LAN/host (e.g. port-scanning).
  Candidates are supplied explicitly (config file, env var, or an `add`
  action) — this proposal does not attempt to solve unauthenticated network
  discovery.
- Changing how the WebAPI itself models multiple `Emulator` objects
  (`target`/`id`) within one process — out of scope, already works.
- Spawning or killing OS processes from the bridge. Selecting an instance
  means redirecting HTTP calls to an already-running process's port, not
  process lifecycle management. (Process lifecycle could be a follow-up once
  this lands.)

## Proposed Design

### New MCP tool: `manage_instances`

Single tool, verb-dispatched via `action`, following the `emulator_manage`
convention:

| Action | Parameters | Behavior |
|--------|-----------|----------|
| `list` | — | Returns known candidate processes (from config + any added at runtime) with `host`, `port`, `reachable` (probed via `GET /api/v1/emulator/status`), and identity fields (`git_branch`, `git_commit`) when reachable. Marks which one is currently selected. |
| `add` | `host`, `port`, optional `label` | Registers a new candidate for this bridge session (in-memory; does not persist unless also written to the config file — see below). |
| `remove` | `host`+`port` or `label` | Drops a candidate from the in-memory list. Refuses to remove the currently selected instance without first selecting another. |
| `select` | `host`+`port` or `label` or list index | Makes this instance the target for all subsequent tool calls in the session. Errors with the current candidate list if the target is unreachable at selection time (fail fast, not on first real call). |

`list`'s indexed/labeled output mirrors `emulator_discovery.py`'s
`select_emulator` UX (`[1] host:port — model/branch (reachable)`).

### Where "selected instance" lives

The bridge (`bridge/src/main.cpp`) is the natural owner of "currently selected
base URL" state, since it already owns the single fixed URL it forwards to
today — `select` becomes a runtime mutation of that URL instead of requiring a
bridge restart with a new `--url`. This keeps the in-process MCP server
(`automation-mcp.cpp`, `mcp-tools.cpp`) unaware of multi-instance concerns
entirely: it still only ever knows its own loopback WebAPI. The bridge
intercepts `manage_instances` calls itself (or proxies `list`/`select` locally
while still forwarding everything else), rather than adding multi-instance
awareness inside the in-process tool registry.

This needs a design decision recorded before implementation starts (see
Open Questions): does `manage_instances` live entirely in the thin bridge, or
does the bridge need a minimal JSON-RPC intercept layer it doesn't have today
(it is currently a dumb line forwarder, `bridge/src/main.cpp`)?

### Candidate discovery source

A simple config file (e.g. `~/.unreal-ng/mcp-instances.json` or a path from
`UNREAL_MCP_INSTANCES`), each entry `{host, port, label}`, loaded at bridge
startup and mutable at runtime via `add`/`remove`. Falls back to a single
entry for the current default (`127.0.0.1:8092`) when no config exists, so
existing single-instance workflows are unaffected.

### Port configurability fix (prerequisite)

Add `UNREAL_MCP_PORT` env-var override to `automation-mcp.cpp:107`, following
the exact `getenv` + `strtol` + range-check pattern already used for
`UNREAL_WEBAPI_PORT`/`UNREAL_CLI_PORT`. Without this, running a second full
`unreal-qt` process with its own independently-addressable MCP endpoint
(distinct from riding on its WebAPI port) is not possible, which undercuts
the value of `manage_instances` for anyone who wants the dedicated MCP
listener rather than `/mcp` on the WebAPI port.

## Documentation Impact

- `docs/features/mcp/README.md` — add `manage_instances` to the tool list.
- `.recipe/README.md` and a new `.recipe/mcp/multi-instance.md` recipe —
  worked example: config file format, `list`/`select` walkthrough, and when to
  use this vs. the existing `target` parameter (avoid agents conflating the
  two disambiguation mechanisms).
- `docs/emulator/design/control-interfaces/cli-interface.md` — note the new
  `UNREAL_MCP_PORT` env var alongside the existing `UNREAL_WEBAPI_PORT`/
  `UNREAL_CLI_PORT` table.
- `core/automation/mcp/README.md` — document the bridge's new stateful
  "selected instance" behavior (currently it's stateless one-shot forwarding).

## Open Questions

1. Does `manage_instances` intercept happen in the bridge (`bridge/src/main.cpp`,
   currently a dumb forwarder with no JSON-RPC parsing) or does the bridge grow
   a minimal JSON-RPC layer to special-case this one tool? The former is less
   invasive; the latter keeps all tool logic in one place
   (`core/automation/mcp/src/`).
2. Should `select` persist across bridge restarts (i.e. write back to the
   config file), or is selection always session-scoped? Leaning session-scoped
   for simplicity — an MCP client that wants persistence can pass an explicit
   default instance via config.
3. Is a config file the right discovery source for a first cut, or should
   `add`-at-runtime-only (no file) ship first and the config file follow once
   there's a concrete multi-instance workflow to validate against?

## Rough Phasing

1. `UNREAL_MCP_PORT` env-var override (small, independent, unblocks manual
   testing of the rest).
2. `manage_instances` with `list`/`select`/`add`/`remove` against an in-memory
   candidate list (no config file yet) — bridge intercepts and rewrites its
   forwarding target.
3. Config-file-backed candidate persistence, once the in-memory version is
   validated in real use.
4. Documentation pass (README, recipe, control-interfaces doc) alongside each
   phase, not deferred to the end.

## References

- `core/automation/mcp/bridge/src/main.cpp` — bridge URL resolution
- `core/automation/mcp/src/webapi-client.cpp` — loopback WebAPI port handling
- `core/automation/mcp/src/automation-mcp.cpp:107` — hardcoded MCP listen port
- `core/automation/mcp/src/target-resolver.h` — existing in-process
  disambiguation this proposal layers above
- `core/automation/webapi/src/automation-webapi.cpp:190-198`,
  `core/automation/cli/src/automation-cli.cpp:78-85` — env-var port override
  pattern to mirror
- `tools/python/emulator_discovery.py` — list/select UX template
- `docs/emulator/design/control-interfaces/cli-interface.md:188-191` —
  existing (undocumented-for-MCP) multi-process port pattern
