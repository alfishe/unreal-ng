# Demo: Insult Loader Analysis

## Bug Fix Notice ⚠️

**A critical emulator bug was discovered and fixed during this investigation:**
- **Problem:** Loader infinite loop when loading snapshots with TR-DOS active
- **Root Cause:** CF_TRDOS flag not restored by snapshot loader
- **Fix:** Applied to `core/src/loaders/snapshot/loader_sna.cpp` (line 505)
- **Tests:** 6 new tests added, all passing (36/36 total)
- **Details:** See `BUG_FIX_CF_TRDOS.md` and `SUMMARY.md`

## Overview

Complete analysis of the custom IM2 disk loader used in the "Insult" demo for ZX Spectrum with TR-DOS.

**Key Features:**
- Interrupt-driven disk I/O (50Hz)
- Self-modifying code for state control
- On-the-fly data depacking
- AY sound chip updates during loading

## Loader Specifications

- **Size:** 550 bytes (513 code + 37 embedded text)
- **Location:** 0xF4F4 - 0xF719
- **Instructions:** 317 total
- **IM2 Vector:** 0xF300 (I register = 0xF3)
- **Technique:** Interrupt-driven with depacking

## Files

### Documentation

1. **IM2_LOADER_ANALYSIS.md** - Complete technical analysis
   - Full disassembly explanation
   - Self-modifying code details
   - TRDOS ROM gateway pattern
   - Complete loading algorithm
   - Timing and performance analysis

2. **README.md** - This file
   - Quick overview
   - File listing
   - Architecture summary
   
3. **QUICK_REFERENCE.md** - Quick reference guide
   - Key code snippets
   - Memory map diagram
   - Important variables
   - Command reference

### Binary Dumps

1. **im2_disk_loader_complete.bin** (550 bytes)
   - Complete loader: 0xF4F4 - 0xF719
   - Includes handler, main loader, depack routine, and text
   
2. **im2_vector_table_0xF300.bin** (512 bytes)
   - IM2 vector table (all entries → 0xF4F4)
   
3. **im2_handler_0xF4F4.bin** (256 bytes)
   - First 256 bytes of loader (handler entry)

### Disassembly

1. **im2_disk_loader_FINAL.txt** (317 instructions)
   - Complete annotated disassembly
   - All code with addresses, bytes, and mnemonics
   - Section markers for navigation

## Architecture

```
IM2 Interrupt (50Hz)
    ↓
Vector Table (0xF300) → all point to 0xF4F4
    ↓
Handler Entry (0xF4F4)
    ↓
State Check (self-modifying code at 0xF4F6)
    ↓
┌─────────────┴─────────────┐
│                           │
│ Loading Done?             │ Still Loading?
│                           │
↓                           ↓
Exit to TRDOS          Continue to 0xF500
(via 0x3D2F)           (Main Loader)
                            │
                            ↓
                       • Depack data (0xF5F5)
                       • Update AY sound (0x5E15)
                       • Check completion
                       • Read sectors via TRDOS
                       • Loop until done
```

## Key Techniques

### 1. Self-Modifying Code

```asm
0xF4F6: 1808    jr #08      ; When done (exit to TRDOS)
0xF4F6: 2828    jr #28      ; When loading (continue to 0xF500)
```

The loader modifies this instruction during execution.

### 2. TRDOS ROM Gateway

```asm
0xF5A6: e5          push hl           ; Save HL
0xF5A7: 21c32f      ld hl,#2FC3       ; Return address
0xF5AA: e3          ex (sp),hl        ; Swap with stack
0xF5AB: c32f3d      jp.#3D2F          ; Jump to TRDOS
                                      ; Returns to 0x2FC3
```

Classic Z80 far call using stack manipulation.

### 3. Interrupt-Driven Transfer

Data transfer occurs during interrupt handler execution, allowing the loader to:
- Update sound chip every frame
- Show loading progress
- Handle disk timing precisely

### 4. On-the-Fly Depacking

The depack routine at 0xF5F5 uses a 64-byte rotating buffer to decode compressed data as it's loaded from disk.

## Memory Map

```
0x3D00-0x3FFF  TRDOS helper routines
  └─ 0x3D2F    ROM page-in/call gateway

0xF300-0xF3FF  IM2 vector table (257 bytes)
  └─ All entries point to 0xF4F4

0xF4F4-0xF4FD  IM2 interrupt entry (10 bytes)
  └─ 0xF4F6    Self-modifying instruction

0xF500-0xF5A5  Main loader logic (166 bytes)
  ├─ Completion check
  ├─ Sector setup
  └─ Error handling

0xF5A6-0xF5F4  TRDOS ROM call wrappers (79 bytes)
  ├─ 0xF5A6    Generic call wrapper
  ├─ 0xF5AE    Read FDC status
  ├─ 0xF5C6    Set track
  ├─ 0xF5CE    Set sector
  ├─ 0xF5E0    Track seek
  ├─ 0xF5E5    Set side
  └─ 0xF5ED    Wait for FDC

0xF5F5-0xF693  Depack routine (159 bytes)
  └─ Bit rotation depacker

0xF694-0xF6D1  Work buffer (62 bytes)
  └─ Rotating buffer for depack

0xF6D2-0xF6F5  Embedded text (36 bytes)
  └─ "<DECRUNCHING PART <INTRO"

0xF6F6-0xF719  Padding (36 bytes)
```

## Capture Details

- **System:** ZX Spectrum Pentagon 512K with TR-DOS
- **Emulator:** unreal-ng
- **Web API:** http://localhost:8090/api/v1/
- **Breakpoint:** 0x3D2F (TRDOS gateway)
- **Date:** 2026-01-29

---

*For complete analysis, see IM2_LOADER_ANALYSIS.md*  
*For quick reference, see QUICK_REFERENCE.md*
