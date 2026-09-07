# Boot Code Disassembly - The Mystery Solved!

## Boot Parameter Setup (0x5BC6-0x5BF2)

```asm
0x5BC6: 21 5E 5E     ld hl, #5E5E      ; Source for copy
0x5BC9: 11 F3 F6     ld de, #F6F3      ; Destination  
0x5BCC: 7E           ld a, (hl)
0x5BCD: ED A0        ldi               ; Copy byte, increment
0x5BCF: FE FF        cp #FF            ; Check for end marker
0x5BD1: 20 F9        jr nz, #5BCC      ; Loop until FF
0x5BD3: 22 C7 5B     ld (#5BC7), hl    ; Store end pointer
0x5BD6: FB           ei                ; Enable interrupts

; ========== CRITICAL SECTION ==========
0x5BD7: 21 26 5E     ld hl, #5E26      ; ← SECTOR TABLE ADDRESS (SELF-MODIFIED!)
0x5BDA: 46           ld b, (hl)        ; ← LOAD B FROM TABLE (sector count)
0x5BDB: 23           inc hl
0x5BDC: 5E           ld e, (hl)        ; Load E (sector)
0x5BDD: 23           inc hl
0x5BDE: 56           ld d, (hl)        ; Load D (track)
0x5BDF: ED 53 A2 5C  ld (#5CA2), de    ; Store DE somewhere
0x5BE3: 23           inc hl
0x5BE4: 5E           ld e, (hl)        ; Load E (dest low)
0x5BE5: 23           inc hl
0x5BE6: 56           ld d, (hl)        ; Load D (dest high)
0x5BE7: 23           inc hl
0x5BE8: 22 D8 5B     ld (#5BD8), hl    ; ← SELF-MODIFY! Update table pointer!
0x5BEB: EB           ex de, hl
0x5BEC: 11 00 0C     ld de, #0C00      ; Track 12, Sector 0
0x5BEF: E5           push hl           ; Save destination
0x5BF0: CD 1D F5     call #F51D        ; ← CALL LOADER!
```

## The Discovery

### Self-Modifying Code Pattern

At 0x5BE8, the code stores HL to 0x5BD8, which is **inside** the `ld hl, #5E26` instruction!

- **0x5BD7**: `21` (LD HL opcode)
- **0x5BD8-0x5BD9**: `26 5E` (the address #5E26)

After the first loader call:
1. HL has advanced through the sector table
2. `ld (#5BD8), hl` overwrites the table address
3. **Next time through, it loads from the NEW position!**

### Sector Table Format

At 0x5E26, the sector table contains entries:
- Byte 0: **Sector count** (loaded into B)
- Bytes 1-2: Sector/Track (DE)
- Bytes 3-4: Destination address (DE, then → HL)
- (5 bytes per entry)

### The Problem

The boot code is designed to be called MULTIPLE times, each time loading:
1. A new B value from the table
2. New sector/track/destination parameters
3. Calling the loader again

**But if the loader never returns**, or returns and immediately gets re-entered, B will keep loading NEW values from the table, explaining why B increases!

## The Real Issue

The loader at 0xF51D should:
1. Read B sectors
2. Return to 0x5BF3

But something is causing it to:
1. Never reach the djnz loop properly
2. Get stuck in TRDOS ROM calls
3. Never return

**Hypothesis**: The TRDOS ROM calls are not returning properly, causing the loader to hang in TRDOS ROM (0x3D2F, 0x3FE5, etc.) instead of progressing through its internal loop.
