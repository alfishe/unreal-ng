# Memory Read/Write Commands Implementation Plan

## Goal
Add comprehensive memory access commands to the ECI, supporting:
1. **Z80 address space** (0x0000-0xFFFF) - uses current memory mapping
2. **Explicit page addressing** - specify RAM/ROM page directly
3. **ROM write protection** - by default disabled, can be explicitly removed for automation

## Existing Commands
- `memory <addr> [len]` (aliases: `mem`, `m`) - Already exists for reading Z80 address space hex dump

## Proposed Changes

### 1. Documentation Changes
**File**: [command-interface.md](docs/emulator/design/control-interfaces/command-interface.md)

Add to Section "3. State Inspection" (around line 412):

#### Z80 Address Space Commands
| Command | Aliases | Arguments | Description |
|:---|:---|:---|:---|
| `memory <addr> [len]` | `mem`, `m` | `<address> [length]` | (Exists) Display hex dump of memory at address. Default 128 bytes. |
| `memory write <addr> <bytes>` | `memw`, `mw` | `<address> <hex-bytes>` | Write bytes to Z80 address space. Example: `mw 0x5000 FF 00 C3` |
| `memory write <addr> "<text>"` | | `<address> "<string>"` | Write ASCII text to address. Example: `mw 0x5000 "HELLO"` |
| `memory fill <start> <end> <byte>` | `memfill` | `<start> <end> <value>` | Fill memory range with byte value. |

#### Explicit Page Addressing Commands
| Command | Aliases | Arguments | Description |
|:---|:---|:---|:---|
| `page read <type> <page> <offset> [len]` | [pr](core/src/debugger/analyzers/basic-lang/basicencoder.cpp#331-345) | `<ram\|rom> <page#> <offset> [length]` | Read from specific RAM/ROM page, bypassing current mapping. |
| `page write <type> <page> <offset> <bytes>` | `pw` | `<ram\|rom> <page#> <offset> <hex-bytes>` | Write to specific page. ROM writes require protection removal. |
| `rom protect <on\|off>` | | [on](core/src/emulator/platform.h#926-932) or `off` | Enable/disable ROM write protection for all ROM pages. Default: ON (protected). |

---

### 2. Implementation Changes

#### [NEW] `cli-processor-memory.cpp`
New command handler file for memory commands:
- [HandleMemory()](core/src/debugger/breakpoints/breakpointmanager.cpp#987-1010) - existing, needs write subcommand
- [HandleMemoryWrite()](core/src/debugger/breakpoints/breakpointmanager.cpp#1011-1034) - write to Z80 address space  
- `HandlePageRead()` - read from explicit page
- `HandlePageWrite()` - write to explicit page
- `HandlePageProtect()` - toggle ROM protection

#### [MODIFY] [Memory](core/src/emulator/cpu/core.h#81-85) class methods used
- `DirectReadFromZ80Memory(uint16_t addr)` - existing
- `DirectWriteToZ80Memory(uint16_t addr, uint8_t val)` - existing
- `ReadRAMPage(uint8_t page, uint16_t offset)` - may need to add
- `WriteRAMPage(uint8_t page, uint16_t offset, uint8_t val)` - may need to add
- `ReadROMPage(uint8_t page, uint16_t offset)` - may need to add
- `WriteROMPage(uint8_t page, uint16_t offset, uint8_t val)` - needs ROM protection check

---

### 3. Memory Class API Additions

```cpp
// In memory.h
// Explicit page access (bypass current paging)
uint8_t ReadRAMPage(uint8_t page, uint16_t offset);
void WriteRAMPage(uint8_t page, uint16_t offset, uint8_t value);
uint8_t ReadROMPage(uint8_t page, uint16_t offset);
void WriteROMPage(uint8_t page, uint16_t offset, uint8_t value);

// ROM protection control
bool IsROMWriteProtected() const;
void SetROMWriteProtection(bool enabled);
```

---

## Verification Plan

### Manual Testing (CLI)
1. Start emulator with 128K model
2. Test Z80 address space read:
   ```
   memory 0x5C00 32
   ```
3. Test Z80 address space write:
   ```
   memory write 0x8000 41 42 43
   memory 0x8000 16
   ```
   Expected: Shows "ABC" at 0x8000

4. Test explicit page read:
   ```
   page read ram 0 0 32
   page read rom 0 0 32
   ```

5. Test ROM write protection:
   ```
   page write rom 0 0 00   # Should fail (protected)
   rom protect off
   page write rom 0 0 00   # Should succeed
   rom protect on
   ```

---

## User Review Required

> [!IMPORTANT]
> This plan adds memory write capabilities which could corrupt running programs. Should we add a safety confirmation for writes to critical areas (system variables, ROM)?

**Questions:**
1. Should ROM writes be allowed at all, or only in a special "debug" mode?
2. Should there be address range restrictions (e.g., prevent writes to 0x0000-0x3FFF unless explicitly confirmed)?
3. What page numbering should be used for clones with extended memory (Pentagon 512K, etc.)?
