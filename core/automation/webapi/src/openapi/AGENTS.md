# OpenAPI Specification Maintenance Guidelines

## Overview

This directory contains modular OpenAPI 3.0 specification fragments for the WebAPI. Each `.inc` file is included into `openapi_spec.cpp` at build time to generate the complete OpenAPI JSON spec served at `/api/v1/openapi.json`.

## File Organization

### Naming Convention
- Files use `openapi_<domain>.inc` naming
- Domain names should be lowercase, single-word or hyphenated
- Examples: `openapi_debug.inc`, `openapi_labels.inc`, `openapi_port-trace.inc`

### Current Structure
```
openapi/
├── AGENTS.md              # This file
├── openapi_analyzers.inc  # Analyzer management
├── openapi_basic.inc      # BASIC interpreter control
├── openapi_breakpoints.inc # Breakpoint management
├── openapi_capture.inc    # Screen/OCR capture
├── openapi_debug.inc      # Memory, registers, disassembly
├── openapi_features.inc   # Runtime feature toggles
├── openapi_interpreter.inc # CLI/Lua/Python interpreters
├── openapi_keyboard.inc   # Keyboard injection
├── openapi_labels.inc     # Labels/symbols management
├── openapi_lifecycle.inc  # Emulator create/start/stop
├── openapi_porttrace.inc  # I/O port tracing
├── openapi_profiler.inc   # Opcode/memory profiling
├── openapi_schemas.inc    # Shared JSON schemas
├── openapi_settings.inc   # Configuration settings
├── openapi_state.inc      # State inspection (screen, audio)
├── openapi_stepping.inc   # Execution control (step, run)
└── openapi_ttd.inc        # Time-Travel Debug
```

## Size Limits and Splitting Rules

### When to Split
- **Target**: Each `.inc` file should be **100-200 lines**
- **Maximum**: Split when file exceeds **300 lines**
- **Minimum**: Don't create files smaller than **50 lines** unless logically distinct

### How to Split
1. Identify logical groupings within the file
2. Create new file with appropriate domain name
3. Move related endpoints to new file
4. Update `openapi_spec.cpp` includes (maintain alphabetical order within groups)
5. Build and verify OpenAPI spec renders correctly

### Split Examples
```
openapi_debug.inc (was 530 lines) → split into:
├── openapi_stepping.inc    (~120 lines) - step, run_*, debugmode
├── openapi_breakpoints.inc (~80 lines)  - bp add/remove/enable
├── openapi_debug.inc       (~200 lines) - registers, memory, disasm
└── openapi_labels.inc      (~130 lines) - labels, symbols
```

## Include Order in openapi_spec.cpp

Includes are grouped by category:
```cpp
// Lifecycle & Core
#include "openapi/openapi_lifecycle.inc"
#include "openapi/openapi_basic.inc"

// Media
#include "openapi/openapi_tape.inc"      // if exists
#include "openapi/openapi_disk.inc"      // if exists
#include "openapi/openapi_snapshot.inc"  // if exists

// Configuration
#include "openapi/openapi_settings.inc"
#include "openapi/openapi_features.inc"

// State & Inspection
#include "openapi/openapi_state.inc"
#include "openapi/openapi_analyzers.inc"

// Debug Commands
#include "openapi/openapi_stepping.inc"
#include "openapi/openapi_breakpoints.inc"
#include "openapi/openapi_debug.inc"
#include "openapi/openapi_labels.inc"

// Profiling
#include "openapi/openapi_profiler.inc"
#include "openapi/openapi_porttrace.inc"

// Advanced
#include "openapi/openapi_ttd.inc"
#include "openapi/openapi_keyboard.inc"
#include "openapi/openapi_capture.inc"
#include "openapi/openapi_interpreter.inc"

// Schemas (always last)
#include "openapi/openapi_schemas.inc"
```

## Writing Endpoint Specs

### Required Fields
Every endpoint must have:
- `summary` - Short description (< 60 chars)
- `tags` - Category for Swagger UI grouping
- `parameters` - Path/query params with name, in, required, schema
- `responses` - At minimum 200, plus error codes if applicable

### Template
```cpp
paths["/api/v1/emulator/{id}/example"]["get"]["summary"] = "Short description";
paths["/api/v1/emulator/{id}/example"]["get"]["tags"].append("Debug Commands");
paths["/api/v1/emulator/{id}/example"]["get"]["parameters"][0]["name"] = "id";
paths["/api/v1/emulator/{id}/example"]["get"]["parameters"][0]["in"] = "path";
paths["/api/v1/emulator/{id}/example"]["get"]["parameters"][0]["required"] = true;
paths["/api/v1/emulator/{id}/example"]["get"]["parameters"][0]["schema"]["type"] = "string";
paths["/api/v1/emulator/{id}/example"]["get"]["responses"]["200"]["description"] = "Success response";
```

### Available Tags
Use existing tags from `openapi_spec.cpp`:
- `Emulator Management`
- `Emulator Control`
- `Settings Management`
- `Feature Management`
- `Tape Control`
- `Disk Control`
- `Disk Inspection`
- `Snapshot Control`
- `Debug Commands`
- `Profiler Commands`
- `Time-Travel Debug`
- `Keyboard Injection`
- `Analyzers`
- `Interpreter`
- `Capture`

## Validation

After modifying any `.inc` file:

1. **Build**: `ninja -C cmake-build-release`
2. **Run emulator** and fetch spec: `curl http://localhost:8090/api/v1/openapi.json | jq .`
3. **Verify** new endpoints appear in Swagger UI
4. **Test** actual endpoint functionality matches spec

## Cross-Platform Notes

- Files are plain C++ code fragments (no preprocessor guards needed)
- Use 4-space indentation (matches project style)
- No trailing whitespace
- UTF-8 encoding
- LF line endings (not CRLF)
