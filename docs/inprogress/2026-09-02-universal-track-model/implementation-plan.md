# Implementation plan — universal track model

> Part of [2026-09-02-universal-track-model](README.md)

## Phase 1 — Core model (this work item) — **done 2026-09-02**

All steps below are implemented; new test files: `core/tests/emulator/io/fdc/mfm_parser_test.cpp`,
`core/tests/emulator/io/fdc/wd1793_universal_track_test.cpp`, `core/tests/loaders/loader_roundtrip_test.cpp`.
A latent bug was fixed on the way: WRITE TRACK emitted the two CRC bytes in swapped order (and `mfm_parser.h`
compensated), so a track formatted by the emulated CPU disagreed with a loader-formatted one; both now use the
on-disk order (true CRC high byte first).

| Step | Files | Task |
|------|-------|------|
| 1.1 | `core/src/emulator/io/fdc/fdc.h` | add `NOMINAL_TRACK_LEN_FM = 3125`; keep `MAX_TRACK_LEN = 6250` as the nominal MFM length |
| 1.2 | `core/src/emulator/io/fdc/diskimage.h/.cpp` | `Encoding`, `TrackFormatSpec`, `Sector`, variable `RawTrack`, `Track::reindex()`, format engine, compat shims, remove dead API |
| 1.3 | `core/src/emulator/io/fdc/mfm_parser.h` | size-agnostic parsing, `std::vector<SectorParseResult>`, `expectedSectors` parameter (default 16) |
| 1.4 | `core/src/loaders/disk/loader_trd.cpp` | `format()` uses `TrackFormatSpec::trdos(pattern)`; `transferSectorData` / `writeImage` via `Sector::data` |
| 1.5 | `core/src/loaders/disk/loader_scl.cpp` | `RawSectorBytes*` → `Sector*`; no behavioural change |
| 1.6 | `core/src/emulator/io/fdc/wd1793.cpp` | `rawData()/rawSize()` for READ/WRITE TRACK; clock marks on F5/F6; `reindex()` + `markRawTrackDirty()`; `Sector*` in `processReadCRC`; `_sectorSize` from matched sector; multi-sector bound by `findSector`; data CRC on WRITE SECTOR completion |
| 1.7 | `core/automation/webapi/src/api/tape_disk_api.cpp`, `core/automation/cli/src/commands/cli-processor-disk.cpp`, `core/automation/lua/src/emulator/lua_emulator.h`, `core/automation/python/src/emulator/python_emulator.h` | `Sector` API; `raw_size = rawSize()`; loops over `sectorCount()`; `sector_size` from `dataSize` |
| 1.8 | `core/tests/emulator/io/fdc/diskimage_test.cpp` | rewrite per test-plan §1 |
| 1.9 | `core/tests/emulator/io/fdc/wd1793_*_test.cpp`, `core/tests/loaders/*` | migrate `sectors[]`, `sectorsOrderedRef[]`, `RawSectorBytes`, `data_crc`, `data_address_mark` |
| 1.10 | `core/tests/emulator/io/fdc/mfm_parser_test.cpp` | new |
| 1.11 | build + run: `DiskImage*`, `LoaderTRD*`, `LoaderSCL*`, `WD1793*`, `FDD*`, `TRDOS*`, `TTD_FDD*`, `TTD_WD1793*`, `Emulator*` | regression gate |

Order: 1.1 → 1.2 → 1.3 → 1.4/1.5 → 1.6 → 1.7 → 1.8/1.9/1.10 → 1.11. The tree compiles only after 1.9, so
the work is done on one branch and verified as a whole.

## Phase 2 — Controller fidelity (follow-up)

* Rotational position in `FDD` (`headByteOffset()`), position-aware `processSearchID()`, READ ADDRESS returns the
  next ID under the head, RNF after 5 revolutions.
* Side/cylinder compare flags of Type II commands.
* FM (DDEN) read/write path, byte-cell timing from `rawSize()`.
* Lost-data and overrun behaviour with variable sector sizes.

## Phase 3 — Loaders (each per its own document) — **done 2026-09-04**

All loaders implemented: UDI, FDI, DSK/EDSK, Hobeta, TD0, MGT/IMG. Each has fixtures in `testdata/loaders/` and
tests in `core/tests/loaders/`. See `loader-registry.md` for the shared interface (detection, capabilities, save policy).

## Phase 4 — Tooling and docs — **done 2026-09-04**

* Python generators/validators in `core/tests/emulator/io/fdc/tools/` accept geometry options (already present).
* `docs/file-formats/disk-images/` gained one reference page per format (README, trd, scl, udi, fdi, dsk, td0, mgt, hobeta).
* `docs/features/automation.md` updated with full disk CLI commands and WebAPI endpoints including `raw_size`, `encoding`, `clock_bitmap` fields.

## Risks and mitigations

| Risk | Mitigation |
|------|------------|
| Silent TRD layout drift | U2 byte-identical layout test + L1 round-trip on four real images |
| WD1793 tests depend on `getSector(i)` returning the sector at physical index i | `formatTrack()` now writes real sector numbers 1..16 (1:1) so both views agree unless an interleave is requested |
| Test buffers built without clock bitmap | scanner fallback (U15) |
| Pointer invalidation after `resizeRaw` | documented invariant; WD1793 re-fetches the track pointer per FSM step already |
| Automation bindings compile only with `ENABLE_*_AUTOMATION` | build with the default `build/` configuration (lua, cli, webapi ON); python binding edited blind but mirrors lua |
| `_sectorDirtyBitmap` consumers in TTD serialisers | grep confirms none; dirty flags are not serialised |
