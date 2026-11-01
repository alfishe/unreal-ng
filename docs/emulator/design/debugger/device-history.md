# Device histories

The history of each peripheral's own state machine: the commands of a WD1793, later the register writes of an AY /
TurboSound / TSFM, the host traffic of a GS / NeoGS, the writes of an OPL4 or a SAA. One pattern for every device
(`core/src/debugger/history/devicehistory.h`), one set of verbs for every automation plane
(`core/src/debugger/history/historycontrol.h`). Design: [debugger device I/O](../../../inprogress/2026-10-31-debugger-device-io/README.md).

## Names

A history is named by the device's id in the machine's registry, the slot manager's report (`GET /slots`): a built-in
by its id (`beta128`), a card by its slot (`zxbus.1`). The port trace names its events by the same ids (`owner`).

## Modes

| Mode | Cost on the device's path | Keeps |
|------|---------------------------|-------|
| `off` | one relaxed load, a branch not taken; no memory | nothing |
| `ring` | a copy into a preallocated ring | the last `limit` records: the standby mode |
| `capture` | a copy into a growing buffer | every record up to `limit` (`0`: no limit), then it counts the dropped ones |

With `ring` or `capture`, a **stream** writes every record to a file as well: JSON Lines, one record per line, by a
writer thread of the history's own (the device's path copies the record into its queue; rendering and file I/O happen
there), up to `stream_limit` records (`0`: no limit). A record goes to the file once it is complete (a command with its
status): when the next one comes, or the stream stops.

**Pause** holds a history in any mode: nothing is recorded, what it has stays readable; resume goes on with it. A
configuration starts unpaused.

Nothing is recorded while TTD replays silently (a seek re-executes what the history already holds).

A configuration starts afresh: the records, the counts and the stream file (opened again, truncated). Fields a request
leaves out keep their value.

## The histories

| Device | What | Starts as | Record |
|--------|------|-----------|--------|
| `beta128` | WD1793 commands | ring of 64 | `start` / `end` / `duration` (T-states of the chip's clock), `frame`, `pc`, `command`, `name`, `track`, `sector`, `data` (when written), `drive`, `side`, `done`, `status` (at the end). A force interrupt ends at once; the command it cut short stays `done: false` |

| the AY ports' owner: `ay` (built-in, the Next too), `ay-socket` or `zxbus.N` (a TurboSound / TSFM card shadowing it) | AY register writes | off | `frame`, `tacts`, `pc`, `port` (#FFFD select, #BFFD data), `chip` (0-1; the Next's three AYs 0-2), `type` (`select` / `switch` / `write`), `reg`, `reg_name`, `value`. One history per machine whatever device takes the AY ports (the TurboSound pair, the TSFM, the Next's NextAudio); the AY log analyzer (`/ay/log`, `aylog_*`) is its face: activation turns it to a ring of the analyzer's capacity, deactivation off |

| the GS card's slot (`zxbus.1`; `gs` when the registry names none) | GS port and activity events | off | `timestamp` (GS cycles, 12 MHz), `frame`, `side` (`host` #B3 / #BB / #33, `gs` the card's ports, `dac` sample fetches, `interrupt`, `zx-dma` the NeoGS host window), `port`, `pc` (the GS CPU's; the host's for `zx-dma`), `value`, `channel`, `out` / `nmi`. One per machine: every personality the slot takes (LLE, LW, NeoGS) records into it, so a personality switch keeps it. The GS port trace (CLI `gsporttrace`, REST `/control/audio/gs/porttrace`, Python / Lua `gs_porttrace_*`) is its face: start = a ring of 65536, pause / resume, stop = paused with the events kept |

| the MoonSound's registry id (the owner of `#C4`; `moonsound`) | OPL4 register writes | off | `frame`, `tacts`, `pc`, `chip` (`opl4` FM: `reg` = bank << 8 \| register; `opl4wave`), `unit`, `reg`, `value` |
| the Covox's (the owner of `#FB`: `covox`, `covox-fb`, `soundrive`; `covox`) | DAC writes | off | the same, `chip` `dac`, `reg` the channel |
| the Fuller Box's slot (`edge.1`) | AY register writes | off | the same, `chip` `ay` |
| the ZX-MultiSound's slot + `.chips` (`zxbus.1.chips`) | SAA and SounDrive writes | off | the same, `chip` `saa` / `dac`; its YM writes into the machine's AY history (the card takes the AY ports, so that history carries the card's name), its GS into `zxbus.1.gs` |
| `covox-blaster` (the Sprinter) | Covox Blaster writes | off | the same, `chip` `cbl`, `reg` 0 data, 1 control |

A device whose history is its own holds a `history::Registered<Record>`: listed in the machine's histories while it
lives. A card with several modules names the others `<slot>.<module>` (as the time-travel instances do).

The FDC state report (`state fdc`, `/state/fdc`, `fdc_state()`, MCP aspect `fdc`) carries the same history as
`history` and its stats as `history_state`.

## Planes

| Plane | List | Read | Configure | Clear |
|-------|------|------|-----------|-------|
| CLI | `history` | `history <device> [N]` | `history <device> mode <off\|ring\|capture> [limit=N] [stream=<file>] [stream_limit=N]` | `history <device> clear` |
| REST, WebSocket `call` | `GET /api/v1/emulator/{id}/history` | `GET .../history/{device}?last=N` | `POST .../history/{device}/config` `{"mode", "limit", "stream", "stream_limit"}` | `POST .../history/{device}/clear` |
| MCP | `device_history` `action=list` | `action=read device= last=` | `action=config device= mode= limit= stream= stream_limit=` | `action=clear device=` |
| Python | `emu.history_list()` | `emu.history(device, last=0)` | `emu.history_config(device, mode=None, limit=None, stream=None, stream_limit=None)` | `emu.history_clear(device)` |
| Lua | `history_list()` | `history(device, last)` | `history_config(device, {mode=, limit=, stream=, stream_limit=})` | `history_clear(device)` |

Pause and resume: CLI `history <device> pause` / `resume`; REST `POST .../history/{device}/pause` / `/resume`; MCP
`action=pause` / `resume`; Python `emu.history_pause(device, paused=True)`; Lua `history_pause(device[, paused])`. The
stats carry `paused`.

Errors: an unknown device is 404 (Python `KeyError`), a bad mode or a stream file that cannot be opened 400 (Python
`ValueError`); Lua and the CLI answer with the `error` text.

## Adding a device

1. A record type: plain values and `void ToState(StateNode&) const`.
2. A member `history::DeviceHistory<Record> _history{ "<registry id>", "<what>", <ring size> };` configured in the
   constructor (the standby mode the device starts in) and registered in `context->histories` while it lives.
3. On its path: `if (_history.On() && !context->ttdReplayActive) [[unlikely]] _history.Push(record);`, and
   `UpdateLast` when a record completes later. Several devices that take the same ports (the AY on the TurboSound,
   the TSFM, the Next) share one history owned above them, named by the port's registry owner
   (`slots::PortOwners`).
4. A row in the table above. The planes need nothing else.
