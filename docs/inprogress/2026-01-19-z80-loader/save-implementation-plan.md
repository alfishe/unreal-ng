# Z80 Snapshot Save Implementation

Implement [.z80](core/tests/build-standalone/bin/testdata/loaders/z80/newbench.z80) snapshot saving with TDD approach, reusing existing automation integration (CLI, WebAPI, Python, Lua) and Qt UI wiring.

## User Review Required

> [!IMPORTANT]
> **Format Version Selection**: Recommend saving as Z80 v3 format for maximum compatibility (supports 48K, 128K, +3, Pentagon, etc.) and includes T-state counter for cycle-accurate restoration. Please confirm.

> [!NOTE]
> **Compression**: Z80 format uses simple RLE compression. Saving compressed is recommended (smaller files, compatible with all loaders). The `compressPage()` function is already declared but needs implementation.

## Proposed Changes

### Core Loader [loader_z80.cpp](core/src/loaders/snapshot/loader_z80.cpp) / [loader_z80.h](core/src/loaders/snapshot/loader_z80.h)

#### [MODIFY] [loader_z80.h](core/src/loaders/snapshot/loader_z80.h)

Add save-related method declarations:
- `bool save()` - Main public save method
- `bool captureStateToStaging()` - Capture current emulator state to staging buffers
- `bool saveV3FromStaging()` - Write v3 format (recommended for maximum compatibility)
- `Z80MemoryMode determineOutputFormat()` - Decide 48K vs 128K based on current mode
- `size_t compressPageToBuffer(uint8_t* src, uint8_t* dst)` - Compress 16KB page, return compressed size
- `uint8_t getModelCodeV3()` - Map current emulator model to Z80 v3 model byte
- Expose save methods in [LoaderZ80CUT](core/src/loaders/snapshot/loader_z80.h#258-275) for testing

#### [MODIFY] [loader_z80.cpp](core/src/loaders/snapshot/loader_z80.cpp)

Implement save functionality:

1. **`determineOutputFormat()`**: Check port 7FFD lock bit and current model. If locked Spectrum 48K → Z80_48K, else → Z80_128K/Z80_SAMCOUPE/etc.

2. **`captureStateToStaging()`**: Read Z80 registers, memory pages, port values, AY registers, T-state counter, border color from emulator into staging buffers.

3. **`compressPage()`** (already declared line 218): Implement RLE compression:
   - Replace sequences of ≥5 identical bytes with `ED ED count byte`
   - Special handling for `ED` bytes: even 2 EDs → `ED ED 02 ED`
   - Return compressed size

4. **`saveV3FromStaging()`**:
   - Build 87-byte v3 header from captured state
   - Set `reg_PC = 0` in v1 header, actual PC in v2 extension
   - Set `extendedHeaderLen = 54` (v3)
   - Set model code based on current emulator model
   - Write header to file
   - For each memory page (8 pages for 128K, 3 for 48K):
     - Compress page with `compressPage()`
     - Write 3-byte block descriptor (compressed size + page number)
     - Write compressed data

5. **Thread-safety**: Pause emulator during capture, resume after

---

### Automation Layer (Already Wired)

The existing automation infrastructure already supports `.z80` format through file extension detection. The CLI `snapshot save`, WebAPI `POST /snapshot/save`, Python `save_snapshot()`, and Lua `save_snapshot()` all route through `Emulator::SaveSnapshot()` which detects format by extension.

#### [VERIFY] Already working in [cli-processor-snapshot.cpp](core/automation/cli/src/commands/cli-processor-snapshot.cpp)

Existing code validates `.sna` and `.z80` extensions:
```cpp
// Format from extension
if (ext == ".sna" || ext == ".z80") { ... }
```

No changes needed to CLI, WebAPI, Python, or Lua bindings - they already support `.z80`.

---

### Emulator Class

#### [MODIFY] [emulator.cpp](core/src/emulator/emulator.cpp)

Update `SaveSnapshot()` to handle `.z80` format:
- Detect format from extension (`.z80` → LoaderZ80)
- Create loader, call `save()`
- Currently only `.sna` is implemented - add `.z80` branch

---

### Qt UI (Already Wired)

#### [VERIFY] Already working in [menumanager.h](unreal-qt/src/menumanager.h) / [menumanager.cpp](unreal-qt/src/menumanager.cpp)

The Save Snapshot submenu already has `.sna` and `.z80` options. Currently `.z80` is disabled (placeholder). Need to enable it once saver is complete.

#### [MODIFY] Enable `.z80` option in menu

Change disabled state to enabled and connect to save handler.

---

## Verification Plan

### Automated Tests

**Run core-tests for Z80 loader:**
```bash
cd /Volumes/TB4-4Tb/Projects/Test/unreal-ng
cmake --build build-standalone --target core-tests
./build-standalone/bin/core-tests --gtest_filter="LoaderZ80*"
```

**New test cases to add in** [loader_z80_test.cpp](core/tests/loaders/loader_z80_test.cpp):

| Test Name | Description |
|-----------|-------------|
| `compressPageBasic` | Verify RLE compression of page with repeated bytes |
| `compressPageEDHandling` | Verify special ED sequence handling |
| `compressDecompressRoundtrip` | Compress then decompress, verify identical |
| `save48kBasic` | Save 48K state, verify file structure |
| `save128kBasic` | Save 128K state, verify file structure |
| `saveAndLoadRoundtrip48k` | Save → Load → Compare all registers |
| `saveAndLoadRoundtrip128k` | Save → Load → Compare registers + all 8 pages |
| `determineOutputFormat_Locked` | Verify locked port → 48K format |
| `determineOutputFormat_Unlocked` | Verify unlocked 128K → 128K format |
| `saveCreatesValidV3Header` | Verify v3 header fields match emulator state |
| `savePreservesAYRegisters` | Verify AY/YM PSG registers saved correctly |
| `savePreservesTStateCounter` | Verify T-state counter in v3 header |

### WebAPI Verification

**Run Python WebAPI tests (already support .z80 extension):**
```bash
cd /Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi
python3 -m pytest src/ -k snapshot -v
```

If needed, add new test to `src/test_snapshot_api.py`:
- `test_save_snapshot_z80()` - POST save with .z80 extension, verify file created
- `test_save_snapshot_z80_roundtrip()` - Save, load, compare state

### Manual Verification

1. **CLI Test:**
   ```bash
   # Connect to CLI
   nc localhost 10000
   > create
   > resume
   > snapshot save /tmp/test.z80
   > snapshot info
   ```
   Expected: "Snapshot saved: /tmp/test.z80" and file exists

2. **Qt UI Test:**
   - Open unreal-qt
   - Load any program or SNA
   - File → Save Snapshot → .z80
   - Verify file dialog opens with .z80 filter
   - Enter filename, click Save
   - Verify success message
   - Load saved file → verify program runs identically

3. **Cross-Loader Verification:**
   - Load `BBG128.z80` (existing 128K test file)
   - Save as new `.z80`
   - Compare CRC32 of original vs saved (may differ due to compression)
   - Load saved file → verify identical screen and behavior

4. **External Emulator Verification:**
   - Save `.z80` from unreal-ng
   - Load in Fuse or another emulator
   - Verify it loads correctly (confirms format compatibility)
