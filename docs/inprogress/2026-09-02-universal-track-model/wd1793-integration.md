# WD1793 integration with the universal track model

> Part of [2026-09-02-universal-track-model](README.md)
> **Status (2026-09-02):** implemented in `core/src/emulator/io/fdc/wd1793.cpp` except where marked *follow-up*.

## 0. What changed in the controller (implemented)

* All Type II / III commands operate on the track physically under the head (`FDD::getTrack()`), no longer on the
  track selected by the track register; the register is compared with the C byte of the ID fields (datasheet).
  Tests that relied on the register alone now also position the drive (`setTrack`).
* READ / WRITE SECTOR locate the sector with `locateSectorForType2()`: C must equal the track register, H must match
  when the side-compare flag (bit 1) is set, R must equal the sector register; the search starts at the head position
  (`headByteOffset()`), so duplicates resolve like on hardware. A bad ID CRC yields CRC ERROR + RNF; an ID without a
  data field yields RNF. `_sectorSize` and the byte count come from the matched sector (128..1024).
* Multi-sector (m flag) always queues the next sector number; the first number that is not on the track ends the
  command with RNF (datasheet), instead of the old hard-coded `< 15` bound. Verified by
  `WD1793_UniversalTrack_Test.ReadSector_Multi_StopsAtFirstMissingNumber` (9 × 512 track, read 7..9 then RNF).
* WRITE SECTOR writes the data mark selected by a0 (F8 / FB), regenerates the data CRC and marks the sector dirty.
* READ TRACK / WRITE TRACK use `rawData()` / `rawSize()`; WRITE TRACK records clock marks for F5 → A1 and F6 → C2,
  writes CRC bytes in on-disk order (true high byte first — the previous code wrote them swapped, and the MFM parser
  compensated), rebuilds the index with `reindex()` and calls `markRawTrackDirty()`.
* READ ADDRESS returns the next ID field under the head (`Track::nextSector(headByteOffset())`), charges the byte
  cells up to that ID field as delay, transfers C H R N CRC1 CRC2, copies the cylinder into the sector register and
  sets CRC ERROR when the ID CRC is bad; an unformatted track ends with RNF after 5 revolutions.
* Rotational latency is charged for Type II commands as well (2026-09-04): `rotationalDelayToData()` = bytes from
  the head position to the data field of the matched sector × byte cell, applied before the first DRQ of READ /
  WRITE SECTOR and before each multi-sector continuation; Record Not Found terminates only after 4 index pulses
  (`WD93_REVOLUTIONS_LIMIT_FOR_TYPE2_INDEX_MARK_SEARCH`). `FSM_CMD_Read_Sector*` / `Write_Sector*` timing
  expectations now include `dataOffset` byte cells; `WD1793_UniversalTrack_Test.ReadSector_ChargesRotationalLatency`
  and `ReadSector_NotFound_TakesSearchRevolutions` pin the behaviour.
* Density (2026-09-04): Beta-128 port #FF bit 6 selects FM (`controllerEncoding()`); a track recorded in the other
  density yields no ID field (RNF for Type II, READ ADDRESS). The byte cell comes from the track in use
  (`byteCellTStates()`: 112 T for 6250-byte MFM, 224 T for 3125-byte FM). WRITE TRACK in FM re-formats the track
  as a 3125-byte FM stream, writes F5/F6 literally, clock-marks FC/FE/F8–FB and presets the CRC to 0xFFFF at the
  mark (`crcWD1793FM`). Tests: `ReadSector_Fm`, `DensityMismatch_ReturnsRNF`, `WriteTrack_Fm_MatchesFormatter`.

## 1. Access patterns today and after

| Command | Today | After |
|---------|-------|-------|
| READ SECTOR | `getDataForSector(reg-1)` → pointer walk of `_sectorSize` bytes; `_sectorSize` from `processSearchID` | `Sector* s = track->findSector(trackReg, sideCompare ? side : -1, reg, headByteOffset())`; search charges the byte cells between the head and the ID field; `_rawDataBuffer = s->data; _bytesToRead = s->dataSize`; RNF when no matching ID within 5 index pulses or no DAM within 43/30 bytes |
| WRITE SECTOR | same pointer walk; **data CRC never recomputed** | same walk; on completion `s->recalculateDataCRC(); s->dirty = true; track->markDirty()`; DAM rewritten to F8 when command bit a0 = 1 |
| READ ADDRESS | `getIDForSector(_sectorRegister)` (1-based, i.e. sector reg+1) → 6 bytes after FE | `track->nextSector(headByteOffset())` — the next ID field physically under the head; the 6 bytes C H R N CRC1 CRC2 are transferred, then the cylinder byte is copied into the sector register (datasheet). RNF after 5 index pulses when the track has no ID field; ID CRC error sets CRC ERR and still terminates normally |
| READ TRACK | `reinterpret_cast<uint8_t*>(Track*)`, exactly 6250 | `track->rawData()`, `track->rawSize()` |
| WRITE TRACK | `&track->sectors[0]`, stops at 6250, `reindexFromIDAM()`; clock marks lost | `track->rawData()`, stops at `rawSize()`; F5/F6 set `track->setClockMark(index, true)`, all other bytes clear it; on completion `reindex()`, `markRawTrackDirty()` |
| Multi-sector (m flag) | `_sectorRegister < SECTORS_PER_TRACK - 1` | continue while `findSector(_sectorRegister + 1)` exists; terminates with RNF on the first missing number, as the chip does |
| Sector size | `128 << (N & 3)` from the ID (only place already honest) | unchanged, but taken from the `Sector` actually matched |
| Side compare (C flag) / cylinder compare | not implemented | `findSector(cyl, side, number, from)` honours both; cylinder mismatch → RNF, ID CRC error → CRC error + RNF |

## 2. Byte-cell timing

`WD93_TSTATES_PER_FDC_BYTE = Z80_FREQUENCY / (MAX_TRACK_LEN * FDD_RPS)` stays nominal (32 µs MFM). For an FM track
the byte cell doubles: `tstatesPerByte = Z80_FREQUENCY / (track->rawSize() * FDD_RPS)` computed once per command
from the track in use, so variable-length and FM tracks keep the 200 ms revolution. READ/WRITE TRACK wait for the
index as before; the index period is unchanged (rotation is a property of the drive, not the track).

## 3. Rotational position (in this work item — decided: behave like the real chip)

The controller already models the rotation phase for S_WAIT_INDEX (`_time % DISK_ROTATION_PERIOD_TSTATES`).
The same phase gives the head position on the track:

```cpp
size_t WD1793::headByteOffset(const DiskImage::Track& t) const
{
    const size_t phase = _time % DISK_ROTATION_PERIOD_TSTATES;          // t-states since last index
    return (size_t)((uint64_t)phase * t.rawSize() / DISK_ROTATION_PERIOD_TSTATES);
}
```

* `processSearchID()` (Type II and READ ADDRESS) calls `Track::nextSector(fromOffset)` / `findSector(...)`
  with the head offset. The distance in bytes from the head to the ID field (wrapping) is charged as
  `bytes × tstatesPerByte` before the ID is "found"; a full miss costs 5 index pulses and ends with RNF.
* READ ADDRESS returns the next ID physically under the head; consecutive READ ADDRESS calls therefore walk the
  track in physical (interleave) order, which is how TR-DOS and copy-protections probe the layout.
* On uniform TR-DOS tracks the sector that is found is the same one as before (numbers are unique), so data-level
  results do not change. Timing changes only by the added rotational latency; the tests that assumed an immediate
  ID match are adjusted to allow up to one revolution (see test-plan W15–W18).
* Regression control: the delay is applied through the existing `transitionFSMWithDelay` mechanism, the same one
  used for byte cells, so TTD serialisation and the FSM contract are untouched.

## 4. Status register mapping from `Sector`

| `Sector` state | Type II / III status |
|----------------|----------------------|
| not found / no data field | `WDS_NOTFOUND` (RNF) |
| `!idCrcValid` | `WDS_CRCERR` + `WDS_NOTFOUND` |
| `!dataCrcValid` after data read | `WDS_CRCERR`, command terminates (even multi-sector) |
| `deleted` | `WDS_RECORDTYPE` (bit 5) |
| write on write-protected drive | `WDS_WRITEPROTECT` (unchanged) |

## 5. FM (DDEN)

Planned in full in [fm-support.md](fm-support.md): density comes from Beta-128 port #FF bit 6 (`BETA_CMD_DENSITY`,
already decoded into `dden_in`), the byte cell doubles to 64 µs, format control bytes follow the FM column of the
datasheet table (F8–FB/FE with clock C7, FC with clock D7, FF with clock FF; F5/F6/F7 are not allowed), and the
scanner recognises FM marks by their clock pattern rather than by A1 sync.

## 6. Test hooks

`WD1793CUT` keeps `_rawDataBuffer`, `_bytesToWrite`, `_rawDataBufferIndex`; tests that allocate their own
`new uint8_t[RAW_TRACK_SIZE]` keep working because the format loop is bounded by `_bytesToWrite`, not by the constant.
