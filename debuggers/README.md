# Debuggers

The debuggers of unreal-ng, separate programs beside the emulator:

| Program | Folder | What | Talks to the emulator |
|---------|--------|------|-----------------------|
| `unreal-debugger` | [qt/](qt/README.md) | eCat3's debugger window (Qt 6 Widgets): a tab per processor (listing with jump arrows, registers, flags), dumps of every address space and memory, stack, breakpoints, history, devices | the core in its own process, or a running unreal-qt through its WebAPI (`--connect host:port`) |
| `unreal-tui-debugger` | [tui/classic/](tui/classic/README.md) | the classic UnrealSpeccy debugger screen, 80 x 30 (TDD-DBG-01) | a running unreal-qt through the WebSocket `/api/v1/ws` (default) or REST |
| `unreal-tui-debugger-tsconf` | [tui/tsconf/](tui/tsconf/README.md) | its TS-Conf fork, 157 x 30 (TDD-DBG-02): PC history, the TS-Conf register board | the same |

They started as POCs: the Qt one as [POC 041](../tools/poc/041-ecat-debugger/README.md), the terminal ones as
[POC 018](../tools/poc/018-tui-debuggers/README.md). The POC folders keep their measurements and conclusions; the code
lives on here.

## Build

On by default: the Qt debugger with the Qt apps (`UNREAL_DEBUGGER_QT`, default `BUILD_QT_APPS`), the terminal debuggers
always (`UNREAL_TUI_DEBUGGERS`, default `ON`; they need neither Qt nor the core).

```bash
tools/build/build.sh unreal-debugger
tools/build/build.sh unreal-tui-debugger unreal-tui-debugger-tsconf tui-debugger-tests
```

| Option | Default | Effect |
|--------|---------|--------|
| `UNREAL_DEBUGGER_QT` | `BUILD_QT_APPS` | `unreal-debugger` (needs Qt 6 Widgets and Network, links the core) |
| `UNREAL_TUI_DEBUGGERS` | `ON` | `unreal-tui-debugger`, `unreal-tui-debugger-tsconf` and their tests |
| `UNREAL_TUI_SDL` | `ON` | the terminal debuggers' pixel window through SDL3: the submodule `lib/SDL` (pinned release, built static with video and the 2D renderer only), else an installed SDL3 (its CMake package or pkg-config); without SDL3 they run in the terminal only. On Linux SDL builds its X11 / Wayland drivers from the dev headers present (`libx11-dev`, `libwayland-dev`, `libxkbcommon-dev`, ...) |
| `UNREAL_TUI_SDL_REQUIRED` | `OFF` | fail the configure when the terminal debuggers get no SDL3. The packages build with `ON`: they carry only the SDL3 builds |

The terminal debuggers' third-party code is vendored in `tui/lib/`: FTXUI 7.1.0 and Z80Ex 1.1.21 (the mock machine of
their tests). SDL 3.4.18 (zlib license) is the submodule `lib/SDL` (`git submodule update --init lib/SDL`).
`--help` names the pixel window: `pixel window: SDL 3.4.18, video: cocoa offscreen dummy` (the package checks read
the drivers from it), or `none` for a terminal-only build. See [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

## Run

```bash
# the Qt debugger over a machine of its own (run from the build's bin/: the core finds data/ there)
cmake-build-agent-release/bin/unreal-debugger --model PENTAGON [file.sna|.z80|.tap|.trd|...]
# over a running unreal-qt
cmake-build-agent-release/bin/unreal-debugger --connect 127.0.0.1:8090 [--emulator <id>]

# the terminal debuggers over a running unreal-qt (ws://localhost:8090 by default)
cmake-build-agent-release/bin/unreal-tui-debugger [--endpoint ws://host:port | --port 8190] [--emulator <id>] [--tui]
cmake-build-agent-release/bin/unreal-tui-debugger-tsconf --backend rest --endpoint http://host:port
cmake-build-agent-release/bin/unreal-tui-debugger --backend mock     # no emulator: the built-in test machine
```

unreal-qt starts each of them over its own emulator: **Debug > Debugger** has **Embedded** (its debugger window),
**eCat** (`unreal-debugger --connect 127.0.0.1:<port>`), **Unreal TUI** and, on a TS-Conf machine, **Unreal TUI, TS-Conf**
(`--endpoint ws://127.0.0.1:<port>`), each with `--emulator <the window's instance id>`; the port is the one this
unreal-qt's WebAPI listens on (8090 or `UNREAL_WEBAPI_PORT`: several emulators side by side, each on its own). An
emulator whose WebAPI did not start (its port taken) says so instead. The programs are looked for beside unreal-qt (the packages) or beside its bundle (a build tree);
an absent one is grayed out. Any of them can be open and used at the same time: a debugger's requests say
`X-Unreal-Source: debugger`, they open no run-control session (the GUI and the other debuggers are not locked), and
every client sees the changes. While an automation session (an MCP agent, GDB, a script) holds the machine their
changes are refused, as the GUI is locked.

A terminal debugger attaches to the emulator's first instance (`--emulator <id or symbolic id>`: that one only;
`--port <n>`: the emulator on this host at that port, short for `--endpoint ws://localhost:<n>`) (or creates a 128K one), pauses it and turns its debug
mode on (the emulator's breakpoints stop a run). An emulator that does not answer is reported at once (exit code 2).

The keys are UnrealSpeccy's (`unreal.ini`, `[SYSTEM.KEYS]`; `tui/common/src/ui/actions.*`). F10 opens the system menu:
every command with its key, so nothing needs a hotkey.

| Key | Command | Key | Command |
|-----|---------|-----|---------|
| F7 | step | Space | breakpoint at the cursor (trace) |
| F8 | step over: CALL, RST, LDIR and the like, HALT | G | go to an address (trace) |
| F11 | exit subroutine: run to the word at SP | Home | cursor to the PC (trace) |
| F4 | run to the cursor (trace) | Z | PC to the cursor (trace) |
| Esc (classic), ` (TS-Conf) | continue; pause while running | Tab, Shift+Tab | next, previous window |
| Ctrl+Tab | dump: hex / text (memory) | Ctrl+P, Ctrl+S | dump at the PC, at the SP (memory) |
| H P S X Y I R M, Ctrl+A/F/B/D | edit HL, PC, SP ... (registers) | Alt+S Z 5 H 3 P N C, Ctrl+1/2 | flags, IFF1/2 (registers) |
| Alt+F4 | exit | F10 | system menu |

A terminal does not deliver Ctrl+Tab, Alt+F4 and the modified F-keys: those commands are in the menu. The pixel window
gets every key (Cmd counts as Ctrl on macOS).

A continue, a run to the cursor and an exit subroutine start the machine and return; the screen follows the emulator:
the WebSocket backend subscribes to the `debug.events` and `run.state` topics and reads the machine again when it
stops, steps or has its breakpoints changed by anybody (another client, its own window, a breakpoint). The REST
backend looks twice a second.

## The terminal debuggers' sources

`tui/common/src/` holds what both use as it is: the screen model, the painters of the panels they share, the UI
subsystems, the FTXUI loop and the WebAPI backend with its transports. `tui/classic/src/` and `tui/tsconf/src/` hold
each one's own: the data model (`model/model.h`), the backend interface (`backend/debugger-backend.h`, the fork adds
the PC history and the TS-Conf block), the mock backend, the side panel, the key map, the SDL window, `main.cpp`. Each
app compiles `common/` against its own headers: its `src/` comes first on the include path.

The WebAPI backend (`common/src/backend/webapi-backend.*`) sends every request through an `IApiTransport`
(`api-transport.h`):

- `WebSocketClient` (`ws-client.*`, the default): one connection to `/api/v1/ws` for the whole session, each request a
  `call` (`{"op":"call","rid":N,"method","path","body"}`) answered by its `result`, the same route handlers as REST
  ([websocket-interface.md](../docs/emulator/design/control-interfaces/websocket-interface.md)). A broken socket is opened again at
  the next request; pings are answered.
- `HttpClient` (`http-client.*`): a connection per request.

Both sit on `net-socket.*` (connect with a timeout, send and receive against a deadline; POSIX and Winsock).

The page titles come from `GET /state/paging` with every register read. The TS-Conf debugger's backend
(`tsconf/src/backend/tsconf-webapi-backend.*`) adds the fork's band: the register board from `GET /state/tsconf` (the
`#xxAF` register file and the DMA report) and the PC history from `GET /debug/pchist` (its first read starts the
recording), each read once per stop.

## Tests and CI

| Check | Command | CI |
|-------|---------|----|
| terminal debuggers: widgets, focus, editing, mouse (classic); golden compare of the fork band, PC ring (tsconf) | `cmake-build-agent-release/bin/unreal-tui-debugger-tests`, `...-tsconf-tests` | `build-debuggers-*` (Linux, macOS, MSVC, MinGW), `build-gcc-latest` |
| the Qt debugger: the debugger API and the window's editors and buttons | `QT_QPA_PLATFORM=offscreen unreal-debugger --self-check` | `build-debuggers-*` |
| packages: each debugger starts from the package (`--help`) | | `release.yml`: DMG (`UnrealNG.app/Contents/MacOS`), Windows ZIPs, DEB, AppImage |
