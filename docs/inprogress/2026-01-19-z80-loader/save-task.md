# Z80 Snapshot Save Implementation

## Phase 1: Core Save Implementation (TDD)

### Compression
- [ ] Write unit tests for `compressPage()` (basic RLE)
- [ ] Write unit tests for `compressPage()` (ED byte handling)  
- [ ] Write compress/decompress roundtrip test
- [ ] Implement `compressPage()` in [loader_z80.cpp](core/src/loaders/snapshot/loader_z80.cpp)

### State Capture
- [ ] Write unit tests for `captureStateToStaging()`
- [ ] Implement `captureStateToStaging()` to capture Z80 registers, ports, memory

### Format Determination
- [ ] Write unit tests for `determineOutputFormat()` (48K mode detection)
- [ ] Write unit tests for `determineOutputFormat()` (128K mode detection)
- [ ] Implement `determineOutputFormat()` based on port 7FFD lock bit

### V3 Save
- [ ] Write unit tests for `saveV3FromStaging()` (header structure)
- [ ] Write unit tests for `saveV3FromStaging()` (memory pages)
- [ ] Implement `saveV3FromStaging()` with proper v3 header layout

### Main Save Method
- [ ] Write unit tests for `save()` (48K mode)
- [ ] Write unit tests for `save()` (128K mode)
- [ ] Implement `save()` public method
- [ ] Add thread-safety with emulator pause/resume during save
- [ ] Verify all save tests pass

## Phase 2: Round-Trip Verification

- [ ] Write `saveAndLoadRoundtrip48k` test
- [ ] Write `saveAndLoadRoundtrip128k` test with all 8 RAM pages
- [ ] Write `savePreservesAYRegisters` test
- [ ] Write `savePreservesTStateCounter` test

## Phase 3: Emulator Integration

- [ ] Add `.z80` save branch in [emulator.cpp](core/src/emulator/emulator.cpp) `SaveSnapshot()`
- [ ] Test integration with LoaderZ80

## Phase 4: Verify Existing Automation (No New Code Expected)

- [ ] Verify CLI `snapshot save /path/file.z80` works
- [ ] Verify WebAPI `POST /snapshot/save` with .z80 path works
- [ ] Verify Python `save_snapshot("file.z80")` works
- [ ] Verify Lua `save_snapshot("file.z80")` works

## Phase 5: Qt UI Enable

- [ ] Enable ".z80" option in Save Snapshot submenu [menumanager.cpp](unreal-qt/src/menumanager.cpp)
- [ ] Test Qt save dialog for .z80 format

## Phase 6: Verification & Documentation

- [ ] Run full test suite (`./bin/core-tests --gtest_filter="LoaderZ80*"`)
- [ ] Manual verification in unreal-qt (load → save → reload)
- [ ] Cross-loader verification (load in Fuse or other emulator)
- [ ] Update walkthrough documentation
