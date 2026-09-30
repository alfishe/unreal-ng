# RZX replay integration: requirements

- **Date:** 2026-09-29
- **Status:** v1 (playback, RZ-F1 … RZ-F15 except F9, RZ-U1 … RZ-U4, RZ-T1 … RZ-T3) implemented 2026-09-29; RZ-F9 (mid-recording snapshots) is R3. Design and as-built notes: [design.md](design.md).
- **PLAN:** row #27 (RZX record / playback and TTD interop, T2); depends on
  #64 (SZX, [2026-09-29-szx-snapshots](../2026-09-29-szx-snapshots/)) for
  machines beyond 48K / 128K.
- **Background (not yet committed to master):** the RZX research article
  `docs/inprogress/2026-09-28-debugger-family/rzx-ttd.md` (format, semantics,
  implementations in 12 emulators, ecosystem, references) and
  `docs/inprogress/2026-09-28-debugger-family/ttd-offline-analysis.md` §6b.
- **Related, in master:** the TTD port journals
  ([ttd-port-read-journal.md](../../emulator/design/debugger/time-travel-debug/ttd-port-read-journal.md)),
  which already record and replay every `IN` inside TTD sessions.

> **In one line.** Play RZX files exactly (the same CPU path as the recording),
> at zero cost to the emulator when no RZX is playing and with a small,
> measured overhead while one is; record RZX later on the same hooks; let TTD
> record while an RZX plays, without making either module depend on the other.

## 1. What RZX needs at run time (answer to "only a loader?")

A loader alone is not enough. Playing an RZX needs four run-time mechanisms
beside the file parser and the start-snapshot load:

| Mechanism | Why |
|---|---|
| **`IN` value substitution** | every `IN` the CPU executes must return the next recorded value, whatever the device answers |
| **Fetch counting** | the recording is framed by the number of R-register increments (opcode and prefix fetches, block repeats, `HALT` cycles; the interrupt acknowledge excluded) |
| **Interrupt schedule** | the maskable interrupt is forced when the frame's fetch count is reached; the machine's own frame interrupt is suppressed |
| **Desync detection** | too many or too few `IN`s in a frame (or a fetch overrun) means the CPU left the recorded path |

Plus policies around them: embedded snapshots mid-stream (multiload),
disabling loader shortcuts that change the CPU path, refusing live input.

## 2. Scope

**In scope (v1, playback)**

- RZX 0.12 and 0.13 files: creator, snapshot (embedded, compressed or not;
  external by file name), input recording blocks (compressed or not, repeat
  frames), security blocks (read and ignored).
- Machines: 48K, 128K, +2, +2A, +3, Pentagon 128 / 512 / 1024, Scorpion —
  every model whose start snapshot we can load (SNA and Z80 now; SZX when #64
  lands).
- Every automation surface and the Qt GUI.

**Later phases**

- Recording RZX (phase 2), on the same hooks.
- TTD interop (phase 3): record TTD while playing; RZX import as a TTD
  recording; TTD → RZX export.

**Out of scope**

- Encrypted ("protected") input blocks: no known use; refused with a message.
- Signature verification: not a trust mechanism (the format's own authors say
  so); signatures are read and reported only.
- Machines whose interrupt is owned by the machine logic (TSConf, Sprinter) or
  whose input reaches memory without `IN` (DMA): refused with a message.

## 3. Functional requirements

**File handling**

| ID | Requirement |
|---|---|
| RZ-F1 | Parse RZX 0.12 / 0.13 from a file or a memory buffer; validate every block length against the file size; never crash on malformed input |
| RZ-F2 | Decompress zlib-compressed snapshot data and input blocks; bound the output size |
| RZ-F3 | Load the first snapshot (embedded or external) through the snapshot loaders; SNA and Z80 now, SZX when #64 is available; an external snapshot is looked up next to the RZX, then by the stored name |
| RZ-F4 | Report the creator, versions, number of frames, blocks, snapshot formats and whether the file is signed |

**Playback**

| ID | Requirement |
|---|---|
| RZ-F5 | Every `IN` form (`IN A,(n)`, `IN r,(C)`, `IN (C)`, `INI`, `IND`, `INIR`, `INDR`) returns the next recorded value of the current frame; the device still sees the read (its side effects happen, as in the TTD port journal) |
| RZ-F6 | Fetch counting follows the specification: +1 per R increment of opcode and prefix fetches, block-instruction repeats and `HALT` cycles; the interrupt acknowledge excluded; `LD R,A` does not disturb the count |
| RZ-F7 | When the frame's fetch count is reached, the next frame starts; if interrupts are enabled, a maskable interrupt is accepted at that point; the machine's own frame interrupt is suppressed during playback |
| RZ-F8 | Repeat frames (IN count 65535) reuse the previous frame's `IN` values |
| RZ-F9 | Snapshot blocks between input blocks are applied at the frame boundary where they occur, without resetting the playback or the frame numbering |
| RZ-F10 | Desync detection: too many `IN`s in a frame, too few at the frame end, or a fetch overrun; **strict** mode stops playback at the first desync and reports the frame, expected and actual counts, and the PC; **tolerant** mode continues and counts |
| RZ-F11 | Conventions as options, defaults chosen for the largest share of files (SkoolKit's): the interrupt after `EI` (default: accept at every frame end; option: honor a short frame after `EI`), the NMOS `LD A,I` / `LD A,R` parity quirk on interrupt (default off), ignoring later snapshots in files known to need it (default off) |
| RZ-F12 | Play, pause, resume, stop, fast-forward (host speed and turbo do not affect correctness), status (block, frame, total, progress, desyncs, drift against the raster) |
| RZ-F13 | While playing, loader shortcuts that change the CPU path are off (fast tape, turbo tape, fast disk, disk autostart, command typer); live input is refused; debugger breakpoints, stepping and analyzers work |
| RZ-F14 | At the end of the recording, playback stops and the machine continues live from the reached state |
| RZ-F15 | A model mismatch between the snapshot and the running machine switches the machine model first (or refuses, by option) |

**Recording (phase 2)**

| ID | Requirement |
|---|---|
| RZ-F16 | Record `IN` values and fetch counts per frame; a frame is closed at every frame-interrupt point, whether or not the interrupt is accepted, and at each retriggered accepted interrupt |
| RZ-F17 | Write RZX 0.12 (unsigned) with a creator block, the start snapshot (SZX when available, else Z80) and compressed input blocks with repeat frames |
| RZ-F18 | Insert snapshot / roll back to a snapshot during recording (the Fuse / Spectaculator practice) |

**TTD interop (phase 3)**

| ID | Requirement |
|---|---|
| RZ-F19 | A TTD recording can run while an RZX plays; the TTD port journal then stores the RZX-fed values; the playback position is part of every TTD checkpoint, so TTD seeks and reverse steps work inside an RZX playback |
| RZ-F20 | RZX import: play an RZX while recording TTD, producing a self-contained TTD session marked with the RZX's identity |
| RZ-F21 | TTD → RZX export from a recorded range (the `IN` values come from the TTD port journal; fetch counts from a replay) |

## 4. Non-functional requirements

| ID | Requirement |
|---|---|
| RZ-N1 | **Zero cost when no RZX is playing or recording:** no new work on the per-instruction and per-memory-access paths; at most the same kind of single predictable pointer test per `IN` that the TTD port journal already has. Verified by interleaved A/B runs of `BM_Frame_PureCPU`, `BM_FrameCostNormal`, `BM_HostFrame_*_Fast` and `BM_ContentionInstructionMix` (with the `action.sna` fixture present): the difference must be within run-to-run noise |
| RZ-N2 | **Minimal overhead while playing:** a few integer operations per instruction and one array read per `IN`; target ≤ 5% emulation-time overhead on `BM_Frame_PureCPU` with a playback active |
| RZ-N3 | **Minimal intrusion:** no change to `m1_cycle`, to the memory-interface tables, to frame length handling, to sound or to video; the emulated (video) frame stays fixed-length |
| RZ-N4 | **Autonomy:** the RZX format and player do not depend on the TTD manager; TTD interop is an optional adapter |
| RZ-N5 | Cross-platform, zero warnings, no new system dependency (a vendored compression library shared with SZX) |
| RZ-N6 | Thread safety: the player state is touched only on the emulation thread; control commands are queued to it |
| RZ-N7 | Robust parsing: fuzz-tested like the Z80 snapshot loader |

## 5. Automation and UI

| ID | Requirement |
|---|---|
| RZ-U1 | Open `.rzx` from every place that opens snapshots (file dialogs, drag and drop, command line, WebAPI upload, CLI, MCP `load_software`, Lua, Python, GDB `monitor load`); `.rzx` in every extension table |
| RZ-U2 | Dedicated verbs on every surface: play, stop, status, options (desync mode, conventions); record verbs in phase 2 |
| RZ-U3 | Notifications: playback started, snapshot applied, desync, finished |
| RZ-U4 | The Qt status bar shows playback progress; a desync is shown with the frame number |

## 6. Tests and acceptance

| ID | Requirement |
|---|---|
| RZ-T1 | Unit tests of the parser (every block, compressed and not, repeat frames, oversize and truncated blocks), fuzz tests |
| RZ-T2 | Fetch-count tests per instruction class (plain, CB, ED, DD / FD, DDCB, block repeats, `HALT`, `LD R,A`, interrupt acceptance) |
| RZ-T3 | End-to-end playback of real RZX files on 48K, 128K, +3, Pentagon without desync; an independent reference (SkoolKit `rzxplay.py`) used to check the same files |
| RZ-T4 | Zero-cost benchmark gate (RZ-N1) and overhead measurement (RZ-N2) |
| RZ-T5 | Phase 2: round trip record → play without desync; files we write play in Fuse |

**Acceptance for v1:** a set of RZX Archive files for the supported machines
plays to the end without desync in strict mode; RZ-N1 holds; every surface can
play, stop and query status.
