# OpenAPI Specification Maintenance Guide

## ⚠️ CRITICAL: Manual Maintenance Required

The OpenAPI 3.0 specification for the Unreal Speccy Emulator WebAPI is **MANUALLY MAINTAINED** and **NOT auto-generated**.

This means that **every API change** requires **manual updates** to the OpenAPI specification.

## Why Manual Maintenance?

- **No auto-generation tooling** is currently integrated
- **Complex API structure** with multiple endpoints and parameter types
- **Custom business logic** that can't be easily inferred from code
- **Performance considerations** - avoiding runtime generation overhead

## Current Status

✅ **100% Coverage** - All 144 registered routes documented  
✅ **Modular Organization** - 14 domain-specific `.inc` files  
✅ **Complete Schemas** - 30+ response schemas defined  
✅ **Organized Tags** - 20 feature-based categories  
✅ **Automated Verification** - Coverage test script available

**Last Updated:** 2026-02-07  
**Registered Routes:** 144 (across `emulator_api.h` and `interpreter_api.h`)  
**OpenAPI Paths:** 149 (includes 5 planned profiler endpoints)

## Code Organization

### OpenAPI Specification (Modular `.inc` Files)

The specification is split into a slim skeleton (`openapi_spec.cpp`, ~160 lines) that `#include`s domain-specific `.inc` files from `src/openapi/`:

| File | Domain | Content |
|------|--------|---------|
| `openapi_interpreter.inc` | Interpreter Control | Python/Lua exec, status, stop |
| `openapi_lifecycle.inc` | Emulator Management | Create, start, stop, pause, resume, reset |
| `openapi_tape_disk.inc` | Tape & Disk Control | Load, eject, insert, sector/track inspection |
| `openapi_snapshot.inc` | Snapshot Control | Load, save, info |
| `openapi_capture.inc` | Capture | Screen capture, OCR |
| `openapi_basic.inc` | BASIC Control | Run, inject, extract, clear, state, mode |
| `openapi_keyboard.inc` | Keyboard Injection | Tap, press, release, combo, macro, type, abort |
| `openapi_settings.inc` | Settings Management | Get/set emulator settings |
| `openapi_features.inc` | Feature Management | Get/set runtime features |
| `openapi_state.inc` | State Inspection | Memory, screen, audio state + memory read/write |
| `openapi_analyzers.inc` | Analyzer Management | Analyzer control, events, sessions |
| `openapi_debug.inc` | Debug Commands | Stepping, breakpoints, registers, disassembly |
| `openapi_profiler.inc` | Profilers | Memory, call trace, opcode, unified profiler |
| `openapi_schemas.inc` | Component Schemas | Reusable response/request schemas |

### Endpoint Implementation Files (`src/api/`)

| File | Domain |
|------|--------|
| `lifecycle_api.cpp` | Emulator lifecycle |
| `tape_disk_api.cpp` | Tape and disk control |
| `snapshot_api.cpp` | Snapshot management |
| `capture_api.cpp` | Screen capture and OCR |
| `basic_api.cpp` | BASIC program control |
| `keyboard_api.cpp` | Keyboard injection |
| `settings_api.cpp` | Settings management |
| `features_api.cpp` | Feature management |
| `state_memory_api.cpp` | Memory state inspection |
| `state_screen_api.cpp` | Screen state inspection |
| `state_audio_api.cpp` | Audio state inspection |
| `analyzers_api.cpp` | Analyzer management |
| `debug_api.cpp` | Debug commands |
| `profiler_api.cpp` | Profiler commands |
| `interpreter_api.cpp` | Python/Lua interpreter |

### Core Files
- **`src/emulator_api.h`** - Main API interface with `ADD_METHOD_TO` route registrations
- **`src/emulator_api.cpp`** - Core infrastructure
- **`src/openapi_spec.cpp`** - OpenAPI skeleton with `#include` directives (~160 lines)
- **`src/api/interpreter_api.h`** - Interpreter API interface (separate Drogon controller)

## Automated Coverage Verification

Run the verification script to ensure all registered routes are documented:

```bash
# From project root
python3 tools/verification/webapi/verify_openapi_coverage.py

# Strict mode (also flags documented-but-unregistered routes as errors)
python3 tools/verification/webapi/verify_openapi_coverage.py --strict
```

The script compares:
- **Source of truth**: `ADD_METHOD_TO` declarations in `emulator_api.h` and `interpreter_api.h`
- **Documentation**: `paths["/api/v1/..."]` entries in `src/openapi/*.inc` files

Exit code 0 = all routes covered. Exit code 1 = missing documentation.

## Maintenance Process

### When Adding a New API Endpoint:

1. **Implement** in the appropriate `src/api/*.cpp` file
2. **Register** the route in `emulator_api.h` (or `interpreter_api.h`) with `ADD_METHOD_TO`
3. **Document** in the matching `src/openapi/openapi_*.inc` file
4. **Run** `python3 tools/verification/webapi/verify_openapi_coverage.py` to verify
5. **Test** with Swagger UI to verify documentation accuracy

### When Changing Parameters/Responses:

1. **Modify** the API endpoint implementation
2. **Update** corresponding OpenAPI paths in the `.inc` file
3. **Verify** parameter schemas match actual implementation
4. **Update** response schemas in `openapi_schemas.inc` if needed
5. **Test** with real API calls

## Common Mistakes to Avoid

❌ **Adding new endpoint without updating OpenAPI `.inc` file**  
❌ **Changing parameter names/types without updating schemas**  
❌ **Modifying response formats without updating response schemas**  
❌ **Not running the coverage verification script after changes**  
❌ **Missing CORS headers** (always call `addCorsHeaders(resp)` before `callback(resp)`)

## Testing Documentation Accuracy

```bash
# 1. Start the API server
./unreal-qt

# 2. Start Swagger UI
docker run --name unreal-speccy-swagger-ui \
  -p 8081:8080 \
  -e SWAGGER_JSON_URL=http://localhost:8090/api/v1/openapi.json \
  swaggerapi/swagger-ui

# 3. Test each endpoint in Swagger UI
# 4. Verify parameters and responses match actual API
```

## Quick Command Reference

```bash
# Run OpenAPI coverage check
python3 tools/verification/webapi/verify_openapi_coverage.py

# View line counts of modular .inc files
wc -l core/automation/webapi/src/openapi/*.inc

# Check OpenAPI endpoint count (requires running server)
curl -s http://localhost:8090/api/v1/openapi.json | jq '.paths | keys | length'

# List all documented endpoints
curl -s http://localhost:8090/api/v1/openapi.json | jq '.paths | keys'

# Verify all methods have CORS headers
grep -r "addCorsHeaders" core/automation/webapi/src/api/*.cpp | wc -l
```

## Files to Update

When making API changes, update these files:

1. **Modular implementation file** (e.g., `src/api/lifecycle_api.cpp`)
2. **`src/emulator_api.h`** - Add `ADD_METHOD_TO` declaration in appropriate region
3. **`src/openapi/openapi_*.inc`** - Add path definition in matching `.inc` file
4. **`src/openapi/openapi_schemas.inc`** - Add/update response schemas if needed
5. **Run** `python3 tools/verification/webapi/verify_openapi_coverage.py`
6. **This file** (`OPENAPI_MAINTENANCE.md`) - Update status section if needed
