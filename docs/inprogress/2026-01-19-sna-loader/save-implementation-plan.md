# SNA Snapshot Save Implementation

Implement [.sna](core/tests/build-standalone/bin/testdata/loaders/sna/s4b-1.sna) snapshot saving with TDD approach, automation integration, and Qt UI.

## User Review Required

> [!IMPORTANT]
> **RAM Randomization Change**: Switching reset randomizer to only randomize RAM pages 5 and 7 (visible in 48K mode) is a significant behavior change affecting all resets. Please confirm this is acceptable.

> [!WARNING]
> **Empty Page Optimization**: Skipping empty pages in 128K saves may affect compatibility with some loaders that expect specific file sizes or page counts. Recommend implementing but allowing a "full save" option if needed.

## Proposed Changes

### Core Loader [loader_sna.cpp](core/src/loaders/snapshot/loader_sna.cpp) / [loader_sna.h](core/src/loaders/snapshot/loader_sna.h)

#### [MODIFY] [loader_sna.h](core/src/loaders/snapshot/loader_sna.h)

Add save-related method declarations:
- `bool save()` - Main public save method
- `bool captureStateToStaging()` - Capture current emulator state to staging
- `bool save48kFromStaging()` - Write 48K format
- `bool save128kFromStaging()` - Write 128K format
- `SNA_MODE determineOutputFormat()` - Decide 48K vs 128K based on current mode
- `bool isPageEmpty(int pageNum)` - Check if RAM page is all zeros
- Expose save methods in [LoaderSNACUT](core/src/loaders/snapshot/loader_sna.h#163-193) for testing

#### [MODIFY] [loader_sna.cpp](core/src/loaders/snapshot/loader_sna.cpp)

Implement save functionality:
- `determineOutputFormat()`: Check port 7FFD lock bit. If locked → 48K, else → 128K
- `captureStateToStaging()`: Read Z80 registers, memory pages, border color from emulator into staging buffers
- `save48kFromStaging()`: Write 27-byte header + 3 RAM pages (5, 2, 0), push PC to stack
- `save128kFromStaging()`: Write header + base pages + extended header + remaining pages (skip empty)
- Thread-safety: Pause emulator during capture, resume after

---

### Emulator Class

#### [MODIFY] [emulator.h](core/src/emulator/emulator.h)

Add method declaration:
```cpp
bool SaveSnapshot(const std::string& path);
```

#### [MODIFY] [emulator.cpp](core/src/emulator/emulator.cpp)

Implement `SaveSnapshot()`:
- Pause emulator
- Detect format from extension ([.sna](core/tests/build-standalone/bin/testdata/loaders/sna/s4b-1.sna) → LoaderSNA)
- Create loader, call `save()`
- Resume emulator
- Return success/failure

---

### CLI Automation

#### [MODIFY] [command-interface.md](docs/emulator/design/control-interfaces/command-interface.md)

Add documentation for:
```
snapshot save <file> [--force]   Save snapshot (format from extension: .sna or .z80)
                                  --force: Overwrite existing file. Without it, rejects if file exists.
```

**Behavior:**
- Format determined by file extension: `.sna` → SNA format, `.z80` → Z80 format
- Unrecognized extension → Error: "Unknown snapshot format"
- File exists without `--force` → Error: "File already exists. Use --force to overwrite."

#### [MODIFY] [cli-processor-snapshot.cpp](core/automation/cli/src/commands/cli-processor-snapshot.cpp)

Add:
- `HandleSnapshotSave()` function following [HandleSnapshotLoad()](core/automation/cli/src/commands/cli-processor-snapshot.cpp#52-78) pattern
- Parse `--force` flag from args
- Validate file extension (.sna, .z80)
- Check file existence if not `--force`
- Update [HandleSnapshot()](core/automation/cli/src/commands/cli-processor-snapshot.cpp#14-51) switch to include `"save"` subcommand
- Update [ShowSnapshotHelp()](core/automation/cli/src/commands/cli-processor-snapshot.cpp#98-110) with new command

---

### WebAPI Automation

#### [MODIFY] [snapshot_api.cpp](core/automation/webapi/src/api/snapshot_api.cpp)

Add:
- `saveSnapshot()` handler for `POST /api/v1/emulator/:id/snapshot/save`
- Request body: `{"path": "/path/to/save.sna", "force": true/false}`
- Response: `{"status": "success/error", "path": "...", "format": "sna48/sna128"}`

**Behavior:**
- Format from path extension: `.sna` or `.z80`
- Invalid extension → 400 Bad Request: "Unknown snapshot format"
- File exists + `force=false` → 409 Conflict: "File already exists"

#### [MODIFY] [emulator_api.h](core/automation/webapi/src/emulator_api.h)

Add method declaration for `saveSnapshot()`

#### [MODIFY] [openapi_spec.cpp](core/automation/webapi/src/openapi_spec.cpp)

Add OpenAPI spec for new endpoint

---

### Python Bindings

#### [MODIFY] Python emulator bindings

Add `save_snapshot(path, force=False)` method to `PythonBindings::registerEmulatorBindings()`

**Behavior:**
- Format from path extension
- `force=False`: Raises exception if file exists
- `force=True`: Overwrites existing file

---

### Lua Bindings

#### [MODIFY] [lua_emulator.cpp](core/automation/lua/src/emulator/lua_emulator.cpp)

Add `save_snapshot(path, force)` method to Lua emulator type

**Behavior:**
- Format from path extension
- `force=false`: Returns error if file exists
- `force=true`: Overwrites existing file

---

### Qt UI

#### [MODIFY] [menumanager.h](unreal-qt/src/menumanager.h)

- Replace `_saveSnapshotAction` with `_saveSnapshotMenu` (submenu)
- Add `_saveSnapshotSnaAction`, `_saveSnapshotZ80Action`
- Add `QString _lastSnapshotSaveDir` for folder persistence

#### [MODIFY] [menumanager.cpp](unreal-qt/src/menumanager.cpp)

- Convert "Save Snapshot..." to submenu with ".sna" and ".z80" options
- `.z80` option disabled (placeholder for future)
- Connect [.sna](core/tests/build-standalone/bin/testdata/loaders/sna/s4b-1.sna) action to new slot `saveSnapshotSna()`

#### [NEW] Save Snapshot Dialog Logic (in mainwindow.cpp or menumanager.cpp)

Implement:
- `saveSnapshotSna()` slot
- Use `QFileDialog::getSaveFileName()` with [.sna](core/tests/build-standalone/bin/testdata/loaders/sna/s4b-1.sna) filter
- Load/save last directory from `QSettings`
- Check file existence, show overwrite confirmation via `QMessageBox::question()`
- Call `emulator->SaveSnapshot(path)`

---

## Verification Plan

### Automated Tests

**Run core-tests for SNA loader:**
```bash
cd /Volumes/TB4-4Tb/Projects/Test/unreal-ng
cmake --build build-standalone --target core-tests
./build-standalone/bin/core-tests --gtest_filter="LoaderSNA*"
```

**New test cases to add in** [loader_sna_test.cpp](core/tests/loaders/loader_sna_test.cpp):

| Test Name | Description |
|-----------|-------------|
| `save48kBasic` | Save 48K state, verify file size = 49179 bytes |
| `save128kBasic` | Save 128K state, verify file size ≥ 49183 bytes |
| `saveAndLoadRoundtrip48k` | Save → Load → Compare registers |
| `saveAndLoadRoundtrip128k` | Save → Load → Compare registers + all pages |
| `determineOutputFormat_Locked` | Verify locked port → 48K format |
| `determineOutputFormat_Unlocked` | Verify unlocked 128K → 128K format |
| `saveSkipsEmptyPages` | Verify empty pages not written (128K) |
| `saveCreatesValidHeader` | Verify header fields match emulator state |
| `save48kPushesPC` | Verify PC is pushed to stack in 48K save |

### WebAPI Verification

**Run Python WebAPI tests:**
```bash
cd /Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi
python3 -m pytest src/ -k snapshot -v
```

Add new test to `src/test_snapshot_api.py`:
- `test_save_snapshot_sna()` - POST save, verify file created
- `test_save_snapshot_roundtrip()` - Save, load, compare state

### Manual Verification

1. **CLI Test:**
   ```bash
   # Connect to CLI
   nc localhost 10000
   > create
   > resume
   > snapshot save /tmp/test.sna
   > snapshot info
   ```
   Expected: "Snapshot saved: /tmp/test.sna" and file exists

2. **Qt UI Test:**
   - Open unreal-qt
   - Load any emulator state (e.g., run a program)
   - File → Save Snapshot → .sna
   - Verify file dialog opens with .sna filter
   - Enter filename, click Save
   - Verify success message
   - Try saving to same file → verify overwrite confirmation

3. **Round-trip Verification:**
   - Load existing SNA file
   - Save to new file
   - Load saved file
   - Compare screen output visually
