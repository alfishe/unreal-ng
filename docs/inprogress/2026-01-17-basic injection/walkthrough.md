# BASIC Commands Propagation - Walkthrough

## Summary

Successfully propagated BASIC commands ([run](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi/src/api_client.py#219-235), [inject](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi/src/api_client.py#236-250), [extract](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi/src/api_client.py#251-262), [clear](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi/src/api_client.py#263-274), [state](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi/src/api_client.py#275-286)) from CLI to all automation modules:

| Module | Status | Files Modified |
|--------|--------|----------------|
| WebAPI | ✅ Complete | [basic_api.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/automation/webapi/src/api/basic_api.cpp) (new), [emulator_api.h](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/automation/webapi/src/emulator_api.h), [openapi_spec.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/automation/webapi/src/openapi_spec.cpp), [CMakeLists.txt](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/automation/CMakeLists.txt) |
| Lua | ✅ Complete | [lua_emulator.h](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/automation/lua/src/emulator/lua_emulator.h) |
| Python | ✅ Complete | [api_client.py](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi/src/api_client.py) |
| Tests | ✅ Complete | [test_api_basic.py](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi/src/test_api_basic.py) (new) |

---

## Changes Made

### WebAPI Endpoints

Created [basic_api.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/automation/webapi/src/api/basic_api.cpp) with 5 endpoints:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/emulator/{id}/basic/run` | POST | Execute BASIC command |
| `/api/v1/emulator/{id}/basic/inject` | POST | Inject multi-line BASIC program |
| `/api/v1/emulator/{id}/basic/extract` | GET | Extract program from memory |
| `/api/v1/emulator/{id}/basic/clear` | POST | Clear program (NEW equivalent) |
| `/api/v1/emulator/{id}/basic/state` | GET | Get BASIC environment state |

### Lua Bindings

Added to [lua_emulator.h](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/automation/lua/src/emulator/lua_emulator.h):

```lua
basic_run([command])   -- Execute BASIC command
basic_inject(program)  -- Inject program into memory  
basic_extract()        -- Extract current program
basic_clear()          -- Clear program
basic_state()          -- Get environment state
```

### Python Client

Added to [api_client.py](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi/src/api_client.py):

```python
basic_run(emulator_id, command=None)
basic_inject(emulator_id, program)
basic_extract(emulator_id)
basic_clear(emulator_id)
basic_state(emulator_id)
```

---

## Verification Tests

Created [test_api_basic.py](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi/src/test_api_basic.py) with 9 tests.

```bash
# Run tests
cd /Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi
pytest src/test_api_basic.py -v
```

---

## Next Steps

1. **Build**: Rebuild automation modules to include new BASIC endpoints
2. **Test**: Run [test_api_basic.py](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi/src/test_api_basic.py) against running WebAPI server
3. **Integration**: Use [basic_run](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/tools/verification/webapi/src/api_client.py#219-235) with `FORMAT "a"` for WRITE TRACK testing
