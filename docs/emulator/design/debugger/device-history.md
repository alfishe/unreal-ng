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

A **filter** says which records are recorded at all: the others are counted (`filtered`) and go nowhere, neither into
memory nor into the stream. A capture of a busy device then holds what is studied, not everything: a GS card's history
without one wrote 1.7 GB for a 2600-frame tape load, nearly all of it the firmware's idle polls and interrupts; with
`side=host out=true` it holds the host's writes. The text is the same on every plane:

```text
side=host out=true             every term must hold
port=#B3,#BB pc=#8000-#9FFF    a term holds when one of its values does; a-b is an inclusive range
```

A term is `field=value[,value...]`, terms are separated by spaces. A field is a name of the record as the history
shows it. A value is a number (decimal; hex as `#B3`, `$B3`, `0xB3`), a range, or a word (`host`, `out`, `true`). A
record without the field does not pass. The filter tests a record when it is pushed: a field the device completes later
(a command's final status) is tested with the value it has then. The busy histories (the GS card, the port histories,
the execution histories) compile the filter into field getters and refuse a field or a word their records do not have,
naming the valid ones; the others test the rendered record. An empty filter records everything. Code:
`core/src/debugger/history/historyfilter.h`.

**Pause** holds a history in any mode: nothing is recorded, what it has stays readable; resume goes on with it. A
configuration starts unpaused.

A device path takes the history's lock per record (`Push`). A history fed at instruction rate takes `PushFast`
instead (`EnableFastPush`): one writer, no lock; the record goes into a stage of 1024 slots with a release store, and
whoever takes the lock next (a reader, a configuration, the writer on a full stage) moves the staged records into the
history in order. A history may tell its gate when it turns on or off (`OnSwitch`): the main CPU's raises a step-work
bit, so the processor pays nothing while it is off.

Nothing is recorded while TTD replays silently (a seek re-executes what the history already holds).

A history may follow time travel (`history::ITimeFollower`, registered in the machine's registry with
`AddFollower`); the GS command log does (gs-debugger design §6.3, D3). The controller tells the followers when a
checkpoint is restored (`TimeRestored`, any restore), when a move ends where the machine stands (`TimeLanded`, from
`PresentPosition` and `SwitchBranch`, with the moment two lines part for a switch to another line), and saves and
loads their derived state around a query (`SaveLiveState` / `RestoreLiveState`). Two generic cuts serve them:
`Rewind(after)` holds the newest records after the moment back (not kept, not counted) and keeps held ones again once
the machine is past them; `Truncate(after, dropHeld)` drops the records of a line the machine left. A record kept
again is not streamed twice; the stream file is never cut. Histories that do not follow keep everything they saw.

A configuration starts afresh: the records, the counts and the stream file (opened again, truncated). Fields a request
leaves out keep their value.

## The histories

| Device | What | Starts as | Record |
|--------|------|-----------|--------|
| `beta128` | WD1793 commands | ring of 64 | `start` / `end` / `duration` (T-states of the chip's clock), `frame`, `pc`, `command`, `name`, `track`, `sector`, `data` (when written), `drive`, `side`, `done`, `status` (at the end). A force interrupt ends at once; the command it cut short stays `done: false` |
| `plusd` (the MGT +D card) | WD1772 commands | ring of 64 | the same as `beta128` |

| the AY ports' owner: `ay` (built-in, the Next too), `ay-socket` or `zxbus.N` (a TurboSound / TSFM card shadowing it) | AY register writes | off | `frame`, `tacts`, `pc`, `port` (#FFFD select, #BFFD data), `chip` (0-1; the Next's three AYs 0-2), `type` (`select` / `switch` / `write`), `reg`, `reg_name`, `value`. One history per machine whatever device takes the AY ports (the TurboSound pair, the TSFM, the Next's NextAudio); the AY log analyzer (`/ay/log`, `aylog_*`) is its face: activation turns it to a ring of the analyzer's capacity, deactivation off |

| the GS card's slot (`zxbus.1`; `gs` when the registry names none) | GS port and activity events | off | `timestamp` (GS cycles, 12 MHz), `frame`, `side` (`host` #B3 / #BB / #33, `gs` the card's ports, `dac` sample fetches, `interrupt`, `zx-dma` the NeoGS host window), `port`, `pc` (the GS CPU's; the host's for `zx-dma`), `value`, `channel`, `out` / `nmi`. One per machine: every personality the slot takes (LLE, LW, NeoGS) records into it, so a personality switch keeps it. The GS port trace (CLI `gsporttrace`, REST `/control/audio/gs/porttrace`, Python / Lua `gs_porttrace_*`) is its face: start = a ring of 65536, pause / resume, stop = paused with the events kept |

| the MoonSound's registry id (the owner of `#C4`; `moonsound`) | OPL4 register writes | off | `frame`, `tacts`, `pc`, `chip` (`opl4` FM: `reg` = bank << 8 \| register; `opl4wave`), `unit`, `reg`, `value` |
| the Covox's (the owner of `#FB`: `covox`, `covox-fb`, `soundrive`; `covox`) | DAC writes | off | the same, `chip` `dac`, `reg` the channel |
| the Fuller Box's slot (`edge.1`) | AY register writes | off | the same, `chip` `ay` |
| the ZX-MultiSound's slot + `.chips` (`zxbus.1.chips`) | SAA and SounDrive writes | off | the same, `chip` `saa` / `dac`; its YM writes into the machine's AY history (the card takes the AY ports, so that history carries the card's name), its GS into `zxbus.1.gs` |
| `covox-blaster` (the Sprinter) | Covox Blaster writes | off | the same, `chip` `cbl`, `reg` 0 data, 1 control |
| an SD card's host: `sd-zc` (the Z-Controller on TS-Conf / ATM3, the owner of `#57`), `sd0` / `sd1` (the Next), the DivMMC's id + `.sd0` / `.sd1`, the NeoGS slot + `.sd` (`zxbus.1.sd`) | SD commands and block transfers | off | `frame`, `us` (the card's clock), `type` (`command` / `read` / `write`), `cmd` (`CMD17`, `ACMD41`), `argument`, `crc_ok`, `r1`; for a block `block`, `ok` |
| the NeoGS slot + `.mp3` (`zxbus.1.mp3`), when the card has its VS10xx | MP3 decoder SCI writes | off | `frame`, `reg`, `reg_name` (`MODE`, `CLOCKF`, `VOL` ...), `value` |
| `ts-registers` (TS-Conf, the owner of `#xxAF`) | FPGA register writes | off | `frame`, `tacts`, `pc`, `reg`, `reg_name` (`BORDER`, `VCONFIG`, `DMACTRL` ...), `value` |
| `ts-registers.dma` | DMA transfers, one record a launch | off | `frame`, `tacts`, `pc`, `device` (`ram`, `spi-in` / `spi-out`, `ide-in` / `ide-out`, `fill`, `blt1` / `blt2`, `cram`, `sfile`), `flags`, `source`, `destination`, `burst`, `blocks`; at the end `done`, `end_frame`, or `aborted` |
| `vdac2` (the TSL-VDAC2 build) | FT812 SPI transactions, one record a chip select window | off | `frame`, `clock`, `type` (`read` / `write` / `command`), `address` or `command` + `parameter`, `length`, `data` (the first 8 bytes, hex) |
| `<device>.ports` for every registry device (`beta128.ports`, `ay.ports`, `zxbus.1.ports`), `board.ports` for the ports no device claims | port accesses (IN / OUT of the main CPU), each device apart | off | `frame`, `tacts`, `pc`, `dir` (`in` / `out`), `port`, `value`, `dos` (the TR-DOS ROM was paged). The owner is the registry's (`slots::PortOwners`, the same as the port trace's `owner`). Off, all of them: one relaxed load of the count of those on an IN / OUT. On: one table load (port, direction, DOS gate to the device; built at the first access after the machine's devices change) and a push into that device's history only; an access whose owner's history is off costs the load and the test. The machine-wide port trace (`porttrace`) stays the decode-debugging tool |
| `cpu` (the machine's main CPU) | instructions it starts | off | `frame`, `pc`, `kind` (`rom` / `ram` / `cache`), `page` (the physical page its window showed). Its own step-work bit (`kStepWorkExecHistory`) while on, beside the debugger's PC history (`/debug/pchist`) on the same hook; `PushFast`; a ring of 65536 without a limit |
| the GS card's slot + `.cpu` (`zxbus.1.cpu`); the ZX-MultiSound's GS `<slot>.gs.cpu` | instructions the card's Z80 starts (classic GS, NeoGS) | off | the same as `cpu`, the card's pages (`rom` the ROM / flash, `ram`). Off: one load and a compare per card run; on: the card runs its debug loop with the history's recorder in front of a card debugger's hooks (`GeneralSoundCard::setDebuggerHooks`) |
| the GS card's slot + `.commands` (`zxbus.1.commands`); the ZX-MultiSound's GS `<slot>.gs.commands` | the host's commands to the card, decoded by the firmware profile (gs-debugger design §6.3, `gscommandlog.h`) | off | `kind` (`command`, `param`, `data`, `reply`, `block`, `card_took`, `anomaly`, `reset`), `card_cycle`, `frame`, `t`, `pc` (the main CPU's), `command`, `name` (the profile's), `profile` (`gs104` / `gs105a` / `gs105b`, empty: raw), `text` (one decoded line); per kind `pre`, `value`, `index`, `param`, `bytes`, `dest`, `address`, `anomaly` (`data_overwritten`, `command_overwritten`, `reply_not_ready`, `transfer_stalled`), `detail`, and the flags `stream`, `not_ready`, `unasked`, `truncated`, `automation`. Every personality feeds it (LLE, LW, NeoGS); an upload is one `block` entry. Off: a null test and one relaxed load per host access of the card |
| `nextreg`, `next-int`, `divmmc` (the ZX Next) | NextREG writes (instruction, port, copper, board); interrupts, NMIs, RETNs, resets; DivMMC map-ins, map-outs, `#E3` writes, esxDOS calls | off | the journals' events as their reports render them. The journals' controls and queries (`/state/next/...`, `next_*_journal`) drive the same history: enable = a ring of the journal's capacity, disable = pause. The Next's SD journal stays the journal's own (`sd0`, `sd1` are the cards' histories) |
| `copper` (the ZX Next) | instructions the copper completes: its second processor's execution history | off | `frame`, `pc` (the list index), `kind` (`wait` / `move` / `nop`), `reg`, `value`, the beam's `line` and `hc`. Recorded in the copper's step watcher, before a breakpoint can hold the step |
| `dma` (the ZX Next) | blocks the zxnDMA finishes | off | `frame`, `tacts`, `pc`, `source` / `destination` with their space (`memory` / `io`), `length` (WR0), `mode` (`byte` / `continuous` / `burst`), `restart` |
| `uart` (the ZX Next) | bytes the two UARTs send and receive | off | `frame`, `at` (the UART clock the byte's frame ended on), `channel` (`esp` / `pi`), `dir` (`tx` / `rx`), `value`, `error` (framing, parity, overflow, lost) |

A device whose history is its own holds a `history::Registered<Record>`: listed in the machine's histories while it
lives. A card with several modules names the others `<slot>.<module>` (as the time-travel instances do).

The FDC state report (`state fdc`, `/state/fdc`, `fdc_state()`, MCP aspect `fdc`) carries the same history as
`history` and its stats as `history_state`.

## Planes

| Plane | List | Read | Configure | Clear |
|-------|------|------|-----------|-------|
| CLI | `history` | `history <device> [N] [since=S]` | `history <device> mode <off\|ring\|capture> [limit=N] [stream=<file>] [stream_limit=N] [filter=<term>]...` | `history <device> clear` |
| REST, WebSocket `call` | `GET /api/v1/emulator/{id}/history` | `GET .../history/{device}?last=N[&since=S]` | `POST .../history/{device}/config` `{"mode", "limit", "stream", "stream_limit", "filter"}` | `POST .../history/{device}/clear` |
| MCP | `device_history` `action=list` | `action=read device= last= since=` | `action=config device= mode= limit= stream= stream_limit= filter=` | `action=clear device=` |
| Python | `emu.history_list()` | `emu.history(device, last=0, since=None)` | `emu.history_config(device, mode=None, limit=None, stream=None, stream_limit=None, filter=None)` | `emu.history_clear(device)` |
| Lua | `history_list()` | `history(device, last, since)` | `history_config(device, {mode=, limit=, stream=, stream_limit=, filter=})` | `history_clear(device)` |

### Reading after a cursor

A read without `since` answers the stats and the newest `last` records (0: all), oldest first. A poller that must not
lose a record between two reads gives `since`, a cursor: every record has a number, `seq` (1 for the first record
written since the last clear or configuration; a record keeps its number while it is kept: a ring evicts the oldest,
a capture keeps the first `limit`). With `since` the read answers the kept records numbered after it, oldest first, at
most `last` of them (0: all): the first `last` after the cursor, so the next read goes on where this one stopped. Each
record carries its `seq`, and the reply adds:

| Field | Meaning |
|-------|---------|
| `next_since` | the cursor of the next read: the last returned record's number (`since` itself when nothing came) |
| `missed` | records numbered after `since` the history no longer keeps: evicted from a ring before they were read; for a capture, once the reader has every kept record, the ones over its cap (then `next_since` goes past them to `written`) |
| `restarted` | `since` was past the newest number written: the history was cleared or configured since (or time travel cut it back); the read starts from the oldest kept, `missed` counted from 0 |

```json
{"device": "zxbus.1.commands", "what": "GS host commands, decoded by the firmware profile", "mode": "ring",
 "limit": 4, "written": 9, "kept": 4, "dropped": 5, "paused": false,
 "records": [{"seq": 6, "kind": "command", "...": "..."}, {"seq": 7, "kind": "command", "...": "..."}],
 "next_since": 7, "missed": 2, "restarted": false}
```

(`GET .../history/zxbus.1.commands?last=2&since=3`: 4 and 5 were evicted unread, 6 and 7 come, 8 and 9 at the next
read.) Without `since` none of these fields appear and the records have no `seq`. The read walks the kept records once
under the history's lock and copies only the ones it returns; the emulation thread is never parked. A clear followed
by more records than the old cursor before the next read is not seen as a restart (the numbers alone cannot tell).
The GS monitor of both debuggers reads this way: it fills with the newest 200, then pages after its cursor.

Pause and resume: CLI `history <device> pause` / `resume`; REST `POST .../history/{device}/pause` / `/resume`; MCP
`action=pause` / `resume`; Python `emu.history_pause(device, paused=True)`; Lua `history_pause(device[, paused])`. The
stats carry `paused`.

Errors: an unknown device is 404 (Python `KeyError`), a bad mode, a stream file that cannot be opened or a `since`
that is not a number 400 (Python `ValueError`); Lua and the CLI answer with the `error` text.

## Adding a device

1. A record type: plain values and `void ToState(StateNode&) const`.
2. A member `history::DeviceHistory<Record> _history{ "<registry id>", "<what>", <ring size> };` configured in the
   constructor (the standby mode the device starts in) and registered in `context->histories` while it lives.
3. On its path: `if (_history.On() && !context->ttdReplayActive) [[unlikely]] _history.Push(record);`, and
   `UpdateLast` when a record completes later. Several devices that take the same ports (the AY on the TurboSound,
   the TSFM, the Next) share one history owned above them, named by the port's registry owner
   (`slots::PortOwners`).
4. A row in the table above. The planes need nothing else.
