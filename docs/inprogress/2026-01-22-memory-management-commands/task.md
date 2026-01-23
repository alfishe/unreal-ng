# Memory Commands Task

## Current Status
Documentation phase - updating `command-interface.md`.

## Design Summary
Unified `memory` command with two addressing modes:
- **Z80 flavor**: `memory read/write <addr>` 
- **Page flavor**: `memory read/write <type> <page> <offset>`
- **File I/O**: `memory save/load bank <N> <file>` or `memory save/load <type> <page> <file>`

---

## Task Checklist

### Phase 1: Documentation
- [x] Create implementation plan
- [x] Refactor to unified `memory` command
- [x] Add save/load file operations
- [x] Reorder parameters (target first, file last)
- [/] Update `command-interface.md`

### Phase 2: CLI Implementation
- [ ] Unified `HandleMemory()` with subcommand dispatch:
  - [ ] `memory read <addr> [len]` - Z80 read
  - [ ] `memory write <addr> <bytes...>` - Z80 write  
  - [ ] `memory read <type> <page> <offset> [len]` - Page read
  - [ ] `memory write <type> <page> <offset> <bytes...> [--force]` - Page write
  - [ ] `memory dump <type> <page>` - Dump page to stdout
  - [ ] `memory save bank <N> <file>` - Save Z80 bank
  - [ ] `memory save <type> <page> <file>` - Save physical page
  - [ ] `memory load bank <N> <file>` - Load to Z80 bank
  - [ ] `memory load <type> <page> <file> [--force]` - Load to physical page
  - [ ] `memory fill <type> <page> <offset> <len> <byte>` - Fill region
  - [ ] `memory info` - Show config

### Phase 3: WebAPI Implementation
- [ ] `GET/PUT /api/v1/emulator/{id}/memory/{addr}` - Z80 access
- [ ] `GET/PUT /api/v1/emulator/{id}/memory/{type}/{page}/{offset}` - Page access
- [ ] `GET /api/v1/emulator/{id}/memory/{type}/{page}` - Dump page
- [ ] `POST /api/v1/emulator/{id}/memory/save` - Save to file
- [ ] `POST /api/v1/emulator/{id}/memory/load` - Load from file
- [ ] `GET /api/v1/emulator/{id}/memory/info` - Config
- [ ] Dual encoding (hex pairs + base64)
- [ ] Update OpenAPI spec

### Phase 4: Python Bindings
- [ ] `memory_read(address, length)` / `memory_read(type, page, offset, length)`
- [ ] `memory_write(address, data)` / `memory_write(type, page, offset, data, force)`
- [ ] `memory_save(target, file)` / `memory_load(target, file, force)`
- [ ] `memory_info()`

### Phase 5: Lua Bindings
- [ ] Same API as Python with Lua conventions

### Phase 6: Testing & Documentation
- [ ] Unit tests
- [ ] Interface-specific docs
- [ ] Walkthrough

---

## File References
- Implementation plan: `docs/inprogress/2026-01-22-memory-management-commands/implementation_plan.md`
- Target spec: `docs/emulator/design/control-interfaces/command-interface.md`
- CLI handler: `core/automation/cli/src/commands/cli-processor-memory.cpp`
