# Disk Monitor Disassembly (0x0D00-0x1300)

The ATM disk monitor handles logical-to-physical disk translation and manages channel descriptors.

## Channel Descriptor (32 bytes)

From ROM at 0xFA01 (floppy default):

| Offset | Field | Value | Description |
|--------|-------|-------|-------------|
| +0x00 | DVALID | 0xFF | Validity flag |
| +0x01 | DTYP | 0x03 | Driver type (3 = floppy) |
| +0x02 | DUS | 0x00 | Drive unit number |
| +0x03 | DDTYP | 0x00 | Internal drive code |
| +0x04 | DHEADF | 0x00 | Fixed heads |
| +0x05 | DHEADR | 0x02 | Removable heads (2 sides) |
| +0x06-07 | DCYLN | 0x0050 (80) | Cylinders |
| +0x08 | DSECTT | 0x10 (16) | Sectors per track |
| +0x09-0A | DBYTES | 0x0100 (256) | Bytes per sector |
| +0x0B | DALTCYL | 0x02 | **System tracks** |
| +0x0C-0D | DBEGCYL | 0x0000 | Starting cylinder |
| +0x0E-0F | DBLDR | - | Blocks on disk |
| +0x10-11 | DBLTR | - | Blocks per track |
| +0x12-13 | DTRACK | - | Tracks on disk |
| +0x14 | DSECTL | - | Block-number length |
| +0x15-16 | DDIRENT | - | Directory entries |
| +0x17 | DIF0 | - | Interleave factor (track 0) |
| +0x18 | DIF1 | - | Interleave factor (track 1) |
| +0x19 | DIF2 | - | Interleave factor (rest) |
| +0x1A | DTIF | - | First-sector offset |
| +0x1B | DF8 | - | 8-inch flag |
| +0x1C | DFMFM | - | Density (MFM/FM) |

**Key Finding**: ROM default DALTCYL = 2, meaning 2 system tracks are expected.

## Track Calculation Flow

### 0x125C - Main Disk I/O Entry

```z80
0x125C: 3A A6 5F   LD A, (#5FA6)   ; Channel number
0x125F: 21 51 F9   LD HL, #F951    ; Channel table base
0x1262: CD 3E 14   CALL #143E      ; Index by channel
0x1265: E5         PUSH HL
0x1266: FD E1      POP IY          ; IY = channel descriptor
```

### 0x1290-0x12B2 - Track Calculation

```z80
; Calculate physical track from logical block
0x128D: CD D7 12   CALL #12D7      ; Divide: DE = block / heads
0x1290: DD 75 F0   LD (IX-#10), L  ; Save remainder (side)
0x1293: FD 6E 0C   LD L, (IY+#0C)  ; DBEGCYL low
0x1296: FD 66 0D   LD H, (IY+#0D)  ; DBEGCYL high
0x1299: 19         ADD HL, DE      ; physical_track = track + DBEGCYL
0x129A: 22 B0 5F   LD (#5FB0), HL  ; Store result
```

**CRITICAL**: The code only uses DBEGCYL, not DALTCYL!

For a floppy with DALTCYL=2 and DBEGCYL=0:
- Expected: physical_track = track + DALTCYL + DBEGCYL = track + 2
- Actual: physical_track = track + DBEGCYL = track + 0

This causes track 0 to be read instead of track 2 for directory access.

## Division Routine (0x12D7)

16-bit division: DE = DE / BC, remainder in HL

```z80
0x12D7: 21 00 00   LD HL, #0000    ; Remainder = 0
0x12DA: 3E 10      LD A, #10       ; 16-bit loop
0x12DC: 08         EX AF, AF'
; ... standard restoring division ...
```

## Channel Table (0xF951)

ROM table of channel descriptor pointers:

| Channel | Pointer | Driver |
|---------|---------|--------|
| 0 | 0xFA01 | Floppy A |
| 1 | 0xFA47 | Floppy B |
| 2 | 0xFA24 | RAM disk |
| 3 | 0xFA65 | - |
| ... | ... | ... |

## System Variables (0x5FA0 region)

| Address | Purpose |
|---------|---------|
| 0x5FA6 | Current channel number |
| 0x5FA7 | Operation type |
| 0x5FA9 | Block address (2 bytes) |
| 0x5FAD | DMA address (2 bytes) |
| 0x5FB0 | Physical track/sector (2 bytes) |
| 0x5FB7 | Retry counter |

## Analysis: DALTCYL Bug

The ROM channel descriptor correctly specifies DALTCYL=2, but the track calculation at 0x1299 only adds DBEGCYL, not DALTCYL. This appears to be either:

1. **A BIOS bug**: DALTCYL should be added to the track calculation
2. **A format assumption**: ATM expects disks with DBEGCYL = DALTCYL (system tracks offset baked into starting cylinder)
3. **An emulation issue**: The channel descriptor is not being loaded correctly

The prince.trd disk has:
- Directory at offset 0x2000 (track 2)
- Expected by CP/M with OFF=2 (2 system tracks)

But ATM BIOS reads track 0 because DBEGCYL=0 in the default channel descriptor.

## Proposed Fix

Either:
1. Add DALTCYL to the track calculation in the emulator
2. Set DBEGCYL=2 in the channel descriptor for CP/M disks
3. Create a disk format with directory at track 0
