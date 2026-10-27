# Phase 1 — Engine core: memory that costs only what changes — technical design

Status: **design, not implemented** (rewritten 2026-10-02 for the new engine; the in-place version of 2026-10-01 is in git history). Roadmap: [README §2, Phase 1](README.md#phase-1--engine-core-memory-that-costs-only-what-changes). Decisions: [engine-decisions.md](engine-decisions.md) D1–D6, D15, D19–D22, D27, D31, D33. Requirements: FR-5, FR-22, PR-3, PR-4, the memory part of PR-9 / PR-10 ([requirements.md](requirements.md)). Parameters measured on real recordings: [experiments E1–E6](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/README.md).

Code references are to master at `8ddaf708e`. `TTM` = `core/src/debugger/ttd/timetravelmanager.cpp`, `PS` = `core/src/debugger/ttd/ttdcodecpagestore.{h,cpp}`.

## 1. Glossary

| Term | Meaning |
|---|---|
| Engine | `ttd::TimeTravelEngine`, the new time-travel engine. v1 is `ttd::TimeTravelManager` |
| Piece | 4 KB of emulated memory, the unit the engine stores |
| Full / Xor / Zero | How a piece version is stored: a compressed copy; the compressed difference (XOR) from the previous version of the same piece; "all zero", no payload |
| Chain | The Xor versions decoded, newest to oldest, until a Full or Zero one, to rebuild a piece |
| Chain length limit (K) | The most links a chain may have; a version that would exceed it is stored Full |
| Region | A block of emulated memory tracked by pieces: machine RAM (region 0) or memory a device owns |
| Delta base | The previous contents of a piece, needed to compute its difference |
| Reference table | For each checkpoint: which stored version holds each piece of memory at that frame |
| Block | A part of the reference table (8 pages = 32 pieces), shared by checkpoints until one of them changes it |
| Checkpoint | The engine's record of one frame boundary: position, parent, the reference tables, CPU and device state |
| Arena | The memory the piece payloads live in, allocated in large chunks instead of one heap block per piece |
| Oracle | The test that restores a position from the engine and from v1 and compares the bytes |
| Machine time | The engine's single time line: main-CPU cycles in top-clock units since the session start (D20) |

## 2. What changes, in one example

ZX-Evo (4 MB RAM, 256 pages of 16 KB) at the BASIC prompt, one frame in which the program writes two pages, then a General Sound card with 512 KB uploads a sample:

| Work per frame | v1 | Engine |
|---|---|---|
| Store the changed pieces | 8 pieces, each compressed **twice** | 8 pieces compressed once, as XOR; the full copy only when the XOR is larger than 128 bytes |
| Every 50th frame | **all ~1,000 non-zero pieces stored again** | nothing; a piece is stored Full only when its own chain reaches K |
| Keep the delta base | copy all 4 MB | copy the 8 changed pieces (32 KB) |
| Reference table | 256 × 16 B = 4 KB | the 2–3 changed blocks of 128 B; measured 520 B per frame in memory (E3) |
| General Sound RAM | **512 KB copied into the GS blob every frame** | a region: only its changed pieces |
| NeoGS RAM (4 MB), MoonSound wave memory (1 MiB) | **not recorded** | regions, only changed pieces |
| Seek one frame back | decode all ~1,000 pieces (765–785 µs) | decode the pieces that differ (E4: ~145 µs) |
| Memory for a minute of Across the Edge | 107 MB (v1 with exact-size allocation, `67e5aff28`) | modeled 32 MB (E6), most of it the write journal, which Phase 3 owns |

## 3. How v1 works today

### 3.1 Capture (`OnFrameBoundary`, TTM `CaptureNow`)

1. CPU and chipset structs; every device blob, whole (`CaptureAll`).
2. Key-frame decision: every 50 frames (`kKeyFrameInterval`) or after `_forceNextKeyFrame`.
3. RAM: the first checkpoint stores every piece Full; later checkpoints take the dirty pages from `TTDDirtyTracker::CollectAndClear` (16 KB bits, set by the debug write path, `DirectWriteToZ80Memory` and `MarkRamPageEdited`) and intern each dirty piece as XOR against `_prevPageCache`; a key frame stores every non-zero piece again.
4. `UpdatePrevPageCache()` copies **all** RAM into the cache.

Recording forces debug mode on, so the fast write path that skips dirty bits is never used.

### 3.2 Page store (PS)

A slot is `{encoding, refcount, prevSlot, crc32c, payload}`. An XorPrev slot references its base by slot id. `InternXor` compresses both the XOR and the full piece and keeps the smaller. Payloads are stored at their exact size since `67e5aff28`. Nothing bounds chain length except the key frame.

### 3.3 Checkpoints and restore

Each checkpoint holds `ramPages`: 4 slot ids per 16 KB page, 16 B, in every checkpoint. A restore writes CPU, chipset, device blobs, banking, then decodes **every** piece into live RAM, then resyncs the screen.

### 3.4 Device memory today

| Memory | Size | In v1 checkpoints |
|---|---|---|
| General Sound RAM | 128–512 KB | whole, inside the GS blob, every checkpoint |
| GS lightweight upload store | up to the card RAM size | whole, inside its blob |
| MoonSound wave memory | up to 1 MiB | **not recorded** |
| NeoGS RAM / flash | 2–4 MB / 512 KB | **not recorded** |
| Sprinter video RAM / fast RAM | 256 KB / 64 KB | whole blobs (Sprinter TTD, phase S7) |
| VDAC2 (FT812) graphics memory, display list, registers, command FIFO | 1 MB, 2 × 8 KB, 4 KB, 4 KB | **refuses to record** until regions exist |
| ZX-Evo AVR EEPROM | 4 KiB | **not recorded** |
| Scorpion SMUC EEPROM + serial-link state | 2 KiB + ~30 B | **not recorded** |

## 4. Design

### 4.1 Overview

```
            TimeTravelEngine (one per emulator instance)
                 │
   ┌─────────────┼──────────────────────────────┐
   │ session: frame table, checkpoints (each links its parent),
   │          optional streams, regions
   │                                             │
   │  region 0 (ram)    region 1 (neogs.ram) ... │      TTDPieceStore (shared, D22)
   │  ┌──────────────┐                           │      ┌───────────────────────┐
   │  │ dirty bits   │                           │      │ versions: Full|Xor|Zero│
   │  │ delta base   │  checkpoint N ──► region  ├────► │ depth, base, CRC       │
   │  │ live map     │  table ──► [blk][blk']…   │      │ payload in the arena   │
   │  └──────────────┘                           │      └───────────────────────┘
   └─────────────────────────────────────────────┘
        ▲ frame input                    ▲ frame input
   live capture (Step 4)          v1 file feeder (Step 1, verification)
```

Both producers hand the engine the same thing, a **frame input**: the frame's position and, per region, the pieces that changed with their new contents, plus CPU, chipset and device state. The engine never needs to know which producer it is fed by.

New code lives in `core/src/debugger/ttd/timetravelengine.{h,cpp}` and `core/src/debugger/ttd/engine/`. v1 is not changed, except for one gated call in Step 4.

### 4.2 Step 1 — Engine skeleton and verification

**Time and positions** (`engine/ttdtime.h`):

```cpp
using TTDMachineTime = uint64_t;          // main-CPU cycles in top-clock units since the session start (D20)

struct TTDPosition                         // D15
{
    uint32_t branch = 0;                   // 0 = the trunk; branches come later, the field is there now
    uint64_t frame = 0;
    uint64_t tInFrame = 0;                 // machine time since the frame's start
};

class TTDFrameTable                        // D21: frames have no fixed length
{
public:
    void Append(uint64_t frame, TTDMachineTime start);
    TTDMachineTime Start(uint64_t frame) const;
    uint64_t FrameAt(TTDMachineTime t) const;   // binary search
};
```

A position of another CPU (`{cpu, cycle}`) comes with Phase 3; `TTDPosition` is the type every engine call takes from the start, so adding it changes no signature.

**Checkpoints** (`engine/ttdcheckpointrecord.h`): position, machine time, `parent` (the previous checkpoint of the same history, D7), one reference-table handle per region (Step 3), CPU and chipset state as v1 stores it (168 B), and the device blobs as v1 stores them (whole; Phase 2 replaces this).

**Optional streams** (D19, `engine/ttdstreamregistry.h`): a stream has a stable id, a name and an enabled flag. At a frame boundary the engine reads one bitmask of enabled streams and calls only those; a stream that is off costs that one check per frame. Phase 1 registers no optional stream; the screenshot stream comes with the file in Phase 4. Every surface gets its on/off control when the engine is switched in (Phase 5).

**Memory accounting** from the first commit (FR-16, E5): every part reports its bytes (`TTDEngineHeapBreakdown`), and the benchmark's `bm4_heap_*` split covers the engine as it covers v1.

**Feeding the engine from a v1 file** (D31, `core/src/debugger/ttd/bench/ttdv1feeder.{h,cpp}`, verification code, not part of the engine):
1. load the file into a `TimeTravelManager` with `DeserializeSession` (TTM:588);
2. for each checkpoint in order (`GetCheckpoint`), decode its RAM pieces through `GetPageStore().GetPage` and compare with the previous checkpoint's; the pieces whose content changed form the frame input, with the checkpoint's CPU, chipset and device blobs;
3. the General Sound RAM, which v1 keeps inside the GS blob, is split off into its region's pieces (as experiment E6 did), so the GS region can be checked from v1 files.

Only content changes are fed, not v1's key-frame re-stores, so the engine sees what really changed.

**The oracle** (`core/tests/debugger/ttd/engine/`): for every checkpoint of a session, restore the engine's memory into a buffer and compare it with v1's decoded memory, region by region, byte for byte, plus CPU, chipset and device blobs. Inputs: the fixture corpus (`testdata/ttd/`) and the real-use sessions of `common/record-real-sessions.sh` (`scratch/ttd-experiments/real/`). Points inside a frame need the recorded events and replay, which Phase 3 adds; Phase 1 compares every frame boundary.

**Benchmark**: the harness's `Engine` interface (`core/src/debugger/ttd/bench/ttdbench.h`) gains a second implementation, `"engine"`. Until live capture exists (Step 4) it records through the feeder: the workload is recorded with v1 and fed to the engine, so bytes and memory are comparable on every matrix case from Step 1. From Step 4 it records live.

### 4.3 Step 2 — Piece store

`engine/ttdpiecestore.{h,cpp}`, `TTDPieceStore`, replaces `TTDCodecPageStore` for the engine.

```cpp
using TTDPieceId = uint32_t;               // opaque; never an address or an offset

struct TTDPieceVersion
{
    uint8_t encoding;                      // Full, Xor, Zero
    uint16_t depth;                        // 0 for Full and Zero, base depth + 1 for Xor
    uint32_t refcount;
    TTDPieceId base;                       // Xor only: the version it is a difference from (its dependency, D5)
    uint32_t crc32c;                       // of the raw 4 KB
    TTDArenaRef payload;                   // where the compressed bytes live; empty for Zero
};
```

**Interning a change.** `TTDPieceId Intern(TTDPieceId previous, const uint8_t* previousBytes, const uint8_t* newBytes)`:
1. XOR the two; an all-zero XOR returns `previous` with one more reference (no new version);
2. new content all zero → a Zero version;
3. the previous version's depth + 1 reaches K → compress the new bytes once, Full;
4. otherwise compress the XOR; only if the result is larger than **T = 128 bytes**, compress the full bytes too and keep the smaller (E2: the full piece wins in 5% of changes; at T = 128 the full compression is skipped for 83% of changes, encoding takes 0.28 of v1's time, bytes +0.07%).

`K` and `T` are parameters of the store (D4), defaults 50 and 128. E1 measured at most 4 forced Full versions per frame at K = 50 on 17 real recordings (v1's key frame: up to 30), without staggering.

**Decoding** walks the chain iteratively from the newest version to the first Full or Zero, then applies the XORs forward, with one scratch buffer per thread. The CRC of the result is checked (the in-memory CRC question, I-6, is decided in Phase 4).

**The arena** (`engine/ttdarena.{h,cpp}`): payloads are allocated from chunks of 1 MB by bumping a pointer, at their exact size. `TTDArenaRef` is `{chunk, offset, size}`. A chunk records its live bytes; a chunk whose live bytes drop to zero is returned. Small payloads (median 39–93 B, E2 / phase-5 results) cost no heap header each. The reference is location-independent on purpose: in Phase 4 a chunk can be written to the session file and released, and `TTDArenaRef` then names the bytes in the file (D28).

**Dependencies** (D5): a version's `base` is its only dependency on another version; blocks (Step 3) depend on the versions they list. The store can therefore answer "what depends on this" for eviction (Phase 4) and for damage ranges (I-4) without a new structure.

**Shared by sessions** (D22): the store is owned by a `std::shared_ptr` that sessions hold. ZX-Poly's four machines can share one later; in Phase 1 each engine has its own.

### 4.4 Step 3 — Regions and the copy-on-write reference table

**Regions** (`engine/ttdregion.h`):

```cpp
enum class TTDRegionId : uint16_t          // stable, stored in files, appended, never reused (D3)
{
    MachineRam = 0,
    GeneralSoundRam = 1,
    GeneralSoundUploadStore = 2,
    MoonSoundWaveMemory = 3,
    NeoGSRam = 4,
    NeoGSFlash = 5,
    SprinterVideoRam = 6,
    SprinterFastRam = 7,
    Vdac2GraphicsMemory = 8,
    Vdac2DisplayList0 = 9,
    Vdac2DisplayList1 = 10,
    Vdac2Registers = 11,
    Vdac2CommandFifo = 12,
    Vdac2Special = 13,
    Vdac2Inflight = 14,
    EvoAvrEeprom = 15,
    SmucEeprom = 16,
    MultiSoundGsRam = 17,
    EvoFlash = 18,
};

struct TTDRegionDesc
{
    TTDRegionId id;
    uint16_t ownerType;                    // the owning device's type id (Phase 2 registry); 0xFFFF for machine RAM
    std::string ownerInstance;             // e.g. "ngs0"; empty for machine RAM
    uint8_t* memory;                       // live memory, owned by the device
    uint32_t pieces;                       // capacity in 4 KB pieces; no fixed cap (D27)
    uint32_t bytes;                        // real size; the last piece may be partial (2 KiB SMUC EEPROM)
    uint32_t dirtyGranularity;             // 16 KB for machine RAM (today's tracker), 4 KB for devices
    TTDPieceRestoreFn restorePiece;        // optional: restore through the device (VDAC2 registers)
    TTDRegionRestoredFn onRestored;        // optional: rebuild caches after the region is restored
};
```

The unused tail of a partial last piece is treated as zero and never read from or written to device memory. The region set of a session is fixed, as is its device set (D38).

**The reference table: change records and periodic full tables** (as built, 2026-10-02; replaces the copy-on-write-only design below, see the measurement).

- Each checkpoint records, per region, only the pieces that got a **new version**: `{piece, version}`, 8 bytes each. A dirty piece whose content came out unchanged records nothing. A frame in which nothing changed records nothing (PR-10).
- Every **S-th** checkpoint (S = 64 by default) also holds a **full table** of the region, as copy-on-write blocks (§ below) derived from the previous full table: only the blocks with a piece changed since then are new.
- A restore takes the nearest full table at or before the target and applies at most S − 1 checkpoints' change records forward.
- A change record holds one reference to its version; a full table's blocks hold theirs.

Measured (`TTDMatrix`, 600 frames, `UNREAL_TTD_BENCH_ENGINE=all`), reference bytes per frame:

| Case | v1 | Copy-on-write blocks only (8-page blocks) | Change records + full table every 64 |
|---|---|---|---|
| 48K, BASIC | 96 | 160 | **34** |
| Pentagon 128, BASIC | 128 | 160 | **26** |
| Pentagon 1024, BASIC | 1,026 | 218 | **33** |
| ZX-Evo, BASIC | 4,103 | 552 | **78** |
| Pentagon 128, game | 128 | 159 | **62** |
| Across the Edge | 128 | 160 | **69** |

Copy-on-write blocks alone lose to v1 on small busy machines at any block size (4–32 pieces measured): when most frames change several blocks of a 128 KB machine, copying blocks costs more than v1's dense 4 bytes per piece. Change records cost 8 bytes per changed piece, and full tables every 64 frames add a few bytes per frame. S = 32 and S = 128 differ from 64 by a few bytes per frame.

**The full tables, two levels** (E3: 8-page blocks for large regions, smaller blocks for small ones - `TTDRefTables::DefaultBlockPieces`):

```cpp
struct TTDRefBlock                         // 32 piece ids = 8 pages; shared, reference-counted
{
    std::atomic<uint32_t> refcount;
    TTDPieceId pieces[32];
};

struct TTDRegionTable                      // one pointer per block; shared, reference-counted
{
    std::atomic<uint32_t> refcount;
    std::vector<TTDRefBlockPtr> blocks;
};
```

A checkpoint holds one `TTDRegionTablePtr` per region. A capture clones a block only when one of its pieces got a **new version id**: a dirty page whose pieces came out unchanged clones nothing. A region with no changed block keeps the previous checkpoint's table, so an unchanged frame costs one pointer per region (PR-10). A block holds one reference to each version it lists, taken when the block is created and released when it is freed.

**Branches** (D7, FR-22): a checkpoint's `parent` is the checkpoint it continues. A branch's first checkpoint shares its parent's tables, so nothing in the table assumes one straight line. Phase 1 records only the trunk.

### 4.5 Step 4 — Live capture next to v1

The engine records the running emulator in parallel with v1 ("shadow mode"), so every live recording is also an oracle run.

- **One gated call in v1.** `TimeTravelManager::CaptureNow` already collects the dirty pages once per frame (`CollectAndClear`). When a shadow engine is attached, it hands the same list and the live memory to `TimeTravelEngine::CaptureFrame`. Without an engine attached this is one pointer check per frame; the dirty bits are not collected twice, so v1 is unaffected.
- **Delta base for changed pieces only.** Each region keeps a copy of its previous contents. After a capture, only the pieces that changed are copied into it (v1 copies all 4 MB of ZX-Evo RAM every frame). It is rebuilt in full only before the next capture after recording starts, resumes from a position or a load. A device region allocates its delta base when its first piece is written, so an idle card costs nothing.
- **Invariant:** the delta base equals the decoded contents of the latest checkpoint. A debug build compares it with live memory for the clean pieces every N captures and asserts; a test that writes memory through a raw pointer without marking it dirty trips it and is fixed to call `MarkRamPageEdited`.
- **Counted work** (`bm2_work_*`) is reported for the engine as for v1, so D33's "capture work not larger than v1" runs in the CI gate.

The engine can also run alone (no v1) for benchmarks and for tests that restore into the emulator. Shadow mode is a test and benchmark setting; users do not see it.

**As built (2026-10-02).** `TimeTravelManager::SetShadowEngine(engine)`; `CaptureNow` calls `FeedShadow` after its own capture with the same dirty page list (four pieces per dirty 16 KB page), live memory, CPU, chipset and device blobs. v1's history being cleared (`StartRecording`, `InvalidateSession`), loaded (`DeserializeSession`) or cut short (`TruncateTimelineAfter`) ends the engine's session, which restarts with a full rescan at the next capture (the engine records the trunk only until branches exist). A v1 restore (`RestoreCheckpoint`) marks the live memory as differing from the engine's delta base, so the next capture hands over every piece once. The benchmark's `"engine"` runs in shadow mode, so its capture time and counted work are its own. Measured on 600-frame runs at host load ~10:

| Case | Capture p50, µs (v1 → engine) | Capture p99, µs | Delta-base copy per frame |
|---|---|---|---|
| ZX-Evo, BASIC | 355 → 18.5 | 822 → 64.5 | 4 MB → 72 KB (the first frame's full rescan averaged in) |
| Pentagon 1024, BASIC | 110 → 5.6 | 481 → 14.6 | 1 MB → 18 KB |
| Pentagon 128, game | 426 → 64 | 1,098 → 114 | 128 KB → 40 KB |
| Across the Edge | 407 → 42 | 1,004 → 90 | 128 KB → 66 KB |

### 4.6 Step 5 — Restore only the pieces that differ

- Each region keeps a **live map**: for every piece, the version whose content is in live memory now. A capture sets it to the new version, a restore to the target's version.
- A write after that point makes the entry unknown; the region's dirty bits already say which pieces were written.
- A restore decodes a piece only if its target version differs from the live map, or the piece was written since. E4: on ZX-Evo memory restore drops from ~760 to ~145 µs (most of v1's restore writes zeros into untouched memory), on Pentagon 1024 from 188 to 34 µs.
- The oracle restores everything into a buffer, so a wrongly skipped piece fails it. A debug-build mode decodes everything and compares.

**As built (2026-10-02).** `RestoreToMemory(index, written, stats)` builds the target's map from the nearest full table and the change records, and decodes a piece only when the version in live memory differs from the target's or `written(region, piece)` says it was written since; `ForgetMemory()` marks everything unknown (a v1 restore in shadow mode calls it). The test restores a recorded game to a sequence of positions and checks both the memory (against v1) and that the decoded count equals the number of versions that differ between the two positions; a mutation treating every piece as equal fails it. Measured (200 frame-aligned seeks over 600 frames, load ~12), memory restore:

| Case | v1 p50, µs | Engine p50 | Engine p99 | Pieces decoded per seek |
|---|---|---|---|---|
| ZX-Evo, BASIC | 731 | 125 | 233 | 4 |
| Pentagon 1024, BASIC | 174 | 27 | 55 | 1 |
| Pentagon 128, BASIC | 55 | 27 | 55 | 1 |
| 48K, BASIC | 73 | 54 | 111 | 2 |
| Across the Edge | 315 | 180 | 417 | 7.5 |
| Pentagon 128, game | 311 | 337 | 568 | 8.3 |

The game is the one case slower than v1 (+8%): the pieces that differ between two positions are the busy ones, with chains of up to 49 links (the K = 50 trade-off of E1); p99 stays far inside PR-5's 5 ms. K is a parameter of the piece store if Phase 1's matrix run shows a case beyond D33's bound.

### 4.7 Step 6 — Device memory as regions, large memories first

| Order | Device | Region(s) | Size | Dirty marking (write path) | Restore |
|---|---|---|---|---|---|
| 1 | NeoGS | `NeoGSRam`, `NeoGSFlash` | 2–4 MB, 512 KB | `NeoGSMemory::write`, `writeFlash` | copy; `onRestored` refreshes cached memory windows |
| 2 | MoonSound | `MoonSoundWaveMemory` | up to 1 MiB | libopl4's own dirty bitmap (`Opl4::RamDirtyBitmap` / `ClearRamDirty`) | copy |
| 3 | General Sound (classic) | `GeneralSoundRam` | 128–512 KB | `SoundChip_GeneralSound::writeMem` | copy; the GS blob keeps registers only (95 B) |
| 4 | GS lightweight | `GeneralSoundUploadStore` | up to the card RAM | the player's store writes | copy; used size stays in the blob |
| 5 | Sprinter | `SprinterVideoRam`, `SprinterFastRam` | 256 KB, 64 KB | the Sprinter memory write paths | copy |
| 6 | VDAC2 (FT812) | `Vdac2GraphicsMemory` (RAM_G), `Vdac2DisplayList0/1`, `Vdac2Registers`, `Vdac2CommandFifo`, `Vdac2Special`, `Vdac2Inflight` | 1 MB, 2 × 8 KB, 4 KB, 4 KB, 4 KB, 1.06 MB | eve-emu's dirty bitmap (one bit per 4 KB page, every chip write path), read before each capture | copy, then `EveMemoryRestored` (the chip rebuilds what it derives), as v1's `Vdac2Memory` blob does |
| 7 | ZX-Evo AVR | `EvoAvrEeprom` | 4 KiB (1 piece) | the EEPROM-window write | copy |
| 8 | Scorpion SMUC | `SmucEeprom` | 2 KiB (1 partial piece) | the page commit on STOP | copy; the serial-link state goes into a SMUC blob |
| 9 | ZX-Evo ROM flash (TS-Conf, ATM3) | `EvoFlash` (the machine's ROM pages 0-31) | 512 KB | the chip's program and erase completions (`Flash29F040B`, the same tracker hook as `NeoGSFlash`) | copy; the command state is the blob `EvoFlash` (61) ([tdd-evo-flash.md](../2026-09-27-tsconf/tdd-evo-flash.md)) |

- **Cost rule** (performance guidelines): the NeoGS, GS and MoonSound write paths run per card-CPU write. The hook is one bit-set behind the existing "recording" check: no work at all when nothing records, and an A/B benchmark (`core-benchmarks`, the card's frame benchmark) shows it within noise before it lands.
- In shadow mode v1 still copies the GS RAM into its blob; the engine's region is checked against it by the oracle. NeoGS, MoonSound, VDAC2 and the EEPROMs are not in v1 files, so their regions are checked by round-trip tests (record, write, seek back, compare) and by live shadow runs.
- With the device blobs still whole in Phase 1, the GS blob shrinks only in the engine: v1 keeps its own format.

**As built (2026-10-02).** A device offers its memory through `ITTDRegionSource` (`engine/ttdregiontracker.h`): its regions with a `TTDRegionTracker` each, arming, an optional before-capture hook, and, when its blob also carries that memory, its state without it (`TTDStateWithoutRegions`). Sources are registered next to the serializers (`TTDPeripheralRegistry::RegisterRegionSource`; every model serializer that is a source is registered automatically). Two ways to find written pieces:
- **write hooks** (NeoGS RAM and flash, General Sound RAM): the device marks the tracker on its write paths; it holds the tracker pointer only while the engine records, so the path pays one null check otherwise (A/B `BM_HostFrame_NeoGS_*`: within noise); MoonSound uses the wave memory's own dirty bitmap through the before-capture hook;
- **compare at each capture** (`compareEachCapture`: Sprinter video RAM and fast RAM): every piece is offered and the engine keeps those whose content differs from its delta base. Used where writes go through shared paths (the fast RAM is written by the generic CPU path, which no machine-specific hook may slow down) or many paths; the cost follows the region's size, not its changes.

The engine skips any offered piece whose content equals its delta base before doing anything else, so a dirty page rewritten with the same bytes costs a comparison only.

### 4.8 What Phase 4 will serialize

The engine has no file before Phase 4. Phase 1 fixes what the file must hold, so the file needs no new concepts: the region table (id u16, owner type u16, owner instance, pieces u32, bytes u32, name); the piece versions with their encoding, depth, base, CRC and payload; the reference blocks and region tables, each written once and referred to by later checkpoints; checkpoints with their parent; the frame table. Every field width is checked against the largest value it can hold (a one-byte `model_ram_pages` once turned 256 pages into 0).

## 5. Performance

| Path | Runs | Cost rule |
|---|---|---|
| Capture | once per frame | follows the number of changed pieces, not installed memory (BM-8); p99 ≤ 3× the median (PR-3) |
| Device write hooks | per card-CPU write | one gated bit-set, A/B benchmark required |
| Optional streams | once per frame | one bitmask check when all are off |
| Shadow call in v1 | once per frame | one pointer check when no engine is attached |
| Restore | per seek | follows the pieces that differ (Step 5) |

Measured by the benchmark matrix with both engines: bytes and counted work are deterministic and go in the CI gate; times are measured on an idle host (load < 12), twice.

## 6. Tests

Every test is checked by mutation: it must fail when the mechanism it guards is removed.

| Step | Test | Proves |
|---|---|---|
| 1 | Oracle on the fixture corpus and the real-use sessions: every checkpoint, every region, CPU, chipset, device blobs | the engine restores what v1 restores |
| 1 | Frame table: variable frame lengths, `FrameAt` at every boundary | no fixed frame length is assumed |
| 1 | Optional stream off: the frame-boundary path calls nothing | zero cost when off |
| 2 | Piece store: unchanged content adds a reference, not a version; all-zero content gives Zero | change stored once |
| 2 | A piece changed every frame is stored Full exactly when its depth reaches K; K is a parameter | the chain limit per piece |
| 2 | Small XOR: one compression call; large XOR: both, the smaller kept | encode once (T) |
| 2 | Payload bytes per region equal E6's model for the same session, byte for byte | the model and the code agree |
| 2 | Arena: chunk returned when its last payload is released; no heap allocation per payload | exact-size storage |
| 2 | Forced Full versions per frame stay at the E1 bound (≤ 4) on the benchmark workloads | no synchronized spike |
| 3 | Consecutive checkpoints with one dirty page share all but one block | copy-on-write works |
| 3 | An unchanged frame adds one pointer per region | cost does not follow installed memory (PR-10) |
| 3 | Reference counts after releasing checkpoints: no leak, no early free | sharing is safe |
| 3 | A checkpoint's parent chain reaches the session start | branches are possible |
| 4 | Shadow mode: every live frame of the benchmark workloads restores identically from both engines | live capture matches v1 |
| 4 | Delta base equals live memory after capture, seek, resume and load; mutation: skip the refresh → fails | the delta-base invariant |
| 4 | No engine attached: v1's capture work and bytes unchanged (CI gate) | v1 unaffected |
| 5 | Seek one frame back decodes only the pieces that frame changed | restore follows the difference |
| 5 | Mutation: treat every piece as equal → the oracle fails | the guard is real |
| 6 | Per region: write device memory, record, seek back past the write, compare | each device region restores exactly |
| 6 | VDAC2: computed registers and the display list are right after a restore | restore through the device |
| 6 | A/B benchmark of each card's write hook, recording off | the hooks cost nothing when off |
| all | D33 on the whole matrix: file-equivalent bytes, memory and counted capture work not larger than v1's in any case | the engine beats v1 |

## 7. Order of work

Each step lands as its own commits and passes the full gate (zero warnings, `core-tests`, CI gate); v1 keeps running the emulator throughout.

1. **Step 1** — skeleton, feeder, oracle, benchmark engine. The engine stores pieces naively at first (every changed piece Full) so the oracle and the comparison exist before the clever parts.
2. **Step 2** — piece store: chains, K, T, arena. The engine's bytes now follow E6.
3. **Step 3** — regions and the reference table.
4. **Step 4** — live capture in shadow mode, delta base.
5. **Step 5** — restore only the pieces that differ.
6. **Step 6** — device regions, one device per commit, in the order of §4.7.
7. Phase check: D33 on the matrix, the results document, the baseline stored for Phase 2.

## 8. Risks and open questions

| Risk / question | Plan |
|---|---|
| K too large makes seeks on busy pieces slow; too small stores more Full versions | Start at 50 (v1's own bound); pick the final value from BM-5 against BM-8 after Step 5 (E1) |
| A write path that bypasses the dirty bit (a new device, a tool edit) leaves the delta base stale | Debug-build comparison; the oracle and the round-trip tests per region |
| Card write hooks cost time on the card CPU's hot path | One gated bit-set; A/B benchmark before landing |
| The feeder sees only frame boundaries, not what happened inside a frame | Phase 1 compares frame boundaries; points inside a frame are compared from Phase 3, when events are recorded |
| Without key frames a damaged Full version spoils every checkpoint until the piece changes again | Recorded as an input for the integrity decision (Phase 4, Step 1); the CRC per version detects it; dependencies give the damaged range (D5) |
| Region ids collide with another branch | Fixed table, the first change to reach master takes the number |
| v1 files lack NeoGS, MoonSound, VDAC2 and EEPROM memory | Those regions are checked by round-trip tests and live shadow runs, not by the feeder |

No question needs the user's decision before Step 1.

## 9. Sources

- Code: master `8ddaf708e` (references above).
- Measurements: [v0b-benchmark-results.md](v0b-benchmark-results.md); experiments [E1](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e1-chain-limit/README.md) (K), [E2](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e2-encode-once/README.md) (T), [E3](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e3-reference-blocks/README.md) (blocks), [E4](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e4-restore-differences/README.md) (restore), [E5](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e5-heap-split/README.md) (where memory goes), [E6](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e6-v1-v2-model/README.md) (the engine modeled on real sessions).
- Device memory and write paths: the earlier version of this design (git history of this file, 2026-10-01), the [MoonSound TTD design](../2026-09-13-moonsound/2026-09-13-0217-opl4-ttd-integration-tdd.md), the [VDAC2 integration design](../2026-10-01-tsconf-vdac2/vdac2-integration-design.md), the Sprinter TTD outcome (`../2026-09-28-sprinter/s7-ttd-outcome.md`).
- Not carried over from POC 011: its model-scalability numbers (always a Pentagon), its "v1 vs v2" gains (against a format storing the whole machine every frame), per-untouched-page back-references and content hashing (they grow with installed memory), batching several pieces per compression call.
