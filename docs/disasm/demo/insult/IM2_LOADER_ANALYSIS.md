# Insult Demo: IM2 Disk Loader Analysis

## Overview

Complete analysis of the custom IM2 interrupt-driven disk loader used in the "Insult" demo for ZX Spectrum with TR-DOS.

## Loader Specifications

- **Total Size:** 550 bytes (513 bytes code + 37 bytes embedded text)
- **Address Range:** 0xF4F4 - 0xF719
- **Total Instructions:** 317
- **Interrupt Mode:** IM2 (Vectored interrupts)
- **I Register:** 0xF3 (vector table at 0xF300)

## System State at Capture

### CPU Registers

```
PC:   0x3D2F (15663) - TRDOS ROM gateway (breakpoint location)
SP:   0x619A (24986)
I:    0xF3 (243)     - IM2 interrupt vector page
IM:   2              - Interrupt Mode 2 enabled
IFF1: 1              - Interrupts enabled
IFF2: 1

Main Registers:
  AF:   0x0604 (1540)
  BC:   0x167F (5759)
  DE:   0x0C00 (3072)
  HL:   0x2A53 (10835)

Index Registers:
  IX:   0xB65A (46682)
  IY:   0x5C3A (23610)

Alternate Registers:
  AF':  0x0042 (66)
  BC':  0x0808 (2056)
  DE':  0x07E0 (2016)
  HL':  0x2758 (10072)
```

## Architecture

### IM2 Interrupt Flow

```
Hardware Interrupt (50Hz)
    ↓
I=0xF3 → Vector Table at 0xF300 (all entries = 0xF4)
    ↓
Handler at 0xF4F4
    ↓
Conditional branching:
    • If loading done: jump to 0x3D2F (exit to TRDOS)
    • If still loading: continue to 0xF500 (main loader)
```

### The TRDOS ROM Gateway at 0x3D2F

The loader uses a clever pattern for calling TR-DOS ROM procedures. Example at 0xF5A6:

```asm
0xF5A6: e5           push hl           ; Save HL
0xF5A7: 21c32f       ld hl,#2FC3       ; Load return address
0xF5AA: e3           ex (sp),hl        ; Swap with stack
0xF5AB: c32f3d       jp.#3D2F          ; Jump to TRDOS ROM gateway
                                       ; Returns to 0x2FC3!
```

**This is a classic Z80 far call trick:**
1. Push return address onto stack
2. Jump to TRDOS ROM gateway at 0x3D2F
3. Gateway pages in TR-DOS ROM, calls procedure, returns to pushed address

## Complete Boot Sequence and Demo Entry Point

### Overview

The demo starts at **0x8700** after all sectors are loaded and unpacked. The complete boot-to-demo flow is:

```
Boot sequence (from 0x5B80)
    ↓
Setup IM2, copy loader to 0xF4F4
    ↓
0x5BF0: call #F51D  ← Call disk loader
    ↓
[IM2 Loader runs, loads and unpacks sectors]
    ↓
Loader finishes, returns to 0x5BF3
    ↓
More setup code (0x5BF3 - 0x5C9E)
    ↓
0x5CA1: call #8700  ← **DEMO STARTS HERE!**
    ↓
0x5CA4: call #5D88  (after demo part finishes)
    ↓
0x5CB6: jp #5B65    (loop or next part)
```

### Key Points

1. **Demo Entry Point:** 0x8700
   - This is where the loaded and unpacked demo code executes
   - At the breakpoint (0x3D2F), this area shows NOPs/zeros because loading hasn't started yet
   - Once the loader completes, this memory contains the actual demo code

2. **Boot Code Location:** 0x5B80-0x5BFF
   - Initial system setup
   - IM2 configuration
   - Loader installation to 0xF4F4

3. **Loader Invocation:** 0x5BF0
   - `call #F51D` - Enters the loader's continue_loading routine
   - The loader runs in interrupt context, loading all sectors

4. **Post-Load Setup:** 0x5BF3-0x5C9E
   - Additional system configuration
   - Memory initialization
   - Screen setup

5. **Demo Launch:** 0x5CA1
   - `call #8700` - Transfer control to the loaded demo code
   - This is the final jump to the demo's actual entry point

### Boot Code Details

The boot sequence at 0x5B80 performs:

```asm
0x5B80-0x5BAC:  Initial setup
    - Copy screen data (0x4000 → 0xC000, 6912 bytes)
    - Configure sound chip
    - Wait loops with interrupts

0x5BAC-0x5BC4:  IM2 Setup
    - Create interrupt vector table at 0xF300
    - All 257 entries point to 0xF4F4
    - Set I register to 0xF3
    - Copy loader code from 0x5EE5 to 0xF4F4

0x5BC6-0x5BEF:  Loader Parameter Setup
    - Configure sector/track information
    - Prepare destination addresses
    - Set up loop counters

0x5BF0:         Call Loader
    - call #F51D  (enters loader)

0x5BF3-0x5C9E:  Post-Load Processing
    - Memory setup
    - Screen initialization
    - System configuration

0x5CA1:         Launch Demo
    - call #8700  (demo entry point)
```

## Loader Structure

### Entry Point: 0xF4F4 (IM2 Interrupt Handler)

```asm
0xF4F4: 08           ex af,af'         ; Save AF in shadow register
0xF4F5: b8           cp b              ; Check loading state
0xF4F6: 1808         jr #08            ; ← SELF-MODIFYING!
0xF4F8: 3269f5       ld (#F569),a      ; Store state
0xF4FB: 08           ex af,af'         ; Restore AF
0xF4FC: fb           ei                ; Re-enable interrupts
0xF4FD: c32f3d       jp.#3D2F          ; Exit to TRDOS
```

**Key:** The instruction at 0xF4F6 is **self-modifying code**!
- When done: `jr #08` (0x1808) → Skip to TRDOS exit
- When loading: Modified to continue to main loader

### Main Loader: 0xF500

```asm
0xF500: d9           exx               ; Switch to alternate registers
0xF501: cdf5f5       call #F5F5        ; Depack/decode routine
0xF504: cd155e       call #5E15        ; AY chip output
0xF507: 3e00         ld a,#00          
0xF509: d9           exx               ; Back to main registers
0xF50A: 7c           ld a,h            ; Check if transfer complete
0xF50B: 3c           inc a             ; HL = 0xFFFF when done
0xF50C: 2006         jr nz,#06         ; Not done, continue
0xF50E: 7d           ld a,l
0xF50F: 3c           inc a
0xF510: 2002         jr nz,#02
0xF512: 33           inc sp            ; Done! Pop return address
0xF513: 33           inc sp
0xF514: af           xor a
0xF515: 3208f5       ld (#F508),a      ; Clear loading flag
0xF518: 08           ex af,af'
0xF519: fb           ei
0xF51A: c32f3d       jp.#3D2F          ; Exit to TRDOS

# Continue loading...
0xF51D: 3e18         ld a,#18          ; Restore jump offset
0xF51F: 32f6f4       ld (#F4F6),a      ; Modify handler!
0xF522: 7a           ld a,d            ; Get track
0xF523: cb3f         srl a             ; Shift right
0xF525: cde0f5       call #F5E0        ; Set track
0xF528: 3e18         ld a,#18
0xF52A: cda6f5       call #F5A6        ; TRDOS ROM call
0xF52D: cdedf5       call #F5ED        ; Wait for operation
0xF530: 7a           ld a,d            ; Track again
0xF531: cb3f         srl a
0xF533: cdc6f5       call #F5C6        ; TRDOS call
0xF536: cb42         bit 0,d           ; Check side
0xF538: 3e2c         ld a,#2C          ; Side 0
0xF53A: 2002         jr nz,#02
0xF53C: 3e3c         ld a,#3C          ; Side 1
0xF53E: cde5f5       call #F5E5        ; Set side
0xF541: 7b           ld a,e            ; Get sector
0xF542: 3c           inc a
0xF543: cdcef5       call #F5CE        ; Set sector
```

### Sector Read Loop: 0xF546

```asm
0xF546: e5           push hl           ; Save destination
0xF547: 3e84         ld a,#84          ; Read sector command
0xF549: 0e01         ld c,#01          ; Sector count = 1
0xF54B: cda6f5       call #F5A6        ; Issue via TRDOS
0xF54E: c5           push bc
0xF54F: 017f00       ld bc,#007F       ; 127 bytes
0xF552: 3e01         ld a,#01
0xF554: 3269f5       ld (#F569),a      ; Loading active
0xF557: 3e28         ld a,#28          
0xF559: 32f6f4       ld (#F4F6),a      ; Modify handler!
0xF55C: af           xor a
0xF55D: 08           ex af,af'
0xF55E: cdedf5       call #F5ED        ; Transfer data
0xF561: 08           ex af,af'
0xF562: c5           push bc
0xF563: 3e18         ld a,#18
0xF565: 32f6f4       ld (#F4F6),a      ; Restore handler
0xF568: 3e01         ld a,#01
0xF56A: a7           and a
0xF56B: ccf4f4       call z,#F4F4      ; Recursive!
0xF56E: c1           pop bc
0xF56F: 78           ld a,b
0xF570: c1           pop bc
0xF571: a7           and a
0xF572: 2803         jr z,#03          ; Error check
0xF574: e1           pop hl
0xF575: 18cf         jr #CF            ; Loop back
```

### TRDOS ROM Call Wrappers (0xF5A6-0xF5F4)

These routines use the stack manipulation trick to call TR-DOS ROM procedures:

```asm
# Call wrapper template
0xF5A6: e5           push hl           ; Save HL
0xF5A7: 21c32f       ld hl,#2FC3       ; Return address
0xF5AA: e3           ex (sp),hl        ; Swap with stack
0xF5AB: c32f3d       jp.#3D2F          ; Jump to TRDOS

# FDC status read
0xF5AE: c5           push bc
0xF5AF: e5           push hl
0xF5B0: 21c1f5       ld hl,#F5C1       ; Return
0xF5B3: e5           push hl
0xF5B4: 21ec3f       ld hl,#3FEC       ; TRDOS routine
0xF5B7: e5           push hl
0xF5B8: 0e1f         ld c,#1F          ; Port #1F
0xF5BA: 21feff       ld hl,#FFFE
0xF5BD: fb           ei
0xF5BE: c32f3d       jp.#3D2F

# Set track (0xF5C6)
0xF5C6: e5           push hl
0xF5C7: 213a1e       ld hl,#1E3A       ; TRDOS set track
0xF5CA: e3           ex (sp),hl
0xF5CB: c32f3d       jp.#3D2F

# Set sector (0xF5CE)
0xF5CE: c5           push bc
0xF5CF: 0e5f         ld c,#5F          ; Sector port
0xF5D1: e5           push hl
0xF5D2: 21ddf5       ld hl,#F5DD
0xF5D5: e5           push hl
0xF5D6: 21532a       ld hl,#2A53       ; TRDOS routine
0xF5D9: e5           push hl
0xF5DA: c32f3d       jp.#3D2F

# Set track (alternate, 0xF5E0)
0xF5E0: c5           push bc
0xF5E1: 0e7f         ld c,#7F          ; Track port
0xF5E3: 18ec         jr #EC            ; Jump back to common code

# Set side (0xF5E5)
0xF5E5: e5           push hl
0xF5E6: 21f31f       ld hl,#1FF3       ; TRDOS set side
0xF5E9: e3           ex (sp),hl
0xF5EA: c32f3d       jp.#3D2F

# Wait for FDC (0xF5ED)
0xF5ED: e5           push hl
0xF5EE: 21e53f       ld hl,#3FE5       ; TRDOS wait routine
0xF5F1: e3           ex (sp),hl
0xF5F2: c32f3d       jp.#3D2F
```

### Depack/Decode Routine: 0xF5F5

This is a complex bit-shifting/rotation routine for depacking compressed data:

```asm
0xF5F5: 21ff50       ld hl,#50FF       ; Source pointer
0xF5F8: 1194f6       ld de,#F694       ; Work buffer
0xF5FB: 0608         ld b,#08          ; 8 bits
0xF5FD: 7d           ld a,l
0xF5FE: eb           ex de,hl
0xF5FF: cb06         rlc (hl)          ; Rotate through memory
0xF601: 23           inc hl
0xF602: eb           ex de,hl
0xF603: cb16         rl (hl)           ; Rotate with carry
0xF605: 2d           dec l
0xF606: cb16         rl (hl)
...
(continues with many rotate operations through 0xF661)
```

The depack routine continues through 0xF693, performing bit rotations on a 64-byte buffer to decode compressed sector data.

### Embedded Text: 0xF6D2

```asm
0xF6D2: 3c           inc a
0xF6D3: 44           ld b,h
0xF6D4: 45           ld b,l
0xF6D5: 43           ld b,e
0xF6D6: 52           ld d,d
0xF6D7: 55           ld d,l
0xF6D8: 4e           ld c,(hl)
0xF6D9: 43           ld b,e
0xF6DA: 48           ld c,b
0xF6DB: 49           ld c,c
0xF6DC: 4e           ld c,(hl)
0xF6DD: 47           ld b,a
```

ASCII: `"<DECRUNCHING"` - Loader identification string

```asm
0xF6DE: 2050         jr nz,#50
0xF6E0: 41           ld b,c
0xF6E1: 52           ld d,d
0xF6E2: 54           ld d,h
0xF6E3: 203c         jr nz,#3C
0xF6E5: 49           ld c,c
0xF6E6: 4e           ld c,(hl)
0xF6E7: 54           ld d,h
0xF6E8: 52           ld d,d
0xF6E9: 4f           ld c,a
```

ASCII: `" PART <INTRO"`

Complete embedded text: **`"<DECRUNCHING PART <INTRO"`**

## Key Techniques

### 1. Self-Modifying Code

The instruction at **0xF4F6** is dynamically changed during loading:
- **0x1808** (jr #08) → Skip to TRDOS exit
- **0x2828** (jr #28) → Continue with loading

This controls whether interrupts perform work or just exit.

### 2. Interrupt-Driven Data Transfer

- Data is transferred **during interrupt handler execution**
- Main code sets up parameters and waits
- Actual I/O happens in interrupt context
- ~20ms per interrupt (50Hz PAL timing)

### 3. Alternate Register Sets

```asm
d9      exx               ; Switch to BC'/DE'/HL'
...     ; Work with alternate registers
d9      exx               ; Back to BC/DE/HL
```

Preserves main registers while processing in interrupts.

### 4. TRDOS ROM Gateway Pattern

```asm
push hl
ld hl,return_addr
ex (sp),hl
jp #3D2F        ; Calls TRDOS ROM, returns to return_addr
```

Stack manipulation enables calling ROM routines with custom return addresses.

## Memory Map

```
0x3D00-0x3FFF : TRDOS helper routines
  └─ 0x3D2F   : ROM page-in/call gateway

0x5B00-0x5B64 : Helper routines (data copy/move)

0x5B65-0x5BFF : Boot initialization code
  ├─ 0x5B80   : Boot sequence entry
  ├─ 0x5BAC   : IM2 vector table setup
  ├─ 0x5BC6   : Loader code installation
  └─ 0x5BF0   : Loader invocation (call #F51D)

0x5BF3-0x5C9E : Post-load setup and configuration
  └─ 0x5CA1   : Demo launch (call #8700)

0x5E15-0x5E20 : AY sound chip output routine

0x8700        : **DEMO ENTRY POINT**
                (populated after loader completes)

0xF300-0xF3FF : IM2 vector table (257 bytes, all → 0xF4F4)

0xF4F4-0xF4FD : IM2 interrupt entry & state machine
  └─ 0xF4F6   : Self-modifying instruction

0xF500-0xF5A5 : Main loader logic
  ├─ State checking
  ├─ Sector loop
  └─ Error handling

0xF5A6-0xF5F4 : TRDOS ROM call wrappers
  ├─ 0xF5A6   : Generic TRDOS call
  ├─ 0xF5AE   : Read FDC status
  ├─ 0xF5C6   : Set track
  ├─ 0xF5CE   : Set sector
  ├─ 0xF5E0   : Track seek
  ├─ 0xF5E5   : Set side
  └─ 0xF5ED   : Wait for FDC

0xF5F5-0xF693 : Depack/decode routine
  └─ 64-byte rotating buffer at 0xF694

0xF694-0xF6D1 : Work buffer (64 bytes)

0xF6D2-0xF6F5 : Embedded text
  └─ "<DECRUNCHING PART <INTRO"

0xF6F6-0xF6FF : Padding
```

## Loading Algorithm

### Phase 1: Interrupt Handler Decision (0xF4F4)

```
1. Save AF register
2. Check loading state (cp b)
3. Execute self-modified jump at 0xF4F6:
   • If done → jr #08 → exit to TRDOS
   • If active → continue to 0xF500
```

### Phase 2: Main Loader (0xF500)

```
1. Switch to alternate registers (exx)
2. Call depack routine (0xF5F5)
3. Update AY sound output (0x5E15)
4. Switch back to main registers (exx)
5. Check if all data loaded (HL = 0xFFFF?)
   • If yes → clean up and exit
   • If no → continue to sector setup
```

### Phase 3: Sector Setup (0xF51D-0xF545)

```
1. Restore handler behavior (write 0x18 to 0xF4F6)
2. Set track number via TRDOS
3. Set side (0/1)
4. Set sector number
5. Continue to sector read
```

### Phase 4: Sector Read (0xF546-0xF5A5)

```
1. Push destination pointer
2. Issue FDC read sector command (0x84)
3. Set byte counter (127 bytes/sector)
4. Mark loading active
5. Modify handler (write 0x28 to 0xF4F6)
6. Wait for data transfer (0xF5ED)
7. Restore handler (write 0x18 to 0xF4F6)
8. Check for errors
9. Loop back to phase 3 for next sector
```

### Phase 5: Data Depacking (0xF5F5)

```
1. Load source pointer (compressed data)
2. Set up 64-byte work buffer at 0xF694
3. Perform bit rotations through buffer
4. Decode compressed data on-the-fly
5. Return to main loader
```

## Port Operations

The loader interacts with several hardware ports:

| Port | Direction | Purpose |
|------|-----------|---------|
| 0xFF | OUT | Memory paging control (128K banking) |
| 0x1F | IN/OUT | FDC (WD1793) command/status register |
| 0x3F | IN | FDC track register |
| 0x5F | OUT | FDC sector register |
| 0x7F | OUT | FDC track register |
| 0xF6 | IN  | TRDOS status register |
| 0xF7 | IN/OUT | TRDOS control register |
| 0xFD | OUT | AY-3-8912 register select |

## FDC Commands

| Value | Command |
|-------|---------|
| 0x84 | Read sector |
| 0x2C | Side 0 select |
| 0x3C | Side 1 select |

## Important Memory Locations

| Address | Purpose |
|---------|---------|
| 0xF4F6 | Self-modifying instruction byte |
| 0xF508 | Loading active flag |
| 0xF569 | Sector transfer state |
| 0xF667 | Depack state variable |
| 0xF66E | Depack pointer storage |
| 0xF694-0xF6D1 | 64-byte rotating work buffer |

## Timing Analysis

- **Interrupt frequency:** 50Hz (PAL) = 20ms per interrupt
- **Sector size:** 256 bytes (TRDOS)
- **Transfer rate:** ~128 bytes per sector read
- **Depacking:** On-the-fly during load
- **Sound updates:** Every interrupt (smooth audio during load)

## Technical Specifications

- **Architecture:** Zilog Z80
- **System:** ZX Spectrum 128K / Pentagon with TR-DOS
- **Interrupt Mode:** IM2 (Vectored)
- **I Register:** 0xF3
- **Vector Table:** 0xF300-0xF3FF (257 bytes)
- **Code Size:** 513 bytes
- **Embedded Text:** 37 bytes
- **Total:** 550 bytes
- **Self-Modifying:** Yes (0xF4F6)
- **Depacking:** Yes (0xF5F5)
- **Multi-tasking:** Sound updates during load

## Files Generated

### Binary Dumps

1. **im2_disk_loader_complete.bin** (550 bytes)
   - Complete loader from 0xF4F4 to 0xF719
   - Includes all code, work buffer space, and embedded text

2. **im2_vector_table_0xF300.bin** (512 bytes)
   - IM2 vector table (all entries point to 0xF4F4)

3. **im2_handler_0xF4F4.bin** (256 bytes)
   - Interrupt handler entry point and state machine

### Disassembly

1. **im2_disk_loader_FINAL.txt** (317 instructions)
   - Complete annotated disassembly
   - All 317 instructions with addresses and opcodes
   - Marked sections for easy navigation

### Documentation

1. **IM2_LOADER_ANALYSIS.md** (this file)
   - Complete technical analysis
   
2. **README.md**
   - Overview and file guide
   
3. **QUICK_REFERENCE.md**
   - Code snippets and quick reference

## Capture Details

- **Date:** 2026-01-29
- **Emulator:** unreal-ng (Pentagon 512K mode)
- **Web API:** http://localhost:8090/api/v1/
- **Breakpoint:** 0x3D2F (TRDOS gateway)
- **Emulator Instance:** bb8a1a90-7422-4263-9215-18759ad4a607
- **Method:** Memory dump and disassembly via REST API

---

*Analysis of interrupt-driven disk loader with self-modifying code, depacking, and sound updates.*
