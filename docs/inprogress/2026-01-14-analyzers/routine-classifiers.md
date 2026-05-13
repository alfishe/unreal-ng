# Routine Classifiers: Screen, Music, and Sprite Detection

**Purpose:** Heuristics for detecting and classifying specific routine types in ZX Spectrum programs.

**Status:** Design Draft - Not Yet Implemented

**Parent:** [Analyzer Architecture](./analyzer-architecture.md)

**Dependencies:**
- [Block Segmentation](./block-segmentation.md)
- [Data Collection Extensions](./data-collection-extensions.md)

---

## 1. Overview

The Routine Classifier identifies specific routine types by analyzing code behavior patterns. This enables:

| Use Case | Benefit |
|----------|---------|
| Disassembler annotation | Label routines with meaningful names |
| Performance profiling | Identify expensive routines |
| Debugging | Quick navigation to music/graphics code |
| Emulator optimization | Apply specialized fast-paths |
| Reverse engineering | Understand program structure |

### 1.1 Classification Targets

| Routine Type | Description | Key Signatures |
|-------------|-------------|----------------|
| **Screen Clear** | Fill VRAM with constant value | Sequential VRAM writes, loop with LD (HL),A |
| **Screen Scroll** | Shift display contents | VRAM reads + writes to adjacent addresses |
| **Sprite Blit** | Draw sprite to screen | Non-contiguous VRAM writes with address calculation |
| **AY Music** | AY-3-8912 playback | Periodic port 0xFFFD/0xBFFD writes |
| **Beeper Music** | 1-bit audio | Tight loop with port 0xFE writes |
| **COVOX** | Digital audio output | Port 0xFB writes |
| **Keyboard Input** | Read keyboard matrix | Port 0xFE reads with row selection |
| **Tape I/O** | Save/load routines | Specific ROM calls or edge timing |
| **Delay Loop** | Waste time | DJNZ to self, or LD B,n; DJNZ loop |
| **Memory Copy** | Block transfer | LDIR, or LD/INC loop |
| **Memory Fill** | Block fill | Sequential writes with increment |

### 1.2 Confidence Scoring

Each classification has a confidence score (0.0-1.0):

| Confidence | Interpretation |
|------------|----------------|
| 0.9-1.0 | Definite match (multiple strong indicators) |
| 0.7-0.9 | Likely match (one strong or multiple weak indicators) |
| 0.5-0.7 | Possible match (weak indicators only) |
| < 0.5 | Insufficient evidence |

---

## 2. Screen Routine Detection

### 2.1 ZX Spectrum Screen Memory Layout

```
Address Range      Size    Purpose
0x4000-0x57FF     6144    Pixel data (192 lines × 32 columns × 8 pixels)
0x5800-0x5AFF     768     Attributes (24 rows × 32 columns)

Pixel Address Encoding:
  Bits: 010T TSSS LLLC CCCC
  T = Third (0-2)
  S = Scanline within character row (0-7)
  L = Character row (0-7)
  C = Column (0-31)
  
Attribute Address:
  0x5800 + (row × 32) + column
```

### 2.2 Screen Clear Detection

**Signature:**
- Writes to VRAM region 0x4000-0x5AFF
- Sequential or strided address pattern
- Same or similar values written (fill pattern)
- Tight loop (high loop_count)

```cpp
struct ScreenClearSignature {
    float sequentialWriteRatio;  // % of writes that are addr+1
    float valueUniformity;       // % of writes with same value
    uint32_t totalVRAMWrites;
    bool hasLDIRPattern;         // Uses LDIR instruction
    bool hasLoopPattern;         // Uses DJNZ or JR loop
};

float ScoreScreenClear(const ScreenClearSignature& sig) {
    float score = 0.0f;
    
    // Sequential writes are strong indicator
    if (sig.sequentialWriteRatio > 0.9f) score += 0.4f;
    else if (sig.sequentialWriteRatio > 0.7f) score += 0.2f;
    
    // Uniform value is strong indicator
    if (sig.valueUniformity > 0.95f) score += 0.3f;
    else if (sig.valueUniformity > 0.8f) score += 0.15f;
    
    // Significant VRAM coverage
    if (sig.totalVRAMWrites > 6000) score += 0.2f;  // Near-full screen
    else if (sig.totalVRAMWrites > 3000) score += 0.1f;
    
    // LDIR pattern is definitive
    if (sig.hasLDIRPattern) score += 0.1f;
    
    return std::min(score, 1.0f);
}
```

**Common Patterns:**

```asm
; Pattern 1: LDIR clear
    LD HL,#4000
    LD DE,#4001
    LD BC,#17FF
    LD (HL),#00
    LDIR

; Pattern 2: Loop clear
    LD HL,#4000
    LD B,#18        ; 24 rows
.loop:
    LD (HL),#00
    INC HL
    DJNZ .loop

; Pattern 3: Unrolled clear (faster)
    LD HL,#4000
    XOR A
    LD (HL),A
    INC L
    LD (HL),A
    ; ... repeated
```

### 2.3 Screen Scroll Detection

**Signature:**
- VRAM reads followed by VRAM writes
- Source and destination addresses differ by fixed offset
- Pattern repeats for screen height

```cpp
struct ScreenScrollSignature {
    int16_t dominantOffset;      // Most common dest-src offset
    float offsetConsistency;     // % of moves with dominant offset
    uint32_t totalMoves;
    ScrollDirection direction;
};

enum class ScrollDirection {
    Up, Down, Left, Right, Unknown
};

ScrollDirection DetectScrollDirection(int16_t offset) {
    // Vertical scroll: offset = ±256 (one text row = 8 pixel lines × 32 bytes)
    if (offset == 256) return ScrollDirection::Up;
    if (offset == -256) return ScrollDirection::Down;
    
    // Horizontal scroll: offset = ±1
    if (offset == 1) return ScrollDirection::Left;
    if (offset == -1) return ScrollDirection::Right;
    
    // Character row scroll: offset = ±32
    if (offset == 32) return ScrollDirection::Up;
    if (offset == -32) return ScrollDirection::Down;
    
    return ScrollDirection::Unknown;
}

float ScoreScreenScroll(const ScreenScrollSignature& sig) {
    float score = 0.0f;
    
    // Consistent offset is key
    if (sig.offsetConsistency > 0.9f) score += 0.5f;
    else if (sig.offsetConsistency > 0.7f) score += 0.3f;
    
    // Significant data movement
    if (sig.totalMoves > 5000) score += 0.3f;
    else if (sig.totalMoves > 2000) score += 0.15f;
    
    // Recognized scroll direction
    if (sig.direction != ScrollDirection::Unknown) score += 0.2f;
    
    return std::min(score, 1.0f);
}
```

### 2.4 Sprite Blit Detection

**Signature:**
- Non-contiguous VRAM writes
- Address calculation visible (ADD, ADC, OR operations)
- Mask operations (AND with sprite mask)
- Source data from non-VRAM region

```cpp
struct SpriteBlitSignature {
    float addressNonContiguity;   // % of non-sequential writes
    bool hasMaskOperation;        // AND with mask detected
    bool hasSourceRead;           // Reads from sprite data region
    bool hasAddressCalculation;   // ADD/ADC on address
    uint32_t writeCount;
    Rect boundingBox;             // Screen area affected
};

float ScoreSpriteBlit(const SpriteBlitSignature& sig) {
    float score = 0.0f;
    
    // Non-contiguous writes (unlike clear/scroll)
    if (sig.addressNonContiguity > 0.3f) score += 0.3f;
    
    // Masking is strong indicator of sprite
    if (sig.hasMaskOperation) score += 0.3f;
    
    // Source read from different region
    if (sig.hasSourceRead) score += 0.2f;
    
    // Address calculation typical for positioning
    if (sig.hasAddressCalculation) score += 0.2f;
    
    return std::min(score, 1.0f);
}
```

**Common Sprite Patterns:**

```asm
; XOR sprite (simple, no mask)
    LD A,(DE)       ; Read sprite byte
    XOR (HL)        ; Combine with screen
    LD (HL),A       ; Write back
    INC DE
    INC H           ; Next pixel row

; Masked sprite (proper transparency)
    LD A,(DE)       ; Read mask
    AND (HL)        ; Clear sprite area
    INC DE
    OR (DE)         ; Combine sprite data
    LD (HL),A       ; Write back
    INC DE
    INC H
```

---

## 3. Music Routine Detection

### 3.1 AY-3-8912 Music Detection

**Port Addresses:**
```
0xFFFD (or 0xFFFD mask: port & 0xC002 == 0xC000) - Register select
0xBFFD (or 0xBFFD mask: port & 0xC002 == 0x8000) - Data write
```

**Signature:**
- Paired writes: register select then data
- Periodic execution (every 1-2 frames for 50Hz music)
- Writes to registers 0-13 (AY has 14 registers)

```cpp
struct AYMusicSignature {
    uint32_t regSelectWrites;     // Writes to 0xFFFD
    uint32_t dataWrites;          // Writes to 0xBFFD
    bool isPeriodic;              // Regular interval detected
    uint32_t periodTstates;       // If periodic, interval in T-states
    std::set<uint8_t> registersUsed;  // Which AY registers written
    bool duringInterrupt;         // Executed in ISR
};

float ScoreAYMusic(const AYMusicSignature& sig) {
    float score = 0.0f;
    
    // Both register select and data writes
    if (sig.regSelectWrites > 0 && sig.dataWrites > 0) {
        float ratio = std::min(sig.regSelectWrites, sig.dataWrites) /
                      std::max(sig.regSelectWrites, sig.dataWrites);
        if (ratio > 0.5f) score += 0.3f;  // Balanced pair
    }
    
    // Periodicity is very strong indicator
    if (sig.isPeriodic) {
        score += 0.3f;
        // 50Hz music: ~70000 T-states period
        if (sig.periodTstates > 60000 && sig.periodTstates < 80000) {
            score += 0.2f;  // Matches frame rate
        }
    }
    
    // Multiple registers used (real music, not just beeps)
    if (sig.registersUsed.size() >= 5) score += 0.2f;
    else if (sig.registersUsed.size() >= 3) score += 0.1f;
    
    // Interrupt context is typical
    if (sig.duringInterrupt) score += 0.1f;
    
    return std::min(score, 1.0f);
}
```

**AY Register Usage Patterns:**

| Register | Purpose | Music Indicator |
|----------|---------|-----------------|
| 0-5 | Tone period A/B/C | Strong (pitch control) |
| 6 | Noise period | Medium |
| 7 | Mixer | Strong (channel enable) |
| 8-10 | Volume A/B/C | Strong (envelope/volume) |
| 11-12 | Envelope period | Medium |
| 13 | Envelope shape | Medium |

### 3.2 Beeper Music Detection

**Signature:**
- Tight loop with OUT to port 0xFE
- Timing-critical code (few instructions per iteration)
- Often uses DJNZ or DEC/JR NZ loop

```cpp
struct BeeperMusicSignature {
    uint32_t portFEWrites;
    uint32_t loopIterations;      // High count indicates music
    float avgTstatesBetweenOuts;  // Timing between port writes
    bool hasDJNZLoop;
    bool hasTimingDelay;          // Explicit delay in loop
};

float ScoreBeeperMusic(const BeeperMusicSignature& sig) {
    float score = 0.0f;
    
    // Port 0xFE writes
    if (sig.portFEWrites > 100) score += 0.3f;
    else if (sig.portFEWrites > 20) score += 0.15f;
    
    // High iteration count (music plays many notes)
    if (sig.loopIterations > 1000) score += 0.3f;
    else if (sig.loopIterations > 100) score += 0.15f;
    
    // Timing-critical loops
    if (sig.hasDJNZLoop) score += 0.2f;
    if (sig.hasTimingDelay) score += 0.2f;
    
    // Reasonable timing between outs (not too fast, not too slow)
    if (sig.avgTstatesBetweenOuts > 50 && sig.avgTstatesBetweenOuts < 5000) {
        score += 0.2f;
    }
    
    return std::min(score, 1.0f);
}
```

**Common Beeper Patterns:**

```asm
; Simple beep loop
    LD B,#FF
.loop:
    XOR #10         ; Toggle speaker bit
    OUT (#FE),A
    DJNZ .loop

; Dual-tone (more complex)
.loop:
    LD A,E
    ADD A,C         ; Tone 1 accumulator
    LD E,A
    SBC A,A
    AND D           ; Mask for speaker
    LD H,A
    
    LD A,L
    ADD A,B         ; Tone 2 accumulator
    LD L,A
    SBC A,A
    AND D
    XOR H           ; Combine tones
    OUT (#FE),A
    
    DEC IX
    JP NZ,.loop
```

### 3.3 COVOX/Digital Audio Detection

**Port Address:** 0xFB (COVOX) or other depending on hardware

**Signature:**
- Continuous stream of port writes
- High write rate (sample playback)
- Values vary significantly (audio waveform)

```cpp
struct COVOXSignature {
    uint32_t portWrites;
    uint16_t port;
    float valueVariance;          // How much values change
    float writeRate;              // Writes per frame
};

float ScoreCOVOX(const COVOXSignature& sig) {
    float score = 0.0f;
    
    // High write rate (digital audio = many samples)
    if (sig.writeRate > 1000) score += 0.4f;
    else if (sig.writeRate > 100) score += 0.2f;
    
    // Value variance (real audio varies a lot)
    if (sig.valueVariance > 50) score += 0.3f;
    else if (sig.valueVariance > 20) score += 0.15f;
    
    // Known COVOX port
    if (sig.port == 0xFB) score += 0.3f;
    
    return std::min(score, 1.0f);
}
```

---

## 4. Input Routine Detection

### 4.1 Keyboard Polling Detection

**Signature:**
- IN from port 0xFE
- High byte varies (row selection)
- Multiple reads (scanning keyboard matrix)

```cpp
struct KeyboardSignature {
    uint32_t portFEReads;
    std::set<uint8_t> rowsScanned;  // Different high bytes used
    bool hasCompareLogic;            // CP/BIT after read
    bool duringInterrupt;
};

float ScoreKeyboardPolling(const KeyboardSignature& sig) {
    float score = 0.0f;
    
    // Multiple port 0xFE reads
    if (sig.portFEReads >= 8) score += 0.4f;      // Full keyboard scan
    else if (sig.portFEReads >= 3) score += 0.2f;
    
    // Multiple rows scanned
    if (sig.rowsScanned.size() >= 5) score += 0.3f;
    else if (sig.rowsScanned.size() >= 2) score += 0.15f;
    
    // Comparison after read (checking for key)
    if (sig.hasCompareLogic) score += 0.2f;
    
    // Often in interrupt (but not always)
    if (sig.duringInterrupt) score += 0.1f;
    
    return std::min(score, 1.0f);
}
```

**Keyboard Matrix:**

```
High byte    Keys
0xFE         CAPS-Z-X-C-V
0xFD         A-S-D-F-G
0xFB         Q-W-E-R-T
0xF7         1-2-3-4-5
0xEF         0-9-8-7-6
0xDF         P-O-I-U-Y
0xBF         ENTER-L-K-J-H
0x7F         SPACE-SYM-M-N-B
```

### 4.2 Joystick Detection

**Signature:**
- IN from Kempston port (0x1F or 0xDF)
- IN from Sinclair joystick via keyboard port
- Followed by direction checks (BIT operations)

```cpp
struct JoystickSignature {
    bool hasKempstonRead;         // Port 0x1F/0xDF
    bool hasSinclairPattern;      // Keyboard port with specific rows
    bool hasDirectionBits;        // BIT 0-4 checks after read
};

float ScoreJoystick(const JoystickSignature& sig) {
    float score = 0.0f;
    
    if (sig.hasKempstonRead) score += 0.5f;
    if (sig.hasSinclairPattern) score += 0.3f;
    if (sig.hasDirectionBits) score += 0.2f;
    
    return std::min(score, 1.0f);
}
```

---

## 5. Utility Routine Detection

### 5.1 Delay Loop Detection

**Signature:**
- Loop with no memory side effects
- High iteration count
- Often DJNZ or DEC/JR NZ

```cpp
struct DelaySignature {
    uint32_t iterations;
    uint32_t tstatesPerIteration;
    bool hasMemoryAccess;         // False for pure delay
    bool hasDJNZ;
};

float ScoreDelay(const DelaySignature& sig) {
    float score = 0.0f;
    
    // No memory access (pure delay)
    if (!sig.hasMemoryAccess) score += 0.4f;
    
    // High iteration count
    if (sig.iterations > 100) score += 0.3f;
    else if (sig.iterations > 10) score += 0.15f;
    
    // DJNZ is classic delay pattern
    if (sig.hasDJNZ) score += 0.3f;
    
    return std::min(score, 1.0f);
}
```

### 5.2 Memory Copy Detection

**Signature:**
- LDIR instruction
- Or: LD (DE),A / INC DE / INC HL / DJNZ loop

```cpp
struct MemoryCopySignature {
    bool hasLDIR;
    bool hasLDDR;                 // Backwards copy
    bool hasManualLoop;           // LD/INC/DJNZ pattern
    uint32_t bytesTransferred;
    uint16_t sourceStart, sourceEnd;
    uint16_t destStart, destEnd;
};

float ScoreMemoryCopy(const MemoryCopySignature& sig) {
    float score = 0.0f;
    
    // LDIR/LDDR is definitive
    if (sig.hasLDIR || sig.hasLDDR) score += 0.5f;
    
    // Manual loop with LD pattern
    if (sig.hasManualLoop) score += 0.3f;
    
    // Significant data transfer
    if (sig.bytesTransferred > 100) score += 0.2f;
    
    return std::min(score, 1.0f);
}
```

---

## 6. Classification Algorithm

### 6.1 Overall Flow

```
┌────────────────────────────────────────────────────────────────────┐
│                    Routine Classification Flow                      │
└────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
            ┌──────────────────────────────────────────┐
            │     Input: Code region (start, end)      │
            │     + Execution traces                   │
            │     + Port access logs                   │
            │     + Memory access patterns             │
            └──────────────────────────────────────────┘
                                  │
                                  ▼
            ┌──────────────────────────────────────────┐
            │         Extract Feature Vectors          │
            │   - VRAM write patterns                  │
            │   - Port access patterns                 │
            │   - Loop structures                      │
            │   - Instruction categories               │
            └──────────────────────────────────────────┘
                                  │
         ┌────────────────────────┼────────────────────────┐
         │                        │                        │
         ▼                        ▼                        ▼
   ┌───────────────┐      ┌───────────────┐      ┌───────────────┐
   │ Screen        │      │ Music         │      │ Input         │
   │ Classifiers   │      │ Classifiers   │      │ Classifiers   │
   ├───────────────┤      ├───────────────┤      ├───────────────┤
   │ - Clear       │      │ - AY          │      │ - Keyboard    │
   │ - Scroll      │      │ - Beeper      │      │ - Joystick    │
   │ - Sprite      │      │ - COVOX       │      │               │
   └───────────────┘      └───────────────┘      └───────────────┘
         │                        │                        │
         └────────────────────────┼────────────────────────┘
                                  │
                                  ▼
            ┌──────────────────────────────────────────┐
            │        Score All Classifiers             │
            │        Pick highest confidence           │
            │        (if above threshold)              │
            └──────────────────────────────────────────┘
                                  │
                                  ▼
            ┌──────────────────────────────────────────┐
            │        Output: Classification            │
            │   { type, confidence, evidence }         │
            └──────────────────────────────────────────┘
```

### 6.2 Classification Pipeline

```cpp
class RoutineClassifier {
public:
    struct Result {
        RoutineType type;
        float confidence;
        std::string evidence;
        uint16_t startAddress;
        uint16_t endAddress;
    };
    
    Result Classify(uint16_t start, uint16_t end, const AnalysisContext& ctx) {
        // Extract features
        auto screenFeatures = ExtractScreenFeatures(start, end, ctx);
        auto musicFeatures = ExtractMusicFeatures(start, end, ctx);
        auto inputFeatures = ExtractInputFeatures(start, end, ctx);
        
        // Score each classifier
        std::vector<std::pair<RoutineType, float>> scores;
        
        scores.push_back({RoutineType::ScreenClear, ScoreScreenClear(screenFeatures.clear)});
        scores.push_back({RoutineType::ScreenScroll, ScoreScreenScroll(screenFeatures.scroll)});
        scores.push_back({RoutineType::SpriteBlit, ScoreSpriteBlit(screenFeatures.sprite)});
        scores.push_back({RoutineType::AYMusic, ScoreAYMusic(musicFeatures.ay)});
        scores.push_back({RoutineType::BeeperMusic, ScoreBeeperMusic(musicFeatures.beeper)});
        scores.push_back({RoutineType::KeyboardPolling, ScoreKeyboardPolling(inputFeatures.keyboard)});
        // ... etc
        
        // Find highest score
        auto best = std::max_element(scores.begin(), scores.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
        
        if (best->second >= _confidenceThreshold) {
            return {
                .type = best->first,
                .confidence = best->second,
                .evidence = GenerateEvidence(best->first, /* features */),
                .startAddress = start,
                .endAddress = end
            };
        }
        
        return {.type = RoutineType::Unknown, .confidence = 0.0f};
    }
    
private:
    float _confidenceThreshold = 0.7f;
};
```

### 6.3 Batch Classification

```cpp
std::vector<RoutineClassifier::Result> ClassifyAllRoutines(
    const std::vector<FunctionBoundary>& functions,
    const AnalysisContext& ctx) 
{
    std::vector<Result> results;
    
    for (const auto& func : functions) {
        auto result = classifier.Classify(func.entryPoint, 
                                          func.exitPoint.value_or(func.entryPoint + 256),
                                          ctx);
        if (result.type != RoutineType::Unknown) {
            results.push_back(result);
        }
    }
    
    return results;
}
```

---

## 7. Evidence Generation

### 7.1 Human-Readable Evidence

```cpp
std::string GenerateEvidence(RoutineType type, const FeatureSet& features) {
    switch (type) {
        case RoutineType::ScreenClear:
            return fmt::format(
                "VRAM writes: {}, sequential: {:.0f}%, value uniformity: {:.0f}%",
                features.screen.clear.totalVRAMWrites,
                features.screen.clear.sequentialWriteRatio * 100,
                features.screen.clear.valueUniformity * 100);
            
        case RoutineType::AYMusic:
            return fmt::format(
                "AY register writes: {}, data writes: {}, periodic: {}, "
                "registers used: {}",
                features.music.ay.regSelectWrites,
                features.music.ay.dataWrites,
                features.music.ay.isPeriodic ? "yes" : "no",
                features.music.ay.registersUsed.size());
            
        case RoutineType::KeyboardPolling:
            return fmt::format(
                "Port 0xFE reads: {}, rows scanned: {}",
                features.input.keyboard.portFEReads,
                features.input.keyboard.rowsScanned.size());
            
        // ... etc
    }
    return "";
}
```

### 7.2 Structured Evidence

```cpp
struct Evidence {
    std::vector<std::string> positiveIndicators;
    std::vector<std::string> negativeIndicators;
    std::vector<uint16_t> keyInstructions;  // Addresses of significant instructions
    std::vector<uint16_t> keyPorts;         // Ports accessed
};
```

---

## 8. Test Specifications

### 8.1 Screen Clear Tests

```cpp
TEST(RoutineClassifier, DetectsLDIRScreenClear) {
    // Given: LDIR-based screen clear pattern
    //   LD HL,#4000; LD DE,#4001; LD BC,#17FF; LD (HL),0; LDIR
    SetupCode(0x8000, {0x21, 0x00, 0x40,   // LD HL,#4000
                       0x11, 0x01, 0x40,   // LD DE,#4001
                       0x01, 0xFF, 0x17,   // LD BC,#17FF
                       0x36, 0x00,         // LD (HL),0
                       0xED, 0xB0});       // LDIR
    
    // Execute and capture access patterns
    RunCode(0x8000);
    
    // When: Classifier runs
    auto result = classifier.Classify(0x8000, 0x800D, ctx);
    
    // Then: Screen clear detected with high confidence
    EXPECT_EQ(result.type, RoutineType::ScreenClear);
    EXPECT_GE(result.confidence, 0.9f);
}

TEST(RoutineClassifier, DetectsLoopScreenClear) {
    // Given: Loop-based screen clear
    SetupCode(0x8000, {0x21, 0x00, 0x40,   // LD HL,#4000
                       0x06, 0x00,         // LD B,0 (256 iterations)
                       0x36, 0x00,         // LD (HL),0
                       0x23,               // INC HL
                       0x10, 0xFB});       // DJNZ -5
    
    RunCode(0x8000);
    
    auto result = classifier.Classify(0x8000, 0x800A, ctx);
    
    EXPECT_EQ(result.type, RoutineType::ScreenClear);
    EXPECT_GE(result.confidence, 0.7f);
}
```

### 8.2 Music Detection Tests

```cpp
TEST(RoutineClassifier, DetectsAYMusicPlayer) {
    // Given: AY music playback pattern
    SetupCode(0x8000, {
        0x3E, 0x07,         // LD A,#07 (mixer register)
        0xD3, 0xFD,         // OUT (#FD),A → 0xFFFD
        0x3E, 0x38,         // LD A,#38 (mixer value)
        0xD3, 0xBF,         // OUT (#BF),A → 0xBFFD
        // ... more register writes
    });
    
    // Run multiple times (simulate periodic execution)
    for (int i = 0; i < 50; i++) {
        RunCode(0x8000);
    }
    
    auto result = classifier.Classify(0x8000, 0x8010, ctx);
    
    EXPECT_EQ(result.type, RoutineType::AYMusic);
    EXPECT_GE(result.confidence, 0.8f);
}

TEST(RoutineClassifier, DetectsBeeperRoutine) {
    // Given: Beeper music pattern
    SetupCode(0x8000, {
        0x06, 0xFF,         // LD B,#FF
        0xAF,               // XOR A → toggle
        0xEE, 0x10,         // XOR #10 (speaker bit)
        0xD3, 0xFE,         // OUT (#FE),A
        0x10, 0xFA});       // DJNZ -6
    
    RunCode(0x8000);
    
    auto result = classifier.Classify(0x8000, 0x8009, ctx);
    
    EXPECT_EQ(result.type, RoutineType::BeeperMusic);
    EXPECT_GE(result.confidence, 0.75f);
}
```

### 8.3 Input Detection Tests

```cpp
TEST(RoutineClassifier, DetectsFullKeyboardScan) {
    // Given: Full keyboard matrix scan
    SetupCode(0x8000, {
        0x01, 0xFE, 0xFE,   // LD BC,#FEFE
        0xED, 0x78,         // IN A,(C)
        0xCB, 0x00,         // RLC B
        0x30, 0xFA          // JR NC,-6 (scan all rows)
    });
    
    RunCode(0x8000);
    
    auto result = classifier.Classify(0x8000, 0x800A, ctx);
    
    EXPECT_EQ(result.type, RoutineType::KeyboardPolling);
    EXPECT_GE(result.confidence, 0.85f);
}
```

---

## 9. Integration

### 9.1 Disassembler Integration

```cpp
// In disassembler output
void RenderWithClassifications(uint16_t start, uint16_t end,
                                const std::vector<RoutineClassifier::Result>& classifications) {
    for (uint16_t addr = start; addr < end; ) {
        // Check if this address starts a classified routine
        for (const auto& cls : classifications) {
            if (cls.startAddress == addr) {
                RenderComment(fmt::format("; {} (confidence: {:.0f}%)",
                    RoutineTypeToString(cls.type),
                    cls.confidence * 100));
                RenderComment(fmt::format("; Evidence: {}", cls.evidence));
            }
        }
        
        // Render instruction
        // ...
    }
}
```

### 9.2 Export Format

```yaml
routine_classifications:
  - start: 0x8000
    end: 0x8050
    type: "screen_clear"
    confidence: 0.95
    evidence:
      vram_writes: 6912
      sequential_ratio: 0.98
      value_uniformity: 1.0
      has_ldir: true
      
  - start: 0x8100
    end: 0x8200
    type: "ay_music"
    confidence: 0.88
    evidence:
      reg_select_writes: 14
      data_writes: 14
      is_periodic: true
      period_tstates: 69888
      registers_used: [0, 1, 2, 3, 4, 5, 7, 8, 9, 10]
      during_interrupt: true
      
  - start: 0x8300
    end: 0x8350
    type: "keyboard_polling"
    confidence: 0.92
    evidence:
      port_fe_reads: 8
      rows_scanned: [0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F]
```

---

## 10. Future Enhancements

### 10.1 Machine Learning Classifier

For improved accuracy, train a classifier on labeled examples:

```cpp
class MLRoutineClassifier {
public:
    // Train on labeled examples
    void Train(const std::vector<LabeledRoutine>& examples);
    
    // Classify using trained model
    Result Classify(const FeatureVector& features);
    
private:
    // Simple decision tree or random forest
    DecisionTree _model;
};
```

### 10.2 Cross-Reference with Known Routines

Match against database of known ROM routines:

```cpp
struct KnownRoutine {
    uint16_t address;
    std::string name;
    RoutineType type;
    std::string source;  // "48K ROM", "128K ROM", etc.
};

std::optional<KnownRoutine> MatchROMRoutine(uint16_t address) {
    static const std::vector<KnownRoutine> romRoutines = {
        {0x0D6B, "CL_ALL", RoutineType::ScreenClear, "48K ROM"},
        {0x0EDF, "KEY_SCAN", RoutineType::KeyboardPolling, "48K ROM"},
        // ... etc
    };
    
    for (const auto& r : romRoutines) {
        if (r.address == address) return r;
    }
    return std::nullopt;
}
```

---

## 11. References

### Parent Documents
- [Analyzer Architecture](./analyzer-architecture.md)

### Dependencies
- [Block Segmentation](./block-segmentation.md)
- [Data Collection Extensions](./data-collection-extensions.md)
- [Interrupt Analyzer](./interrupt-analyzer.md)

### ZX Spectrum Technical References
- Complete Spectrum ROM Disassembly
- AY-3-8912 Technical Manual
- ZX Spectrum Technical FAQ
