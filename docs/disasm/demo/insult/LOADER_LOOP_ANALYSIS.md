# Loader Infinite Loop Analysis - SOLVED

## Problem Statement

The disk loader appeared to loop forever without completing when loaded from a snapshot.

**ROOT CAUSE FOUND:** The `.sna` snapshot loader was not setting the `CF_TRDOS` flag when restoring TR-DOS state, causing TR-DOS ROM to be deactivated and all TRDOS calls to fail.

**STATUS:** Bug fixed in `core/src/loaders/snapshot/loader_sna.cpp` (see BUG_FIX_CF_TRDOS.md)

## Exit Condition

The loader exits when HL == 0xFFFF (checked at 0xF50A-0xF511):

```asm
0xF50A: 7c           ld a,h         ; Check HL = 0xFFFF?
0xF50B: 3c           inc a          ; H+1 == 0? (H must be 0xFF)
0xF50C: 2006         jr nz,.continue_loading
0xF50E: 7d           ld a,l         ; Check L
0xF50F: 3c           inc a          ; L+1 == 0? (L must be 0xFF)
0xF510: 2002         jr nz,.continue_loading
0xF512: [EXIT]       ; HL == 0xFFFF, done!
```

**Key Question:** What sets HL to 0xFFFF to signal completion?

## Loop Structure

The main loop is:

```
1. Check HL == 0xFFFF? (0xF50A)
   ├─ YES → Exit (0xF512)
   └─ NO  → Continue (0xF51D)

2. Setup sector parameters (0xF51D-0xF545)
   - Set track, side, sector

3. Read sector (0xF546-0xF577)
   - Push HL (save destination)
   - Read 127 bytes via FDC
   - Pop HL
   - Loop back to step 1

4. Return to step 1
```

## Critical Missing Logic

**Problem:** There's no visible code that:
1. Updates HL to point to the next destination after each sector
2. Sets HL to 0xFFFF when the last sector is loaded

### Expected Behavior

A typical disk loader would:

```asm
; After reading sector...
pop hl                ; Restore destination
ld bc, 256            ; Sector size
add hl, bc            ; HL += 256 (next destination)

; Check if reached end marker
ld a, h
cp 0xFF
jr nz, continue
ld a, l
cp 0xFF
jr nz, continue
; HL == 0xFFFF, exit!
```

### What the Code Actually Does

Looking at 0xF546-0xF577:

```asm
0xF546: e5           push hl        ; Save HL
0xF547: 3e84         ld a,#84       ; Read command
...
[sector read happens]
...
0xF574: e1           pop hl         ; Restore HL
0xF575: 18cf         jr #CF         ; Jump back to 0xF546!
```

**BUG:** After `pop hl` at 0xF574, it jumps to 0xF546, which is INSIDE the sector loop, not back to the HL check at 0xF50A!

## Root Cause Hypothesis

The jump at 0xF575 (`jr #CF`) goes to 0xF546, creating a tight loop that:

1. Pushes HL
2. Reads a sector  
3. Pops HL (HL unchanged!)
4. Jumps back to step 1

**HL never changes**, so it never reaches 0xFFFF, causing an infinite loop!

## Where HL Should Be Updated

### Theory 1: Data-Driven Loading

HL might be updated by **reading it from the loaded sector data**. For example:
- Each sector contains the next destination address in its header
- The loader reads this and updates HL

### Theory 2: External Table

There might be a table in memory (set up during boot at 0x5B80) that contains:
- List of sectors to load
- Destination addresses
- End marker (0xFFFF)

The table pointer might be in **DE** or another register pair.

## Investigation Results

### Boot Code Analysis (0x5BD7-0x5BF0)

The boot code sets up initial parameters:

```asm
0x5BD7: ld hl, #5E26     ; Load table address
0x5BDA: ld b, (hl)       ; B = count (0x15 = 21 sectors)
...
0x5BEB: ex de, hl        ; HL = 0x2870 (destination from table)
0x5BEC: ld de, #0C00     ; DE = 0x0C00 (track=12, sector=0)
0x5BEF: push hl          ; Push initial destination
0x5BF0: call #F51D       ; Call loader
```

**Initial loader parameters:**
- HL = 0x2870 (destination address)
- DE = 0x0C00 (track 12, sector 0)
- Stack: return address

### Loader Code Search

**Critical Finding:** The loader code (0xF500-0xF577) contains **NO** instructions that:
1. Read from a sector table
2. Update HL to next destination
3. Update DE to next track/sector
4. Read control info from anywhere

**The only memory WRITES found:**
- 0xF515: `ld (#F508),a` - loader status
- 0xF51F/F559/F565: `ld (#F4F6),a` - self-modifying code
- 0xF554: `ld (#F569),a` - transfer state

**No memory READS except these variables!**

## Root Cause: MISSING SECTOR CHAINING LOGIC

### Expected Behavior

A multi-sector loader needs to:

```asm
; After each sector:
pop hl                 ; Get current destination
call read_next_params  ; Read HL', DE' from somewhere
ld hl, hl'            ; Set next destination
ld de, de'            ; Set next track/sector
cp hl, #FFFF          ; Check for end
jr z, exit
jr sector_loop        ; Continue
```

### Actual Behavior

The loader at 0xF575:

```asm
0xF574: pop hl         ; Restore HL (unchanged!)
0xF575: jr #CF         ; Jump to 0xF546 (same HL, same DE!)
```

**Result:** Infinite loop reading track 12, sector 0 to address 0x2870, forever.

## Possible Explanations

### Theory 1: Sector Contains Chain Info

Each sector might have a header:

```
Bytes 0-1: Next destination HL
Bytes 2-3: Next track/sector DE
Bytes 4+:  Actual data
```

The loader should extract these before transferring data. **BUT**: No such code exists in the loader!

### Theory 2: Loader is Incomplete/Broken

The loader code captured from the snapshot may be:
1. Incomplete (missing initialization)
2. Corrupted in the snapshot
3. Wrong version (not the actual working loader)

### Theory 3: External Mechanism

There might be:
1. Interrupt-driven updates to HL/DE
2. Hardware-assisted chaining
3. Hidden code in another memory area

**BUT**: IM2 handler (0xF4F4) doesn't update HL/DE either!

## Conclusion

**The loader is BROKEN or INCOMPLETE in this snapshot.**

The loader can successfully read one sector but has no mechanism to:
- Advance to the next sector
- Update the destination address
- Detect end of loading

This explains the infinite loop behavior observed during tracing.

## Recommendations

1. **Check if snapshot is correct** - Maybe capture at a different point
2. **Examine actual disk image** - Check if sector 12/0 has chain info
3. **Look for other loader code** - Maybe this is just a stub
4. **Check for missing initialization** - Code might be overwritten during boot

---

*Analysis date: 2026-01-30*
*Snapshot: insult-1-4.sna at first 0x3D2F breakpoint*
*Conclusion: Loader missing sector chaining logic - causes infinite loop*
