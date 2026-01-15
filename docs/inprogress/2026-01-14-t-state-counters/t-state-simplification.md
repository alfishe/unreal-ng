# T-State Counter Simplification

**Purpose:** Simplify and minimize CPU timing counters by aligning with the original UnrealSpeccy design.

**Status:** Implementation Plan

**Parent:** [Analyzer Architecture](./analyzer-architecture.md)

---

## 1. Problem Statement

During porting from the original UnrealSpeccy codebase, the T-state management became unnecessarily complex with redundant counters serving overlapping purposes.

### 1.1 Original Design (Clean)

From [build/unrealspeccy/architecture/emulator/t-state-management.md](../../../build/unrealspeccy/architecture/emulator/t-state-management.md):

| Counter | Type | Purpose |
|---------|------|---------|
| `cpu.t` | `unsigned` | Frame-relative T-state (wraps at frame boundary via `t -= conf.frame`) |
| `comp.t_states` | `int64` | Monotonic absolute counter (incremented by `conf.frame` each frame) |
| `cpu.eipos` | `unsigned` | T-state position when EI executed (for interrupt delay) |
| `cpu.haltpos` | `unsigned` | T-state position when HALT entered |
| `cpu.tpi` | `unsigned` | Frame size threshold ("T-states per interrupt") |

**Absolute time:** `comp.t_states + cpu.t`

### 1.2 Current Implementation (Bloated)

From [emulator/cpu/z80.h](../../../core/src/emulator/cpu/z80.h):

| Counter | Type | Location | Purpose | Status |
|---------|------|----------|---------|--------|
| `tt` | `uint32_t` | Z80Registers | Scaled T-state (`t * rate`) | Legacy, confusing |
| `t` | `unsigned:24` | Z80Registers (bitfield in `tt` union) | Frame-relative | ✅ Correct but buried in union |
| `t_l` | `unsigned:8` | Z80Registers (bitfield in `tt` union) | Low bits for scaling | Legacy, unused? |
| `clock_count` | `uint64_t` | Z80State | Monotonic counter | ✅ Used (tape, timing) |
| `cycle_count` | `uint32_t` | Z80Registers | "Cycle accuracy counter" | ❓ Purpose unclear |
| `tStatesPerFrame` | `uint32_t` | Z80Registers | Frame-relative counter | ❌ **Never populated** |
| `eipos` | `uint16_t` | Z80Registers | EI position | ✅ Correct |
| `haltpos` | `uint16_t` | Z80Registers | HALT position | ✅ Correct |
| `tpi` | `uint32_t` | Z80State | T-states per interrupt | ✅ Correct |
| `t_states` | `uint64_t` | EmulatorState | Cumulative counter | ✅ Monotonic, correct |
| `frame_counter` | `uint64_t` | EmulatorState | Frame number | ✅ Correct |

**Problems:**
1. **`tt`/`t`/`t_l` union** — Legacy from when `rate` was used for frequency scaling. Now `rate` is always 256 at 1x speed.
2. **`cycle_count`** — Duplicates `clock_count` functionality
3. **`tStatesPerFrame`** — Declared but never populated
4. **`clock_count` vs `t_states`** — Both are monotonic, but incremented differently

---

## 2. Proposed Simplification

### 2.1 Target State (After Cleanup)

| Counter | Type | Location | Purpose | Status |
|---------|------|----------|---------|--------|
| `tt`/`t`/`t_l` | union | Z80Registers | Frame-relative with rate scaling | ✅ Keep |
| `rate` | `uint32_t` | Z80State | CPU speed multiplier (256=1x, 128=2x) | ✅ Keep |
| `eipos` | `uint32_t` | Z80Registers | T-state when EI executed | ✅ Keep |
| `haltpos` | `uint32_t` | Z80Registers | T-state when HALT entered | ✅ Keep |
| `tpi` | `uint32_t` | Z80State | Frame size (T-states per interrupt) | ✅ Keep (rename later) |
| `t_states` | `uint64_t` | EmulatorState | Monotonic absolute T-state counter | ✅ Keep |
| `frame_counter` | `uint64_t` | EmulatorState | Frame number | ✅ Keep |
| `cycle_count` | `uint32_t` | Z80Registers | Redundant (same as `t`) | ❌ Remove |
| `clock_count` | `uint64_t` | Z80State | Redundant (same as `t`) | ❌ Remove |
| `tStatesPerFrame` | `uint32_t` | Z80Registers | Never populated | ❌ Remove |

---

### 2.2 Detailed Counter Usage (from Original Codebase)

#### `eipos` — EI Instruction Position

**Purpose:** Records the T-state when the `EI` (Enable Interrupts) instruction was executed.

**Why it exists:** The Z80 has a quirk: interrupts are not enabled until **one instruction after** EI executes. This prevents `EI; RET` from being interrupted before the return.

**Original code (op_noprefix.cpp:1348-1351):**
```cpp
Z80OPCODE op_FB(Z80 *cpu) { // ei
   cpu->iff1 = cpu->iff2 = 1;
   cpu->eipos = cpu->t;  // Record when EI was executed
}
```

**Usage in interrupt handling (dbgtrace.cpp:522):**
```cpp
if (cpu.int_pend && cpu.iff1 && cpu.t != cpu.eipos &&  // int enabled but NOT immediately after EI
```

**Frame boundary wrap (mainloop.cpp:62):**
```cpp
cpu.eipos -= conf.frame;  // Adjust position for new frame
```

---

#### `haltpos` — HALT Instruction Position

**Purpose:** Records the T-state when `HALT` was entered. Used for CPU usage display.

**Why it exists:** HALT causes the CPU to do nothing until an interrupt. Knowing when HALT started allows showing "% of frame spent in HALT" as a performance metric.

**Original code (op_noprefix.cpp:597-603):**
```cpp
Z80OPCODE op_76(Z80 *cpu) { // halt
   if(!cpu->halted)
       cpu->haltpos = cpu->t;  // Record when HALT started

   cpu->pc--;
   cpu->halted = 1;
}
```

**Usage in LED display (leds.cpp:267-275):**
```cpp
sprintf(bf, "%6d*%2.2f", cpu.haltpos ? cpu.haltpos : cpu.t, p_fps);
// ...
if (cpu.haltpos) {
   // Draw bar showing CPU usage before HALT
   unsigned mx = cpu.haltpos * PSZ * 8 / conf.frame;
}
```

---

#### `tpi` — T-States Per Interrupt (Frame Size)

**Purpose:** The frame size threshold used for interrupt timing decisions.

**Why it exists:** HALT optimization needs to know how far into the frame we can "fast-forward" to the interrupt.

**Original code (z80/defs.h:254):**
```cpp
void SetTpi(u32 Tpi) { tpi = Tpi; }
```

**Set from configuration (config.cpp:944):**
```cpp
cpu.SetTpi(conf.frame);  // tpi = frame size (e.g., 71680 for Pentagon)
```

**Usage in HALT handling (op_ed.cpp:181, 236):**
```cpp
// Can we fast-forward HALT to end of frame?
if (cpu->iff1 && (cpu->t + 10 < cpu->tpi))
   // ...

if (cpu->iff2 && ((cpu->t + 10 < cpu->tpi) || cpu->eipos + 8 == cpu->t))
```

---

### 2.3 Understanding the `tt` Union (NOT to Remove)

**Correction:** The `tt`/`t`/`t_l` union is **not legacy** — it's an optimization for CPU speed scaling.

```cpp
union {
    uint32_t tt;       // Full scaled value
    struct {
        unsigned t_l : 8;   // Sub-cycle precision (fractional ticks)
        unsigned t : 24;    // Unscaled T-states (frame-relative)
    };
};
```

**How it works:**

| Speed | `rate` | Write | Effect |
|-------|--------|-------|--------|
| 1x (3.5 MHz) | 256 | `tt += cycles * 256` | `t` increments by `cycles` |
| 2x (7 MHz) | 128 | `tt += cycles * 128` | `t` increments by `cycles/2` |
| 4x (14 MHz) | 64 | `tt += cycles * 64` | `t` increments by `cycles/4` |

---

### 2.3.1 Frame Limits at Different Speeds

The frame loop uses a scaled limit based on the speed multiplier:

```cpp
uint32_t frameLimit = config.frame * state.current_z80_frequency_multiplier;
while (cpu.t < frameLimit) { ... }
```

**Example (Pentagon 71680 T-states/frame):**

| Speed | Multiplier | `frameLimit` | `t` range | Z80 work per frame |
|-------|------------|--------------|-----------|-------------------|
| 1x | 1 | 71680 | 0 → 71680 | Normal (~20ms worth) |
| 2x | 2 | 143360 | 0 → 143360 | 2× instructions |
| 4x | 4 | 286720 | 0 → 286720 | 4× instructions |
| 8x | 8 | 573440 | 0 → 573440 | 8× instructions |

At higher speeds, more Z80 T-states execute per video frame, fitting more emulated work into the same wall-clock time.

**Read paths:**
- **`t`** — Frame-relative T-states (always correct for beam position, frame boundaries)
- **`tt`** — Scaled value (used internally for frame loop comparison)

**Why this is clever:**
1. **Single write path** — always `tt += cycles * rate`
2. **Two read paths** — scaled or unscaled, no division needed
3. **Sub-cycle precision** — `t_l` captures fractional cycles during turbo modes

**In original codebase:**
- Used plain `unsigned t` without scaling
- "Max speed" mode worked by **disabling sound** (removes frame pacing), not by scaling cycles
- Opcodes directly used `cpu->t += cycles`

**In ported codebase:**
- Added `tt`/`t`/`t_l` union for CPU speed multiplier support (2x, 4x, etc.)
- `cputact(a)` macro: `cpu->tt += ((a) * cpu->rate); cpu->cycle_count += (a); cpu->clock_count += (a);`
- The bitfield extraction (`t = tt >> 8`) provides the unscaled T-state count

**The problem:** This created **three redundant counters** (`tt`/`t`, `cycle_count`, `clock_count`) that all track the same thing differently.

---

### 2.4 Redundant Counters to Remove

The `tt`/`t` union with `rate` is **required** for the frequency multiplication feature. Only these redundant counters should be removed:

| Counter | Why Redundant |
|---------|---------------|
| `cycle_count` | Duplicates the unscaled T-state count (same as `t`) |
| `clock_count` | Duplicates the unscaled T-state count (same as `t`) |
| `tStatesPerFrame` | Declared but never populated |

**Keep:**
- `tt`/`t`/`t_l` union — required for frequency multiplication
- `rate` — the scaling factor for CPU speed (256=1x, 128=2x, 64=4x)
- `t_states` — monotonic absolute counter
- `eipos`, `haltpos`, `tpi` — interrupt timing

**After cleanup, the cputact macro becomes:**
```cpp
#define cputact(a) cpu->tt += ((a) * cpu->rate);
// No more cycle_count or clock_count increments
```

**For code needing absolute T-state count:**
```cpp
// Instead of clock_count, use:
uint64_t absoluteT = _context->emulatorState.t_states + cpu.t;
```

---

## 3. Proposed Changes

### 3.1 Phase 1: Audit Current Usage

Before removing any counters, audit all usages:

| Counter | Files Using | Action |
|---------|-------------|--------|
| `tt` | z80.cpp, cpulogic.h | ✅ Keep (required for rate scaling) |
| `t` | z80.cpp, core.cpp, screenzx.cpp, vg93.cpp | ✅ Keep (frame-relative position) |
| `t_l` | cpulogic.h | ✅ Keep (sub-cycle precision) |
| `rate` | z80.cpp, z80.h | ✅ Keep (speed multiplier) |
| `clock_count` | tape.cpp, z80.cpp | ❌ Replace with `t_states + t` |
| `cycle_count` | z80.cpp, cpulogic.h | ❌ Remove (redundant with `t`) |
| `tStatesPerFrame` | None | ❌ Remove (never populated) |

### 3.2 Phase 1: Remove `tStatesPerFrame`

**Safest first** — this field is never used.

```cpp
// Z80Registers — REMOVE this line:
uint32_t tStatesPerFrame;  // How many t-states already spent during current frame
```

### 3.3 Phase 2: Remove `cycle_count`

**Current (cpulogic.h):**
```cpp
#define cputact(a) cpu->tt += ((a) * cpu->rate); cpu->cycle_count += (a); cpu->clock_count += (a);
```

**Step 1 — Remove from macro:**
```cpp
#define cputact(a) cpu->tt += ((a) * cpu->rate); cpu->clock_count += (a);
```

**Step 2 — Remove field from Z80Registers:**
```cpp
// Z80Registers — REMOVE this line:
uint32_t cycle_count;      // Counter to track cycle accuracy
```

### 3.4 Phase 3: Remove `clock_count`

**Audit usages first** (tape.cpp uses it for timing):

```cpp
// tape.cpp — BEFORE:
size_t clockCount = cpu.clock_count;

// tape.cpp — AFTER:
uint64_t clockCount = _context->emulatorState.t_states + cpu.t;
```

**Then remove from macro:**
```cpp
#define cputact(a) cpu->tt += ((a) * cpu->rate);
```

**And remove field from Z80State:**
```cpp
// Z80State — REMOVE this line:
uint64_t clock_count;      // Monotonically increasing clock edge counter
```

### 3.5 Summary: Before vs After

**Before (`cputact` macro):**
```cpp
#define cputact(a) cpu->tt += ((a) * cpu->rate); cpu->cycle_count += (a); cpu->clock_count += (a);
```

**After:**
```cpp
#define cputact(a) cpu->tt += ((a) * cpu->rate);
```

**Before (`IncrementCPUCyclesCounter`):**
```cpp
void Z80::IncrementCPUCyclesCounter(uint8_t cycles) {
    tt += cycles * rate;
    cycle_count += cycles;
    clock_count += cycles;
}
```

**After:**
```cpp
void Z80::IncrementCPUCyclesCounter(uint8_t cycles) {
    tt += cycles * rate;
}
```

---

## 4. Verification Plan

### 4.1 Existing Tests to Reuse

| Test Suite | What it Verifies | Location |
|------------|------------------|----------|
| `core-tests` | Z80 opcode cycle counts, instruction timing | `core/tests/` |
| `snapshot-tests` | Save/load preserves CPU state | `core/tests/` |
| `automation-tests` | Multi-emulator synchronization | `tools/verify-*.py` |

### 4.2 New Tests to Add

#### Test Fixture Setup

```cpp
// test_tstate_counters.cpp

class TStateCountersTest : public ::testing::Test {
protected:
    EmulatorContext context;
    Z80* cpu;
    uint32_t frameSize;
    
    void SetUp() override {
        // Initialize emulator with Pentagon configuration
        context.config = GetPentagonConfig();  // 71680 T-states/frame
        context.Initialize();
        
        cpu = context.pCore->GetZ80();
        frameSize = context.config.frame;
        
        // Reset all counters to known state
        cpu->Reset();
        context.emulatorState.t_states = 0;
        context.emulatorState.frame_counter = 0;
    }
    
    void TearDown() override {
        context.Shutdown();
    }
    
    void RunFrame() {
        context.pCore->ExecuteCPUFrameCycle();
        context.pCore->AdjustFrameCounters();
    }
    
    void RunFrames(int n) {
        for (int i = 0; i < n; i++) RunFrame();
    }
};
```

#### T-State Counter Integrity Tests

```cpp
TEST_F(TStateCountersTest, FrameRelativeWrapsCorrectly) {
    // Verify t resets to ~0 at frame boundary
    RunFrame();
    EXPECT_LT(cpu->t, 100);  // Should wrap to near-zero
}

TEST_F(TStateCountersTest, AbsoluteTimeCalculation) {
    // Verify t_states + t gives correct absolute time
    RunFrames(5);
    EXPECT_EQ(context.emulatorState.t_states, 5 * frameSize);
}

TEST_F(TStateCountersTest, SpeedMultiplierScaling) {
    // At 2x speed: frameLimit = frameSize * 2
    for (int mult : {1, 2, 4, 8}) {
        SetUp();  // Fresh instance each time
        context.state.current_z80_frequency_multiplier = mult;
        RunFrame();
        // More T-states executed at higher multipliers
        EXPECT_GE(context.emulatorState.t_states, frameSize * mult - 10);
    }
}
```

#### Rate Scaling Tests

```cpp
TEST_F(TStateCountersTest, WritesToTTCorrectly) {
    // rate=256 (1x): tt += 256 per cycle, t += 1
    cpu->rate = 256;
    cpu->tt = 0;
    cpu->IncrementCPUCyclesCounter(1);
    EXPECT_EQ(cpu->t, 1);
    
    // rate=128 (2x): tt += 128 per cycle, t += 0.5 (via bitfield)
    cpu->rate = 128;
    cpu->tt = 0;
    cpu->IncrementCPUCyclesCounter(2);
    EXPECT_EQ(cpu->t, 1);  // 2 cycles at half rate = 1 t-state
}
```

#### Cross-Frame Timing Tests

```cpp
TEST_F(TStateCountersTest, EiposWrapsAtFrameBoundary) {
    cpu->eipos = frameSize - 10;
    context.pCore->AdjustFrameCounters();
    EXPECT_LT(cpu->eipos, 10);  // Wrapped correctly
}

TEST_F(TStateCountersTest, TapeTimingUsesAbsoluteT) {
    // Tape timing must work across frame boundaries
    uint64_t edgeTime = context.emulatorState.t_states + cpu->t;
    RunFrames(5);
    uint64_t elapsed = (context.emulatorState.t_states + cpu->t) - edgeTime;
    EXPECT_EQ(elapsed, 5 * frameSize);
}
```

#### Multi-Instance Timing Independence

```cpp
TEST(MultiEmulatorTest, InstancesHaveIndependentTiming) {
    EmulatorContext ctx1, ctx2;
    ctx1.Initialize();
    ctx2.Initialize();
    
    // Run different number of frames
    ctx1.pCore->ExecuteCPUFrameCycle();
    ctx1.pCore->ExecuteCPUFrameCycle();
    ctx2.pCore->ExecuteCPUFrameCycle();
    
    // Each instance tracks its own timing
    EXPECT_NE(ctx1.emulatorState.t_states, ctx2.emulatorState.t_states);
    EXPECT_EQ(ctx1.emulatorState.frame_counter, 2);
    EXPECT_EQ(ctx2.emulatorState.frame_counter, 1);
    
    ctx1.Shutdown();
    ctx2.Shutdown();
}
```

### 4.3 Integration Tests

| Test | Speed | Frames | Validates |
|------|-------|--------|-----------|
| Tape loading | 1x | ~1000 | Timing-sensitive bit detection |
| Sound playback | 1x, 2x | 50 | Audio buffer sync with frame timing |
| Screen rendering | 1x, 4x | 10 | Beam position matches T-state |
| Multi-emulator | 1x | 100 | Each instance has independent timing |

### 4.4 Regression Test: `clock_count` Removal

Before removing `clock_count`, verify tape timing still works:

```bash
# Load timing-sensitive tape file
./unreal-qt --load test_tapes/timing_test.tap --headless --frames 5000
# Expected: successful load, no timing errors
```

---

## 5. Implementation Order

| Step | Description | Risk |
|------|-------------|------|
| 1 | Remove `tStatesPerFrame` from Z80Registers | None (unused) |
| 2 | Remove `cycle_count` from Z80Registers + cputact macro | Low |
| 3 | Replace `clock_count` usages with `t_states + t` in tape.cpp | Medium |
| 4 | Remove `clock_count` from Z80State + cputact macro | Low |
| 5 | Simplify `IncrementCPUCyclesCounter()` | Low |
| 6 | Verify all tests pass | None |

---

## 6. Resolved Questions

| Question | Answer |
|----------|--------|
| **GS CPU timing** | Separate handling — fully asynchronous device, different beast |
| **Snapshot compatibility** | ✅ Safe — `cycle_count`, `clock_count`, `tStatesPerFrame` are NOT used in `.sna`/`.z80` loaders |
| **`tpi` rename** | Keep as-is, add comments explaining what each variable is for |

---

## References

- [build/unrealspeccy/architecture/emulator/t-state-management.md](../../../build/unrealspeccy/architecture/emulator/t-state-management.md)
- [emulator/cpu/z80.h](../../../core/src/emulator/cpu/z80.h)
- [emulator/cpu/z80.cpp](../../../core/src/emulator/cpu/z80.cpp)
- [emulator/cpu/core.cpp](../../../core/src/emulator/cpu/core.cpp)
- [emulator/cpu/cpulogic.h](../../../core/src/emulator/cpu/cpulogic.h)
