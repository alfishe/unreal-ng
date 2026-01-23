# Memory Commands Implementation Walkthrough

## Summary

Implemented unified memory access commands across all automation interfaces, providing consistent Z80 and physical page access for debugging, testing, and ROM/RAM patching scenarios.

---

## What Was Implemented

### CLI - Unified `memory` Command

New file: [cli-processor-memory.cpp](core/automation/cli/src/commands/cli-processor-memory.cpp)

**Subcommands:**
| Command | Description |
|---------|-------------|
| `memory read <addr> [len]` | Read from Z80 address space |
| `memory write <addr> <bytes...>` | Write to Z80 address space |
| `memory read <type> <page> <offset> [len]` | Read from physical page |
| `memory write <type> <page> <offset> <bytes...> [--force]` | Write to physical page |
| `memory dump <type> <page>` | Dump full 16KB page to stdout |
| `memory save bank <N> <file>` | Save Z80 bank to file |
| `memory save <type> <page> <file>` | Save physical page to file |
| `memory load bank <N> <file>` | Load file to Z80 bank |
| `memory load <type> <page> <file> [--force]` | Load file to physical page |
| `memory fill <type> <page> <offset> <len> <byte>` | Fill region with byte |
| `memory info` | Show memory configuration |

**Page Types:** `ram`, `rom`, `cache`, `misc`

---

### WebAPI - New Endpoints

Modified: [debug_api.cpp](core/automation/webapi/src/api/debug_api.cpp)

| Method | Endpoint | Description |
|--------|----------|-------------|
| `PUT` | `/api/v1/emulator/{id}/memory/{addr}` | Write to Z80 memory |
| `GET` | `/api/v1/emulator/{id}/memory/{type}/{page}/{offset}` | Read from physical page |
| `PUT` | `/api/v1/emulator/{id}/memory/{type}/{page}/{offset}` | Write to physical page |
| `GET` | `/api/v1/emulator/{id}/memory/info` | Get memory configuration |

**Request Body Format (PUT):**
```json
{
  "data": [0x00, 0x01, 0x02],  // OR
  "hex": "00 01 02",
  "force": true  // Required for ROM write
}
```

---

### Python Bindings

Modified: [python_emulator.h](core/automation/python/src/emulator/python_emulator.h)

**New Methods on Emulator class:**
```python
# Z80 address space
emu.mem_read_block(addr, len) -> bytes
emu.mem_write_block(addr, data: bytes)

# Physical page access  
emu.page_read(type, page, offset) -> int
emu.page_write(type, page, offset, value)
emu.page_read_block(type, page, offset, len) -> bytes
emu.page_write_block(type, page, offset, data: bytes)

# Configuration
emu.memory_info() -> dict
```

---

### Lua Bindings

Modified: [lua_emulator.h](core/automation/lua/src/emulator/lua_emulator.h)

**New Global Functions:**
```lua
-- Z80 address space
mem_read_block(addr, len) -> table
mem_write_block(addr, data_table)

-- Physical page access
page_read(type, page, offset) -> int
page_write(type, page, offset, value)
page_read_block(type, page, offset, len) -> table
page_write_block(type, page, offset, data_table)

-- Configuration
memory_info() -> table
```

---

## Use Cases

1. **ROM Patching** - Modify ROM at runtime for debugging
2. **RAM Analysis** - Inspect any RAM page regardless of Z80 paging
3. **Snapshot Manipulation** - Save/load memory regions for test fixtures
4. **Cache Inspection** - Debug SIMM/cache memory behavior
5. **Custom Loaders** - Load code directly to physical pages

---

## Files Changed

| File | Change |
|------|--------|
| [cli-processor-memory.cpp](core/automation/cli/src/commands/cli-processor-memory.cpp) | **NEW** - CLI memory command implementation |
| [cli-processor.cpp](core/automation/cli/src/cli-processor.cpp) | Added dispatch to HandleMemory() |
| [cli-processor.h](core/automation/cli/include/cli-processor.h) | Added HandleMemory() declaration |
| [emulator_api.h](core/automation/webapi/src/emulator_api.h) | Added new route declarations |
| [debug_api.cpp](core/automation/webapi/src/api/debug_api.cpp) | Added putMemory, getMemoryPage, putMemoryPage, getMemoryInfo |
| [openapi_spec.cpp](core/automation/webapi/src/openapi_spec.cpp) | Added OpenAPI definitions for new endpoints |
| [python_emulator.h](core/automation/python/src/emulator/python_emulator.h) | Added page access bindings |
| [lua_emulator.h](core/automation/lua/src/emulator/lua_emulator.h) | Added page access bindings |
| [command-interface.md](docs/emulator/design/control-interfaces/command-interface.md) | Documented unified memory command |
