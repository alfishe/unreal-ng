# Z80 Block I/O Instructions - Flag Fix Summary

**Date**: 2026-01-28  
**Status**: ✅ All z80tests passing

## Overview

This document describes the fixes applied to the Z80 block I/O instructions (INI, IND, OUTI, OUTD and their repeating variants INIR, INDR, OTIR, OTDR) to pass both:

1. **z80tests** (Woodmass) - Hardware-captured CRC verification
2. **Z80 Block Flags Test v5.0** (Ped7g) - David Banks undocumented flag behavior

## Problem Summary

The original implementation used simplified flag calculations (`dec8()`) that didn't match real Z80 hardware behavior for the undocumented XF, YF, HF, and PF flags in block I/O operations.

## Solution

Two different flag implementations were required:

| Instruction Type | Implementation Style | Reference |
|-----------------|---------------------|-----------|
| Single-iteration (INI, IND, OUTI, OUTD) | Xpeccy-style | [Xpeccy emulator](file:///Volumes/TB4-4Tb/Projects/emulators/github/Xpeccy/src/libxpeccy/cpu/Z80/z80ed.c) |
| Repeating (INIR, INDR, OTIR, OTDR) | David Banks full | [hoglet67/Z80Decoder](https://github.com/hoglet67/Z80Decoder/wiki/Undocumented-Flags) |

---

## Single-Iteration Instructions (INI, IND, OUTI, OUTD)

### Flag Calculation

```cpp
// T value calculation
// INI:  T = M + ((C + 1) & 0xFF)
// IND:  T = M + ((C - 1) & 0xFF)
// OUTI: T = M + L  (L after increment)
// OUTD: T = M + L  (L after decrement)

// Flags
cpu->f = (b_out & (SF|F3|F5));  // SF, XF, YF from B
if (!b_out) cpu->f |= ZF;       // ZF = (B == 0)
if (value & 0x80) cpu->f |= NF; // NF = M.7
if (t > 255) cpu->f |= (CF|HF); // CF = HF = (T > 255)

// PF = parity((T & 7) ^ B)
uint8_t pf = (t & 7) ^ b_out;
pf ^= pf >> 4;
pf ^= pf >> 2;
pf ^= pf >> 1;
if (!(pf & 1)) cpu->f |= PV;
```

### Key Insights

- **XF and YF** come directly from bits 3 and 5 of the B register (not complex calculations)
- **NF** = bit 7 of the port/memory value (M.7)
- **T value** differs between input (uses C±1) and output (uses L) instructions

---

## Repeating Instructions (INIR, INDR, OTIR, OTDR)

### Full David Banks Implementation

The repeating versions require more complex flag calculations for HF and PF:

```cpp
if (b_out) {  // B != 0 (will repeat)
    uint8_t sf = (b_out & 0x80) ? SF : 0;
    uint8_t hf = 0;
    uint8_t pf;

    if (cf) {
        if (value & 0x80) {
            // NF=1 (subtract case): Balu = B - 1
            uint8_t balu = b_out - 1;
            hf = ((b_out & 0x0F) == 0) ? HF : 0;
            pf = ((t & 7) ^ b_out ^ (balu & 7));
        } else {
            // NF=0 (add case): Balu = B + 1
            uint8_t balu = b_out + 1;
            hf = ((b_out & 0x0F) == 0x0F) ? HF : 0;
            pf = ((t & 7) ^ b_out ^ (balu & 7));
        }
    } else {
        pf = ((t & 7) ^ b_out ^ (b_out & 7));  // Balu = B
    }
    
    // Calculate parity
    pf ^= pf >> 4;
    pf ^= pf >> 2;
    pf ^= pf >> 1;
    pf = (pf & 1) ? 0 : PV;

    cpu->f = sf | pf | hf | nf | cf;
}
```

### Additional Considerations

- **XF/YF during repeat**: Set from PC+1 high byte when instruction repeats
- **PV flag timing**: Must be set BEFORE `cputact()` timing call for correct behavior

---

## Files Modified

| File | Functions Modified |
|------|-------------------|
| [op_ed.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/cpu/op_ed.cpp) | `ope_A2` (INI), `ope_A3` (OUTI), `ope_AA` (IND), `ope_AB` (OUTD), `ope_B2` (INIR), `ope_B3` (OTIR), `ope_BA` (INDR), `ope_BB` (OTDR) |

---

## Test Results

### Before Fix
```
INI   exp=0x45C2BF9A got=0x0C9EDE87 ❌
IND   exp=0xA349E955 got=0x0C9EDE87 ❌
OUTI  exp=0xF0C58202 got=0x949C4A27 ❌
OUTD  exp=0xFA1AD03E got=0xF4CF97D8 ❌
INIR  exp=0x95F331A2 got=0xA5772287 ❌
INDR  exp=0x845343FF got=0xA5772287 ❌
OTIR  exp=0x1A975ED3 got=0x8F3E4A77 ❌
OTDR  exp=0xB611C16F got=0xEA160076 ❌
```

### After Fix
```
INI   ✅ PASS
IND   ✅ PASS
OUTI  ✅ PASS
OUTD  ✅ PASS
INIR  ✅ PASS
INDR  ✅ PASS
OTIR  ✅ PASS
OTDR  ✅ PASS
```

---

## References

1. **David Banks Z80 Decoder Wiki**: https://github.com/hoglet67/Z80Decoder/wiki/Undocumented-Flags
2. **Xpeccy Emulator**: `/Volumes/TB4-4Tb/Projects/emulators/github/Xpeccy/src/libxpeccy/cpu/Z80/z80ed.c`
3. **Verified z80 Emulator**: `/Volumes/TB4-4Tb/Projects/emulators/github/z80/z80.c`
4. **z80tests Suite**: Mark Woodmass Z80 verification tests
5. **Z80 Block Flags Test v5.0**: Ped7g comprehensive block instruction test
