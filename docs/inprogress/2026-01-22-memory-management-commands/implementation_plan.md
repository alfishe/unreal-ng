# Memory Commands Specification

## Overview

This plan adds comprehensive memory read/write command specifications to the ECI (Emulator Control Interface). The design uses a unified `memory` command with two addressing modes:

1. **Z80 Address Space** - Virtual 64KB memory through current bank configuration
2. **Physical Page Access** - Direct access to RAM/ROM/Cache/Misc pages with type enumeration

---

## Command Summary

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `memory read <addr> [len]` | Z80 address | Read from Z80 virtual address space |
| `memory write <addr> <bytes...>` | Z80 address + data | Write to Z80 virtual address space |
| `memory read <type> <page> <offset> [len]` | type + page + offset | Read from physical page |
| `memory write <type> <page> <offset> <bytes...>` | type + page + offset + data | Write to physical page |
| `memory dump <type> <page>` | type + page | Dump entire 16KB page to stdout |
| `memory save bank <N> <file>` | bank 0-3 + file | Save Z80 bank (16KB) to file |
| `memory save <type> <page> <file>` | type + page + file | Save physical page (16KB) to file |
| `memory load bank <N> <file>` | bank 0-3 + file | Load file into Z80 bank |
| `memory load <type> <page> <file> [--force]` | type + page + file | Load file into physical page |
| `memory fill <type> <page> <offset> <len> <byte>` | type + page + offset + fill params | Fill region with byte |
| `memory info` | | Show memory configuration |

**Page Types**: `ram` | `rom` | `cache` | `misc`

---

## Z80 Address Space Commands

### `memory read <addr> [len]`

Read from Z80 virtual 64KB address space using current bank configuration.

**Aliases**: `mem`, `m`

```bash
> memory read 0x5C00
5C00: 00 00 C3 CB 11 FF 00 00 | ........ 
5C10: F7 C9 FE 06 38 11 A9 6F | ....8..o

> memory read 0x5C00 32
> m $8000 16
```

**Implementation**: `Memory::DirectReadFromZ80Memory()` - respects current bank configuration.

---

### `memory write <addr> <bytes...>`

Write to Z80 virtual address space.

```bash
> memory write 0x5000 FF 00 C3
Wrote 3 bytes at 0x5000

> memory write 0x8000 00 00 00 00 C9
Wrote 5 bytes at 0x8000
```

**Implementation**: `Memory::DirectWriteToZ80Memory()` - ROM writes silently ignored (hardware behavior).

---

## Physical Page Commands

### `memory read <type> <page> <offset> [len]`

Read from physical memory page, bypassing Z80 bank configuration.

**Page Types**:
| Type | Pages | Size | Description |
| :--- | :--- | :--- | :--- |
| `ram` | 0-255 | 4MB max | Main RAM (model-dependent) |
| `rom` | 0-3 | 64KB | ROM: 0=48K, 1=128K, 2=TR-DOS, 3=System |
| `cache` | 0-15 | 256KB | Cache RAM (ZX-Evo/TSConf only) |
| `misc` | 0-15 | 256KB | Misc pages (ZX-Evo/TSConf only) |

```bash
# Read 256 bytes from RAM page 5 at offset 0
> memory read ram 5 0 256

# Read TR-DOS ROM signature
> memory read rom 2 0 16

# Read screen attributes from page 5
> memory read ram 5 0x1800 768
```

**Implementation**: 
- RAM: `Memory::RAMPageAddress(page) + offset`
- ROM: `Memory::ROMPageHostAddress(page) + offset`

---

### `memory write <type> <page> <offset> <bytes...> [--force]`

Write to physical memory page.

```bash
# Write to RAM page
> memory write ram 0 0x1000 C3 00 80
Wrote 3 bytes to RAM page 0 at offset 0x1000

# Write to ROM (requires --force)
> memory write rom 0 0x0000 F3 --force
Wrote 1 byte to ROM page 0 at offset 0x0000

# Without --force on ROM produces error
> memory write rom 0 0x0000 F3
Error: ROM write requires --force flag
```

**Implementation Notes**:
- ROM writes require `--force` flag
- Modifies in-memory buffer only (not ROM file on disk)
- Changes persist until reset/reload

---

### `memory dump <type> <page>`

Dump entire 16KB page as hex to stdout.

```bash
> memory dump ram 5 > screen_memory.hex
> memory dump rom 2 > trdos_rom.hex
```

---

### `memory save bank <N> <file>` / `memory save <type> <page> <file>`

Save memory to binary file.

**Z80 Bank Save** (saves current mapped content):
```bash
# Save Z80 bank 0 (0x0000-0x3FFF) - may be ROM or RAM depending on config
> memory save bank 0 bank0.bin
Saved 16384 bytes to bank0.bin

# Save all 4 banks (full 64KB Z80 address space)
> memory save bank 1 bank1.bin
> memory save bank 2 bank2.bin
> memory save bank 3 bank3.bin
```

**Physical Page Save** (saves specific page regardless of mapping):
```bash
# Save RAM page 5 (screen memory)
> memory save ram 5 screen.bin
Saved 16384 bytes to screen.bin

# Save TR-DOS ROM
> memory save rom 2 trdos.bin
Saved 16384 bytes to trdos.bin
```

---

### `memory load bank <N> <file>` / `memory load <type> <page> <file> [--force]`

Load binary file into memory.

**Z80 Bank Load**:
```bash
# Load file into Z80 bank 2 (0x8000-0xBFFF)
> memory load bank 2 code.bin
Loaded 16384 bytes into bank 2

# Partial load (file smaller than 16KB)
> memory load bank 3 small.bin
Loaded 4096 bytes into bank 3 (padded with zeros)
```

**Physical Page Load**:
```bash
# Load into RAM page
> memory load ram 5 screen.bin
Loaded 16384 bytes into RAM page 5

# Load into ROM (requires --force)
> memory load rom 0 custom_rom.bin --force
Loaded 16384 bytes into ROM page 0

# Without --force on ROM produces error
> memory load rom 0 custom_rom.bin
Error: ROM load requires --force flag
```

**Implementation Notes**:
- File size validated (max 16KB per page)
- Files smaller than 16KB load from offset 0, remaining bytes unchanged
- ROM loads require `--force` flag
- Binary format (raw bytes, no headers)

---

### `memory fill <type> <page> <offset> <len> <byte>`

Fill region with byte value.

```bash
> memory fill ram 5 0x1800 768 0x38
Filled 768 bytes with 0x38 in RAM page 5 at offset 0x1800
```

---

### `memory info`

Show memory configuration for current model.

```bash
> memory info
Memory Configuration (Pentagon 128):
  RAM:   pages 0-15 (256KB)
  ROM:   pages 0-3  (64KB)
  Cache: not available
  Misc:  not available

Current Z80 Bank Mapping:
  Bank 0 (0x0000-0x3FFF): ROM page 0 (48K BASIC)
  Bank 1 (0x4000-0x7FFF): RAM page 5 (contended)
  Bank 2 (0x8000-0xBFFF): RAM page 2
  Bank 3 (0xC000-0xFFFF): RAM page 0
```

---

## Address Formats

All commands support multiple address formats:

| Format | Example | Description |
| :--- | :--- | :--- |
| `0x...` | `0x8000` | Hexadecimal (C-style) |
| `$...` | `$8000` | Hexadecimal (assembly) |
| `#...` | `#8000` | Hexadecimal (ZX Spectrum) |
| Decimal | `32768` | Plain decimal |

---

## Interface Mapping

### CLI Implementation

Single unified handler with subcommand dispatch:

| Command | Handler |
| :--- | :--- |
| `memory` | `CLIProcessor::HandleMemory()` |

**CLI Output Format**: Hex-only (`XX XX XX ...`) for human readability.

---

### WebAPI Endpoints

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/api/v1/emulator/{id}/memory/{addr}` | GET | Read Z80 address |
| `/api/v1/emulator/{id}/memory/{addr}` | PUT | Write Z80 address |
| `/api/v1/emulator/{id}/memory/{type}/{page}/{offset}` | GET | Read physical page |
| `/api/v1/emulator/{id}/memory/{type}/{page}/{offset}` | PUT | Write physical page |
| `/api/v1/emulator/{id}/memory/{type}/{page}` | GET | Dump entire page |
| `/api/v1/emulator/{id}/memory/info` | GET | Get memory config |

#### Encoding Formats

Dual encoding for requests and responses:

| Format | Content-Type | Description |
| :--- | :--- | :--- |
| **Base64** | `application/octet-stream` | Binary data (large transfers) |
| **Hex Pairs** | `application/json` | Space-separated `XX XX XX` (default) |

**Content Negotiation**:
- `Accept: application/octet-stream` → Base64 response
- `Accept: application/json` → Hex pairs (default)

```json
// Hex pairs (default)
{"format": "hex", "data": "FF 00 C3 21 00 80"}

// Base64
{"format": "base64", "data": "QUJDREVGRw=="}
```

---

### Python Bindings

**Data Format**: Native `bytes` objects (no encoding - direct memory access).

```python
class Emulator:
    # Z80 Address Space
    def memory_read(self, address: int, length: int = 128) -> bytes:
        """Read from Z80 address space"""
        
    def memory_write(self, address: int, data: bytes) -> int:
        """Write to Z80 address space"""
    
    # Physical Page Access
    def memory_read(self, type: str, page: int, offset: int, length: int = 128) -> bytes:
        """Read from physical page. type: 'ram'|'rom'|'cache'|'misc'"""
        
    def memory_write(self, type: str, page: int, offset: int, data: bytes, force: bool = False) -> int:
        """Write to physical page. ROM requires force=True"""
    
    def memory_info(self) -> dict:
        """Get memory configuration"""
```

---

### Lua Bindings

**Data Format**: Tables of integers `{0xFF, 0x00, 0xC3, ...}`.

```lua
-- Z80 Address Space
emu:memory_read(address, length)
emu:memory_write(address, {...})

-- Physical Page Access  
emu:memory_read(type, page, offset, length)
emu:memory_write(type, page, offset, {...}, force)
emu:memory_info()
```

---

## Memory Layout Reference

```
Physical Memory Layout (4MB System):
┌────────────────────────────────────────────────────────────┐
│  RAM Pages 0-255       (4MB, pages 0x000-0x0FF)            │
├────────────────────────────────────────────────────────────┤
│  Cache Pages 0-15      (256KB, pages 0x100-0x10F)          │
├────────────────────────────────────────────────────────────┤
│  Misc Pages 0-15       (256KB, pages 0x110-0x11F)          │
├────────────────────────────────────────────────────────────┤
│  ROM Pages 0-3         (64KB, pages 0x120-0x123)           │
│    0: 48K BASIC (SOS)  │  1: 128K ROM                      │
│    2: TR-DOS ROM       │  3: System/Service ROM            │
└────────────────────────────────────────────────────────────┘
```

---

## Implementation Status

- ✅ `memory read <addr>` - Implemented (CLI only)
- 🔧 `memory write <addr>` - **NEW**
- 🔧 `memory read <type> <page> <offset>` - **NEW**
- 🔧 `memory write <type> <page> <offset>` - **NEW**
- 🔧 `memory dump` - **NEW**
- 🔧 `memory fill` - **NEW**
- 🔧 `memory info` - **NEW**

---

## Use Cases

### 1. BASIC Variable Injection
```bash
memory write 0x5C8B 10 00
```

### 2. Direct Screen Manipulation
```bash
memory write ram 5 0x0000 FF FF FF FF FF FF FF FF
```

### 3. ROM Patching
```bash
memory write rom 0 0x0205 CF --force
```

### 4. Memory Comparison
```bash
memory dump ram 5 > before.hex
step 1000
memory dump ram 5 > after.hex
```

### 5. Python Code Injection
```python
emu.memory_write("ram", 3, 0x0000, my_loader_code)
```
