# Phase 1 — Memory that costs only what changes: technical design

Status: **design, not implemented** (2026-10-01). Parameters measured on real recordings: [Phase 1 experiments](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/README.md) (E1–E4). Roadmap and checks: [README.md](README.md#phase-1--memory-that-costs-only-what-changes). Requirements: FR-5, FR-22, PR-3, PR-4, the memory part of PR-9 / PR-10 ([requirements.md](requirements.md)).

Code references are to master at `99467101`. `TTM` = `core/src/debugger/ttd/timetravelmanager.cpp`, `PS` = `core/src/debugger/ttd/ttdcodecpagestore.{h,cpp}`.

## 1. Glossary

| Term | Meaning |
|---|---|
| Piece | 4 KB of emulated memory as the page store keeps it. The code calls it a *slot* |
| Full / XorPrev / Zero | How a piece is stored: compressed copy; compressed difference (XOR) from the previous version of the same piece; "all zero", no payload |
| Chain | The XorPrev pieces that must be decoded, newest to oldest, until a Full or Zero one, to rebuild one piece |
| Chain length limit | The most links a chain may have; a piece that would exceed it is stored Full |
| Region | A block of emulated memory tracked by pieces: machine RAM (region 0) or memory a device owns |
| Delta base | The previous contents of a piece, needed to compute its XOR difference |
| Reference table | For each checkpoint: which stored piece holds each piece of memory at that frame |
| Copy-on-write block | A part of the reference table shared between checkpoints until one of them changes it |
| Device blob | A device's small state (registers, latches), saved whole in every checkpoint |

## 2. What changes, in one example

ZX-Evo (4 MB RAM, 256 pages of 16 KB) at the BASIC prompt, one frame in which the program writes two pages:

| Work per frame | Today | After Phase 1 |
|---|---|---|
| Find what changed | dirty bitmap, 2 pages | same |
| Store the changed pieces | 8 pieces, each compressed **twice** (as XOR and as a full copy, the smaller kept) | 8 pieces compressed once as XOR; the full copy only when the XOR is large |
| Every 50th frame | **all ~1,000 non-zero pieces stored again in full** (the key frame) | nothing; a piece is stored Full only when its own chain reaches the limit |
| Keep the delta base | **copy all 4 MB** | copy the 8 changed pieces (32 KB) |
| Reference table | **write 256 × 16 B = 4 KB** | copy the blocks that changed (2–3 × 128 B) and share the other ~30; measured 520 B per frame in memory, 280 B in the file (E3) |
| Device memory (with a GS card) | **copy 128–512 KB into the GS blob** | the GS RAM is a region: only its changed pieces are stored |
| Seek one frame back | **decode all ~1,000 pieces** into RAM (765–785 µs) | decode the 8 pieces that differ |

Measured today (Phase 0, Step 2): 190 µs per capture on ZX-Evo with nothing written, 7–60 µs on 128K-class machines, p99 2.9× the median, 300–420 µs with a GS card. Target: capture cost follows the number of changed pieces, the same on every machine.

## 3. How it works today

### 3.1 Capture (`OnFrameBoundary`, TTM:881)

`CaptureNow` (TTM:979-1056) runs, in order:
1. CPU and chipset structs (TTM:994, 998);
2. every device blob (`CaptureAll`, TTM:1005);
3. the key-frame decision: `frame - _lastKeyFrameIdx >= 50` (TTM:1019, `kKeyFrameInterval`, timetravelmanager.h:386), or `_forceNextKeyFrame` (set after `InvalidateSession` TTM:486 and after a load TTM:4588);
4. RAM:
   - the first checkpoint interns every piece Full (`CaptureBaselineRamPages`, TTM:1058-1087);
   - later checkpoints call `CollectAndClear` on the dirty tracker, then `UpdateRamPages` (TTM:1046-1049, 1107-1253):
     - a clean page adds a reference to its 4 previous pieces;
     - a dirty page is XOR'd against `_prevPageCache` (`InternXorCached`);
     - on a key frame every non-zero page, clean or not, is stored again Full (TTM:1146-1167);
5. `UpdatePrevPageCache()` copies **all** model RAM into the cache (TTM:1282-1296).

Dirty tracking: one bit per 16 KB page (`ttddirtytracker.h:7-19`), set by the debug write path (`memory.cpp:438-445`), `DirectWriteToZ80Memory` (`memory.cpp:1806-1812`) and `MarkRamPageEdited` (TSConf DMA, `tsconfdma.cpp:162`). Recording forces debug mode on so the fast write path is never used (TTM:183-197).

### 3.2 Page store (PS)

- A slot is `{encoding, refcount, prevSlot, crc32c, payload}` (PS.h:205-211). An XorPrev slot references its base by slot id and holds a reference to it (PS.cpp:115-126).
- `InternXor`: an all-zero difference returns the previous slot with one more reference (no new slot); otherwise both the XOR and the full piece are compressed and the smaller kept (PS.cpp:73-181). zstd level 1, CRC32C of the raw piece.
- Decoding walks the whole chain recursively (PS.cpp:253-312). Nothing in the store bounds chain length; only the key frame does (comment PS.cpp:221).

### 3.3 Checkpoint references

`ramPages`: one `TTDPageRef` (4 × u32 slot ids, 16 B) per RAM page, in every checkpoint (ttdcheckpoint.h:262-282, 316). Clean pages repeat the previous ids.

### 3.4 Restore (`RestoreCheckpoint`, TTM:1404-1492)

CPU → chipset → device blobs (`RestoreAll`, before banking because model serializers restore paging latches) → `UpdateZ80Banks` → `RestoreRamPages` (every piece decoded straight into live RAM, CRC failure zero-fills, TTM:1532-1574) → screen resync.

### 3.5 File (`SerializeSession`, TTM:3479-3879)

- Live slots, ordered so each XorPrev follows its base.
- Then per checkpoint:
  - frame, `globalT`, `frameKind`, `keyFrameAnchor`, CPU, chipset;
  - `model_ram_pages × 4 × u32` references;
  - the device blobs.
- `kSchemaVersion = 1`; the format has been amended in place before (ttddumpformat.h:37-47), each time with the fixtures re-recorded.

### 3.6 Device memory today

| Memory | Size | In checkpoints today | Where |
|---|---|---|---|
| General Sound RAM | 128–512 KB | whole, inside the GS blob, every checkpoint | soundchip_gs.cpp:835-844 |
| GS lightweight upload store | up to the card RAM size, variable | whole, inside its blob (variable blob size) | soundchip_gslw.cpp:1311-1313 |
| MoonSound wave memory | up to 1 MiB | **not captured** | soundchip_moonsound.h:169-172 |
| NeoGS RAM / flash | 2–4 MB / 512 KB | **not captured** | soundchip_neogs.h:244-257 |
| ZX-Evo AVR EEPROM | 4 KiB | **not captured** | evoavr.h:149-151 |
| Scorpion SMUC EEPROM + serial-link state | 2 KiB + ~30 B | **not captured** | smucnvram.h:20-40 |

No region concept exists in the code yet; several headers already point at it (ttdserializable.h:58, ttddumpformat.h:219).

## 4. Design

### 4.1 Overview

```
 region table (session header)          page store (shared by all regions)
 ┌────┬───────────────┬────────┐        ┌──────────────────────────────┐
 │ id │ name          │ pieces │        │ slot: Full | XorPrev | Zero   │
 │ 0  │ ram           │ 1024   │        │       depth (chain length)    │
 │ 1  │ gs.ram        │ 128    │        └──────────────────────────────┘
 └────┴───────────────┴────────┘                      ▲
                                                      │ slot ids
 checkpoint N:  per region → [block 0][block 1]…[block k]   (copy-on-write,
 checkpoint N+1:            → [block 0][block 1']…[block k]  shared when equal)
```

Each region keeps:
- its own dirty bitmap;
- its delta base (previous contents of every piece);
- a reference table cut into copy-on-write blocks.

All regions share one page store, so every piece — machine RAM or device memory — gets the same deltas, the same sharing and the same chain length limit.

### 4.2 Step 1 — Memory regions

**Data.**

```cpp
enum class TTDRegionId : uint16_t      // stable: stored in files
{
    MachineRam = 0,
    GeneralSoundRam = 1,
    GeneralSoundUploadStore = 2,
    MoonSoundWaveMemory = 3,
    NeoGSRam = 4,
    NeoGSFlash = 5,
    EvoAvrEeprom = 6,
    SmucEeprom = 7,
    // future: TSConf VDAC2 (FT812) memory, Spec256 shadow RAM, Sprinter video RAM, DivIDE RAM, ...
};

struct TTDRegionDesc
{
    TTDRegionId id;
    PeripheralId owner;      // PeripheralId::Count for machine RAM
    uint8_t* memory;         // live memory, owned by the device
    uint32_t pieces;         // capacity in 4 KB pieces (a 2 KiB EEPROM is 1 piece)
    uint32_t bytes;          // real size (the last piece may be partial)
    const char* name;        // "ram", "gs.ram", "moonsound.wave", ...
};
```

- **Ids** are a fixed table, like `PeripheralId`: appended, never reused. The first change to reach master takes the next free number.
- **Registration.** `RegisterMachinePeripherals` (`ttdmachineperipherals.cpp`) registers machine RAM as region 0. A device that owns memory registers its regions next to its blob serializer. The set is fixed for a session, which holds because a device change (the GS card switch) is already refused during a recording (FR-4).
- **Partial pieces** (2 KiB SMUC EEPROM): the unused tail of the last piece is treated as zero and never read from or written to device memory.
- **Variable used size** (GS upload store): the region has the capacity of the largest store, and the used size stays in the device blob. This removes the only variable-size blob.

**Dirty marking.**
- Machine RAM keeps the existing 16 KB tracker; one dirty page means 4 dirty pieces.
- A device region keeps a bitmap at 4 KB piece granularity, set by the device's own write path:

| Region | Write path to hook |
|---|---|
| General Sound RAM | `SoundChip_GeneralSound::writeMem` (soundchip_gs.cpp:674) |
| GS upload store | the lightweight player's store writes (soundchip_gslw.cpp) |
| MoonSound wave memory | libopl4 already keeps a dirty bitmap: `Opl4::RamDirtyBitmap` / `ClearRamDirty` (opl4.h:113-114) |
| NeoGS RAM / flash | `NeoGSMemory::write` / `writeFlash` (neogsmemory.h:67, 99) |
| ZX-Evo AVR EEPROM | the EEPROM-window write (evoavr.cpp:36) |
| SMUC EEPROM | the page commit on STOP (smucnvram.cpp:63) |

- **Cost rule** (performance guidelines): the GS and NeoGS write paths run per card-CPU write. The hook is one gated bit-set: no work at all when no recording runs, and an A/B benchmark (`core-benchmarks`, GS frame) shows it within noise.

**Device blobs shrink.** The GS blob loses its RAM copy (`95 + RAM` bytes → 95), and the lightweight blob loses its store. Their blob layout versions increase.

**Restore order.**

1. CPU, chipset.
2. Device blobs.
3. Banking.
4. **All regions.**
5. Screen.

After its region is restored, a device with derived caches gets an `OnRegionRestored(id)` call. NeoGS, for example, may cache memory windows.

**Restore through the device, where memory is not plain memory.** By default a region is restored by copying pieces into `memory`. A device may instead supply a restore function per piece, for memory where a write has side effects or where some words are computed on read rather than stored. The first such device is the TSConf VDAC2 card (design in progress: `docs/inprogress/2026-10-01-tsconf-vdac2/vdac2-tdd.md`): FT812's register page holds computed registers (`REG_ID`, `REG_CLOCK`, the command-ring pointers), and its display list must be rebuilt after a restore. Its regions are graphics memory (1 MB), display list (2 × 8 KB), registers (4 KB) and command FIFO (4 KB).

### 4.3 Step 2 — Chain length limit per piece

- Each slot records its **depth**: 0 for Full and Zero, the base's depth + 1 for XorPrev (PS.h slot gains `uint16_t depth`).
- `InternXor` stores the piece Full instead of XorPrev when the new depth would reach **K**, so a chain has at most K − 1 XorPrev links. K = 50 (confirmed by [E1](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e1-chain-limit/README.md) and [E4](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e4-restore-differences/README.md)): the deepest chain is 49 links, exactly today's maximum (a key frame every 50 frames), so the worst-case seek decode does not change. The final value comes from BM-5 (seek) against BM-8 (capture).
- What a link costs, measured: decoding an XOR piece takes about 1 µs and a Full one about 2 µs on real data (phase-5 results, `2026-07-19-time-travel/phase-5-codec-poc-results.md:1389`); with today's 50-frame bound a seek walks 24 links on average and 47 at p95 (same document, :107-117).
- The global key frame goes away: `kKeyFrameInterval`, `_lastKeyFrameIdx` and the key-frame branch of `UpdateRamPages` (TTM:1146-1167).
- `_forceNextKeyFrame` keeps one meaning only: the next capture is a **baseline** (all pieces Full), after `StartRecording`, a load or `InvalidateSession`.
- **No synchronized spike, without staggering.** The concern: every piece starts its chain at the same baseline and changes every frame, so all chains reach K on the same frame, which is the old key-frame spike again. Measured on 17 real recordings ([E1](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e1-chain-limit/README.md)): pieces change at different moments, so with K = 50 at most 4 pieces per frame are forced to Full (v1's key frame: up to 30), and stored bytes are 0.51 of v1. Staggered per-piece limits brought that to 2–3 but cost 5–9% more bytes, so they are **not** part of the design. A test pins the spike bound on the benchmark workloads.

### 4.4 Step 3 — Delta base for changed pieces only

- The delta base stays a full copy of each region's previous contents (4 MB on ZX-Evo), so a changed piece is XOR'd without decoding anything. It is no longer copied every frame: after a capture, **only the pieces dirty in that frame** are copied from live memory.
- Invariant: the delta base equals the decoded contents of the latest checkpoint. It holds as long as every write reaches a dirty bit, which recording guarantees by forcing debug mode, the DMA path and tool edits (`MarkRamPageEdited`).
- **Explicit rebuild only before the next capture**: when recording starts or resumes from a position, and after a load. A seek in a stopped session only marks the delta base stale, so seeking pays nothing for it (a full rebuild costs about 5.4 ms per 4 MB, `ttd-v1-architecture-and-format.md:460`). This is also the definitive fix of the stale-cache class of bugs (B1).
- **Memory cost**: the delta base is a permanent copy of every region - 4 MB for ZX-Evo RAM, up to 4.5 MB more for NeoGS, 1 MiB for MoonSound. A device region gets its delta base only when one of its pieces is first written, so an idle card costs nothing. Phase 4 reports it in the memory accounting.
- Safety net in debug builds: every N-th capture compares the delta base with live memory for the clean pieces and asserts. Existing tests that write memory through raw pointers (`RAMPageAddress()`) without marking it dirty will trip this check (`2026-07-19-time-travel/phase-2-seek-engine.md:196-200`); they are found by running the suite once with the check on every capture and fixed to call `MarkRamPageEdited`.

### 4.5 Step 4 — Copy-on-write reference table

- A region's reference table is cut into **blocks of 8 pages**: 32 pieces, 8 × 4 slot ids = 128 B. Measured ([E3](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e3-reference-blocks/README.md)): table bytes 0.21 of v1 over all inputs, and on ZX-Evo 4,096 → 520 B per frame in memory and 280 B in the file. 16-page blocks give 0.26, 4-page blocks 0.22.
- **Two levels, both shared.** A region's block table (one pointer per block) is itself copy-on-write: a checkpoint holds one pointer per region to its block table. A frame in which nothing changed costs one pointer per region in memory, independent of installed memory. A one-level design (one pointer per block in every checkpoint) would still grow with installed memory: 32 entries for ZX-Evo RAM at 8-page blocks, 16-32 more for NeoGS, and in the file it alone would exceed the PR-10 limit of 64 B per unchanged frame.
- Capture clones a block only when one of its pieces got a **new slot id**, not merely a dirty bit: a dirty 16 KB page whose 4 KB pieces came out unchanged (the all-zero XOR fast path) clones nothing. Measured: 92.9% of dirty 16 KB pages have only one changed 4 KB piece (phase-5 results :73-80).
- The block size is tuned with BM-3; the estimate in [target-architecture §3](target-architecture.md) is 8–16× less table memory at 1–2 dirty pages per frame.
- Reference counting of slots does not change: a block holds one reference per slot id it contains, taken when the block is created and released when the block is freed. Sharing a block costs nothing.
- **Branched histories** (FR-22): a branch's first checkpoint shares its parent's blocks, so Phase 1 adds nothing that assumes one straight timeline.

### 4.6 Step 5 — Device memory as regions

| Device | Region(s) | Notes |
|---|---|---|
| General Sound (classic) | `GeneralSoundRam` | the blob keeps registers only |
| GS lightweight | `GeneralSoundUploadStore` | used size in the blob |
| MoonSound | `MoonSoundWaveMemory` | uses libopl4's dirty bitmap; completes MoonSound "Tier B" ([MoonSound TDD](../2026-09-13-moonsound/2026-09-13-0217-opl4-ttd-integration-tdd.md)) |
| NeoGS | `NeoGSRam`, `NeoGSFlash` | flash writes are rare; its blob layout version increases (`TTD_LAYOUT`) |
| ZX-Evo AVR | `EvoAvrEeprom` | 1 piece |
| Scorpion SMUC | `SmucEeprom` | 1 partial piece; the serial-link state (mode, shift register, address, page buffer) goes into a new SMUC blob next to the existing `Ds12887` one |

Not regions:
- TSConf: its main RAM is machine RAM, and CRAM / SFILE stay in its 2 KB blob.
- ZX-Poly: each of the four members records its own machine RAM, as today.

### 4.7 Step 6 — Restore only the pieces that differ

Today a seek writes every piece of memory into live RAM (TTM:1532-1574). On ZX-Evo memory restore takes 765-785 µs at p50, the largest part of a seek (Phase 0, Step 2, BM-6), although two nearby positions differ in a handful of pieces. Measured ([E4](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e4-restore-differences/README.md)):
- **about 550 of those 760 µs is writing zeros**: v1's baseline stores every piece, all-zero ones as Zero slots, and ZX-Evo at the BASIC prompt has 1,003 all-zero pieces out of 1,024;
- restoring only differing pieces: an estimated **~145 µs** on ZX-Evo (5×) and 34 µs on Pentagon 1024. A fully used 128K machine saves about a third, because the pieces that differ are the busy ones with the longest chains.

- Each region keeps a **live slot map**: for every piece, the slot id whose content is in live memory now. It is set by a capture (the piece's new slot) and by a restore (the target's slot).
- A write after that point makes the piece's map entry unknown. The region's dirty bits already say which pieces were written, so no new tracking is needed.
- Restore decodes a piece only if its target slot id differs from the live map, or if the piece was written since; every other piece already holds the right bytes.
- Expected effect: restore cost follows the difference between the two positions, not installed memory. A seek one frame back on ZX-Evo decodes about as many pieces as that frame changed.
- Guard: the corpus and state-completeness tests compare full memory after restore, so a wrongly skipped piece fails them. A debug-build mode decodes everything and compares.

### 4.8 Step 7 — Encode a changed piece once

`InternXor` compresses both the XOR difference and the full piece and keeps the smaller (PS.cpp:73-181). The full compression costs 8.6 µs against 0.96 µs for the XOR on real data and almost never wins: mean stored XOR piece 39-93 B, full about 1.4 KB (phase-5 results :1387-1392; `ttd-v1-architecture-and-format.md:958-981`).

- Compress the XOR first. Compress the full piece only when the XOR result is larger than **128 bytes**, and keep the smaller. Measured on 70,283 real changes ([E2](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e2-encode-once/README.md)): the full piece wins in 5% of changes; at T = 128 the full compression is skipped for 83% of changes, encoding takes 0.28 of the v1 time, and stored bytes grow by 0.07%. 1 KB would cost 3.7% bytes for little more speed.
- A piece the chain limit forces to Full is compressed once, as Full.
- Expected effect: the cost per changed piece drops several-fold, which sets the slope of BM-8 (PR-4).
- Bytes: identical whenever the XOR is below the threshold; the matrix comparison shows the rest.

### 4.9 File format

Amended in place (the versioned format comes in Phase 5); `ttd.ksy`, the Python analyzer and `testdata/ttd/README.md` change with it:

- the header gains the **region table**: id u16, owner u8, pieces u32, bytes u32, name (u8 length + bytes). Every field width is checked against the largest possible value before the format is fixed: the one-byte `model_ram_pages` once turned 256 pages into 0 (`2026-07-19-time-travel/phase-S1-session-serialization.md:267-274`);
- checkpoints lose `frameKind` / `keyFrameAnchor`;
- references are written as **blocks**: a block section holds each distinct block once (block index u32); each checkpoint lists only what changed since the previous checkpoint - (region, block position, block index) triples - and the reader rebuilds the tables going forward. An unchanged frame writes a zero count (PR-10). The first checkpoint and every checkpoint after a baseline list all blocks;
- device blobs change as listed in §4.6;
- the fixture corpus and the CI gate baseline are re-recorded once, at the end of the phase.

### 4.10 What does not change

- The public API: every WebAPI route, CLI command, MCP tool and Lua/Python function keeps working (QR-8). Phase 1 adds no new automation fields; memory accounting per region comes in Phase 4.
- Dirty tracking of machine RAM stays at 16 KB.
- The stored codec stays as it is: Full / XorPrev / Zero, zstd level 1, CRC32C per piece, reference counting. Only the encoder's choice changes (Step 7).
- Not adopted, with the measurement behind it: content deduplication across pieces (4% hit rate, turned off; phase-5 results :22, :235, :413); 4 KB dirty tracking and a fast "hot tier" (target-architecture §3).

## 5. Tests

Every new test is checked by mutation: it must fail when the mechanism it guards is removed.

| Step | Test | Proves |
|---|---|---|
| 1 | Region round trip per device: write device memory, record, seek back past the write | old contents come back (GS, upload store, MoonSound, NeoGS, both EEPROMs) |
| 1 | State-completeness test extended to regions | every registered region is restored on every creatable model |
| 1 | Session file round trip with regions | save / load restores every region byte for byte |
| 2 | A piece changed every frame is stored Full exactly when its chain reaches K | the limit works per piece |
| 2 | Benchmark workloads: forced Full stores per frame stay at the E1 bound (≤ 4) | no synchronized spike |
| 2 | Pieces that never change stay at depth 0 and are never re-stored | cost follows change |
| 3 | Delta base equals live memory after capture, seek, resume and load | the delta base invariant |
| 3 | Mutation: skip the dirty-piece refresh → the test fails | the guard is real |
| 4 | Consecutive checkpoints with one dirty page share all but one block | copy-on-write works |
| 4 | Block reference counts after releasing checkpoints | no leak, no early free |
| 4 | An unchanged frame adds one pointer per region in memory and a zero count in the file | cost does not follow installed memory |
| 6 | Seek one frame back decodes only the pieces that frame changed; seek after replay decodes the pieces the replay wrote | restore follows the difference |
| 6 | Mutation: treat every piece as equal → the corpus test fails | the guard is real |
| 7 | Small XOR difference: full compression not run; large difference: the smaller result kept | encode once |

Existing tests that change because the key frame goes away (`ttdformatv2_test.cpp` `IFrame_*` / `PFrame_*`, `ttdfullrestore_test.cpp` `KeyFrame_*`, `ttdseekexhaustive_test.cpp` `Setup_RecordsSessionSpanningTwoKeyFrames`) are rewritten around the chain length limit, keeping what they proved: a seek anywhere restores exactly.

**Regression guards for the whole phase** ([README §2](README.md#phase-1--memory-that-costs-only-what-changes)):
- `TTD_Corpus_Test` compares RAM after restore and replay;
- `TTDBench_Test` gate, with the baseline re-exported and every byte change explained;
- the full benchmark matrix against the Phase 0 baseline: no metric worse by more than 5% on configurations without device memory, and BM-8 / PR-3 met everywhere;
- the full `core-tests` suite with zero warnings.

## 6. Order of work

Each step lands as its own commits, and each commit passes the full gate:

1. **Step 3** (delta base) first: smallest, and it removes the 4 MB copy on its own.
2. **Step 7** (encode once): local to the page store, no format change.
3. **Step 2** (chain length limit): removes the key frame; file change (`frameKind`).
4. **Step 6** (restore only differing pieces): restore path only, no format change.
5. **Step 4** (copy-on-write table): file change (blocks).
6. **Step 1** (regions, machine RAM as region 0): file change (region table). No device regions yet, so behavior and bytes stay the same apart from the format.
7. **Step 5** (device regions), one device per commit: GS, GS lightweight, MoonSound, NeoGS, EEPROMs.
8. Re-record fixtures, re-export the CI baseline, run the full matrix, store it as the Phase 1 baseline, write the results document.

The order 3 → 7 → 2 → 6 → 4 → 1 → 5 keeps every intermediate state a complete, working engine. Implementation order differs from the step numbers, which follow the roadmap.

## 7. Risks and open questions

| Risk / question | Plan |
|---|---|
| K too large: seek decode gets slow on busy pieces; too small: more Full stores | Start at 50 (today's 49-link bound); pick the final value from BM-5 against BM-8 |
| A write path that bypasses the dirty bit (a new device, a tool edit) leaves the delta base stale | Debug-build comparison (§4.4); the state-completeness test covers every region |
| GS / NeoGS write hooks cost time on the card CPU's hot path | One gated bit-set; A/B benchmark required before landing (performance guidelines) |
| Region ids collide with another branch | Fixed table, first to master takes the number (same rule as `PeripheralId`) |
| 8-page blocks are wrong for some device region | Per-region block size; E3 found idle device memory cheap at any size thanks to the second level |
| The file grows a block section that older readers cannot read | Amended in place: old files are re-recorded, as with every amendment so far; versioning comes in Phase 5 |
| Without key frames a damaged Full piece spoils every checkpoint until that piece changes again; key frames used to bound the damage to 50 frames (phase-5 results :358-367) | Recorded as an input for the integrity decision (Phase 4, Step 1); CRC32C per piece still detects it |
| Experiment figures are simulations on recordings, not the implemented engine | The benchmark matrix checks them after implementation: BM-6 memory restore on ZX-Evo expected ~150 µs (E4), table and payload bytes per frame (E1, E3), capture cost per changed piece (E2) |

## 8. Sources and what was not carried over

- Code: master `99467101` (references above).
- Phase 0 measurements: [v0b-benchmark-results.md](v0b-benchmark-results.md).
- Codec and change statistics: `docs/inprogress/2026-07-19-time-travel/phase-5-codec-poc-results.md` (0.98-1.19 dirty 16 KB pages per frame, 651-810 changed bytes per frame, 92.9% of dirty pages with one changed 4 KB piece, encode / decode times), `docs/emulator/design/debugger/time-travel-debug/ttd-v1-architecture-and-format.md` (piece sizes on the fixtures, chain depth, full-capture speed).
- POC 011 (`tools/poc/011-ttd-v2-capture-analysis/`): kept its reference-index measurements (a sparse per-frame index of dirty pages, 36 B + 4 B per dirty page; `knowledge/v2-index-overhead.md`), which shaped the two-level table and the sparse file records of Step 4. **Not carried over**:
  - its "model scalability" numbers: the benchmark always booted a Pentagon and only varied the number of written pages (`benchmarks/ttd_v2_model_scalability_bench.cpp:62`), so they are not measurements of 4 MB or TSConf machines;
  - its "v1 vs v2" gains, measured against a format that stores the whole machine every frame, not against the shipped engine;
  - per-untouched-page back-references and content hashing (`knowledge/ttd-v2-design.md:292-309`): they grow with installed memory;
  - batching several pieces per compression call (its own audit contradicts itself on the effect);
  - its General Sound write rates, which contradict each other; the GS region relies on the measured write hook instead.

