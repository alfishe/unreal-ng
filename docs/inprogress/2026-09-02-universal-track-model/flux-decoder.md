# Flux / bit-cell decoder and encoder — comprehensive design

> Part of [2026-09-02-universal-track-model](README.md). Needed by [loader-hfe.md](loader-hfe.md) and
> [loader-scp.md](loader-scp.md). Status: **implementing**.

## 1. Overview and Pipeline

```
Input sources:
  SCP flux transitions (ns intervals)  ──► FluxPll ──► bit cells ──┐
  HFE bit cells (packed bit stream)    ────────────────────────────┴──► MfmDecoder/FmDecoder ──► bytes + clock bitmap

Model integration:
  bytes + clock bitmap ──► Track::setRaw() + Track::setClockBitmap() ──► Track::reindex()

Save path:
  Track bytes + clock bitmap ──► MfmEncoder/FmEncoder ──► bit cells ──► HFE (v1/v3) / SCP (single revolution)
```

## 2. FluxPll — Phase-Locked Loop for flux-to-bitcell conversion

Location: `core/src/emulator/io/fdc/flux/flux_pll.h/.cpp`

### 2.1 Theory of operation

A digital PLL converts raw flux transition timings (nanosecond intervals between magnetic reversals) into 
a stream of bit cells. Each bit cell represents one clock period where either a transition occurred (1) 
or did not occur (0).

**Nominal cell widths:**
- MFM (double density, 250 kbps): 2 µs = 2000 ns
- FM (single density, 125 kbps): 4 µs = 4000 ns

**Tolerance:** Real drives have ±10-20% timing variation due to motor speed variations, media defects,
and head alignment. The PLL must track these variations while maintaining lock.

### 2.2 Algorithm

```cpp
// State
uint32_t nominalCellNs;    // 2000 (MFM) or 4000 (FM)
uint32_t currentCellNs;    // Adjusted cell width
int32_t phaseErrorNs;      // Accumulated phase error
int gainPercent;           // PLL adjustment rate (default 5%)

// Process one flux interval
void processInterval(uint32_t intervalNs) {
    int cellsInInterval = round(intervalNs / currentCellNs);
    cellsInInterval = clamp(cellsInInterval, 1, 8);  // Sanity limit
    
    // Emit cells: first cell has the transition (1), rest are gaps (0)
    for (int i = 0; i < cellsInInterval; i++) {
        bitCells.push_back(i == 0 ? 1 : 0);
    }
    
    // Phase error: actual interval vs expected
    int32_t expectedNs = cellsInInterval * currentCellNs;
    int32_t errorNs = (int32_t)intervalNs - expectedNs;
    
    // Adjust cell width by gain% of the error
    int32_t adjustment = (errorNs * gainPercent) / 100;
    currentCellNs = clamp(currentCellNs + adjustment, 
                          nominalCellNs * 75 / 100,   // Min: -25%
                          nominalCellNs * 125 / 100); // Max: +25%
}
```

### 2.3 Automatic mode detection

Analyse a histogram of flux intervals to determine MFM vs FM:
- MFM: clusters at 2 µs, 3 µs, 4 µs (1T, 1.5T, 2T cells)
- FM: clusters at 4 µs, 8 µs (clock-only, clock+data)

```cpp
Mode detectMode(const uint32_t* intervals, size_t count) {
    // Build histogram in 500 ns buckets
    int histogram[20] = {0};  // 0-10 µs range
    for (size_t i = 0; i < count; i++) {
        int bucket = intervals[i] / 500;
        if (bucket < 20) histogram[bucket]++;
    }
    
    // MFM: strong peak at bucket 4 (2000 ns)
    // FM: strong peak at bucket 8 (4000 ns)
    int mfmScore = histogram[3] + histogram[4] + histogram[5];  // 1.5-2.5 µs
    int fmScore = histogram[7] + histogram[8] + histogram[9];   // 3.5-4.5 µs
    
    return (fmScore > mfmScore * 2) ? Mode::FM : Mode::MFM;
}
```

### 2.4 Multi-revolution weak bit detection (SCP only)

When multiple revolutions are available, compare decoded bytes across revolutions:
- Bytes that match: normal data
- Bytes that differ: weak bits (mark in weak bitmap, store majority value)

## 3. MfmDecoder — bit cells to bytes + clock bitmap

Location: `core/src/emulator/io/fdc/flux/mfm_decoder.h/.cpp`

### 3.1 MFM encoding primer

MFM encodes data bits with clock bits inserted between them:

```
Data:    D7 D6 D5 D4 D3 D2 D1 D0
MFM:     C7 D7 C6 D6 C5 D5 C4 D4 C3 D3 C2 D2 C1 D1 C0 D0

Clock rule: Cn = 1 if (Dn == 0 && D(n+1) == 0), else 0
            (clock inserted when two consecutive zeros)
```

Exception: Address marks use deliberately wrong clocks for detection:
- A1 sync (0xA1 with clock C2,C5 = 0): MFM pattern 0x4489
- C2 sync (0xC2 with clock C3 = 0): MFM pattern 0x5224

### 3.2 Decoding algorithm

```cpp
// State
uint16_t shiftReg;     // 16-bit window for pattern matching
int bitCount;          // Bits in current byte (0-15)
bool synced;           // True if clock/data phase is known

void processBit(uint8_t bit) {
    shiftReg = (shiftReg << 1) | bit;
    bitCount++;
    
    // Check for sync patterns (re-aligns phase)
    if (shiftReg == 0x4489) {  // A1 sync
        emitByte(0xA1, /*isSyncMark=*/true);
        bitCount = 0;
        synced = true;
        return;
    }
    if (shiftReg == 0x5224) {  // C2 sync
        emitByte(0xC2, /*isSyncMark=*/true);
        bitCount = 0;
        synced = true;
        return;
    }
    
    // Emit byte every 16 bits (even if not synced - garbage but matching READ TRACK)
    if (bitCount >= 16) {
        // Extract data bits (odd positions: 1, 3, 5, 7, 9, 11, 13, 15)
        uint8_t byte = 0;
        for (int i = 0; i < 8; i++) {
            if (shiftReg & (1 << (14 - i * 2))) {
                byte |= (1 << (7 - i));
            }
        }
        emitByte(byte, /*isSyncMark=*/false);
        bitCount = 0;
    }
}

void emitByte(uint8_t byte, bool isSyncMark) {
    bytes.push_back(byte);
    
    // Update clock bitmap (bit i marks byte i as sync)
    size_t byteIndex = bytes.size() - 1;
    size_t bitmapByte = byteIndex / 8;
    size_t bitmapBit = byteIndex % 8;
    
    if (bitmapByte >= clockBitmap.size()) {
        clockBitmap.resize(bitmapByte + 1, 0);
    }
    if (isSyncMark) {
        clockBitmap[bitmapByte] |= (1 << bitmapBit);
    }
}
```

### 3.3 FmDecoder

FM is simpler: every data bit is preceded by a clock bit, so cells are grouped in pairs:

```
FM bit pairs: C7 D7 C6 D6 C5 D5 C4 D4 C3 D3 C2 D2 C1 D1 C0 D0
              ↑  ↑  ↑  ↑  ...
              clock data clock data

Normal data: clock = 1 always
Address marks: special clock patterns
  - FE (IDAM): clock pattern 0xC7 (11000111)
  - F8-FB (DAM variants): clock pattern 0xD7 (11010111)
```

## 4. MfmEncoder — bytes + clock bitmap to bit cells

Location: `core/src/emulator/io/fdc/flux/mfm_encoder.h/.cpp`

### 4.1 Encoding algorithm

```cpp
bool prevDataBit = false;  // Track last data bit for clock calculation

void encodeByte(uint8_t byte, bool isSyncMark) {
    if (isSyncMark && byte == 0xA1) {
        // A1 with missing clocks: emit raw 0x4489
        emit16Bits(0x4489);
        prevDataBit = true;  // A1 ends with data bit 1
        return;
    }
    if (isSyncMark && byte == 0xC2) {
        // C2 with missing clock: emit raw 0x5224
        emit16Bits(0x5224);
        prevDataBit = false;  // C2 ends with data bit 0
        return;
    }
    
    // Normal MFM encoding
    for (int i = 7; i >= 0; i--) {
        bool dataBit = (byte >> i) & 1;
        bool clockBit = (!prevDataBit && !dataBit);
        
        bitCells.push_back(clockBit ? 1 : 0);
        bitCells.push_back(dataBit ? 1 : 0);
        
        prevDataBit = dataBit;
    }
}
```

### 4.2 FM encoding

FM always has clock = 1, except for address marks:

```cpp
void encodeByte(uint8_t byte, bool isSyncMark) {
    uint8_t clockPattern = 0xFF;  // Default: all clocks present
    
    if (isSyncMark && byte == 0xFE) clockPattern = 0xC7;  // IDAM
    if (isSyncMark && byte >= 0xF8 && byte <= 0xFB) clockPattern = 0xD7;  // DAM variants
    
    for (int i = 7; i >= 0; i--) {
        bool clockBit = (clockPattern >> i) & 1;
        bool dataBit = (byte >> i) & 1;
        
        bitCells.push_back(clockBit ? 1 : 0);
        bitCells.push_back(dataBit ? 1 : 0);
    }
}
```

## 5. Track length calculation

For SCP: `rawSize = round(revolutionTimeNs / byteCellTimeNs)`
- MFM: byteCellTimeNs = 16 × 2000 = 32000 ns
- FM: byteCellTimeNs = 16 × 4000 = 64000 ns

For HFE: `rawSize = bitCount / 16`

Typical values:
- MFM 300 RPM: 200 ms / 32 µs = 6250 bytes
- FM 300 RPM: 200 ms / 64 µs = 3125 bytes

## 6. Test plan

Location: `core/tests/emulator/io/fdc/flux/`

### 6.1 FluxPll tests (`flux_pll_test.cpp`)

| Test | Input | Expected |
|------|-------|----------|
| `IdealTiming_MFM` | Intervals: 2000, 4000, 2000, 2000, 6000 | Cells: 1, 1 0, 1, 1, 1 0 0 |
| `IdealTiming_FM` | Intervals: 4000, 8000, 4000 | Cells: 1, 1 0, 1 |
| `JitteredTiming_10Percent` | ±10% jitter on ideal | Same as ideal |
| `JitteredTiming_20Percent` | ±20% jitter on ideal | Same as ideal |
| `ModeSwitching` | Reset and switch MFM→FM | Cell width doubles |
| `DetectMode_MFM` | MFM-like histogram | Returns MFM |
| `DetectMode_FM` | FM-like histogram | Returns FM |
| `LongGap` | 10× normal interval | 10 cells (1 + 9 zeros) |
| `ShortPulse` | 0.75× normal interval | 1 cell |

### 6.2 MfmDecoder tests (`mfm_decoder_test.cpp`)

| Test | Input | Expected |
|------|-------|----------|
| `SyncA1_Pattern` | 0x4489 bit pattern | 0xA1, clock bit set |
| `SyncC2_Pattern` | 0x5224 bit pattern | 0xC2, clock bit set |
| `NormalByte_0x00` | MFM-encoded 0x00 | 0x00, no clock bit |
| `NormalByte_0xFF` | MFM-encoded 0xFF | 0xFF, no clock bit |
| `NormalByte_0x4E` | MFM-encoded 0x4E (gap) | 0x4E, no clock bit |
| `MultipleBytes` | GAP + 3×A1 + FE + data | Correct bytes, clock bits on A1s |
| `PhaseRecovery` | Garbage + A1 + data | A1 re-syncs, data decoded |
| `FullTrack_TRDos` | 6250 MFM cells from TRD track | 16 sectors detected after reindex |

### 6.3 FmDecoder tests (`fm_decoder_test.cpp`)

| Test | Input | Expected |
|------|-------|----------|
| `IdamMark_FE` | FM-encoded 0xFE with C7 clocks | 0xFE, clock bit set |
| `DamMark_FB` | FM-encoded 0xFB with D7 clocks | 0xFB, clock bit set |
| `NormalByte` | FM-encoded 0x55 | 0x55, no clock bit |
| `FullTrack_FM` | FM track with 26×128 sectors | All sectors detected |

### 6.4 Encoder roundtrip tests (`flux_roundtrip_test.cpp`)

| Test | Pattern | Assertion |
|------|---------|-----------|
| `MFM_Roundtrip_TrdosTrack` | TrackFormatSpec::trdos() | Encode→Decode == original |
| `MFM_Roundtrip_512ByteSectors` | 9×512 track | Encode→Decode == original |
| `MFM_Roundtrip_BadCRC` | Track with deliberate CRC error | Error preserved |
| `FM_Roundtrip_128ByteSectors` | 26×128 FM track | Encode→Decode == original |
| `MFM_Roundtrip_MixedSectorSizes` | 128+256+512 mixed | Encode→Decode == original |

### 6.5 Integration tests with real data

| Test | Fixture | Assertion |
|------|---------|-----------|
| `RealSCP_TRDos` | `testdata/loaders/scp/trdos.scp` (to add) | Loads, validates as TR-DOS |
| `RealHFE_TRDos` | `testdata/loaders/hfe/trdos.hfe` (to add) | Loads, validates as TR-DOS |
| `SCP_Save_Reload` | Model → SCP → Model | Bytes identical |
| `HFE_Save_Reload` | Model → HFE → Model | Bytes identical |

## 7. Implementation files

```
core/src/emulator/io/fdc/flux/
├── flux_pll.h        // FluxPll class declaration
├── flux_pll.cpp      // FluxPll implementation
├── mfm_decoder.h     // MfmDecoder, FmDecoder declarations
├── mfm_decoder.cpp   // MfmDecoder, FmDecoder implementations
├── mfm_encoder.h     // MfmEncoder, FmEncoder declarations
└── mfm_encoder.cpp   // MfmEncoder, FmEncoder implementations

core/tests/emulator/io/fdc/flux/
├── flux_pll_test.cpp
├── mfm_decoder_test.cpp
├── fm_decoder_test.cpp
└── flux_roundtrip_test.cpp
```

## 8. Out of scope

- Copy protections requiring flux-level timing precision beyond byte granularity
- Multi-revolution preservation in save (single revolution only)
- Advanced HFE v3 opcodes: SETBITRATE mid-track (logged, not implemented)
- SCP extended mode flags (bit 6)

## 9. References

- [HxC Floppy Emulator file format](http://hxc2001.free.fr/floppy_drive_emulator/HFE_file_format.pdf)
- [SuperCard Pro file format](http://www.oocities.org/bfreak_1999/scp_file_format.pdf)
- [WD1793 datasheet](../../WD1793/) — address mark timing
- [MFM encoding](https://en.wikipedia.org/wiki/Modified_frequency_modulation)
