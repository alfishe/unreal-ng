# Reverse-search acceleration and per-configuration TTD

Status: complete. Per-configuration defects fixed; coverage index implemented,
wired into reverse search and reverse breakpoints, persisted into `.ttd`,
benchmarked and differentially tested; write journal block-compressed.
2026-08-21.

This note records what was measured, what was changed, and what is deliberately
left for later — including the parts that cannot be finished until the missing
machine models land.

---

## 1. The question

Can reverse debugging — reverse breakpoints, memory watchpoints, "who wrote this
byte" — be made fast enough to be interactive over a long session, and does the
answer survive clone models with up to 4 MB of RAM?

The starting assumption in the TDD (§9.3) was that a linear backward scan over
the packed write journal is fast enough that no index is needed. That is true of
the journal, and false of reverse search as a whole: Read and Execute accesses
are not journaled at all and fall back to replay.

## 2. What the numbers say

### 2.1 Retention, not file size, is the binding constraint

Measured write rate on a real demo: 2280 writes/frame × 50 Hz × 12 B =
**1.37 MB/s**. The 64 MB journal ring therefore holds **47 seconds**. For a
debugging session that is the real problem — long before disk space is.

Journal compression, measured on journals extracted from real `.ttd` files:

| Layout | Ratio |
|---|---|
| zstd-1 over raw records | 3.4× |
| Columnar + zstd-1, blocks in time order | 13.5× |
| **Columnar + zstd-1, partitioned by address bucket** | **24.4×** |

Address partitioning wins twice over: within a bucket the address column is
nearly constant, *and* the dominant query ("last write to X before T") becomes
local — 1.3 blocks touched instead of a scan. Block size 2048 records is the
sweet spot: 21.6×, average compressed block 0.8 KB, 6 MB of open blocks in RAM.

Streaming to disk makes retention effectively unbounded: ~220 MB per hour at
21.6×, and a query reads one sub-kilobyte block rather than a 64 MB window.

### 2.2 Coverage indexes beat scanning by orders of magnitude

Per-frame sets, measured over 1200 frames of a real demo:

| Set | Distinct/frame | % of 64K | Sparse + zstd-1 | Index cost |
|---|---|---|---|---|
| Executed PCs | 318 | 0.5% | 124 B | 18 MB/hour |
| Written addresses | 75 | 0.1% | 47 B | 12 MB/hour |
| Read addresses | 749 | 1.1% | 96 B | 23 MB/hour |

All three together: **~307 B/frame, 54 MB/hour** — less than the compressed
journal.

Selectivity, expressed as frames scanned backwards before the first candidate:

| Query | Density | Frames scanned |
|---|---|---|
| PC breakpoint | 7.3% | ~111 |
| Write watchpoint | 1.2% | ~183 |
| Read watchpoint | 5.2% | ~215 |

A frame replay cost 1.3 ms (with instrumentation, so an upper bound). The
typical unassisted backward search is therefore ~145 ms; the same query against
the index decompresses ~12 KB. The worst case — an address touched once an hour
— is bounded by index size (tens of ms), not by history length. That is the
whole point: **query cost stops scaling with how long you have been recording.**

Also measured: SP spans only 187 bytes per frame on average (max 2763), so
`SP min/max` is a 4-byte-per-frame zone map that prunes almost every
stack-related predicate.

### 2.3 Sparse, not bitmaps — and this is what makes 4 MB free

| Set | Cardinality | Flat 64K bitmap | Sparse 16-bit | Flat 4 MB bitmap | Sparse 22-bit |
|---|---|---|---|---|---|
| Executed | 318 | 107 B | 123 B | 129 B | **124 B** |
| Written | 75 | 68 B | 46 B | 90 B | **47 B** |
| Read | 749 | 132 B | 96 B | 155 B | **96 B** |

Sparse encoding (sorted delta varint; roaring-style containers) costs the same
whether keys are 16 or 22 bits wide, because cost follows set *cardinality* and
cardinality does not depend on installed RAM — a frame touches ~75 addresses on a
128K machine and on a 4 MB machine alike.

The flat bitmap degrades where it is least visible: its per-frame **scratch
buffer** grows from 8 KB to **512 KB**, and it must be cleared every frame —
more expensive on its own than the entire 117 µs/frame capture.

A pyramid of coarser summaries is counterproductive here: one-second summaries
are *less* selective (26%) than per-frame ones (5.6%), because a demo touches
7.7% of the address space per second. At ~100 B/frame, keep the per-frame level
whole.

### 2.4 What an index cannot do

Reverse breakpoints **on execution** are not served by any of this. The journal
records writes and port OUTs; stopping "when PC == X, going backwards" is replay
from a checkpoint. Coverage bitmaps of executed PCs turn that into "replay only
the frames where X actually executed", which is the large win, but the final
step is still a replay.

Checkpoint density is *not* a lever worth pulling: a checkpoint is captured every
frame (`kKeyFrameInterval = 50` only controls I- vs P-frames) and any checkpoint
is restorable directly through its page refs, so a single reverse step already
costs "restore + replay less than one frame".

## 3. How this maps onto conditional breakpoints

Conditional breakpoints are fully specified
(`docs/inprogress/2026-08-17-conditional-breakpoints/`) but only Phase 0 — the
hot-path prefilter — has landed. There is no condition parser or evaluator yet,
which means the condition language can still be shaped to be *prunable*.

The designed operand set splits cleanly:

* **Answerable from journal + indexes**: `MWA`/`MRA` (address), `MWV`/`MRV`
  (value), `PC`, `physPage`, access kind, `T` / `FRAME`.
* **Replay-only**: registers, flags, memory dereference, `PG0-3` / `DOS` /
  `SHADOW`, `IFF1/2`, `HALTED`, `DT`.

Usefully, `FastPredicate::Kind` from the hot-path spec — `CtxCmpImm`,
`CtxMaskCmpImm`, `Mem8CmpImm`, `Reg16CmpImm` — is exactly the set that maps onto
zone maps. The predicates that are fast forwards are the ones prunable
backwards.

Proposed three-tier reverse search:

1. **Coverage sets** per frame (executed / written / read) — prunes address-keyed
   predicates.
2. **Zone maps** per frame — written-value set, port set, `SP` min/max, register
   -touched mask, and a flag for "wrote into a page it also executed from"
   (a direct self-modifying-code detector).
3. **Replay** the survivors, for everything summaries cannot answer.

Every summary must be **conservative**: allowed to say "maybe", never to say
"no" incorrectly.

Note that regular breakpoints do not fire during replay at all today — every
`BreakpointManager::Handle*` returns early on `ttdReplayActive` — and
`ReverseContinue` takes a bare `std::vector<uint16_t>` of PCs. Wiring conditions
into reverse execution is its own piece of work.

## 4. What was changed

### 4.1 Per-configuration RAM pages (fixed)

`ResolveModelRamPages()` derived the captured page range as `ramsize / 16`, which
reads the value as a page count. It is a page-index **bound**. For the 48K
machine — three pages numbered 0, 2 and 5 — a bound of 3 walked pages 0..2 and
never captured page 5, the display. Every existing test passed because they all
ran on models with contiguous page numbers.

Fixed, with regression coverage in `TTD_FullRestore_Spectrum48_Test`
(`PageBoundReachesTheScreenPage`, `ScreenPageSurvivesCaptureAndRestore` — both
verified to fail without the fix). `UpdateRamPages` now also warns once per
session if any dirty page falls at or beyond the bound, so a future model with a
wider page set announces itself instead of recording partial memory.

See the TDD, "6.2a Per-configuration RAM pages".

### 4.2 `model_ram_pages` widened to u16 (fixed)

A 4 MB machine resolves to exactly 256 pages, which truncated to **0** in the u8
header field, leaving the file describing an empty checkpoint RAM table. Widened
in `ttd_dump_format.h`, `ttd.ksy` and the Python parser.

The format has not shipped, so it was amended in place rather than versioned:
schema stays **v1**. Sessions recorded before this change do not parse and must
be re-recorded. This also resolved a standing inconsistency where `ttd.ksy`
declared `schema-version: 2` while the C++ writer emitted 1.

### 4.3 Bank-aware search (implemented)

`TTDSearchQuery` gained `hasPhysPageFilter` / `physPage`, applied on both the
journal predicate and the replay probe, and exposed as `phys_page` (WebAPI,
Python, Lua) and `--phys-page` (CLI). Read and Execute probe hits now resolve the
mapped bank instead of reporting page 0, which is what makes
`TTDM1Record::physPage` meaningful. Covered by
`TTD_FindLast_Test.FindWrite_PhysPageFilter_*`.

### 4.4 Null ROM bank crash (fixed, outside TTD)

Executing at `0x3D00` on a 48K machine set `CF_TRDOS` and paged in a TR-DOS ROM
the model does not have (`rom.cpp` leaves `base_dos_rom` null), installing a null
bank-0 read pointer; the next instruction fetch segfaulted the emulator. This
crashed `TTD_Divergence_Corpus_Test.AccuracyCoinZX_SelfModifying_FramesMatch`
and took the whole test process down with it.

Fixed in two layers: `Z80Step` gates the TR-DOS trigger on `Memory::HasDosRom()`,
and `SetROMDOS` / `SetROMSystem` refuse to install a null pointer. On a real 48K,
`0x3D00` is ordinary ROM, not a paging trigger.

### 4.5 Test-only instruction trace hook

`Z80::m1TraceHook` fires once per executed instruction with the opcode's PC. Kept
separate from `busTraceHook` so it does not perturb the event counts the
bus-phase timing tests assert on. Null in production; one branch per instruction
when unset. It is what made the executed-PC measurements above possible.

## 4.6 Coverage index — implemented (collection layer)

`TTDCoverageIndex` records, per frame, the distinct physical `(page, offset)`
keys the frame executed and wrote. Executed coverage is collected at the Z80 M1
cycle, written coverage inside `RecordMemoryWrite` (independent of the write
journal flag). `OnFrameBoundary` seals the frame; `InvalidateSession` clears it.
Read coverage is wired but not yet collected — the read hot path is untouched
so far.

### Storage footprint (compressed)

Coverage is stored as zstd-compressed blocks of 64 frames, with the per-frame
key counts inside the block payload rather than in a side table.
`BM_TTD_CoverageIndex_Footprint`:

| Workload | Raw | Compressed | Ratio | B/frame | MB/hour |
|---|---|---|---|---|---|
| ROM startup | 336 KB | 5.2 KB | **65×** | 17.2 | **3.0** |
| Real program (Dizzy Y) | 129 KB | 8.0 KB | **16×** | 26.5 | **4.6** |

The two workloads bracket the range rather than disagreeing: ROM startup clears
memory, touching thousands of addresses per frame but the *same* ones every
frame, so volume is high and compression enormous. A real program touches fewer
addresses, less repetitively.

Two structural findings came out of measuring rather than estimating:

* **The side table cost more than the data it described.** A per-frame entry of
  `{frame, block, offset, count}` is 24 bytes, ×3 kinds = 72 B/frame — against
  17 B/frame of compressed keys. Moving the counts inside the compressed block
  removed the table entirely; blocks are now located by the frame range they
  cover.
* **The floor is the zstd frame header**, about 17 bytes per block, so a
  completely idle session still costs ~0.27 B/frame. 1000 idle frames measured
  at 272 bytes. Not zero, but not worth a special case.

Fixed heap, independent of session length: **~2.4 MB** (three 512 KB membership
bitmaps, the repeat filters, and one decompressed block cache).

### Rebuild cost if not persisted

`BM_TTD_CoverageIndex_RebuildPerFrame`: **1.05 ms/frame**. A session loaded from
a file with no coverage section can only obtain one by replaying itself, so:

| Session length | Rebuild wait |
|---|---|
| 10 minutes | ~32 s |
| 1 hour | ~3 min |

That, not the 3–5 MB/hour of volume, is the argument for persisting it. See
`docs/emulator/design/debugger/time-travel-debug/ttd-container-format.md` for
the container design and the in-file-versus-sidecar trade.

### What it costs

Measured per frame, Gaming profile, by disabling each collector in turn:

| Collected | Frame time | Increment |
|---|---|---|
| Nothing (coverage off) | 1127 µs | — |
| Writes only | 1155 µs | +28 µs |
| + execution | 1286 µs | +131 µs |
| + reads (all three) | 1368 µs | +82 µs |
| + block compression | **1394 µs** | +26 µs |

Total **+267 µs/frame**, compression included — it runs once per 64 frames, so
the 26 µs is already amortised per frame. Execution dominates: it is the only collector that runs
per instruction rather than per memory access.

Against the emulator's own frame time that is a 24% increase, which sounds
alarming; against the 20 ms real-time budget of a 50 Hz frame it is **1.2%**,
comfortably inside the <5% goal in the overhead budget. Both framings are worth
keeping in view — the first is what a headless replay farm pays, the second is
what an interactive user feels.

The structure carries a 16 KB direct-mapped repeat filter in front of an exact
512 KB membership bitmap, so a re-touched address is dropped by one L1-resident
comparison instead of a random access into half a megabyte. Collisions fall
through to the exact bitmap: the filter can cost extra work, never a wrong
answer.

Two false-negative bugs came out of that filter and are now regression-tested:
key 0 (page 0, offset 0) collided with the "empty slot" marker and was swallowed
entirely, and the filter initially leaked across frames so a key touched in
consecutive frames only appeared in the first.

### What it buys

`BM_TTD_FindLastAccess_Coverage`, worst case — an address the workload never
touches, so the search must examine the whole history:

| History | Replay scan | With index | Speed-up |
|---|---|---|---|
| 10 frames | 10.1 ms | 1.02 ms | 9.8× |
| 50 frames | 46.3 ms | 2.42 ms | 19× |
| 200 frames | 197 ms | 2.50 ms | **79×** |

The multiplier matters less than the shape: replay cost grows linearly with
session length while the indexed query barely moves (1.0 → 2.4 → 2.5 ms). Query
cost has stopped scaling with how long the user has been recording, which was
the entire point.

### Correctness

Pruning is guarded by a differential test suite
(`TTD_CoverageIntegration_Test`): every query runs twice, with and without the
index, and the answers must be byte-identical. Any pruning defect surfaces as a
mismatch rather than as a plausible-looking wrong answer. It found two real bugs
during development:

* **ROM accesses were dropped, not indexed.** Code executing from ROM has no
  physical RAM page, and skipping those accesses made frames look empty when
  they held ROM hits — so pruning deleted them. They are now recorded under a
  single "no page" bucket, which over-approximates (extra replays) rather than
  under-approximates (lost answers).
* **A loaded session inherited the previous recording's index.** Coverage is not
  part of the file format, so `DeserializeSession` now clears it; otherwise
  reverse search over a loaded `.ttd` would prune its frames using another
  session's data.

## 5. Not done, and why

* **Index-guided reverse breakpoints stop helping with large breakpoint sets** —
  see section 8.
* **Wide and wrapping address ranges are never pruned.** A query spanning more
  than one 16 KB page, or wrapping a page boundary, falls back to the full scan
  rather than being approximated.
* **Write and Io searches** still go through the journal and ignore the index —
  the journal already answers them quickly.

## 7. Write journal compression (implemented)

The journal turned out to be the real size problem, not the page store. Before
compression it was **89% of the heaviest recording** (8.36 MB of 9.37 MB),
against 648 KB for the entire page store: 2280 writes per frame at 12 bytes
each is 1.37 MB/s, or 4.9 GB/hour, and it was the one section stored verbatim.

It is now written as zstd-compressed blocks of 2048 records, each block
transposed into columns first. The transposition matters more than the
compression: the columns are individually near-constant (addresses cluster,
values repeat, physPage rarely changes, globalT is monotonic and becomes small
deltas) while an interleaved record stream mixes five unrelated distributions.

| Recording | Journal before | after | ratio | File before → after |
|---|---|---|---|---|
| Dizzy Y | 0.45 MB | 0.03 MB | **14.7x** | 0.94 → 0.52 MB |
| 7threality | 0.69 MB | 0.08 MB | 8.6x | 1.67 → 1.06 MB |
| idle | 2.01 MB | 0.25 MB | 8.0x | 2.40 → 0.73 MB |
| across-the-edge | 8.36 MB | 2.48 MB | 3.4x | 9.37 → **3.49 MB** |

Random access into a block is deliberately not supported. That is the division
of labour the index exists for: reverse search asks the coverage index which
frames could match and never scans the journal, so the journal only has to be
readable start-to-finish when the ring is reloaded. The block directory still
carries each block's globalT range, so a time-bounded reload can skip blocks
without decompressing them.

Ratios vary far more by workload than the page store's do (3.4x–14.7x), because
they depend on how clustered the written addresses are.

## 8. Reverse breakpoints on the index (implemented)

`ReverseContinue` used to enumerate every M1 cycle from session start —
replaying the entire recording, measured at 68–168 ms and growing without bound.
It now walks candidate frames from the index, newest first, and enumerates one
frame at a time: **168 ms → 1.15 ms** for a single breakpoint.

Two defects surfaced while wiring it, both of the false-negative kind:

* **Coverage was sealed one frame late.** `OnFrameBoundary` runs after the frame
  counter has advanced, so every set was labelled N+1 while the execution it
  described belonged to frame N. Reverse search then looked in the wrong frame
  and reported no match for a PC that had plainly executed.
* **The unindexed opening frames were skipped.** Coverage is sealed at frame
  boundaries, so the session's first frame has no entry; bounding the scan at
  the index's first covered frame silently discarded hits that occurred there.
  Frames outside the covered range are *unknown*, not empty, and are now
  enumerated.

Worth recording how the first was caught, because it says something about test
design: the purpose-built differential tests **did not** catch it. A PC taken
from a loop executes in neighbouring frames too, so a one-frame shift still
produces a candidate and the search returns the right answer by luck. What
catches it deterministically is asserting the invariant directly — the first
covered frame must equal the session's first frame
(`CoveredRangeAlignsWithTheTimeline`).
* **Zone maps** (value set, SP min/max, port set) and the **blocked columnar
  journal** remain unstarted.
* **Read watchpoints** remain replay-only. A per-frame read coverage set makes
  them practical for the first time; that is a capability gain, not just a
  speed-up.
* **Conditions in reverse execution.** Blocked on the conditional-breakpoint
  evaluator, which does not exist yet.

### Blocked on machine-model work

These are not TTD defects but they cap what TTD can be tested against, and the
per-configuration table above must be revisited when they are addressed:

* **Port decoders exist for 6 of 17 models.** `GetPortDecoderForModel` throws in
  its `default:` branch, so `MM_TSL` and `MM_ATM3` — the 4 MB machines whose 256
  pages motivated the u16 widening — cannot be instantiated at all. The widening
  is therefore correct but currently unexercised on a live 4 MB machine.
* **Extended paging is stubbed.** `Scorpion256::Port_1FFD`, `Profi::Port_DFFD`
  and `Spectrum3::Port_1FFD` have empty bodies; only Pentagon 512 implements
  extended paging. Profi 1024K cannot reach pages above 7, and Scorpion is
  effectively 128K-paged, so neither exercises a wide page set.
* **Bank bounds are checked against the global ceiling.** `SetRAMPageToBank*`
  validates against `MAX_RAM_PAGES` rather than the instance's page count, so a
  128K model can legally be pointed at page 200.

When those land, `ResolveModelRamPages()` should consume a page set published by
the configuration layer instead of the switch it uses today.

## 6. Reproducing the measurements

Two `DISABLED_` probes were used and are kept for re-measurement:

* `TTD_CoverageIndexProbe.DISABLED_DumpPerFrameReadWriteSets` — dumps per-frame
  read/write/executed sets and SP range for a snapshot. Driven by
  `PROBE_SNAPSHOT`, `PROBE_OUT`, `PROBE_FRAMES`.
* `TTD_ModelPagesProbe.DISABLED_ScreenPageIsCapturedFor48K` — prints the
  resolved page bound and checkpoint ref count for a 48K machine.

Run with `--gtest_also_run_disabled_tests`. Journal compression figures were
produced from journals extracted directly out of `.ttd` files.

## 9. Persisting the index (implemented)

The index is now written into the `.ttd` behind header flag bit 2
(`kFlagsHasCoverageIndex`), after the write journal so a reader that knows only
the earlier layout stops cleanly at the journal's end.

It is stored exactly as it is held in memory — a per-kind list of
`{baseFrame, frameCount, rawSize, compressed payload}` — because the in-memory
block layout was designed for this. Serialization does not mutate the index: the
block still accumulating is compressed into a temporary rather than sealed in
place, which is what lets it run from the `const SerializeSession`.

The covered frame range is **recomputed from the blocks on load** rather than
stored. A stored range could disagree with the blocks it describes, and would
then authorise pruning frames the index does not actually hold — the same
false-negative failure mode as everything else here.

### Cost

| Recording | File | Coverage section | Share |
|---|---|---|---|
| Dizzy Y | 0.53 MB | 8.2 KB | 1.5% |
| 7threality | 1.07 MB | 11.4 KB | 1.0% |
| idle | 0.74 MB | 16.7 KB | 2.2% |
| across-the-edge | 3.48 MB | 13.9 KB | **0.4%** |

About 8 MB per hour of recording, against a session running at ~2.1 GB/hour.
The section itself compresses ~232x (3.09 MB raw to 13.6 KB on the heaviest
fixture).

### What it buys on a loaded session

`BM_TTD_LoadedSession_Query` loads `testdata/ttd/demo_across-the-edge-second.ttd`
from disk and runs the same query with the loaded index enabled and disabled.
The worst case is used deliberately — a target the workload never touches, so
the query must consider the whole history:

| Operation | Without index | With index | Speed-up |
|---|---|---|---|
| `FindLastAccess` (read watchpoint) | 447 ms | **0.41 ms** | **1080x** |
| `ReverseContinue` (reverse breakpoint) | 754 ms | **8.28 ms** | **91x** |

The benchmark reports `indexed_frames` as a counter, so a silently absent
section cannot masquerade as "the index does not help" — a failure mode worth
guarding after the first attempt at these numbers was taken with a stale
emulator binary that wrote no section at all.

### Behaviour when the section is absent

A file without coverage is complete and correct; its reverse queries fall back
to replay, which is what they did before the index existed. A file whose section
fails to load is treated the same way: the index is cleared and a warning
logged, never partially populated. Covered by
`SessionWithoutCoverageSectionStillSearches` and
`PersistedCoverageRangeSurvivesRoundTrip`.
