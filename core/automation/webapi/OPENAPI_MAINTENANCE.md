# OpenAPI Specification Maintenance Guide

## ⚠️ CRITICAL: Manual Maintenance Required

The OpenAPI 3.0 specification for the Unreal Speccy Emulator WebAPI is **MANUALLY MAINTAINED** and **NOT auto-generated**.

This means that **every API change** requires **manual updates** to the OpenAPI JSON specification.

## Why Manual Maintenance?

- **No auto-generation tooling** is currently integrated
- **Complex API structure** with multiple endpoints and parameter types
- **Custom business logic** that can't be easily inferred from code
- **Performance considerations** - avoiding runtime generation overhead

## Current Status

✅ **100% Coverage** - All 52 endpoints documented  
✅ **Modular Organization** - Endpoints grouped by functional area  
✅ **Complete Schemas** - 17 response schemas defined  
✅ **Organized Tags** - 10 feature-based categories

**Last Updated:** 2026-01-08  
**Endpoints Documented:** 52/52  
**File Size:** 638 lines

## Code Organization (Post-Refactoring)

The WebAPI codebase has been refactored into modular files organized by functional area:

### Core Files
- **`src/emulator_api.h`** - Main API interface with region markers
- **`src/emulator_api.cpp`** - Core infrastructure (265 lines, 90% reduction)
- **`src/openapi_spec.cpp`** - OpenAPI specification (638 lines)

### Modular Endpoint Implementations (`src/api/`)
- **`lifecycle_api.cpp`** - Emulator lifecycle (get, create, delete, start, stop, pause, resume, reset)
- **`tape_disk_api.cpp`** - Tape and disk control
- **`snapshot_api.cpp`** - Snapshot management  
- **`settings_api.cpp`** - Settings management
- **`state_memory_api.cpp`** - Memory state inspection (RAM, ROM)
- **`state_screen_api.cpp`** - Screen state inspection
- **`state_audio_api.cpp`** - Audio state inspection (AY, beeper, GS, Covox)

## Maintenance Process

### When Adding/Modifying API Endpoints:

1. **Implement the API endpoint** in the appropriate modular file:
   - Lifecycle operations → `api/lifecycle_api.cpp`
   - Tape/disk operations → `api/tape_disk_api.cpp`
   - Snapshot operations → `api/snapshot_api.cpp`
   - Settings operations → `api/settings_api.cpp`
   - Memory state → `api/state_memory_api.cpp`
   - Screen state → `api/state_screen_api.cpp`
   - Audio state → `api/state_audio_api.cpp`

2. **Add autodoc comments** above the method:
   ```cpp
   /// @brief GET /api/v1/emulator/{id}/endpoint
   /// @brief Description of what this endpoint does
   void EmulatorAPI::methodName(...)
   ```

3. **Update the OpenAPI specification** in `openapi_spec.cpp`:
   - Add path definition
   - Specify parameters (path, query, body)
   - Define responses with status codes
   - Add to appropriate tag

4. **Register the endpoint** in `emulator_api.h`:
   - Add to appropriate region (see region markers)
   - Include implementation file comment

5. **Test with Swagger UI** to verify documentation accuracy

6. **Update HTML documentation** if needed

### When Changing Parameters/Responses:

1. **Modify the API endpoint** implementation in modular file
2. **Update corresponding OpenAPI paths** in `openapi_spec.cpp`
3. **Verify parameter schemas** match actual implementation
4. **Update response schemas** if response format changed
5. **Test with real API calls**

## OpenAPI Structure

The specification in `openapi_spec.cpp` includes:

- **Info section**: API title, description, version
- **Servers**: Available server URLs (localhost:8090)
- **Paths**: All 52 API endpoints with methods, parameters, responses
- **Components**: 17 reusable schemas for requests/responses
- **Tags**: 10 categories grouping related endpoints

## Current Endpoint Coverage

### Emulator Management (8 endpoints) ✅
- List, status, models, create, get, delete

### Emulator Control (5 endpoints) ✅
- Start, stop, pause, resume, reset

### Settings Management (3 endpoints) ✅
- Get all settings, get specific setting, update setting

### Tape Control (6 endpoints) ✅
- Load, eject, play, stop, rewind, info

### Disk Control (3 endpoints) ✅
- Insert, eject, info

### Snapshot Control (2 endpoints) ✅
- Load, info

### Memory State (3 endpoints) ✅
- Overview, RAM details, ROM details

### Screen State (3 endpoints) ✅
- Overview, mode, flash state

### Audio State (12 endpoints) ✅
- AY chips (with ID): overview, chip details, register details
- Extended audio (with ID): beeper, GS, Covox, channels
- Stateless variants: all audio endpoints without ID requirement

### Special Endpoints (7 endpoints) ✅  
- Root redirect, CORS preflight, OpenAPI spec

## Response Schemas

All response types have proper schemas defined:

- `EmulatorList`, `EmulatorInfo`
- `SettingsResponse`
- `MemoryStateResponse`, `RAMStateResponse`, `ROMStateResponse`
- `ScreenStateResponse`, `ScreenModeResponse`, `FlashStateResponse`
- `AYChipsResponse`, `AYChipResponse`, `AYRegisterResponse`
- `BeeperStateResponse`, `GSStateResponse`, `CovoxStateResponse`, `AudioChannelsResponse`
- `TapeInfoResponse`, `DiskInfoResponse`, `SnapshotInfoResponse`

## Common Mistakes to Avoid

❌ **Adding new endpoint without updating OpenAPI spec**  
❌ **Changing parameter names/types without updating schemas**  
❌ **Modifying response formats without updating response schemas**  
❌ **Forgetting to update path parameters or query parameters**  
❌ **Not adding autodoc comments to new methods**  
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
# 5. Check that all 52 endpoints are visible
```

## Region Markers in Header

The `emulator_api.h` file uses region markers for code organization:

```cpp
// region Root and OpenAPI (implementation: emulator_api.cpp)
// endregion Root and OpenAPI

// region Lifecycle Management (implementation: api/lifecycle_api.cpp)
// endregion Lifecycle Management

// region Tape/Disk/Snapshot Control (implementation: api/tape_disk_api.cpp and api/snapshot_api.cpp)
// endregion Tape/Disk/Snapshot Control

// region Settings Management (implementation: api/settings_api.cpp)
// endregion Settings Management

// region Memory State (implementation: api/state_memory_api.cpp)
// endregion Memory State

// region Screen State (implementation: api/state_screen_api.cpp)
// endregion Screen State

// region Audio State (implementation: api/state_audio_api.cpp)
// endregion Audio State
```

These enable code folding in IDEs and make navigation easier.

## Future Considerations

- **Auto-generation tooling** could be added in the future
- **Runtime validation** against OpenAPI spec could be implemented
- **API versioning** might require separate OpenAPI specs
- **Automated testing** of OpenAPI spec against actual endpoints

## Files to Update

When making API changes, update these files:

1. **Modular implementation file** (e.g., `src/api/lifecycle_api.cpp`)
2. **`src/openapi_spec.cpp`** - OpenAPI specification
3. **`src/emulator_api.h`** - Add method declaration in appropriate region
4. **`resources/html/index.html`** - HTML documentation (if needed)
5. **`DEPLOYMENT.md`** - Deployment guide (if new features added)
6. **This file** (`OPENAPI_MAINTENANCE.md`) - Update endpoint count/coverage

## Quick Command Reference

```bash
# View current line counts
wc -l src/emulator_api.cpp src/openapi_spec.cpp src/api/*.cpp

# Check OpenAPI endpoint count
curl -s http://localhost:8090/api/v1/openapi.json | jq '.paths | keys | length'

# List all documented endpoints
curl -s http://localhost:8090/api/v1/openapi.json | jq '.paths | keys'

# Verify all methods have CORS headers
grep -r "addCorsHeaders" src/api/*.cpp | wc -l
```

## Contact

If you're unsure about updating the OpenAPI specification, please consult the development team to ensure API documentation remains accurate and complete.
