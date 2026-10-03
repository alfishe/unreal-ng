# TimeTravelEngine: design decisions before code

Decided 2026-10-02. Part of the [TTD v1 → v2 migration](README.md). How the engine is built and named: [engine-approach-and-naming.md](engine-approach-and-naming.md).

The engine must not need another redesign, the way v1 needed one for v2. Three reviews collected what it has to support from the start, even where a feature is used only later:
- the migration documents in this folder;
- the designs of machines and devices;
- the designs of the engine's users (debuggers, UI, automation, RZX).

This page records the resulting decisions and the conflicts they settle. "Built in now" means the data model and interfaces carry it from the first commit, even if nothing uses it yet.

## Glossary

| Term | Meaning |
|---|---|
| Piece | 4 KB of emulated memory, the unit the engine stores |
| Region | A block of emulated memory tracked by pieces: machine RAM (region 0) or memory a device owns |
| Chain | The difference (XOR) pieces decoded, newest to oldest, until a full one, to rebuild a piece |
| Branch | A second history that starts at some position of an existing one, for example after an edit in the past |
| Spill file | The file the engine writes the session into as it records; memory is only a cache of it |
| Machine time | The engine's single time line: main-CPU cycles in top-clock units since the session start |

## A. Structure and parameters

| # | Decision | Why / source |
|---|---|---|
| 1 | A new engine, `ttd::TimeTravelEngine`, next to v1, checked byte for byte against v1 on the same sessions. The roadmap, the Phase 1 TDD and the requirements tracing are rewritten for it | [engine-approach-and-naming.md](engine-approach-and-naming.md) |
| 2 | Reference table blocks of **8 pages**, the size set per region. *As built (Phase 1, Step 3):* each checkpoint records only its changed pieces (8 B each) and every 64th holds a full table of copy-on-write blocks; blocks alone lost to v1 on small busy machines ([Phase 1 TDD §4.4](phase-1-memory-regions-tdd.md#44-step-3--regions-and-the-copy-on-write-reference-table)) | Measured: [E3](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e3-reference-blocks/README.md). target-architecture §3 and migration-trajectory said 16 |
| 3 | Region ids are their own fixed table (`TTDRegionId`, u16); a region's owner is a device id | One device can own several regions (NeoGS: RAM and flash). target-architecture §4.6 had proposed device ids |
| 4 | The chain length limit K is a parameter, default 50 | [E1](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e1-chain-limit/README.md) asks to revisit it after restore-only-differences |
| 5 | Every stored item records what it depends on (the base of a chain, a shared block, an event payload). When the start of history moves forward, pieces whose chains reach past the new start are rebuilt as full pieces first | Without key frames nothing old can otherwise be removed safely (Phase 1 TDD §7; integrity I-4) |
| 6 | The file is cut into self-contained parts by these explicit dependencies, not at "chain-limit boundaries" | Per-piece limits have no common boundary (target-architecture §7 assumed one) |

## B. Behavior users see

No history is ever cut short ([model what-if](../2026-09-29-model-what-if/design.md) WI-1). This settles conflicts between the recipes, the TTD toolbar, DeZog and the what-if design.

| # | Decision | Conflict settled |
|---|---|---|
| 7 | Resuming from a past position starts a **branch**; it never discards the later history. The data model carries branches from the start; branch operations in the UI come later (PLAN #76) | The recipe and the toolbar design "truncate the future" on resume |
| 8 | Seeking while recording is allowed: recording pauses. Resuming at the end continues the recording, resuming in the past starts a branch | The recipe refuses with HTTP 409; DeZog and the toolbar allow it |
| 9 | A debugger edit at the present is journaled as an external event; an edit in the past starts a branch. The engine never wipes history on an edit | Debugger rules refuse edits, DeZog wipes history, the use cases journal them |
| 10 | Loading a snapshot is an event on the timeline, not the end of the session | The recipe wipes the session; WI-6 keeps it |
| 11 | Retention is a policy: a memory budget by default; "last N minutes" is a separate policy for the black box | WI §4.6 "memory, never seconds" against the black-box "last N minutes" |
| 12 | "Start" means the earliest position still kept, not frame 0. Every position is absolute and validated | The toolbar's "Jump to start" seeks frame 0 |
| 13 | A seek to "frame N" lands at the end of frame N: machine state and picture agree. A seek to "frame N, T" gives the state at T and the picture up to the beam | Positioning design §7: state at the frame start, picture at its end |

## C. What the engine provides

| # | Decision | Source |
|---|---|---|
| 14 | Two input modes for replay: recorded input events (default, branches), and recorded `IN` values (RZX). RZX's own key-frame store retires once the engine replaces it | WI-9; RZX RZ-F19 and its design §18 |
| 15 | A position is `{branch, frame, T}` plus, where it matters, the position of each CPU in its own units | WI §4.1; debugger rules §3, protocol `CpuPosition` |
| 16 | Read-only analyzer hooks run during replay; reverse search uses them, including event and card breakpoints | Today `BreakpointManager` returns early during replay (reverse-search index §3) |
| 17 | The write journal is a derived index with a replaceable retention policy: a ring, the whole history, or a window around the current position with the rest regenerated by replay. The default comes from experiment E7, before Phase 4 | [E6](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e6-v1-v2-model/README.md): the largest stream on active content |
| 18 | Phase 2 stores only the device fields that changed, and treats counters that advance with time as derived from time | E6: device state costs 2.7 MB per minute in v1 even at an idle prompt; storing only changed state brings it to 0.8–0.9 MB, mostly timers of cards that play nothing |
| 19 | **Optional streams at frame boundaries**, for example a screenshot to debug quickly without restoring the frame. They can be switched on and off at run time, with controls on every surface. A stream that is off costs nothing on capture or restore: one check per frame, never per event | Requirement of 2026-10-02 |

## D. Documents

Fixed as part of this change, without further discussion:
- the stale statement that bug B4 "stays planned in Phase 3" (fixed 2026-09-28);
- three different seek p99 figures: the measured one stays;
- requirements no phase covered get a phase: FR-14 (open in another configuration), QR-3 (deterministic output), PR-1 / PR-2 (frame overhead), PR-6 / PR-7 / PR-11, FR-8 / FR-9, a partially damaged session on every surface.

## E. Machines and devices

| # | Decision | Source |
|---|---|---|
| 20 | **Machine time**: main-CPU cycles in top-clock units since the session start, 64-bit. Every other CPU keeps its own cycle counter, and a clock change is an event on the timeline, used to convert between units. A position can name any CPU | GS (12 MHz), NeoGS (10–24 MHz, switched while running), the ATM2 keyboard controller (MCS-51, 11.06 MHz), VDAC2's FT812 (48–80 MHz). The GS debugger needs reverse steps in card instructions ([requirements](../2026-09-27-gs-debugger/requirements.md) D1, D4) |
| 21 | Frames have no fixed length: a frame table holds each frame's start in machine time | Sprinter 320 or 312 lines and ×6 turbo from a frame boundary; RZX frames are not video frames ([RZX design](../2026-09-29-rzx-replay/design.md) §6) |
| 22 | The piece store is not owned by a session and can be shared by several. A **group** container sits above sessions: unused at first, later a group seek and one group file | ZX-Poly: four machines in lockstep ([quad-instance architecture](../2026-09-27-zxpoly/quad-instance-architecture.md) §4.4, §8); v1 would cost 4× the memory |
| 23 | Devices are identified by a stable type id (u16) plus an instance name (`isa2.uart0`), each with a layout version, explicit restore-order dependencies, a firmware fingerprint and an after-restore call | The one-byte `PeripheralId` is 0–41 full; designs already collide (VDAC2 plans 26, the Sprinter network 39–43, both taken); several instances of one device (UARTs, IDE channels) |
| 24 | One event stream with a kind per event; each kind has its point of application (inside the access that reads it, at an instruction boundary, or at a frame boundary) and may carry a payload. A payload referred to by any checkpoint is kept. The journal covers any bus data, not only `IN` | Network, modem, automation injections; Sprinter IM2 vectors come from the device, so its replay is not sealed today |
| 25 | Each checkpoint refers to a version of every inserted medium. Real-time clocks keep their emulated time base in the session. NVRAM and EEPROM contents are part of the initial state | While recording, every DS12887 user already runs on emulated time; outside a recording the clocks read the host's time, and the session does not keep the time base |
| 26 | A change of configuration or device set is an event on the timeline, and a seek restores the device set of its target (FR-4). A model switch starts a new session linked to its parent | FR-4; what-if P-2 |
| 27 | Region size has no fixed cap; FR-5's "1 MB device RAM per device" is corrected | NeoGS needs about 4.5 MB |

## F. Storage and use

| # | Decision | Detail |
|---|---|---|
| 28 | **The session is written to a file as it records, transparently.** Memory is only a cache: old data is evicted and read back from the file on a seek. "Save" renames the finished file; an unwanted recording is deleted | Capture never writes to disk itself: it hands already-compressed items to a background writer thread, which appends in large blocks, with no forced flush on the hot path. Evicted data is read back through a memory-mapped view. The format is append-only and survives a crash of the emulator from the first version. Write volume is the engine's file rate from E6: about 3 MB per minute idle, up to about 37 MB per minute on heavy demos (about 2 GB per hour). This moves part of Phase 5 into the core |
| 29 | **Always-on recording (black box)** is a global persisted setting in unreal-qt. For automation (WebAPI, CLI, MCP, tests) it is off by default | |
| 30 | The session file goes where the UI says at run time. Default: `scratch/ttd/<date-time>-<name>.ttd` | |
| 31 | v1 files are read only to recheck and compare without re-recording: the engine reads a v1 file and writes it in its own format, or into memory buffers when no file is wanted. v1 data has buffers of its own | |
| 32 | Users get only the engine. v1 stays only for verification (tests, benchmark, comparison) and cannot be chosen in the application | |

## G. Quality bar after every phase

| # | Decision |
|---|---|
| 33 | After every phase the engine is compared with v1 on the whole benchmark matrix, and it must hold the bar below |

| What | Condition | How it is checked |
|---|---|---|
| Correctness (mandatory) | every frame and every point inside a frame restores byte for byte as v1 restores it | the oracle on recorded sessions |
| File size | **in every case** not larger than v1 for the same history kept; no case may get worse, averages do not count | deterministic, CI gate |
| Memory | not larger than v1 for the same history kept (v1 with exact-size allocation) | benchmark, with the allocator tolerance |
| Capture work | counted work per frame (`bm2_work_*`: pages walked, bytes copied, zstd calls) not larger than v1 | deterministic, CI gate, no clock |
| Seek time | may be slower than v1, but p99 ≤ 5 ms on every configuration (PR-5), and from Phase 1, Step 5 (restore only the pieces that differ) on no slower than E4's estimates by more than 25% (E4's own accuracy) | benchmark on an idle host (load < 12), run twice |
| Capture time | p99 ≤ 3× the median (PR-3) | benchmark |

**Same history kept.** v1's write-journal ring drops old writes after 1–3 minutes of active content, while the engine keeps them ([E6](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e6-v1-v2-model/README.md): Eye Ache after 5 minutes, v1 81 MB against the engine's 177 MB, because the engine holds five times the journal). Sizes are therefore compared per stream, per frame of history actually kept, or on recordings where v1's ring did not wrap.

**Fixed ratios by design are not regressions.** Where the ratio between v1 and the engine is fixed by how the two are built, not by a phase's work, the rule does not apply to it. Each such case is listed here with the stream, the ratio and the reason, and every other difference falls under the rule. Known so far:
- the engine's fixed header (region table, session metadata) on very short recordings: compared in bytes per minute of recording, the header allowed once;
- memory v1 does not record at all (MoonSound wave RAM, NeoGS RAM and flash, the EEPROMs): the engine stores more history there, so the stream is compared only where v1 has it (measured: MoonSound upload, memory pieces +8% with the wave RAM included);
- device memory moves between streams: v1 keeps the General Sound RAM inside the device blob, the engine as region pieces, so the two streams are compared as their sum (GS 512 upload: 2,304 → 1,624 bytes per frame); the memory-restore time likewise includes card RAM for the engine and not for v1, so seek time is compared on whole seeks;
- before Phase 1, Step 5 (restore only the pieces that differ), seeks decode chains of up to K links where v1 decodes up to 49 from a key frame: only the PR-5 limit applies to seek time until then.

Sizes and counted work run in the CI gate; times in a manual run of the matrix, as the benchmark rules already require.

## H. Classes of recorded data

Decided 2026-10-02. Every item the engine records belongs to exactly one class, and the class decides how the engine treats it. The full list of items, with their class, is the [state registry](state-registry.md).

| # | Decision |
|---|---|
| 34 | Recorded data has three classes, declared per item by the device that owns it: **required**, **derived** and **telemetry** |
| 35 | Telemetry is an optional frame-boundary stream (decision 19), one stream per kind, switched on and off at run time. It is never part of the state a restore needs |
| 36 | The [state registry](state-registry.md) lists every item the engine records or deliberately leaves out, with its class, its stream and its size. A device change that adds or drops state updates the registry in the same commit |

| Class | What it is | Examples | How the engine treats it |
|---|---|---|---|
| **Required** | State without which replay from a checkpoint diverges: memory, registers, latches, internal counters of chips (timers, dividers, FIFO positions), the emulated time base of real-time clocks, media versions | RAM pages, AY registers and envelope counters, WD1793 command phase, FT812 memory and its control state | Stored in every checkpoint that changed it; included in integrity checks and in the determinism comparison (QR-3); a missing item is a restore error, never a silent default |
| **Derived** | Caches a device rebuilds from required state | Sprinter palette RGBA cache and INT list; FT812 state rebuilt by `EveMemoryRestored`; decoded page tables; renderer lookup tables | Not stored. After a restore the engine calls the device's after-restore hook (`onRestored`, decision 23), which rebuilds it |
| **Telemetry** | Values that emulation never reads back but that a user or a debugger wants to see at a past position | VDAC2 line-budget metrics of the last frame, drive and IDE activity LEDs, per-frame statistics (port accesses, contended cycles), audio peak levels | An optional stream (decision 19): off by default, one mask check per frame when off; outside integrity checks and determinism comparison; dropped first when the memory budget is reached; a position without it shows "no data", and a file without it loads without error |

**How a device decides.** If the emulation of any later instruction can read a value, directly or through a computation, it is *required*. If it can be recomputed from required state at any moment, it is *derived*. Only what neither applies to is *telemetry*. When in doubt, required: a wrong "telemetry" label breaks replay, a wrong "required" label only costs bytes.

**Host-facing state is none of the three.** The audio ring, the host framebuffer, open host files of disk images, network sockets and host timers are not recorded. A restore reconnects them to the restored state (the picture is composed by replay, `ttddisplayparticipant.h`; media are versions per decision 25; network input comes from the event journal, decision 24).

**Existing items to reclassify.** The VDAC2 card blob (`Vdac2`, id 43) carries the line-budget metrics of the last finished frame next to the chip's control state. If emulation does not read them back they move to a telemetry stream when the device blobs move to the engine (Phase 2). The registry marks every such case.

## Devices that cannot be recorded yet

Not decisions for the core, but work for the steps that add them: both EEPROMs (Phase 1 device regions; NeoGS RAM and flash, MoonSound wave memory, Sprinter video and fast RAM and the VDAC2 chip memory are engine regions since Step 6); ZX-Poly (refuses per instance; decision 22); the WD1793 context outside the Sprinter; ids 33 and 34 reserved without a serializer.
