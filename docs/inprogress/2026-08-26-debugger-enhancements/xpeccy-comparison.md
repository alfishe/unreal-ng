# Feature Comparison: xpeccy-plus vs unreal-qt Debugger

## Breakpoint System

### xpeccy-plus Breakpoint Types

```cpp
// From xcore/xcore.h
enum {
    BRK_CPUADR,   // CPU address space (0000-FFFF)
    BRK_MEMRAM,   // Physical RAM (page:offset)
    BRK_MEMROM,   // Physical ROM (page:offset)
    BRK_MEMSLT,   // Slot memory
    BRK_MEMEXT,   // External memory
    BRK_IOPORT,   // I/O port with mask
    BRK_IRQ,      // Interrupt trigger
    BRK_COND,     // Global condition (no address)
};

struct xBrkPoint {
    unsigned off:1;      // Disabled flag
    unsigned fetch:1;    // Break on execute
    unsigned read:1;     // Break on read
    unsigned write:1;    // Break on write
    unsigned temp:1;     // Temporary breakpoint
    unsigned last:1;     // Last condition value (for edge detection)
    unsigned fired:1;    // Fired this instruction
    unsigned onchg:1;    // Fire on false->true edge only
    
    int type;            // BRK_* type
    int adr;             // Start address
    int eadr;            // End address (for ranges)
    int mask;            // Port mask (for IO)
    int hits;            // Hit count
    int count;           // Fire count (after condition passes)
    int action;          // BRK_ACT_DBG | BRK_ACT_SCR | BRK_ACT_COUNT
    
    std::string cond;    // Condition expression text
    xExpr script;        // Compiled condition
};
```

### unreal-qt Breakpoint Types

```cpp
// From core/src/debugger/breakpoints/breakpointmanager.h
enum BreakpointTypeEnum : uint8_t {
    BRK_MEMORY = 0,   // Memory access
    BRK_IO,           // I/O port
    BRK_KEYBOARD,     // Key press
};

struct BreakpointDescriptor {
    uint16_t breakpointID;
    BreakpointTypeEnum type;
    BreakpointAddressMatchEnum matchType;  // Z80 addr or bank+addr
    
    uint8_t memoryType;  // execute | read | write
    uint8_t ioType;      // in | out
    
    uint16_t z80address;
    uint8_t page;
    MemoryBankModeEnum pageType;
    uint16_t bankOffset;
    
    bool active;
    std::string owner;
    std::string note;
    std::string group;
};
```

### Gap Analysis

| Feature | xpeccy-plus | unreal-qt | Notes |
|---------|-------------|-----------|-------|
| Address ranges | `adr` to `eadr` | Single address only | Need range support |
| Port mask | `mask` field | No | Allows partial port matching |
| IRQ breakpoint | `BRK_IRQ` | Not supported | Useful for interrupt debugging |
| Global conditions | `BRK_COND` | Not supported | Condition without address |
| Hit counter | `hits`, `count` | None | Essential for "break after N" |
| Skip count | Via condition | None | Break every Nth hit |
| Condition | `cond` + `script` | None | **Critical missing feature** |
| Edge detection | `onchg` flag | None | Fire on value change only |
| Actions | DBG/SCR/COUNT | Break only | Screenshot, count-only modes |
| Temp breakpoint | `temp` flag | Via group | Step-over implementation |

## Expression Evaluator

### xpeccy-plus Expression System

Location: `src/xcore/xexpr.cpp`, `src/xcore/xexpr.h`

**Features:**
- C-like syntax
- RPN-based evaluation (efficient, no recursion)
- Full arithmetic: `+ - * / %`
- Bitwise: `& | ^ ~ << >>`
- Comparison: `< > <= >= == !=`
- Logical: `&& || !`
- Memory access: `M(addr)` for byte, `[addr]` for word
- Pointer arithmetic: `addr->offset` = `M(addr + offset)`

**Pseudo-variables:**

| Variable | Description |
|----------|-------------|
| `RD` | Last memory read address |
| `WR` | Last memory write address |
| `MDT` | Last memory read/write data |
| `IN` | Last IN port |
| `OUT` | Last OUT port |
| `VAL` | Last IN/OUT data |
| `DOS` | TR-DOS ROM active |
| `SLOT0`-`SLOT3` | Page number in each 16K window |
| `FRAME` | Frame counter since reset |
| `RAYX`, `RAYY` | Current beam position |
| `HITS` | This breakpoint's hit count |

**Special functions:**
- `RAY(x, y)` - True if beam crossed this pixel during current instruction

**Number formats:**
- Decimal: `1234`
- Hex: `0x1234`, `#1234`, `$1234`
- Octal: `01234`
- Char: `'A'`

### unreal-qt Expression System

**Current state:** None

**Required implementation:**
1. Expression compiler (xexpr-compatible syntax)
2. Integration with CPU state access
3. Integration with memory subsystem for M()/[] operators
4. Pseudo-variable support tied to emulator events

## Port Watch

### xpeccy-plus Implementation

Location: `src/xgui/portwatch.cpp`, `src/xgui/portwatch.h`

- Configurable list of ports to display
- Shows port number, current value, read/write indicators
- Updates in real-time during single-stepping

### unreal-qt

**Current state:** None

**Proposed:** New `PortWatchWidget` in `unreal-qt/src/debugger/widgets/`

## Memory Watcher

### xpeccy-plus Implementation

Location: `src/watcher.cpp`, `src/watcher.h`

- Expression-based address specification
- Multiple watch entries
- Types: CPU addr, RAM addr, ROM addr
- Shows 13 bytes from each address
- Uses xexpr for address calculation

### unreal-qt

**Current state:** Memory view widget exists but no expression-based watcher

**Proposed:** New `MemoryWatcherWidget` with expression support

## Hardware Visualization

### xpeccy-plus Widgets

Found in `src/xgui/debuga/`:
- `dbg_ayym.cpp` - AY/YM sound chip
- `dbg_cmos_dump.cpp` - CMOS memory
- `dbg_diskdump.cpp` - Disk sector view
- `dbg_dump.cpp` - Memory dump
- `dbg_fdd.cpp` - Floppy disk controller
- `dbg_finder.cpp` - Memory search
- `dbg_gameboy.cpp` - Game Boy registers
- `dbg_memfill.cpp` - Memory fill tool
- `dbg_memmap.cpp` - Memory map visualization
- `dbg_nesppu.cpp` - NES PPU
- `dbg_palette.cpp` - Palette viewer
- `dbg_rdump.cpp` - Register dump
- `dbg_sprscan.cpp` - Sprite scanner
- `dbg_tape.cpp` - Tape visualization
- `dbg_zxscr.cpp` - ZX screen attribute overlay

### unreal-qt Widgets

Found in `unreal-qt/src/debugger/widgets/`:
- Memory view (basic)
- Registers view
- Disassembler
- Stack view
- Debug visualization window

## Heat Map Feature

### xpeccy-plus

- Tracks execution frequency per address
- Visual overlay on disassembly
- Export capability
- Reset function

### unreal-qt

**Current state:** None

**Proposed:** Add to `DebugVisualizationWindow` or as new widget
