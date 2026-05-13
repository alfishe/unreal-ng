# Beam-to-Execution Correlation: Implementation Options

**Purpose:** Evaluate implementation approaches for correlating Z80 execution timing with ULA beam position.

**Status:** Options Analysis

**Parent:** [Analyzer Architecture](./analyzer-architecture.md)

---

## 1. Goal

Enable the analyzer subsystem to answer timing-related questions:

### 1.1 Reverse Engineering Use Cases

| Use Case | User Question |
|----------|---------------|
| **[UC-TIM-01](./analysis-use-cases.md#uc-tim-01)** | "Which scan line was being drawn when this executed?" |
| **[UC-TIM-02](./analysis-use-cases.md#uc-tim-02)** | "Is this routine timing-critical for display?" |
| **[UC-TIM-03](./analysis-use-cases.md#uc-tim-03)** | "Is this screen write racing the beam?" |
| **[UC-TIM-04](./analysis-use-cases.md#uc-tim-04)** | "How consistent is the interrupt timing?" |
| **[UC-TIM-05](./analysis-use-cases.md#uc-tim-05)** | "How long does the interrupt handler take?" |

### 1.2 Developer Profiling Use Cases

| Use Case | User Question |
|----------|---------------|
| **[UC-TIM-06](./analysis-use-cases.md#uc-tim-06)** | "What % of frame does this function consume?" |
| **[UC-TIM-07](./analysis-use-cases.md#uc-tim-07)** | "How much free CPU budget do I have within frame?" |
| **[UC-TIM-08](./analysis-use-cases.md#uc-tim-08)** | "Is this code synchronizing attribute writes with the beam?" |

### 1.3 Developer Questions (Derived)

Beyond the catalogued use cases, developers writing games/demos need to answer:

| Question | What's Needed |
|----------|---------------|
| "Will my function fit in VBlank?" | Function duration vs VBlank budget (14,336 T-states) |
| "Is my blit synced with ULA for multicolor?" | Precise beam position during each attribute write |
| "Am I racing the beam?" | Compare write T-state vs ULA fetch T-state for same address |
| "How much margin do I have for ISR?" | Frame budget - used - ISR reserved |
| "Is this multicolor effect?" | Attribute writes timed to specific scanlines during active screen |

#### Multicolor Effects Explained

The ZX Spectrum's ULA enforces a **2-colors-per-8x8-pixel-cell** limit via the attribute area (0x5800-0x5AFF). However, clever programmers can change attributes **mid-screen** while the beam is drawing, effectively getting more colors on screen.

**How It Works:**
1. The ULA reads attributes **just before** drawing each character row
2. If the CPU writes to an attribute address **after** the beam has passed that row but **before** the next frame, the old color is used
3. If the CPU writes **just before** the ULA fetches that attribute, the new color is used for that row

**Detection Signature:**
```
IF write_address ∈ [0x5800, 0x5AFF]           // Attribute area
   AND write_tstate ∈ BeamRegion::Screen       // During active display
   AND |write_tstate - ula_fetch_tstate| ≤ 8   // Precisely synchronized
THEN → Multicolor effect detected
```

**What Analysis Reveals:**
- Which scanlines are affected by the effect
- The sync precision (tight = intentional effect, loose = may be accidental)
- The routine responsible for the timed writes

**Business Value:**
- **Reverse Engineering:** Distinguish frame-intrinsic code (sprites, rasters) from extra-frame logic (AI, engine)
- **Developer Profiling:** Optimize code to fit within frame budget, implement raster effects

**Reference:** See [Analysis Use Cases](./analysis-use-cases.md) for complete details on all timing use cases.

---

## 2. Background

### 2.1 The Problem

The ZX Spectrum's ULA draws the screen continuously while the Z80 executes code. Currently, our capture infrastructure records **what** happened but not **when within the frame** it happened.

```
Current State:
┌─────────────────────────────────────────────────────────────┐
│  Z80 CPU              Memory Tracker              Screen    │
│  ┌──────────────┐    ┌──────────────┐        ┌────────────┐ │
│  │ Has tstate!  │    │ NO tstate    │        │ Has LUT!   │ │
│  │              │──X─│ passed here  │───X───▶│ but private│ │
│  │ Knows timing │    │              │        │            │ │
│  └──────────────┘    │ Only:        │        │ tstate →   │ │
│                      │ - address    │        │ screen pos │ │
│                      │ - value      │        └────────────┘ │
│                      │ - caller     │                       │
│                      └──────────────┘                       │
└─────────────────────────────────────────────────────────────┘

Target State:
┌─────────────────────────────────────────────────────────────────────────┐
│  Z80 CPU              Memory Tracker                 BeamPositionService│
│  ┌──────────────┐    ┌──────────────────────┐       ┌──────────────────┐│
│  │ frameTstate  │    │ Receives:            │       │ Exposes LUT      ││
│  │ frameNumber  │───▶│ - address            │──────▶│                  ││
│  │              │    │ - value              │       │ GetBeamInfo()    ││
│  │ GetFrame-    │    │ - caller             │       │ GetBeamRegion()  ││
│  │ Tstate()     │    │ - frameTstate  [NEW] │       │                  ││
│  └──────────────┘    │ - frameNumber  [NEW] │       └──────────────────┘│
│                      │                      │                           │
│                      │ Stores:              │       ┌──────────────────┐│
│                      │ - BeamHistogram      │       │ TimedAccessLog   ││
│                      │ - TimedAccessEvent   │──────▶│ (optional)       ││
│                      └──────────────────────┘       └──────────────────┘│
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Frame Timing Reference (48K)

```
Frame Duration: 69,888 T-states (50 Hz PAL)
T-states per line: 224
Total lines: 312 (192 visible + 56 top border + 56 bottom + 8 VBlank)

    T-state    Region          Beam Activity
    ─────────────────────────────────────────
    0          VBlank          INT fires here
    14,336     Top Border      Border color
    28,672     Screen          Pixel + attr fetch (192 lines)
    71,680     Bottom Border   Border color
```

### 2.3 Existing Infrastructure

| Component | What Exists | Gap |
|-----------|-------------|-----|
| **TstateCoordLUT** | Pre-computed tstate → screen coords (71,680 entries) | Private to ScreenZX |
| **Z80 tstate** | CPU tracks cumulative T-state counter | Not passed to trackers |
| **MemoryAccessTracker** | Records R/W/X counts, callers | No timing field |
| **CallTraceBuffer** | Has `frame_counter` | No `frameTstate` |

---

## 3. Overview of Requirements

### 3.1 Data Requirements (from Use Cases)

#### Reverse Engineering Requirements

| Requirement | Use Cases | Priority |
|-------------|-----------|----------|
| **Frame-relative T-state** in memory access hooks | [UC-TIM-01](./analysis-use-cases.md#uc-tim-01), [UC-TIM-03](./analysis-use-cases.md#uc-tim-03) | HIGH |
| **Frame number** in memory access hooks | [UC-SMC-01](./analysis-use-cases.md#uc-smc-01), [UC-TMP-04](./analysis-use-cases.md#uc-tmp-04), [UC-CMP-01](./analysis-use-cases.md#uc-cmp-01) | HIGH |
| Both (T-state + frame) in control flow events | [UC-TIM-04](./analysis-use-cases.md#uc-tim-04), [UC-TIM-05](./analysis-use-cases.md#uc-tim-05) | HIGH |
| Beam region classifier | [UC-TIM-01](./analysis-use-cases.md#uc-tim-01), [UC-TIM-02](./analysis-use-cases.md#uc-tim-02) | MEDIUM |
| Beam histogram aggregation | [UC-TIM-02](./analysis-use-cases.md#uc-tim-02) | LOW |
| VRAM write vs beam analysis | [UC-TIM-03](./analysis-use-cases.md#uc-tim-03) | MEDIUM |

#### Developer Profiling Requirements

| Requirement | Use Cases | Priority |
|-------------|-----------|----------|
| **Function entry/exit T-states** | [UC-TIM-06](./analysis-use-cases.md#uc-tim-06), VBlank fit analysis | HIGH |
| **Per-function T-state accumulator** | [UC-TIM-06](./analysis-use-cases.md#uc-tim-06) | HIGH |
| **Per-frame total T-state consumption** | [UC-TIM-07](./analysis-use-cases.md#uc-tim-07) | HIGH |
| **ISR duration tracking** | [UC-TIM-07](./analysis-use-cases.md#uc-tim-07) (margin calculation) | MEDIUM |
| **VRAM write timing (per-write T-state)** | ULA sync / multicolor analysis | HIGH |
| **Time window comparison** (fn duration vs VBlank) | VBlank fit analysis | MEDIUM |

### 3.2 Performance Constraints

| Constraint | Impact |
|------------|--------|
| Memory access is in hot path | Every instruction touches memory |
| 3.5 MHz = ~3.5M accesses/sec | Must be sub-microsecond |
| Multi-instance support (165+) | Per-instance overhead multiplies |
| Long session analysis | Memory must be bounded |

### 3.3 Capture vs Analysis: What Must Be Real-Time?

A critical architectural question: **must we capture timing data in real-time, or can we compute it on-demand?**

| Data Type | Real-Time Required? | Rationale |
|-----------|---------------------|-----------|
| **Frame-relative T-state** | ⚠️ **Yes — must capture at event time** | T-state is ephemeral; once the moment passes, we cannot reconstruct "what T-state was it when this happened?" |
| **Frame number** | ⚠️ **Yes — must capture at event time** | Same reason; temporal ordering requires knowing which frame |
| **Beam region classification** | ❌ No — post-analysis OK | Given T-state, we can always compute region from the LUT |
| **Histogram aggregation** | ❌ No — post-analysis OK | Sum events by region after capture |
| **Pattern analysis** | ❌ No — post-analysis OK | Run algorithms on captured data when user requests |

**Key Insight:** The T-state and frame number must be captured at the moment of the event. Everything else (histograms, analysis, visualization) can be computed lazily when the user activates the feature.

### 3.4 Storage Strategies

Two fundamentally different approaches to store captured timing data, with different trade-offs:

#### Strategy 1: BeamHistogram (Aggregated Counters)

**Concept:** Simple counters per beam region — increment `histogram.screen++` on each access.

```
┌─────────────────────────────────────────────────────────────┐
│  Per Memory Access:                                          │
│                                                              │
│    T-state ──▶ LUT lookup ──▶ region ──▶ histogram[region]++ │
│                                                              │
│  Result: "70% of accesses during Screen, 20% VBlank, 10% Border" │
└─────────────────────────────────────────────────────────────┘
```

| Aspect | Detail |
|--------|--------|
| **What it stores** | Counters per beam region (VBlank, HBlank, Screen, Border, etc.) |
| **How it works** | Single indexed array increment per access |
| **Storage cost** | 64 bytes (1 cache line) per tracked entity |
| **Use cases answered** | "Did this routine run mostly during VBlank or Screen?" |
| **Precision** | Statistical distribution only — no individual event details |

#### Strategy 2: TimedAccessEvent (Individual Event Log)

**Concept:** Ring buffer of individual events with full context (address, caller, tstate, frame).

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Per Memory Access:                                                     │
│                                                                         │
│    Event { frame, tstate, addr, caller, value } ──▶ push to ring buffer │
│                                                                         │
│  Result: "Address 0x8200 written at frame 500, tstate 35000"            │
└─────────────────────────────────────────────────────────────────────────┘
```

| Aspect | Detail |
|--------|--------|
| **What it stores** | Individual events: frame, T-state, address, caller, value |
| **How it works** | Lock-free ring buffer push per access |
| **Storage cost** | ~6 MB for 256K events (ring buffer wraps) |
| **Use cases answered** | "Exactly when did this SMC write happen? In what order?" |
| **Precision** | Full event-level detail |

#### Strategy Comparison

| Aspect | BeamHistogram | TimedAccessEvent |
|--------|---------------|------------------|
| **Overhead** | ~1-2% (array increment) | ~5-10% (struct copy + atomic) |
| **Memory** | 64 bytes | ~6 MB |
| **Precision** | Statistical | Exact |
| **Use case** | Profiling, routine classification | Debugging, SMC timeline, race detection |
| **Always-on viable?** | ✅ Yes | ⚠️ Only when analysis enabled |

### 3.5 Capture Mode Options

Based on the above, we have **five** possible capture modes that address different use cases:

#### Mode 1: Off (Zero Overhead)
- No timing data captured
- Zero overhead — production default
- No timing analysis possible

#### Mode 2: Histogram Only (Statistical Profiling)
- Capture T-state + frame with each event
- Immediately aggregate into BeamHistogram (discard individual events)
- ~1-2% overhead
- **Answers:** "What fraction of this routine runs during Screen vs VBlank?"
- **Limitation:** Cannot answer "exactly when" questions

#### Mode 3: Full Event Log (Detailed Tracing)
- Capture T-state + frame with each event
- Store in TimedAccessRingBuffer
- ~5-10% overhead
- **Answers:** "Show me the exact timeline of SMC modifications"
- **Use when:** Debugging SMC, race conditions, precise timing analysis

#### Mode 4: Function Profiler (Developer Profiling)
- Track function entry/exit T-states via CallTraceBuffer
- Accumulate per-function T-state totals per frame
- ~2-3% overhead (leverages existing call tracking)
- **Answers:**
  - "What % of frame does DrawSprites consume?"
  - "Will my render loop fit in VBlank?"
  - "How much free CPU budget is left?"
- **Key metric:** `function_tstates / 69888 × 100 = % of frame`

#### Mode 5: ULA Sync / Multicolor (Precision VRAM Tracking)
- Full event log **filtered to VRAM writes only** (0x4000-0x5AFF)
- Compare each write's T-state to ULA's fetch T-state for same address
- ~3-5% overhead (only VRAM, not all memory)
- **Answers:**
  - "Is my blit synced with ULA for multicolor?"
  - "Am I racing the beam?"
  - "Which writes happen before/after ULA fetch?"
- **Key analysis:** `write_tstate vs ula_fetch_tstate[address]`

### 3.6 Mode Combinations

Modes can be combined based on analysis needs:

| Analysis Goal | Recommended Modes |
|---------------|-------------------|
| General profiling | Mode 2 (Histogram) |
| SMC debugging | Mode 3 (Full Log) |
| Game optimization | Mode 4 (Function Profiler) |
| Raster effect development | Mode 4 + Mode 5 |
| "Will it fit in VBlank?" | Mode 4 (compare duration to 14,336) |
| Race condition detection | Mode 5 (VRAM) or Mode 3 (Full) |

#### Mode Selection Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Capture Mode Selection                  │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  User activates    ┌──────────────┐    Events     ┌────────┐ │
│  "Timing Analysis" │ Z80 + Memory │ ─────────────▶│Storage │ │
│  ─────────────────▶│   Tracker    │               │        │ │
│                    └──────────────┘               └────────┘ │
│                          │                             │     │
│                          ▼                             ▼     │
│                   ┌──────────────────────────────────────────┤
│                   │  Mode 1: Off       → Nothing             │
│                   │  Mode 2: Histogram → BeamHistogram only  │
│                   │  Mode 3: Full      → TimedAccessLog      │
│                   └──────────────────────────────────────────┤
│                                                              │
│  User opens        ┌──────────────┐                          │
│  visualization  ──▶│ Lazy Analysis│ ◀── Read from storage    │
│                    └──────────────┘                          │
│                          │                                   │
│                          ▼                                   │
│                   Pattern detection, histogram computation,  │
│                   race condition analysis, etc.              │
└──────────────────────────────────────────────────────────────┘
```

**Key point:** The expensive analysis work (pattern detection, visualization) always happens post-capture, on-demand. The only question is whether we capture timing data at all, and at what granularity.

## 4. Solution Options

### 4.1 Option A: API Extension (Simple)

**Approach:** Add `frameTstate` parameter to existing tracker methods.

```cpp
// Current
void TrackMemoryRead(uint16_t address, uint8_t value, uint16_t callerAddress);

// Proposed
void TrackMemoryRead(uint16_t address, uint8_t value, uint16_t callerAddress,
                     uint32_t frameTstate);  // NEW
```

**Pros:**
- Minimal code changes
- Works with existing architecture
- Easy to understand

**Cons:**
- Runtime overhead on every call (even if not analyzing)
- All call sites must be updated
- No way to disable when not needed

**Estimated overhead:** ~5-10% per memory access

---

### 4.2 Option B: Template-Based Dispatch (Zero-Cost)

**Approach:** Template the memory class on a tracking policy. Disabled = zero instructions.

```cpp
template<typename TrackingPolicy>
class MemoryWithTracking {
    inline uint8_t Read(uint16_t addr) {
        uint8_t value = _pages[addr >> 14][addr & 0x3FFF];
        
        if constexpr (TrackingPolicy::ENABLED) {
            TrackingPolicy::OnAccess(_cpu->GetFrameTstate(), addr, ...);
        }
        
        return value;
    }
};

// Compile-time selection
#if BEAM_TRACKING_ENABLED
    using Memory = MemoryWithTracking<TrackingFull>;
#else
    using Memory = MemoryWithTracking<TrackingDisabled>;
#endif
```

**Pros:**
- Zero overhead when disabled (compile-time elimination)
- Maximum performance when enabled
- Clear separation of concerns

**Cons:**
- Requires recompilation to switch modes
- More complex codebase
- Template bloat in debug symbols

**Estimated overhead:** 0% disabled, ~1-2% enabled (histogram only)

---

### 4.3 Option C: Sampling-Based (Statistical)

**Approach:** Sample execution state periodically rather than tracking every access.

```cpp
class SampledTimingTracker {
    static constexpr uint32_t SAMPLE_INTERVAL = 1000;  // Every 1000 T-states
    
    void OnTick(uint32_t tstate) {
        if (tstate % SAMPLE_INTERVAL == 0) {
            RecordSample(tstate, _cpu->GetPC(), AccessFlags());
        }
    }
};
```

**Pros:**
- ~70× less data than full tracking
- Minimal hot-path impact
- Good enough for profiling/patterns

**Cons:**
- Cannot answer "exactly when did X happen"
- May miss short events
- Statistical, not deterministic

**Estimated overhead:** <0.5% (periodic sampling)

---

### 4.4 Option D: Hybrid (Adaptive)

**Approach:** Low-overhead sampling always on + full precision on-demand for specific regions.

```cpp
class AdaptiveBeamTracker {
    // Always running: periodic samples
    void OnTick(uint32_t tstate) {
        if (tstate % 1000 == 0) RecordSample(...);
    }
    
    // On-demand: full precision for monitored regions
    void OnAccess(uint16_t addr, uint32_t tstate) {
        if (_monitoredRegions.Contains(addr)) {
            RecordFullEvent(addr, tstate);
        }
    }
};
```

**Pros:**
- Baseline visibility always available
- Full detail when needed without global overhead
- Runtime configurable

**Cons:**
- Two code paths to maintain
- Monitored region lookup has cost
- Complexity in coordination

**Estimated overhead:** 0.5% baseline + ~5% for monitored regions

---

## 5. Detailed Design Elements

### 5.1 Data Structures (All Options)

#### Compact Event (24 bytes, cache-aligned)

```cpp
struct alignas(8) TimedAccessEvent {
    uint64_t frameNumber;         // Which frame (for temporal ordering)
    uint32_t frameTstate : 20;    // 0-69887 fits in 17 bits (beam position)
    uint32_t accessType  : 2;     // R=0, W=1, X=2
    uint32_t regionCode  : 3;     // BeamRegion enum (7 values)
    uint32_t reserved    : 7;
    
    uint16_t address;
    uint16_t callerPC;
    uint8_t  value;
    uint8_t  pad[3];
};
static_assert(sizeof(TimedAccessEvent) == 24);

// Note: 24 bytes = 2.67 events per cache line (vs 4 with 16 bytes)
// Trade-off: Frame number is essential for multi-frame analysis
```

#### Frame Histogram (64 bytes = 1 cache line)

```cpp
struct alignas(64) FrameBeamHistogram {
    uint32_t vblank;
    uint32_t hblank;
    uint32_t topBorder;
    uint32_t leftBorder;
    uint32_t screen;
    uint32_t rightBorder;
    uint32_t bottomBorder;
    uint32_t screenByThird[3];    // Top/Mid/Bottom
    uint32_t interruptWindow;      // First ~1000 T-states
    uint32_t mainLoopWindow;       // Rest of frame
    uint32_t _pad[4];
};
static_assert(sizeof(FrameBeamHistogram) == 64);
```

### 5.2 Pre-Computed Region LUT

#### Purpose

The Pre-Computed Region LUT answers the question: **"Given a T-state, which screen region is the ULA beam drawing?"**

This is needed because:
1. **T-state alone is meaningless** — knowing code executed at T=35000 means nothing without context
2. **Computing region per-access is expensive** — division, modulo, conditionals for each access
3. **ScreenZX already has partial data** — `TstateCoordLUT` knows coordinates, but doesn't expose semantic regions

#### What It Provides

| Query | Input | Output | Cost |
|-------|-------|--------|------|
| "Is this VBlank?" | `tstate=500` | `BeamRegion::VBlank` | O(1) lookup |
| "Is this visible screen?" | `tstate=40000` | `BeamRegion::Screen` | O(1) lookup |
| "Is this border?" | `tstate=20000` | `BeamRegion::TopBorder` | O(1) lookup |

#### How It's Built (Once at Startup)

```cpp
// Pre-computed at startup from TstateCoordLUT (72KB for 48K, variable for 128K)
uint8_t _beamRegionLUT[MAX_FRAME_TSTATES];

void BuildBeamRegionLUT() {
    for (uint32_t t = 0; t < _frameSize; t++) {
        const auto& coord = _tstateLUT[t];
        
        if (coord.renderType == RT_BLANK) {
            _beamRegionLUT[t] = static_cast<uint8_t>(BeamRegion::VBlank);
        }
        else if (coord.renderType == RT_BORDER) {
            // Distinguish top/bottom/left/right based on scanline
            uint16_t line = coord.framebufferY;
            if (line < _firstScreenLine)
                _beamRegionLUT[t] = static_cast<uint8_t>(BeamRegion::TopBorder);
            else if (line >= _lastScreenLine)
                _beamRegionLUT[t] = static_cast<uint8_t>(BeamRegion::BottomBorder);
            else
                _beamRegionLUT[t] = static_cast<uint8_t>(BeamRegion::SideBorder);
        }
        else { // RT_SCREEN
            _beamRegionLUT[t] = static_cast<uint8_t>(BeamRegion::Screen);
        }
    }
}

inline BeamRegion GetBeamRegion(uint32_t tstate) {
    return static_cast<BeamRegion>(_beamRegionLUT[tstate]);
}
```

#### How It's Used

**1. Histogram Aggregation (Hot Path)**

Every memory access updates the appropriate histogram bucket:

```cpp
void TrackMemoryWrite(uint16_t addr, uint8_t val, uint16_t caller, uint32_t tstate) {
    // O(1) region lookup — no computation
    BeamRegion region = GetBeamRegion(tstate);
    
    // Increment appropriate histogram counter
    _stats[addr].beamHistogram.Increment(region);
}
```

**2. VRAM Race Detection (Analysis Time)**

When analyzing screen writes, determine if code is racing the beam:

```cpp
bool IsRacingBeam(uint16_t vramAddr, uint32_t writeTstate) {
    BeamRegion region = GetBeamRegion(writeTstate);
    
    if (region != BeamRegion::Screen)
        return false;  // Safe: beam not in screen area
    
    // Check if beam position matches the VRAM row being written
    uint16_t writeRow = VRAMAddressToScanline(vramAddr);
    uint16_t beamRow = TstateToScanline(writeTstate);
    
    return (writeRow == beamRow);  // Same line = race!
}
```

**3. Routine Classification (Higher-Level Analysis)**

Determine if a routine is timing-sensitive based on its activity distribution:

```cpp
struct RoutineTimingProfile {
    float vblankRatio;    // % of accesses during VBlank
    float screenRatio;    // % during active screen
    float borderRatio;    // % during border
};

RoutineTimingProfile AnalyzeRoutineTiming(uint16_t routineStart, uint16_t routineEnd) {
    auto& stats = GetAggregatedStats(routineStart, routineEnd);
    auto& hist = stats.beamHistogram;
    
    uint32_t total = hist.Total();
    return {
        .vblankRatio = hist.vblankCount / (float)total,
        .screenRatio = hist.screenCount / (float)total,
        .borderRatio = (hist.topBorderCount + hist.bottomBorderCount) / (float)total
    };
}

// Usage: Detect screen routines that safely use VBlank
if (profile.vblankRatio > 0.8f) {
    AddTag(routineAddr, BlockTag::VBlankSafe);
} else if (profile.screenRatio > 0.5f) {
    AddTag(routineAddr, BlockTag::RasterSensitive);
}
```

**4. Interrupt Analysis (ISR Timing)**

Determine where the beam is when ISR completes:

```cpp
InterruptTimingInfo AnalyzeISR(const InterruptEvent& event) {
    BeamRegion entryRegion = GetBeamRegion(event.entryTstate);    // Always VBlank (interrupt fires at frame start)
    BeamRegion exitRegion = GetBeamRegion(event.exitTstate);
    
    return {
        .entryRegion = entryRegion,
        .exitRegion = exitRegion,
        .overlapsScreen = (exitRegion == BeamRegion::Screen),
        .durationTstates = event.exitTstate - event.entryTstate
    };
}
```

#### Memory Cost

| Model | Frame Size | LUT Size |
|-------|-----------|----------|
| 48K | 69,888 T-states | 68 KB |
| 128K | 70,908 T-states | 69 KB |
| Pentagon | 71,680 T-states | 70 KB |

This is a one-time allocation at startup, reused for all analysis.

### 5.3 Frame-Relative T-State (Avoid Modulo)

#### Purpose

The Frame-Relative T-State answers the question: **"How far into the current frame is the CPU?"**

This is needed because:
1. **Z80's `clock_count` is monotonic** — it counts total cycles since power-on, not within current frame
2. **Modulo is expensive** — computing `tstate % FRAME_SIZE` on every memory access is costly
3. **Frame boundaries matter** — all beam correlation depends on knowing position within the frame (0 to 69,887 for 48K)

#### Existing Infrastructure

The Z80 already has several timing-related fields in `z80.h`:

```cpp
// Z80Registers (z80.h lines 17-25)
union {
    uint32_t tt;         // Scaled T-state counter (tt = t * rate)
    struct {
        unsigned t_l : 8;
        unsigned t : 24;  // Current T-state (scaled by rate)
    };
};

// Z80Registers (lines 198-199)
uint32_t cycle_count;      // Counter to track cycle accuracy
uint32_t tStatesPerFrame;  // How many t-states already spent during current frame  <-- EXISTS BUT UNUSED

// Z80State (line 240)
uint64_t clock_count;      // Monotonically increasing clock edge counter

// Z80State (line 266)
uint32_t tpi;              // Ticks per interrupt (CPU cycles per video frame)
```

**Key Finding:** `tStatesPerFrame` already exists but is **not actively populated** in the current implementation.

#### What's Available Now

| Field | Location | Purpose | Currently Used? |
|-------|----------|---------|-----------------|
| `clock_count` | Z80State | Global monotonic counter | ✅ Yes (tape, timing) |
| `t` | Z80Registers | Frame-relative (scaled by `rate`) | ✅ Yes (frame loop) |
| `tt` | Z80Registers | `t * rate` for frequency scaling | ✅ Yes |
| `tStatesPerFrame` | Z80Registers | Declared for frame-relative counting | ❌ **Not populated** |
| `tpi` | Z80State | Ticks per interrupt (frame size) | ✅ Yes (config) |

#### What Needs to Change

The `t` field already provides frame-relative T-state, but it's scaled by `rate` for CPU frequency emulation. Two options:

**Option 1: Use Existing `t` Field Directly**

```cpp
// In IncrementCPUCyclesCounter() - z80.cpp line 650
void Z80::IncrementCPUCyclesCounter(uint8_t cycles) {
    tt += cycles * rate;     // Already incremented
    cycle_count += cycles;   // Already incremented
    clock_count += cycles;   // Already incremented
    // t is derived from tt, so it's already available
}

// For beam correlation, use:
inline uint32_t GetFrameTstate() const {
    return t;  // Already frame-relative! Reset happens in Z80FrameCycle()
}
```

**Note:** The `t` field is already reset implicitly at frame end since the frame loop in `Z80FrameCycle()` compares against `frameLimit` and renders `t` frame-relative.

**Option 2: Populate the Existing `tStatesPerFrame` Field**

The field exists but isn't populated. To use it:

```cpp
// In Z80FrameCycle() - at frame start
tStatesPerFrame = 0;

// In IncrementCPUCyclesCounter()
void Z80::IncrementCPUCyclesCounter(uint8_t cycles) {
    tt += cycles * rate;
    cycle_count += cycles;
    clock_count += cycles;
    tStatesPerFrame += cycles;  // ADD: Populate existing field
}
```

#### Recommendation

**Use the existing `t` field** — it's already frame-relative and requires no code changes.

The key is understanding how `t` is adjusted at frame boundaries. Looking at `Core::AdjustFrameCounters()`:

```cpp
// core.cpp lines 584-601
void Core::AdjustFrameCounters() {
    uint32_t scaledFrame = _config->frame * _state->current_z80_frequency_multiplier;

    if (_z80->t < scaledFrame)
        return;

    _state->frame_counter++;

    // Re-adjust CPU frame t-state counter (SUBTRACTION, not reset!)
    _z80->t -= scaledFrame;      // Wraps by subtracting frame size
    _z80->eipos -= scaledFrame;  // Also adjust EI instruction position
}
```

**Important:** This is `t -= scaledFrame` (subtraction), **not** `t = 0` (reset). This means:
- If frame ended at `t = 69900` (12 cycles past frame boundary), next frame starts at `t = 12`
- This preserves cycle-accurate continuity across frame boundaries
- For beam correlation, this is exactly what we need

**Implication for Analysis:** The `t` value is always valid for beam lookups within the current frame context. At frame boundary, values >frameSize mean the frame ended mid-instruction, and the excess rolls into the next frame.

#### How It's Used

**1. Memory Tracker Hook (Hot Path)**

Pass the frame T-state to the tracker:

```cpp
void Memory::Write(uint16_t addr, uint8_t val) {
    _ram[addr] = val;
    
    if (_trackingEnabled) {
        uint32_t frameTstate = _cpu->t;  // Use existing field!
        _tracker->OnWrite(addr, val, _currentPC, frameTstate);
    }
}
```

**2. Beam Region Lookup**

The frame T-state is the key into the Pre-Computed Region LUT:

```cpp
void Tracker::OnWrite(uint16_t addr, uint8_t val, uint16_t caller, uint32_t frameTstate) {
    BeamRegion region = _beamService->GetBeamRegion(frameTstate);  // O(1)
    _stats[addr].beamHistogram.Increment(region);
}
```

#### Memory Cost

| Field | Size | Status |
|-------|------|--------|
| `t` | Part of existing `tt` union | ✅ Already exists |
| `tStatesPerFrame` | 4 bytes | Exists but unused |

**No new allocation required** — existing infrastructure is sufficient.

### 5.4 Lock-Free Ring Buffer

#### Purpose

The Lock-Free Ring Buffer answers the question: **"How do we log every memory access without blocking the emulator?"**

This is needed because:
1. **Full event logging is expensive** — logging every memory access could slow emulation
2. **Locks cause stalls** — mutex contention would destroy real-time performance
3. **Events are needed for post-analysis** — detailed timing analysis requires event-level data, not just histograms

#### Design Goals

| Goal | Solution |
|------|----------|
| No locks on write | Atomic `fetch_add` for slot allocation |
| Cache-friendly | 64-byte alignment (one cache line per control field) |
| Fixed memory | Pre-allocated array, no runtime allocation |
| Bounded | Oldest events overwritten when full |

#### Implementation

```cpp
template<size_t CAPACITY = 262144>  // 256K events = ~6 MB (24 bytes/event)
class alignas(64) TimedAccessRingBuffer {
    inline void Push(const TimedAccessEvent& event) {
        // Lock-free: atomic increment, no mutex
        size_t slot = _writePos.fetch_add(1, std::memory_order_relaxed) % CAPACITY;
        _buffer[slot] = event;
    }
    
    // Read interface for analysis (called between frames or when paused)
    inline size_t GetWritePos() const { return _writePos.load(std::memory_order_acquire); }
    inline const TimedAccessEvent& At(size_t index) const { return _buffer[index % CAPACITY]; }
    
    void Clear() { _writePos.store(0, std::memory_order_release); }
    
private:
    alignas(64) std::atomic<size_t> _writePos{0};  // Own cache line
    std::array<TimedAccessEvent, CAPACITY> _buffer;
};
```

#### Event Structure

Each event captures the full context needed for beam correlation:

```cpp
struct TimedAccessEvent {
    uint16_t addr;          // Target address
    uint16_t caller;        // PC that initiated access
    uint32_t frameTstate;   // T-state within frame
    uint16_t frameNumber;   // Which frame (for cross-frame analysis)
    uint8_t  value;         // Data written/read
    uint8_t  flags;         // READ/WRITE, beamRegion (packed)
};  // 12 bytes per event (or 16/24 with alignment)
```

#### How It's Used

**1. Real-Time Logging (Hot Path)**

Every tracked memory access pushes an event:

```cpp
void Tracker::OnWrite(uint16_t addr, uint8_t val, uint16_t caller, uint32_t frameTstate) {
    // Fast histogram update (always)
    BeamRegion region = GetBeamRegion(frameTstate);
    _stats[addr].beamHistogram.Increment(region);
    
    // Full logging (optional, for detailed analysis)
    if (_fullLoggingEnabled) {
        _eventLog.Push({
            .addr = addr,
            .caller = caller,
            .frameTstate = frameTstate,
            .frameNumber = _currentFrame,
            .value = val,
            .flags = static_cast<uint8_t>(region) | FLAG_WRITE
        });
    }
}
```

**2. Post-Frame Analysis**

After a frame completes (or when paused), analyze the logged events:

```cpp
void AnalyzeFrameEvents() {
    size_t end = _eventLog.GetWritePos();
    size_t start = (end > CAPACITY) ? (end - CAPACITY) : 0;
    
    for (size_t i = start; i < end; i++) {
        const auto& event = _eventLog.At(i);
        
        // Example: Detect writes to same VRAM address from multiple callers
        if (IsVRAM(event.addr)) {
            _vramWriterMap[event.addr].insert(event.caller);
        }
    }
}
```

**3. Targeted Queries**

Find specific patterns in the event stream:

```cpp
// Find all writes to a specific address range during screen drawing
std::vector<TimedAccessEvent> FindRacingBitmap(uint16_t startAddr, uint16_t endAddr) {
    std::vector<TimedAccessEvent> results;
    
    size_t pos = _eventLog.GetWritePos();
    for (size_t i = 0; i < std::min(pos, CAPACITY); i++) {
        const auto& event = _eventLog.At(pos - 1 - i);
        
        if (event.addr >= startAddr && event.addr < endAddr) {
            BeamRegion region = static_cast<BeamRegion>(event.flags & 0x0F);
            if (region == BeamRegion::Screen) {
                results.push_back(event);
            }
        }
    }
    return results;
}
```

**4. Export for External Analysis**

Dump events for visualization in external tools:

```cpp
void ExportEventsToCSV(const std::string& filename) {
    std::ofstream out(filename);
    out << "frame,tstate,addr,value,caller,region\n";
    
    size_t pos = _eventLog.GetWritePos();
    for (size_t i = 0; i < std::min(pos, CAPACITY); i++) {
        const auto& e = _eventLog.At(i);
        out << e.frameNumber << "," << e.frameTstate << ","
            << std::hex << e.addr << "," << (int)e.value << ","
            << e.caller << "," << (e.flags & 0x0F) << "\n";
    }
}
```

#### Capacity Trade-offs

| Capacity | Memory | Frames Covered* | Use Case |
|----------|--------|-----------------|----------|
| 64K events | ~1.5 MB | ~3 frames | Quick debugging |
| 256K events | ~6 MB | ~12 frames | Detailed analysis |
| 1M events | ~24 MB | ~50 frames | Long-running capture |

*Assuming ~20K events per frame (typical game)

#### Thread Safety Note

The ring buffer is designed for single-producer (emulator thread) and single-consumer (analysis, when paused). For multi-threaded analysis while running, additional synchronization would be needed for the read path.

---

## 6. Comparison / Analysis

### 6.1 Performance Comparison

| Option | Disabled | Histogram | Full Log | Notes |
|--------|----------|-----------|----------|-------|
| **A: API Extension** | N/A | ~5% | ~10% | Always has overhead |
| **B: Template** | **0%** | ~1-2% | ~5% | Best performance |
| **C: Sampling** | N/A | ~0.5% | N/A | Statistical only |
| **D: Hybrid** | ~0.5% | ~1% | ~5%* | *Monitored regions |

### 6.2 Feature Comparison

| Option | Zero-Cost Disabled | Runtime Switch | Full Precision | Easy Impl |
|--------|-------------------|----------------|----------------|------------|
| **A** | ❌ | ✅ | ✅ | ✅ |
| **B** | ✅ | ❌ | ✅ | ⚠️ |
| **C** | ⚠️ | ✅ | ❌ | ✅ |
| **D** | ⚠️ | ✅ | ✅ (partial) | ⚠️ |

### 6.3 Use Case Coverage

| Use Case | Option A | Option B | Option C | Option D |
|----------|----------|----------|----------|----------|
| [UC-TIM-01](./analysis-use-cases.md#uc-tim-01): Beam position | ✅ | ✅ | ⚠️ ~1K T-state | ✅ |
| [UC-TIM-02](./analysis-use-cases.md#uc-tim-02): Raster effects | ✅ | ✅ | ⚠️ | ✅ |
| [UC-TIM-03](./analysis-use-cases.md#uc-tim-03): Beam racing | ✅ | ✅ | ❌ | ✅ (monitored) |
| [UC-TIM-04](./analysis-use-cases.md#uc-tim-04): Interrupt jitter | ✅ | ✅ | ❌ | ✅ |
| [UC-TIM-05](./analysis-use-cases.md#uc-tim-05): ISR duration | ✅ | ✅ | ⚠️ | ✅ |

### 6.4 Memory Budget

| Component | Option A | Option B | Option C | Option D |
|-----------|----------|----------|----------|----------|
| Region LUT | 72 KB | 72 KB | 72 KB | 72 KB |
| Frame histogram | 64 B | 64 B | 64 B | 64 B |
| Event buffer (256K × 24B) | 6 MB | 6 MB | ~750 KB | 6 MB |
| Z80 frameTstate | 4 B | 4 B | 4 B | 4 B |
| **Total** | **~6.1 MB** | **~6.1 MB** | **~850 KB** | **~6.1 MB** |

---

## 7. Conclusion

### 7.1 Recommendation: Option B (Template-Based) for Core + Option D (Hybrid) for Runtime

**Rationale:**

1. **Production builds** need zero overhead → Template with `TrackingDisabled`
2. **Analysis builds** need full precision → Template with `TrackingFull`
3. **Runtime flexibility** for targeted analysis → Hybrid monitored regions

### 7.2 Implementation Strategy

| Phase | Work | Effort |
|-------|------|--------|
| **1. Foundation** | Add `_frameTstate` to Z80, expose region LUT | 1 day |
| **2. Data Structures** | Implement `TimedAccessEvent`, `FrameBeamHistogram` | 1 day |
| **3. Histogram Mode** | Template-based histogram tracking | 2 days |
| **4. Full Logging** | Ring buffer + full event capture | 2 days |
| **5. Hybrid** | Monitored region on-demand precision | 2 days |
| **6. Analysis** | SIMD aggregation, pattern matching | 1 day |

### 7.3 Key Trade-offs Accepted

| Trade-off | Decision | Rationale |
|-----------|----------|-----------|
| Compile-time vs runtime | Compile-time for core | Zero-cost is critical for 165+ instances |
| Complexity vs performance | Accept template complexity | Performance wins for hot path |
| Precision vs overhead | Default to histogram | Full precision on-demand |

### 7.4 Next Steps

1. [ ] Implement `GetFrameTstate()` in Z80 class
2. [ ] Expose beam region LUT from ScreenZX
3. [ ] Create `FrameBeamHistogram` structure
4. [ ] Prototype Option B with histogram tracking
5. [ ] Benchmark overhead on real workloads

---

## 8. References

### Related Documents
- [Beam-to-Execution Correlation (Main Design)](./beam-to-execution-correlation.md)
- [Analysis Use Cases](./analysis-use-cases.md) — Complete use case catalog; see [Timing Use Cases](./analysis-use-cases.md#10-timing-use-cases) section for UC-TIM-* details
- [Analyzer Architecture](./analyzer-architecture.md)

### Source Files
- [screenzx.h](../../../core/src/emulator/video/zx/screenzx.h) — TstateCoordLUT
- [z80ex/typedefs.h](../../../core/src/3rdparty/z80ex/typedefs.h) — Z80 tstate field
- [memoryaccesstracker.h](../../../core/src/emulator/memory/memoryaccesstracker.h) — Tracking hooks
