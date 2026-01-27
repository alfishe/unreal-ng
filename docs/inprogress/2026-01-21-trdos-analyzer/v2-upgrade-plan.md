# TR-DOS Analyzer v2 - Upgrade Plan

> **Updated**: 2026-01-26  
> **Status**: Planning  
> **Related**: Forensic research in `/docs/beta-disk-interface/TRDOS_Format_Procedure.md`

---

## Current State (v1 - Completed)

### ✅ Implemented Features
- [x] `IWD1793Observer` interface for FDC callbacks
- [x] `RingBuffer<T>` for bounded event storage
- [x] Breakpoints at TR-DOS entry points ($3D00, $3D13, $3D1A, $030A)
- [x] Service code detection (C register at $3D13)
- [x] User command detection (token at CH_ADD) 
- [x] Filename extraction from $5CDD
- [x] FDC command → semantic event mapping
- [x] Interrupt state capture (iff1, im)
- [x] Raw FDC events with CMD_START/CMD_END lifecycle

### ⚠️ Known Limitations
1. **No $3D2F trampoline detection** - Custom loaders using internal ROM routines undetected
2. **Incomplete loader classification** - Can't distinguish API loaders from direct ROM loaders
3. **No density/track info in raw log** - System register (port 0xFF) not captured
4. **No RST #10 pattern detection** - Inline port access pattern invisible

---

## v2 Upgrade Goals

### Goal 1: Complete Loader Classification

Distinguish between these loader types:

| Type | Detection Method | Characteristics |
|------|------------------|-----------------|
| **Interactive** | BP at $030A (internal dispatcher) | User at A> prompt |
| **BASIC Command** | BP at $3D1A | `RUN "boot"` from BASIC |
| **API Loader** | BP at $3D13, C=service code | Uses documented $3D13 API |
| **ROM-Internal** | BP at $3D2F, IX=routine address | Bypasses API, uses internal routines |
| **Direct FDC** | FDC command with PC in RAM, no ROM in stack | No ROM calls at all |

### Goal 2: Complete Raw Event Capture

Each FDC command should log:

```
-> RESTORE ($08) T=00 S=01 Side=0 Density=MFM [PC=$3E44 ROM]
<-- RESTORE Status=$00 T=00 S=01

-> READ_SECTOR ($80) T=00 S=09 Side=0 [PC=$3FE5 ROM]
<-- READ_SECTOR Status=$00 T=00 S=09 (256 bytes)

-> WRITE_TRACK ($F0) T=01 Side=0 [PC=$8120 RAM - CUSTOM]
<-- WRITE_TRACK Status=$00 T=01
```

### Goal 3: System State Tracking

Track TR-DOS system variables in real-time:
- Track count ($5CD7): 40 or 80
- Side flag ($5CDA): $00=SS, $80=DS  
- Default drive ($5D19)
- Current filename ($5CDD)

---

## Implementation Plan

### Phase 1: $3D2F Trampoline Detection (Priority: HIGH)

**Rationale**: The $3D2F trampoline is used by ALL custom loaders that need to call internal ROM routines. Detecting it provides visibility into non-API disk access.

**Pattern detected**:
```asm
TODOS:  push ix          ; IX = internal ROM routine address
        jp #3D2F         ; ROM switch trampoline
```

**Tasks**:
- [ ] Add breakpoint at $3D2F in `onActivate()`
- [ ] Capture IX register value (target routine address)
- [ ] Add `TRDOSEventType::ROM_INTERNAL_CALL` event type
- [ ] Map common IX values to routine names:
  - `$3FE5` → RD_DATA (read sector data)
  - `$3FCA` → WR_DATA (write sector data)
  - `$3F33` → FROM1F (read status)
  - `$3EF5` → WAITFF (wait for FDC)
  - `$2A53` → Port OUT

**Header changes** (`trdosevent.h`):
```cpp
enum class TRDOSEventType : uint8_t
{
    // ... existing ...
    ROM_INTERNAL_CALL,  // $3D2F trampoline with IX = routine address
};
```

**New field in `TRDOSEvent`**:
```cpp
uint16_t targetRoutine;  // IX value when hitting $3D2F
```

---

### Phase 2: System Register Capture (Priority: HIGH)

**Rationale**: Port 0xFF (Beta 128 system register) contains density, side selection, and drive info. Essential for analyzing FORMAT and disk geometry.

**Tasks**:
- [ ] Add `systemReg` capture to `RawFDCEvent` (already present, not populated)
- [ ] In WD1793, expose last system register value via `getSystemRegister()`
- [ ] Or capture via `onFDCPortAccess()` when port=0xFF

**Bit definitions** (already in `spectrumconstants.h`):
```cpp
namespace SystemRegister
{
    constexpr uint8_t DRIVE_MASK = 0x03;    // Bits 0-1: drive select
    constexpr uint8_t SIDE_SELECT = 0x10;   // Bit 4: 0=side 1, 1=side 0
    constexpr uint8_t DENSITY_MFM = 0x20;   // Bit 5: 0=FM, 1=MFM (double)
}
```

---

### Phase 3: Loader Type Classification Enum (Priority: MEDIUM)

**Tasks**:
- [ ] Add `LoaderType` enum to `TRDOSEvent`:
  ```cpp
  enum class LoaderType : uint8_t
  {
      INTERACTIVE,     // User at A> prompt
      BASIC_COMMAND,   // RUN/LOAD from BASIC
      API_LOADER,      // Uses $3D13 API correctly
      ROM_INTERNAL,    // Uses $3D2F trampoline
      DIRECT_FDC,      // No ROM calls, direct port access
      UNKNOWN,
  };
  ```
- [ ] Set loader type based on how command was detected
- [ ] Update event formatting to show loader type

---

### Phase 4: Stack-Based ROM Detection (Priority: MEDIUM)

**Rationale**: To detect "Direct FDC" loaders (no ROM calls), analyze the stack when FDC command is issued. If stack contains only RAM addresses, it's direct access.

**Tasks**:
- [ ] In `captureRawFDCEvent()`, analyze stack snapshot
- [ ] Add helper `bool stackContainsROMAddress(const std::array<uint8_t, 16>& stack)`
- [ ] If PC in RAM and no ROM in stack → set `LoaderType::DIRECT_FDC`
- [ ] If PC in RAM but ROM in stack → set `LoaderType::ROM_INTERNAL`

---

### Phase 5: Enhanced Raw Event Formatting (Priority: LOW)

**Tasks**:
- [ ] Add `format()` method to `RawFDCEvent`
- [ ] Include human-readable command names, register values, loader type
- [ ] Make output parseable for offline analysis tools

**Example output**:
```
[T:12345678] -> READ_SECTOR ($80)
    Track=00 Sector=09 Side=0 Density=MFM Drive=A
    PC=$3FE5 (ROM:RD_DATA) SP=$5F00
    Loader: API_LOADER
[T:12346000] <-- READ_SECTOR Status=$00 (OK)
    Track=00 Sector=09
```

---

## Updated File Changes

### `trdosevent.h`
```cpp
// Add loader type enum
enum class LoaderType : uint8_t { ... };

// Add to RawFDCEvent:
LoaderType loaderType;

// Add to TRDOSEvent:
LoaderType loaderType;
uint16_t targetRoutine;  // For ROM_INTERNAL_CALL events
```

### `trdosanalyzer.h`
```cpp
// New breakpoint
static constexpr uint16_t BP_ROM_TRAMPOLINE = 0x3D2F;

// New handler
void handleROMTrampoline(Z80* cpu);

// New helper
bool stackContainsROMAddress(const std::array<uint8_t, 16>& stack);
LoaderType classifyLoader(uint16_t pc, const std::array<uint8_t, 16>& stack);
```

### `trdosanalyzer.cpp`
- Add $3D2F breakpoint registration
- Implement `handleROMTrampoline()`
- Implement `classifyLoader()`
- Update `captureRawFDCEvent()` to set loader type

---

## Testing Plan

### Unit Tests
- [ ] Test loader classification logic with mock PC/stack values
- [ ] Test $3D2F detection with various IX values
- [ ] Test system register parsing

### Integration Tests (using real ROMs)
- [ ] Run `CAT` - should show `INTERACTIVE` or `BASIC_COMMAND`
- [ ] Run game with standard loader - should show `API_LOADER`
- [ ] Run game with custom loader (e.g., protection) - should show `ROM_INTERNAL`
- [ ] Run FORMAT - should show complete FDC command trace with density info

### Manual Verification
- [ ] Compare analyzer output with WD1793Collector raw dump
- [ ] Verify stack traces match expected call chains

---

## Dependencies

| Dependency | Status |
|------------|--------|
| `spectrumconstants.h` updates | ✅ Done (added SystemRegister, ROMVariables) |
| `RawFDCEventType` enum | ✅ Done (CMD_START/CMD_END) |
| Fixed disk type values | ✅ Done (0x16=80T/DS, 0x17=40T/DS, etc.) |
| WD1793 system register access | ⚠️ Need to add getter |

---

## Priority Order

1. **Phase 1**: $3D2F trampoline - ✅ DONE (unlocks custom loader visibility)
2. **Phase 2**: System register - needed for FORMAT debugging
3. **Phase 3**: Loader type enum - improves log readability
4. **Phase 4**: Stack analysis - completes classification
5. **Phase 5**: Formatting - polish
6. **Phase 6**: Semantic Layer Aggregation - high-level operation summaries

---

## Phase 6: Semantic Layer Aggregation (Priority: HIGH)

**Rationale**: Current semantic events are low-level (individual SEEK, READ_SECTOR, etc.). Users need high-level operation summaries that aggregate raw events into meaningful disk operations.

**Goal**: Transform raw events into human-readable operation summaries:

| Instead of... | Generate... |
|---------------|-------------|
| 50 low-level events | `RUN "boot" loaded 12 sectors from T5-T7 in 350 frames` |
| Hundreds of FDC commands | `FORMAT completed: 160 tracks, 0 errors, 3500 frames` |
| Scattered trampoline calls | `Custom loader used SECTOR_LOOP 245 times` |

**Implementation Approach**:

1. **Operation State Machine**
   - Track current operation state: IDLE, LOADING, SAVING, FORMATTING, CATALOGING
   - Accumulate raw events during operation
   - Emit summary event when operation completes (e.g., on TRDOS_EXIT)

2. **New Semantic Event Types**:
   ```cpp
   enum class SemanticEventType : uint8_t
   {
       OPERATION_LOAD,     // File load completed
       OPERATION_SAVE,     // File save completed  
       OPERATION_FORMAT,   // Format completed
       OPERATION_CATALOG,  // Directory listing completed
       OPERATION_ERROR,    // Operation failed with error
   };
   ```

3. **Operation Summary Structure**:
   ```cpp
   struct OperationSummary
   {
       SemanticEventType type;
       std::string command;           // "RUN \"boot\"" or "FORMAT \"test\""
       std::string filename;
       uint32_t sectorsTransferred;
       uint32_t tracksAccessed;
       uint32_t errorsEncountered;
       uint64_t startFrame;
       uint64_t endFrame;
       uint64_t durationFrames;
       LoaderType loaderType;         // How was disk accessed?
   };
   ```

4. **Tasks**:
   - [ ] Create `OperationTracker` class to accumulate raw events
   - [ ] Define operation start/end detection logic
   - [ ] Implement summary generation for LOAD, SAVE, FORMAT, CAT
   - [ ] Add `getOperationSummaries()` API
   - [ ] Update WebAPI with `/operations` endpoint

---

## References

- [TRDOS_Format_Procedure.md](../beta-disk-interface/TRDOS_Format_Procedure.md) - FORMAT command forensics
- [spectrumconstants.h](../../core/src/emulator/spectrumconstants.h) - TR-DOS system variables
- [trdos-rom-forensics.md](./trdos-rom-forensics.md) - ROM analysis findings
- Custom loader example: ZX-FORMAT 8 (XL-DESIGN) - uses $3D2F trampoline extensively
