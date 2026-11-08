# GUI control plane

The plane that drives the **programs**, not the emulator: the windows and views of unreal-qt and the video wall (and
later the standalone debugger and the screen viewer). The emulator planes (CLI, Python, Lua, MCP, REST, WebSocket) control
the core; this plane controls what a person sees. The core does not know about it and does not link it.

Design and decisions: [docs/inprogress/2026-11-07-gui-control-plane](../../../inprogress/2026-11-07-gui-control-plane/README.md).
Code: [`guicontrol/`](../../../../guicontrol/).

## Contents

- [1. Status](#1-status)
- [2. Pieces](#2-pieces)
- [3. Finding a program](#3-finding-a-program)
- [4. Protocol](#4-protocol)
- [5. Targets](#5-targets)
- [6. Interfaces](#6-interfaces)
- [7. The unreal-gui command](#7-the-unreal-gui-command)
- [8. Settings](#8-settings)
- [9. MCP and the Python client](#9-mcp-and-the-python-client)
  - [9.1 Through a program's core](#91-through-a-programs-core)
- [10. Bindings: profiles, the deck, the catalogue](#10-bindings-profiles-the-deck-the-catalogue)
- [11. Checks](#11-checks)

## 1. Status

| Phase | What | State |
|:--|:--|:--|
| 1 | Host, discovery, protocol, the `window` interface, `unreal-gui list` / `window.*` / `watch`, unreal-qt hosts it | done |
| 2 | unreal-qt: `debugger`, `metadata`, `disassembly`, `memory`, `unrealqt`; the controller bindings in process; the `state` call; `unreal-gui state` / `mcp`; the Python client | done |
| 3 | the controller profiles and the Stream Deck scenarios on the plane; `gui-catalogue.json`; the daemon's `gui` keys and `gui.*` readouts; the TTD scrub a core action | done |
| 4 | the GUI actions, the GUI cursors and the video wall's single sync out of the core; the video wall hosts `tilegrid` | done |
| 5 | unreal-debugger hosts `debugger`, `disassembly` and `memory`, with its own core and with `--connect` | done |
| 6 | the video wall's `tilegrid.focus` / `tilegrid.layout`; `screen` on unreal-qt's main screen and on unreal-screen-viewer, which hosts the plane | done |
| 7 | one port: the plane's methods in the core's API (REST `/api/v1/gui`, WebSocket calls, MCP `gui_<interface>`, CLI `gui`, Lua / Python `gui_*`) in unreal-qt and the video wall, reaching every GUI program on the machine ([§9.1](#91-through-a-programs-core)) | done |

## 2. Pieces

| Target | What | Links |
|:--|:--|:--|
| `guicontrol` | the host, the node tree, the client, discovery, the interface catalogue | Qt Core + Network |
| `guicontrol-widgets` | `WindowBinding`: the `window` interface on a top-level widget | + Qt Widgets |
| `unreal-gui` | the command line and the MCP server (`unreal-gui mcp`) | `guicontrol` |

A program makes one `guicontrol::Host`, starts it, and hangs nodes on its root: windows under the root, views under
windows. A node implements interfaces (named sets of commands and state keys, [§6](#6-interfaces)); a command is a
handler on the node, synchronous or with a `Done` callback. Everything runs on the GUI thread: the host's sockets are
Qt sockets of that thread, a handler touches widgets directly, slow work (a PNG encode) goes to a pool and posts its
answer back.

In-process callers (the controller bindings) use `Host::Invoke` with the same targets and get the same `Result`.

## 3. Finding a program

Each running host writes `<id>.json` into the discovery directory: `UNREAL_GUI_CONTROL_DIR`, else
`<runtime dir>/unreal-ng/gui` (Qt's runtime location: `$XDG_RUNTIME_DIR` on Linux, `~/Library/Application Support`
on macOS, the home folder on Windows). The file is
owner-only and removed when the host stops; an entry whose process is gone is skipped.

```json
{"id": "41234", "pid": 41234, "program": "unreal-qt", "version": "…", "protocol": 1,
 "socket": "/var/folders/…/unreal-gui-41234-3fa9c1", "started": 1794093637000,
 "endpoints": ["http://127.0.0.1:8090"], "tcp_port": 9300, "tcp_bind": ""}
```

`endpoints` are the emulator WebAPIs the program serves: where a client crosses from a window to its machine.

The id is the pid; a second host in one process gets `<pid>-1`. The local socket accepts the same user only. A TCP
listener opens only on request ([§8](#8-settings)), on all interfaces unless bound to one, and **without
authentication** for now: the host logs that once.

## 4. Protocol

Newline-delimited JSON, one object per line, in the envelope of the core WebSocket plane.

| Op | From | Fields |
|:--|:--|:--|
| `welcome` | host, on connect | `protocol`, `host`, `program`, `version`, `auth` (false) |
| `call` | client | `rid`, `method`, `target`?, `params`? |
| `result` | host | `rid`, `ok`, `error`?, `message`?, `body`? |
| `subscribe` / `unsubscribe` | client | `rid`, `topics`, `max_hz`? (1..50, default 20) |
| `subscribed` / `unsubscribed` | host | `rid`, `topics`, `max_hz` |
| `event` | host | `topic` and the topic's fields |
| `auth` | client | reserved; answered `ok` |
| `error` | host | `rid`?, `error`, `message`: a line that is not a message |

Errors: `not_found`, `bad_request`, `ambiguous` (with `body.candidates`), `not_now`, `unauthorized`, `failed`,
`timeout`.

Calls the host itself answers:

| Method | Body |
|:--|:--|
| `list` | `host`, `pid`, `program`, `version`, `protocol`, `endpoints`, `focus`, and the tree: `windows[]`, each with `views[]`; every node has `id`, `interfaces`, `commands`, `state`, `instance`? |
| `resolve` | params `method`, `target`: the node it names here (`target`) and since when it has focus (`focus_at`, 0 = not focused) |
| `state` | params `target` (any node, no method needed): `target`, `interfaces`, `state`, `instance`?, `focus_at` |

Topics:

| Topic | Event fields | When |
|:--|:--|:--|
| `state` | `target`, `state` (the whole latest state) | a node's state changed; at most `max_hz` per connection, the latest wins |
| `nodes` | `added` or `removed` (a path) | a window or view came or went |
| `focus` | `interface`, `target` (null: none), `at` | the user activated a node of that interface |
| `activity` | `method`, `target`, `client`, `ok` | a command ran |

A connection whose unsent bytes pass the backlog limit (4 MB) when something is pushed to it is closed
(`slow_consumer`).

## 5. Targets

| Target | Means |
|:--|:--|
| (none) | the one node that has the command |
| `debugger`, `debugger/disasm` | a path: window, window/view; the host id may lead it (`41234/debugger`) |
| `memory` | an interface: its one node, else the one with that interface's focus |
| `memory@<instance>` | the node of that interface showing that emulator instance; none: `not_found` with the instance and what the views show |

A bare word is a window when that window has the command, else an interface (`debugger` is both a window and the
host's debugger interface). Several matches and no focus: `ambiguous` with the candidates. Across programs the client
asks each host to `resolve` and takes the only match, else the one focused last.

## 6. Interfaces

| Interface | Level | Commands | State |
|:--|:--|:--|:--|
| `window` | window | `window.raise`, `window.show`, `window.hide`, `window.move {x?, y?, w?, h?}`, `window.screenshot {path?}` (no path: the clipboard) | `title`, `geometry {x, y, w, h}`, `visible`, `active` |
| `debugger` | host | `debugger.open`, `debugger.close`, `debugger.focus {pane}` (`disassembly`, `registers`, `memory`, `stack`), `debugger.focus_next`, `debugger.focus_prev`; on the focused pane: `debugger.cursor_move {dir}`, `debugger.page {n?}`, `debugger.activate` (Enter) | `open`, `pane` |
| `metadata` | host | `metadata.set {switch, on?}`: `metadata`, `regions`, `colours`, `notes`, `labels` | each switch |
| `disassembly` | view | `disassembly.goto {addr}`, `disassembly.follow_pc`, `disassembly.cursor {addr}`, `disassembly.cursor_move {dir}` (up, down), `disassembly.page {n?}` (16 rows), `disassembly.scroll_mode {mode?}` (`byte`, `command`); composed: `disassembly.run_to_cursor`, `disassembly.breakpoint_toggle_cursor` | `top`, `cursor`, `pc`, `scroll_mode` |
| `memory` | view | `memory.goto {addr}`, `memory.cursor {addr}`, `memory.cursor_move {dir}`, `memory.page {n?}`, `memory.follow {register}` (`pc`, `sp`, `bc`, `de`, `hl`, `ix`, `iy`, `off`), `memory.select {from, to}` (on the page shown), `memory.follow_cursor` (the word at the cursor); composed: `memory.find_last_write`, `memory.find_last {access?}` | `top`, `cursor`, `selection {from, to}`, `follow` |
| `unrealqt` | host | `unrealqt.osd_keyboard {on?}`, `unrealqt.controllers_open`, `unrealqt.controllers_learn {device}` | `osd_keyboard` |
| `tilegrid` | view | `tilegrid.single_sync {on?}`: every tile shows the first tile's machine in step with it, the others removed; `tilegrid.focus {tile}` (an index from 0: selects the tile as a click does, its machine plays the sound); `tilegrid.layout {preset}` (`windowed`, `frameless`, `fullscreen`: the wall's window mode, see below) | `tiles [{instance}]`, `focused` (null: none), `single_sync`, `layout` |
| `screen` | view | `screen.zoom {factor}` (1..4: the window fits the picture at that whole scale, leaving full screen), `screen.fullscreen {on?}` | `zoom` (the scale the picture is shown at now), `fullscreen` |

A declared command takes its declared parameters only: an unknown or a missing one is `bad_request` naming it and
listing what the command takes (`memory.goto: unknown parameter file; missing addr; memory.goto takes addr (...)`).

Values: an address is a number or text (`27136`, `0x6A00`, `#6A00`, `$6A00`, `6A00h`) and comes back as `0x6A00`;
`dir` is `up`, `down`, `left` or `right`; `n` is signed (default 1); a toggle without `on` flips it. The memory view
works in CPU addresses of the machine's current mapping: unreal-qt's shows one 16 KB page, `goto` and a cursor past
the page's edge show the address's page; unreal-debugger's shows the processor's whole space. A line is the view's
own: as wide as the window in unreal-qt, 16 bytes in unreal-debugger. A view's state is null while the debugger shows no machine; its commands then answer
`not_now`.

unreal-qt binds its main window as `main`, the debugger as `debugger` with the views `debugger/disassembly` and
`debugger/memory` (each names the machine it shows: `instance {endpoint, id}`), and the `debugger`, `metadata` and
`unrealqt` interfaces on its root. Composed commands call the core with the view's address and the GUI origin
(`run.to_cursor`, `ttd.find_last_write`, `ttd.find_last`), as the window's own buttons do; the core's refusal comes
back as the command's (`not_now: no time travel history`). The catalogue in
[`interfaces.cpp`](../../../../guicontrol/src/interfaces.cpp) is the source; a program adds its own commands with
`AddProgramInterface` (unreal-qt: [`programcontrol.cpp`](../../../../unreal-qt/src/guiplane/programcontrol.cpp)).

unreal-debugger (`debuggers/qt`, [`guiplane/debuggercontrol.cpp`](../../../../debuggers/qt/guiplane/debuggercontrol.cpp))
implements the same three, as program `unreal-debugger`: the `debugger` interface on its root, its window as
`debugger`, the views `debugger/disassembly` and `debugger/memory` on the listing and the dump of the processor whose
tab is in front (their state adds `cpu`, its name). With its own core the views name its machine with no endpoint;
with `--connect` they name the emulator taken over and its WebAPI, and the instance is null while it is gone.
`run_to_cursor` and `breakpoint_toggle_cursor` are the window's F4 and F2 (`not_now` while the machine runs, or is only
seen); `find_last_write` and `find_last` run the core action with the dump's address, through the registry in process
and `POST /api/v1/controllers/actions/{action}` with `--connect` (the main processor only: a card's Z80 has no
history). It has no `metadata` interface (no markup switches). The plane code is on the views' side and sees no core
header: the machine's id and its core actions come from the adapter through hooks.

The video wall (unreal-videowall) binds its window as `main` and the view `main/tilegrid` (`tilegrid`). Its single
sync mode lives here only: the emulator planes have no video wall command
([`videowall_singlesync.py`](../../../../tools/verification/videowall/src/videowall_singlesync.py) drives it).
`tilegrid.layout` switches the wall's window mode, the F10 / Ctrl+F switches: `frameless` and `fullscreen` fill the
screen with tiles (machines are created for them), `windowed` restores the window and drops the extra tiles. It is not
a layout preset (how many tiles, where, of what size): the wall has none yet.

`screen` is the shared [`ScreenBinding`](../../../../guicontrol/include/guicontrol/screenbinding.h) of
`guicontrol-widgets`: unreal-qt binds its main screen as `main/screen` (`zoom` is View > Scale, `fullscreen` the
window's full screen), unreal-screen-viewer (program `unreal-screen-viewer`, `--connect host:port` names the WebAPI it
watches, its only endpoint) binds its screen as `main/screen`, the picture being one, two side by side or two stacked
256x192 screens. Each names the machine shown; the viewer's is the one selected in its list.

**No GUI verbs in the core.** The core's action registry holds core actions only: the debugger's panes, the windows,
the screenshot to the clipboard, the on-screen keyboard and the Controllers window are commands of this plane, with
no alias among the actions (a binding of an old id such as `dbg.open` or `cap.screenshot` finds no action). The core
actions a GUI composes take the address it shows: `run.to_cursor {addr}`, `ttd.find_last_write {addr}`,
`ttd.find_last {addr}`; the core keeps no GUI cursor.

**Controller bindings.** A binding in a controller profile that names a plane command (`memory.cursor_move`,
`unrealqt.osd_keyboard`) runs it on the program's own host (`GuiActions::TriggerGui`, `Host::Invoke`), not through
the core. The binding's parameters are the command's; a `target` among them names the node, else a view command goes
to `<interface>@<the device's machine>` (refused when no view shows that machine). A held button of a `repeats` command (`cursor_move`, `page`) repeats after
300 ms every 60 ms; an encoder step runs it once per step.

## 7. The unreal-gui command

```bash
unreal-gui hosts                         # the running programs
unreal-gui list                          # their windows and views, state, commands
unreal-gui --target debugger window.raise
unreal-gui --target debugger window.move x=40 y=50 w=900 h=700
unreal-gui --target main window.screenshot scratch/main.png
unreal-gui memory.goto 0x6A00            # the focused memory view
unreal-gui --target memory@<instance> memory.select from=0x6A10 to=0x6A1F
unreal-gui state memory                  # a node's state, interfaces and machine
unreal-gui watch state focus --max-hz 5  # print the pushes
unreal-gui --tcp 192.168.1.5:9300 list   # a program on another machine
```

Values without `key=` fill the command's parameters in their declared order (`window.screenshot PATH`,
`window.move X Y W H`); the target always goes with `--target`. `--host ID` picks a program, `--json` prints the raw
answer. Exit codes: 0 done, 1 refused or failed, 2 wrong usage.

## 8. Settings

| Setting | unreal-qt option (the video wall: the environment only) | Environment |
|:--|:--|:--|
| TCP listener port (0: any free) | `--gui-control-port N` | `UNREAL_GUI_CONTROL_PORT` |
| its address (default all interfaces) | `--gui-control-bind ADDR` | `UNREAL_GUI_CONTROL_BIND` |
| discovery directory | | `UNREAL_GUI_CONTROL_DIR` |

The option wins over the environment.

## 9. MCP and the Python client

`unreal-gui mcp` serves MCP on stdio (newline-delimited JSON-RPC 2.0: `initialize`, `ping`, `tools/list`,
`tools/call`). Three tools over the same protocol: `gui_list` (every host's tree), `gui_call {method, target?,
params?, host?}`, `gui_state {target, host?}`. The `gui_call` description lists every declared command with its
parameters. Every answer carries the body as text in `content[]` and the plane's result in `structuredContent`; a
refusal is `isError` with `error: message` (and the candidates of an ambiguous target). It reads nothing of the
machine: a view's `instance` points to the emulator's own MCP or WebAPI.

```json
{"mcpServers": {"unreal-gui": {"command": "cmake-build-agent-release/bin/unreal-gui", "args": ["mcp"]}}}
```

[`tools/guicontrol/guicontrol.py`](../../../../tools/guicontrol/guicontrol.py) is the Python client (standard library
only) for demo scripts and GUI tests:

```python
from guicontrol import Gui
with Gui.connect() as gui:                  # host=ID, tcp="ADDR:PORT"
    gui.call("memory.goto", addr="0x6A00")
    print(gui.state("memory")["state"])
```

### 9.1 Through a program's core

A program with a core and the plane (unreal-qt, the video wall) registers two callbacks with the core, `GuiPlane`
(`core/automation/common/guimethods.h`): the catalogue and a call. The core knows no GUI command; its planes hand
the call over and pass the plane's result back. The callbacks are `guicontrol::corebridge`
(`guicontrol/corebridge.h`): the same routing as `unreal-gui` (`guicontrol::route`, a host by id, the method's
interface, the focus records), over the local sockets, to any GUI program on the machine, the program's own included
(called on the GUI thread, it runs inline; from another thread, on the global pool). unreal-debugger in process has
no automation of its own: it is reached through unreal-qt's port or the wall's.

| Plane | Form |
|:--|:--|
| REST | `GET /api/v1/gui`, `GET /api/v1/gui/catalogue`, `GET /api/v1/gui/state?target=`, `POST /api/v1/gui/{method}` (body the parameters and `target`); 200 with the result, 404 `no_gui`, 400 a body not an object |
| WebSocket | a `call` of any of these routes |
| MCP | one tool per interface (`gui_memory`, `gui_screen`, ...: `action`, `target`, the parameters), `gui_list`, `gui_state` |
| CLI | `gui [list]`, `gui catalogue`, `gui state <target>`, `gui <method> name=value .. [target=]` |
| Lua / Python | `gui_list()`, `gui_catalogue()`, `gui_state(target)`, `gui_call(method, ...)`; they wait up to 15 s |

No session or companion scope: the routes are as open as the plane's own socket. The Python client has `WebGui(port,
host=None, via="rest" | "mcp")` with the same `call` / `state` / `list`. Command reference:
[command-interface.md §16.4](../control-interfaces/command-interface.md#164-the-gui-control-planes-methods).

## 10. Bindings: profiles, the deck, the catalogue

GUI verbs are named by their plane method everywhere a binding is written down; no shipped controller profile or deck
scenario names a GUI action id of the core (`GuiPlaneProfiles_Test` holds it for the profiles).

**`gui-catalogue.json`.** The build writes [`data/streamdeck/gui-catalogue.json`](../../../../data/streamdeck/gui-catalogue.json)
(target `gui-catalogue`, the tool `unreal-gui-catalogue`; the file changes only when the declarations do) from the
interface declarations, the way `catalogue.json` comes from the core's action registry:

```json
{"format": "unreal-ng-gui-catalogue", "version": 1, "interfaces": [
  {"name": "debugger", "level": "host", "description": "…", "state": ["open", "pane"],
   "commands": [{"method": "debugger.cursor_move", "label": "Cursor in the focused pane", "repeats": true,
                 "params": [{"name": "dir", "kind": "string", "optional": false, "description": "up, down, left, right"}]}]}]}
```

`level` is where the interface sits: `host` (the program's root), `window`, `view`. The deck tooling checks scenario
keys against it.

**Controller profiles.** A control binds a plane method the same way it binds a core action; the parameters go in
`params:` (an object; it excludes a `curve` object, which sets them too). The router runs it in process
(`Host::Invoke`), a held control of a `repeats` command repeats.

```yaml
dpad_up: { action: debugger.cursor_move, params: { dir: up } }
start:   { action: unrealqt.osd_keyboard }
```

**Stream Deck daemon.** A key step `{kind: gui, method, params?, target?, hold?}` goes to the program that serves the
daemon's WebAPI: the client (`companions/streamdeck/lib/src/guiplane.cpp`) reads the discovery files, takes the host
whose `endpoints` hold that WebAPI, talks over its local socket and connects again after the program restarts. With
no target the plane resolves it; when the deck follows a chosen instance (`emulator` not `auto`), a view interface
goes to `iface@<instance>`. `hold: true` (a `repeats` command) repeats while the key is held: again after 300 ms, then
every 60 ms, until the release or a refusal. A refusal shows on the key and in the activity feed with the plane's
message. Readouts `gui.<path>` read the program's root state (`gui.metadata.labels`, `gui.osd_keyboard`,
`gui.open`); the daemon polls them while a page shows one, also while a surface pushes the core readouts.

In a scenario spec (`data/streamdeck/scenarios/src/*.spec.yaml`) the key field is `gui:`; `deckflow` fills the glyph
(glyphs.json binds methods, per parameter value), the title (the command's label) and the hold:

```yaml
cup:    {at: [2, 4], uc: D6.2, gui: debugger.cursor_move, params: {dir: up}, title: Cursor up}
labels: {at: [2, 8], uc: D6.4, gui: metadata.set, params: {switch: labels}, label: LABELS}
```

**The TTD scrub** (`ttd.scrub_back`, `ttd.scrub_forward`, level actions of a held stick or trigger) is a core action
with a clock in the core (`core/src/emulator/actions/scrubactions.cpp`), so the profiles keep it; it is not a GUI verb.

## 11. Checks

| Check | What |
|:--|:--|
| `unreal-qt-tests --gtest_filter='GuiControl_Test.*'` | host and client in process: list, call, state, push, coalescing, a dropped slow client, TCP, `WindowBinding`, the parameter helpers |
| `python3 tools/guicontrol/verify_unrealqt.py` | every unreal-qt command against an offscreen unreal-qt with a fresh machine, then `verify_mcp.py` |
| `python3 tools/guicontrol/verify_debugger.py` | the shared `debugger` / `disassembly` / `memory` checks on unreal-qt, unreal-debugger with its core and unreal-debugger `--connect` (offscreen), then one `unreal-gui memory.goto` moving each, by host and by focus |
| `python3 tools/guicontrol/verify_programs.py` | the four programs at once (unreal-qt, unreal-debugger `--connect`, unreal-screen-viewer `--connect`, unreal-videowall, offscreen): `unreal-gui list` shows them, `screen` on unreal-qt and the viewer, `tilegrid` and `window` on the wall |
| `python3 tools/guicontrol/verify_one_port.py` | the checks of phases 5 and 6 once more through unreal-qt's WebAPI port and its MCP (the three debuggers, the screen viewer), the wall's through its own port, one WebSocket, CLI, Lua and Python call each |
| `core-tests --gtest_filter='GuiMethods_Test.*:McpGui_Test.*:OpenApiGuiRoutes_Test.*'` | the registry, the waiting calls, the MCP tools from a catalogue, the routes in the OpenAPI spec |
| `python3 tools/guicontrol/verify_mcp.py <unreal-gui>` | the MCP server against a running program: the tools, their text, the refusals |
| `unreal-qt-tests --gtest_filter='GuiPlaneProfiles_Test.*'` | every shipped profile binding: no GUI action of the core, each plane method declared, its params accepted |
| `streamdeck-tests` (`TestGuiPlane`) | the daemon's client against a fake host and a Unix socket, discovery by endpoint, the `gui` kind |
| `python3 companions/streamdeck/tools/tests/test_deckflow.py` | `gui:` keys in a spec: check, defaults, build, the round trip |
| `python3 tools/guicontrol/demo_zynaps_hexview.py --tape Zynaps.tzx` | the memory view at the Zynaps variables, sprites and font through the plane, a shot per scene |
