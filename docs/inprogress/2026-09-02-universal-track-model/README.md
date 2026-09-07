# Work Item: Universal Track Model (any WD1793 + FDD track layout)

> **Date:** 2026-09-02
> **Status:** Design complete; core model, controller integration, TRD/SCL migration and tests implemented (2026-09-02). UDI, FDI and DSK/EDSK loaders implemented (read + write) and wired into `Emulator::LoadDisk` / `Emulator::SaveDisk` (with UDI re-target, `NC_FDD_DISK_SAVE_RETARGETED`); Type II rotational latency and the FM controller path implemented (2026-09-04). TD0 (read + uncompressed write), MGT/IMG and Hobeta implemented as well; all disk formats are selected by extension in `Emulator::LoadDisk` / `SaveDisk` (Hobeta is a file injector, not a disk). HFE/SCP design-only.
> **Priority:** High (unblocks FDI / UDI / DSK / TD0 loaders and copy-protection fidelity)
> **Supersedes:** the "Non-Standard Track Layouts" part (F2) of
> [2026-01-24-diskimage-modernization](../2026-01-24-diskimage-modernization/implementation-plan.md).
> Write protection, DiskManager, save modes and heatmaps from that plan stay separate.

## 1. Verification of the external review

The review (second opinion on `core/src/emulator/io/fdc/diskimage.h`) was checked line by line
against the current sources. Verdict: **correct**.

| Claim | Evidence | Verdict |
|-------|----------|---------|
| Model is already MFM-level (gaps, sync, A1 tokens, IDAM/DAM, CRCs) | `RawSectorBytes` fields `gap0/sync0/f5_token0/address_record/gap1/sync1/f5_token1/data_address_mark/data/data_crc/gap2` (`diskimage.h:80-131`) | true |
| `FullTrack` carries a per-byte clock-mark bitmap | `clockMarksBitmap[782]`, `badBytesBitmap[782]` (`diskimage.h:236-240`) — allocated but **never written or read** by any production code | true, but dormant |
| Dirty tracking split raw-track vs sectors | `_rawTrackDirty`, `_sectorDirtyBitmap` (`diskimage.h:262-264`) — `markRawTrackDirty()` is never called by WD1793 | true, partially wired |
| Interleave table exists | `sectorInterleaveTable[16]`, `applyInterleaveTable(const uint8_t(&)[16])` | true |
| Storage is hard-wired to TR-DOS: `sectors[16]` × 388 B, `data[256]`, `static_assert(sizeof == 6250)` | `diskimage.h:172-210`; `_sectorDirtyBitmap` is `uint16_t`; every accessor masks `sectorNo &= 0x0F` | true |
| FDI / UDI / DSK need variable sector count, variable sector size, variable track length (6208..6464), per-byte clock bitmap | see format docs in this folder | true |
| Direction already exists in `Track : FullTrack` indexes; only storage must be detached from 16×256 | `sectorsOrderedRef[]` / `sectorIDsOrderedRef[]` are already pointer indexes into the raw bytes | true |

Additional findings from the code audit (not in the review):

* Three independent copies of `6250` (`diskimage.h`, `fdc.h:MAX_TRACK_LEN`, `mfm_parser.h`) and four of `16`.
* `Track::reindexFromMFM()` is dead code and relies on a hard-coded `idamOffset - 25` prefix.
* WD1793 WRITE_TRACK casts `&track->sectors[0]` to a flat byte buffer and terminates at exactly 6250; READ_TRACK
  uses `reinterpret_cast<uint8_t*>(Track*)`. Both must go through an explicit `rawData()/rawSize()` pair.
* WD1793 WRITE_SECTOR never recomputes the data CRC (`wd1793.cpp:2312 TODO`), so a sector written by the
  emulated CPU reads back as CRC error until something else recalculates it.
* The HTTP automation API publishes `sizeof(RawSectorBytes)` (388) and the literal 6250 in its wire format.
* `processSearchID()` looks up `getIDForSector(_sectorRegister)` with the *1-based* register value, while
  READ/WRITE SECTOR use `_sectorRegister - 1`.

## 2. Documents in this folder

| Document | Purpose |
|----------|---------|
| [track-model-design.md](track-model-design.md) | The new `RawTrack` / `Sector` / `Track` model, API, invariants, migration table |
| [wd1793-integration.md](wd1793-integration.md) | What changes in the controller (READ/WRITE SECTOR, READ/WRITE TRACK, READ ADDRESS, FM) |
| [test-plan.md](test-plan.md) | Unit and integration test design, regression gates |
| [implementation-plan.md](implementation-plan.md) | Phases, file-by-file task list, order of work, risks |
| [loader-registry.md](loader-registry.md) | Common loader interface, format detection, capability matrix, lossless/lossy save policy |
| [loader-trd.md](loader-trd.md) | Existing TRD loader on the new model (read + write, no regression) |
| [loader-scl.md](loader-scl.md) | Existing SCL loader on the new model (read + write, no regression) |
| [loader-fdi.md](loader-fdi.md) | FDI (UKV) — variable sector size/count, CRC flags, deleted marks |
| [loader-udi.md](loader-udi.md) | UDI (Ultra Disk Image) — raw track + clock bitmap, lossless native format |
| [loader-dsk.md](loader-dsk.md) | CPC/+3 DSK and Extended DSK |
| [loader-td0.md](loader-td0.md) | Teledisk TD0 (normal + advanced compression) |
| [loader-mgt.md](loader-mgt.md) | DISCiPLE/+D MGT and IMG raw images |
| [loader-hobeta.md](loader-hobeta.md) | Hobeta `$` single-file container (inject into TR-DOS disk) |
| [loader-hfe.md](loader-hfe.md) | HxC HFE bit-cell images (v1/v3) |
| [loader-scp.md](loader-scp.md) | SuperCard Pro flux images |
| [flux-decoder.md](flux-decoder.md) | Flux / bit-cell → byte stream + clock bitmap decoder (PLL, MFM/FM) and encoder for save |
| [fm-support.md](fm-support.md) | FM (single density, DDEN=1) across model, controller, loaders and tests |

## 3. Decisions taken (and why)

1. **`RawTrack` becomes a variable-length byte stream** (`std::vector<uint8_t>`) plus a clock-mark bitmap and an
   optional weak-bit bitmap. `6250` becomes the *default* MFM length, not a structural constant.
2. **`RawSectorBytes` is replaced by `Sector`, an index entry + view** (offsets into the stream plus cached pointers).
   It never owns bytes. Sector count, size and position are whatever the stream contains.
3. **The index is always rebuilt by scanning the stream** (`Track::reindex()`), using the clock bitmap to recognise
   real A1/C2 sync marks. The interleave table disappears as a storage concept: interleave is simply the order in
   which sectors appear in the stream. `applyInterleaveTable()` survives as a formatting helper.
4. **Formatting is data-driven** (`TrackFormatSpec`): sector order, size code, gap lengths, fill bytes, encoding,
   track length. The TR-DOS spec reproduces the previous byte layout exactly (388 bytes per sector, 42-byte end gap),
   so TRD/SCL images are bit-identical to what they were.
5. **Compatibility shims are kept where they are cheap** (`getSector(idx)`, `getRawSector(idx)`, `getDataForSector`,
   `getIDForSector`, `writeSectorData`, `formatTrack(cyl, side)`, `reindexSectors()`, `reindexFromIDAM()`,
   `RAW_TRACK_SIZE`, `SECTORS_PER_TRACK`). Everything that depended on `sizeof()` or casting a `Track*` to bytes
   is migrated to `rawData()/rawSize()`.
6. **FM (single density, DDEN=1) is modelled** as `Encoding::FM` with a 3125-byte nominal track, and the controller
   gets a real FM read/write path (Beta-128 port #FF bit 6 selects density); see [fm-support.md](fm-support.md).
7. **WD1793 WRITE_TRACK now records clock marks** for F5→A1 and F6→C2 and calls `markRawTrackDirty()`; WRITE_SECTOR
   recomputes the data CRC. Both are required for an honest UDI round trip.
8. **Loaders are designed, not implemented, in this work item** (except TRD/SCL which are migrated). Each format has
   its own document with a byte-level layout, mapping to the model, save policy and tests.

## 4. Assumptions

* WD1793 sector size code is masked to 2 bits (`N & 3`) as on the real chip (the FD179x ignores bits 2..7).
* The DAM must follow the ID field within 43 bytes (MFM) / 30 bytes (FM), per the FD179x datasheet; sectors whose
  data field lies outside that window are indexed as "ID only".
* A sector whose data field would cross the index (end of stream) is treated as "ID only" (no wrap-around).
* Maximum supported raw track length is 12 500 bytes (2× nominal, room for HD images); maximum sectors per track 255.
* Track/side geometry stays `cylinders ≤ 86`, `sides ≤ 2` (`fdc.h`).

## 5. Decisions on the open questions (answered by the owner, 2026-09-02)

1. **READ ADDRESS and ID search behave as the real chip.** The controller derives the head position from the
   rotation phase it already models (`_time % DISK_ROTATION_PERIOD_TSTATES`), READ ADDRESS returns the *next* ID
   field under the head and copies the cylinder byte into the sector register; Type II commands search from the head
   position with the datasheet timing and give RNF after 5 index pulses. Regression is controlled by keeping the
   data-level results identical on uniform tracks and by adjusting only the tests that encoded the accidental
   `register+1` behaviour. See [wd1793-integration.md](wd1793-integration.md).
2. **TRD is 256-byte-sector only.** `LoaderTRD::writeImage()` refuses any image whose tracks are not
   16 × 256 with sector numbers 1..16 (returns false with a reason). The save path then re-targets to UDI
   (`<name>.udi` next to the original) and notifies through `NC_FDD_DISK_WRITTEN` with `format = "udi"` and
   `retargeted = true`. See [loader-registry.md](loader-registry.md) §4 and [loader-trd.md](loader-trd.md).
3. **HFE and SCP are designed, not deferred**, together with the flux/bit-cell → byte-stream decoder they need:
   [flux-decoder.md](flux-decoder.md), [loader-hfe.md](loader-hfe.md), [loader-scp.md](loader-scp.md).
4. **FM (single density) is planned end to end**, not only as a storage encoding: [fm-support.md](fm-support.md).

## 6. Regression gates

* All existing suites: `DiskImage_Test`, `LoaderTRD_Test`, `LoaderSCL_Test`, `WD1793_*`, `FDD*`, `TRDOSIntegration_test`,
  `TTD_FDD_*`, `TTD_WD1793_*`, `Emulator_Test` must pass (baseline: all pass except two `Scroller_Boot_Test` cases
  that already fail for a missing fixture `testdata/sound/covox/scroller_by_demarche.trd`).
* TRD written from a loaded TRD must be byte-identical to the input (`EyeAche.trd`, `Satisfaction.trd`, `atarin.trd`,
  `zx-format8.trd`).
* SCL → disk → SCL must preserve every file's data and catalog entry (`eyeache2.scl`, `insult.scl`).
* A track formatted through WD1793 WRITE TRACK with the TR-DOS format routine must produce the same 6250 bytes as
  `TrackFormatSpec::trdos()` for the same interleave.
