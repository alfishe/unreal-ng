# GUI control plane

The plane that drives the **programs**, not the emulator: the windows and views of unreal-qt (and later the standalone
debugger, the video wall and the screen viewer). The emulator planes (CLI, Python, Lua, MCP, REST, WebSocket) control
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
- [10. Checks](#10-checks)

## 1. Status

| Phase | What | State |
|:--|:--|:--|
| 1 | Host, discovery, protocol, the `window` interface, `unreal-gui list` / `window.*` / `watch`, unreal-qt hosts it | done |
| 2 | unreal-qt: `debugger`, `metadata`, `disassembly`, `memory`, `unrealqt`; the controller bindings in process; the `state` call; `unreal-gui state` / `mcp`; the Python client | done |
| 3-6 | the bindings move to the plane, the GUI actions out of the core, the other programs | see the design |

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
`<runtime dir>/unreal-ng/gui` (`$XDG_RUNTIME_DIR`, the per-user temp directory on macOS and Windows). The file is
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
| `memory` | view | `memory.goto {addr}`, `memory.cursor {addr}`, `memory.cursor_move {dir}`, `memory.page {n?}`, `memory.follow {register}` (`pc`, `sp`, `bc`, `de`, `hl`, `ix`, `iy`, `off`), `memory.select {from, to}` (one 16 KB page), `memory.follow_cursor` (the word at the cursor); composed: `memory.find_last_write`, `memory.find_last {access?}` | `top`, `cursor`, `selection {from, to}`, `follow` |
| `unrealqt` | host | `unrealqt.osd_keyboard {on?}`, `unrealqt.controllers_open`, `unrealqt.controllers_learn {device}` | `osd_keyboard` |

A declared command takes its declared parameters only: an unknown or a missing one is `bad_request` naming it and
listing what the command takes (`memory.goto: unknown parameter file; missing addr; memory.goto takes addr (...)`).

Values: an address is a number or text (`27136`, `0x6A00`, `#6A00`, `$6A00`, `6A00h`) and comes back as `0x6A00`;
`dir` is `up`, `down`, `left` or `right`; `n` is signed (default 1); a toggle without `on` flips it. The memory view
works in CPU addresses of the machine's current mapping: it shows one 16 KB page, `goto` and a cursor past the page's
edge show the address's page. A view's state is null while the debugger shows no machine; its commands then answer
`not_now`.

unreal-qt binds its main window as `main`, the debugger as `debugger` with the views `debugger/disassembly` and
`debugger/memory` (each names the machine it shows: `instance {endpoint, id}`), and the `debugger`, `metadata` and
`unrealqt` interfaces on its root. Composed commands call the core with the view's address and the GUI origin
(`run.to_cursor`, `ttd.find_last_write`, `ttd.find_last`), as the window's own buttons do; the core's refusal comes
back as the command's (`not_now: no time travel history`). The catalogue in
[`interfaces.cpp`](../../../../guicontrol/src/interfaces.cpp) is the source; a program adds its own commands with
`AddProgramInterface` (unreal-qt: [`programcontrol.cpp`](../../../../unreal-qt/src/guiplane/programcontrol.cpp)).

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

| Setting | unreal-qt option | Environment |
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

## 10. Checks

| Check | What |
|:--|:--|
| `unreal-qt-tests --gtest_filter='GuiControl_Test.*'` | host and client in process: list, call, state, push, coalescing, a dropped slow client, TCP, `WindowBinding`, the parameter helpers |
| `python3 tools/guicontrol/verify_unrealqt.py` | every unreal-qt command against an offscreen unreal-qt with a fresh machine, then `verify_mcp.py` |
| `python3 tools/guicontrol/verify_mcp.py <unreal-gui>` | the MCP server against a running program: the tools, their text, the refusals |
| `python3 tools/guicontrol/demo_zynaps_hexview.py --tape Zynaps.tzx` | the memory view at the Zynaps variables, sprites and font through the plane, a shot per scene |
