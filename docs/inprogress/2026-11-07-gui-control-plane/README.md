# GUI control plane

Status: design, 2026-11-07. No code yet.

Owner decisions (2026-11-07):

- Controlling a GUI is a **separate plane**, not an action of the core's automation planes. A GUI here means the
  debugger's views, the video wall's tiles or a window.
- One mechanism for every GUI program: the Qt client's embedded debugger, the standalone `unreal-debugger`, the video
  wall, the screen viewer, and later ones.
- The core does not know about it and does not depend on it.
- Design first, code after review.
- Transport: a local socket, plus an optional TCP listener on all interfaces, without a token for now (§4.8).
- A surface's GUI keys follow its core target (§4.5).
- The plane has a thin MCP server of its own, not tools in the core's MCP (§4.9).
- No aliases for the moved action ids: the shipped profiles move at once (§5).
- `TargetState` and `ContextState` stay in the core (§5).

## Contents

- [1. Why](#1-why)
- [2. What the core knows about GUIs today](#2-what-the-core-knows-about-guis-today)
- [3. Requirements](#3-requirements)
- [4. Design](#4-design)
- [5. Moving today's GUI parts out of the core](#5-moving-todays-gui-parts-out-of-the-core)
- [6. Phases](#6-phases)
- [7. Tests](#7-tests)
- [8. Open points](#8-open-points)

## 1. Why

The automation planes (CLI, Python, Lua, MCP, REST, WebSocket;
[automation planes guidelines](../../guidelines/automation-planes-guidelines.md)) drive the **emulator**: the
machine, its media, its debugger state. What a window shows is not emulator state. Today GUI control still goes
through the core in a few places (§2), and each of them only works for one program:

- `dbg.goto` reaches the Qt client's disassembly, never `unreal-debugger`'s.
- The video wall's single-sync switch is a core CLI command, a REST route and a core notification.
- A demo or a GUI test that needs the hex view at an address has no way to ask for it. The Zynaps demo
  ([software metadata profile](../2026-11-05-software-metadata-profile/README.md)) fell back to synthetic mouse
  clicks.

The GUI plane gives every GUI program the same way to be driven. Its clients:

- **inside the program:** the controller bindings of the Qt client (pads, ShuttlePRO, the Surface Dial; §4.7);
- **outside:**
  - control surfaces, such as the Stream Deck daemon's GUI keys;
  - demo and recording scripts;
  - GUI tests, which today post synthetic clicks;
  - one GUI program driving another, for example the video wall pointing a running debugger at a tile's instance.

Agents use the plane only to drive what a human sees: authoring a demo, checking a GUI change. They read the machine
through the core planes, and agent verbs never go to Qt ("Qt is for humans"). For that use the plane has its own thin
MCP server (§4.9).

## 2. What the core knows about GUIs today

| What | Where | Used by |
|:--|:--|:--|
| GUI action declarations: id, label, params, the `gui` flag. The Stream Deck catalogue lists them without the GUI running | `core/src/emulator/actions/catalogue.cpp` (the GUI table), `ActionInfo::gui` (`actionregistry.h`) | `unreal-qt/src/controllers/guiactions.cpp` registers the executors (the list in §5) |
| GUI actions reach every caller through the core registry | `POST /controllers/actions/{id}`, MCP `actions`, the `surface` topic; `NC_ACTION_CATALOGUE` when the Qt client registers them | the Stream Deck, demo scripts, **and the Qt client's own controller bindings** (`GuiActions::Trigger` → `ActionRegistry::Submit`), driven by the shipped profiles `data/controllers/profiles/debug/pad-default.yaml`, `debug/shuttle-pro.yaml`, `play/pad-default.yaml` |
| The debugger's cursors | `actions::DebuggerCursors` (`core/src/emulator/actions/steppingactions.cpp`): the Qt debugger writes the disassembly and memory cursors per instance | `run.to_cursor`, `ttd.find_last_write`, `ttd.find_last` take the cursor when `addr` is absent (`AddressOrCursor`) |
| Video wall single sync | CLI `videowall`, `POST /api/v1/videowall/singlesync`, `Automation::SetVideowallSingleSyncMode`, `NC_VIDEOWALL_SINGLE_SYNC_MODE` + `VideowallSyncModePayload` | `unreal-videowall/src/videowall/VideoWallWindow.cpp` |
| Views and their active instance | `actions::TargetState` (`targetstate.h`): views registered by `unreal-qt` and `videowall`, focus order, the followed instance | surface targeting (follow focus, pins) |
| The surface context | `actions::ContextState`: `play`, `ttd`, `debug.disasm`, set by the Qt client when a pane takes focus | deck pages and Dial layers |

Not every `dbg.*` id is a GUI action. `dbg.mode_toggle`, `dbg.breakpoint_toggle_pc`, `dbg.breakpoints_clear` and
`dbg.breakpoint_hits_reset` are core actions on core state (`hostactions*.cpp`). They stay.

The display settings behind `/video/window` (CRT parameters, recording, the viewport) are core state that a front end
reads, not GUI control. They stay too.

## 3. Requirements

| # | Requirement |
|:--|:--|
| R1 | No GUI command, GUI cursor or GUI program's switch in the core: no code, header, route or notification of the plane. A GUI program links the plane's library; the core does not. The generic pieces of §5 that stay (`TargetState`, `ContextState`) know views and contexts only as opaque names. |
| R2 | One library and one protocol for every GUI program. A program declares what it has; nothing is hardcoded per program in the client tools. |
| R3 | Shared interfaces: the same command works on every view of that kind. `memory.goto` works on the Qt debugger's hex view and on `unreal-debugger`'s dump; `disassembly.goto` works on both disassemblies. |
| R4 | Self-describing: a client lists the programs, their windows and views, each one's commands with parameters, and its state. A build-time catalogue exists for the Stream Deck without running a GUI, including each program's own commands. |
| R5 | State pushes. State that a client would poll (address, cursor, toggles, focus) is pushed on change, coalesced. |
| R6 | Targeting: the focused view of a kind, or an explicit program, window and view. Each view names the emulator instance it shows, so a client can say "the memory view showing instance X". |
| R7 | A command acts on the GUI. Anything that changes the machine goes to the core, either from the client or from the GUI program. The program calls the emulator the way its own buttons do. |
| R8 | Never blocks the GUI. Commands run on the GUI thread's event loop and are short. Slow work (encoding and writing a file) runs off that thread. No thread per client. |
| R9 | Local by default. A TCP listener for a remote surface is opt-in (§4.8). |
| R10 | Works with the core in process (unreal-qt, the video wall, `unreal-debugger` with its own core) and without it (`unreal-debugger --connect`, the screen viewer). |

## 4. Design

### 4.1 Shape

```text
 unreal-qt ─────┐   unreal-debugger ─┐   unreal-videowall ─┐   unreal-screen-viewer ─┐
   guicontrol::Host   guicontrol::Host     guicontrol::Host     guicontrol::Host
        │                  │                    │                     │
        └──── discovery directory: one file per running host (socket, optional TCP port) ────┘
                                │
     clients: unreal-gui (CLI, MCP), Python client, the Stream Deck daemon, another GUI program
```

- **`guicontrol/`**, a new top-level library: Qt Core + Qt Network only, no core headers. It holds:
  - the host: the servers, the registry of nodes, the topics, the in-process dispatcher (§4.7);
  - the interfaces (§4.3) as static C++ tables;
  - the client;
  - the catalogue writer.
- **One host per GUI process.** It listens on a local socket and, if enabled, on TCP (§4.8). The local socket is
  `QLocalServer`: a Unix domain socket on macOS and Linux, a named pipe on Windows.
- **Discovery.** On start the host writes `unreal-ng/gui/<pid>.json` into the per-user runtime directory. The file
  holds:
  - the program (`unreal-qt`, `unreal-debugger`, `unreal-videowall`, …), its version, the protocol version;
  - the process start time: with the pid it tells a live host from a reused pid;
  - the socket name, and the TCP port when TCP is on;
  - the emulator endpoints it is bound to: the in-process core's WebAPI port, or the `--connect` URL.

  The host removes its file when it stops. A client skips files whose process is gone or whose start time differs.
  The socket goes in a short directory (the macOS limit is 104 bytes for a socket path). A stale socket left by a
  crash is removed before listening (`QLocalServer::removeServer`).
- **The plane never starts a program.** A program that wants another one (the video wall wanting a debugger) launches
  it itself, with `--connect`, and waits for its discovery file.
- **The core sees nothing of this:** no registry entry, no notification, no route.

### 4.2 Model

```text
host (a process)                 the pid; program name           interfaces: debugger, metadata, program extras
 └─ window                       "main", "debugger", "videowall"  interfaces: window
     └─ view                     "disasm", "memory", "tile.3"     interfaces: disassembly, memory, screen, tilegrid
          └─ instance            the emulator instance it shows: { endpoint, id } (R6); none for a view without one
```

- **Every node** (host, window, view) has interfaces, state and commands. Each interface lives where its subject
  lives:
  - the metadata switches are process-wide (Debug → Metadata), so `metadata` is on the host;
  - `debugger.open` must work while no debugger window exists, so `debugger` is on the host;
  - `window` is on each window;
  - the views carry the rest.
- **Paths:** `<host>`, `<host>/<window>`, `<host>/<window>/<view>` (`48211/debugger/memory`). A client may name
  fewer parts and let targeting find the rest (§4.5).
- **Interfaces are the contract (R3).** Each interface is declared once in `guicontrol/`: its commands with their
  parameters and result, its state fields, its toggles. A program implements it on the matching node.
- **Program extras.** A program's own commands live in its namespace (`unrealqt.*`, `videowall.*`). They are
  declared in a static table in the program's tree that does not depend on widgets, so the catalogue writer links them
  without the program (R4).

### 4.3 First interfaces

| Interface | On | Commands | State |
|:--|:--|:--|:--|
| `debugger` | host | `open`, `close`, `focus {pane}`, `focus_next`, `focus_prev` | open, focused pane |
| `metadata` | host | `set {switch, on?}`: `on` absent flips. Switches: `metadata`, `regions`, `colours`, `notes`, `labels` | each switch |
| `window` | window | `raise`, `show`, `hide`, `move {x,y,w,h}`, `screenshot {path?}` (the window's pixels; no path: to the clipboard, as today's `cap.screenshot`) | title, geometry, visible, active |
| `disassembly` | view | `goto {addr}`, `follow_pc`, `cursor {addr}`, `cursor_move {dir}`, `page {n}`, `scroll_mode {mode?}`; composed (R7): `run_to_cursor`, `breakpoint_toggle_cursor` | top address, cursor, follow PC, scroll mode |
| `memory` | view | `goto {addr, space?}`, `cursor {addr}`, `cursor_move {dir}`, `page {n}`, `follow {register\|off}`, `select {from,to}`, `follow_cursor`; composed: `find_last_write`, `find_last` | top address, cursor, selection, follow |
| `screen` | view | `zoom {factor}`, `fullscreen {on?}` | zoom, fullscreen |
| `tilegrid` | view | `focus {tile}`, `single_sync {on?}`, `layout {preset}` | tiles (each with its instance), focused tile, single sync |

Values:

- `dir` is `up`, `down`, `left` or `right`;
- `n` is signed;
- a toggle command without `on` flips it.

**Composed commands** use the view's own cursor and call the core with an explicit address. For example,
`run_to_cursor` calls `run.to_cursor {addr}` with the program's own origin, exactly as its toolbar button does.

### 4.4 Protocol

Newline-delimited JSON. The **envelope** is the core WebSocket plane's
([websocket-interface.md](../../emulator/design/control-interfaces/websocket-interface.md)): `op`, `rid`, `call` /
`result` / `error`, `subscribe` / `event`, `auth`. A client's framing and dispatch code fits both planes.

Two fields differ, because there is no HTTP underneath:

- **`call`** names a command (`"method":"memory.goto"`, `"target":…, "params":…`), not an HTTP verb and path.
- **`result`** carries `ok` plus `error` / `message`, not an HTTP `status`.

```jsonc
// on connect, the host speaks first
{"op":"welcome","protocol":1,"host":48211,"program":"unreal-qt","auth":false}

{"op":"call","rid":1,"method":"list"}
{"op":"result","rid":1,"ok":true,"body":{"host":48211,"program":"unreal-qt",
  "interfaces":["debugger","metadata"],"state":{"metadata":{"metadata":true,"regions":true}},
  "windows":[{"id":"debugger","interfaces":["window"],"state":{"title":"Debugger","visible":true},
    "views":[{"id":"memory","interfaces":["memory"],"instance":{"endpoint":"http://127.0.0.1:8090","id":"…"},
      "state":{"top":"0x6000","cursor":"0x6000"}}]}]}}

{"op":"call","rid":2,"method":"memory.goto","target":"debugger/memory","params":{"addr":"0x6A00"}}
{"op":"result","rid":2,"ok":true,"body":{"top":"0x6A00"}}

{"op":"subscribe","rid":3,"topics":["state","nodes","focus"],"max_hz":20}
{"op":"event","topic":"state","target":"debugger/memory","state":{"top":"0x6A00"}}
```

- **Errors:**
  - `not_found` (node or command), `bad_request` (params), `ambiguous` (§4.5, with the candidates);
  - `not_now` (for example, the command needs a shown view that is hidden);
  - `unauthorized`, reserved for a later token (§4.8);
  - `failed` with a message;
  - a composed command passes the core's refusal on (for example `run.to_cursor` refused "Run-control held").
- **Topics:**
  - `state`: per node, latest value, coalesced to `max_hz`;
  - `nodes`: windows and views appearing and going;
  - `focus`: per interface, the focused node and the time it took focus (§4.5);
  - `activity`: who ran which command. The core's activity feed does not see GUI commands, so this is where a
    program shows "the Stream Deck moved the memory view".
- **Versions.** `welcome.protocol` changes only on an incompatible change. Adding commands or fields is not
  incompatible.
- **Several drivers.** The user, the bindings and any number of clients act on the same views, and the last command
  wins. There is no lock, as with the user's own hands.

### 4.5 Targeting

- A `target` is a path (§4.2), `<window>/<view>` within a host, an interface alone (`memory`), or an interface with an
  instance (`memory@<instance id>`).
- A partial target resolves to the **focused** node of that interface: the one the user last gave keyboard focus. Each
  host publishes, per interface, its focused node and the wall-clock time it took focus (`focus` topic). A client
  that talks to several hosts compares those times; there is no broker.
- With more than one match and no focus time, the call answers `ambiguous` and lists the candidates.
- **The CLI and scripts** default to an interface alone, so "memory at `#6A00`" works on whichever debugger the user
  looks at.
- **A surface** (Stream Deck, a remote surface) keeps one target for both kinds of keys (owner decision 2026-11-07).
  Its emulator keys follow its core target (`TargetState`: the followed instance, or its pin). Its GUI keys are sent
  as `<interface>@<that instance>`: the focused view of that interface that shows the same machine, and only when no
  view shows it, the focused view alone. A pin of the surface therefore moves both kinds of keys. An emulator key and
  a GUI key never act on two different machines while a view of the target exists.

### 4.6 The GUI and the emulator

- **A view names its instance (R6).** A client can then read the machine through the core planes and drive the view
  through this one.
- **Composed commands (R7)** are built by the GUI program, not by the core (§4.3). The core loses its last reason to
  hold GUI cursors (`DebuggerCursors`, §5).

### 4.7 Inside the program: the controller bindings

The Qt client's pads, ShuttlePRO and Surface Dial bind buttons to action ids, and `GuiActions::Trigger` sends every id
to the core registry. The profiles bind GUI ids: `dbg.cursor_*`, `dbg.focus_next_pane`, `dbg.follow_pc`,
`dbg.memory_follow`, `osd.keyboard`.

After the move the binding layer dispatches by namespace:

- an id of the GUI plane goes to the program's own host, **in process**: a direct call on the GUI thread, with no
  socket and no serialisation;
- every other id goes to the core registry, as today.

The GUI ids in a binding name the command and, optionally, the target, for example `{ gui: memory.cursor_move, dir:
up }`. The profile loader and its validation learn the GUI catalogue next to the core's, so a profile that binds a
GUI command still validates.

### 4.8 Threads, safety

- **Threads.** The host runs on the GUI thread's event loop (`QLocalServer` and `QTcpServer` are non-blocking), so no
  thread is added for it or for clients.
  - A command runs on the GUI thread and is short.
  - Slow work, such as encoding and writing a screenshot, runs on a pool thread, and the result is sent when it is
    done.
  - Backpressure: a client whose unsent bytes stay over a mark is dropped.
- **Local.** The local socket is always on. The socket and the discovery file are owner-only (mode 0600;
  `QLocalServer::UserAccessOption` on Windows).
- **TCP** (owner decision 2026-11-07). An optional listener serves a client on another machine, such as a Steam Deck
  used as a surface. It uses the same protocol and the same host code.
  - It is off by default. `--gui-control-port <port>` or `UNREAL_GUI_CONTROL_PORT` turns it on (a new row in
    [environment-variables.md](../../emulator/environment-variables.md)).
  - With a port given it listens on **all interfaces** (owner decision 2026-11-07), because serving another machine is
    its purpose. `--gui-control-bind <address>` narrows it, for example to `127.0.0.1`.
  - **No token for now** (owner decision 2026-11-07). Anyone who reaches the port drives the GUI, so it is meant for a
    trusted local network only, with the listener off by default. The program logs one line saying that TCP is on,
    on which address, and without authentication.
  - When a token comes later, it is planned as one persistent token per user, regenerable (§8). The protocol already
    has its place: an `{"op":"auth","token":…}` first message, the core WebSocket's form. `welcome.auth` says
    whether the host requires it; today it is always `false`.
  - It is plain TCP without TLS.
  - It runs on the same event loop as the local socket, so R8 holds.

### 4.9 Clients

- **`unreal-gui`**, the CLI, built from `guicontrol/`:
  - `unreal-gui list`;
  - `unreal-gui memory.goto 0x6A00`;
  - `unreal-gui --target debugger/disasm disassembly.goto 0x8000`;
  - `unreal-gui watch state`.
- **Python client:** `tools/guicontrol/guicontrol.py`, for the demo scripts and GUI tests. It replaces the synthetic
  clicks.
- **Stream Deck daemon:**
  - It keeps its core connection for emulator actions and adds a GUI-plane connection for GUI keys.
  - It ships two catalogues: `catalogue.json` (core actions only) and `gui-catalogue.json`. The second is written at
    build time from the interfaces and the programs' extras (R4).
  - Toggle readouts of GUI switches come from the `state` topic.
- **Thin MCP** (owner decision 2026-11-07): `unreal-gui mcp`, a stdio MCP server built from `guicontrol/` and
  separate from the core's MCP server.
  - Three tools over the same protocol: `gui_list` (the nodes with their state and commands),
    `gui_call {target?, method, params?}` and `gui_state {target}`.
  - It adds no verbs of its own: it can do what the CLI does and nothing that reads the machine.
  - The answers carry their content as text, not only `structuredContent`.
- **GUI programs** use the same client to drive each other.

## 5. Moving today's GUI parts out of the core

The GUI actions of `guiactions.cpp` and their new commands:

| Today | After |
|:--|:--|
| `dbg.open` | `debugger.open` |
| `dbg.focus_next_pane`, `dbg.focus_prev_pane` | `debugger.focus_next`, `debugger.focus_prev` |
| `dbg.cursor_up` / `_down` / `_left` / `_right` | `disassembly.cursor_move` / `memory.cursor_move {dir}` on the focused pane |
| `dbg.page_up`, `dbg.page_down` | `disassembly.page` / `memory.page {n}` on the focused pane |
| `dbg.memory_follow` (Enter in the memory pane) | `memory.follow_cursor` |
| `dbg.breakpoint_toggle_cursor` | `disassembly.breakpoint_toggle_cursor` (composed) |
| `dbg.follow_pc`, `dbg.goto`, `dbg.scroll_mode_toggle` | `disassembly.follow_pc`, `disassembly.goto`, `disassembly.scroll_mode` |
| `dbg.labels_toggle`, `dbg.metadata_toggle`, `dbg.metadata_regions_toggle`, `dbg.metadata_colours_toggle`, `dbg.metadata_notes_toggle` | `metadata.set {switch}` |
| `cap.screenshot` (the window to the clipboard) | `window.screenshot` without `path` |
| `osd.keyboard`, `ctl.open`, `ctl.learn` | `unrealqt.osd_keyboard`, `unrealqt.controllers_open`, `unrealqt.controllers_learn` |

The pane-relative commands (`cursor_move`, `page`) of today's actions act on whichever pane has focus. Sent to the
`debugger` interface on the host, they go to the focused pane; sent to a view, they act on that view.

The core's other GUI parts:

| Today (core) | After | Note |
|:--|:--|:--|
| GUI action declarations in `catalogue.cpp`, `ActionInfo::gui`, the GUI registrations in the core registry | `guicontrol` interfaces and `unrealqt.*` extras | No aliases (owner decision 2026-11-07). The same phase moves every shipped binding: the Stream Deck scenarios (`data/streamdeck/scenarios/`) and the controller profiles (`data/controllers/profiles/`, §4.7). |
| `DebuggerCursors` and the cursor fallback in `AddressOrCursor` | removed; the GUI composes `run.to_cursor {addr}`, `ttd.find_last_write {addr}`, `ttd.find_last {addr}` (§4.3) | These core actions then need `addr`. A deck key "run to cursor" binds `disassembly.run_to_cursor` instead. |
| Video wall single sync: CLI `videowall`, REST `/videowall/singlesync`, `Automation::SetVideowallSingleSyncMode`, `NC_VIDEOWALL_SINGLE_SYNC_MODE` | `tilegrid.single_sync` | The core route goes, and `command-interface.md` and the plane documents lose its row. |
| `TargetState` views | **stays** | It answers "which **instance** do core actions follow", a core question. Its views are opaque names. |
| `ContextState` (`debug.disasm` set by the Qt client) | **stays** | A surface context per instance, set by anyone; the GUI is one setter among others. |
| `dbg.mode_toggle`, `dbg.breakpoint_toggle_pc`, `dbg.breakpoints_clear`, `dbg.breakpoint_hits_reset` | **stay** | core actions on core state |

## 6. Phases

The core's GUI parts go only after every binding of them has moved, so phases 3 and 4 run in this order.

| Phase | Content | Done when |
|:--|:--|:--|
| 1 | `guicontrol/`: host, discovery, protocol, the `window` interface, `unreal-gui list` / `window.*` / `watch`; unreal-qt hosts it | `GuiControl_Test` (host and client in process, offscreen): list, call, push, coalescing, a dropped slow client; a live check against unreal-qt |
| 2 | unreal-qt: `debugger`, `metadata`, `disassembly`, `memory`, the composed commands, `unrealqt.*`, `window.screenshot`; the in-process dispatch of the bindings (§4.7); the thin MCP | every GUI action of §5 has its command; the Zynaps demo drives the hex view through the plane; a live MCP script lists and calls |
| 3 | The bindings move: the Stream Deck daemon on the GUI plane, the build-time `gui-catalogue.json`, the controller profiles and their validation, the Stream Deck scenarios | no shipped profile or scenario names a GUI action id of the core; deck keys and pad bindings work through the plane |
| 4 | The core's GUI parts go (§5): GUI declarations, `ActionInfo::gui`, `DebuggerCursors` and the cursor fallback, the video wall single-sync route and notification (the video wall takes `tilegrid.single_sync` in the same phase) | R1 holds: the core has no GUI action, GUI cursor or video wall code; core tests and MCP scripts green |
| 5 | `unreal-debugger` implements `debugger`, `disassembly`, `memory` (in process and `--connect`) | the same `unreal-gui memory.goto` moves either debugger |
| 6 | The rest of the video wall (`tilegrid`, `window`); the screen viewer and the Qt client's main screen (`screen`, `window`) | `unreal-gui list` shows all four programs |

Progress:

- **Phase 1: done (2026-11-07).** `guicontrol/` (host, client, discovery, the `window` interface, `WindowBinding`),
  `unreal-gui`, unreal-qt hosts it (`main`, `debugger`; `--gui-control-port`, `--gui-control-bind`). `GuiControl_Test`
  (11 tests) green; live check against unreal-qt on the offscreen platform: `hosts`, `list`, `window.move`,
  `window.show` / `hide` / `raise`, `window.screenshot` to a file, the TCP listener, `watch` at `--max-hz 5`. The
  offscreen platform sends no activation events, so the `focus` push was checked in the tests only. Module document:
  [docs/emulator/design/gui-control](../../emulator/design/gui-control/README.md).

Docs per phase:

- a module document `docs/emulator/design/gui-control/` (the interfaces, the protocol, the CLI, the MCP);
- `command-interface.md` points to it as a separate plane;
- the automation planes guidelines get one paragraph: GUI control is not an automation plane, and a GUI verb never
  goes into the core registry.

## 7. Tests

- **Library:** host and client in one process on the offscreen platform. Covers:
  - `welcome`, `list`, each error;
  - target resolution: full, partial, focus across two hosts, ambiguous;
  - push coalescing at `max_hz`;
  - a slow client dropped;
  - the TCP listener answers like the local socket and logs its address;
  - the discovery file written, removed, and skipped when stale (a dead pid, a reused pid).
- **Per program:** each interface the program implements passes the interface's shared conformance suite, so every
  implementation is held to the same contract: command, then the state push, then the widget shows it.
- **Bindings:** a profile binding a GUI command validates and runs in process without touching the core registry.
- **Live:** a `tools/verification/gui/` script per program, launched offscreen on its own socket.
- **The core:** after phase 4 the core tests and MCP scripts run unchanged, except the removed routes and their
  tests.

## 8. Open points

Decided 2026-11-07:

1. Transport: the local socket plus an optional TCP listener (§4.8).
2. MCP: a thin MCP server in `guicontrol/`, not in the core (§4.9).
3. No aliases; the shipped profiles and scenarios move at once (§5).
4. `TargetState` and `ContextState` stay in the core (§5).

Decided later the same day:

5. ~~**TCP bind address**~~: decided 2026-11-07: all interfaces when a port is given; `--gui-control-bind` narrows
   it (§4.8).
6. ~~**TCP token**~~: decided 2026-11-07: no token for now (§4.8). If one is added later, it is persistent, one per
   user, regenerable.
7. ~~**Core target versus GUI target**~~: decided 2026-11-07: GUI keys follow the surface's core target (§4.5).

No point is open; the design waits for the owner's review.
