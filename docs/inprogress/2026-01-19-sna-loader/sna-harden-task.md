# SNA Loader Hardening - Implementation Tasks

## Phase 1: Test Infrastructure
- [x] Generate invalid test files (14 files)
- [x] Apply unlock pattern to 128K loading
- [ ] Create LoaderSNACUT test helper class
- [ ] Set up test fixtures and helper methods

## Phase 2: Defensive Programming
- [ ] Add minimum file size validation (27 bytes)
- [ ] Add bounds checking before page reads
- [ ] Add header field sanitization (border, IM mode)
- [ ] Add file size validation (48K vs 128K)
- [ ] Error path cleanup verification

## Phase 3: Invalid File Tests
- [ ] Empty file rejection
- [ ] Truncated header rejection
- [ ] Header-only file rejection
- [ ] Truncated 48K snapshot
- [ ] Truncated 128K snapshot
- [ ] Wrong format files (PNG, JPEG, text, etc.)
- [ ] Edge cases (zeros, 0xFF, patterns)

## Phase 4: Lock/State Tests  
- [ ] Load 128K after locked port
- [ ] Load 48K after 128K
- [ ] Repeated loads with lock variations

## Phase 5: Functional Verification
- [ ] Verify existing tests still pass
- [ ] Test PC restoration (48K from stack)
- [ ] Test border/IM/IFF restoration
- [ ] Test TR-DOS ROM activation

## Phase 6: Build & Verification
- [ ] All tests passing
- [ ] Manual testing in unreal-qt
- [ ] Documentation update

**Current:** Creating test infrastructure
