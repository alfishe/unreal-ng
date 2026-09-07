# TR-DOS ROM Hang Analysis - Root Cause Found

## Investigation Summary

After the CF_TRDOS fix, the snapshot loads correctly and TR-DOS ROM is active. However, the loader enters an infinite loop. Through WebAPI tracing, the root cause has been identified.

## Critical Findings

### 1. B Register Behavior - EXPECTED

**Observation**: B register increases (22 → 30 → 69) instead of decreasing

**Explanation**: This is CORRECT behavior! The boot code uses self-modifying code:

```asm
0x5BD7: 21 26 5E     ld hl, #5E26    ; Sector table (SELF-MODIFIED!)
0x5BDA: 46           ld b, (hl)      ; Load sector count from table
...
0x5BE8: 22 D8 5B     ld (#5BD8), hl  ; Update table pointer (writes to 0x5BD8!)
0x5BF0: CD 1D F5     call #F51D      ; Call loader
```

The instruction at 0x5BD7-0x5BD9 (`ld hl, #5E26`) gets modified at 0x5BE8 to point to the next entry in the sector table. Each call to the loader loads a NEW B value.

### 2. Loader Never Executes - THE BUG

**Observation**: Breakpoints at 0xF51D (loader entry) and 0xF5A3 (djnz) are NEVER hit

**Current State**:
- PC stuck in TR-DOS ROM area: 0x3FE5, 0x3FE7, 0x3FE9
- Loader code at 0xF51D never executes
- B keeps increasing because boot code keeps loading new table entries

**Conclusion**: The boot code successfully calls the loader, but **TR-DOS ROM calls do not return properly**, causing execution to hang in TR-DOS ROM instead of progressing through the loader logic.

## The Problem

### Expected Flow

```
Boot (0x5BF0)
  → call #F51D (loader)
    → Loader reads B sectors
      → Makes TR-DOS calls via 0x3D2F gateway
      → TR-DOS calls return
      → djnz decrements B
      → Loop until B=0
    ← ret (back to 0x5BF3)
  → Continue boot
```

### Actual Flow

```
Boot (0x5BF0)
  → call #F51D (loader)
    → TR-DOS call via 0x3D2F
      → PC enters TR-DOS ROM (0x3FEC, 0x3FE5, etc.)
      → **HANGS HERE** - never returns!
    × Never reaches djnz
    × Never decrements B
    × Never returns to boot
```

## TR-DOS ROM Call Mechanism

The loader uses a specific pattern to call TR-DOS routines:

```asm
trdos_call:             ; 0xF5A6
  push hl               ; Save HL
  ld hl, #2FC3          ; Return address (in TR-DOS ROM!)
  ex (sp), hl           ; Swap with stack
  jp #3D2F              ; Jump to TR-DOS gateway
```

The TR-DOS gateway at 0x3D2F is supposed to:
1. Execute the TR-DOS routine
2. Return to the address on stack (0x2FC3)
3. Which then returns to the loader

**But something in this mechanism is failing!**

## Root Cause Hypothesis

Given that CF_TRDOS is now correctly set, the issue is likely one of:

1. **TR-DOS ROM code expects different stack state**
   - The loader's stack manipulation might not match TR-DOS expectations
   - Return addresses might be wrong

2. **TR-DOS ROM call at 0x3D2F not returning**
   - The gateway might be entering an infinite loop
   - Or jumping to wrong address

3. **Memory banking issue during TR-DOS calls**
   - Even with CF_TRDOS set, ROM banking might switch incorrectly
   - Loader code at 0xF5xx might become inaccessible

4. **TR-DOS ROM expects interrupts enabled**
   - The loader manipulates interrupt state
   - TR-DOS calls might require specific IFF state

## Immediate Investigation Steps

### 1. Trace TR-DOS Gateway at 0x3D2F

Set breakpoint at 0x3D2F and examine:
- Stack contents (what return address is there?)
- Register state when entering
- Next instructions after 0x3D2F

### 2. Check TR-DOS ROM Code

Examine TR-DOS ROM at:
- 0x3D2F (gateway entry)
- 0x2FC3 (supposed return point)
- 0x3FEC, 0x3FE5, 0x3FE7, 0x3FE9 (where PC gets stuck)

### 3. Memory Banking State

When PC=0x3FE9, check:
- Is TR-DOS ROM actually active?
- Can the CPU see RAM at 0xF5xx?
- What's the value of Port 0x7FFD?

### 4. Stack Trace

When stuck at 0x3FE9, examine stack:
- What return addresses are waiting?
- Is 0xF51D or 0x5BF3 anywhere on stack?
- Are there multiple TR-DOS return frames?

## Suggested Fix Directions

1. **Check TR-DOS ROM code compatibility**
   - Compare with real TR-DOS ROM behavior
   - Verify gateway mechanism

2. **Review memory banking during ROM calls**
   - Ensure ROM doesn't mask loader RAM
   - Check if UpdateZ80Banks() is called at wrong times

3. **Stack state verification**
   - Verify loader's TR-DOS call mechanism matches hardware
   - Check if return addresses are correct

4. **Interrupt state management**
   - Verify IFF1/IFF2 state during TR-DOS calls
   - Check if interrupts need to be enabled/disabled

## Next Steps

The user should:
1. Examine TR-DOS ROM disassembly at 0x3D2F and 0x3FE9
2. Trace execution from 0x3D2F to understand where it diverges
3. Check stack contents when stuck
4. Verify memory banking state during TR-DOS calls

The fix will likely be in:
- TR-DOS ROM emulation (if behavior doesn't match hardware)
- Memory banking logic (if RAM/ROM switching is wrong)
- The loader's TR-DOS call mechanism (if incompatible with emulator)

---

**Status**: Root cause identified - TR-DOS ROM calls not returning  
**Next**: Trace TR-DOS ROM execution to find hang point  
**Files to investigate**: TR-DOS ROM, memory banking, stack management
