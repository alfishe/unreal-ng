# Disk Inspection API Design - Implementation Plan

## Problem Analysis

### User's Issue
1. **Single-Side Detection**: TR-DOS FORMAT shows cylinders changing but head always 0
2. **Sector 9 Read Failure**: TR-DOS cannot read system volume info after formatting

### Root Cause
The **Shadow Sector Hazard**: WD1793 Write Track physically writes MFM stream but logical sector re-indexing may not trigger automatically.

> [!IMPORTANT]
> Current WebAPI has **no disk data inspection** - only insert/eject/info. Need new endpoints to inspect FDC writes.

---

## Architecture: Multi-Drive Support

### Key Design Decisions
1. **Per-Emulator-Instance**: Each emulator instance has its own set of 4 drives (A-D / 0-3)
2. **Expandable**: Currently 4 drives, but not limited in future
3. **Auto-Selection**: If only one disk mounted, auto-select for commands without explicit drive
4. **Drive Selection**: Track "selected drive" for subsequent commands (like emulator selection)

---

## Proposed Commands

### Drive Management Commands

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `disk list` | | List all drives with mount status (*, mounted filename, geometry) |
| `disk select <drive>` | `A-D` or `0-3` | Select drive for subsequent commands |
| `disk` | | Show selected drive info (or list if none selected) |

### Logical Data Access (parsed/structured)

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `disk sector [drv] <cyl> <side> <sec>` | `[A-D]` `0-79` `0-1` `1-16` | Parsed sector: ID, data (256b), CRCs |
| `disk track [drv] <cyl> <side>` | `[A-D]` `0-79` `0-1` | Track summary: 16 sectors with metadata |
| `disk sysinfo [drv]` | `[A-D]` | TR-DOS system sector parsed (T0/S9) |

### Raw Data Access (byte-level)

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `disk sector raw [drv] <cyl> <side> <sec>` | `[A-D]` `0-79` `0-1` `1-16` | Raw sector bytes (388b incl gaps/marks) |
| `disk track raw [drv] <cyl> <side>` | `[A-D]` `0-79` `0-1` | Raw track bytes (6250b MFM stream) |
| `disk image [drv]` | `[A-D]` | Whole image binary (all tracks) |
| `disk reindex [drv] <cyl> <side>` | `[A-D]` `0-79` `0-1` | Force sector re-indexing (debug) |

---

### 3.1 WebAPI Endpoints ✅ COMPLETE

All endpoints scoped to emulator instance: `/api/v1/emulator/{id}/disk/...`

| Endpoint | Response |
|:---|:---|
| `GET /disk` | Drives list, FDC state, auto-select |
| `GET /disk/{drive}` | Drive info |
| `GET /disk/{drive}/sector/{c}/{s}/{n}` | Sector + address mark + base64 data |
| `GET /disk/{drive}/sector/{c}/{s}/{n}/raw` | Raw sector base64 |
| `GET /disk/{drive}/track/{c}/{s}` | 16-sector summary |
| `GET /disk/{drive}/track/{c}/{s}/raw` | 6250b MFM base64 |
| `GET /disk/{drive}/image` | Full image base64 |
| `GET /disk/{drive}/sysinfo` | TR-DOS T0/S9 parsed |
| `GET /disk/{drive}/catalog` | TR-DOS file list |

**Files Modified:**
- `emulator_api.h` - Routes and declarations
- `tape_disk_api.cpp` - ~900 LOC implementations
- `openapi_spec.cpp` - OpenAPI paths and Disk Inspection tag

### Response: `GET /disk` (Drive List)
```json
{
  "drives": [
    {"id": 0, "letter": "A", "mounted": true, "file": "game.trd", 
     "cylinders": 80, "sides": 2, "write_protected": false},
    {"id": 1, "letter": "B", "mounted": false},
    {"id": 2, "letter": "C", "mounted": false},
    {"id": 3, "letter": "D", "mounted": false}
  ],
  "selected": "A",
  "fdc_state": {"track_reg": 0, "sector_reg": 1, "status": "0x00", "busy": false}
}
```

### Response: `GET /disk/{drive}/sysinfo` (TR-DOS)
```json
{
  "drive": "A",
  "dos_type": "TR-DOS",
  "disk_type": "0x19",
  "disk_type_decoded": "80T Double-Sided", 
  "label": "TESTDISK",
  "file_count": 0,
  "free_sectors": 2544,
  "first_free_track": 1,
  "first_free_sector": 0,
  "trdos_signature": "0x10",
  "signature_valid": true
}
```

---

## Files to Modify

### 1. Command Interface Doc
- [command-interface.md](docs/emulator/design/control-interfaces/command-interface.md) - Add Section 6.5 disk commands

---

### 2. WebAPI Module
| File | Changes |
| :--- | :--- |
| [emulator_api.h](core/automation/webapi/src/emulator_api.h) | Method declarations + ADD_METHOD_TO routes |
| [tape_disk_api.cpp](core/automation/webapi/src/api/tape_disk_api.cpp) | Endpoint implementations |
| [openapi_spec.cpp](core/automation/webapi/src/openapi_spec.cpp) | OpenAPI schema + response models |

---

### 3. CLI Automation Module

| File | Changes |
| :--- | :--- |
| [cli-processor-analyzer.cpp](core/automation/cli/src/commands/cli-processor-analyzer.cpp) | Command handlers for `disk` command tree |

**CLI Commands:**
```
disk list                    # List all drives with status
disk select <A-D|0-3>        # Select active drive
disk sector [drv] C S N      # Logical sector (256b + metadata)
disk sector raw [drv] C S N  # Raw sector (388b)
disk track [drv] C S         # Track summary
disk track raw [drv] C S     # Raw track (6250b)
disk image [drv]             # Whole image dump
disk sysinfo [drv]           # TR-DOS system sector
```

---

### 4. Lua Automation Module

| File | Changes |
| :--- | :--- |
| Lua bindings source | Register disk inspection functions |

**Lua API:**
```lua
emu:disk_list()                       -- Table of drive info
emu:disk_select(drv)                  -- Select drive
emu:disk_sector(drv, cyl, side, sec)  -- Logical sector (256b + meta)
emu:disk_sector_raw(drv, cyl, s, sec) -- Raw sector bytes (388b)
emu:disk_track(drv, cyl, side)        -- Track summary
emu:disk_track_raw(drv, cyl, side)    -- Raw track bytes (6250b)
emu:disk_image(drv)                   -- Whole image binary
emu:disk_sysinfo(drv)                 -- TR-DOS info table
```

---

### 5. Python Automation Module

| File | Changes |
| :--- | :--- |
| Python bindings (pybind11) | Register disk inspection class/methods |

**Python API:**
```python
emulator.disk.list()                      # List[DriveInfo]
emulator.disk.select(drv)                 # Select drive
emulator.disk.read_sector(drv, c, s, n)   # SectorData (256b + meta)
emulator.disk.read_sector_raw(drv, c,s,n) # bytes (388b)
emulator.disk.read_track(drv, cyl, side)  # TrackSummary  
emulator.disk.read_track_raw(drv, c, s)   # bytes (6250b)
emulator.disk.read_image(drv)             # bytes (whole image)
emulator.disk.sysinfo(drv)                # TRDOSInfo
```

---

## Verification Plan

1. Start Pentagon with blank TRD on drive A
2. `curl .../disk` → verify drive A shows mounted
3. `curl .../disk/A/track/0/0` → verify empty/default sectors
4. Run TR-DOS FORMAT in emulator
5. `curl .../disk/A/sysinfo` → verify `disk_type=0x19`, `free_sectors=2544`
6. `curl .../disk/A/sector/0/0/9` → verify sector 9 metadata valid
