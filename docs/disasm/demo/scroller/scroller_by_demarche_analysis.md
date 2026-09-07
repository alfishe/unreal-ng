# Scroller by Demarche - Demo Loading Analysis

> **SUPERSEDED - conclusions below are disproven.**
>
> The "POKE 32765 bug" root cause in this document is wrong: the bus trace later
> proved line 80 is `OUT VAL"32765",VAL"20"`, the port write **was** applied
> (`#7FFD <- $14`, page 4 at `$C000`). The real chain: in the 128K-editor boot
> environment the editor's SWAP hook re-derives `#7FFD` from the stale `BANK_M`
> shadow (`$5B5C`) at the next statement boundary, reverting the page - so
> SCROLL12 loads into page 0 and page 2 ends up zeroes. This is authentic
> stock-ROM behavior (no emulator compensation by design); the demo runs fine
> when booted without the editor hooks (direct TR-DOS / 48K BASIC path).
>
> Current analysis, evidence chain and regression tests:
> [`docs/disasm/demo/scroller/`](demo/scroller/) (`README.md`, `REVERSING.md`,
> `TRIAGE.md`, `trd_mlz.py`). Kept below for investigation history.

## Overview
- **Demo**: Scroller by Demarche (AAA Party demo)
- **Platform**: Pentagon 128K/512K (uses standard 128K pages 0-7)
- **Sound**: COVOX on port #FB
- **Issue**: Demo crashes after loading, shows 48K ROM prompt

## File Structure (TRD Disk)

| File | Load Address | Content |
|------|-------------|---------|
| SCROLLER.B | #C000 | BASIC loader (tokenized) |
| SCROLL00.C | #6200 | Main code: LOADER + LOADTBL + DEPACK + player |
| SCROLL15.C | #8000 | MegaLZ compressed data block |
| SCROLL10.C | #8000 | MegaLZ compressed data block |
| SCROLL11.C | #8000 | MegaLZ compressed data block |
| SCROLL13.C | #8000 | MegaLZ compressed data block |
| SCROLL17.C | #8000 | MegaLZ compressed data block |
| SCROLL12.C | #C000 | MegaLZ compressed data block |

## Memory Map

```
0000-3FFF: ROM (48K or 128K depending on bit 4 of #7FFD)
4000-7FFF: RAM Page 5 (screen/video RAM, also used for code!)
8000-BFFF: RAM Page 2 (always mapped - compressed data loaded here)
C000-FFFF: Switchable RAM (pages 0-7 via #7FFD bits 0-2)
```

## Key Addresses

| Address | Symbol | Description |
|---------|--------|-------------|
| #6200 (25088) | START | Entry point, then JP SETUP |
| #6206 (25094) | LOADER | Decompression routine entry |
| #620A | LDTBLA | Self-modifying pointer to current LOADTBL entry |
| ~#6226 | LOADTBL | Decompression table (6 entries × 5 bytes) |
| ~#6240 | DEPACK | MegaLZ V4 decompressor |
| #3D03 (15619) | TR-DOS | TR-DOS command entry point |

## BASIC Loader Flow

```basic
10 RANDOMIZE USR VAL "25088"         ; Initialize (does LD SP,START; JP SETUP)
20 RANDOMIZE USR VAL "15619": CODE: LOAD "SCROLL00" CODE  ; Load main code to #6200
30 RANDOMIZE USR VAL "15619": CODE: LOAD "SCROLL15" CODE  ; Load compressed to #8000

40 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": CODE: LOAD "SCROLL10" CODE
50 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": CODE: LOAD "SCROLL11" CODE
60 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": CODE: LOAD "SCROLL13" CODE
70 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": CODE: LOAD "SCROLL17" CODE
80 RANDOMIZE USR VAL "25094": POKE 32765,20: RANDOMIZE USR VAL "15619": CODE: LOAD "SCROLL12" CODE
90 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "25088"  ; Final depack, then start demo
```

## LOADER Routine (#6206)

```asm
LOADER:
        DI
        PUSH    IY
        LD      HL,(LDTBLA)     ; Get pointer to current table entry
        LD      A,(HL)          ; Get page value
        INC     HL
        LD      BC,#7FFD
        OUT     (C),A           ; Switch RAM page at C000-FFFF
        LD      E,(HL)          ; Get destination low
        INC     HL
        LD      D,(HL)          ; Get destination high
        INC     HL
        LD      C,(HL)          ; Get source low
        INC     HL
        LD      B,(HL)          ; Get source high
        INC     HL
        LD      (LDTBLA),HL     ; Update pointer for next call
        LD      H,B
        LD      L,C             ; HL = source address
        CALL    DEPACK          ; Decompress: HL=source, DE=dest
        POP     IY
        RET
```

## LOADTBL Structure

Each entry: [1 byte PAGE] [2 bytes DEST] [2 bytes SOURCE]

| # | PAGE | Dest Addr | Src Addr | Meaning |
|---|------|-----------|----------|---------|
| 1 | #15 | LOADER_END | #8000 | Page 5 at C000, decompress from #8000 to ~#6300 |
| 2 | #10 | #C000 | #8000 | Page 0 at C000, decompress from #8000 to #C000 |
| 3 | #11 | #C000 | #8000 | Page 1 at C000, decompress from #8000 to #C000 |
| 4 | #13 | #C000 | #8000 | Page 3 at C000, decompress from #8000 to #C000 |
| 5 | #17 | #DB00 | #8000 | Page 7 at C000, decompress from #8000 to #DB00 |
| 6 | #14 | #8000 | #C000 | Page 4 at C000, decompress from #C000 to #8000 |

## Page Value Decoding

Port #7FFD format:
- Bits 0-2: RAM page at C000-FFFF (0-7)
- Bit 3: Screen select (0=normal, 1=shadow)
- Bit 4: ROM select (0=128K ROM, 1=48K ROM)
- Bit 5: Paging lock
- Bits 6-7: Extended RAM for 512K+ (not used by this demo)

| Value | Binary | ROM | RAM Page |
|-------|--------|-----|----------|
| #10 | 0001'0000 | 48K | 0 |
| #11 | 0001'0001 | 48K | 1 |
| #13 | 0001'0011 | 48K | 3 |
| #14 | 0001'0100 | 48K | 4 |
| #15 | 0001'0101 | 48K | 5 |
| #17 | 0001'0111 | 48K | 7 |

**All values select 48K ROM (bit 4=1) and standard 128K RAM pages (0-7).**

## Loading Sequence

1. **Line 20**: SCROLL00 loads to #6200 (main code installed)
2. **Line 30**: SCROLL15 loads to #8000 (first compressed block)
3. **Line 40**: 
   - USR 25094 → LOADER processes entry 1: decompress #8000 → page 5
   - Then SCROLL10 loads to #8000 (overwrites compressed data)
4. **Line 50**: 
   - USR 25094 → LOADER processes entry 2: decompress #8000 → page 0 at C000
   - Then SCROLL11 loads to #8000
5. **Lines 60-70**: Same pattern for pages 3, 7
6. **Line 80**: 
   - USR 25094 → LOADER processes entry 5
   - POKE 32765,20 → OUT #7FFD,#14 (page 4 at C000)
   - Then SCROLL12 loads to #C000 (directly to page 4!)
7. **Line 90**: 
   - USR 25094 → LOADER processes entry 6: decompress #C000 → #8000
   - USR 25088 → JP SETUP (start demo)

## Critical Observations

1. **All compressed data goes through #8000** (page 2, always mapped)
2. **Decompression outputs to paged memory** via C000-FFFF window
3. **Line 80 is special**: POKE #7FFD,#14 BEFORE loading SCROLL12 to #C000
4. **SCROLL12 loads directly to page 4** (not to #8000 like others)

## Critical Bug Found (CONFIRMED)

**Line 80's POKE is WRONG!**

```basic
POKE VAL"32765",VAL"20"   ; WRONG! Writes to RAM at 0x7FFD (screen memory)
```

The demo author intended to set port #7FFD to #14 (page 4) before loading SCROLL12, but:
- `POKE addr,val` writes to **memory**, not I/O ports
- Address 32765 (0x7FFD) is in video RAM (0x4000-0x7FFF = page 5)
- The correct approach is machine code with `OUT (#7FFD),A`
- Alternative: POKE BANK_M (23388/0x5B5C), but standard TR-DOS doesn't use it for CODE loads

### Hardware-Level Explanation

On the Z80 bus, memory and I/O are completely separate operations:
- **Memory access** (POKE): Uses MREQ signal, writes to RAM at address 0x7FFD (video memory page 5)
- **Port access** (OUT): Uses IORQ signal, writes to paging register at port #7FFD

There is NO hardware mechanism on real ZX Spectrum that could convert a memory write to a port write. These are fundamentally different bus cycles.

### Emulator Verification

All emulators correctly separate memory and I/O:
- **UnrealSpeccy**: `set_banks()` only updates from `comp.p7FFD` which is set by io.cpp (OUT handler), never by memory writes
- **MiSTer FPGA**: Port decode uses `~nIORQ` condition; memory uses `~nMREQ`
- **Our emulator**: Memory writes go through `Memory::*`, port writes go through `PortDecoder::*`

Port #7FFD decoding patterns compared:
- **Our mask**: A15=0, A2=1, A1=0 (0x8006/0x0004)
- **UnrealSpeccy**: A15=0, A1=0 (less strict, but #7FFD has A2=1 anyway)
- **MiSTer**: `~addr[15] & ~addr[1]` for non-Plus3

All three correctly decode port #7FFD. No emulator has a "compatibility hack" for POKE to 0x7FFD.

### Actual Failure Sequence

1. LOADER entry 5 sets #7FFD = #17 (page 7 at C000)
2. SCROLL17.C loads to #8000, decompresses to #DB00 in page 7 ✓
3. POKE 32765,20 writes to RAM address 0x7FFD - **paging unchanged (#17)**!
4. TR-DOS loads SCROLL12.C to #C000, but page 7 is still active
5. Data goes to **page 7** instead of intended **page 4**
6. LOADER entry 6 switches to page 4 and tries to decompress from #C000
7. Finds garbage (page 4 is empty), MegaLZ crashes
8. Demo shows 48K ROM prompt

### Conclusion

**This demo is buggy and will not work correctly on any accurate emulator or real hardware.** The POKE instruction does not switch memory pages - it only writes to video RAM. The author likely:
- Tested with lucky initial state where page was already correct
- Or used a modified ROM/loader during development
- Or the demo was never properly tested end-to-end

## Investigation Results

| Issue | Status | Finding |
|-------|--------|---------|
| POKE 32765 bug | **CONFIRMED** | Demo writes to RAM, not I/O port - this is the root cause |
| Port #7FFD decoding | **VERIFIED** | Correct - matches UnrealSpeccy and MiSTer |
| Pentagon 512K paging | **VERIFIED** | Bits [0:2] + [6:7] correctly combined |
| Memory::SetRAMPageToBank3 | **VERIFIED** | Correctly updates bank pointers |
| TR-DOS LOAD behavior | **VERIFIED** | Uses currently paged memory (no magic) |

## Verification Steps (if debugging)

1. Set breakpoint at #6206 (LOADER entry) - verify it's called 6 times
2. Check #7FFD value when SCROLL12 loads at line 80:
   - **Expected if demo worked**: #14 (page 4)
   - **Actual due to POKE bug**: #17 (page 7) - unchanged from LOADER entry 5
3. After SCROLL12 loads, check #C000 in page 4 vs page 7:
   - Page 7 will have the compressed data
   - Page 4 will be empty/garbage

## MegaLZ Decompressor

The demo uses MegaLZ V4 algorithm (fyrex^mhm). Input: HL=source, DE=dest.
If source data is empty/corrupt, decompressor may crash or produce garbage.
