# TTD v2 target architecture

> **Note (2026-10-02).** This is the target as first designed, for migrating v1
> in place. v2 is now built as a new engine, `ttd::TimeTravelEngine`, next to v1
> ([engine-approach-and-naming.md](engine-approach-and-naming.md)). Where this
> document differs from [engine-decisions.md](engine-decisions.md) (D1–D33), the
> decisions win; the main differences are marked inline.

The architecture TTD should end up with, derived from the current code
([current-state.md](current-state.md)), the design corpus (§8 lists it), the
three feature branches ([branch-merge-strategy.md](branch-merge-strategy.md))
and the measurements. It keeps what works, fixes what does not scale, and
explicitly drops the parts of earlier designs that the measurements no longer
support.

---

## 1. Principles

1. **Restore is exact or it says it is not.** Every restore either reproduces
   the recorded machine byte for byte, or returns a result naming what could
   not be restored. No silent zero-fill, no silently kept live device state.
2. **Cost follows change, not installed memory.** Recording cost per frame is
   proportional to what the frame changed. Nothing is O(installed RAM) per
   frame or per key frame. This is what makes 4 MB ZX-Evo + 512 KB GS +
   1 MB MoonSound affordable.
3. **One mechanism per kind of state.** Large memory, whoever owns it, goes
   through one page store. Small state goes through one device registry. No
   device invents its own path.
4. **Memory is bounded; history is not lost.** A budget caps process memory.
   Beyond it, history either spills to disk (lossless) or drops the oldest
   frames (explicit, reported).
5. **The file is versioned from v2 on.** "Amend in place and re-record" ends at
   the v2 cut. After it, the format evolves through versioned streams that old
   readers can skip.

## 2. State model

Everything TTD restores falls into three kinds:

| Kind | Examples | Mechanism | Stored |
|---|---|---|---|
| **Machine core** | Z80 registers, standard port latches, frame counters, in-frame T-state | fixed structs (48 B CPU, 120 B chipset) | every checkpoint (small, compresses to ~nothing in a chunk) |
| **Device state** | AY/TSFM, WD1793, tape position, Covox, mouse, model paging (ATM, Scorpion, Profi), GS/MoonSound registers, RTC/CMOS latches | device registry blobs, each with a layout version | only when changed (see §4) |
| **Memory regions** | machine RAM; GS RAM; GS-lightweight upload store; MoonSound wave SRAM; later NeoGS RAM | page store, 4 KB pieces | only changed pieces (see §3) |

### 2.1 Memory regions (new)

A **memory region** is a block of emulated memory that TTD tracks by 4 KB
pieces. Machine RAM becomes region 0; a device that owns memory registers its
own region.

```
struct TTDRegionDesc {
    uint16_t   id;          // 0 = machine RAM; device regions allocated from the id table
    PeripheralId owner;     // 0xFF for machine RAM
    uint32_t   pieces;      // capacity in 4 KB pieces (GS 512 KB = 128)
    const char* name;       // "ram", "gs.ram", "moonsound.wave-sram", ...
};
```

*Superseded by D3:* region ids are their own fixed table, `TTDRegionId` (u16),
not `PeripheralId` values; a region's owner is a device id, and one device can
own several regions (NeoGS: RAM and flash). Region size has no fixed cap (D27).

- The owner marks pieces dirty on its own write path (GS: its Z80's memory
  write; MoonSound: the wave-SRAM data port). Machine RAM keeps the existing
  debug-write hook, with page indices widened so page 255 is no longer the
  "not RAM" sentinel (done in Phase 0, Step 1, 2026-09-27: `ttd::PhysPage`).
- A region can have a *used size* smaller than its capacity (GS lightweight
  upload store: capacity = largest module allowed, used = uploaded bytes; the
  used size lives in the device blob). This replaces variable-size device
  blobs, which break the fixed-size contract.
- Checkpoints reference region pieces exactly as they reference RAM today.
- The session header lists the regions, so a reader knows every region's size
  before the first checkpoint.

This single mechanism covers GS (currently 512 KB copied into a blob every
frame on the `generalsound` branch), MoonSound Tier B (1 MiB wave SRAM, not
captured at all on the `moonsound` branch; its TDD F2 describes the same thing)
and any future device with RAM.

## 3. Page store and codec

Kept from today: 4 KB pieces, `Full` / `XorPrev` / `Zero` encodings, zstd
level 1, CRC32C of the reconstructed piece, reference counting.

Changed:

| Today | v2 | Why |
|---|---|---|
| Global key frame every 50 frames re-stores every non-zero page in full | **Per-piece chain cap**: a piece is stored `Full` when *its own* XOR chain would exceed K links. K = 50 initially, matching v1's key-frame interval so seek cost stays comparable; the final value comes from the benchmarks (BM-5 vs BM-8). Unchanged pieces are never re-stored. Worst-case chain depth is the same as v1 (every piece at 49); pieces that do not change stay at depth 0 instead of being re-stored every 50 frames | Key frames are O(installed RAM): 5.4 ms per key frame on 4 MB. The only reason for them is to bound chain length, which is a per-piece property |
| `_prevPageCache` refreshed by copying all model RAM every frame | Refresh only the pieces that were dirty this frame; rebuild from the restored checkpoint on resume/seek | 4 MB memcpy per frame on ZX-Evo; the stale-cache resume bug (current-state B1) disappears with an explicit rebuild |
| Dense reference table per checkpoint (16 B × pages) | Reference table split into blocks of 8 pages (128 B each), copy-on-write between consecutive checkpoints; the block size is set per region (D2). First designed as 16 pages; experiment [E3](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e3-reference-blocks/README.md) chose 8 | 4 KB/frame on a 256-page machine (~720 MB/hour) for tables that are almost always identical to the previous frame's. With the measured 1–2 dirty 16 KB pages per frame, 1–2 blocks are copied per frame plus the block pointers; measured numbers in E3 |
| Dirty tracking at 16 KB, splitting into 4 KB at the codec | Unchanged | Measured and argued in `ttddirtytracker.h`; the all-zero XOR fast path already skips unchanged 4 KB pieces |

**Not adopted** (and why):

- **lz4 "hot tier"** — measured 1.38× worse storage with no practical latency
  gain (POC 011 `tiered-storage-analysis.md`); phase 5 lists the conditions
  under which to revisit it.
- **Batching several pieces per zstd call** — incompatible with per-piece
  reference counting; the POC's own documents disagree on whether it makes
  restore faster or 3.8× slower.
- **4 KB dirty tracking** — 4× more write-hook traffic for a gain the codec
  already provides.

## 4. Device state

Kept: the registry, the port-decoder declaration contract
(`GetTTDModelStateIds` / `CreateTTDSerializers`), the 12-byte blob header.

Changed:

1. **Device table in the session header**: `{id, name, layout version, state
   size}` for every registered device. Loading checks it against the live
   machine *before* any restore. A mismatch (device missing, layout older,
   size different) is reported up front, per device.
2. **Layout version per blob** in the header's reserved field; each serializer
   declares its version and rejects others in release builds too (TSFM today
   only `assert`s).
3. **Unchanged blobs are shared, not re-stored.** In memory, a checkpoint keeps
   a shared pointer to the previous blob when the device's state bytes did not
   change; on disk, a one-byte "same as previous" marker. Today device blobs
   cost more than RAM on idle workloads (309–646 B vs 215 B per frame).
4. **The restore report is enforced.** Missing / wrong-size / unclaimed blobs
   make the seek result *degraded*, carried to WebAPI, MCP, CLI, DeZog and the
   Qt widget. Nothing silently keeps live state.
5. **Every device is under the contract.** Sound devices that register
   directly (GS, MoonSound, TurboSound slot) go through the same declare /
   implement check as model state. Runtime device replacement (the GS branch's
   `UpdatePeripheral`) is part of the registry API.
6. **One id table.** `PeripheralId` values are allocated in one place on master
   (see [branch-merge-strategy.md](branch-merge-strategy.md) §2) and never
   reused. Region ids come from the same table. *Superseded:* region ids are
   their own table `TTDRegionId` (D3); the engine identifies devices by a
   stable u16 type id plus an instance name (D23).

## 5. Determinism inputs

Exact replay depends on more than machine state. The session header records a
**configuration fingerprint**: T-states per frame, CPU clock and turbo
configuration, audio core rate, decimator quality, `soundhq` / `screenhq`, ROM
signature, device table. On load:

- restore of a checkpoint is exact regardless (it restores state, not replay);
- if the fingerprint differs from the live machine, operations that *replay*
  (seek inside a frame, reverse step, resume recording) are reported as "not
  bit-exact" instead of silently diverging.

Also persisted (they exist in memory today but are lost on save):

- the **input journal** (keyboard, mouse) — otherwise a loaded session replays
  inside a frame without the recorded input;
- **external-event markers** (tape control, disk writes, resets) — otherwise
  reverse search crosses them silently;
- **RTC/CMOS reads** where the device serves host time (Profi RTC, ATM CMOS
  clock): record the emulated clock base in the session so replay reads the
  same time.

## 6. Storage tiers

| Tier | What | Bound |
|---|---|---|
| **Hot** (memory) | recent checkpoints, their pieces and blobs, the write-journal ring, coverage | configurable budget (default to be measured; today's sessions reach 0.3–2.1 GB/hour) |
| **Cold** (disk, optional) | older chunks appended to the session file by a background writer | disk space |

> *Superseded 2026-10-02 by [D28](engine-decisions.md#f-storage-and-use):* there is no choice between the two modes below. The session is always written to its file as it records; memory is a cache of it under the budget, and "save" is a rename. The two descriptions remain as the history of the design.

- **Memory-only mode** (default for casual use): when the budget is reached, the
  oldest frames are dropped (their pieces released) and the session start moves
  forward. The UI and status report the earliest reachable frame. This is a
  ring, not "thinning". *Per D5:* without key frames, a piece whose chain reaches
  past the new start is first rebuilt as a full piece, and a piece still referred
  to (by a later checkpoint, a shared block, a branch or another session sharing
  the piece store, D22) is never released; every stored item records what it
  depends on. *Per D28:* the session is always written to a file as it records,
  memory is a cache of it, and evicted data is read back on a seek.
- **Disk mode**: sealed chunks (≈50 frames of checkpoints plus the pieces and
  blobs they introduced, journal blocks, coverage blocks; *per D6* a part is cut
  by the explicit dependencies of its items, not at a fixed frame count) are appended to the
  session file by a background thread that reads immutable pieces without
  blocking the emulator. Evicted pieces are fetched back through the cue table
  on seek. "Save" becomes "finalize": write the cue table and footer.
- **Switching modes during a session** (memory-only → disk after history has
  already been released) is an open UX decision: either the mode is fixed at
  session start, or switching keeps only what is still in memory
  ([migration-trajectory.md](migration-trajectory.md) §6).
- **Thinning** (keeping only every Nth frame of old history) is **dropped**:
  disk mode keeps full history losslessly, and thinned regions made the
  "≤ 6 ms seek anywhere" target false anyway.

### 6.1 Memory as linked blocks (direction, 2026-09-29)

Direction from the user, recorded here for Phase 4 / Phase 5; the numbers and the spill
mechanism are still **open** ([migration-trajectory.md](migration-trajectory.md)
§6, item 3):

- **History memory is a chain of linked blocks.** A session starts with **one
  64 MB block** and adds blocks as it grows, **up to the configured limit**.
  Pieces, blobs, checkpoints, journals and coverage are allocated inside the
  blocks.
- **Everything is tied to memory, not to time.** The budget, eviction and the
  status report count blocks and bytes; no setting is expressed in seconds of
  history (bytes per second differ by an order of magnitude between an idle
  48K and a ZX-Evo with GS and MoonSound).
- **At the limit, whole blocks move.** With spill, the oldest block leaves
  memory for disk (disk mode, §6) or is backed by a **memory-mapped file** so the
  OS pages it; without spill, it is released and the session start moves
  forward (the memory-only ring). Evicting by block makes the cost of eviction
  independent of the number of pieces in it.
- **Branches** ([model what-if design](../2026-09-29-model-what-if/design.md)
  §4.6) share the same blocks; pinned branches and the newest block of the
  active history are never released.
- Open: the default limit, when spill starts (at the limit, or earlier in the
  background), explicit disk writes vs memory-mapped blocks (or both: mapped
  blocks as the disk mode's storage), and how a block's contents are arranged
  so a spilled block can be read back in one read.

## 7. File container (`.ttd` v2)

```
Header   magic "TTDD", container_version 2, session UUID, created,
         configuration fingerprint (§5), region table (§2.1), device table (§4)
Chunks   repeated: 32-byte chunk header
           { stream_id u16, flags u16, first_frame u64, frame_count u32,
             raw_size u32, comp_size u32, crc32c u32 }
         + payload (zstd when flags say so)
Footer   cue table (stream, first_frame -> file offset), footer magic
```

| Stream | Content |
|---|---|
| 0 | page-store pieces (all regions) |
| 1 | checkpoints (first designed as cut at chain-cap boundaries; per-piece limits have no common boundary, so *per D6* parts are cut by explicit dependencies and each is self-contained enough to seek into) |
| 2 | write journal blocks |
| 3 | input journal |
| 4 | external events |
| 5–7 | coverage: executed / written / read |
| 8 | bookmarks |
| 9+ | reserved; **readers skip unknown streams** |

- Written incrementally (disk mode) or all at once (save of a memory session):
  same layout either way.
- **Crash recovery**: a missing footer means the file was not finalized; the
  reader scans chunk headers, keeps every chunk whose CRC matches, and rebuilds
  the cue table.
- **Versioning after v2** (direction, **not decided** — see
  [integrity-and-versioning.md](integrity-and-versioning.md)): `container_version`
  changes only for incompatible framing; new content arrives as new streams or
  new device layout versions. Which content old readers may skip, how device
  layout changes are handled and when the compatibility promise starts are open.

## 8. Integrity — what checksums are for

**Status: direction, not a decision.** The mechanism (what a CRC covers, at
which granularity, when it is checked, what a failure does, how it works with
streaming) is an open investigation:
[integrity-and-versioning.md](integrity-and-versioning.md). This section keeps
the rationale for having checksums at all.

What a chunk CRC would buy:

1. **Telling a damaged file from an emulator bug.** When a restore looks wrong,
   the first question is whether the file is intact. Today 57–100% of random
   flips per section pass unnoticed (measured, [current-state.md](current-state.md)
   §9), so that question has no answer.
2. **Crash recovery and streaming.** Disk mode appends while recording; after a
   crash or power loss the reader needs to know where valid data ends. That is
   exactly what per-chunk CRCs provide.
3. **Clear errors instead of silent wrong state.** A flipped CPU register or
   device blob today restores a wrong machine with no error; with a chunk CRC
   the load fails naming the section and frame range.
4. **Shared files.** Sessions attached to bug reports travel through tools
   and filesystems that are not guaranteed to preserve bytes.

What it does **not** buy: detection of determinism bugs, format drift or
emulator bugs that produce well-formed but wrong state. Every TTD defect found
in September 2026 was of that kind; those are caught by `TTD_Corpus_Test`,
the divergence hash and replay comparisons, not by checksums.

Cost: CRC32C runs on the CPU's CRC instructions (SSE4.2 / ARMv8, already used by
the page store) at several GB/s — under 1 ms for today's 3.5 MB fixtures,
a fraction of a second for a 1 GB session.

Open (see the investigation): whether the per-piece CRC32C in memory stays,
what a mismatch does (refuse vs damaged ranges), eager vs lazy checking, and
how damage that propagates through XOR chains and shared blobs is reported.

Before v2, only the Python analyzer fix went ahead (2026-09-25: it compares
the stored piece CRC instead of overwriting it).

## 9. What stays the same

- One checkpoint per frame at the frame boundary; silent deterministic replay
  inside a frame. Checkpoints inside a frame are added only if the turbo
  measurements require them (requirements PR-5, trajectory Phase 0, Step 4).
  They do not: seek p99 ≤ 3.8 ms on every configuration, 3.46 ms on the
  heaviest 10-minute session ([v0b-benchmark-results.md](v0b-benchmark-results.md) §3).
- Capture after every device has caught up to the frame boundary
  (requirements FR-19) — today by call order, in v2 as a tested rule.
- The DeZog frame cache: it replays one frame through the normal restore path
  and collects instruction records, so it does not depend on how pieces,
  references or blobs are stored.
- The coverage index. The write journal (12-byte records, columnar zstd
  blocks) no longer stays as it is: *per D17* it becomes a derived index with a
  replaceable retention policy (a ring, the whole history, or a window
  regenerated by replay), the default chosen by experiment E7 (roadmap Phase 3,
  Step 7).
- Seek, find-last, reverse step / continue algorithms.
- The WebAPI / MCP / CLI / Python / Lua surface. New fields (degraded restore,
  earliest reachable frame, fingerprint mismatch, real memory use) are added; no
  route is removed.

## 10. Sources

- Current code: [current-state.md](current-state.md).
- Parent design: `docs/emulator/design/debugger/time-travel-debug/`
  (`time-travel-debugging-tdd.md` v2.2 §6.5–6.6 storage modes and streaming,
  `ttd-container-format.md` chunked container, `overhead-and-gating.md`
  measurements).
- Codec decisions and measurements:
  [phase-5-codec-poc-results.md](../2026-07-19-time-travel/phase-5-codec-poc-results.md).
- Registry plan (presence set, paged peripherals):
  [2026-09-10-ttd-registry-integration/PLAN.md](../2026-09-10-ttd-registry-integration/PLAN.md).
- MoonSound TTD design (paged region F2, content-aware I-frames F2a, blob I/P
  F3): [opl4-ttd-integration-tdd.md](../2026-09-13-moonsound/2026-09-13-0217-opl4-ttd-integration-tdd.md).
- v2 research PoC: `tools/poc/011-ttd-v2-capture-analysis/` — useful for codec
  and index-overhead numbers; its "v1 vs v2" comparison is against a
  hypothetical full-state-per-frame format, not the shipped engine (see
  [README.md §6](README.md#6-what-v1-and-v2-mean-here)).
