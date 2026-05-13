# Disk Image Change Tracking & Save Implementation

## Summary
Implemented disk image change tracking (dirty flags) and save functionality for TRD and SCL formats in the Unreal-NG emulator core.

## Changes Made

### 1. Dirty Tracking in [diskimage.h](core/src/emulator/io/fdc/diskimage.h) / [diskimage.cpp](core/src/emulator/io/fdc/diskimage.cpp)

| Class | Added Fields/Methods |
|-------|---------------------|
| **Track** | `_dirty`, `_rawTrackDirty` flags, `_sectorDirtyBitmap` (uint16_t) |
| **Track** | [isDirty()](core/src/emulator/io/fdc/diskimage.h#650-652), [isSectorDirty()](core/src/emulator/io/fdc/diskimage.h#281-288), [hasAnySectorDirty()](core/src/emulator/io/fdc/diskimage.h#289-294) |
| **Track** | [writeSectorData()](core/src/emulator/io/fdc/diskimage.h#314-335) - writes with memcmp change detection |
| **Track** | [markClean()](core/src/emulator/io/fdc/diskimage.h#663-672) - resets all flags |
| **DiskImage** | `_dirty` flag, [isDirty()](core/src/emulator/io/fdc/diskimage.h#650-652), [markClean()](core/src/emulator/io/fdc/diskimage.h#663-672) |

**Key Design Decisions:**
- Sector dirty tracking at Track level via bitmap (16 bits for 16 sectors)
- [RawSectorBytes](core/src/emulator/io/fdc/diskimage.h#86-165) remains packed at 388 bytes (no fields added)
- Content-change detection via `memcmp` before marking dirty
- Auto-propagation: sector write → track dirty → disk image dirty

---

### 2. TRD Save Enhancement in [loader_trd.cpp](core/src/loaders/disk/loader_trd.cpp)

```cpp
// New path overload for "Save As"
bool writeImage(const std::string& path);

// Original now delegates to overload
bool writeImage() { return writeImage(_filepath); }
```

After successful save:
- Calls `_diskImage->markClean()`
- Updates `_diskImage->setFilePath(path)`

---

### 3. SCL Save Implementation in [loader_scl.cpp](core/src/loaders/disk/loader_scl.cpp)

Full implementation of [writeImage()](core/src/loaders/disk/loader_scl.cpp#33-37):
1. Reads TR-DOS catalog from track 0
2. Outputs SCL header: `"SINCLAIR"` (8 bytes) + file count (1 byte)
3. Writes file descriptors (14 bytes each - without start sector/track)
4. Writes file data sequentially
5. Appends CRC32 checksum (sum of all preceding bytes)

---

## Test Results

**27 tests passed:**

| Test Suite | Tests |
|------------|-------|
| DiskImage_Test | 14 (including 5 new dirty tracking) |
| DiskImagePathTest | 5 |
| LoaderSCL_Test | 2 |
| LoaderTRD_Test | 6 |

### New Dirty Tracking Tests
- `DirtyTracking_InitialState` - verifies clean initial state
- `DirtyTracking_WriteSectorData` - verifies write marks dirty
- `DirtyTracking_NoChangeNoDirty` - identical data doesn't trigger dirty
- `DirtyTracking_MarkClean` - resets all flags correctly
- `DirtyTracking_MultipleSectors` - sector bitmap works correctly

---

## Files Modified

| File | Changes |
|------|---------|
| [diskimage.h](core/src/emulator/io/fdc/diskimage.h) | Added dirty tracking to Track and DiskImage |
| [diskimage.cpp](core/src/emulator/io/fdc/diskimage.cpp) | Implemented [markDirty()](core/src/emulator/io/fdc/diskimage.h#641-643) propagation |
| [loader_trd.h](core/src/loaders/disk/loader_trd.h) | Added [writeImage(path)](core/src/loaders/disk/loader_scl.cpp#33-37) declaration |
| [loader_trd.cpp](core/src/loaders/disk/loader_trd.cpp) | Enhanced save with markClean, path overload |
| [loader_scl.h](core/src/loaders/disk/loader_scl.h) | Added [writeImage(path)](core/src/loaders/disk/loader_scl.cpp#33-37), [setImage()](core/src/loaders/disk/loader_scl.cpp#169-174) |
| [loader_scl.cpp](core/src/loaders/disk/loader_scl.cpp) | Full SCL save implementation |
| [diskimage_test.cpp](core/tests/emulator/io/fdc/diskimage_test.cpp) | Added 5 dirty tracking tests |

---

### 4. Unreal-Qt UI Integration

**MenuManager** ([menumanager.h](unreal-qt/src/menumanager.h), [menumanager.cpp](unreal-qt/src/menumanager.cpp)):
- Added `_saveDiskMenu` submenu with 3 actions
- Added signals: `saveDiskRequested`, `saveDiskAsTRDRequested`, `saveDiskAsSCLRequested`
- Menu enabled when emulator exists

**MainWindow** ([mainwindow.h](unreal-qt/src/mainwindow.h), [mainwindow.cpp](unreal-qt/src/mainwindow.cpp)):
- Added 3 slots: [saveDiskDialog()](unreal-qt/src/mainwindow.h#73-74), [saveDiskAsTRDDialog()](unreal-qt/src/mainwindow.cpp#1633-1708), [saveDiskAsSCLDialog()](unreal-qt/src/mainwindow.cpp#1709-1784)
- Each slot accesses disk via `Context->pBetaDisk->getDrive()->getDiskImage()`
- TRD/SCL format selection via appropriate loader

> [!NOTE]
> Qt `signals` macro conflict with WD1793 resolved using `#undef signals` / `#define signals Q_SIGNALS` around include.

---

## Files Modified

| File | Changes |
|------|---------|
| [diskimage.h](core/src/emulator/io/fdc/diskimage.h) | Added dirty tracking to Track and DiskImage |
| [loader_trd.cpp](core/src/loaders/disk/loader_trd.cpp) | Enhanced save with markClean, path overload |
| [loader_scl.cpp](core/src/loaders/disk/loader_scl.cpp) | Full SCL save implementation |
| [menumanager.h](unreal-qt/src/menumanager.h) | Added disk save signals and menu members |
| [menumanager.cpp](unreal-qt/src/menumanager.cpp) | Added Save Disk submenu creation & state mgmt |
| [mainwindow.h](unreal-qt/src/mainwindow.h) | Added disk save slots |
| [mainwindow.cpp](unreal-qt/src/mainwindow.cpp) | Implemented 3 disk save dialogs |
| [diskimage_test.cpp](core/tests/emulator/io/fdc/diskimage_test.cpp) | Added 5 dirty tracking tests |
