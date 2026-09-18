# ATM 710 CP/M BIOS Disassembly

This folder contains the complete disassembly and analysis of the ATM 710
CP/M BIOS, correlating with the [ATM CP/M BIOS documentation](http://atmturbo.nedopc.com/inf/bios_cpm.htm).

| ROM page                            | Listing                                           |
|-------------------------------------|---------------------------------------------------|
| `data/rom/atm2.rom` page 3 (16 KiB) | [atm710-cpm-p3.asm](atm710-cpm-p3.asm)            |

[atm710-cpm-p3.asm](atm710-cpm-p3.asm) is byte-complete: the
instruction/`defb` lines reconstruct the 16 KiB page exactly (16384/16384
bytes verified).  It covers the page-zero stubs, service monitor core,
cold-boot config shell, DRI CCP 2.2, BDOS 2.2, XVR BIOS and the
driver/WBOOT tail; the listing header documents the four runtime address
mappings of the one image (1:1 service view, +$C000 alias and driver
windows, and the dual $D400-$EFFF window: cold config shell at +$C000
vs. WBOOT's CCP+BDOS copy at runtime-$B83A).  The markdown files below
are the traced analysis behind it.

**Key facts** (verified by M1/bus tracing during the prince.trd investigation):

- **A: is the electronic (RAM) disk; the floppy is B:** - a transient that
  opens its data files with FCB drive 0 (= current drive) must be launched
  with B: current (`B:` then `PR2` at the `B>` prompt, not `B:PR2` in one
  line - the CCP re-selects A: before jumping to the transient).
- BDOS and disk I/O run in **service sessions**: 7FFD bit 4 swaps the low
  16K (0000-3FFF) from CP/M RAM to a service ROM bank while the monitor
  services the request.

## Memory Map

| Address Range | Purpose |
|--------------|---------|
| 0x0000-0x00FF | Page Zero (WBOOT, BDOS entry) |
| 0x0100-0x35FF | TPA (Transient Program Area) |
| 0x3600-0x387A | BDOS/CCP |
| 0x387B-0x3A00 | BIOS (in RAM) |
| 0x5F00-0x5FFF | XBIOS Variables / Channel Descriptors |
| 0x7F00-0x7FFF | Sector Buffer Area |
| 0xF800-0xFFFF | Monitor ROM |

## Key Entry Points

### Page Zero
- 0x0001: JP 0x387E - WBOOT vector
- 0x0005: ED 59 00 00 - Non-standard (ATM-specific)

### BIOS (0x387B)
The BIOS is loaded into RAM at 0x387B. Unlike standard CP/M, ATM uses a minimal BIOS that delegates to the Monitor ROM.

| Offset | Vector | Function |
|--------|--------|----------|
| +0 | 0x387B | BOOT - Cold start (DI; SCF; RET) |
| +3 | 0x387E | WBOOT - Warm boot (see analysis) |

### Monitor ROM (0xF800)
Jump table for system services:

| Address | Vector | Function |
|---------|--------|----------|
| 0xF800 | JP 0xF835 | Boot entry |
| 0xF803 | JP 0xF8A9 | Console input |
| 0xF806 | JP 0xF833 | NOP/RET |
| 0xF809 | JP 0xF89A | Console output |
| 0xF80F | JP 0xF84A | List output |
| 0xF818 | JP 0xF857 | Home disk |
| 0xF81B | JP 0xF833 | NOP/RET |
| 0xF81E | JP 0xF83B | System reset |
| 0xF821 | JP 0xF88F | Read sector |
| 0xF824 | JP 0xF872 | Write sector |

For the full 17-entry image-relative table on the sys page (and the open
question about the $F833/$F835/$F857 entries), see the `BiosJumpTable`
comment in [atm710-cpm-p3.asm](atm710-cpm-p3.asm).

## Disk Monitor

### FDC Low-Level Routines (0x1400-0x1800)

The disk driver uses direct FDC (WD1793) port access:

| Port | Read | Write |
|------|------|-------|
| 0x1F | Status | Command |
| 0x3F | Track | Track |
| 0x5F | Sector | Sector |
| 0x7F | Data | Data |
| 0xFF | Status | System (drive select, side, etc) |

### Key Routines

#### 0x148E - Restore to Track 0
```z80
0x148E: 3E 01      LD A, #01
0x1490: 3D         DEC A         ; A = 0
0x1491: D3 3F      OUT (#3F), A  ; Track register = 0
0x1493: AF         XOR A
0x1494: C9         RET
```

#### 0x149E - FDC Reset
```z80
0x149E: F3         DI
0x149F: 3E 08      LD A, #08     ; Select drive 0
0x14A1: D3 FF      OUT (#FF), A  ; Beta register
0x14A3: 06 00      LD B, #00
0x14A5: 10 FE      DJNZ #14A5    ; Delay loop
0x14A7: 3E 0C      LD A, #0C
0x14A9: D3 FF      OUT (#FF), A
0x14AB: 3E D0      LD A, #D0     ; Force interrupt
0x14AD: D3 1F      OUT (#1F), A
```

#### 0x14F0 - Read Address (READ ID)
Reads sector header into buffer at 0x7F40:
```z80
0x14F0: 3E C0      LD A, #C0     ; READ ADDRESS command
0x14F2: 21 40 7F   LD HL, #7F40  ; Buffer address
...
0x1500: D3 1F      OUT (#1F), A  ; Issue command
; After READ ADDRESS, (0x7F40) = cylinder from ID field
```

#### 0x14FC-0x14FF - Set Track from Buffer
```z80
0x14FC: 3A 40 7F   LD A, (#7F40) ; Load track from buffer
0x14FF: D3 3F      OUT (#3F), A  ; Set FDC track register
```

#### 0x15A0 - Read Sector
```z80
0x15A2: F6 80      OR #80        ; Add READ SECTOR bit (0x80)
0x15A4: F3         DI
0x15A5: D3 1F      OUT (#1F), A  ; Issue command
0x15A7: CD 83 15   CALL #1583    ; Wait for DRQ
...
```

## System Variables

### XBIOS Variables (0x5F00-0x5FFF)

| Address | Purpose |
|---------|---------|
| 0x5F40 | Drive status/flags |
| 0x5F56 | Current drive (0-3) |
| 0x5F5D | FDD status byte |
| 0x5F69 | Motor timeout counter |
| 0x5F6A-0x5F6C | Drive timing parameters |
| 0x5F6D | Current operation flag |
| 0x5F75 | JP 0x0D0B - Read handler |
| 0x5F78 | JP 0x0D50 - Write handler |
| 0x5FB0 | Current sector number |

### Sector Buffer (0x7F00-0x7FFF)

| Address | Purpose |
|---------|---------|
| 0x7F40 | READ ADDRESS buffer (cylinder, head, sector, size) |
| 0x7FC0 | Sector data buffer |

## Files

- [atm710-cpm-p3.asm](atm710-cpm-p3.asm) - THE complete annotated
  disassembly of the CP/M system page (16 KiB, byte-verified)
- [monitor-rom.md](monitor-rom.md) - Monitor ROM: service sessions, F8xx FDC driver, EAxx BDOS, warm boot (D400)
- [bios-ram.md](bios-ram.md) - RAM-resident BIOS/BDOS/CCP: page zero, BDOS conventions, CCP launch protocol, drive mapping
- [fdc-driver.md](fdc-driver.md) - FDC driver routines (0x1400-0x1800)
- [disk-monitor.md](disk-monitor.md) - Disk monitor (0x0D00-0x1300): channel descriptors, track calculation
- [pr2-loader.md](pr2-loader.md) - PR2.COM (Prince of Persia): unpacking stub, #FF7 window loader, launch failure analysis
- [analysis.md](analysis.md) - DALTCYL track-calculation investigation (see resolution addendum)

## How the listing was made

The generation pipeline is preserved in [scripts/](scripts/) next to the
listing (it needs `z80dasm` 1.2.0 on PATH and Python 3):

0. `extract.py` - slices the system page (`atm_sys_p3.bin`) out of
   `data/rom/atm2.rom` (file offset $C000-$FFFF).
1. `analyze.py` - dedicated Z80 length decoder + reachability analysis
   with the dual-window address translation (the $D400-$EFFF targets
   translate both as +$C000 and -$B83A) produces the code/data block
   map (`atm_sys_p3.blocks`); hand-verified code islands are freed from
   the string carver via the FREES/UNCARVE lists, data blocks are named
   via RENAMES.
2. `dict_atm710.py` - hand-written symbol dictionary and educational
   comments; every runtime anchor was traced on the emulator during the
   prince.trd investigation.
3. `gen.py` - runs z80dasm with the block map and symbols, then inserts
   the comment blocks and the file header.
4. `checkcov.py` - verifies the listing against the binary byte for
   byte; `auditblocks.py` checks that every `; BLOCK` header is preceded
   by a comment naming it.

After editing the dict or block map, regenerate and verify from
`scripts/`:

```sh
python3 gen.py atm_sys_p3.bin atm_sys_p3.blocks dict_atm710.py \
    ../atm710-cpm-p3.asm
python3 checkcov.py ../atm710-cpm-p3.asm atm_sys_p3.bin
python3 auditblocks.py ../atm710-cpm-p3.asm
```

## References

- [ATM CP/M BIOS Documentation](http://atmturbo.nedopc.com/inf/bios_cpm.htm)
- [Local translated copy](/Volumes/TB4-4Tb/Projects/Knowledge/zx/04_operating_systems/atm_cpm_bios.md)
