# BASIC Commands Propagation Implementation Plan

Propagate BASIC commands from CLI to all automation modules, then proceed with WRITE TRACK integration testing.

## Current State

| Module | BASIC Support | Status |
|--------|--------------|--------|
| CLI | `basic run/inject/extract/list/clear` | ✅ Implemented |
| WebAPI | None | 🔧 To Implement |
| Lua | None | 🔧 To Implement |
| Python | None | 🔧 To Implement |
| OpenAPI | None | 🔧 To Implement |

---

## Phase 1: WebAPI BASIC Endpoints

### [NEW] [basic_api.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/automation/webapi/src/api/basic_api.cpp)

Following the [tape_disk_api.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/automation/webapi/src/api/tape_disk_api.cpp) pattern:

```cpp
// POST /api/v1/emulator/:id/basic/run
// Body: {"command": "FORMAT \"a\""} or {} for plain RUN
void EmulatorAPI::basicRun(const HttpRequestPtr& req,
                           std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id);

// POST /api/v1/emulator/:id/basic/inject  
// Body: {"program": "10 PRINT \"HELLO\"\n20 GOTO 10"}
void EmulatorAPI::basicInject(const HttpRequestPtr& req, ...);

// GET /api/v1/emulator/:id/basic/extract
// Returns: {"program": "10 PRINT \"HELLO\"..."}
void EmulatorAPI::basicExtract(const HttpRequestPtr& req, ...);

// POST /api/v1/emulator/:id/basic/clear
void EmulatorAPI::basicClear(const HttpRequestPtr& req, ...);
```

### [MODIFY] [emulator_api.h](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/automation/webapi/src/emulator_api.h)

Add method declarations and PATH/METHOD macros.

---

## Phase 2: Update OpenAPI Spec

### [MODIFY] [openapi_spec.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/automation/webapi/src/openapi_spec.cpp)

Add BASIC endpoint definitions following existing patterns.

---

## Phase 3: Lua Bindings

### [MODIFY] [lua_emulator.h](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/automation/lua/src/emulator/lua_emulator.h)

```lua
-- Global functions:
basic_run(command)        -- Inject and execute command
basic_inject(program)     -- Load program into memory  
basic_extract()           -- Return current program as string
basic_clear()             -- Clear program (NEW equivalent)
basic_state()             -- Returns "48k", "128k", or "menu128k"
```

---

## Phase 4: Python API Client

### [MODIFY] [api_client.py](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi/src/api_client.py)

```python
def basic_run(self, emulator_id, command=None):
def basic_inject(self, emulator_id, program):
def basic_extract(self, emulator_id):
def basic_clear(self, emulator_id):
```

---

## Phase 5: Integration Test (FORMAT)

### [NEW] [test_api_format.py](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi/src/test_api_format.py)

```python
def test_trdos_format(self, api_client, pentagon_emulator):
    # 1. Create blank disk
    api_client.create_disk(emu_id, "A")
    
    # 2. Execute FORMAT via BASIC
    api_client.basic_run(emu_id, 'FORMAT "a"')
    
    # 3. Wait for format (run frames)
    time.sleep(5)  # Or use polling
    
    # 4. Verify
    sysinfo = api_client.get_disk_sysinfo(emu_id, "A")
    assert sysinfo["signature_valid"] is True
```

---

## Files Summary

| Action | File | Purpose |
|--------|------|---------|
| NEW | `webapi/src/api/basic_api.cpp` | WebAPI BASIC endpoints |
| MODIFY | [webapi/src/emulator_api.h](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/automation/webapi/src/emulator_api.h) | Add declarations |
| MODIFY | [webapi/src/openapi_spec.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/automation/webapi/src/openapi_spec.cpp) | OpenAPI definitions |
| MODIFY | `lua/src/emulator/lua_emulator.h` | Lua bindings |
| MODIFY | [tools/verification/webapi/src/api_client.py](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi/src/api_client.py) | Python client |
| NEW | `tools/verification/webapi/src/test_api_format.py` | Integration test |

---

## Verification

```bash
# Build automation modules
cd /Volumes/TB4-4Tb/Projects/Test/unreal-ng
cmake --build build --target automation-webapi automation-lua

# Run WebAPI tests
cd tools/verification/webapi
pytest src/test_api_format.py -v
```
