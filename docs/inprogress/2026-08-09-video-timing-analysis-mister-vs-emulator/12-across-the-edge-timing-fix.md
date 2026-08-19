# Across The Edge Timing Fix Analysis

Demo: "Across The Edge" by Demarche
Files: `across_the_edge_by_demarche_fix_0.trd` through `across_the_edge_by_demarche_fix_3.trd`

## Summary

The demo provides 4 TRD variants with different timing compensation values to synchronize border effects with paper rendering across different Pentagon clones and emulators.

**Key finding**: Each fix version shifts by 2 T-states (4 pixels), controlled by a 1-byte code shift mechanism.

## Timing Adjustment Mechanism

### Code Shift Technique

The demo uses a clever technique: the entire ACROSSLK.C code block is shifted by 1 byte between versions. This affects:

1. **CALL target addresses** change: `6806` → `6807` → `6808` → `6809`
2. **LD A,(xxxx) addresses** change: `67D0` → `67D1` → `67D2` → `67D3`

### How It Works

The loader code includes:
```asm
    CALL 6806h    ; fix0
    ; or CALL 6807h (fix1), CALL 6808h (fix2), CALL 6809h (fix3)
```

All four addresses point to the same instruction (DI) because the entire code block is shifted:

| Fix | CALL Target | Byte at Target |
|-----|-------------|----------------|
| 0   | 6806h       | F3 (DI)        |
| 1   | 6807h       | F3 (DI)        |
| 2   | 6808h       | F3 (DI)        |
| 3   | 6809h       | F3 (DI)        |

The timing difference comes from the shifted alignment affecting instruction fetch timing from contended memory during the frame.

### Measured Timing Differences

| Transition    | Pixel Shift | T-State Shift |
|---------------|-------------|---------------|
| fix0 → fix1   | 4 pixels    | 2 T-states    |
| fix1 → fix2   | 4 pixels    | 2 T-states    |
| fix2 → fix3   | 4 pixels    | 2 T-states    |

**Total range**: 12 pixels (6 T-states) between fix0 and fix3

## File Differences

Only 2 byte positions show consistent incremental changes across all 4 files:

1. **Offset 0x1113** (memory 0x6213): CALL address low byte
   - fix0: `06`, fix1: `07`, fix2: `08`, fix3: `09`

2. **Offset 0xBC7E**: LD A,(xxxx) address low byte  
   - fix0: `D0`, fix1: `D1`, fix2: `D2`, fix3: `D3`

## Emulator Calibration

If your emulator shows border effects misaligned with paper:

1. Start with `fix0` (most common baseline)
2. If border effects are **ahead** of paper → try higher fix numbers
3. If border effects are **behind** paper → your INT timing may be too late

For unreal-ng with Pentagon model and `intstart=71625`:
- Test which fix version produces aligned border/paper effects
- The working fix number indicates your emulator's relative timing offset

## Technical Details

### Why 2 T-states per 1-byte shift?

The 1-byte shift doesn't directly add 2 T-states. Instead, it affects:

1. Memory contention patterns during code fetch
2. Instruction alignment affecting subsequent M-cycles
3. The cumulative effect over the frame's critical timing path

The demo likely uses a tight timing loop where even small alignment changes cascade through contention delays.

### ULA Timing Reference

- 1 T-state = 2 pixels (at 3.5 MHz)
- 4 pixels = 2 T-states
- Pentagon: 71680 T-states/frame, 224 T-states/line
