# WD1793 WRITE_TRACK Implementation Comparison

## unrealspeccy Reference Implementation (wd93cmd.cpp)

### Key FSM States
- `S_WRTRACK` (L262-274): Initial state after command, waits for DRQ, then calls [getindex()](other/unrealspeccy/wd93cmd.cpp#493-500) to wait for index pulse
- `S_WR_TRACK_DATA` (L276-336): Main write loop - writes bytes from `rwptr=0` until `rwlen=0`

### Critical Code (L276-336):
```cpp
case S_WR_TRACK_DATA:
{
    if (notready()) break;
    if (rqs & DRQ) { status |= WDS_LOST; data = 0; }
    
    seldrive->t.seek(seldrive, seldrive->track, side, JUST_SEEK);
    seldrive->t.sf = JUST_SEEK; // invalidate sectors
    
    unsigned char marker = 0, byte = data;
    unsigned crc;
    
    switch(data) {
        case 0xF5: byte = 0xA1; marker = 1; start_crc = rwptr + 1; break;
        case 0xF6: byte = 0xC2; marker = 1; break;
        case 0xF7:
            crc = wd93_crc(seldrive->t.trkd + start_crc, rwptr - start_crc);
            byte = crc & 0xFF; break;
    }
    
    seldrive->t.write(rwptr++, byte, marker);  // USES TRKCACHE::write()
    rwlen--;
    
    if (data == 0xF7) {
        seldrive->t.write(rwptr++, (crc >> 8) & 0xFF, 0);
        rwlen--;  // CRC is 2 bytes
    }
    
    if ((int)rwlen > 0) {
        next += seldrive->t.ts_byte;
        state2 = S_WR_TRACK_DATA; state = S_WAIT;
        rqs = DRQ; status |= WDS_DRQ;
    } else {
        state = S_IDLE;
    }
}
```

### TRKCACHE::write() (wd93trk.cpp L42-52):
```cpp
void write(unsigned pos, unsigned char byte, char index) {
    if (!trkd) return;
    trkd[pos] = byte;           // Write data byte
    if (index) set_i(pos);       // Set clock mark bit in bitmap
    else clr_i(pos);             // Clear clock mark bit
}
```

### Key Observations:
1. **Clock mark bitmap** - unrealspeccy uses `trki[]` array to track MFM clock marks (bit per byte)
2. **rwptr/rwlen** - Position and remaining length set by [getindex()](other/unrealspeccy/wd93cmd.cpp#493-500) which gets track length from disk
3. **Index-to-index semantics** - [getindex()](other/unrealspeccy/wd93cmd.cpp#493-500) calculates wait until index pulse, sets `rwlen = trklen`
4. **Track is written IN PLACE** - `seldrive->t.trkd[]` IS the raw track buffer in FDD
5. **Sector invalidation** - `sf = JUST_SEEK` invalidates parsed sector cache after write

---

## unreal-ng Current Implementation (wd1793.cpp)

### Current Issues Identified:

1. **No `rwptr`/`rwlen` semantics** - Uses `_rawDataBufferIndex` and `_bytesToWrite` instead
2. **No clock mark bitmap** - Missing `trki[]` equivalent for special MFM bytes (0xA1, 0xC2)
3. **Buffer initialization** - [getindex()](other/unrealspeccy/wd93cmd.cpp#493-500) equivalent may not properly set `rwlen`
4. **In-place write** - Does write to DiskImage directly via `_rawDataBuffer`
5. **Missing sector invalidation** - Needs to mark sector cache dirty after write

### Missing from unreal-ng:
- The [write(pos, byte, marker)](other/unrealspeccy/wd93.h#42-53) function that sets clock mark bits
- Clock mark bitmap in DiskImage::Track

---

## Required Fix: Add Clock Mark Support

DiskImage::Track needs:
```cpp
uint8_t clockMarksBitmap[TRACK_BITMAP_SIZE_BYTES];  // ~782 bytes (6250/8)

void writeWithClockMark(unsigned pos, uint8_t byte, bool clockMark) {
    if (pos < RAW_TRACK_SIZE) {
        // Get pointer to raw track data
        uint8_t* rawData = reinterpret_cast<uint8_t*>(static_cast<RawTrack*>(this));
        rawData[pos] = byte;
        
        // Set/clear clock mark in bitmap
        if (clockMark)
            clockMarksBitmap[pos/8] |= (1 << (pos & 7));
        else
            clockMarksBitmap[pos/8] &= ~(1 << (pos & 7));
    }
}
```

But wait - DiskImage already has this! Check [FullTrack](core/src/emulator/io/fdc/diskimage.h#229-234):
```cpp
struct FullTrack : public RawTrack {
    uint8_t clockMarksBitmap[TRACK_BITMAP_SIZE_BYTES] = {};  // Already exists!
    uint8_t badBytesBitmap[TRACK_BITMAP_SIZE_BYTES] {};
};
```

So unreal-ng DOES have clock mark support in the disk image structure, just not using it in [processWriteTrack()](core/src/emulator/io/fdc/wd1793.cpp#2172-2311).
