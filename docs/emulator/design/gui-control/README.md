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

## 1. Status

| Phase | What | State |
|:--|:--|:--|
| 1 | Host, discovery, protocol, the `window` interface, `unreal-gui list` / `window.*` / `watch`, unreal-qt hosts it | done |
| 2-6 | debugger and view interfaces, the GUI actions out of the core, the other programs, the MCP | see the design |

## 2. Pieces

| Target | What | Links |
|:--|:--|:--|
| `guicontrol` | the host, the node tree, the client, discovery, the interface catalogue | Qt Core + Network |
| `guicontrol-widgets` | `WindowBinding`: the `window` interface on a top-level widget | + Qt Widgets |
| `unreal-gui` | the command line | `guicontrol` |

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
| `memory@<instance>` | the node of that interface showing that emulator instance, else as `memory` |

A bare word is a window when that window has the command, else an interface (`debugger` is both a window and the
host's debugger interface). Several matches and no focus: `ambiguous` with the candidates. Across programs the client
asks each host to `resolve` and takes the only match, else the one focused last.

## 6. Interfaces

| Interface | Level | Commands | State |
|:--|:--|:--|:--|
| `window` | window | `window.raise`, `window.show`, `window.hide`, `window.move {x?, y?, w?, h?}`, `window.screenshot {path?}` (no path: the clipboard) | `title`, `geometry {x, y, w, h}`, `visible`, `active` |

unreal-qt binds its main window as `main` and the debugger as `debugger`. The catalogue in
[`interfaces.cpp`](../../../../guicontrol/src/interfaces.cpp) is the source; the next phases add `debugger`,
`metadata`, `disassembly`, `memory`, `screen` and `tilegrid`.

## 7. The unreal-gui command

```bash
unreal-gui hosts                         # the running programs
unreal-gui list                          # their windows and views, state, commands
unreal-gui --target debugger window.raise
unreal-gui --target debugger window.move x=40 y=50 w=900 h=700
unreal-gui --target main window.screenshot scratch/main.png
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
