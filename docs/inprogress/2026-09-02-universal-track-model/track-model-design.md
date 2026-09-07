# Universal Track Model — Design

> Part of [2026-09-02-universal-track-model](README.md)

## 1. What a WD1793 + FDD can physically produce

The model has to hold *anything* the controller can write and read back, not only what TR-DOS writes.

| Property | Physical reality (FD179x datasheet, 300 RPM drive) | Model requirement |
|----------|----------------------------------------------------|-------------------|
| Bit rate | MFM 250 kbit/s (DDEN=0), FM 125 kbit/s (DDEN=1) | `Encoding {MFM, FM}` per track |
| Bytes per revolution | 6250 MFM / 3125 FM nominal; real drives ±2 % (UDI uses 6208..6464) | variable `rawSize()` |
| Byte stream | gaps (4E / FF), sync (00), marks, ID fields, data fields, CRCs, garbage | flat `uint8_t` stream, no structure imposed |
| Special marks | F5→A1 and F6→C2 written with missing clock (MFM); FC index mark; FE ID mark; F8..FB data marks | 1 bit per byte "clock mark" bitmap |
| Sector size | N=0..3 → 128/256/512/1024 (bits 2..7 of N ignored by the chip) | per sector, from its own ID field |
| Sectors per track | anything from 0 up to what fits (≈26 × 128 B); copy protections use duplicates, gaps, ID-only sectors | unbounded list, duplicates allowed |
| Sector numbering | arbitrary 0..255 in the ID field, arbitrary C/H values (may differ from physical) | read from the ID field, never assumed |
| DAM search window | DAM must appear within 43 bytes (MFM) / 30 bytes (FM) after the ID CRC, else RNF | scanner constant |
| CRC errors | ID CRC or data CRC may be wrong on disk, deliberately or not | stored as-is, validity is derived |
| Deleted data | F8 DAM sets status bit 5 on read | `Sector::deleted` derived from DAM byte |
| Weak / fuzzy bits | bytes that read differently each revolution (copy protection) | optional per-byte "weak" bitmap, unused by the controller for now |
| Index position | READ/WRITE TRACK start at the index pulse; stream offset 0 == index | offset 0 is the index |

## 2. Type model

All types stay nested in `class DiskImage` to keep every caller's spelling (`DiskImage::Track`, `DiskImage::AddressMarkRecord`).

```
DiskImage
 ├── enum class Encoding : uint8_t { MFM = 0, FM = 1 }
 ├── struct AddressMarkRecord            // unchanged: packed 7-byte overlay FE C H R N CRC
 ├── struct TrackFormatSpec              // NEW: data-driven format description
 ├── struct Sector                       // NEW: index entry + view (replaces RawSectorBytes)
 ├── struct RawTrack                     // CHANGED: variable-length stream + bitmaps + encoding
 └── struct Track : RawTrack             // CHANGED: sector index, dirty tracking, DiskImage back-pointer
```

`FullTrack` is folded into `RawTrack` (a `using FullTrack = RawTrack;` alias is kept for one release).

### 2.1 `RawTrack` — storage

```cpp
struct RawTrack
{
    // Nominal sizes (compat names kept)
    static constexpr size_t RAW_TRACK_SIZE       = 6250;   // == DEFAULT_TRACK_SIZE_MFM
    static constexpr size_t DEFAULT_TRACK_SIZE_MFM = 6250;
    static constexpr size_t DEFAULT_TRACK_SIZE_FM  = 3125;
    static constexpr size_t MIN_TRACK_SIZE       = 64;
    static constexpr size_t MAX_TRACK_SIZE       = 12500;
    static constexpr size_t SECTORS_PER_TRACK    = 16;     // TR-DOS default, formatting only (compat)
    static constexpr size_t MAX_SECTORS_PER_TRACK = 255;
    static constexpr size_t DAM_SEARCH_WINDOW_MFM = 43;
    static constexpr size_t DAM_SEARCH_WINDOW_FM  = 30;

    Encoding encoding() const;
    size_t   rawSize() const;                 // bytes per revolution for this track
    uint8_t* rawData();  const uint8_t* rawData() const;

    bool clockMark(size_t offset) const;      // byte was written with missing clock (A1/C2)
    void setClockMark(size_t offset, bool on);
    bool hasClockMarks() const;               // any bit set
    const std::vector<uint8_t>& clockBitmap() const;   // (rawSize()+7)/8 bytes, bit i = byte i
    std::vector<uint8_t>&       clockBitmap();

    bool hasWeakBits() const;                 // bitmap empty => no weak bytes
    bool weakByte(size_t offset) const;
    void setWeakByte(size_t offset, bool on);

    void resizeRaw(size_t newSize, Encoding enc, uint8_t fill = 0x4E);   // clears bitmaps and index
    void setRaw(const uint8_t* data, size_t len, Encoding enc);          // copy stream in
    void setClockBitmap(const uint8_t* bitmap, size_t len);
};
```

Invariants:

* `clockBitmap().size() == (rawSize() + 7) / 8` always; weak bitmap is either empty or the same size.
* `rawSize()` is fixed for the life of a formatted track; only `resizeRaw()/setRaw()` (loaders, format) change it.
  Pointers handed out by `Sector` therefore stay valid until the next `resizeRaw()/setRaw()/formatTrack()`.
* `MIN_TRACK_SIZE <= rawSize() <= MAX_TRACK_SIZE`.

### 2.2 `Sector` — index entry + view (replaces `RawSectorBytes`)

```cpp
struct Sector
{
    static constexpr uint32_t NO_OFFSET = 0xFFFFFFFF;

    // Position in the stream
    uint32_t idamOffset = NO_OFFSET;   // offset of the 0xFE byte (A1 A1 A1 precede it)
    uint32_t damOffset  = NO_OFFSET;   // offset of the DAM byte (F8..FB) or NO_OFFSET
    uint32_t dataOffset = NO_OFFSET;   // first data byte
    uint16_t dataSize   = 0;           // 128 << (sizeCode & 3), 0 when no data field

    // Views into RawTrack (rebuilt by reindex(), never owned)
    AddressMarkRecord* id   = nullptr; // overlay at idamOffset
    uint8_t*           data = nullptr; // nullptr when no data field

    // Derived status
    bool hasData      = false;
    bool deleted      = false;         // DAM == 0xF8
    bool idCrcValid   = false;
    bool dataCrcValid = false;
    bool dirty        = false;         // set by writeSectorData / WD1793 WRITE SECTOR

    uint8_t cylinder() const; uint8_t head() const; uint8_t number() const; uint8_t sizeCode() const;
    uint8_t  dataAddressMark() const;  void setDataAddressMark(uint8_t dam);
    uint16_t dataCRC() const;          void setDataCRC(uint16_t crc);       // same byte order as before
    void recalculateDataCRC();         bool isDataCRCValid();               // CRC over DAM + data
    void recalculateIDCRC();           bool isIDCRCValid();
};
```

`Sector` values live in `Track::_sectors` in *physical* (stream) order. `getRawSector(i)` returns element `i`;
`getSector(idx)` / `findSector(number)` search by ID number.

### 2.3 `TrackFormatSpec` — how to lay a track out

```cpp
struct TrackFormatSpec
{
    Encoding encoding      = Encoding::MFM;
    size_t   trackLength   = RawTrack::DEFAULT_TRACK_SIZE_MFM;
    uint8_t  sizeCode      = 1;              // applies to all sectors unless sectorSizeCodes is set
    std::vector<uint8_t> sectorNumbers;      // physical order == interleave; e.g. {1,9,2,10,...}
    std::vector<uint8_t> sectorSizeCodes;    // optional per-sector override (mixed sizes)
    std::vector<uint8_t> sectorCylinders;    // optional per-sector C override (copy protection)
    std::vector<uint8_t> sectorHeads;        // optional per-sector H override
    bool     indexMark     = false;          // write C2 C2 C2 FC (+ gap) at the index
    uint8_t  gapIndex      = 0;              // 4E bytes before the index mark / first sector when indexMark
    uint8_t  gapPreID      = 10;             // 4E before each ID sync   (old gap0)
    uint8_t  syncLength    = 12;             // 00 before A1 A1 A1       (old sync0/sync1)
    uint8_t  gapPostID     = 22;             // 4E between ID CRC and data sync (old gap1)
    uint8_t  gapPostData   = 60;             // 4E after data CRC        (old gap2)
    uint8_t  gapFill       = 0x4E;           // FM formats use 0xFF
    uint8_t  dataFill      = 0x00;
    uint8_t  dataMark      = 0xFB;

    static TrackFormatSpec trdos(const uint8_t* order = nullptr, size_t count = 16); // exact legacy layout
    static TrackFormatSpec ibm(uint8_t sectors, uint8_t sizeCode, uint8_t firstSector = 1, uint8_t gap3 = 0x2A);
    static TrackFormatSpec plus3();         // 9 × 512, gap3 0x2A, filler E5, +3DOS
    static TrackFormatSpec plusD();         // 10 × 512
    size_t bytesPerSector(uint8_t sizeCode) const;   // gaps + sync + marks + id + data + crcs
    bool   fits() const;                             // sum(bytesPerSector) + gapIndex ≤ trackLength
};
```

`trdos()` yields, per sector: 10×4E, 12×00, A1 A1 A1, FE C H R N CRC, 22×4E, 12×00, A1 A1 A1, FB, 256 B, CRC,
60×4E = 388 bytes; 16 sectors = 6208; 42 × 4E to 6250. This is byte-identical to the old `RawTrack`.

### 2.4 `Track` — index and change tracking

```cpp
struct Track : RawTrack
{
    // Index
    size_t   sectorCount() const;
    Sector*  getRawSector(size_t physicalIndex);          // stream order
    Sector*  findSector(uint8_t number);                  // first in stream order, nullptr if absent
    Sector*  findSector(uint8_t number, size_t fromOffset); // next after offset, wrapping (rotational search)
    Sector*  findSector(uint8_t cylinder, int side, uint8_t number, size_t fromOffset); // WD1793 Type II match
    const std::vector<Sector>& sectors() const;
    size_t   indexMarkOffset() const;                     // NO_OFFSET when none
    void     reindex();                                   // rescan stream, rebuild _sectors

    // Compatibility (0-based logical index == ID number - 1)
    Sector*            getSector(uint8_t idx)          { return findSector(uint8_t(idx + 1)); }
    AddressMarkRecord* getIDForSector(uint8_t idx);
    uint8_t*           getDataForSector(uint8_t idx);
    void               reindexSectors()  { reindex(); }   // deprecated alias
    void               reindexFromIDAM() { reindex(); }   // deprecated alias

    // Formatting
    void formatTrack(uint8_t cylinder, uint8_t side);                             // TR-DOS default
    void formatTrack(uint8_t cylinder, uint8_t side, const TrackFormatSpec& spec);
    void applyInterleaveTable(const uint8_t* order, size_t count);                // reformat blank TR-DOS track
    template<size_t N> void applyInterleaveTable(const uint8_t (&order)[N]);
    void reset();                                                                 // blank TR-DOS track for (0,0)

    // Writes with change detection
    void writeSectorData(uint8_t idx, const uint8_t* src, size_t len);            // clamps to dataSize, recomputes CRC
    void markSectorDirtyIfChanged(uint8_t idx, const uint8_t* newData, size_t len);

    // Dirty tracking (unchanged semantics)
    bool isDirty() const; bool isRawTrackDirty() const;
    bool isSectorDirty(uint8_t idx) const; bool hasAnySectorDirty() const;
    void markClean();
protected:
    void markDirty(); void markRawTrackDirty();   // friend class WD1793
};
```

Removed (dead, no production caller): `getRawTrackData(cyl, side)`, `getCRCForSector`, `setCRCForSector`,
`calculateDataCRCForSector`, `getCRCForSectorAddress`, `reindexFromMFM`, `sectorsOrderedRef`,
`sectorIDsOrderedRef`, `sectorInterleaveTable`, `DEFAULT_INTERLEAVE`, `endGap`, `clockMarksBitmap[]`,
`badBytesBitmap[]`, `SectorSizeEnum` (kept as `enum SectorSizeEnum` for the default in `AddressMarkRecord`).

## 3. The scanner (`Track::reindex()`)

```
offset = 0; sectors.clear(); indexMark = NO_OFFSET
while offset + 4 <= rawSize:
    if isSync(offset):                           # three A1 bytes with clock marks (or plain A1 A1 A1 when the
        mark = raw[offset+3]                     #  track has no clock information at all)
        if mark == 0xFC and isIndexSync(offset): indexMark = offset+3 (C2 C2 C2 FC)
        if mark == 0xFE:
            s.idamOffset = offset+3; s.id = overlay; s.idCrcValid = crc(FE..N) == stored
            s.dataSize = 128 << (N & 3)
            look for next sync + DAM (F8..FB) starting at idamOffset+7 within DAM_SEARCH_WINDOW
            if found and dataOffset + dataSize + 2 <= rawSize:
                s.hasData = true; s.deleted = (DAM == F8); s.dataCrcValid = crc(DAM..data) == stored
                offset = dataOffset + dataSize + 2  (skip data when the track has no clock bitmap)
            sectors.push_back(s)   (cap at MAX_SECTORS_PER_TRACK)
    offset += 1
```

Rules:

* With clock information the scan never skips bytes: A1 bytes inside a data field without a clock mark are not sync,
  exactly like the real controller. Without clock information (hand-built buffers in tests) the scanner skips over
  data fields so that random `A1 A1 A1 FE` inside data cannot create phantom sectors.
* The scanner does not sort, merge or de-duplicate. Two sectors with the same number are both listed;
  `findSector(number)` returns the first in stream order, `findSector(number, fromOffset)` the next one after the
  current head position.
* `reindex()` is called by `formatTrack`, `setRaw`, `resizeRaw`, loaders after filling a track, and by WD1793 after
  WRITE TRACK. It is `O(rawSize)` and allocation-free after the first call.

## 4. Where each caller lands

| Old API / idiom | New API | Callers |
|-----------------|---------|---------|
| `RawSectorBytes* s = t->getSector(i)`; `s->data` | `Sector* s = t->getSector(i)`; `s->data` (pointer) | loaders, tests, webapi, cli, lua, python |
| `s->address_record.x` | `s->id->x` | cli, webapi, tests |
| `s->data_address_mark` | `s->dataAddressMark()` / `s->deleted` | wd1793.cpp, webapi, tests |
| `s->data_crc` | `s->dataCRC()` / `setDataCRC()` | cli, webapi, tests |
| `sizeof(s->data)` | `s->dataSize` | diskimage.h |
| `sizeof(RawSectorBytes)` in wire format | `s->dataSize` + explicit `raw_size = rawSize()` | webapi |
| `reinterpret_cast<uint8_t*>(track)` | `track->rawData()` | wd1793.cpp, webapi |
| `&track->sectors[0]` as byte buffer | `track->rawData()` | wd1793.cpp |
| `track->sectors[i]` | `*track->getRawSector(i)` | tests |
| `RawTrack::RAW_TRACK_SIZE` as loop bound | `track->rawSize()` | wd1793.cpp, webapi, tests |
| `SECTORS_PER_TRACK` as loop bound | `track->sectorCount()` | wd1793.cpp, cli, webapi |
| `track->sectorsOrderedRef[i]` | `track->getSector(i)` | tests |
| `applyInterleaveTable(pattern[16])` | unchanged spelling (template overload) | loader_trd.cpp |
| `reindexFromIDAM()` / `reindexSectors()` | `reindex()` (aliases kept) | wd1793.cpp, tests |
| `getRawTrackData(cyl, side)` | `rawData()` | wd1793.cpp |
| `FullTrack` | `RawTrack` (alias kept) | diskimage_test.cpp |

## 5. Memory and performance

* Per track: `rawSize` + 2 bitmaps (≈ 6250 + 782 + 0) + `sectorCount × sizeof(Sector)` (≈ 16 × 40) ≈ 7.7 KB,
  versus 7.9 KB before. A DS 80 disk stays ≈ 1.2 MB.
* `std::vector<Track>` moves are cheap (vector buffers move, sector pointers stay valid).
* `Track` is non-copyable (as before). A `clone()` helper is provided for tests/tools; it deep-copies and reindexes.
* Hot path (WD1793 byte read/write) is unchanged: raw pointer increment over contiguous memory.

## 6. Constants consolidation

`fdc.h` becomes the single owner of nominal sizes:

```cpp
static constexpr size_t MAX_TRACK_LEN = 6250;           // nominal MFM (already there)
static constexpr size_t NOMINAL_TRACK_LEN_FM = 3125;
```

`DiskImage::RawTrack::RAW_TRACK_SIZE` and `MFM::RAW_TRACK_SIZE` reference it. `MFM::SECTORS_PER_TRACK` and the
`sectors[16]` arrays in `mfm_parser.h` become `std::vector` results; the TR-DOS specific validation
(`isCompliant() == 16 valid sectors`, interleave check) moves behind an explicit `expectedSectors` parameter that
defaults to 16 so the existing WRITE TRACK tests keep their meaning.
