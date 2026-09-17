# ATM CP/M BIOS Analysis - DALTCYL Bug

## Summary

**Problem**: ATM CP/M cannot find files on prince.trd disk - reports "NO FILE" for directory listing.

**Root Cause**: The ATM BIOS track calculation routine does NOT use the DALTCYL (system tracks) field from the channel descriptor. It only adds DBEGCYL (starting cylinder), which is 0 for standard floppy configuration.

## Technical Details

### Expected Behavior

Per CP/M specification, the DPB contains an `OFF` field specifying system tracks to skip:
- CP/M directory starts at track `OFF`
- Physical track = logical track + OFF

Per ATM channel descriptor, `DALTCYL` (offset +0x0B) specifies the number of system tracks.

### Actual Behavior

The track calculation at address 0x1299 (4785):

```z80
; At 0x128D-0x129A:
0x128D: CD D7 12   CALL #12D7      ; DE = block / total_heads
0x1290: DD 75 F0   LD (IX-#10), L  ; Save side (remainder)
0x1293: FD 6E 0C   LD L, (IY+#0C)  ; DBEGCYL low
0x1296: FD 66 0D   LD H, (IY+#0D)  ; DBEGCYL high
0x1299: 19         ADD HL, DE      ; physical_track = track + DBEGCYL
0x129A: 22 B0 5F   LD (#5FB0), HL  ; Store result
```

**Missing code**: There is no instruction to load DALTCYL (IY+0x0B) and add it to the track.

### ROM Channel Descriptor

At 0xFA01 (floppy A default):
```
Offset 0x0B: DALTCYL = 0x02 (2 system tracks)
Offset 0x0C: DBEGCYL = 0x00 (starting cylinder 0)
```

The ROM correctly specifies DALTCYL=2, but the code never reads this value.

### Disk Image (prince.trd)

```
Track 0 (offset 0x0000): 0xE5 fill (empty/formatted)
Track 1 (offset 0x1000): 0xE5 fill (empty/formatted)  
Track 2 (offset 0x2000): CP/M directory entries (TITLE1.DAT, PR.COM, etc.)
```

This is correct for a disk with 2 system tracks (OFF=2).

### What Happens

1. CP/M BDOS requests directory read (logical track 0, sector 1)
2. BIOS calculates: physical_track = 0 + DBEGCYL = 0 + 0 = 0
3. FDC reads physical track 0 (cylinder 0, side 0)
4. Returns 0xE5 (empty data)
5. CP/M interprets empty directory → "NO FILE"

### What Should Happen

1. CP/M BDOS requests directory read (logical track 0, sector 1)
2. BIOS should calculate: physical_track = 0 + DALTCYL + DBEGCYL = 0 + 2 + 0 = 2
3. FDC reads physical track 2 (cylinder 1, side 0)
4. Returns valid CP/M directory entries
5. CP/M lists files correctly

## Fix Options

### Option 1: Patch ATM ROM (Recommended)

Add DALTCYL to track calculation at 0x1299:

**Before:**
```z80
0x1299: 19         ADD HL, DE      ; HL = track + DBEGCYL
0x129A: 22 B0 5F   LD (#5FB0), HL
```

**After:**
```z80
0x1293: FD 7E 0B   LD A, (IY+#0B)  ; A = DALTCYL
0x1296: 5F         LD E, A
0x1297: 16 00      LD D, #00       ; DE = DALTCYL (16-bit)
0x1299: 19         ADD HL, DE      ; HL = track + DBEGCYL + DALTCYL
0x129A: 22 B0 5F   LD (#5FB0), HL
```

This requires ROM patching in the emulator or creating a fixed ROM image.

### Option 2: Modify Channel Descriptor

Set DBEGCYL = DALTCYL in the floppy channel descriptor:

At 0xFA0C-0xFA0D, change:
- 0xFA0C: 00 00 (DBEGCYL = 0)
To:
- 0xFA0C: 02 00 (DBEGCYL = 2)

This would make DBEGCYL incorporate the system tracks, working around the missing DALTCYL addition.

### Option 3: Create CP/M-only Disk Format

Format disks with directory at track 0 (no system tracks) so DALTCYL=0 works.

This is not ideal for compatibility with other CP/M systems that expect OFF=2.

## Verification

To confirm this is the issue:
1. Set breakpoint at 0x1299
2. Check values: DE should be 0 (logical track), HL should become 2 (physical track)
3. Currently: DE=0, HL becomes 0 (wrong)

## Impact

This bug affects all CP/M disk access on ATM 710 with standard TR-DOS/CP/M formatted disks that have 2 system tracks.

## Architecture Clarification

The ATM system has two layers:
1. **Monitor ROM** (0xF800+) - permanent ROM code
2. **CP/M BIOS** (0x0000-0x3FFF) - loaded from disk into RAM at boot

The track calculation code at 0x1290 is in the **RAM-resident BIOS**, not ROM. This code is loaded from the boot disk during CP/M cold start.

The ROM at 0xF800 contains stubs that jump to actual monitor routines. The channel descriptor template at 0xFA01 in ROM has DALTCYL=2.

## Conclusion

The ATM CP/M BIOS (disk-resident code, not ROM) does not add DALTCYL to track calculations. This is either:
1. A bug in the original ATM CP/M system software
2. An intentional design where DBEGCYL must incorporate system tracks
3. A requirement for ATM-specific disk formats

## Recommended Fix

Since the issue is in the disk-resident BIOS, the fix should:

1. **Patch the BIOS in emulator** - intercept and fix the track calculation
2. **Or modify channel descriptor** - set DBEGCYL=2 for floppy

## Next Steps

1. Test with Unreal Speccy to verify if the same issue occurs
2. Find original ATM CP/M boot disk to compare behavior
3. Check if prince.trd was formatted for a different CP/M system

## Resolution (post-investigation addendum, 2026-09-17)

The hand-disassembly above remains a valid description of the ROM track
calculation, but the "NO FILE" reproduction it describes no longer occurs
on the current emulator. Verified end state:

- `ATM710CpmBoot_Test.CpmDirListsPrinceCatalog` passes: the full user-0
  catalog of prince.trd is listed, the hidden user-15 CONFIG entry stays
  hidden, and the "garbage" in the BUG entry is the on-disk 0xFD byte in
  the extension field rendered as a glyph - on-disk content, not a read
  error.
- The subsequent PR2 launch investigation
  ([pr2-loader.md](pr2-loader.md), [bios-ram.md](bios-ram.md)) traced
  every BDOS open/read during a full game launch with no track-calculation
  anomalies.

The fix options listed above were therefore not applied.
