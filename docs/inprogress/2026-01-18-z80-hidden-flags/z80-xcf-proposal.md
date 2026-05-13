# Z80 XCF (CCF/SCF) Undocumented Flag Behavior - Implementation

## ✅ Result: Zilog Z80 Flavor Detected

The XCF Flavor v1.6 test now correctly identifies our emulator as **"Zilog flavor"**.

---

## Background: The Q Register

The Z80 contains an internal register called **Q** (not accessible to programmers) that affects `CCF` and `SCF` undocumented flag behavior. Research by Manuel Sainz de Baranda y Goñi (2022-2024) documented this mechanism.

### Q Register Behavior
- **Flag-modifying instructions:** Q = F & 0x28 (captures YF/XF bits)
- **Non-flag-modifying instructions:** Q = 0

### CCF/SCF Formula (Zilog)
```
undocumented_flags = (A | (F & ~Q)) & 0x28
```

**Key insight:** When Q equals F (both have same YF/XF), only A contributes. When Q differs from F, the F bits also contribute.

---

## Implementation Summary

### Files Modified

| File | Changes |
|------|---------|
| [z80.h](core/src/emulator/cpu/z80.h) | Added `uint8_t q` to `Z80Registers` with documentation |
| [z80.cpp](core/src/emulator/cpu/z80.cpp) | Constructor and Reset() init; conditional Q update in Z80Step() |
| [op_noprefix.cpp](core/src/emulator/cpu/op_noprefix.cpp) | SCF/CCF using `(A | (F & ~Q)) & 0x28` |
| [loader_sna.cpp](core/src/loaders/snapshot/loader_sna.cpp) | Zero memptr and q on load |
| [loader_z80.cpp](core/src/loaders/snapshot/loader_z80.cpp) | Zero memptr and q on load |

### Key Implementation Details

#### Q Register Update Logic ([z80.cpp](core/src/emulator/cpu/z80.cpp#L260-L277))
```cpp
// Save F before opcode
uint8_t prev_f = cpu.f;

// Execute opcode
(normal_opcode[opcode])(&cpu);

// Update Q based on whether flags changed
if (cpu.f != prev_f)
    cpu.q = cpu.f & 0x28;  // Flags changed: capture YF/XF
else
    cpu.q = 0;  // Flags unchanged: Q=0
```

#### CCF/SCF Implementation ([op_noprefix.cpp](core/src/emulator/cpu/op_noprefix.cpp#L425-L432))
```cpp
Z80OPCODE op_37(Z80 *cpu) { // scf - Zilog Z80 behavior
    // When Q=F, only A contributes; when Q≠F, F also contributes
    uint8_t undoc = (cpu->a | (cpu->f & ~cpu->q)) & (F3 | F5);
    cpu->f = (cpu->f & ~(HF | NF | F3 | F5)) | undoc | CF;
}
```

---

## XCF Flavor Test Results

| Case (Q<>F)\|A | Zilog Expected | Our Result |
|---------------|----------------|------------|
| (0<>0)\|0     | 00             | 00 ✅      |
| (0<>1)\|0     | 11             | 11 ✅      |
| (1<>1)\|0     | 00             | 00 ✅      |
| (0<>0)\|1     | 11             | 11 ✅      |
| (0<>1)\|1     | 11             | 11 ✅      |
| (1<>1)\|1     | 11             | 11 ✅      |

---

## Alternative Z80 Variants (Not Implemented)

For reference, other Z80 clones have different behavior:

| Variant | Formula | Notes |
|---------|---------|-------|
| **Zilog** (implemented) | `(A \| (F & ~Q)) & 0x28` | Original, Q-dependent |
| NEC NMOS | `A & 0x28` | Ignores F and Q entirely |
| ST CMOS | Asymmetric YF/XF | Different formulas per bit |

---

## References

1. **Z80 XCF Flavor Test v1.6** - Manuel Sainz de Baranda y Goñi (https://zxe.io)(https://github.com/redcode/Z80_XCF_Flavor)
2. **MEMPTR Documentation** - Boo-boo, Vladimir Kladov (zx.pk.ru, 2006)
3. **z80_last_secrets.pdf** - Included in this folder
