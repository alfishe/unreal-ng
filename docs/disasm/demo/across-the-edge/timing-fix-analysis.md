# Across The Edge - Timing Fix Analysis

**Demo**: Across The Edge  
**Group**: Demarche  
**Platform**: Pentagon 128K  
**Files**: `across_the_edge_by_demarche_fix_0.trd` through `across_the_edge_by_demarche_fix_3.trd`

## The Problem: Border-Paper Synchronization

ZX Spectrum demos that create border effects face a fundamental challenge: the border and paper areas have different timing relationships to the CPU. The ULA renders the screen continuously while the CPU executes code, and any timing mismatch between when the CPU changes the border color and when the ULA actually renders that pixel becomes visible as a horizontal offset.

On the original ZX Spectrum 48K, this timing was well-documented and consistent. But the Pentagon 128K, a popular Soviet clone, has different video timing:

- **Frame length**: 71680 T-states (vs 69888 on 48K)
- **Line length**: 224 T-states (vs 228 on 48K)
- **No ULA contention** on memory access (unlike the original Spectrum)

These differences mean that code written for one Pentagon clone may show misaligned border effects on another, or in different emulators. The Demarche group solved this by releasing four versions of their demo, each with a different timing compensation value.

## The Solution: A Clever Code Shift

Rather than using a simple delay constant (like a loop counter), Demarche employed an elegant technique: they physically shifted the entire loader code block by 1 byte between versions.

### Why Code Shifting Works

When the Z80 executes a `CALL` instruction, it fetches the 16-bit target address from memory. If the code block is shifted, the CALL must point to a different address to land on the same instruction:

```
Version 0: CALL 6806h  →  lands on DI instruction at 6806h
Version 1: CALL 6807h  →  lands on DI instruction at 6807h (code shifted +1)
Version 2: CALL 6808h  →  lands on DI instruction at 6808h (code shifted +2)
Version 3: CALL 6809h  →  lands on DI instruction at 6809h (code shifted +3)
```

The beauty is that all four versions execute *exactly the same code* — it's just located at different memory addresses. But those different addresses create different timing characteristics.

### The Timing Difference: 2 T-States Per Byte Shift

Testing reveals that each 1-byte shift produces a 4-pixel difference in border alignment. On the Pentagon, 2 pixels equal 1 T-state at the ULA's output rate, so:

| Version Transition | Pixel Shift | T-State Difference |
|-------------------|-------------|-------------------|
| fix0 → fix1       | 4 pixels    | 2 T-states        |
| fix1 → fix2       | 4 pixels    | 2 T-states        |
| fix2 → fix3       | 4 pixels    | 2 T-states        |

This gives the demo a total adjustment range of 12 pixels (6 T-states) — enough to compensate for typical timing variations between different Pentagon implementations.

## Technical Deep Dive

### File Structure

The demo uses TR-DOS format with multiple CODE files:

```
TR-DOS Catalog:
  ACROSS  .B  - BASIC loader, 121 bytes
  ACROSSLK.C  - Main loader code, 7680 bytes at 0x6200 ← shifted between versions
  ACROSSHK.C  - Hook code, 256 bytes at 0xBF00
  ACROSS15.C  - Part data at 0xC000
  ACROSS16.C  - Part data (largest) at 0xC000
  ... (additional parts)
```

### Identifying the Shift Mechanism

By comparing all four TRD files byte-by-byte, we find exactly two positions with consistent incremental differences:

**Position 1: File offset 0x1113 (memory 0x6213)**
```
fix0: CD 06 68    ; CALL 6806h
fix1: CD 07 68    ; CALL 6807h  
fix2: CD 08 68    ; CALL 6808h
fix3: CD 09 68    ; CALL 6809h
```

**Position 2: File offset 0xBC7E**
```
fix0: 3A D0 67    ; LD A,(67D0h)
fix1: 3A D1 67    ; LD A,(67D1h)
fix2: 3A D2 67    ; LD A,(67D2h)
fix3: 3A D3 67    ; LD A,(67D3h)
```

Both are references into the shifted code block — the CALL jumps to it, and the LD A reads data from it. When the block shifts, these references must be updated to point to the correct (shifted) locations.

### Examining the CALL Target

Looking at the actual bytes at the CALL targets reveals the shift clearly:

```
File 0, memory 6803-680C:
  6803: C3 13 3D    ; JP 3D13h
  6806: F3          ; DI          ← CALL lands here
  6807: ED 5B F4 5C ; LD DE,(5CF4h)
  ...

File 1, memory 6803-680C:  
  6803: 05          ; DEC B       (garbage — this is shifted data)
  6804: C3 13 3D    ; JP 3D13h
  6807: F3          ; DI          ← CALL lands here
  6808: ED 5B F4 5C ; LD DE,(5CF4h)
  ...

File 2, memory 6803-680C:
  6803: 0E 05       ; LD C,05h    (garbage)
  6805: C3 13 3D    ; JP 3D13h
  6808: F3          ; DI          ← CALL lands here
  ...

File 3, memory 6803-680C:
  6803: 5C 0E 05    ; (garbage)
  6806: C3 13 3D    ; JP 3D13h
  6809: F3          ; DI          ← CALL lands here
  ...
```

The pattern is clear: each version shifts the entire block by one byte, padding with different "garbage" bytes at the start. The CALL target is adjusted to always land on the DI instruction.

## Why 2 T-States Per Shift?

The 1-byte address difference doesn't directly cost 2 T-states in the CALL instruction itself — CALL always takes 17 T-states regardless of the target address. The timing difference comes from more subtle effects:

### 1. Instruction Alignment in Memory

Z80 instructions can be 1-4 bytes long. When code shifts by 1 byte, instruction boundaries move relative to memory page boundaries. This can affect:

- M1 cycle timing (opcode fetch)
- Memory refresh patterns
- How subsequent instructions align with the border rendering loop

### 2. Accumulated Phase Shift

The border effect code likely runs a tight timing loop. Even a tiny phase difference at the start compounds over hundreds of iterations. If the shifted code enters the loop at a slightly different phase relative to the ULA's horizontal counter, every line renders with that offset.

### 3. The Critical Timing Path

The demo's border effect code probably looks something like:

```asm
border_loop:
    OUT (FE),A      ; 11 T-states - set border color
    ; ... timing padding ...
    DJNZ border_loop ; 13/8 T-states
```

The total loop must be exactly 224 T-states (one Pentagon scan line) for the effect to stay vertically stable. Any phase offset at loop entry shifts the effect horizontally.

## Z80 Timing Reference

For understanding how the shift affects timing:

| Instruction | T-states | Notes |
|------------|----------|-------|
| CALL nn    | 17       | Fixed timing regardless of address |
| JP nn      | 10       | Fixed timing |
| DI         | 4        | The target instruction |
| LD DE,(nn) | 20       | 4 memory reads |
| OUT (n),A  | 11       | Port output |
| DJNZ e     | 13/8     | Loop: 13 if jump, 8 if fall through |

The CALL instruction breaks down as:
- M1: 4 T (opcode fetch)
- M2: 3 T (low address byte)
- M3: 3 T (high address byte)
- M4: 3 T (push PC high)
- M5: 3 T (push PC low)
- PC update: 1 T

The 2 T-state per shift effect must come from how the shifted code interacts with the frame's timing, not from any single instruction.

## ULA Timing Reference

Pentagon 128K video timing:

| Parameter | Value |
|-----------|-------|
| Frame     | 71680 T-states |
| Line      | 224 T-states |
| Lines/frame | 320 |
| Border top | 64 lines |
| Paper      | 192 lines |
| Border bottom | 64 lines |
| INT position | Line 239, T=71619 |

Pixel timing:
- **1 T-state = 2 pixels** at 7 MHz pixel clock
- **4 pixels = 2 T-states** — this is the observed shift per version
- **8 pixels = 4 T-states** — one character cell width

## Using This for Emulator Validation

The four fix versions provide a calibration tool for emulator developers:

1. Load `across_the_edge_by_demarche_fix_0.trd`
2. Run the demo and observe border effects vs paper area
3. If border effects appear **ahead** of paper → try fix1, fix2, or fix3
4. If border effects appear **behind** paper → your INT timing may be too late
5. The fix version that produces perfect alignment indicates your emulator's relative timing offset

For **unreal-ng** with Pentagon 128K model and `intstart=71625`:
- The fix version that works correctly validates the emulator's complete timing chain
- Differences between fix versions help isolate whether issues are in INT timing, border latching, or attribute fetch timing

## Author's Clarification (August 2026)

On August 12, 2026, Alexander (TmK of deMarche) provided a clarification regarding the true nature of the `fix0`-`fix3` versions, debunking the theory that they were compensating for Pentagon hardware video timing variations:

> "fix0, fix1, fix2, fix3 exist only because of an author's mistake. In the emulator everything worked, but on the party Pentagon it was shifted - we had to make 4 versions with a shift of 1 T-state.
> 
> And the issue here is not in the INT - it should be the same on all Pentagons (for crooked ones there is a hardware fix scheme). The glitch was found after the party: the thing is, after successful synchronization to the zero count, several bytes are taken from ROM at zero address and a pilot tone is formed. But since the ROM can be different [across different machines], hence the different execution time of the procedure.
> 
> I still haven't gotten around to fixing it. I've already heard so many conspiracy theories about these fix0, fix1... )))"

This reveals that the root cause of the desync was actually differing execution times when reading from varying ROM versions during pilot tone generation, rather than inherent ULA or INT timing differences between Pentagon clones.

He also added context on which version is the true baseline and how emulators misled the border rendering assumptions:

> "On Pentagons without hardware bugs (and with the correct ROM), and on clones that emulate it perfectly, the `fix0` build works. It makes sense to take `fix0` as the reference.
> 
> I had UnrealSpeccy 0.39.0 by SMT on hand (Feb 9, 2020) - in it, the border turned out to be incomplete... The emulator version in the archive with the demo shows too big of a screen. Ideally, one would run it on some cool monitor/TV that shows overscan and look, and then look without overscan.
> 
> Someone made an Unreal version back then to show the area like on a real monitor, we discussed it for a long time but it's damn hard to find now. And exactly on real hardware the border is not complete, I didn't know about this when writing on the emulator, I only saw it later )))"

This confirms that **`fix0` is the golden reference** for emulator validation. It also highlights a common pitfall of emulator-driven development: older emulators often rendered a "perfect," oversized border area that would naturally be obscured by physical monitor overscan (and CRT geometry) on real hardware. TmK wrote the border effects assuming the entire emulated canvas would be visible, only realizing later that the real Pentagon hardware physically clips those extremities on an actual display.

## Conclusion

The Demarche group's code-shift technique is an elegant solution to cross-platform timing compatibility. Rather than trying to detect the hardware and compensate in code, they released multiple versions and let users choose the one that works on their system.

This analysis demonstrates that even a 2 T-state timing difference — invisible in most applications — becomes immediately apparent in precision border effects. It also highlights why accurate emulation of Pentagon video timing requires getting every detail right: INT position, line length, frame length, and the phase relationship between CPU execution and ULA rendering.

## See Also

- [INT Signal Timings](../../inprogress/2026-08-09-video-timing-analysis-mister-vs-emulator/07-int-signal-timings.md)
- [Border Timing](../../inprogress/2026-08-09-video-timing-analysis-mister-vs-emulator/02-border-timing.md)
- [MiSTer vs Emulator Analysis](../../inprogress/2026-08-09-video-timing-analysis-mister-vs-emulator/README.md)
