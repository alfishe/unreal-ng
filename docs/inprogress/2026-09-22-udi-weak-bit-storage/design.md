# UDI Weak-Bit Storage + FlakySectorEmulator End-to-End (VORON1)

**Created:** 2026-09-22 · **Status:** implemented (UDIW storage + tool + tests, 2026-09-23); end-to-end resolved with a corrected interpretation — see §10
**Related:**
[FlakySectorEmulator.md](../../WD1793/FlakySectorEmulator.md) ·
[udi.md](../../file-formats/disk-images/udi.md) ·
[Black Raven protection analysis](../../disasm/black-raven-voron-protection/README.md) ·
[universal track model](../2026-09-02-universal-track-model/) ·
[fast disk loading](../2026-09-16-fast-disk-loading/design.md) ·
[agent recipe library](../2026-09-22-agent-recipe-library/) (physical-protection forensics / UDI authoring recipes)

## 1. Problem

`FlakySectorEmulator` (FSE) turned the `DiskImage::RawTrack` per-byte weak-bit
bitmap into FDC behavior — revolution-aware ID search and per-read data
mutation — but **no weak-bit-carrying capture of the protected media exists**,
so the module is inert for the title that motivated it (Black Raven /
VORON1). The only captures we have are FDI and UDI, and:

- FDI structurally cannot express weak bits (one static opinion per byte —
  [fdi.md](../../file-formats/disk-images/fdi.md));
- UDI has no weak-bit field either: the loader refuses the reserved
  multi-revolution track type (`core/src/loaders/disk/loader_udi.cpp:155`) and,
  before this work, **silently dropped weak bits on save** with a warning
  (`loader_udi.cpp:274` as of 2026-09-22). UDI is the project's designated
  "save anything" format, so every weak-bit image that existed in memory
  became lossy the moment it was saved.

**Goal:** make UDI round-trip weak bits, author a weak-bit VORON1 fixture from
the existing single-revolution capture, and have the protection/loader pass all
checks under the real FDC (`fastdisk`/`trdos_traps` off).

## 2. Evidence base

### 2.1 TTD journal (2026-09-22, deterministic boot — first real protection trace)

Recorded with the deterministic entry sequence (see the package
[tools README](../../disasm/black-raven-voron-protection/tools/README.md)):
reset → 48K mode → insert `testdata/loaders/udi/VORON1.UDI` →
`RANDOMIZE USR 15616` → `RUN"boot"`. Frames 0–6361, journal (instance lost,
timeline distilled into the tool docstrings and the package README §8.3):

1. loader starts and **real sector reads happen** — the TR-DOS sector-read
   block at `0x3F17` executes repeatedly, last at ~f2265;
2. ~f2700: loader clears the screen;
3. f3042: a check stub at `0x5D8C–0x5E30` polls a 21t loop at `0x5DE0`
   (22+ iterations), then `ret` at `0x5DE5` pops **0xF5C9** (garbage);
4. NOP march `0xF5C9 → 0xFFFF` (4t per byte, empty RAM), wrap to `0x0000`,
   silent TR-DOS re-initialization, blank screen;
5. **the TR-DOS error entries (`0x1D1A`/`0x1D29`) never execute** — the
   derailment is silent, no error report is printed.

Interpretation: the stage-1 bootstrap (forged catalog executed as code, package
README §8.3) runs and issues its reads; the *check* stage fails on something a
deterministic single-revolution image cannot provide. The two candidates a
weak-bit bitmap can fix are: **fuzzy data** (same sector read twice must
differ) and **floating ID** (a "missing" sector number must occasionally
appear). Which one VORON1 uses is the open gate — see §9.

### 2.2 FDI damage census (`tools/voron1-damage-map.py`)

Across `testdata/loaders/fdi/VORON1.FDI`:

- **138 tracks** shaped `(5 sectors, N=3)` — the reformatted protected band
  (`R=192..196`, 1024-byte sectors) from cylinder 1 onward, both heads;
- **one sector with bad data CRC**: cylinder 59 / head 1 / `R=192`, whose ID
  field simultaneously **lies about its cylinder** (`C=55` on physical 59);
- **three unformatted tracks**: (58,0), (80,0), (80,1).

### 2.3 UDI census (`testdata/loaders/udi/VORON1.UDI`, tool-verified 2026-09-22)

| Location | Shape | Notes |
|---|---|---|
| cyl 0 / head 0 | single `C=0,H=0,R=9,N=3` (1024 B) | the forged track 0 that makes `TrdosCatalog::IsTrdos` refuse autostart |
| cyl 0 / head 1 | 16×256 B, `R=15,16,1..14` | classic TR-DOS interleave; catalog-shaped |
| cyl 1+ (both heads) | 5×1024 B `R=192..196` | the protected band, same as FDI |
| cyl 55 / head 1 | `R=192..196` with **honest `C=55`** | reference copy of the trap track at its claimed position |
| cyl 59 / head 1 | `R=192` with **`C=55` + bad data CRC**; `R=193..196` honest `C=59` | the lying-C + acetone spot |
| cyl 58/h0, 80/h0, 80/h1 | unformatted | probes? |

Note the FDI/UDI divergence on track 0: the FDI's forged-catalog analysis
(package README §8.2–8.3) describes the 16×256 side; the UDI additionally
carries the single-`R=9` head 0. Which head the real `RUN"boot"` chain reads
depends on TR-DOS's head handling — unresolved, tracked in §9.

### 2.4 Protection shapes a weak-bit bitmap can express

| Shape | Physical reality | FSE path | Loader-visible behavior |
|---|---|---|---|
| **Fuzzy data** (acetone-wiped spot) | flux marginal ⇒ data bytes decode differently every pass, CRC never valid | `mutateWeakDataByte` on the data-field range (+ `dataCrcValid=false`) | read succeeds/ends with CRCERR, bytes differ read-to-read |
| **Floating ID** | one revolution's IDAM decodes as the "missing" sector number, next doesn't | weak mark on a (synthetic) sector's IDAM range ⇒ `isIdamVisible` 1-in-4 | persistent retry eventually finds the sector |
| **Lying C** (position proof) | ID claims `C=55` while physically on 59 | **no weak bits needed** — see §3 | readable only with a desynced track register (a copier's reformat breaks it) |

## 3. Datasheet verification: Type II sector matching (do not "fix" the C compare)

During design, `WD1793::locateSectorForType2(track, trackReg, sectorReg)`
([wd1793.cpp:1386](../../../core/src/emulator/io/fdc/wd1793.cpp)) was
suspected of wrongly requiring `ID.C == track register` for READ/WRITE SECTOR.
The in-tree datasheet OCR disproves the suspicion — the real FD179X **does**
compare the ID's Track Number against the **Track Register** for Type II
commands (`docs/WD1793/datasheeets/ocr/wd1793_comprehensive_reference.md`,
TYPE II COMMANDS):

> "When an ID field is located on the disk, the FD179X compares the Track
> Number on the ID field with the Track Register. If there is not a match, the
> next encountered ID field is read … If there was a match, the Sector Number
> of the ID field is compared with the Sector Register."

Decision (recorded to prevent a future mis-fix): **keep the C compare against
the Track Register.** The chip never sees the physical head position — which
is precisely what the lying-C trap exploits: with the register desynced from
the head (software seeks to 55, then issues four STEP-INs with `u=0`, leaving
`trackReg=55` while physically on 59), the `C=55` ID on physical 59 matches
and the sector is returned — with a CRC error from the acetone spot. A
bit-perfect *reformatted* copy would carry `C=59` there, fail the register
compare, and RNF. Our model already reproduces all of this.

Also verified: the datasheet's "must find an ID field with the correct track
number, sector number, side number, and CRC within four revolutions" is the
window FSE's 1-in-4 IDAM visibility was chosen for — consistent.

## 4. Storage design — the `UDIW` trailer chunk

### 4.1 Vehicle: the UDI trailer

The UDI layout ([udi.md](../../file-formats/disk-images/udi.md)) allows
arbitrary bytes between the last track descriptor and the trailing CRC-32 —
the "comment" area; TRX2X-generated files already carry an ASCIIZ comment
there and the loader preserves it verbatim round-trip
(`loader_udi.cpp:205`, `_trailer`). A structured chunk in the trailer is:

- **spec-legal**: any conformant reader ignores unknown comment bytes;
- **integrity-preserving**: the CRC-32 covers everything before it, including
  the chunk;
- **already round-tripped**: no new plumbing for preservation.

### 4.2 Chunk layout (little-endian, like the rest of UDI)

```
"UDIW" | u16 version = 1 | u16 recordCount | recordCount × record
record: u8 cylinder | u8 side | u8 flags | u16 streamOffset | u16 length
```

- `streamOffset`/`length` address a **byte range in that track's raw MFM
  stream** — the same coordinate space as `Sector::idamOffset` /
  `dataOffset` (`diskimage.h`). Ranges are how every consumer wants them:
  `hasWeakIdam` checks IDAM bytes, `mutateWeakDataByte` checks data bytes.
- `u16` suffices: `RawTrack::MAX_TRACK_SIZE` ≤ 65535.
- `flags` reserved 0. Earmarked meaning (not assigned now): bit-mapped duty
  cycle, if a title ever needs a visibility ratio ≠ 1-in-4 (the knob
  FlakySectorEmulator.md §5 anticipates). Unknown flag bits ⇒ warning, record
  still applied (forward compatibility).
- Adjacent/overlapping ranges are legal (weakness is idempotent).

### 4.3 Parse and serialize rules

**Parse** (end of `LoaderUDI::parse`, after the track loop):
1. scan `_trailer` for the first `"UDIW"` occurrence; if the chunk is
   truncated/malformed → warning, ignored (load still succeeds — the chunk is
   advisory metadata, never structural);
2. per record: bounds-check `cylinder`/`side` against the image geometry and
   `streamOffset+length` against that track's `rawSize()`, then apply
   `track->setWeakByte(offset, true)` per byte;
3. **strip the chunk from `_trailer`** (keep the comment bytes only), so a
   later save cannot duplicate it.

**Serialize** (`LoaderUDI::serialize`): for each track with
`hasWeakBits()`, RLE-compress its `weakBitmap()` into records (one record per
maximal run of set bits — the marks are contiguous IDAM/data fields, so a
handful of records per protected track) and append one `UDIW` chunk after the
(preserved) comment. The former drop-warning (pre-2026-09-23,
`loader_udi.cpp:274`) is **removed** — saving becomes lossless. `markClean()`
semantics unchanged
(weak bits are image data, not dirt).

Re-load of a saved file reproduces the exact same weak bitmap ⇒ byte-stable
round-trip, verifiable by the existing `Save_RoundTrip_ByteExact` pattern on
an authored file.

### 4.4 Alternatives considered

- **Multi-revolution track type (`0x80|type`, spec-reserved)** — the "proper"
  extension: store N revolutions, majority-vote like
  `LoaderSCP::mergeRevolutions` and derive the weak bitmap. Rejected for now:
  no multi-rev capture of this media exists to feed it; the on-disk layout of
  the reserved type is not fully documented in the sources we hold; and it is
  strictly more implementation. Deferred (§8, P2) — the trailer chunk does not
  conflict with adding it later.
- **Clock-bitmap abuse** — the clock bitmap is missing-clock sync marks
  (A1/C2), a different concept; overloading it corrupts unrelated tooling.
  Rejected.
- **Sidecar file** (`.udi.weak`) — two files to lose, no CRC binding.
  Rejected.

## 5. Processing design — what FSE does with the marks

All of this exists and is verified; no FSE code changes are required for the
fuzzy-data and floating-ID shapes:

| Mark placement | Path taken | Behavior the loader sees |
|---|---|---|
| data-field range of a sector | `processReadByte` → `mutateWeakDataByte(offset, _time, …)` | every read of those bytes returns fresh deterministic "garbage"; read completes, `dataCrcValid=false` ⇒ CRCERR status at the end (acetone spot) |
| IDAM range (6 bytes from `idamOffset`) of a sector | `locateSectorForType2` → `FlakySectorEmulator::findSector` → `isIdamVisible` | the sector is skipped on 3 of 4 revolutions; a persistent search (TR-DOS re-seek retries, or the loader's own poll loop) finds it within the datasheet window |
| no mark | old code path | byte-identical to pre-FSE behavior |

- **Gating** stays `track->hasWeakBits()` — every solid track pays one branch.
- **Determinism** is preserved (hashes over `_indexPulseCounter`/`_time` only,
  TTD-replay-safe — FlakySectorEmulator.md §4).
- **Lying-C trap**: untouched by FSE (§3); the authored fixture just carries
  the `C=55` ID and the CRC-bad data as captured.

Two verification items during implementation (not expected to need changes):
- confirm `S_READ_CRC` raises CRCERR from `sector->dataCrcValid` (the captured
  cyl-59 `R=192` already has a bad data CRC in the image, so the authored
  fixture inherits it);
- confirm a weak-marked data field whose *stored* CRC is valid does not
  silently pass (mutation changes delivered bytes; CRC status should follow
  the stored-field validity as the datasheet's per-field CRC model dictates).

Out of scope (unchanged from FlakySectorEmulator.md §5): write-side
flakiness; whole-track noise for unformatted tracks (a check reading an
unformatted track today gets RNF — if VORON1's stub expects noise *bytes*, see
§9).

## 6. VORON1 authoring plan

### 6.1 Candidate marks

| # | Mark | Shape addressed | Evidence strength |
|---|---|---|---|
| M1 | cyl 59/h1 `R=192` **data field** → weak | fuzzy data | **prime suspect** — the only CRC-bad data on the disk, physically co-located with the lying-C trap; a fuzzy-compare check ("read twice, differ") fails silently on deterministic media, matching the journal's silent derailment |
| M2 | synthetic sector (`R` inside `1..16`) with weak IDAM on a protected track | floating ID | secondary — would explain a poll loop like `0x5DE0` that never sees its sector; requires knowing which `R` the check waits for and what data it must return |
| M3 | none on cyl 55/h1, 58, 80 | — | reference/probe tracks; expected to work as captured |

Decision gate: the stub disassembly (`0x5D8C–0x5E30`, voron-4) plus io-write
mining of a fresh TTD journal (ports `0x1F`/`0x5F` → the exact command/sector
sequence the check issues) selects M1/M2 and pins M2's sector number and data
if needed. Both marks can coexist; start with M1 alone.

### 6.2 Authoring tool — `tools/voron1-author-weak.py` (package `tools/`)

Pure python, stdlib only, writes the §4.2 chunk so **no C++ round-trip is
needed to produce the fixture**:

1. parse the UDI container directly (16-byte header + ext, per track:
   `type|u16 len|len bytes|⌈len/8⌉ clock bytes`);
2. locate fields in the raw stream: `A1 A1 A1 FE C H R N crc crc` for IDAM,
   `A1 A1 A1 FB/F8 …` for the data field; `dataSize = 128 << (N & 3)`;
3. apply a declarative mark list (the M1/M2 candidates are data, not code);
4. append the `UDIW` chunk to the existing trailer, recompute the CRC-32 with
   the **signed-accumulator variant** (`CRCHelper::crcUDI` — replicate it, and
   validate the replication against a stock fixture's stored CRC before first
   use);
5. write to `scratch/` (test-artifact rule). Promotion to `testdata/` only if
   it becomes a committed test fixture.

### 6.3 What the tool must NOT do

- mutate raw bytes or clock bits (marks are additive metadata);
- write outside `scratch/`;
- leave a duplicate `UDIW` chunk if the input already carries one (strip
   first, same rule as §4.3).

## 7. Validation plan

1. **FSE unit test** (the gap FlakySectorEmulator.md §6 names): synthetic
   track — weak-IDAM sector visible on revolutions 0/4/8 and RNF elsewhere;
   weak data byte varies with `_time`; solid track byte-identical.
2. **Loader round-trip** (`LoaderUDI_Test`): authored file loads with the
   expected `weakByte()` set; save → reload is byte-stable; comment preserved;
   no chunk duplication on second save; malformed chunk ⇒ warning + load OK.
3. **CRCERR interplay**: read of the marked CRC-bad sector ends CRCERR with
   mutated bytes delivered (§5 verification items).
4. **End-to-end (voron-8)**: `tools/voron1-detboot.py` on the authored
   fixture — loader proceeds past the `0x5DE0` poll, no NOP march, game
   screen. Journal again for evidence.
5. **No-regression**: full WD1793/DiskImage/loader suites green (the chunk
   parse path is inert for all stock fixtures — none carries `UDIW`).

## 8. Implementation order

| Step | Deliverable | Gate |
|---|---|---|
| 1 | `UDIW` parse+serialize in `loader_udi.{h,cpp}` | loader round-trip test green |
| 2 | FSE synthetic unit test | documents the existing behavior |
| 3 | `tools/voron1-author-weak.py` + `scratch/voron1-weak.udi` (M1) | chunk re-parses; stock emulator loads it (warnings empty) |
| 4 | fresh journal via `voron1-detboot.py` on stock + authored images; stub disasm + io-mining | M1/M2 decision evidence |
| 5 | final marks; end-to-end pass | voron-8 green; docs updated (below) |
| P2 | multi-revolution track type read support (§4.4) | only when a real multi-rev capture exists |

Docs already carry the design as clearly-marked pointers (2026-09-22 wiring
pass): `udi.md` gained a `UDIW` section ("designed, not yet implemented"),
`FlakySectorEmulator.md` §2/§5/§6 point here, the package README §7 carries
the M1/M2 update blockquote, and the stale `flakysectoremulator.h:17` claim
(UDI filling `_weak`) was corrected in the header comment. Step 5 flips those
markers to "implemented" and answers package README §7 with the final mark
list. (Flip executed 2026-09-23 — see §10.1.)

## 9. Open questions

1. **Which mark does the check need (M1 vs M2)?** — **Answered 2026-09-23, and the
   premise dissolved: neither.** The `0x5DE0` "poll" and the whole §2.1 journal
   derailment were artifacts of the boot harness paging lock (§10), not a
   protection check. With paging unlocked, the boot path (149 READ commands,
   cylinders 0–13 + 64–68) never reads cylinder 59 at all; M1's marked sector
   is inert in this flow. The lying-C trap remains a *later-in-the-disk* check
   whose consumer has not been triggered yet (see §10.4).
2. **FDI/UDI track-0 divergence** — which head does the real `RUN"boot"`
   chain read (single-`R=9` h0 vs 16×256 h1)? Affects only which bytes are
   "stage 1", not the marks.
3. **Unformatted-track expectations** — does any check read cyl 58/80
   expecting noise rather than RNF? (Journal showed reads only through the
   TR-DOS block; unprobed so far.)
4. **CRC semantics for mutated bytes** — §5 verification items.

## 10. Outcome (2026-09-23)

### 10.1 Shipped

Everything in §8 steps 1–4 landed: `UDIW` parse/serialize in
`loader_udi.cpp` (drop-warning removed), `LoaderUDI_Test` weak-chunk cases
(round-trip, comment preserved, malformed ignored), the FSE synthetic unit
test, `tools/voron1-author-weak.py`, and `scratch/voron1-weak.udi` (M1 mark:
cyl 59/h1 `R=192` data field). All 181 UDI/WD1793/DiskImage/Flaky tests and
the full 20-shard `test-parallel` suite pass.

### 10.2 The "protection failure" was a harness bug

The §2.1 journal derailment (`0x5DE0` poll → `ret` pops 0xF5C9 → NOP march →
silent re-init) reproduced identically on stock and M1 images because it was
never a weak-bit check failing. The boot recipe entered 48K BASIC from the
128K menu (`POST /basic/mode 48k`), and that ROM transition ends with
**`OUT 0x7FFD,0x30` — bit 5 latches the paging lock**. Every later loader
bank write (21 of them, all traced with `handled_inline=true`) updated `p7FFD`
only; `switchRAMPage` never ran, the 0xC000 window stayed on page 0, all
game banks piled into one physical page, and the loader derailed when the
post-load bank walk-back hit unmapped state.

Evidence chain (all on PENTAGON 128, `master/69273c70`):

- porttrace on the menu transition: final write `0x7FFD←0x30` (48K path)
  vs only `0x17/0x07` toggles (128K path);
- pure-execution OUT test on a fresh instance (`scratch/pentagon-paging-test.py`):
  6/6 pass — bank switch works, bit-5 lock engages for both `OUT (C),A` and
  `OUT (#FD),A`, reset clears the lock — the emulator is faithful;
- `/basic/mode 128k` control: paging alive after boot (bank walk `ram6→ram4→ram7→lock`).

All harness scripts were fixed to boot via 128K BASIC
(`scratch/voron1-{datadump,midload,gameplay,porttrace,detboot-weak}.py`, the
package `tools/voron1-{detboot,trap-chain,catch-error}.py`).

### 10.3 Corrected end-to-end result (voron-8 gate)

With the fixed recipe, **both the stock `VORON1.UDI` and the M1 weak fixture
boot to a running game**: 149 READ commands (cyl 0–13 title/menu band +
cyl 64–68 game band, `R=192..196`, all completing `st=00`), full depack
across all 8 RAM pages, PC settles in game code (`0x775F`), DI/IM2,
animated intro, and zero FDC activity afterward (fully RAM-resident;
`scratch/voron1-datadump-{weak,stock}-128k.json`, 214 919 / 215 746 events).
Weak-vs-stock RAM diffs are animation-phase differences in the screen pages
only. The `0x62A4` "unpack loop" from earlier notes was the depack engine
working as designed.

Consequence for this design: **the M1 mark is not consumed by the boot
path** — the goal "have the protection/loader pass all checks under the real
FDC" is met by both images equally, so the live A/B proof of
FlakySectorEmulator on this title remains open (§10.4). FSE's correctness
currently rests on its synthetic unit test plus the WD1793 suite.

### 10.4 What remains open

- The lying-C + CRC-bad sector (cyl 59/h1 `R=192`, `C=55` ID) is per the
  package README §3 a *second, later* protection check. No boot/menu/intro
  action (ENTER/SPACE/1–5 tapped with porttrace armed) triggered any FDC
  access after load. Finding its consumer (deeper gameplay, a specific scene,
  or a disk-verify operation) is the remaining path to a live weak-bit A/B
  on this title.
- §9.2–9.4 stay open as before (they only matter once a consumer exists).
- M2 (floating ID) remains unauthored — nothing in the observed boot program
  needs it.
