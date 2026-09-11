# TTD Peripheral Registry Integration Plan

**Created:** 2026-09-10  
**Status:** Phase 1 COMPLETE, Phase 2-5 TODO  
**Branch:** atm  

## Summary

This plan addresses findings from the TTD code review. The peripheral registry is implemented but not wired end-to-end. This document tracks all required work to complete the integration.

---

## Phase 1: Bug Fixes (BLOCKING)

### 1.1 Use-after-free in RestoreAll
- **Status:** FIXED
- **File:** `core/src/debugger/ttd/ttdperipheralregistry.cpp`
- **Issue:** `prevState` vector scoped inside `if` block, `prevData` dangles after block closes
- **Fix:** Move `prevState` to function scope

### 1.2 Silent corruption on delta-without-prev
- **Status:** FIXED
- **File:** `core/src/debugger/ttd/ttdperipheralregistry.cpp`
- **Issue:** Delta blob without predecessor returns raw XOR data as if it were valid state
- **Fix:** Return empty vector (hard fail) when isDelta set but prev unavailable

### 1.3 uncompressedSize uint16_t cap
- **Status:** FIXED
- **Files:** `ttdperipheralregistry.h`, `ttdperipheralregistry.cpp`
- **Issue:** 64KB cap truncates GeneralSound 512KB SRAM
- **Fix:** Changed to uint32_t, updated header size to 12 bytes

### 1.4 Doc inconsistencies
- **Status:** FIXED
- **Files:** `ttdserializable.h`, TDD doc
- **Issues:**
  - LZ4 reference (actual: zstd-1)
  - "checkpoints never persist" (SerializeSession exists)
  - Profi 1024K and ATM3 4MB missing from scalability goals
- **Fix:** Updated all references

### 1.6 Python verification tools sync
- **Status:** FIXED
- **Files:** `ttd.ksy`, `tools/verification/ttd-analyzer/src/ttd_format.py`
- **Issue:** Missing `videoMode` field in chipset state
- **Fix:** Added `video_mode` field to both Kaitai schema and Python parser

### 1.5 Dizzy Y divergence test failure
- **Status:** FIXED (side effect of bugs 1.1-1.3)
- **Test:** `TTD_Divergence_Corpus_Test.DizzyY_48K_CaptureRestoreRoundTrip`
- **Symptom:** Was "Snapshot hash mismatch at frame 20"
- **Resolution:** Test passes after fixing use-after-free and header size issues

---

## Phase 2: Registry Wiring (P0)

### 2.1 Replace legacy 4-slot blob vectors
**Files to modify:**
- `timetravelmanager.cpp`: CaptureNow, RestoreCheckpoint
- `timetravelmanager.cpp`: SerializeSession, DeserializeSession
- `timetravelmanager.cpp`: EstimateSessionHeapBytes

**Current state:**
```cpp
// Legacy 4-slot:
cp.turboSoundBlob  // Always allocated regardless of connected
cp.betaDiskBlob
cp.tapeBlob
cp.covoxBlob
```

**Target state:**
```cpp
// Registry-driven:
cp.peripheralBlobs  // Map<PeripheralId, vector<uint8_t>>
                    // Only connected devices present
```

### 2.2 Device registration at construction
**Files to modify:**
- Each peripheral's constructor (AY, TurboSound, WD1793, Tape, Covox)
- Model reset path (P1.6 Unregister)

**Pattern:**
```cpp
TurboSound::TurboSound(EmulatorContext* ctx)
{
    // ... existing init ...
    if (ctx->pTimeTravelManager)
        ctx->pTimeTravelManager->GetRegistry().Register(
            PeripheralId::TurboSound, this);
}

TurboSound::~TurboSound()
{
    if (_context->pTimeTravelManager)
        _context->pTimeTravelManager->GetRegistry().Unregister(
            PeripheralId::TurboSound);
}
```

### 2.3 Presence-set serialization
**Design:** Session header carries device table (which peripherals were connected at session start). Per-checkpoint presence bitmask indicates which subset has changed state.

**File format change:**
```
[Header]
  ...existing fields...
  device_count: u8
  device_ids: u8[device_count]  // PeripheralId values

[Checkpoint]
  ...existing fields...
  peripheral_presence: u8  // Bitmask of which devices have blobs
  peripheral_blobs: ...    // Only present devices
```

### 2.4 Random-access delta decoding
**Issue:** XOR-delta blob requires K-1 predecessor to decode checkpoint K. SeekTo(K) from cold has no K-1.

**Solutions (evaluate in POC):**
1. **I-frame anchors:** Every N checkpoints stores full state (like video keyframes)
2. **Chain walking:** Store prev_slot index, reconstruct on demand (like RAM page store)
3. **Hybrid:** Full state for small peripherals, delta with I-frames for large (GS)

---

## Phase 3: Proofs and Benchmarks (P1)

### 3.1 POC: Zero-bytes for non-connected peripherals
**Goal:** Prove that non-connected peripherals add exactly 0 bytes to serialized session.

**Test design:**
```cpp
TEST(TTD_Stream_Overhead, NonConnectedPeripheralsZeroBytes)
{
    // Session A: 48K, no BDI/tape/covox
    auto sizeA = RunAndSerialize(Config::Spectrum48K, frames=1000);
    
    // Session B: 128K + BDI + tape + covox
    auto sizeB = RunAndSerialize(Config::Pentagon128_BDI_Tape_Covox, frames=1000);
    
    // Per-checkpoint overhead for non-connected should be 0
    // (presence bitmask + only present device blobs)
    auto perCpOverheadA = sizeA / 1000;
    auto perCpOverheadB = sizeB / 1000;
    
    // With presence-set format, A should not pay for peripherals it lacks
    EXPECT_EQ(perCpOverheadA, baseline);  // No peripheral overhead
}
```

**Location:** `core/tests/debugger/ttd/ttdstreamoverhead_test.cpp`

### 3.2 Benchmark: Peripheral capture/restore cost
**Goal:** Measure per-frame capture cost and per-seek restore cost for peripherals.

**Metrics:**
- `ttd_peripheral_capture_ns` (per device, per frame)
- `ttd_peripheral_restore_ns` (per device, full state)
- `ttd_peripheral_restore_delta_ns` (per device, delta decode)
- `ttd_peripheral_bytes_per_frame` (full vs delta)

**Location:** `core/benchmarks/debugger/ttd/ttdperipheralbenchmark.cpp`

**Skeleton:**
```cpp
static void BM_TTD_Peripheral_Capture(benchmark::State& state)
{
    auto peripheral = CreateMockPeripheral(state.range(0));  // size param
    std::vector<uint8_t> buffer(peripheral->TTDStateSize());
    
    for (auto _ : state)
    {
        peripheral->TTDSaveState(buffer.data());
        benchmark::DoNotOptimize(buffer.data());
    }
    
    state.SetBytesProcessed(state.iterations() * peripheral->TTDStateSize());
}
BENCHMARK(BM_TTD_Peripheral_Capture)->Arg(256)->Arg(4096)->Arg(32768)->Arg(524288);

static void BM_TTD_Peripheral_Restore_Delta(benchmark::State& state)
{
    // Setup: create previous state and delta blob
    auto peripheral = CreateMockPeripheral(state.range(0));
    auto prevState = CaptureState(peripheral);
    MutateState(peripheral, 0.01);  // 1% change
    auto deltaBlob = registry.CompressWithDelta(current, prev, true);
    
    for (auto _ : state)
    {
        auto decoded = registry.DecompressWithDelta(deltaBlob, prevState.data(), prevState.size());
        peripheral->TTDLoadState(decoded.data());
        benchmark::DoNotOptimize(decoded.data());
    }
}
```

### 3.3 Benchmark: SeekTo worst-case latency
**Goal:** Validate "ANY frame ≤6ms" claim or scope it appropriately.

**Test design:**
```cpp
static void BM_TTD_SeekTo_WorstCase(benchmark::State& state)
{
    // Record 5+ minutes of session
    auto session = RecordSession(frames=15000);  // 5 min @ 50fps
    
    // Force thinning by filling budget
    session.SetBudget(64 * 1024 * 1024);
    session.ThinToFit();
    
    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> dist(0, session.GetCheckpointCount() - 1);
    
    for (auto _ : state)
    {
        size_t target = dist(rng);
        auto start = std::chrono::high_resolution_clock::now();
        session.SeekTo(session.GetCheckpoint(target)->time);
        auto end = std::chrono::high_resolution_clock::now();
        
        state.SetIterationTime(
            std::chrono::duration<double>(end - start).count());
    }
}
BENCHMARK(BM_TTD_SeekTo_WorstCase)->UseManualTime()->Iterations(1000);
```

**Expected output:**
- Mean seek time
- p50/p95/p99/max latency
- Whether thinned regions violate 6ms (expected: yes, up to ~200ms for replay)

**Location:** `core/benchmarks/debugger/ttd/ttdseeekbenchmark.cpp`

### 3.4 Benchmark: 4MB machine (ZX Evo / ATM3)
**Goal:** Validate scalability to 256 RAM pages.

**Metrics:**
- Checkpoint capture time with 256 pages
- Coverage index memory (should be constant per-frame, not per-page)
- Seek time with full 4MB working set

**Location:** `core/benchmarks/debugger/ttd/ttd4mbbenchmark.cpp`

### 3.5 POC: GeneralSound 512KB SRAM
**Goal:** Validate delta compression effectiveness for large peripheral.

**Test design:**
```cpp
TEST(TTD_Delta_Compression, GeneralSound512KB)
{
    MockGeneralSound gs(512 * 1024);  // 512KB SRAM
    
    // Capture baseline
    auto full = registry.CaptureAll(nullptr);
    
    // Simulate typical GS usage: streaming audio, <5% change per frame
    gs.SimulateAudioStream(0.05);  // 5% change
    auto delta = registry.CaptureAll(&full);
    
    // Delta should be significantly smaller
    size_t fullSize = GetTotalSize(full);
    size_t deltaSize = GetTotalSize(delta);
    
    EXPECT_LT(deltaSize, fullSize * 0.1);  // >10x compression for 5% change
}
```

---

## Phase 4: Test Coverage (P1)

### 4.1 Delta round-trip test
**Current gap:** `DeltaEncodingSmallChange` checks sizes only, not correctness.

**Required test:**
```cpp
TEST(TTDPeripheralRegistry, DeltaRoundTrip)
{
    // Create device, capture state A
    auto stateA = device.SaveState();
    auto blobA = registry.Compress(stateA);
    
    // Modify, capture delta B
    device.Modify();
    auto stateB = device.SaveState();
    auto blobB = registry.CompressWithDelta(stateB, stateA, true);
    
    // Restore B from scratch (simulate SeekTo)
    auto decodedA = registry.Decompress(blobA);
    auto decodedB = registry.DecompressWithDelta(blobB, decodedA.data(), decodedA.size());
    
    EXPECT_EQ(stateB, decodedB);  // Bit-exact
    
    // Restore and verify device state
    device.LoadState(decodedB.data());
    EXPECT_EQ(device.ReadRegister(0), expectedValue);
}
```

### 4.2 Move timing asserts to benchmarks
**Current:** `ttdperipheralregistry_test.cpp:253-301` has wall-clock EXPECT_LT.
**Target:** Move to Google Benchmark, report counters, remove flaky gtest timing.

### 4.3 Presence-set serialization test
```cpp
TEST(TTD_Serialization, PresenceSet)
{
    // Session with subset of peripherals
    Session session;
    session.ConnectPeripheral(PeripheralId::TurboSound);
    // NOT: BetaDisk, Tape, Covox
    
    auto serialized = session.Serialize();
    auto restored = Session::Deserialize(serialized);
    
    EXPECT_TRUE(restored.HasPeripheral(PeripheralId::TurboSound));
    EXPECT_FALSE(restored.HasPeripheral(PeripheralId::BetaDisk));
    EXPECT_EQ(restored.PeripheralBlobOverhead(), sizeof(presence_bitmask));
}
```

---

## Phase 5: Documentation (P2)

### 5.1 Label "measured" vs "target"
**Current issue:** TDD §6.4.1/§14.3 present "Benchmark results" and "Actual (measured)" for numbers that have no backing benchmark.

**Action:** 
- Add "(target)" suffix to unverified numbers
- Add "(measured)" with benchmark name/date when verified

### 5.2 Document registry integration status
**Action:** Add note to TDD §6.4 clarifying:
- Registry is implemented and tested
- Not yet wired end-to-end (legacy 4-slot still active)
- Phase 2 tracks integration work

### 5.3 Update restore latency table
**Current:** Claims 6ms for "any random frame"
**Reality:** Thinned regions require replay, can be ~200ms

**Action:** Scope the 6ms claim:
- Dense regions (within I-frame interval): ≤6ms
- Thinned regions: up to kKeyFrameInterval × frame_replay_time

---

## Priority Order

| Phase | Priority | Blocking? | Effort | Status |
|-------|----------|-----------|--------|--------|
| 1.1-1.6 | P0 | Yes (bugs) | — | **DONE** |
| 2.1-2.4 | P0 | No | 1d | TODO |
| 3.1-3.5 | P1 | No | 2d | TODO |
| 4.1-4.3 | P1 | No | 0.5d | TODO |
| 5.1-5.3 | P2 | No | 2h | TODO |

---

## Open Questions

1. **Delta I-frame interval:** How often should large peripherals (GS) emit full snapshots?
   - Every 50 frames (~1 sec)?
   - On seek miss (lazy)?

2. **Presence-set format:** Bitmask (compact, 8 devices max) or varint list (extensible)?

3. **Backward compat:** Can we support reading legacy 4-slot format while writing new presence-set format?

4. **Large ROM configurations:** Scorpion ProfROM (256KB), ATM (512KB), ZX Evo (512KB) - ensure ROM page tracking handles these correctly:
   - 256KB ROM = 16 pages (4-bit page index within slice)
   - 512KB ROM = 32 pages
   - ROM paging via port latches already captured in TTDChipsetState
   - ROM data pages use same COW store as RAM

---

## Large Configuration Targets

| Config | RAM | ROM | Total Pages | Notes |
|--------|-----|-----|-------------|-------|
| ZX-48K | 48KB | 16KB | 4 | Baseline |
| ZX-128K | 128KB | 32KB | 10 | Reference |
| Pentagon 512K | 512KB | 64KB | 36 | Common |
| Scorpion + ProfROM | 256KB | **256KB** | 32 | Extended ROM |
| Profi 1024K | 1MB | 64KB | 68 | Extended RAM |
| ATM3 / ZX Evo | **4MB** | 512KB | **288** | Maximum config |

All use same page store addressing (22-bit keys support up to 256 pages × 16KB).

---

## All Known Tasks

### Phase 1: Bug Fixes (DONE)
- [x] Fix use-after-free in `RestoreAll` — move `prevState` to function scope
- [x] Fix silent corruption — delta-without-prev now returns empty (hard fail)
- [x] Fix uint16_t cap — changed `uncompressedSize` to uint32_t for GS 512KB SRAM
- [x] Fix LZ4 → zstd reference in `ttdserializable.h`
- [x] Fix "checkpoints never persist" doc (SerializeSession exists)
- [x] Add Profi 1024K and ATM3 4MB to scalability goals in TDD doc
- [x] Add large ROM configurations (Scorpion ProfROM 256KB, ATM 512KB)
- [x] Add `videoMode` field to Kaitai schema (`ttd.ksy`)
- [x] Add `video_mode` field to Python parser (`ttd_format.py`)
- [x] Verify Dizzy Y divergence test passes

### Phase 2: Registry Wiring
- [ ] Wire `TTDPeripheralRegistry` into `TimeTravelManager::CaptureNow()`
- [ ] Wire registry into `TimeTravelManager::RestoreCheckpoint()`
- [ ] Wire registry into `TimeTravelManager::SerializeSession()`
- [ ] Wire registry into `TimeTravelManager::DeserializeSession()`
- [ ] Wire registry into `TimeTravelManager::EstimateSessionHeapBytes()`
- [ ] Add `Register()` call in TurboSound constructor
- [ ] Add `Register()` call in WD1793/BetaDisk constructor
- [ ] Add `Register()` call in Tape constructor
- [ ] Add `Register()` call in Covox constructor
- [ ] Add `Unregister()` in each peripheral destructor
- [ ] Handle model reset path (P1.6 unregister on device teardown)
- [ ] Implement presence-set serialization format (session device table + per-checkpoint bitmask)
- [ ] Design and implement I-frame anchors for delta-encoded large peripherals
- [ ] Remove legacy 4-slot blob vectors (`turboSoundBlob`, `betaDiskBlob`, `tapeBlob`, `covoxBlob`)

### Phase 3: POCs and Benchmarks
- [ ] **POC: Zero-bytes for non-connected peripherals**
  - [ ] Create `core/tests/debugger/ttd/ttdstreamoverhead_test.cpp`
  - [ ] Test: serialize 48K session (no peripherals) vs 128K+BDI+tape+covox
  - [ ] Assert per-checkpoint overhead delta == 0 for non-connected devices
  
- [ ] **Benchmark: Peripheral capture/restore cost**
  - [ ] Create `core/benchmarks/debugger/ttd/ttdperipheralbenchmark.cpp`
  - [ ] Measure `ttd_peripheral_capture_ns` per device per frame
  - [ ] Measure `ttd_peripheral_restore_ns` full state path
  - [ ] Measure `ttd_peripheral_restore_delta_ns` delta decode path
  - [ ] Measure `ttd_peripheral_bytes_per_frame` full vs delta
  - [ ] Test sizes: 256B, 4KB, 32KB, 512KB

- [ ] **Benchmark: SeekTo worst-case latency**
  - [ ] Create `core/benchmarks/debugger/ttd/ttdseekbenchmark.cpp`
  - [ ] Record 5+ minute session (15000 frames)
  - [ ] Force thinning by filling 64MB budget
  - [ ] Measure random SeekTo across full timeline
  - [ ] Report mean/p50/p95/p99/max latency
  - [ ] Document whether thinned regions exceed 6ms budget

- [ ] **Benchmark: 4MB machine (ZX Evo / ATM3)**
  - [ ] Create `core/benchmarks/debugger/ttd/ttd4mbbenchmark.cpp`
  - [ ] Measure checkpoint capture time with 256 RAM pages
  - [ ] Measure coverage index memory (should be constant per-frame)
  - [ ] Measure seek time with full 4MB working set
  - [ ] Compare against 128K baseline

- [ ] **POC: GeneralSound 512KB SRAM delta compression**
  - [ ] Create mock GeneralSound peripheral with 512KB state
  - [ ] Measure full state compression ratio
  - [ ] Simulate 5% change per frame (audio streaming)
  - [ ] Verify delta achieves >10x compression vs full
  - [ ] Test round-trip correctness

### Phase 4: Test Coverage
- [ ] **Delta round-trip test**
  - [ ] Add `TTDPeripheralRegistry.DeltaRoundTrip` test
  - [ ] Capture state A, compress
  - [ ] Modify device, capture state B with delta
  - [ ] Decompress A, then decompress B using A as prev
  - [ ] Verify bit-exact match with original state B

- [ ] **Move timing asserts to benchmarks**
  - [ ] Remove wall-clock `EXPECT_LT` from `ttdperipheralregistry_test.cpp:253-301`
  - [ ] Move to Google Benchmark with proper counters
  - [ ] Eliminate CI flakiness from timing assertions in gtests

- [ ] **Presence-set serialization test**
  - [ ] Test session with subset of peripherals
  - [ ] Verify only connected devices in serialized output
  - [ ] Verify presence bitmask overhead is minimal

- [ ] **Large peripheral round-trip test**
  - [ ] Test with 512KB mock peripheral
  - [ ] Full capture → serialize → deserialize → restore
  - [ ] Verify bit-exact state

### Phase 5: Documentation
- [ ] Label "measured" vs "target" in TDD benchmark tables
- [ ] Add "(target)" suffix to unverified numbers
- [ ] Add "(measured)" with benchmark name/date when verified
- [ ] Document registry integration status in TDD §6.4
- [ ] Clarify registry is implemented but not yet wired
- [ ] Update restore latency table to scope 6ms claim
- [ ] Document dense regions ≤6ms vs thinned regions up to ~200ms
- [ ] Add sequence diagram for peripheral capture/restore flow
- [ ] Document I-frame interval strategy for large peripherals

### Future / Nice-to-Have
- [ ] Implement TSFM peripheral TTDSerializable interface
- [ ] Implement GeneralSound peripheral TTDSerializable interface
- [ ] Implement SAA1099 peripheral TTDSerializable interface
- [ ] Consider per-frame heap churn optimization (reuse buffers)
- [ ] Explore chain-walking for random-access delta decode (like RAM page store)
- [ ] Consider lazy I-frame generation on seek miss
- [ ] Profile and optimize coverage index memory for 4MB machines
- [ ] Add TTD session size estimation API for UI budget display
