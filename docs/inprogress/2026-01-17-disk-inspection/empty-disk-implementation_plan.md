# Disk Create Command Implementation

## Goal
Add `disk create <drive> [cylinders] [sides]` command to insert a blank, unformatted disk into any drive.

## Design

### Command Specification

| Interface | Command/Endpoint | Arguments | Description |
|-----------|------------------|-----------|-------------|
| **CLI** | `disk create <drv> [cyl] [sides]` | `A-D`, `[40\|80]`, `[1\|2]` | Create empty disk, default 80T/DS |
| **WebAPI** | `POST /disk/{drive}/create` | JSON: `{cylinders, sides}` | Returns disk info |
| **Python** | `create_disk(id, drv, cyl, sides)` | | Returns response |

### Core Implementation
- Use existing `DiskImage(cylinders, sides)` constructor
- Insert into FDD via existing `fdd->insertDisk(diskImage)`
- Track path as `<virtual-blank>` for API queries

### Default Values
- **cylinders**: 80 (standard TRD, can be 40)
- **sides**: 2 (double-sided, can be 1 for single-sided)

---

## Changes

### 1. CLI (`cli-processor-disk.cpp`)

Add handler in `HandleDisk()`:
```cpp
else if (subcommand == "create") {
    HandleDiskCreate(session, context, args);
}
```

New function `HandleDiskCreate()`:
- Parse args: drive (required), cylinders (default 80), sides (default 2)
- Create `DiskImage* disk = new DiskImage(cyl, sides);`
- Get FDD from context and call `fdd->insertDisk(disk);`
- Update `coreState.diskFilePaths[driveNum] = "<blank>";`
- Return success message

Update help text to include `create <drv> [cyl] [sides]`.

### 2. WebAPI (`tape_disk_api.cpp`, `emulator_api.h`)

Add endpoint declaration in `emulator_api.h`:
```cpp
ADD_METHOD_TO(EmulatorAPI::createDisk, "/api/v1/emulator/{id}/disk/{drive}/create", drogon::Post);
void createDisk(...);
```

Implement in `tape_disk_api.cpp`:
- Parse JSON body for optional `cylinders` and `sides`
- Create DiskImage and insert
- Return JSON with drive, cylinders, sides, success

Update OpenAPI spec in `openapi_spec.cpp`.

### 3. Python Client (`api_client.py`)

Add method:
```python
def create_disk(self, emulator_id, drive, cylinders=80, sides=2):
    """POST /api/v1/emulator/{id}/disk/{drive}/create"""
    data = {"cylinders": cylinders, "sides": sides}
    resp = self.session.post(self._url(f"/api/v1/emulator/{emulator_id}/disk/{drive}/create"), json=data)
    return resp.json()
```

### 4. Tests (`test_api_disk.py`)

Add tests:
```python
def test_create_blank_disk(self, api_client, active_emulator):
    """Test creating a blank unformatted disk."""
    result = api_client.create_disk(active_emulator, "A")
    assert result["success"] is True
    # Verify disk is mounted
    drives = api_client.list_disk_drives(active_emulator)
    assert drives["drives"][0]["mounted"] is True

def test_create_disk_custom_geometry(self, api_client, active_emulator):
    """Test creating disk with custom geometry."""
    result = api_client.create_disk(active_emulator, "B", cylinders=40, sides=1)
    assert result["cylinders"] == 40
    assert result["sides"] == 1
```

### 5. Documentation (`command-interface.md`)

Add to disk commands table:
```markdown
| `disk create <drive> [cyl] [sides]` | `<A\|B\|C\|D> [40\|80] [1\|2]` | Create blank unformatted disk in drive. Default: 80 cylinders, 2 sides. Ready for TR-DOS FORMAT. | ✅ Implemented |
```

---

## Verification
- [ ] CLI: `disk create A` inserts blank disk
- [ ] CLI: `disk list` shows A as mounted with `<blank>`
- [ ] WebAPI: POST returns success JSON
- [ ] Python: `create_disk()` works
- [ ] Test: All new tests pass
- [ ] TR-DOS FORMAT can format the blank disk
