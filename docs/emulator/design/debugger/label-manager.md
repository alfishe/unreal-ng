# Label Manager Design

> Symbolic debugging infrastructure for ZX Spectrum development

## Background

### The Problem

Debugging Z80 assembly without symbols is painful. Raw addresses like `CALL $8050` or `LD HL,$5C00` are meaningless without cross-referencing documentation or disassembly listings. Developers working with assemblers like sjasmplus, SJASM, Pasmo, or Z88DK generate symbol files that map addresses to meaningful names.

The LabelManager bridges this gap by:
1. Loading symbols from various assembler output formats
2. Providing fast bidirectional lookup (name ↔ address)
3. Supporting bank-aware symbols for 128K+ machines
4. Integrating with disassembler, breakpoints, and automation

### Historical Context

Early ZX Spectrum development used simple `.map` files with address-label pairs. As machines evolved (128K, Pentagon, Scorpion), bank switching introduced complexity - the same Z80 address could refer to different physical memory depending on which bank is paged in.

Modern assemblers like sjasmplus support Source Level Debugging (SLD) format that includes:
- Source file/line mappings
- Bank-aware symbols
- Code vs data distinction
- Module/segment grouping

## Architecture

### Position in the System

```
┌─────────────────────────────────────────────────────────────┐
│                      Debugger Subsystem                     │
├──────────────┬──────────────┬──────────────┬────────────────┤
│ Breakpoint   │ Disassembler │    Label     │   Analyzers    │
│   Manager    │              │   Manager    │                │
├──────────────┴──────────────┴──────────────┴────────────────┤
│                     EmulatorContext                         │
├─────────────────────────────────────────────────────────────┤
│                        Memory                               │
│  ┌─────────┬─────────┬─────────┬─────────┐                  │
│  │ Bank 0  │ Bank 1  │  ...    │ Bank N  │  ← RAM Pages     │
│  └─────────┴─────────┴─────────┴─────────┘                  │
│  ┌─────────┬─────────┐                                      │
│  │ ROM 0   │ ROM 1   │  ← ROM Pages                         │
│  └─────────┴─────────┘                                      │
└─────────────────────────────────────────────────────────────┘
```

### Integration Points

| Component | Integration |
|-----------|-------------|
| **Disassembler** | Resolves addresses to labels in output (`CALL main` instead of `CALL $8000`) |
| **BreakpointManager** | Symbolic breakpoints (`bp main` sets breakpoint at label address) |
| **DebuggerWindow** | Label editor UI, symbolic display in registers/memory views |
| **Automation** | CLI/WebAPI/Lua/Python access for scripted debugging |
| **Analyzers** | TR-DOS analyzer uses labels to annotate system calls |

### Data Flow

```
Symbol Files (.map, .sym, .sld)
         │
         ▼
    ┌─────────────┐
    │ LabelManager │◄──── Manual label entry (UI/CLI)
    └─────────────┘
         │
    ┌────┴────┐
    ▼         ▼
By Name    By Address
 Map         Map
    │         │
    └────┬────┘
         ▼
   Notifications
  (NC_LABEL_CHANGED)
         │
    ┌────┴────────┬──────────────┐
    ▼             ▼              ▼
Disassembler  Breakpoints   UI Refresh
```

## Label Structure

### Core Fields

```cpp
struct Label {
    std::string name;              // Unique identifier, case-sensitive
    uint16_t address;              // Z80 logical address (0x0000-0xFFFF)
    uint16_t bank;                 // Physical bank number
    uint16_t bankOffset;           // Offset within physical bank
    MemoryBankModeEnum bankType;   // BANK_RAM or BANK_ROM
    std::string type;              // Semantic classification
    std::string module;            // Grouping/namespace
    std::string comment;           // Human documentation
    bool active;                   // Enable/disable toggle
};
```

### Field Specifications

#### `name`
- **Required**: Yes
- **Uniqueness**: Must be unique within the label set
- **Case sensitivity**: Preserved (case-sensitive matching)
- **Valid characters**: Alphanumeric, underscore, dot (assembler-dependent)
- **Examples**: `main`, `SCORE`, `gfx.sprite_draw`, `_start`

#### `address`
- **Required**: Yes
- **Range**: 0x0000-0xFFFF (Z80 16-bit address space)
- **Meaning**: Logical address as seen by the Z80 CPU
- **Note**: Multiple labels can share the same address (aliases, overlays)

#### `bank` and `bankOffset`
- **Default**: `0xFFFF` (unspecified/any bank)
- **Range**: 0-254 for bank, 0x0000-0x3FFF for offset
- **Purpose**: Bank-aware debugging for 128K+ machines

**Bank-aware example:**
```
; Pentagon 128K memory layout when bank 5 is paged at 0xC000:
; Z80 address 0xC000 = physical RAM bank 5, offset 0x0000

Label: "shadow_screen"
  address: 0xC000
  bank: 5
  bankOffset: 0x0000
  bankType: BANK_RAM
```

**Bank-agnostic example:**
```
; ROM routine - always at same address regardless of banking
Label: "ROM_CLS"
  address: 0x0D6B
  bank: 0xFFFF (any)
  bankOffset: 0xFFFF (same as address)
  bankType: BANK_ROM
```

#### `bankType`
- **Values**: `BANK_RAM`, `BANK_ROM`
- **Auto-detection**: If address < 0x4000, defaults to `BANK_ROM`
- **Purpose**: Distinguishes RAM pages from ROM pages for same bank numbers

#### `type`
- **Common values**: `"code"`, `"data"`, `"const"`, `"var"`, `"struct"`, `"array"`
- **Purpose**: Semantic classification for filtering and display
- **Usage**: Disassembler can show data as DB/DW instead of instructions

#### `module`
- **Purpose**: Grouping labels by source file, segment, or logical unit
- **Examples**: `"MAIN"`, `"GFX"`, `"SOUND"`, `"game/player.asm"`
- **Usage**: Filter labels by module in large projects

#### `comment`
- **Purpose**: Human-readable documentation
- **Display**: Shown in label editor, can appear in disassembly output
- **Example**: `"Player score, BCD format, 3 bytes"`

#### `active`
- **Default**: `true`
- **Purpose**: Temporarily disable label without deleting
- **Usage**: Hide irrelevant labels, compare behavior with/without symbols

## Use Cases

### UC1: Loading Assembler Output

**Scenario**: Developer compiles ZX Spectrum game with sjasmplus, wants to debug with symbols.

```bash
# sjasmplus generates .sym file
sjasmplus game.asm --sym=game.sym

# In emulator CLI
symbols load game.sym
```

**Expected behavior**:
1. Auto-detect file format (SJASM in this case)
2. Parse all symbols with addresses
3. Extract module names if present
4. Post `NC_LABEL_CHANGED` notification
5. Disassembler immediately shows symbolic names

### UC2: Bank-Aware Debugging

**Scenario**: Developer debugging Pentagon 512K program with code in multiple banks.

```
; Code in bank 3
org $C000
bank_3_init:
    ld a, 3
    ret

; Code in bank 7  
org $C000
bank_7_init:
    ld a, 7
    ret
```

**Challenge**: Both routines are at Z80 address 0xC000.

**Solution with bank-aware labels**:
```cpp
AddLabel("bank_3_init", 0xC000, 3, 0x0000, "code", "BANKS");
AddLabel("bank_7_init", 0xC000, 7, 0x0000, "code", "BANKS");
```

**Behavior**:
- When bank 3 is paged: disassembly shows `bank_3_init`
- When bank 7 is paged: disassembly shows `bank_7_init`
- Breakpoint on `bank_3_init` only triggers when bank 3 is active

### UC3: Symbolic Breakpoints

**Scenario**: Set breakpoint on function entry without knowing address.

```
# Instead of:
bp 0x8050

# Use:
bp game_loop
```

**Expected behavior**:
1. Resolve `game_loop` to address via LabelManager
2. If label not found, report error
3. If found, set breakpoint at resolved address
4. When label address changes (recompile), breakpoint follows

### UC4: Automation Scripting

**Scenario**: Lua script that monitors game state using symbolic names.

```lua
-- Instead of magic numbers:
local score_addr = 0x5C00
local lives_addr = 0x5C03

-- Use labels:
local score_label = get_label("SCORE")
local lives_label = get_label("LIVES")

function check_game_state()
    local score = mem_read_word(score_label.address)
    local lives = mem_read(lives_label.address)
    print(string.format("Score: %d, Lives: %d", score, lives))
end
```

### UC5: Filtering Large Symbol Sets

**Scenario**: Game has 2000+ symbols, developer wants to see only graphics-related ones.

```
# CLI
labels --module GFX
labels --type data --module GFX

# WebAPI
GET /api/v1/emulator/{id}/labels?module=GFX&type=data

# Lua
local gfx_labels = get_labels({module = "GFX", type = "data"})
```

### UC6: Interactive Label Management

**Scenario**: During debugging, developer discovers undocumented routine.

```
# Add label on the fly
label add mystery_routine 0x9A50 --type code --comment "Called after level complete"

# Later, update with more info
label update mystery_routine --comment "Level transition handler, trashes BC"
```

## File Formats

### MAP Format (Linker Output)

```
; address  label
0031       NODSK
6D91       ERRL
A250       RD_SEC
A255       READLP
```

**Characteristics**:
- Simplest format
- No type/module information
- Widely supported

### SYM Format (Symbol Table)

```
; SJASM-style
main            EQU $8000
game_loop       EQU $8050
score           EQU $5C00

; Alternative style
$8000 main
$8050 game_loop
$5C00 score
```

### VICE Format

```
# VICE labels
al C:8000 .main
al C:8050 .game_loop
al C:5C00 .score
```

### sjasmplus SLD Format (Future)

```
|SLD.data.version|1
|PAGES.cnt|8
|PAGES.size|16384
|SLOT|0|0|0|16384|MEMORY_PAGES_16K
|LABEL|main|8000|1|code|MAIN
|LABEL|game_loop|8050|1|code|MAIN
|LABEL|score|5C00|1|data|VARS
|SOURCE|8000|game.asm|42
```

**Advantages**:
- Bank-aware (`PAGES` section)
- Source file/line mapping (`SOURCE`)
- Type information
- Module grouping

## Current Implementation

### Class: LabelManager

**Location**: `core/src/debugger/labels/labelmanager.h`

```cpp
class LabelManager {
public:
    // Lifecycle
    LabelManager(EmulatorContext* context);
    ~LabelManager();

    // Label Management
    bool AddLabel(const std::string& name, uint16_t z80Address, 
                  uint16_t bank = UINT16_MAX, uint16_t bankOffset = UINT16_MAX,
                  const std::string& type = "", const std::string& module = "",
                  const std::string& comment = "", bool active = true);
    bool UpdateLabel(const Label& label);
    bool RemoveLabel(const std::string& name);
    void ClearAllLabels();

    // Lookup
    std::shared_ptr<Label> GetLabelByZ80Address(uint16_t address) const;
    std::shared_ptr<Label> GetLabelByName(const std::string& name) const;
    std::vector<std::shared_ptr<Label>> GetAllLabels() const;
    size_t GetLabelCount() const;

    // File I/O
    bool LoadLabels(const std::string& path);      // Auto-detect
    bool LoadMapFile(const std::string& path);
    bool LoadSymFile(const std::string& path);
    bool SaveLabels(const std::string& path, FileFormat format) const;

    enum class FileFormat { UNKNOWN, MAP, SYM, VICE, SJASM, Z88DK };
};
```

### Internal Storage

```cpp
// Dual-map structure for O(1) lookup in both directions
std::map<uint16_t, std::shared_ptr<Label>> _labelsByZ80Address;
std::map<std::string, std::shared_ptr<Label>> _labelsByName;
```

**Limitation**: Current address map stores only one label per address. Bank-aware lookup requires scanning all labels.

### Notifications

```cpp
// Posted when labels change
constexpr char const* NC_LABEL_CHANGED = "LABEL_CHANGED";
```

Observers (disassembler, UI) refresh their display when this fires.

### Qt UI Components

| Component | File | Purpose |
|-----------|------|---------|
| LabelEditor | `unreal-qt/src/debugger/labeleditor.h` | Table view, load/save, bulk operations |
| LabelDialog | `unreal-qt/src/debugger/labeldialog.h` | Single label edit form |

**LabelEditor columns**: Name, Address, Bank, Bank Offset, RAM/ROM, Type, Module, Comment

## Improvements Required

### 1. Filtering API (Core)

**Problem**: `GetAllLabels()` returns everything; filtering happens client-side.

**Solution**: Add filtered query methods:

```cpp
// Individual filters
std::vector<std::shared_ptr<Label>> GetLabelsByModule(const std::string& module) const;
std::vector<std::shared_ptr<Label>> GetLabelsByBank(uint16_t bank, 
    std::optional<MemoryBankModeEnum> bankType = std::nullopt) const;
std::vector<std::shared_ptr<Label>> GetLabelsByType(const std::string& type) const;
std::vector<std::shared_ptr<Label>> GetLabelsInRange(uint16_t from, uint16_t to) const;

// Combined filter
struct LabelFilter {
    std::optional<std::string> module;
    std::optional<uint16_t> bank;
    std::optional<MemoryBankModeEnum> bankType;
    std::optional<std::string> type;
    std::optional<uint16_t> addressFrom;
    std::optional<uint16_t> addressTo;
    std::optional<bool> activeOnly = true;
};
std::vector<std::shared_ptr<Label>> GetLabels(const LabelFilter& filter) const;
```

**Implementation note**: Consider adding indices for frequently-filtered fields (module, type) if performance becomes an issue with large symbol sets.

### 2. Bank-Aware Address Lookup

**Problem**: `GetLabelByZ80Address()` returns first match, ignoring current bank state.

**Solution**: Add context-aware lookup:

```cpp
// Returns label matching address AND current bank state
std::shared_ptr<Label> GetLabelByZ80AddressInContext(uint16_t address) const;

// Returns all labels at address (for showing alternatives)
std::vector<std::shared_ptr<Label>> GetAllLabelsAtAddress(uint16_t address) const;
```

### 3. Automation Bindings

#### CLI Commands

```
# Lookup
label <name>                              # Show label details
label at <address>                        # Find label(s) at address

# Add/modify
label add <name> <addr> [options]
  --bank N                                # Physical bank number
  --rom | --ram                           # Bank type
  --type code|data|const|var              # Semantic type
  --module NAME                           # Module/segment
  --comment "text"                        # Description
label update <name> [options]             # Modify existing
label remove <name>                       # Delete
label toggle <name>                       # Toggle active state

# List/filter
labels                                    # All labels
labels --module NAME                      # By module
labels --bank N [--rom|--ram]             # By bank
labels --type TYPE                        # By type
labels --range 0x8000 0xBFFF              # By address range
labels --inactive                         # Show disabled labels

# File operations
symbols load <file>                       # Load (auto-detect format)
symbols save <file> [--format sym|map]    # Save
symbols clear                             # Remove all
symbols info                              # Show loaded file, label count
```

#### WebAPI Endpoints

```
GET    /api/v1/emulator/{id}/labels
       ?module=X&bank=N&type=T&from=ADDR&to=ADDR&active=true

GET    /api/v1/emulator/{id}/labels/{name}
GET    /api/v1/emulator/{id}/labels/at/{address}

POST   /api/v1/emulator/{id}/labels
       Body: {"name": "main", "address": 32768, "type": "code", ...}

PUT    /api/v1/emulator/{id}/labels/{name}
       Body: {"comment": "Updated comment", ...}

DELETE /api/v1/emulator/{id}/labels/{name}
DELETE /api/v1/emulator/{id}/labels          # Clear all (with confirmation)

POST   /api/v1/emulator/{id}/symbols/load
       Body: {"path": "/path/to/file.sym"}

POST   /api/v1/emulator/{id}/symbols/save
       Body: {"path": "/path/to/file.sym", "format": "sym"}
```

#### Lua Bindings

```lua
-- Lookup
local lbl = get_label("main")
-- Returns: {name="main", address=0x8000, bank=0xFFFF, type="code", ...}

local lbl = get_label_at(0x8000)
local all = get_labels_at(0x8000)  -- All labels at address

-- Filtered list
local labels = get_labels()  -- All
local labels = get_labels({
    module = "GFX",
    type = "code",
    bank = 5,
    bank_type = "ram",
    from = 0x8000,
    to = 0xBFFF,
    active_only = true
})

-- Mutation
add_label("main", 0x8000)  -- Minimal
add_label("sprite_data", 0x9000, {
    bank = 5,
    bank_type = "ram",
    type = "data",
    module = "GFX",
    comment = "Player sprite frames"
})

update_label("main", {comment = "Entry point after ROM init"})
remove_label("old_label")
toggle_label("debug_routine")  -- Flip active state
clear_labels()

-- File I/O
load_symbols("/path/to/game.sym")
save_symbols("/path/to/game.sym", "sym")  -- or "map"

-- Info
local count = label_count()
local info = symbols_info()
-- {count=150, file="/path/to/game.sym", format="SJASM"}
```

#### Python Bindings

```python
# Same API as Lua, returns dicts
label = emu.get_label("main")
# {'name': 'main', 'address': 0x8000, 'bank': 0xFFFF, 'bank_type': 'ram',
#  'type': 'code', 'module': 'MAIN', 'comment': '', 'active': True}

# Filtered query
gfx_data = emu.get_labels(module="GFX", type="data")

# Add with full options
emu.add_label("player_x", 0x5C00, 
              type="var", 
              module="GAME",
              comment="Player X coordinate, pixels")

# Iteration
for label in emu.get_labels():
    if label['type'] == 'code':
        emu.bp(label['address'])  # Breakpoint on all code labels
```

### 4. Debugger UI Enhancements

#### Filter Toolbar (LabelEditor)

```
┌─────────────────────────────────────────────────────────────┐
│ [Module: ▼ All    ] [Bank: ▼ Any] [Type: ▼ All] [🔍 Search ]│
│ ☑ Active only                                    [Clear]    │
├─────────────────────────────────────────────────────────────┤
│ Name          │ Address │ Bank │ Type │ Module │ Comment    │
├───────────────┼─────────┼──────┼──────┼────────┼────────────┤
│ main          │ $8000   │  *   │ code │ MAIN   │ Entry...   │
│ game_loop     │ $8050   │  *   │ code │ MAIN   │            │
│ sprite_data   │ $9000   │  5   │ data │ GFX    │ Player...  │
├───────────────┴─────────┴──────┴──────┴────────┴────────────┤
│ Showing 3 of 150 labels                          [+ Add]    │
└─────────────────────────────────────────────────────────────┘
```

**Features needed**:
- Module dropdown populated from loaded labels
- Bank dropdown with RAM/ROM distinction
- Type dropdown (code/data/const/var/all)
- Text search (name, comment)
- Active-only checkbox
- Clear filters button
- Status bar with filtered/total count
- Remember filter state in settings

#### Context Menu Additions

- "Copy address" - copy hex address to clipboard
- "Go to address" - jump disassembler to label
- "Set breakpoint" - create breakpoint at label
- "Find references" - search disassembly for references to this label

### 5. Symbolic Disassembly (Task 0.2.3)

**Current output**:
```
8000  CD 50 80     CALL $8050
8003  21 00 5C     LD HL,$5C00
8006  C3 00 80     JP $8000
```

**Improved output**:
```
8000  CD 50 80     CALL game_loop ($8050)
8003  21 00 5C     LD HL,score ($5C00)
8006  C3 00 80     JP main ($8000)
```

**Implementation**:
1. In `Z80Disassembler::disassembleSingleCommand()`, check if operand is an address
2. If `LabelManager::GetLabelByZ80Address(operand)` returns a label, format as `label (address)`
3. For relative jumps, resolve target address first, then lookup
4. Consider bank context for bank-aware labels

**Edge cases**:
- Multiple labels at same address: use first active, or show list
- Inactive labels: optionally skip or show in different format
- Data sections: show label at data address if type is "data"

### 6. sjasmplus SLD Format (Task 0.2.2)

**Priority**: High - sjasmplus is the most popular ZX Spectrum assembler.

**Implementation**:
```cpp
bool LoadSLDFile(const std::string& path);
```

**SLD parsing requirements**:
- Parse `|PAGES.cnt|` for bank count
- Parse `|LABEL|name|addr|bank|type|module|` entries
- Parse `|SOURCE|addr|file|line|` for source mapping (future)
- Handle relative addresses within pages

### 7. Symbolic Breakpoints (Task 0.2.4)

**Challenge**: When source code changes and is recompiled, label addresses change. Breakpoint at old address is now wrong.

**Options**:
1. **Address-based** (current): Breakpoint stays at address, symbol display updates
2. **Symbol-bound**: Breakpoint follows symbol when symbols reload
3. **Hybrid**: User chooses per-breakpoint

**Proposed solution**:
```cpp
struct Breakpoint {
    // Existing fields...
    
    // New: optional symbolic binding
    std::optional<std::string> boundLabel;
    
    // When symbols reload:
    // - If boundLabel set and exists, update address
    // - If boundLabel set but missing, mark breakpoint as "unresolved"
};
```

## Performance Considerations

### Current Complexity

| Operation | Complexity |
|-----------|------------|
| Lookup by name | O(log n) |
| Lookup by address | O(log n) |
| Add label | O(log n) |
| Remove label | O(log n) |
| Get all labels | O(n) |
| Filter by module | O(n) |
| Filter by bank | O(n) |

### Potential Optimizations

For projects with 10,000+ symbols:
1. Add `std::map<std::string, std::vector<Label*>> _labelsByModule` index
2. Add `std::map<std::string, std::vector<Label*>> _labelsByType` index
3. Consider `std::unordered_map` for name lookup (O(1) average)

### Memory Usage

Each Label: ~200 bytes (strings dominate)
10,000 labels: ~2MB

Acceptable for desktop debugging scenarios.

## Testing

### Existing Tests

**Location**: `core/tests/debugger/labelmanager_test.cpp`

**Coverage**:
- Add/remove/update labels
- Lookup by name and address
- Load MAP file format
- Load SYM file format
- File format detection

### Tests Needed

- [ ] Bank-aware lookup tests
- [ ] Filter API tests (by module, bank, type, range)
- [ ] Combined filter tests
- [ ] Large symbol set performance tests
- [ ] SLD format parsing tests
- [ ] Edge cases: duplicate names, same address multiple labels

## Implementation Order

1. **Core filtering** - `GetLabels(filter)` and convenience methods
2. **Unit tests** - Cover filtering API
3. **CLI bindings** - `label`, `labels`, `symbols` commands
4. **WebAPI bindings** - REST endpoints
5. **Lua bindings** - `get_label`, `get_labels`, etc.
6. **Python bindings** - Same as Lua
7. **UI filtering** - LabelEditor toolbar
8. **Symbolic disasm** - Z80Disassembler integration
9. **SLD format** - sjasmplus support
10. **Symbolic breakpoints** - Design and implement

## Related Documentation

- [Automation Interface](../../../features/automation.md) - CLI/WebAPI/Lua/Python overview
- [Breakpoint Manager](./breakpoints.md) - Breakpoint system (if exists)
- [Z80 Disassembler](./disassembler.md) - Disassembly engine (if exists)

## Related Tasks

| Task | Description | Status |
|------|-------------|--------|
| 0.2.1 | Expose LabelManager to automation | TODO |
| 0.2.2 | `symbols load` command (sjasmplus .sld) | POSTPONED |
| 0.2.3 | Symbolic disassembly output | TODO |
| 0.2.4 | Symbolic breakpoints (`bp label`) | DESIGN |

## Files

| Path | Purpose |
|------|---------|
| `core/src/debugger/labels/labelmanager.h` | Core API definition |
| `core/src/debugger/labels/labelmanager.cpp` | Implementation |
| `core/tests/debugger/labelmanager_test.cpp` | Unit tests |
| `unreal-qt/src/debugger/labeleditor.h` | Qt label table widget |
| `unreal-qt/src/debugger/labeleditor.cpp` | Qt label table implementation |
| `unreal-qt/src/debugger/labeldialog.h` | Qt single label edit dialog |
| `unreal-qt/src/debugger/labeldialog.cpp` | Qt dialog implementation |
