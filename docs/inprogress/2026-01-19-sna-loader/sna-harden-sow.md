# SNA Loader Hardening - Scope of Work

**Created:** 2026-01-19  
**Status:** 🔄 In Planning  
**Based on:** Z80 Loader Hardening (Complete)

## Overview

Apply the same defensive programming and state-independent loading patterns from the Z80 loader hardening project to the SNA snapshot loader. This will ensure consistent, robust loading of SNA snapshots regardless of emulator's previous state.

## Immediate Changes Applied

### 1. State-Independent 128K Loading ✅

**Status:** Applied (build verification needed)  
**File:** `loader_sna.cpp` lines 427-449

Added unlock pattern to SNA 128K loading:
```cpp
// Step 1: Unlock paging
_context->pPortDecoder->UnlockPaging();

// Step 2: Configure banks
memory.SetRAMPageToBank1(5);
memory.SetRAMPageToBank2(2);
memory.SetRAMPageToBank3(currentTopPage);

// Step 3: Set port via decoder
_context->pPortDecoder->DecodePortOut(0x7FFD, _ext128Header.port_7FFD, z80.pc);

// Step 4: Explicit state assignment (including lock bit)
_context->emulatorState.p7FFD = _ext128Header.port_7FFD;
```

**Benefit:** 128K SNA snapshots will now load correctly even if port 7FFD was previously locked.

## Proposed Hardening Tasks

### Phase 1: Defensive Programming

| Task | Priority | Effort | Description |
|------|----------|--------|-------------|
| **Bounds checking** | HIGH | Small | Validate file size before reading pages |
| **Header validation** | HIGH | Small | Check header fields for sanity |
| **Page count validation** | MEDIUM | Small | Verify expected vs actual page count |
| **Border color sanitization** | LOW | Trivial | Clamp border to 0-7 |
| **IM mode sanitization** | MEDIUM | Trivial | Clamp IM to 0-2 |
| **Error cleanup** | MEDIUM | Small | Ensure file closed on all error paths |

### Phase 2: Test Coverage

| Test Category | Count | Files Needed |
|---------------|-------|--------------|
| **Functional** | 8 | Existing + new |
| **Invalid files** | 6 | Synthetic |
| **Lock/State** | 3 | Existing |
| **Fuzzing** | 5 | Synthetic |
| **Total** | ~22 | Mix |

#### Functional Tests (8)
- ✅ Load valid 48K snapshot (existing test)
- ✅ Load valid 128K snapshot (existing test)
- ⬜ Reject empty file
- ⬜ Reject truncated header
- ⬜ Reject file with wrong size
- ⬜ Handle partial RAM pages
- ⬜ Verify PC restoration (48K from stack)
- ⬜ Verify border/IM/IFF restoration

#### Invalid File Tests (6)
- ⬜ File < 27 bytes (header size)
- ⬜ File with header but no RAM
- ⬜ 128K with truncated extended header
- ⬜ Wrong file format (PNG, JPEG, text)
- ⬜ All zeros file
- ⬜ All 0xFF file

#### Lock/State Tests (3)
- ⬜ Load 128K after locked port
- ⬜ Load 48K after 128K
- ⬜ Repeated loads with different lock states

#### Fuzzing Tests (5)
- ⬜ Random data small/medium/large
- ⬜ Corrupted valid snapshots
- ⬜ Malformed headers
- ⬜ Extreme values in header fields
- ⬜ Boundary conditions (page counts)

### Phase 3: Documentation

- ⬜ Create SNA format reference (similar to Z80 validation doc)
- ⬜ Document 48K vs 128K differences
- ⬜ Note on PC restoration quirk (48K stores on stack)
- ⬜ Create test data catalogue
- ⬜ Final walkthrough document

## SNA Format Notes

### 48K SNA
- **Size:** 49,179 bytes (27 byte header + 49,152 bytes RAM)
- **Quirk:** PC not in header; stored on stack (must pop after load)
- **RAM Layout:** Banks 5, 2, 0 (fixed)

### 128K SNA
- **Size:** 131,103 or 147,487 bytes
  - 131,103 = Header + 3 pages + ext header + 5 pages
  - 147,487 = Header + 3 pages + ext header + 8 pages
- **Extended Header:** 4 bytes at offset 49,179
  - PC (16-bit)
  - Port 7FFD (8-bit) - **May include lock bit!**
  - TR-DOS flag (8-bit)
- **RAM Layout:** Banks configurable via port 7FFD

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Breaking existing snapshots | HIGH | Comprehensive testing |
| PC restoration regression (48K) | HIGH | Dedicated test case |
| Extended header parsing bugs | MEDIUM | Bounds checking |
| TR-DOS ROM activation issues | LOW | Manual testing needed |

## Success Criteria

- [ ] All existing SNA tests pass
- [ ] 22+ new tests added and passing
- [ ] Manual verification with 48K and 128K snapshots in unreal-qt
- [ ] No regressions in snapshot loading
- [ ] Consistent loading regardless of emulator state
- [ ] Documentation complete

## Test Data

**Existing Valid Snapshots:**
- 23 SNA files in `testdata/loaders/sna/`
- Mix of 48K (49,179 bytes) and 128K (131,103 bytes)
- Good variety for functional testing

**Invalid Test Files:** ✅ Generated
- 14 files in `testdata/loaders/sna/invalid/`
- Empty, truncated headers, truncated snapshots
- Wrong formats (PNG, JPEG, text, JSON, markdown)
- Edge cases (all zeros, all 0xFF, patterns)

### Invalid Files Catalogue

| File | Size | Purpose |
|------|------|---------|
| `empty.sna` | 0 | Empty file rejection |
| `truncated_header.sna` | 15 | Header too short (< 27 bytes) |
| `header_only.sna` | 27 | Header but no RAM data |
| `truncated_48k.sna` | ~32KB | 48K with only 2 pages instead of 3 |
| `truncated_128k.sna` | ~49KB | 128K missing additional pages |
| `all_zeros.sna` | 1000 | All zero bytes |
| `all_ff.sna` | 1000 | All 0xFF bytes |
| `fake_png.sna` | 108 | PNG magic bytes |
| `fake_jpeg.sna` | 104 | JPEG magic bytes |
| `text_file.sna` | text | Plain text content |
| `markdown.sna` | text | Markdown content |
| `data.sna` | text | JSON content |
| `pattern.sna` | 1000 | Repeating 0xAA55 pattern |
| `pseudo_random.sna` | 1000 | Deterministic pseudo-random data |


## Implementation Order

1. ✅ Apply unlock pattern (DONE - build verification pending)
2. ⬜ Add bounds checking and validation
3. ⬜ Create test helper class (LoaderSNACUT)
4. ⬜ Add invalid file tests
5. ⬜ Add lock/state tests
6. ⬜ Add fuzzing tests
7. ⬜ Manual verification
8. ⬜ Documentation

## References

- Z80 Loader: `docs/inprogress/2026-01-19-z80-loader/COMPLETE.md`
- SNA Format: World of Spectrum archives
- Existing tests: `core/tests/loaders/loader_sna_test.cpp`

## Notes

- SNA format is simpler than Z80 (no compression, no multiple versions)
- Main complexity is 48K PC restoration quirk
- 128K extended header is where state-dependence issues arise
- TR-DOS ROM switching needs careful testing
