# TTD port journals: replay without the outside world, and "when did the program ..."

- **Date:** 2026-09-29
- **Status:** built. First landed in v1, which was deleted on 2026-10-30 (TTD v2
  migration, Phase 6). The engine records the port journals as its bus journals
  ([port-journals-on-engine.md](../../../../inprogress/2026-09-25-ttd-v2-migration/port-journals-on-engine.md)).
  The v1 file layout below (flag bit 8, `ttd_format.py`, v1's `ttd.ksy`) is history;
  the engine's file stores the journals as streams of schema 2 (`ttd.ksy`).
- **Code:** `core/src/debugger/ttd/ttdportjournal.{h,cpp}` (the journals),
  `ttdportsearch.{h,cpp}` (the search), the hooks in `Z80::in` / `Z80::out`
  (`core/src/emulator/cpu/z80.cpp`), the lifecycle in `TimeTravelController`
  (`timetravelcontroller.cpp`), the journals in the engine (`TimeTravelEngine::BusReads` /
  `BusWrites`).
- **Related:** [ttd-v1-architecture-and-format.md](ttd-v1-architecture-and-format.md)
  (history: TTD v1 as a whole), TTD v2 requirements FR-20 and FR-21
  ([requirements.md](../../../../inprogress/2026-09-25-ttd-v2-migration/requirements.md) §2.5).

## 0. Summary

A TTD replay used to re-run the machine together with its devices: the tape
player read the tape file again, the disk controller read the disk image
again. If the file had changed, or was missing, the replay went its own way -
silently.

The outside world reaches a classic ZX Spectrum's CPU through one door only:
the `IN` instruction. So TTD now records the result of every `IN` the CPU
executes while a session records - and, as facts of the machine's output,
every `OUT`. Each record carries the moment of the access (frame and T-state)
and the address of the instruction. On replay, the CPU gets the recorded `IN`
value instead of whatever the device answers now, and every `OUT` is checked
against the record. The replay no longer depends on any file or host device.

The same records answer "when did the program ..." questions without
replaying anything: when it first saw a key pressed, when it saw the tape
signal change, when it wrote AY register 7, when the border changed (§10).

Cost, measured on real recordings (§6): the ROM loading a game from tape -
the heaviest ordinary case, 36,000 `IN` a second - adds about **7.7 KB a
second** to the session file, 13% of it; a game played from the keyboard
(Dizzy X) about 1.7 KB a second, under 1%. One recorded access costs about
10 ns; a search scans about 60 million records a second.

## 1. The problem, in one example

A session is recorded while a game loads from `game.tap`. The file is saved
and sent to someone else. They open it without `game.tap`, or with a slightly
different copy of it.

- **Before:** seeking into the loading screen re-executes the recorded frames.
  The loader polls the tape's EAR bit; the tape player, with no tape, answers
  "no signal". The loader never sees the edges it saw during the recording,
  takes other branches and ends somewhere else. Nothing reports it.
- **Now:** the loader's `IN A,(#FE)` returns the value recorded at that exact
  read. The loader sees the same edges, runs the same instructions and arrives
  in the same state as the recording. The session status counts how many
  replayed reads the live machine answered differently
  (`port_replay_value_mismatches`), so the missing tape is visible, not
  harmful.

The test `TimeTravelController_PortReadJournal_Test.LoadedSessionReplaysTheTapeWithoutTheTape`
does exactly this; its control test strips the journal from the same file and
shows the replay diverge.

## 2. Scope of the first version

**The engine (the default backend since Phase 5) records the journals on every machine** and plays them in every
replay; the CPU writes straight into the engine's bus journals (2026-10-09,
[port-journals-on-engine.md](../../../../inprogress/2026-09-25-ttd-v2-migration/port-journals-on-engine.md)), and
`port_journal_active` is true unless the session has a gap. The scope below is v1's (history: v1 was deleted in
Phase 6).

**On:** every classic machine - Pentagon, 48K, 128K, +2, +2A, +3, Scorpion,
Profi, ATM, and the rest - as long as the General Sound slot does not hold a
NeoGS card.

**Off**, with the reason in the session status (`port_journal_off_reason`):

| Configuration | Why it is not isolated yet |
|---|---|
| TSConf | its DMA moves data into RAM without the CPU reading a port |
| ZX Next | the same |
| NeoGS in the GS slot | its ZX-DMA serves host memory reads from the card without an `IN` |

On these configurations nothing changes: replay reads the live devices as
before. DMA is out of scope for the first version (TTD v2 FR-21).

**Interrupt vectors.** In IM 2 the CPU also reads a byte from the data bus when
an interrupt is accepted. On the current classic clones nothing outside
drives that byte; it comes from the floating bus, which is emulated
deterministically, so it does not need recording. If TSConf or Sprinter turn
out to supply the vector from a device, the vector joins the journal with
DMA (TTD v2 FR-21).

## 3. What is recorded

Two journals, one per direction. The **IN journal** holds every value the
main CPU reads with an `IN`:

- `IN A,(n)` and `IN r,(C)` (including the undocumented `IN (C)`);
- the block instructions `INI`, `INIR`, `IND`, `INDR` - one record per
  iteration, each with the port as `BC` was at that moment.

The value is what the CPU received: after the model's port decoder, the
observer cards (for example a MoonSound on a shared port), the floating bus
and a ZX-Poly interceptor.

The **OUT journal** holds every `OUT` (`OUT (n),A`, `OUT (C),r`, `OUTI`,
`OTIR`, `OUTD`, `OTDR`): the machine's output, logged as facts.

Each record is:

| Field | Size | Meaning |
|---|---|---|
| frame | - | frame counter at the access |
| T-state | - | position in the frame, at the model's top clock (like every TTD time) |
| PC | 2 bytes | address of the `IN` / `OUT` instruction |
| port | 2 bytes | the full 16-bit port address |
| value | 1 byte | read (as the CPU got it) or written |

**Replay needs only the order.** A replay is deterministic, so the 1,000th
`IN` of a replay is the 1,000th `IN` of the recording. Every checkpoint stores
both journals' positions at its capture (`TTDCheckpoint::portReadCursor`,
`portWriteCursor`), and a replay from a checkpoint starts there.

**The time and PC are stored anyway** - redundantly, bound to the moment the
CPU made the access - because they make the journals searchable without a
replay, and because they make the replay check strict: an access at another
T-state or from another instruction means the execution left the recording.
In a polling loop the PC repeats and the T-state advances by the same step,
so after compression they cost about as much as the values (§6).

The card CPUs (General Sound, NeoGS, MoonSound) are not recorded: their own
ports are inside the card, and the card's state is restored from its
checkpoint blob.

## 4. Replay

While a session replays, `Z80::in` still asks the bus. The device sees the
read and does whatever reading does to it: an FDC advances its data
register, a keyboard read is counted. Then the journal replaces the value the
CPU gets with the recorded one. `Z80::out` writes to the device as always,
and the journal compares the access with its record.

- **Same answer:** nothing happens besides the substitution.
- **A read's value differs:** `port_replay_value_mismatches` grows. Typical
  cause: a medium that is missing or changed, a host device.
- **Another time, instruction or port, or an `OUT` of another value:**
  `port_replay_divergences` grows. The replay ran other code than the
  recording; this should not happen.
- **End of the journal:** the recorded history is over; the accesses go to
  the live devices again.

The first mismatch and the first divergence are kept with the journal
position and both records (`TTDPortJournal::FirstMismatch`,
`FirstDivergence`), for diagnostics.

Keyboard, mouse and General Sound input is still applied by the input journal
during replay. The CPU no longer depends on it - it gets the recorded port
values either way - but the devices do: with the input journal the keyboard
matrix a debugger shows during replay matches the recording.

## 5. Lifecycle

| Event | What the journal does |
|---|---|
| `StartRecording` | both cleared; they record if the configuration is supported, otherwise off with the reason |
| each frame's checkpoint | the checkpoint stores both journals' sizes as its cursors |
| `StopRecording` | stops recording; the machine runs on unrecorded |
| restore for replay (seek, reverse query, step back, frame cache, display) | plays from the checkpoint's cursor |
| live-state snapshot around a throwaway replay | mode and position saved and restored with the snapshot |
| `ResumeRecordingFrom(point)` | cut at the position the seek reached; records the new history after it |
| `ResumeRecordingLive` right after a stop | records on |
| `ResumeRecordingLive` after the machine ran while stopped | **given up** for the session: the reads made meanwhile were not recorded, and a gap would shift every later record. Replay reads the live devices again; the reason says so. |
| machine reset while replaying | stops playing: the machine left the recorded history |
| `InvalidateSession` | cleared, off |
| save | written as flag bit 8 when the session holds every read of its history |
| load | read and checked (every block decompressed and CRC-checked); a file without it replays against the live devices |

The CPU hooks are pointers in `EmulatorContext` (`ttdPortReads`,
`ttdPortWrites`), set only while the journals record or play. With no session
they cost one predictable branch per `IN` / `OUT`.

## 6. Storage

Records are kept in blocks of 32,768. A full block is compressed with zstd
(level 1, like the rest of TTD) and gets a CRC32C of its raw bytes; the newest
block stays raw until it fills.

The raw block is **five columns**, each holding one field of every record:

```
ports:   FE 7F FE 7F FE 7F ...   u16, one per record
values:  FF FF BF BF BF FF ...   u8
PCs:     ED 05 ED 05 ED 05 ...   u16
frames:  00000000 00000000 ...   u32: frames since the previous record (0 for the first)
T:       00008594 0000002F 0000002F ...   u32: absolute for the first record of a
                                          frame, else the step from the previous record
```

Grouping the fields this way puts long runs of identical bytes next to each
other, which is what zstd compresses best. A tape loader's 32,768 reads of
`#7FFE` from one instruction, 47 T-states apart, become a few bytes of ports,
PCs, frames and T-states; the values change only at a tape edge.

**Real recordings** - the fixtures in `testdata/ttd/port-journals/`
(recorded by `record_port_journal_fixtures.py`, reproducible):

| | Dizzy X, keys pressed | Green Beret loading from tape (128K ROM loader) |
|---|---|---|
| length | 495 frames (9.9 s) | 1,010 frames (20.2 s) |
| IN / OUT | 7,905 / 14,920 | 722,025 / 28,859 |
| raw journals | 290 KB (13 bytes a record) | 9.8 MB |
| journals in the file | 16.8 KB (1.7 KB a second) | 155.6 KB (7.7 KB a second) |
| whole session file | 2.09 MB | 1.17 MB |
| journals' share of the file | 0.8% | 13% |

The ROM loader is the worst case: 36,000 reads a second whose T-state step
changes with every tape edge and bit. A synthetic poller with a fixed 47 T
loop (benchmark `BM_TTD_PortJournal_TapeSession`) gets 2.7 KB a second; the
real loader costs almost three times that. Without the time and PC columns
(the first version) the synthetic session took 6.2 KB instead of 15.9 KB.

## 7. The file section (flag bit 8)

Written after the external-event section (bit 7), only when the session holds
all of its history's I/O: the IN journal, then the OUT journal, each laid out
as below.

| Field | Type | Notes |
|---|---|---|
| `record_count` | u64 | records in the journal |
| `block_records` | u32 | 32768; any other value is refused |
| `block_count` | u32 | must equal ceil(record_count / block_records) |
| per block: `records` | u32 | 1..32768; every block but the last is full |
| per block: `base_frame` | u64 | the frame of the block's first record |
| per block: `crc32c` | u32 | of the raw block (the five columns) |
| per block: `compressed_size` | u32 | at most 32768 x 13 + 4096 |
| per block: payload | bytes | zstd frame of `records x 13` bytes |
| `cursor_count` | u32 | must equal the header's checkpoint count |
| per checkpoint: `cursor` | u64 | non-decreasing, at most `record_count` |

A reader refuses the file on any violation: truncation, a block that does not
decompress to its size, a CRC mismatch, records out of time order, counts
that disagree, cursors out of order or past the end. The load leaves the live
session untouched.

## 8. Status fields

On every automation surface (WebAPI `GET /ttd/status`, MCP, CLI `ttd status`,
Lua, Python):

| Field | Meaning |
|---|---|
| `port_journal_active` | the session holds every `IN` and `OUT` of its history: replay is isolated, and the journals can be searched |
| `port_journal_off_reason` | why they are off (configuration, a gap, a file without them); null / empty while on |
| `port_read_count`, `port_write_count` | records |
| `port_journal_bytes` | both journals' compressed size in a file |
| `port_replay_value_mismatches` | replayed reads the live machine answered differently (the CPU got the recorded value) |
| `port_replay_divergences` | replayed accesses at another time, from another instruction, to another port, or `OUT`s of another value (execution left the recording; expected 0) |

## 9. Measurements

- **Size:** §6 - about 7.7 KB a second while the ROM loads from tape, the
  heaviest ordinary case; about 1.7 KB a second for a game played from the
  keyboard (its sound and keyboard I/O, 800 IN and 1,500 OUT a second).
- **One access** (`BM_TTD_PortJournal_Record` / `_Play`): about 10 ns
  recorded, about 15 ns replayed. At 74,000 reads a second that is about
  0.75 ms of every emulated second (0.075%).
- **Search** (`BM_TTD_PortSearch_Ear`): one million records scanned in about
  16 ms.
- **Per frame, whole machine:** the A/B frame benchmarks (`BM_Frame_TTD_*`,
  base 435eb0cc against the branch) are still to be run on an idle machine;
  the numbers above were taken while other builds kept the load average above
  100, so they are upper bounds.

## 10. "When did the program ...": searching the journals

The journals are searched directly (`ttdportsearch.h`,
`TimeTravelController::SearchPortEvents`). Nothing is replayed, so a search is
instant and answers the same on a loaded `.ttd` file as on the live session.
A running recording is searched too: the search holds the CPU while it reads
the journals (they grow on the thread that runs the machine), and answers an
error only when the hold cannot be taken during a long run. Sessions without
the journals are refused.

**A saved file** is searched without loading it
(`TimeTravelController::SearchPortEventsInFile`; `file` on every surface): the
file is read and checked like a load, but nothing is committed - the
instance keeps its own session, even a recording in progress, and the
file's machine model and ROM do not matter. Until v1 was deleted the offline
analyzer answered the same questions (`run.sh search`, module
`src/port_search.py`; history: the analyzer now reads schema 2 with `info`,
`validate`, `parts` and `recover` only). The live search and the file search
are checked against each other on the real recordings:
`record_port_journal_fixtures.py` asks 27 questions of the live session and of
the file (checked equal) and writes the answers to `expected.json`, which the
C++ test `TimeTravelController_PortJournalFixture_Test` reproduces. The
analyzer's `tests/test_port_search.py` did too; it was deleted with v1's reader.

**Named events**, the same on every surface (CLI `ttd port-events`, WebAPI
`POST /ttd/port-events`, MCP `time_travel` action `port_events`, Lua and
Python `ttd_port_events`):

| Event | Finds | Argument |
|---|---|---|
| `key` | the first read that shows a key down, once per press | a key name; only reads of that key's half-row alone count |
| `ear` | every change of the tape bit (port #FE bit 6) the program saw | - |
| `ay-read` | IN from #FFFD | an AY register 0..15 |
| `ay-write` | OUT to #BFFD | an AY register 0..15 |
| `ay-select` | OUT to #FFFD | a register: only selections of it |
| `border` | OUT #FE that changed the border color | - |
| `beeper` | OUT #FE that changed the beeper bit | - |
| `in`, `out` | every IN / OUT, narrowed by the raw filters | - |

**Why a named key needs its half-row alone.** A program that reads all eight
half-rows at once (`XOR A; IN A,(#FE)`) sees bit 0 low for A, Q, 1, 0, P,
ENTER, SPACE and CAPS SHIFT alike: it knows "some key in that column is down",
not which. `key a` therefore counts only reads that select A's half-row
alone - what the ROM's KEY-SCAN and most games do. `key` with no name finds
the all-rows reads too.

**Who read it.** A hit names more than the record: a ULA read the half-rows
its address selects and the keys it saw down (one half-row only), taken from
the record; and, by default (`context`; off while a recording runs, refused
for a file), the registers at the instruction's M1 and the word at SP - the
caller of a key routine - found by a replay to each hit, the machine put back
(`TimeTravelController::PortAccessContexts`). The journal stays as it is: the
registers are not recorded, so the search itself still replays nothing.

**The AY register** of a read or write is followed through the OUT journal:
the last #FFFD write before the access selects it (values #F0-#FF are
TurboSound / TSFM control and keep the selection).

**Raw filters** narrow any event: port mask and value, value mask and a test
(`any`, `equals`, `any-clear`, `any-set`), a trigger - `every` access,
`rising` (the test starts passing), `change` (the masked value changes) -
compared with the previous access **of the same stream**; a time window, a
limit, newest first.

**Streams.** For `key` a stream is one port address - one half-row - so a
program polling several half-rows in turn does not trigger itself. For
`ear`, `border` and `beeper` it is the ULA's port whatever the high byte
(`stream_mask=0x0001`): the ULA decodes A0 alone. The first version compared
full addresses everywhere, and the real recording caught it: Dizzy X clicks
the beeper with `OUT (C)`, alternating `#10FE` and `#00FE`, and `beeper`
found nothing.

Worked examples, from the real recordings (the outputs are theirs):

Dizzy X - when did the game notice SPACE, and where in its code:

```
ttd port-events key space
1 hit(s), 7905 IN record(s) scanned
  frame 430 t 7824  PC #72B2  port #7FFE  value #FE
```

The hit is the read at which the key was first seen down: the previous read
of the same half-row saw it up. `#72B2` is the game's keyboard routine;
`ttd seek 430 7824` shows the machine at that instant. `key p` finds
nothing: the game reads six half-rows (`#BFFE`, `#EFFE`, `#7FFE`, `#FEFE`,
`#F7FE`, `#FBFE`) and never P's (`#DFFE`) - the search answered a question
the recording raised: P and O are not this game's controls.

The same session, the AY mixer as the music player writes it (register 7:
from `#D86E` up to frame 174, then from `#C176` to the end - the game
switched to another player routine):

```
ttd port-events ay-write 7 limit=3
3 hit(s) (more than the limit), 103 OUT record(s) scanned
  frame 50 t 9202  PC #D86E  port #BEFD  value #18  R7
  frame 51 t 6203  PC #D86E  port #BEFD  value #18  R7
  frame 52 t 6114  PC #D86E  port #BEFD  value #18  R7
```

Green Beret loading - the tape edges the ROM loader saw (`#05F1` is
`IN A,(#FE)` in LD-SAMPLE) and the border stripes it drew (`#0601`):

```
ttd port-events ear limit=3
3 hit(s) (more than the limit), 106 IN record(s) scanned
  frame 155 t 40902  PC #05F1  port #7FFE  value #BF
  frame 204 t 63309  PC #05F1  port #7FFE  value #FF
  frame 204 t 65134  PC #05F1  port #7FFE  value #BF
```

In the same recording `key` (any key) finds two presses: ENTER in the 128K
menu (`#0296`, KEY-SCAN), and the `IN A,(#FE)` at `#0562` in LD-BYTES, which
reads the starting EAR level with A still `#0F` from the border `OUT` before
it - four half-rows selected (port `#0FFE`) while ENTER was still held. Both
are real key-down reads; the second is a loader that does not look at the key
bits.

## 11. Not done yet

- ~~**No writes while replaying** (TTD v2 FR-20).~~ **Done on the engine**
  (migration Phase 3, Step 6): writes a replay makes to write-through media are
  held in memory until it ends (`HostWriteHold`), and a restore sets every medium
  to the checkpoint's version (`MediaUndoLog`).
- ~~**DMA and device interrupt vectors** (TTD v2 FR-21).~~ **Done on the engine**
  (migration Phase 3, Step 1): the bus journals are recorded on every machine,
  sector reads from images go into the media read journal (TS-Conf SD / IDE DMA,
  the NeoGS card's SD), and interrupt vectors are recorded (`BusVectors`).
- **Replay barriers.** On the engine tape and disk markers are crossed (the
  journals carry the data) and debugger edits with their bytes are applied; a
  debugger edit without its bytes, a reset and an unclassified marker still stop
  a seek.
- **Qt panel for the search.** Every automation surface has it; the Qt
  debugger does not show it yet (planned: PLAN #46, debugger-enhancements
  TODO item 7).
- **Other host inputs that do not come through `IN`:** debugger edits and
  automation tasks (`SubmitMachineTask`) are barriers, not replayable events.
