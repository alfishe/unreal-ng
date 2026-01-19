# Z80Test Suite GTest Port - Implementation Proposal

## Overview

This proposal details porting the comprehensive **z80test-1.2a** CPU verification suite to a native C++ GTest framework. The original test suite runs on a ZX Spectrum and verifies Z80 instruction behavior against CRCs captured from genuine Zilog Z80 hardware.

---

## Source: z80test-1.2a

**Location:** `testdata/z80/z80test-1.2a/`

**Author:** Patrik Rak (2012-2023)

**License:** MIT

### Available Test Variants

| File | Description |
|------|-------------|
| z80full.tap | Tests all flags and registers |
| z80doc.tap | All registers, documented flags only |
| z80flags.tap | All flags, ignores registers |
| z80docflags.tap | Documented flags only, ignores registers |
| z80ccf.tap | Tests flags after CCF following each instruction |
| z80memptr.tap | Tests flags after BIT N,(HL) for MEMPTR behavior |

---

## How z80test Works

### Test Vector Structure

Each test is defined by three vectors:

```
┌─────────────────────────────────────────────────────────────────┐
│ flags: bitmask for which flags are officially documented        │
├─────────────────────────────────────────────────────────────────┤
│ BASE VECTOR:                                                    │
│   - Instruction bytes (op1, op2, op3, op4)                      │
│   - Initial memory value                                        │
│   - Initial registers: A, F, BC, DE, HL, IX, IY, SP             │
├─────────────────────────────────────────────────────────────────┤
│ COUNTER VECTOR:                                                 │
│   - Bits to toggle for all 2^N combinations                     │
│   - Each bit position generates (base XOR counter_bit)          │
├─────────────────────────────────────────────────────────────────┤
│ SHIFTER VECTOR:                                                 │
│   - Bits to toggle sequentially (one at a time)                 │
│   - After all counter combinations, shift through shifter bits  │
├─────────────────────────────────────────────────────────────────┤
│ EXPECTED CRCs:                                                  │
│   - allflags: CRC of all 8 flag bits                            │
│   - all: CRC of all flags + registers                           │
│   - docflags: CRC of documented flags only                      │
│   - doc: CRC of documented flags + registers                    │
│   - ccf: CRC when testing with CCF postfix                      │
│   - mptr: CRC when testing with BIT for MEMPTR                  │
├─────────────────────────────────────────────────────────────────┤
│ NAME: Human-readable test name                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Example: SCF Test

```asm
.scf    flags   s,1,z,1,f5,0,hc,1,f3,0,pv,1,n,1,c,1
        vec     0x37,stop,0x00,0x00,mem,0x1234,a,0xaa,f,0xff,bc,0xbbcc,...
        vec     0x00,0x00,0x00,0x00,mem,0x0000,a,0x28,f,0xff,...  ; counter
        vec     0x00,0x00,0x00,0x00,mem,0x0000,a,0xd7,f,0x00,...  ; shifter
        crcs    allflags,0x3ec05634,all,0xd841bd8a,docflags,0xafbf608b,...
        name    "SCF"
```

**Execution flow:**
1. Start with base vector: A=0xAA, F=0xFF, ...
2. Toggle counter bits: A cycles through {0xAA, 0x82, 0xA8, 0x80, ...} (0x28 mask)
3. After all combinations, toggle shifter bits sequentially
4. For each combination: execute SCF, capture result, update CRC
5. Compare final CRC against expected 0x3ec05634

### CRC-32 Algorithm

The test uses a **reflected CRC-32** with polynomial 0xEDB88320:

```cpp
uint32_t crc32_update(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
        if (crc & 1)
            crc = (crc >> 1) ^ 0xEDB88320;
        else
            crc >>= 1;
    }
    return crc;
}
```

The CRC is computed over all 16 bytes of register/memory state after each instruction execution.

---

## Test Coverage

### Complete Test List (~120 tests)

#### Flag Manipulation (8 tests)
- SCF, CCF
- SCF (NEC), CCF (NEC) - skipped unless prior test fails
- SCF (ST), CCF (ST) - skipped unless prior test fails
- SCF+CCF, CCF+SCF

#### 8-bit Arithmetic (16 tests)
- DAA, CPL, NEG, NEG'
- ADD A,N / ADC A,N / SUB A,N / SBC A,N
- AND N / XOR N / OR N / CP N
- ALO A,A / ALO A,[B,C] / ALO A,[D,E] / ALO A,[H,L]
- ALO A,(HL) / ALO A,[HX,LX] / ALO A,[HY,LY] / ALO A,(XY)

#### Shift/Rotate Operations (18 tests)
- RLCA, RRCA, RLA, RRA
- RLD, RRD
- RLC A / RRC A / RL A / RR A / SLA A / SRA A / SLIA A / SRL A
- RLC [R,(HL)] / RRC [R,(HL)] / RL [R,(HL)] / RR [R,(HL)]
- SLA [R,(HL)] / SRA [R,(HL)] / SLIA [R,(HL)] / SRL [R,(HL)]
- SRO (XY) / SRO (XY),R

#### Increment/Decrement (12 tests)
- INC A / DEC A
- INC [R,(HL)] / DEC [R,(HL)]
- INC X / DEC X (IX/IY half-registers)
- INC (XY) / DEC (XY)
- INC RR / DEC RR
- INC XY / DEC XY

#### 16-bit Arithmetic (5 tests)
- ADD HL,RR / ADD IX,RR / ADD IY,RR
- ADC HL,RR / SBC HL,RR

#### BIT/SET/RES (15 tests)
- BIT N,A / BIT N,(HL) / BIT N,R / BIT N,(XY) / BIT N,(XY)'
- SET N,A / SET N,(HL) / SET N,R / SET N,(XY) / SET N,(XY)'
- RES N,A / RES N,(HL) / RES N,R / RES N,(XY) / RES N,(XY)'

#### Block Transfer (6 tests)
- LDI, LDD, LDIR, LDDR
- LDIR (nop), LDDR (nop) - verify no-op when BC=0

#### Block Compare (4 tests)
- CPI, CPD, CPIR, CPDR

#### Block I/O (13 tests)
- IN A,(N) / IN R,(C) / IN (C)
- INI, IND, INIR, INDR
- INIR (nop), INDR (nop)
- OUT (N),A / OUT (C),R / OUT (C),0
- OUTI, OUTD, OTIR, OTDR

#### Jumps (8 tests)
- JP NN / JP CC,NN / JP (HL) / JP (XY)
- JR N / JR CC,N / DJNZ N

#### Calls/Returns (9 tests)
- CALL NN / CALL CC,NN
- RET / RET CC / RETN / RETI / RETI+RETN

#### Stack Operations (3 tests)
- PUSH/POP RR / POP+PUSH AF / PUSH/POP XY

#### Exchange (5 tests)
- EX DE,HL / EX AF,AF' / EXX
- EX (SP),HL / EX (SP),XY

#### Load Instructions (19 tests)
- LD R,R' / LD X,X' / LD R,(XY) / LD (XY),R
- LD R,N / LD X,N / LD (XY),N
- LD A,(RR) / LD (RR),A / LD A,(NN) / LD (NN),A
- LD RR,NN / LD XY,NN
- LD HL,(NN) / LD XY,(NN) / LD RR,(NN)
- LD (NN),HL / LD (NN),XY / LD (NN),RR
- LD SP,HL / LD SP,XY

#### Special (4 tests)
- LD I,A / LD R,A / LD A,I / LD A,R
- EI/DI / IM N

---

## Implementation Plan

### Phase 1: Data Extraction

Create a Python script to parse `tests.asm` and generate C++ data:

```python
# extract_z80tests.py
# Parses tests.asm and generates z80test_vectors.h

# Output format:
struct Z80TestVector {
    const char* name;
    uint8_t opcode[5];      // Instruction bytes (0-terminated)
    uint8_t flags_mask;      // Which flags are documented
    
    // Base vector (20 bytes)
    uint8_t base_opcode[4];
    uint16_t base_mem;
    uint8_t base_a, base_f;
    uint16_t base_bc, base_de, base_hl;
    uint16_t base_ix, base_iy, base_sp;
    
    // Counter vector (same structure)
    // Shifter vector (same structure)
    
    // Expected CRCs
    uint32_t crc_allflags;
    uint32_t crc_all;
    uint32_t crc_docflags;
    uint32_t crc_doc;
    uint32_t crc_ccf;
    uint32_t crc_memptr;
    
    uint8_t precheck;  // 0=failcheck, 1=incheck, 255=nocheck
};
```

### Phase 2: Test Engine Implementation

Create the core test execution engine:

```cpp
// z80test_engine.h

class Z80TestEngine {
public:
    Z80TestEngine(Z80* cpu, Memory* memory);
    
    // Run exhaustive test and return CRC
    uint32_t RunTest(const Z80TestVector& vec, TestMode mode);
    
private:
    // Combinatorial vector generation
    void InitVectors(const Z80TestVector& vec);
    bool NextCombination();  // Returns false when done
    
    // Apply current vector to CPU state
    void ApplyVector();
    
    // Execute instruction and capture state
    void ExecuteAndCapture();
    
    // CRC update
    void UpdateCRC(uint32_t& crc, const uint8_t* data, size_t len);
    
    Z80* cpu_;
    Memory* memory_;
    
    // Working vector state
    uint8_t vector_[20];
    uint8_t counter_[20];
    uint8_t shifter_[20];
    uint8_t counter_mask_[20];
    uint8_t shifter_mask_[20];
    int shifter_pos_;
};
```

### Phase 3: GTest Integration

Create parametrized tests:

```cpp
// z80test_full.cpp

#include <gtest/gtest.h>
#include "z80test_vectors.h"
#include "z80test_engine.h"

class Z80FullTest : public ::testing::TestWithParam<Z80TestVector> {
protected:
    void SetUp() override {
        cpu_ = std::make_unique<Z80>();
        memory_ = std::make_unique<Memory>();
        engine_ = std::make_unique<Z80TestEngine>(cpu_.get(), memory_.get());
    }
    
    std::unique_ptr<Z80> cpu_;
    std::unique_ptr<Memory> memory_;
    std::unique_ptr<Z80TestEngine> engine_;
};

TEST_P(Z80FullTest, AllFlags) {
    auto& vec = GetParam();
    
    // Skip tests with failcheck if no prior failures
    if (vec.precheck == 0 /* failcheck */) {
        GTEST_SKIP() << "Skipped (failcheck)";
    }
    
    uint32_t crc = engine_->RunTest(vec, TestMode::AllFlags);
    EXPECT_EQ(crc, vec.crc_allflags) 
        << "Test: " << vec.name 
        << " CRC: 0x" << std::hex << crc 
        << " Expected: 0x" << vec.crc_allflags;
}

TEST_P(Z80FullTest, DocFlags) {
    auto& vec = GetParam();
    uint32_t crc = engine_->RunTest(vec, TestMode::DocFlags);
    EXPECT_EQ(crc, vec.crc_docflags) << "Test: " << vec.name;
}

INSTANTIATE_TEST_SUITE_P(
    Z80Test,
    Z80FullTest,
    ::testing::ValuesIn(kZ80TestVectors),
    [](const testing::TestParamInfo<Z80TestVector>& info) {
        return info.param.name;
    }
);
```

### Phase 4: Diagnostic Mode (Human-Readable Failures)

When a CRC mismatch occurs, provide detailed diagnostic output showing exactly what went wrong:

```cpp
// z80test_diagnostics.h

struct TestResult {
    uint8_t got_f, expected_f;
    uint8_t got_a, expected_a;
    uint16_t got_bc, got_de, got_hl;
    uint16_t got_ix, got_iy, got_sp;
    uint16_t got_mem;
    std::string input_description;  // "A=0xAA, F=0xFF, BC=0xBBCC, ..."
};

class Z80TestDiagnostics {
public:
    // Run test in diagnostic mode - finds first mismatch and reports it
    DiagnosticResult RunDiagnostic(const Z80TestVector& vec, TestMode mode);
    
    // Generate human-readable failure message
    static std::string FormatFailure(const TestResult& got, const TestResult& expected);
    
private:
    // Compare single execution result against reference
    bool CompareResult(const TestResult& got, const TestResult& expected, 
                       std::string& diff_description);
};

std::string Z80TestDiagnostics::FormatFailure(const TestResult& got, 
                                               const TestResult& expected) {
    std::ostringstream ss;
    
    // Flag analysis with bit names
    if (got.got_f != expected.expected_f) {
        uint8_t diff = got.got_f ^ expected.expected_f;
        ss << "Flags mismatch:\n";
        ss << "  Got:      " << FormatFlags(got.got_f) << "\n";
        ss << "  Expected: " << FormatFlags(expected.expected_f) << "\n";
        ss << "  Wrong bits: ";
        if (diff & 0x80) ss << "SF ";
        if (diff & 0x40) ss << "ZF ";
        if (diff & 0x20) ss << "YF(F5) ";
        if (diff & 0x10) ss << "HF ";
        if (diff & 0x08) ss << "XF(F3) ";
        if (diff & 0x04) ss << "PV ";
        if (diff & 0x02) ss << "NF ";
        if (diff & 0x01) ss << "CF ";
        ss << "\n";
    }
    
    // Register differences
    if (got.got_a != expected.expected_a)
        ss << "A: got 0x" << std::hex << (int)got.got_a 
           << ", expected 0x" << (int)expected.expected_a << "\n";
    if (got.got_bc != expected.got_bc)
        ss << "BC: got 0x" << got.got_bc << ", expected 0x" << expected.got_bc << "\n";
    // ... etc for DE, HL, IX, IY, SP, (HL)
    
    ss << "\nInput state:\n  " << got.input_description;
    return ss.str();
}

static std::string FormatFlags(uint8_t f) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)f << " [";
    ss << ((f & 0x80) ? "S" : "-");
    ss << ((f & 0x40) ? "Z" : "-");
    ss << ((f & 0x20) ? "Y" : "-");  // YF/F5
    ss << ((f & 0x10) ? "H" : "-");
    ss << ((f & 0x08) ? "X" : "-");  // XF/F3
    ss << ((f & 0x04) ? "P" : "-");
    ss << ((f & 0x02) ? "N" : "-");
    ss << ((f & 0x01) ? "C" : "-");
    ss << "]";
    return ss.str();
}
```

**Example Diagnostic Output:**

```
Test FAILED: SCF+CCF
CRC: 0x06BEF5C6  Expected: 0x9086496C

First mismatch found at iteration 1847:

Flags mismatch:
  Got:      0x29 [--Y-X--C]
  Expected: 0x28 [--Y-X---]
  Wrong bits: CF 

A: got 0xAA, expected 0xAA (OK)
BC: got 0xBBCC, expected 0xBBCC (OK)

Input state:
  Instruction: SCF; CCF
  A=0x82, F=0xD7, BC=0xBBCC, DE=0xDDEE, HL=0x4411

Analysis: The Carry Flag should be cleared after CCF following SCF,
          but emulator left it set.
```

**Common Diagnostic Categories:**

| Symptom | Likely Cause |
|---------|--------------|
| YF/XF wrong after ALU | Undocumented flag bits 3,5 not set from result |
| YF/XF wrong after SCF/CCF | Q register not implemented correctly |
| YF/XF wrong after BIT n,(HL) | MEMPTR high byte not used |
| HF wrong after ADD/SUB | Half-carry calculation error |
| PV wrong after arithmetic | Overflow vs parity confusion |
| PV wrong after block ops | PV should reflect BC != 0 |
| Flags wrong after interrupt | Block instruction interrupted flags not set from PC |

---

### Phase 5: Verification

1. Run all tests and compare against expected CRCs
2. For failures, run diagnostic mode to identify specific issues
3. Cross-reference with z80test running in emulator

---

## File Structure

```
core/tests/z80/
├── z80_test.cpp                  # Existing tests
├── z80test/
│   ├── z80test_vectors.h         # Generated test data
│   ├── z80test_vectors.cpp       # Generated test data (large arrays)
│   ├── z80test_engine.h          # Test engine header
│   ├── z80test_engine.cpp        # Test engine implementation  
│   ├── z80test_diagnostics.h     # Human-readable failure diagnostics
│   ├── z80test_diagnostics.cpp   # Diagnostic formatting implementation
│   ├── z80test_crc.h             # CRC-32 implementation
│   ├── z80test_full.cpp          # Parametrized GTest tests
│   └── CMakeLists.txt            # Build configuration
└── CMakeLists.txt                # Updated to include z80test/
```

---

## Effort Estimate

| Task | Estimate |
|------|----------|
| Python parser for tests.asm | 2-3 hours |
| Generate test vectors | 1 hour |
| CRC-32 implementation | 0.5 hours |
| Test engine (vector combinator) | 3-4 hours |
| Test engine (execution) | 2-3 hours |
| Diagnostic mode implementation | 2-3 hours |
| GTest integration | 1-2 hours |
| Debugging & verification | 3-4 hours |
| **Total** | **14-21 hours** |

---

## Benefits

1. **Authoritative Reference:** CRCs from genuine Zilog Z80 hardware
2. **Exhaustive Coverage:** Tests all flag behaviors, including undocumented
3. **Regression Detection:** Any CPU emulation change triggers CRC mismatch
4. **Multiple Variants:** Can test documented-only or all flags
5. **Fast Execution:** Native C++ vs running in emulated Spectrum
6. **CI Integration:** Runs as part of standard test suite

---

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Complex vector generation logic | Study idea.asm carefully, add extensive logging |
| I/O tests require port emulation | Mock IN/OUT to return expected values |
| Some tests depend on specific memory layout | Use fixed data address (0x8800) as original |
| CRC mismatch debugging is hard | Add mode to dump individual test vector results |

---

## Implementation Checklist

- [ ] Create Python parser for tests.asm
- [x] Generate z80test_vectors.h/cpp (initial 11 vectors manually created)
- [x] Implement CRC-32 function (verified with standard test)
- [/] Implement vector combinator (counter/shifter) - needs refinement
- [/] Implement test execution engine - framework complete, iteration logic WIP
- [x] Create Z80FullTest parametrized tests
- [ ] Run and verify SELF TEST passes
- [ ] Run and verify SCF/CCF tests pass
- [ ] Run all 120+ tests
- [ ] Debug any failures
- [ ] Add to CI pipeline
- [ ] Update documentation

### Implementation Status (2026-01-18)

**Files Created:**
- `core/tests/z80/z80test/z80test_crc.h` - CRC-32 header
- `core/tests/z80/z80test/z80test_crc.cpp` - CRC-32 implementation (verified)
- `core/tests/z80/z80test/z80test_vectors.h` - Test vector data structure
- `core/tests/z80/z80test/z80test_vectors.cpp` - 11 initial test vectors
- `core/tests/z80/z80test/z80test_engine.h` - Test engine header
- `core/tests/z80/z80test/z80test_engine.cpp` - Test engine implementation
- `core/tests/z80/z80test/z80test_full.cpp` - GTest parametrized tests
- `core/tests/z80/z80test/CMakeLists.txt` - Build configuration

**Verified Working:**
- All files compile without errors
- CRC-32 test passes: `Z80TestCRC.VerifyTable`
- GTest registration works: 33 tests registered (11 vectors × 3 test modes)

**Remaining Work:**
- Vector iteration logic needs to match z80test exactly
- Add remaining ~110 test vectors (parser or manual extraction)
- Debug CRC mismatches once iteration is correct

---

## References

1. **z80test-1.2a Source:** `testdata/z80/z80test-1.2a/src/`
2. **Original Author:** [Patrik Rak](https://github.com/raxoft)
3. **ZEXALL:** Related Z80 exerciser (different approach)
4. **Zero-Emulator:** Reference implementation at `/Volumes/TB4-4Tb/Projects/emulators/github/Zero-Emulator/`
