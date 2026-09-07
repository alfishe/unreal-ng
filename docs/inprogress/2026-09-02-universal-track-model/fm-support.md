# FM (single density, DDEN = 1) support — plan

> Part of [2026-09-02-universal-track-model](README.md). **Status: implemented for the controller and the model**
> (2026-09-04): density input, byte cell, mismatch RNF, WRITE TRACK FM branch, FM formatter / scanner, UDI type 1.
> Open: F7 handling in FM follows the "accept F7 in both modes" decision; TD0/EDSK/HFE/SCP FM mapping lands with those loaders.

## 1. What FM is on a WD1793 + Beta-128

* Bit rate 125 kbit/s (byte cell 64 µs), 3125 bytes per 200 ms revolution nominal (real: ≈3100..3230).
* No A1/C2 sync bytes. Marks are ordinary data bytes written with a *non-standard clock pattern*:

| Byte written by host | Written on disk | Clock | Meaning |
|---------------------|-----------------|-------|---------|
| 00–F4, FD, FF | as is | FF | data / gap (`FF` gap fill, `00` sync) |
| F5, F6, F7 | **not allowed** (the chip writes them literally with clock FF; no CRC generation) | FF | — |
| F8–FB | as is, CRC preset | C7 | Data address marks (F8 deleted, FB normal) |
| FC | as is | D7 | Index address mark |
| FE | as is, CRC preset | C7 | ID address mark |

* CRC generation in FM: the CRC is generated automatically after the ID/data field — the FD179x writes the
  two CRC bytes when the host writes **F7**? No: in FM F7 is not allowed; the datasheet states the CRC is *generated*
  by writing F7 only in MFM. In FM the 1791/1793 generates CRC when it sees F7 as well ("F7 — generate 2 CRC bytes"
  is listed in both columns of the 1793 datasheet table in later revisions). Decision: accept F7 in both modes
  (the Beta-128 ROM never formats FM, so this only matters for foreign tracks) and document the choice.
* Sync detection on read: the AM detector matches the clock pattern C7/D7 together with the data byte, after at
  least 6 bytes of 00 (FM: 6, MFM: 12).
* Standard layout at 125 kbit/s (IBM 3740 style on 5.25"/3.5" media, 16 × 128; the 8" original carries 26 × 128
  at 250 kbit/s): gap4a 40×FF, 6×00, FC(D7), gap1 26×FF, per sector: 6×00, FE C H R N CRC, gap2 11×FF, 6×00,
  FB + 128 B + CRC, gap3 27×FF; gap4b to index.
* Beta-128: system port #FF bit 6 = 1 selects FM (`BETA_CMD_DENSITY` is already decoded into `dden_in`).

## 2. Model (already covered by the universal track model)

* `Encoding::FM` per track; `rawSize()` ≈ 3125; `gapFill = 0xFF`.
* Clock bitmap bit = "this byte was written with a mark clock" (C7 or D7). Which clock it was is implied by the
  data byte (FC → D7, else C7), so one bit per byte is sufficient — same bit meaning as UDI type 1.
* `TrackFormatSpec::ibm3740()` preset: 16 × 128, FM, 3125 bytes (implemented); `syncLength` 6 for FM.
* `Track::reindex()` in FM mode looks for `clockMark(i) && raw[i] == FE` (ID) / `F8..FB` (data) / `FC` (index)
  instead of the A1 triple; DAM search window 30 bytes.

## 3. Controller

| Area | Change |
|------|--------|
| Density input | `_dden = !(betaPortFF & BETA_CMD_DENSITY)`; stored in `WD1793State::dden_in` (exists) and in the TTD blob (1 byte, format bump) |
| Byte cell | `tstatesPerByte = Z80_FREQUENCY / (rawSize() × FDD_RPS)` per command (≈228 T for 3125 bytes); DRQ lost-data window doubles (`MAX_DRQ_SERVICE_TIME_FM_*` exist in `fdc.h`) |
| Track/encoding mismatch | Reading an MFM track with DDEN=1 (or vice versa) must fail like hardware: no address marks are recognised → RNF after 5 index pulses (Type II), READ TRACK returns the stream re-interpreted (garbage) — implemented by `reindex()`-time encoding check: `findSector` returns nothing when `track.encoding() != controllerEncoding` |
| WRITE TRACK | FM branch of the control-byte switch: F8–FB/FE set clock mark + CRC preset, FC sets clock mark, F5/F6 written literally, F7 generates CRC (decision above); track length from `rawSize()` which the format command sets to 3125 when the track is re-formatted in FM (`Track::formatBlank(Encoding::FM)` before the first byte) |
| READ TRACK | unchanged (stream copy), 3125 bytes |
| READ/WRITE SECTOR, READ ADDRESS | unchanged logic, sectors come from the FM index |
| Timing constants | `WD93_TSTATES_PER_FDC_BYTE_FM`, Type I step/settle unchanged |

## 4. Loaders

| Format | FM handling |
|--------|-------------|
| UDI | track type 1 → `Encoding::FM`; type 2 (mixed) → MFM with warning (no known ZX images use it) |
| TD0 | header data-rate bit 7 or track head bit 7 → FM |
| EDSK | recording mode 1 → FM |
| HFE | track encoding field 2 (ISOIBM_FM) → FM; bit-cell decoding in the flux decoder |
| SCP | decoder detects FM from the cell-length histogram (2 µs vs 4 µs base cells at 250 kbps sample rate) |
| TRD/SCL/FDI/MGT/Hobeta | MFM only; FM tracks make `writeImage` refuse (strict) or warn (FDI) |

## 5. Tests

| # | Test |
|---|------|
| F1 | `Format_Ibm3740_Fm` — 16 × 128 fits in 3125 bytes, reindex finds 16, every FE/FB/FC has a clock mark (implemented as `DiskImage_Test.TrackLength_FmNominal`) |
| F2 | `Reindex_Fm_IgnoresA1` — an A1 A1 A1 FE sequence without clock marks in an FM track is not a sector (implemented inside `TrackLength_FmNominal`) |
| F3 | `Wd1793_ReadSector_Fm` — DDEN=1, track FM: READ SECTOR 7 returns 128 bytes; byte cell 64 µs (assert DRQ spacing ±1 T) |
| F4 | `Wd1793_DensityMismatch_Rnf` — DDEN=0 on an FM track → RNF after 5 index pulses (time-bounded) |
| F5 | `Wd1793_WriteTrack_Fm` — CPU-side IBM 3740 format loop through WRITE TRACK; result equals `ibm3740()` byte-for-byte incl. clock bitmap |
| F6 | `Udi_Fm_RoundTrip` — type-1 track → model → UDI, byte-exact |
| F7 | `Ttd_Dden_Serialized` — density survives save/restore |

## 6. Order of work

1. Model + reindex FM mode + preset + F1/F2 — **done** in the core implementation (FM CRC preset 0xFFFF via `CRCHelper::crcWD1793FM`, FM mark detection by clock bit).
2. Controller density input + byte cell + mismatch RNF + F3/F4 — **done** (`WD1793_UniversalTrack_Test.ReadSector_Fm`, `DensityMismatch_ReturnsRNF`). F7 (TTD serialisation of the density) is implicit: `_beta128Register` is already part of the WD1793 TTD blob.
3. WRITE TRACK FM branch + F5 — **done** (`WriteTrack_Fm_MatchesFormatter`: CPU-side single density format equals `ibm3740()` byte for byte, clock bitmap included).
4. Loaders as they arrive (UDI first) + F6.
