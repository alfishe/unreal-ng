# Quick Reference: Insult IM2 Loader

## Specifications

- **Location:** 0xF4F4 - 0xF719
- **Size:** 550 bytes (513 code + 37 text)
- **Instructions:** 317 total
- **Vector Table:** 0xF300 (I = 0xF3)
- **Self-Modifying:** Yes (0xF4F6)

## Key Code Snippets

### IM2 Handler Entry (0xF4F4)

```asm
0xF4F4: 08           ex af,af'         ; Preserve AF
0xF4F5: b8           cp b              ; Check state
0xF4F6: 1808         jr #08            ; ← SELF-MODIFIED!
0xF4F8: 3269f5       ld (#F569),a      ; Store state
0xF4FB: 08           ex af,af'         ; Restore AF
0xF4FC: fb           ei                ; Re-enable interrupts
0xF4FD: c32f3d       jp.#3D2F          ; Exit to TRDOS
```

**Self-modifying instruction at 0xF4F6:**
- `0x18` → Skip to exit (when done)
- `0x28` → Continue to loader (when active)

### Main Loader (0xF500)

```asm
0xF500: d9           exx               ; Alternate registers
0xF501: cdf5f5       call #F5F5        ; Depack data
0xF504: cd155e       call #5E15        ; Update AY sound
0xF507: 3e00         ld a,#00          
0xF509: d9           exx               ; Back to main
0xF50A: 7c           ld a,h            ; Check done (HL=0xFFFF?)
0xF50B: 3c           inc a
0xF50C: 2006         jr nz,#06
0xF50E: 7d           ld a,l
0xF50F: 3c           inc a
0xF510: 2002         jr nz,#02
0xF512: 33           inc sp            ; Done! Clean stack
0xF513: 33           inc sp
0xF514: af           xor a
0xF515: 3208f5       ld (#F508),a      ; Clear flag
0xF518: 08           ex af,af'
0xF519: fb           ei
0xF51A: c32f3d       jp.#3D2F          ; Exit
```

### TRDOS ROM Gateway (0xF5A6)

```asm
0xF5A6: e5           push hl           ; Save HL
0xF5A7: 21c32f       ld hl,#2FC3       ; Return address
0xF5AA: e3           ex (sp),hl        ; Swap with stack
0xF5AB: c32f3d       jp.#3D2F          ; Jump to TRDOS
```

**Z80 Far Call Trick:**
- Pushes custom return address
- Jumps to TRDOS gateway at 0x3D2F
- Gateway pages in ROM, calls procedure, returns to pushed address

### Sector Read (0xF546)

```asm
0xF546: e5           push hl           ; Save pointer
0xF547: 3e84         ld a,#84          ; Read sector
0xF549: 0e01         ld c,#01          ; Count = 1
0xF54B: cda6f5       call #F5A6        ; Issue via TRDOS
0xF54E: c5           push bc
0xF54F: 017f00       ld bc,#007F       ; 127 bytes
0xF552: 3e01         ld a,#01
0xF554: 3269f5       ld (#F569),a      ; Mark active
0xF557: 3e28         ld a,#28          
0xF559: 32f6f4       ld (#F4F6),a      ; Modify handler!
0xF55C: af           xor a
0xF55D: 08           ex af,af'
0xF55E: cdedf5       call #F5ED        ; Transfer
0xF561: 08           ex af,af'
0xF562: c5           push bc
0xF563: 3e18         ld a,#18
0xF565: 32f6f4       ld (#F4F6),a      ; Restore handler
0xF568: 3e01         ld a,#01
0xF56A: a7           and a
0xF56B: ccf4f4       call z,#F4F4      ; Recursive call!
```

## Memory Map

```
┌─────────────────────────────────────────┐
│ 0x3D2F         TRDOS ROM Gateway        │
├─────────────────────────────────────────┤
│ 0xF300-0xF3FF  IM2 Vector Table         │
│                (all → 0xF4F4)           │
├─────────────────────────────────────────┤
│ 0xF4F4-0xF4FD  IM2 Handler Entry        │
│   └─ 0xF4F6    Self-modifying byte      │
├─────────────────────────────────────────┤
│ 0xF500-0xF5A5  Main Loader Logic        │
├─────────────────────────────────────────┤
│ 0xF5A6-0xF5F4  TRDOS Call Wrappers      │
│   ├─ 0xF5A6    Generic wrapper          │
│   ├─ 0xF5AE    Read status              │
│   ├─ 0xF5C6    Set track                │
│   ├─ 0xF5CE    Set sector               │
│   ├─ 0xF5E0    Track seek               │
│   ├─ 0xF5E5    Set side                 │
│   └─ 0xF5ED    Wait for FDC             │
├─────────────────────────────────────────┤
│ 0xF5F5-0xF693  Depack Routine           │
│                (bit rotations)          │
├─────────────────────────────────────────┤
│ 0xF694-0xF6D1  Work Buffer (64 bytes)   │
├─────────────────────────────────────────┤
│ 0xF6D2-0xF6F5  Embedded Text            │
│                "<DECRUNCHING PART       │
│                 <INTRO"                 │
├─────────────────────────────────────────┤
│ 0xF6F6-0xF719  Padding                  │
└─────────────────────────────────────────┘
```

## Flow Diagram

```
1. Interrupt fires (50Hz)
     ↓
2. Vector at 0xF300 → 0xF4F4
     ↓
3. Check byte at 0xF4F6:
     ├─ 0x18: jr #08 → exit to TRDOS
     └─ 0x28: jr #28 → continue to 0xF500
           ↓
4. Main loader (0xF500):
     ├─ Depack data (0xF5F5)
     ├─ Update AY sound (0x5E15)
     ├─ Check completion (HL=0xFFFF?)
     └─ If done: exit
         If not: continue
           ↓
5. Sector setup:
     ├─ Restore 0xF4F6 = 0x18
     ├─ Set track/side/sector
     └─ Continue to read
           ↓
6. Sector read:
     ├─ Issue read command (0x84)
     ├─ Modify 0xF4F6 = 0x28
     ├─ Transfer 127 bytes
     ├─ Restore 0xF4F6 = 0x18
     └─ Loop back to step 5
```

## Important Variables

| Address | Size | Purpose |
|---------|------|---------|
| 0xF4F6 | 1 byte | Self-modifying jump offset |
| 0xF508 | 1 byte | Loading active flag |
| 0xF569 | 1 byte | Sector transfer state |
| 0xF667 | 1 byte | Depack state |
| 0xF66E | 2 bytes | Depack pointer |
| 0xF694 | 64 bytes | Rotating work buffer |

## Port I/O

| Port | Dir | Purpose |
|------|-----|---------|
| 0x1F | I/O | WD1793 FDC command/status |
| 0x3F | IN | WD1793 track register |
| 0x5F | OUT | WD1793 sector register |
| 0x7F | OUT | WD1793 track register |
| 0xFF | OUT | Memory paging (128K) |
| 0xF6 | IN | TRDOS status |
| 0xF7 | I/O | TRDOS control |
| 0xFD | OUT | AY-3-8912 register select |

## FDC Commands

| Value | Command |
|-------|---------|
| 0x84 | Read sector |
| 0x2C | Select side 0 |
| 0x3C | Select side 1 |

## Files

1. **IM2_LOADER_ANALYSIS.md** - Complete analysis
2. **README.md** - Overview
3. **QUICK_REFERENCE.md** - This file
4. **im2_disk_loader_complete.bin** - Complete binary (550 bytes)
5. **im2_disk_loader_FINAL.txt** - Complete disassembly (317 instructions)
6. **im2_vector_table_0xF300.bin** - Vector table (512 bytes)
7. **im2_handler_0xF4F4.bin** - Handler entry (256 bytes)
8. **insult-1-4.sna** - Snapshot at breakpoint

---

*For detailed analysis, see IM2_LOADER_ANALYSIS.md*
