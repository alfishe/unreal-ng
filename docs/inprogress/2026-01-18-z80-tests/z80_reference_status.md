# Z80 Reference Implementation Status

**Date**: 2026-01-19  
**Last Updated**: 01:38 EST

## Current State Summary

| Metric | Value |
|--------|-------|
| **Total Tests** | 163 |
| **Passing** | 110 |
| **Failing** | 53 |
| **No Zilog Ref** | 0 (all have refs) |
| **Pass Rate** | 67.5% |

## Files Involved

| File | Purpose |
|------|---------|
| `core/tests/z80/z80test/z80_reference.py` | Python Z80 instruction executor |
| `core/tests/z80/z80test/z80test_generator.py` | Test vector iterator & CRC calculator |
| `core/tests/z80/z80test/extract_z80tests.py` | Parser for `tests.asm` |
| `core/tests/z80/z80test/scf_unwrapper.py` | SCF-specific test (✅ CRC MATCH) |
| `testdata/z80/z80test-1.2a/src/tests.asm` | Original z80test vectors |

## Passing Test Categories (110 tests)

- ✅ **SCF/CCF** - All 8 variants (Zilog, NEC, ST, combos)
- ✅ **DAA, CPL, NEG** - Special flag operations
- ✅ **ALU immediate** - ADD/ADC/SUB/SBC/AND/XOR/OR/CP A,N
- ✅ **ALU register-register** - ALO A,[BC], ALO A,[DE], ALO A,[HL]
- ✅ **Accumulator rotates** - RLCA, RRCA, RLA, RRA
- ✅ **RLD, RRD** - Nibble rotates
- ✅ **CB rotates on A** - RLC A, RRC A, RL A, RR A, SLA A, SRA A, SLIA A, SRL A
- ✅ **16-bit INC/DEC** - INC RR, DEC RR, INC XY, DEC XY
- ✅ **ADD HL,RR** - 16-bit add to HL

## Failing Test Categories (53 tests)

### Pattern: Memory operand `(HL)` tests
- ❌ `ALO A,(HL)` - expected 0xC0F1F3D4, got 0x6BD779FC
- ❌ `RLC/RRC/RL/RR/SLA/SRA/SLIA/SRL [R,(HL)]` - 8 tests

### Pattern: Indexed `(XY)` tests
- ❌ `ALO A,(XY)` - expected 0x0688D4D0, got 0xB539C72B
- ❌ `SRO (XY)`, `SRO (XY),R`
- ❌ `INC (XY)`, `DEC (XY)`
- ❌ `BIT N,(XY)`, `BIT N,(XY),-`

### Pattern: IX/IY register operations
- ❌ `ADD IX,RR` - expected 0x38891C0B, got 0x13BAA10D
- ❌ `ADD IY,RR` - expected 0x38891C0B, got 0xCED8EEAD
- ❌ `INC X`, `DEC X` (IXH/IXL/IYH/IYL)

### Pattern: 8-bit INC/DEC with register selection
- ❌ `INC [R,(HL)]` - expected 0x6D329BB5, got 0x36C23614
- ❌ `DEC [R,(HL)]` - expected 0xE2207AF8, got 0x69FBA945

### Pattern: BIT tests
- ❌ `BIT N,(HL)`, `BIT N,[R,(HL)]`

### Pattern: Block operations
- ❌ `LDI`, `LDD`, `LDIR`, `LDDR`, etc.
- ❌ `CPI`, `CPD`, `CPIR`, `CPDR`
- ❌ `INI`, `IND`, `INIR`, `INDR` (possibly I/O input related)
- ❌ `OUTI`, `OUTD`, `OTIR`, `OTDR`

## Root Cause Analysis (In Progress)

### Confirmed Working
The iteration engine and CRC calculation are correct:
- SCF test: 7168 iterations, CRC 0x3EC05634 ✅ MATCH
- Iteration counts verified for all 163 tests

### Suspected Issues in `z80_reference.py`

1. **Operand extraction for `[R,(HL)]` tests**
   - Tests vary opcode bits to select which register (B,C,D,E,H,L,(HL),A)
   - `sro_r()`, `inc_r()`, `dec_r()` may extract operand incorrectly

2. **Indexed register operations**
   - `inc_x()`, `dec_x()` read from wrong byte in vector
   - `add_ix_rr()`, `add_iy_rr()` may use wrong RR index

3. **Memory operand in XY tests**  
   - `alu_xy_mem()` needs to read opcode from `v[2]` not `v[0]`
   - `sro_xy()` needs to read opcode from `v[3]`

4. **BIT tests on memory**
   - `bit_hl()`, `bit_xy()` - XY flags may need MEMPTR high byte

## Vector Layout Reference

```
Bytes 0-3:   [opcode0, opcode1, opcode2, opcode3]
Byte 4:      F (flags)
Byte 5:      A (accumulator)  
Bytes 6-7:   C, B (BC register pair, little endian)
Bytes 8-9:   E, D (DE register pair)
Bytes 10-11: L, H (HL register pair)
Bytes 12-13: XL, XH (IX register)
Bytes 14-15: YL, YH (IY register)
Bytes 16-17: MEMlo, MEMhi (memory value, not address!)
Bytes 18-19: SPlo, SPhi (stack pointer)
```

## Test Commands

```bash
cd /Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/tests/z80/z80test

# Quick status check
python3 -c "
from extract_z80tests import extract_tests, TESTS_ASM
from z80test_generator import run_test

tests = extract_tests(TESTS_ASM)
tests = [t for t in tests if len(t.get('vecs', [])) >= 3]

m, f = 0, []
for t in tests:
    r = run_test(t, verbose=False)
    if not r.get('has_reference'): continue
    if r.get('crc_match'): m += 1
    else: f.append(t.get('name', ''))

print(f'Pass: {m}, Fail: {len(f)}')
for n in f[:20]: print(f'  {n}')
"

# Test specific instruction
python3 -c "
from extract_z80tests import extract_tests, TESTS_ASM
from z80test_generator import run_test

tests = extract_tests(TESTS_ASM)
for t in tests:
    if t.get('name') == 'ALO A,(HL)':
        run_test(t, verbose=True)
        
cd /Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/tests/z80/z80test && python3 << 'EOF'
from extract_z80tests import extract_tests, TESTS_ASM
from z80test_generator import run_test
tests = extract_tests(TESTS_ASM)
tests = [t for t in tests if len(t.get('vecs', [])) >= 3]
passed, failed = 0, []
for t in tests:
    name = t.get('name', '')
    r = run_test(t, verbose=False)
    if not r.get('has_reference'):
        continue  # Skip tests without reference
    if r.get('crc_match'):
        passed += 1
    else:
        failed.append((name, r['expected_crc_allflags'], r.get('output_flags_crc', 0)))

print(f"PASSED: {passed}")
print(f"FAILED: {len(failed)}")
print(f"TOTAL:  {passed + len(failed)}")
print()
print("=" * 60)
print("FAILING TESTS:")
print("=" * 60)
for name, exp, got in failed:
    print(f"  {name:40s} exp=0x{exp:08X} got=0x{got:08X}")
EOF



PASSED: 114
FAILED: 49
TOTAL:  163

============================================================
FAILING TESTS:
============================================================
  ALO A,(HL)                               exp=0xC0F1F3D4 got=0x6BD779FC
  ALO A,(XY)                               exp=0x0688D4D0 got=0xB539C72B
  RLC [R,(HL)]                             exp=0xA9CF462B got=0x190A43D5
  RRC [R,(HL)]                             exp=0x46951078 got=0xC75D01CE
  RL [R,(HL)]                              exp=0x39619CCF got=0x851990CC
  RR [R,(HL)]                              exp=0x92FB276B got=0xEA3671FB
  SLA [R,(HL)]                             exp=0xF40A2FA5 got=0x8EAFC81D
  SRA [R,(HL)]                             exp=0x8F46E5AF got=0x31D69DF3
  SLIA [R,(HL)]                            exp=0x1A61A672 got=0xE4D780E7
  SRL [R,(HL)]                             exp=0xE3E1480C got=0xB73BEB40
  SRO (XY)                                 exp=0xC76B6FB8 got=0x178DB054
  SRO (XY),R                               exp=0xD88FDE47 got=0x93EC92AE
  INC [R,(HL)]                             exp=0x6D329BB5 got=0x36C23614
  DEC [R,(HL)]                             exp=0xE2207AF8 got=0x69FBA945
  INC (XY)                                 exp=0xDCA01A72 got=0xD52748C8
  DEC (XY)                                 exp=0xF0C6BA74 got=0xF941E8CE
  BIT N,(HL)                               exp=0xF5D39111 got=0x6F06A835
  BIT N,[R,(HL)]                           exp=0x80F58ECA got=0x9BAC57CA
  BIT N,(XY)                               exp=0x69D8B807 got=0x0C308880
  BIT N,(XY),-                             exp=0xB99E5A2E got=0x6008CA9A
  LDI                                      exp=0x74905A21 got=0xC16C285B
  LDD                                      exp=0x74905A21 got=0xC16C285B
  LDIR                                     exp=0x62A5E441 got=0xB29AB65E
  LDDR                                     exp=0x4650D4E7 got=0x1F525FE3
  LDIR->NOP'                               exp=0x97C3BACF got=0x8D0AFDC9
  LDDR->NOP'                               exp=0x97C3BACF got=0x8D0AFDC9
  CPI                                      exp=0xB051C618 got=0xD575B15A
  CPD                                      exp=0xB051C618 got=0xD575B15A
  CPIR                                     exp=0x7FE4A6E0 got=0x0DB41365
  CPDR                                     exp=0x76CC7038 got=0x72A9EFC9
  IN R,(C)                                 exp=0x61F21A52 got=0x91D81D55
  IN (C)                                   exp=0x8F4B242F got=0xA8B29E4D
  INI                                      exp=0x45C2BF9A got=0x0C9EDE87
  IND                                      exp=0xA349E955 got=0x0C9EDE87
  INIR                                     exp=0x95F331A2 got=0xA5772287
  INDR                                     exp=0x845343FF got=0xA5772287
  INIR->NOP'                               exp=0x9D0CADCE got=0x573816C3
  INDR->NOP'                               exp=0x739789B5 got=0x573816C3
  OUTI                                     exp=0xF0C58202 got=0x949C4A27
  OUTD                                     exp=0xFA1AD03E got=0xF4CF97D8
  OTIR                                     exp=0x1A975ED3 got=0x8F3E4A77
  OTDR                                     exp=0xB611C16F got=0xEA160076
  PUSH+POP RR                              exp=0xDAC88897 got=0x6B93B22F
  POP+PUSH AF                              exp=0x43219C3C got=0x0641B703
  LD [R,(HL)],[R,(HL)]                     exp=0x8CC99857 got=0x823DCF29
  LD [X,(XY)],[X,(XY)]                     exp=0x02D51675 got=0xA25830E1
  LD R,(XY)                                exp=0x322904D3 got=0x7B04B1B3
  LD (XY),R                                exp=0x322904D3 got=0x7B04B1B3
  LD A,R                                   exp=0x7A32E3F5 got=0xDEDE7754

cd /Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/tests/z80/z80test && python3 z80test_generator.py --v
erify 2>&1 | tail -30
Parsing /Volumes/TB4-4Tb/Projects/Test/unreal-ng/testdata/z80/z80test-1.2a/src/tests.asm...
Found 163 complete tests

Verifying test iteration counts...
----------------------------------------------------------------------
----------------------------------------------------------------------
Passed: 163, Failed: 0



## External Reference

The emulator at `/Volumes/TB4-4Tb/Projects/emulators/github/z80/z80.c` claims to pass all zexdoc and zexall tests. It contains a clean, minimal Z80 implementation that could be used as a reference for correct flag behavior.

## Next Steps

1. **Fix operand extraction** in `z80_reference.py` for register-selection tests
2. **Fix indexed addressing** handlers (INC X, ADD IX,RR, etc.)
3. **Fix block operations** (LDI/LDD/CPI/CPD families)
4. **Verify against external z80.c** for correct flag calculations
