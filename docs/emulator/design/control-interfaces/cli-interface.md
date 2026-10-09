# CLI (Command Line Interface) - TCP-based

## Overview

The CLI interface provides a human-friendly text-based protocol for interacting with the emulator over TCP sockets. It implements a REPL (Read-Eval-Print Loop) model similar to telnet/netcat sessions.

**Status**: ✅ Fully Implemented  
**Implementation**: `core/automation/cli/`  
**Port**: Configurable (default: TBD)  
**Protocol**: TCP socket, line-oriented text  

## Architecture

```
┌─────────────────┐
│  Client         │  (telnet, netcat, custom CLI tool)
│  Application    │
└────────┬────────┘
         │ TCP Connection
         ▼
┌─────────────────┐
│ AutomationCLI   │  TCP server, connection handling
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ ClientSession   │  Per-connection state
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ CLIProcessor    │  Command parsing & dispatch
└─────────────────┘
```

## Connection Flow

1. **Server Start**: AutomationCLI opens TCP socket on configured port
2. **Client Connect**: Client establishes TCP connection (telnet, netcat, etc.)
3. **Session Init**: ClientSession created with unique socket handle
4. **Auto-Select**: If single emulator exists, auto-selected for convenience
5. **Command Loop**: Client sends commands, receives responses
6. **Disconnection**: Client closes connection, session cleaned up

## Protocol Specification

### Message Format

**Command Format**:
```
<command> [arg1] [arg2] ... [argN]\n
```

**Response Format**:
```
<response-text>\r\n
```

### Line Endings
- **Client → Server**: LF (`\n`) or CRLF (`\r\n`) accepted
- **Server → Client**: CRLF (`\r\n`) always

### Argument Parsing
- Arguments separated by whitespace
- Quoted strings supported: `"argument with spaces"`
- Escape sequences: `\"` for literal quote
- Hex numbers: `0x` prefix (e.g., `0x8000`)
- Decimal numbers: no prefix (e.g., `32768`)

### Special Characters
- `#` - Comment (ignored to end of line)
- `;` - Command separator (not yet implemented)

## Session Management

### Session State
Each TCP connection maintains:
- Selected emulator ID
- Client socket handle
- Connection timestamp

### Auto-Selection Behavior
```
if (no emulator selected && emulator exists):
    auto-select most recent emulator
```

This allows immediate use without explicit `select` command when only one emulator is running.

## Command Reference

See [command-interface.md](./command-interface.md) for complete command reference. All commands listed there are available via CLI.

### Device State Commands

Text renderings of the core device reports (the same data the WebAPI, Lua,
Python and MCP return — [command-interface.md §3.3](./command-interface.md#33-device-state-reports-ay--ssg-turbosound-fm-beta-disk-fdc)):

```
state audio ay          AY/SSG overview           state audio ay 0    one chip decoded
state audio fm          TurboSound FM overview    state audio fm 1    one YM2203 FM half in full
state audio channels    Mixer overview: per-device levels + master (mute, live core sample rate)
state fdc               Beta Disk WD1793 (aliases: state disk, state wd1793)
state ide               IDE board: scheme, latches, both units, CD sense (aliases: state hdd, state cdrom)
state cdaudio           CD drives' audio: disc, tracks, status, head (LBA, MSF, track, index), volume, mixer row (alias: state cdda)
cdaudio [status] [drive]                      the same as `state cdaudio`, or one drive (ide0.slave, a unit 0-3)
cdaudio play [drive] track=N [to=M]           play track N through track M (default: the last); also lba=X frames=N, msf=MM:SS:FF end=MM:SS:FF
cdaudio pause|resume|stop [drive]             as PAUSE / RESUME and STOP PLAY / SCAN (refused while TTD records)
cdaudio volume [drive] left=0-255 right=0-255 route=stereo|swap|mono|left|right|mute sotc=on|off   page 0Eh
cdaudio mixer [drive] volume=0-4 mute=on|off solo=on|off                                           the drive's mixer row
cdaudio ... --json                            the raw reply (the WebAPI's JSON)
state rtc               CMOS clock: time, registers A-D, alarms, every cell (aliases: state cmos, rtc, cmos)
state profi             ZX Profi board chips: the port map in force, 8255, 8253 counters, 8251 and the #B3 latch
network                 Network adapters: ZXNETUSB card, W5300 sockets, virtual network (DHCP, sockets, activity), expansion slot cards (the Sprinter's NE2000 / 3C509B / SprinterESP rows; a 3C509B: ID port, window, FIFOs, EEPROM, link) (alias: net)
key route [auto|matrix|ps2|both]  Where host and injected keys go: the ZX matrix, the PS/2 keyboard controller (ZX-Evo, ATM Turbo 2+), both
network set k=v ..      Change [NETWORK] settings: card=none|zxnetusb|zxwifi|atm2ioesp (a list with ',') host_access=on|off dns_mode=host|pass hosts=name=ip,.. forwards=tcp:host:guest,.. remote_access=on|off (the host listeners of guest servers: on = 0.0.0.0, other computers on the LAN can connect; off = 127.0.0.1 only; alone it keeps every connection, `network` shows virtual_network.listen_address) connect_timeout_ms=n com_port=none|loopback|tcp:host:port|serial:dev[,baud]|espnet[,baud]|at[,firmware][,baud]|modem[,guest port] (the machine's own serial port: the ZX-Evo AVR's; modem = a Hayes modem that dials host:port) zx_wifi=<same values> (the ZX-WiFi card's ESP, default at) com_modem_lines=on|off esp_chip=esp32|esp8266|esp8266-at221|esp8266-at222 (the Sprinter's SprinterESP takes an ESP8266 build, else esp8266-at222) isa1_peer=at|modem[,port]|loopback|tcp:..|serial:.. isa2_peer=.. (Sprinter: a UART card's line - SprinterESP default at, ISA modem default modem, SprinterSerial COM1 default none; `network` prints its slot row with the UART, the ESP's session or the modem's call) isa1_peer_b= isa2_peer_b= (SprinterSerial COM2 #2F8) modem_phonebook=5551234=host:port,.. (the numbers a Hayes modem peer dials with ATDT, any machine: com_port=modem puts one on the machine's own serial port) avr_firmware=baseconf|base2010..base2023|ts|ts2013|ts2016-02|ts2016-04 (ZX-Evo) kbc_firmware=none|v22-7..v41 (ATM Turbo 2+ keyboard controller; com_port is its RS-232 from v31) atm2ioesp=at|espnet|.. atm2ioesp_address=0xF0|0xF8 (the ATM2IOESP card on the ATM Turbo 2+ INTERNAL I/O connector) zifi=none|at[,firmware]|zifi-native[,s3|esp01s]|loopback|tcp:..|serial:.. (TS-Conf, ZX-Evo with a TS-Labs AVR firmware: what the AVR's ZiFi UART is wired to; the state's zifi block shows the API registers and both rings); devices the machine cannot take are listed under not_fitted in `network`. A card= value that changes the ZX-bus cards (zxnetusb, zxwifi) is a slot change applied by a restart (owner decision Q11, like `slots plug` / `remove`): flags --replace (allow removals / an unrealistic fit), --dry-run (the plan only), --media save|discard, --json; the other keys go to the restarted machine. Without a card change the settings apply in place (accepted)
rtc read <start> [n]    Read CMOS cells as the guest reads them (no side effects; numbers: decimal, 0x.., #.., ..h)
rtc write <start> <b>.. Write CMOS cells like the guest: time registers set the clock, C and D are read-only
state contention        Memory contention: rule, switch, interface, contended slots, waits while debugging
state screen            Screen state: video mode, active screen + RAM pages, contention, flash
state screen verbose    + per-screen RAM page and Z80 mapping, decoded #7FFD
state screen mode       Video mode: picture format, memory layout, #EFF7/#DFFD/#FF77
state screen flash      FLASH phase and timing
state sprinter [ports|port <hex>|text|video|palette [k]|ring|bios [<name> ...]]  Sprinter Sp2000 reports; bios <3.04|3.06|3.07|file> selects
memory regions          Device memory regions (the Sprinter's video RAM "vram", the TS-Conf palette "cram" and sprite table "sfile", the CMOS clock's "cmos", the ZX-Evo AVR's "eeprom"); memory region read|write|save|load <name> ...
mixer [<source> [muted=0|1] [solo=0|1] [volume=0..1] [gain_db=..]]  Per-device audio mixer (beeper, ay1, covox, gs, ...)
```

The screen reports ([command-interface.md §6.6](./command-interface.md#66-screen-configuration)) are rendered from the same core reports.

Nested keys are indented, arrays print as `[index]` blocks. A device that is
not on the machine prints `available: false` and a `description`.

### CLI-Specific Behavior

**Interactive Help**:
```
> help
Available commands:
  help [command]    - Show this help or help for specific command
  status           - Show emulator status
  list             - List all emulators
  ...
```

**Command Aliases**:
The CLI supports command aliases for convenience:
- `?` → `help`
- `r` → `registers`
- `m` → `memory`
- `bp` → `breakpoint`
- `u` → `disasm` (disassemble)
- `quit` → `exit`

**Model Creation Semantics** (strict, matching WebAPI):
```
> create pentagon
Created emulator instance: 550e8400-...
Model: PENTAGON - Pentagon (128KB)
Config folder: pentagon128k
Video mode: ZX

> create atm710
Error: Failed to create emulator with model 'atm710'
Reason: model 'ATM710' is not supported by this build (PortDecoder::GetPortDecoderForModel - unknown model 6)
Available models: PENTAGON, 48K, ...

> models
Available ZX Spectrum:
=============================
  PENTAGON - Pentagon
  48K - ZX-Spectrum 48K
  ATM710 - ATM-Turbo 2+ v7.10 (not creatable on this build)
  ...
```
- `create`/`start <model>` echo the RESOLVED model and RAM, not the requested string.
- `create`/`start`/`zxpoly start` take `--ram-power-on random|zero`: `zero` creates the machine with every RAM page reading 0 (reproducible runs), `random` fills the screen pages with noise like real DRAM; omitted = `[MISC] RAMPowerOn` of the model's `unreal.ini`. `model <name>` keeps the current machine's mode unless the option names one.
- A model this build cannot create fails with a `Reason:` line — no silent fallback to 48K.
- `status` output starts with a `Build: v<version> (<branch> @ <commit>, <type>)` fingerprint line.
- `GET /api/v1/emulator/models` (`creatable` flags) remains the runtime-authoritative model source.

**ZX-Poly** (four synchronized instances of one model; recipe
[.recipe/machines/zxpoly.md](../../../../.recipe/machines/zxpoly.md)):
```
> zxpoly start PENTAGON /path/to/zxpolytest.prom
Started ZX-Poly machine: fa3b65e0-...
> zxpoly status
ZX-Poly machine fa3b65e0-...
  #3D00: #00  locked: no  video mode: 0  slaves: waiting
  CPU0 fa3b65e0-...  R0-R3: #20 #00 #00 #00
  ...
  lockstep: ok
```
`zxpoly start <model> [file]` takes a `.zxp`, a `.prom` or a multiloader
disk. `<model>` is a configuration name (`ZXPOLY-48K`, `ZXPOLY-128K`,
`ZXPOLY-PENTAGON`) or a base model. `start ZXPOLY-128K` (the ordinary
command) starts the bare machine, and `models` lists the configurations.
`zxpoly status [id|index]` works for any member of the group. A locked
machine also shows its `schedule`: `parallel` (the slaves run their frame
at the same time), `pipelined` (at unlimited speed, also overlapping the
master's next frame) or `sequential`.

The CLI server listens on port 8765. `UNREAL_CLI_PORT` moves it, just as
`UNREAL_WEBAPI_PORT` moves the WebAPI's 8090 (and the MCP endpoint `/mcp` with it:
its loopback calls follow the same variable), so a second instance can run
beside one that owns the default ports.

**Error Messages**:
```
> invalid_command
Unknown command: invalid_command
Type 'help' for available commands.
```

### Interface Parity

The CLI implements the same command semantics as other interfaces (WebAPI, Python, Lua). However, some capabilities are **intentionally WebAPI-only** as they are designed for programmatic/remote access patterns rather than interactive CLI use:

| WebAPI Endpoint | CLI Equivalent | Reason Not in CLI |
| :--- | :--- | :--- |
| `GET /emulator/models` | `models` | CLI shows the same list with `(not creatable on this build)` markers; the endpoint's `creatable` flags stay authoritative |
| `POST /emulator/start` with `zxpoly` | `zxpoly start <model> [file]` | Same entry point (`EmulatorManager::CreateZXPolyMachine`) |
| `GET /emulator/{id}/zxpoly` | `zxpoly status [id]` | Same source (`ZXPolyGroup::Status`) |
| `DELETE /emulator/{id}` | `stop` | CLI uses `stop` which both stops and removes; separate remove is for advanced orchestration |
| `POST /emulator/{id}/start` | `resume` | Starting an existing (initialized but not running) emulator uses `resume` in CLI |

> [!NOTE]
> The `create` command in CLI creates an emulator in initialized state. Use `resume` to start it. WebAPI provides separate `create` and `start` endpoints for finer-grained control in orchestration scenarios.

### Time-Travel Debugging (TTD) Commands

The CLI exposes the TTD surface with the `ttd` top-level verb and a subcommand. Aliases exist for the most common verbs to keep interactive sessions terse. Full command semantics (arguments, result envelopes, halt reasons, session invalidation rules) live in [command-interface.md §8](./command-interface.md#8-time-travel-debugging-ttd).

| CLI Verb | Alias | Description | Status |
| :--- | :--- | :--- | :--- |
| `ttd status` | `ttd info` | Session snapshot: origin (recorded here vs loaded from file, with path), machine model, frame range, checkpoint count, write-journal and coverage-index sizes, memory. Always available (doubles as capability probe). | ✅ Implemented |
| `ttd start` | `ttd rec` | Begin recording at the next frame boundary. | ✅ Implemented |
| `ttd stop` | — | Stop capturing; retain history. | ✅ Implemented |
| `ttd invalidate` | `ttd clear`, `ttd reset` | Invalidate the session and drop captured data. | ✅ Implemented |
| `ttd limit [frames N] [bytes N[K\|M\|G]]` | `ttd history-limit` | Bound the history: the oldest frames are released while recording beyond either limit (`off` clears; no arguments shows it). See command-interface.md. | ✅ Implemented |
| `ttd seek <frame> [tinframe]`, `ttd seek --bookmark <label>` | `ttd goto` | Seek to a (frame, T-state) point or a bookmark. Refused while recording; the machine stays paused at the target. | ✅ Implemented |
| `ttd step-back` | `ttd sb`, `back` | One frame back. | ✅ Implemented |
| `ttd step-forward` | `ttd sf`, `forward` | One frame forward. | ✅ Implemented |
| `ttd resume [frame [tinframe]]` | — | Truncate future at the current (detached) position, or the given one, and record again; the machine runs. | ✅ Implemented |
| `ttd position` | `ttd pos` | Current time point (frame/tstate) and session bounds. | ✅ Implemented |
| `ttd markers` | `ttd barriers` | List external-event markers in the timeline. | ✅ Implemented |
| `ttd dump <path>` | `ttd save` | Serialize the session to a `.ttd` file. | ✅ Implemented |
| `ttd load <path>` | `ttd open` | Restore a dumped session (model must match the recording). | ✅ Implemented |
| `ttd info <path>` | `ttd file-info` | Describe a `.ttd` file without loading it (no emulator needed): frames, sections, recorded machine - model, ROM signature, General Sound card, TurboSound slot device, devices. `ttd info` without a path is `ttd status`. | ✅ Implemented |
| `ttd export-clip <from> <to> <dir> [--chunk N]` | `ttd clip` | Write frames from..to as a lossless clip into dir (final picture, plane B with `zxdlss`, frame meta); not while recording. See command-interface.md → Exporting a clip. | ✅ Implemented |
| `ttd find-last --addr A` | `ttd fl` | Reverse watchpoint: find the last access at an address (full filter set in the command reference). | ✅ Implemented |
| `ttd port-events <event> [arg]` | `ttd pe` | "When did the program ..." - saw a key (`key space`), the tape signal change (`ear`), wrote an AY register (`ay-write 7`), changed the border... From the port journals, no replay ([command reference](./command-interface.md), "Port events"). | ✅ Implemented |
| `ttd step-instruction` | `si-back` / `si-forward` | Step one Z80 instruction back or forward within recorded history. | ✅ Implemented |
| `ttd reverse-step` | `ttd rs` | Reverse-step execution. | ✅ Implemented |
| `ttd reverse-continue` | `ttd rc` | Reverse-continue execution. | ✅ Implemented |

**Interactive session example** (post-mortem crash forensics):

```
> pause
> ttd start
[armed: capture begins at next frame boundary]
> resume
[wait ~10s while reproducing the bug]
> pause
> ttd find-last --addr 0x5B00 --access write
frame=4823  tstate=14982  pc=0x4A21  value=0x07  physpage=5
> ttd seek 4823 14982
[ok target]
> ttd step-back
> disasm
> registers
```

**Notes**:
- All run-affecting `ttd` verbs require the emulator to be paused and the CLI session to hold the run-control claim. Read-only verbs (`status`, `timeline`, `bookmark list`) work regardless.
- The `timetravel` feature flag must be ON for recording/seek/replay. `status` works regardless and returns `{recording: false}` when TTD is off.
- Seek latency depends on tier density — typically 1–20 ms in the dense (recent) tier; see the [implementation plan](../debugger/time-travel-debug/implementation-plan.md) for targets.

### Mouse Input Commands

The `mouse` verb drives the machine's own mouse of the selected emulator (Kempston interface,
Sprinter serial mouse, ZX-Evo / TS-Conf PS/2 mouse; source:
`core/automation/cli/src/commands/cli-processor-mouse.cpp`, output formatting in
`cli-mouse-format.h`). Full semantics, units and limits: [command-interface.md §11](./command-interface.md#11-mouse-input-injection).

| CLI Command | Alias | Description |
| :--- | :--- | :--- |
| `mouse move <dx> <dy>` | | Relative move in emulated pixels, +x right, **+y up**, −127…127 each |
| `mouse glide <dx> <dy>` | | Long move, −4096…4096 each, one step (at most 127) per frame as the program reads; later commands queue behind it |
| `mouse press <button>` / `mouse release <button>` | | `left`/`right`/`middle` or `l`/`r`/`m` |
| `mouse click <button> [frames]` | | Hold for `frames` (default 2), then release on its own |
| `mouse buttons <none\|b1,b2…>` | | Exact pressed set; `left,middle` and `left middle` both work |
| `mouse wheel <steps>` | | −7…7, not 0 |
| `mouse clear` | `mouse release_all` | Release all buttons, cancel a pending click and queued input |
| `mouse status [device]` | `mouse info` | Multi-line state block: the Kempston interface, routing, then `Machine mouse:` (id, kind, fitted / in use, registers, the serial line or the PS/2 state) and the glide queue |
| `mouse devices` | | The machine's mouse devices |
| `mouse set <x> <y>` | | Debug: raw counters 0…255 |
| `mouse help` | | Subcommand help |

Arguments must be whole integers: `10px` or `1.5` gives `Error: Invalid dx '10px': expected an integer`.
Every successful command prints one line with the resulting state; a second `Warning:` line
appears when part of the change cannot reach the program (a wheel step with no wheel fitted). On a
machine with no mouse fitted every input command prints `Error: no mouse fitted on this machine: ...`.

**Interactive session example** (Pentagon after reset, shipped config `Wheel=NONE`):

```
> pause
> mouse move 10 -5
Moved: dx=+10 dy=-5 -> X=41 Y=80
> mouse press l
Pressed: left -> buttons=L-- (#FADF=0xFE)
> mouse wheel 1
Wheel: +1 -> wheel=1 (#FADF=0xFE)
Warning: no wheel fitted ([INPUT] Wheel=NONE): the guest does not see the wheel counter
> mouse move 300 0
Error: dx=300 out of range -127..127; split into several moves with run_frames between them
> mouse click right 3
Clicked: right for 3 frames -> buttons=LR- (#FADF=0xFC)
> mouse status
Kempston Mouse [present]
  X=41 (0x29)  Y=80 (0x50)
  Buttons: left=down right=down middle=up  (mask 0xFC)
  Wheel: 1 (no wheel fitted)
  Ports: #FADF=0xFC #FBDF=0x29 #FFDF=0x50
  Pending click: right, 3 frame(s) left
  TTD journal: supported
  Routing: decoded (standard Kempston address decode)
Machine mouse: kempston (kempston) [fitted] - Kempston mouse interface
  X=41 Y=80 mask 0xFC no wheel, 3 buttons
  Ports: #FADF=0xFC #FBDF=0x29 #FFDF=0x50
```

On the Sprinter the `Machine mouse:` block shows the board mouse and its serial line, e.g. under
DSS 1.71 after `mouse move 5 3`:

```
Machine mouse: sprinter (serial-microsoft) [fitted, in use] - Sprinter board mouse: Microsoft serial mouse on SIO B + the PLD's Kempston view (#58)
  X=36 Y=88 mask 0xFF no wheel, 3 buttons
  Ports: #FADF=0xFF #FBDF=0x24 #FFDF=0x58
  Serial: line 1200 baud, receiver 1215.3 baud (in tune, enabled)
  Packet: 4C 05 3D (3/3 sent), pending dx=+0 dy=+0
  Line: 1 packets, 3 bytes received, 0 framing errors; receiver FIFO []
```

How to read `#FADF`: bits 0–2 are the buttons (0 = pressed: `0xFC` = left and right down),
bit 3 is always 1, and bits 4–7 are 1 unless a wheel is fitted (`[INPUT] Wheel=KEMPSTON`),
in which case they carry the wheel counter.

The `Routing:` line (mirrors the WebAPI `/mouse/status` `routing` object) tells whether a
mouse port read is decoded **right now**: `shadowed - TR-DOS ports accessible (CF_DOSPORTS):
only Beta Disk operations answer` while TR-DOS owns the port space, `shadowed - hidden by
model-specific decoder gating` behind the Scorpion DOS trigger / Shadow Monitor, or
`shadowed - mouse not fitted for this config ([INPUT] Mouse=)` when nothing is connected.

**Notes**:
- `mouse` commands are refused with `Error: TTD replay in progress; live mouse input refused`
  while TTD replays. While TTD records they are journalled, `mouse set` included.
- The CLI does not run frames for you. After a `click`, use `run_frames` (or resume) so the
  program sees the press and the release.

### Joystick Input Commands

The `joystick` verb drives the Kempston joystick of the selected emulator (source:
`core/automation/cli/src/commands/cli-processor-joystick.cpp`, the command logic and output
formatting in `cli-joystick-format.h`). Full semantics, units and limits:
[command-interface.md §13](./command-interface.md#13-joystick-input-injection).

| CLI Command | Alias | Description |
| :--- | :--- | :--- |
| `joystick press <buttons>` / `joystick release <buttons>` | | `up`, `down`, `left`, `right`, `fire`, `b5`..`b7`; several as `up+fire`, `up,fire` or `up fire` |
| `joystick set <state\|buttons\|none>` | | Exactly these buttons: a byte (`0x18`, `24`), a list, or `none` |
| `joystick tap <buttons> [frames]` | | Hold for `frames` (default 2), then release on its own |
| `joystick clear` | `joystick release_all` | Release everything, cancel a pending tap |
| `joystick status` | `joystick info` | Multi-line state block |
| `joystick list` | | Button names and bits |
| `joystick help` | | Subcommand help |

```
> joystick press up+fire
Joystick pressed: up,fire -> state=0x18 buttons=up,fire (IN #1F=0x18)
> joystick set 300
Error: state=300 out of range 0..255
> joystick tap fire 3
Joystick tap: fire for 3 frames -> state=0x18 buttons=up,fire (IN #1F=0x18)
```

### Analysis, Capture & Assembly Commands

The analysis, capture and assembly families are available on **all** automation
interfaces with identical semantics (CLI, WebAPI, Lua, Python). Full argument
reference: [command-interface.md](./command-interface.md).

| CLI Command | Description |
| :--- | :--- |
| `stepout` | Run until the current subroutine returns to its caller. |
| `skip_until <pc>` | Fast-forward until PC reaches the target (breakpoints skipped, bounded budget). |
| (run control) | `step`, `steps`, `stepover`, `stepout`, `skip_until` and every `run_*` answer "Error: Run-control held by <surface>" while another surface (a GDB client) holds the run-control claim. |
| `find <pattern>` | Search memory for a byte pattern, `??` = any byte (`--space cpu\|ram\|ram5\|rom2\|cache0\|<region>`, `--mask`, `--from`, `--to`, `--align`, `--max`). |
| `out <port> <value>` | A debugger's port write through the machine's decoder, like a CPU OUT (paging, TS-Conf registers, AY, border): no breakpoints, no device waits, a TTD tool edit; paused, stopped or running. Numbers: `0x13AF`, `#13AF`, `13AFh` or decimal. |
| `pchist [depth]` / `pchist on\|off` | PC history: the newest instructions the CPU started, each with its window's page (`C000  ram32`), newest first; the first read starts recording; `debug-snapshot --pchist N` adds it to a snapshot. |
| `debug-wait [since] [--timeout ms]` | Wait until something a debugger shows changes (a stop, a run start, a tool edit) or the timeout (default 10000 ms, at most 60000) passes: `Changed: seq 43, paused at breakpoint #3 (8000)` or `No change: seq 42, running`. |
| `debug-snapshot` | One coherent debugger snapshot: registers, pages, time, code, stack, memory (`--disasm N`, `--stack N`, `--memory space:addr:len`, repeatable). |
| `digest <start> <end>` | Stable 64-bit screen-content digest (`--banks`, `--active`, `--no-border`). |
| `ports` | Static port map with live routing flags: port/mask/match/device/gate rows from the machine's port decoder, plus TR-DOS active, mouse routing and the Scorpion Shadow Monitor latch. |
| `beam` | Current raster position and beam zone, plus the layer pixel under the beam. |
| `video layout` | The current mode's layers (surface, beam window, dots per T) and framebuffer placement. |
| `video pixel <x> <y> [layer]` / `video pixel t <tstate>` | Memory, registers and palette cell behind a pixel (or the point under the beam, border included). |
| `video address <page> <offset>` / `video address z80 <addr>` | Every area of the picture a byte feeds. |
| `video address palette <offset>` / `video address sprite_ram <offset>` | Every pixel drawn with a palette cell (16-bit cells, cell n at byte 2n) / of the sprite an attribute word describes (TS-Conf SFILE word n at 2n). |
| `video text [layer]` | Exact text grid of an ATM / ZX-Evo / TS-Conf text mode, or the Sprinter's text squares. |
| `video address vram <offset>` | Every pixel a byte of the machine's own video RAM feeds (the Sprinter). |
| `video changes [1\|2]` | Video change log: latch changes (mode, #7FFD, border, the Sprinter's RGMOD / HOLD / PORT_Y / ALL_MODE / frame height) with T, line and PC; palette / mode table writes per frame. |
| `capture screen [--area=full\|screen] [--format=png\|gif] [--source=presented\|live] [file]` | Screenshot of the session's emulator: the whole frame with border (default) or the working picture; PNG (default) or GIF; the presented frame (default) or `--source=live`, the frame as drawn now (a paused machine reports where the beam stopped). Prints the size, the cut rectangle, the screen window and a data URI, or saves to `file`. An unknown word is an error. |
| `capture framebuffer <file> [rgba\|index]` | The picture as raw pixels (R,G,B,A, or the Sprinter's u16 pens). |
| `video temporal [status\|list\|off\|<algorithm>]` | ZX DLSS de-flicker: show its status (algorithm, video / audio delay it causes, timing), list the algorithms, switch it off or on (default `mod-tpgwafsd`, which shows the picture 7 frames later and delays the sound 5 more frames to match; see [command-interface.md §5.9](./command-interface.md)). |
| `frame_cost` | Per-frame halt/run cost accounting. |
| `coverage <start\|stop\|clear\|gaps\|status>` | Executed-address coverage analysis. |
| `aylog <start\|stop\|clear\|dump\|status>` | AY-3-8910 register access log. |
| `audiocapture <start\|stop\|clear\|result\|save>` | Audio capture with level stats and WAV export; `start <seconds> [source]` records one mixer device. |
| `videorecord <start\|stop\|pause\|resume\|status>` | Screen recording (requires `ENABLE_RECORDING` build). `start` accepts `--region full\|screen` (default `full`: the whole frame with its border; `screen`: the working picture, fitted into the start window's size when the window changes), `--profile native\|1080p\|1440p\|4k` (a fixed frame, the picture scaled sharply into it with black bars; h264/h265 only), `--acceleration auto\|hardware\|software` (GPU or software encoder), `--audio-rate N\|auto` to pin the core audio rate for the whole recording (one step for fixed-rate captures; fails fast if the emulator is paused so the file cannot be mislabeled — see [command-interface.md §5.8](./command-interface.md)). |
| `assemble <addr> <code>` (`asm`) | Assemble Z80 source in place (`--write` to patch RAM). |
| `label resolve <name\|addr>` | Resolve a label by name or an address to labels + context. |
| `listing <load\|clear\|info\|source_at\|step_line\|run_to_line>` | Source-level debugging via assembler listings. |

## Connection Examples

### Using Telnet
```bash
telnet localhost <port>
> help
> list
> select 0
> registers
> bp 0x8000
> resume
```

### Using Netcat
```bash
nc localhost <port>
> status
> memory 0x8000 256
```

### Scripted Automation
```bash
#!/bin/bash
{
    echo "select 0"
    echo "bp 0x8000"
    echo "resume"
    sleep 5
    echo "pause"
    echo "registers"
    echo "exit"
} | nc localhost <port>
```

## Advanced Usage

### Batch Commands (Future)
```bash
# Load commands from file
cat commands.txt | nc localhost <port>
```

### Programmatic Integration
```python
import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('localhost', port))

def send_command(cmd):
    sock.sendall(f"{cmd}\n".encode())
    return sock.recv(4096).decode()

print(send_command("list"))
print(send_command("select 0"))
print(send_command("registers"))

sock.close()
```

## Performance Characteristics

- **Latency**: ~1-5ms per command (local connection)
- **Throughput**: ~1000 commands/second
- **Concurrent Connections**: Unlimited (one thread per connection)
- **Overhead**: Minimal (~0.1% CPU per connection)

## Security Considerations

⚠️ **Warning**: Current implementation has NO authentication or encryption.

**Current State**:
- Binds to all interfaces (0.0.0.0)
- No password required
- Plain text protocol
- No rate limiting

**Recommended Deployment**:
- Bind to localhost only for local-only access
- Use SSH tunneling for remote access:
  ```bash
  ssh -L local_port:localhost:remote_port user@remote_host
  ```
- Firewall rules to restrict access
- Consider VPN for trusted remote access

**Future Security Enhancements** (planned):
- Authentication (username/password)
- TLS/SSL encryption
- Token-based auth
- Rate limiting and flood protection
- Audit logging

## Error Handling

### Connection Errors
- **Port in use**: Server fails to start, error logged
- **Connection refused**: Check server is running and port is correct
- **Connection reset**: Server crashed or was stopped
- **Timeout**: No response from server (check network)

### Command Errors
- **Unknown command**: Returns suggestion to use `help`
- **Invalid arguments**: Returns usage information
- **No emulator selected**: Returns error message
- **Emulator not found**: Check `list` output

### Recovery Strategies
1. **Connection lost**: Reconnect and re-select emulator
2. **Command failed**: Check syntax with `help <command>`
3. **Hung connection**: Close and reconnect
4. **Server unresponsive**: Restart emulator application

## Configuration

**Configuration File**: `config.ini` (location TBD)

```ini
[CLI]
enabled = true
port = 9999
bind_address = 127.0.0.1
max_connections = 10
command_timeout = 30000
```

**Environment Variables**:
```bash
UNREAL_CLI_PORT=9999
UNREAL_CLI_BIND=127.0.0.1
```

## Implementation Details

### Source Files
- `core/automation/cli/src/automation-cli.cpp` - Server main loop
- `core/automation/cli/src/cli-processor.cpp` - Command processing
- `core/automation/cli/include/cli-processor.h` - Command handlers

### Key Classes
- `AutomationCLI` - TCP server, connection management
- `ClientSession` - Per-connection state
- `CLIProcessor` - Command parser and dispatcher

### Threading Model
- Main thread: TCP accept loop
- Worker threads: One per client connection
- Synchronization: Mutex on emulator access

### Dependencies
- Platform sockets (Winsock on Windows, BSD sockets on Unix)
- C++ standard library (std::thread, std::string, std::vector)
- CLI11 library (command-line argument parsing)

## Troubleshooting

### "Connection refused"
**Cause**: Server not running or wrong port  
**Solution**: 
1. Check emulator is running with CLI enabled
2. Verify port with `netstat -an | grep <port>`
3. Check firewall rules

### "No emulator selected"
**Cause**: No emulator instance available  
**Solution**:
1. Create emulator instance in main application
2. Use `list` to see available emulators
3. Use `select <id>` to choose one

### Commands hang
**Cause**: Emulator deadlock or processing long operation  
**Solution**:
1. Wait for operation to complete
2. Disconnect and reconnect
3. Restart emulator if hung

### Unexpected responses
**Cause**: Command syntax error or unsupported feature  
**Solution**:
1. Check command syntax with `help <command>`
2. Verify command is implemented (see status in docs)
3. Check emulator log for errors

## Best Practices

1. **Always select emulator first** (unless auto-selected)
2. **Use `status` to verify state** before control commands
3. **Pause before inspecting state** for consistent results
4. **Close connections properly** with `exit`
5. **Script repetitive tasks** using shell scripts or Python
6. **Test commands interactively** before scripting
7. **Monitor connection status** in production automation

## See Also

### Interface Documentation
- **[Command Interface Overview](./command-interface.md)** - Core command reference and architecture
- **[WebAPI Interface](./webapi-interface.md)** - HTTP/REST API for web integration
- **[Python Bindings](./python-interface.md)** - Direct C++ bindings for automation and AI/ML
- **[Lua Bindings](./lua-interface.md)** - Lightweight scripting and embedded logic

### Advanced Interfaces (Future)
- **[GDB Protocol](./gdb-protocol.md)** - Professional debugging with standard GDB/LLDB clients
- **[Universal Debug Bridge](./udb-protocol.md)** - High-performance analysis and profiling

### Navigation
- **[Interface Documentation Index](./README.md)** - Overview of all control interfaces
