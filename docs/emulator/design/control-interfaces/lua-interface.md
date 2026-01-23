# Lua Bindings - Lightweight Scripting

## Overview

The Lua bindings provide a lightweight, fast scripting interface to the emulator. Perfect for macros, event handlers, quick automation, and embedded logic.

**Status**: ✅ Implemented
**Implementation**: `core/automation/lua/`
**Binding Library**: sol2 (C++/Lua bridge)
**Lua Version**: 5.4

**Note**: See [command-interface.md](./command-interface.md) for the complete command reference that these Lua bindings implement.  

## Architecture

```
┌──────────────────────────────┐
│   Lua Scripts                │
│   (macro.lua, event.lua)     │
└───────────┬──────────────────┘
            │ Lua C API
            ▼
┌──────────────────────────────┐
│   sol2 Bindings              │
│   • Emulator userdata        │
│   • Memory userdata          │
│   • Z80 userdata             │
└───────────┬──────────────────┘
            │ Direct calls
            ▼
┌──────────────────────────────┐
│   C++ Emulator Core          │
└──────────────────────────────┘
```

## Key Advantages

1. **Lightweight**: Minimal memory footprint (~200KB)
2. **Fast Execution**: JIT compilation possible with LuaJIT
3. **Easy to Learn**: Simple, clean syntax
4. **Sandboxed**: Safe execution environment
5. **Hot Reload**: Reload scripts without restarting emulator
6. **Embeddable**: Scripts can be embedded in save states

## Basic Usage

### Hello World
```lua
-- hello.lua
print("Hello from Lua!")

-- Access emulator
local emu = get_emulator()
print("Emulator ID: " .. emu:get_id())

-- Read registers
local cpu = emu:get_cpu()
print(string.format("PC = 0x%04X", cpu:get_pc()))
```

### Running Scripts

#### From Command Line
```bash
unreal-emulator --lua-script macro.lua
```

#### From CLI Interface
```
> script run macro.lua
```

#### Programmatically (C++)
```cpp
AutomationLua* lua = new AutomationLua();
lua->start();
lua->executeScript("macro.lua");
```

## API Reference

### Global Functions

```lua
-- Get main emulator instance
emu = get_emulator()

-- Print to console
print(message)

-- Sleep (milliseconds)
sleep(100)

-- Get current timestamp
time = os.time()
```

### Emulator Object

```lua
-- Create new emulator
emu = Emulator.new()
emu = Emulator.new("symbolic_id")

-- Initialize
success = emu:init()

-- Control
emu:reset()
emu:pause()
emu:resume()
pc = emu:step()          -- Returns PC after step
pc = emu:steps(count)    -- Returns PC after N steps

-- Properties
id = emu:get_id()
sym_id = emu:get_symbolic_id()
emu:set_symbolic_id("name")
state = emu:get_state()  -- "running", "paused", "stopped"

-- Subsystems
cpu = emu:get_cpu()
mem = emu:get_memory()
debug_mgr = emu:get_debug_manager()
bp_mgr = emu:get_breakpoint_manager()

-- Cleanup
emu:release()
```

### CPU Object (Z80)

```lua
cpu = emu:get_cpu()

-- Register access
af = cpu:get_af()
cpu:set_af(0x44C4)

bc = cpu:get_bc()
cpu:set_bc(0x3F00)

-- Individual registers (16-bit pairs)
cpu:get_de()
cpu:get_hl()
cpu:get_ix()
cpu:get_iy()
cpu:get_sp()
cpu:get_pc()

-- 8-bit register access
a = cpu:get_a()
f = cpu:get_f()
b = cpu:get_b()
c = cpu:get_c()
-- etc.

-- Flags
flags = cpu:get_flags()  -- Returns table
-- flags.S, flags.Z, flags.H, flags.P, flags.N, flags.C

-- Execution
cycles = cpu:execute(instruction_count)
```

### Memory Object

```lua
mem = emu:get_memory()

-- Byte access
value = mem:read(0x8000)
mem:write(0x8000, 0x42)

-- Word access (16-bit, little-endian)
word = mem:read_word(0x8000)
mem:write_word(0x8000, 0x1234)

-- Block operations
bytes = mem:read_bytes(0x8000, 256)  -- Returns string
mem:write_bytes(0x8000, data_string)

mem:fill(0x8000, 256, 0xFF)  -- Fill with value

-- Helper to get RAM/ROM size
ram_size = mem:get_ram_size()
rom_size = mem:get_rom_size()
```

### Physical Page Access

> **Status**: ✅ Implemented (2026-01)

Access physical memory pages directly, bypassing Z80 paging. Useful for ROM patching, shadow memory inspection, and cache/misc memory access.

```lua
-- Read/write single byte from physical page
-- type: "ram", "rom", "cache", "misc"
-- page: page number (0-255 for RAM, 0-63 for ROM, etc.)
-- offset: offset within 16KB page (0-16383)
value = page_read("ram", 5, 0x100)
page_write("ram", 5, 0x100, 0xFF)

-- Block operations
data = page_read_block("rom", 2, 0, 256)    -- Read 256 bytes from ROM page 2
page_write_block("ram", 7, 0x1000, data)    -- Write block to RAM page 7

-- Get memory configuration
info = memory_info()
-- Returns: {
--   pages = {ram_count=256, rom_count=64, cache_count=4, misc_count=4},
--   z80_banks = {{bank=0, start=0, end=16383, mapping="ROM0"}, ...}
-- }
```

### Breakpoint Manager

```lua
bp_mgr = emu:get_breakpoint_manager()

-- Add breakpoints
bp_id = bp_mgr:add_execution_bp(0x8000)
bp_id = bp_mgr:add_memory_read_bp(0x4000)
bp_id = bp_mgr:add_memory_write_bp(0x5C00)
bp_id = bp_mgr:add_port_in_bp(0xFE)
bp_id = bp_mgr:add_port_out_bp(0xFE)

-- Remove breakpoints
bp_mgr:remove_bp(bp_id)
bp_mgr:clear_all_bps()

-- Activate/deactivate
bp_mgr:activate_bp(bp_id)
bp_mgr:deactivate_bp(bp_id)
bp_mgr:activate_all()
bp_mgr:deactivate_all()

-- Query
count = bp_mgr:get_bp_count()
bp_list = bp_mgr:get_all_bps()  -- Returns table of breakpoints
```

### Analyzer Management

```lua
-- List registered analyzers
local names = analyzer_list()  -- → {"trdos"}

-- Enable/disable analyzer
analyzer_enable("trdos")   -- → true
analyzer_disable("trdos")  -- → true

-- Get analyzer status
local status = analyzer_status("trdos")
-- status.enabled = true
-- status.state = "IN_TRDOS"
-- status.event_count = 42

-- Get captured events (optional limit parameter)
local events = analyzer_events("trdos", 50)
-- → {"[0001234] TR-DOS Entry (PC=$3D00)", ...}

-- Clear analyzer events
analyzer_clear("trdos")
```

**TRDOSAnalyzer States:**
- `IDLE` - Not in TR-DOS ROM
- `IN_TRDOS` - In TR-DOS ROM but not executing command
- `IN_COMMAND` - Executing TR-DOS command
- `IN_SECTOR_OP` - Sector read/write in progress
- `IN_CUSTOM` - Custom sector operation

### Debug Commands (Direct Methods)

> **Status**: ✅ Implemented (2026-01)

The `Emulator` object exposes these debug methods directly:

```lua
-- Breakpoint Status (Last Triggered)
local status = emu:bp_status()  -- Returns table with type-specific fields:
-- Memory: {valid=bool, id=int, type='memory', address=int,
--          execute=bool, read=bool, write=bool, active=bool, note=str, group=str}
-- Port:   {valid=bool, id=int, type='port', address=int,
--          in_=bool, out=bool, active=bool, note=str, group=str}
emu:bp_clear_last()  -- Clear last triggered breakpoint ID

-- Disassembly
local lines = disasm()                   -- Disassemble from PC (default 10 lines)
local lines = disasm(0x8000, 20)         -- Disassemble from address, count
local lines = disasm_page("rom", 2, 0, 20)   -- Disassemble physical ROM page (e.g., TR-DOS)
local lines = disasm_page("ram", 5, 0x100, 10)  -- Disassemble physical RAM page
-- Returns: table with entries: {offset, bytes, mnemonic, size, target (if jump)}

-- Debug Mode (via feature manager)
emu:feature_set("debugmode", true)   -- Enable debug mode
emu:feature_set("debugmode", false)  -- Disable debug mode
local enabled = emu:feature_get("debugmode")
```

> [!NOTE]
> Memory counters (`memcounters`) and call trace (`calltrace`) are available via CLI and WebAPI. Full Lua bindings for these analysis features are planned.

## Usage Examples

### Register Dump Macro

```lua
-- dump_regs.lua
function dump_registers()
    local emu = get_emulator()
    local cpu = emu:get_cpu()
    
    print(string.format("AF = 0x%04X", cpu:get_af()))
    print(string.format("BC = 0x%04X", cpu:get_bc()))
    print(string.format("DE = 0x%04X", cpu:get_de()))
    print(string.format("HL = 0x%04X", cpu:get_hl()))
    print(string.format("PC = 0x%04X", cpu:get_pc()))
    print(string.format("SP = 0x%04X", cpu:get_sp()))
    
    local flags = cpu:get_flags()
    print(string.format("Flags: S=%d Z=%d H=%d P=%d N=%d C=%d",
        flags.S and 1 or 0,
        flags.Z and 1 or 0,
        flags.H and 1 or 0,
        flags.P and 1 or 0,
        flags.N and 1 or 0,
        flags.C and 1 or 0))
end

dump_registers()
```

### Memory Scanner

```lua
-- find_pattern.lua
function find_pattern(pattern, start_addr, end_addr)
    local emu = get_emulator()
    local mem = emu:get_memory()
    
    local pattern_len = #pattern
    local found = {}
    
    for addr = start_addr, end_addr - pattern_len do
        local match = true
        for i = 1, pattern_len do
            if mem:read(addr + i - 1) ~= pattern[i] then
                match = false
                break
            end
        end
        
        if match then
            table.insert(found, addr)
        end
    end
    
    return found
end

-- Find all occurrences of: 0xCD 0x00 0x00 (CALL 0x0000)
local pattern = {0xCD, 0x00, 0x00}
local results = find_pattern(pattern, 0x4000, 0xFFFF)

print(string.format("Found %d occurrences:", #results))
for _, addr in ipairs(results) do
    print(string.format("  0x%04X", addr))
end
```

### Breakpoint Automation

```lua
-- auto_bp.lua
function on_breakpoint_hit(bp_id, address)
    local emu = get_emulator()
    local cpu = emu:get_cpu()
    local mem = emu:get_memory()
    
    print(string.format("Breakpoint %d hit at 0x%04X", bp_id, address))
    
    -- Dump registers
    print(string.format("  AF=0x%04X BC=0x%04X DE=0x%04X HL=0x%04X",
        cpu:get_af(), cpu:get_bc(), cpu:get_de(), cpu:get_hl()))
    
    -- Read instruction bytes
    local op1 = mem:read(address)
    local op2 = mem:read(address + 1)
    print(string.format("  Opcode: %02X %02X", op1, op2))
    
    -- Continue execution
    emu:resume()
end

-- Set up breakpoints
local emu = get_emulator()
local bp_mgr = emu:get_breakpoint_manager()

bp_mgr:add_execution_bp(0x0562)  -- Tape load routine
bp_mgr:add_execution_bp(0x0E9B)  -- Print routine

print("Breakpoints set. Resuming...")
emu:resume()
```

### Frame Counter

```lua
-- frame_counter.lua
local frame_count = 0
local emu = get_emulator()

function on_frame()
    frame_count = frame_count + 1
    
    if frame_count % 50 == 0 then  -- Every second (50 fps)
        print(string.format("Frame: %d", frame_count))
        
        local cpu = emu:get_cpu()
        print(string.format("  PC: 0x%04X", cpu:get_pc()))
    end
end

-- Register frame callback
emu:set_frame_callback(on_frame)

print("Frame counter started. Press Ctrl+C to stop.")
while true do
    sleep(100)
end
```

### Auto-Save Macro

```lua
-- autosave.lua
local emu = get_emulator()
local save_interval = 60  -- seconds
local save_count = 0

while true do
    sleep(save_interval * 1000)
    
    save_count = save_count + 1
    local filename = string.format("autosave_%03d.z80", save_count)
    
    emu:pause()
    emu:save_snapshot(filename)
    print(string.format("Auto-saved to %s", filename))
    emu:resume()
end
```

### Cheat Code Injector

```lua
-- cheat.lua
-- Infinite lives cheat for a hypothetical game

local emu = get_emulator()
local mem = emu:get_memory()

local LIVES_ADDR = 0x8000  -- Address storing lives count

function apply_cheat()
    local lives = mem:read(LIVES_ADDR)
    if lives < 9 then
        mem:write(LIVES_ADDR, 9)
        print("Lives set to 9")
    end
end

-- Apply cheat every frame
emu:set_frame_callback(apply_cheat)

print("Cheat activated: Infinite lives")
```

### Disassembler Output

```lua
-- disasm.lua
function disassemble(start_addr, count)
    local emu = get_emulator()
    local mem = emu:get_memory()
    
    -- Simple Z80 disassembler (subset)
    local opcodes = {
        [0x00] = "NOP",
        [0x3E] = "LD A,#",
        [0x06] = "LD B,#",
        [0xC3] = "JP nn",
        [0xCD] = "CALL nn",
        [0xC9] = "RET",
        -- ... more opcodes
    }
    
    local addr = start_addr
    for i = 1, count do
        local opcode = mem:read(addr)
        local mnemonic = opcodes[opcode] or string.format("DB 0x%02X", opcode)
        
        print(string.format("0x%04X: %02X  %s", addr, opcode, mnemonic))
        
        addr = addr + 1
    end
end

-- Disassemble 16 instructions from current PC
local emu = get_emulator()
local cpu = emu:get_cpu()
disassemble(cpu:get_pc(), 16)
```

## Event Callbacks

### Supported Events

```lua
-- Frame rendered
emu:set_frame_callback(function()
    -- Called every frame
end)

-- Breakpoint hit
emu:set_breakpoint_callback(function(bp_id, address)
    -- Called when breakpoint triggers
end)

-- State changed
emu:set_state_callback(function(old_state, new_state)
    print("State: " .. old_state .. " -> " .. new_state)
end)

-- Memory written
emu:set_memory_write_callback(function(address, value)
    print(string.format("Write: [0x%04X] = 0x%02X", address, value))
end)
```

### Callback Management

```lua
-- Register callback
callback_id = emu:register_callback("frame", my_function)

-- Unregister callback
emu:unregister_callback(callback_id)

-- Clear all callbacks
emu:clear_callbacks()
```

## Lua Standard Library

Available Lua modules:
- `base` - Basic functions (print, tonumber, etc.)
- `string` - String manipulation
- `table` - Table operations
- `math` - Mathematical functions
- `io` - File I/O (sandboxed)
- `os` - OS functions (time, date)

**Note**: Unsafe functions are disabled in sandboxed mode:
- `os.execute()` - Execute shell commands (disabled)
- `io.popen()` - Pipe to process (disabled)
- `loadfile()` - Load arbitrary files (restricted)
- `dofile()` - Execute arbitrary files (restricted)

## Performance

- **Function Call Overhead**: ~100-200ns
- **Memory Access**: Comparable to C (no GC overhead for reads)
- **Script Load Time**: <1ms for typical scripts
- **Memory Usage**: ~200KB base + script size

### Optimization Tips

1. **Cache emulator objects**:
   ```lua
   local emu = get_emulator()  -- Once
   local mem = emu:get_memory()  -- Once
   
   for i = 1, 1000 do
       mem:read(0x8000)  -- Fast
   end
   ```

2. **Use local variables**:
   ```lua
   local mem = emu:get_memory()  -- Faster than global
   ```

3. **Batch operations**:
   ```lua
   -- Slow: individual reads
   for addr = 0x8000, 0x8100 do
       local val = mem:read(addr)
   end
   
   -- Fast: block read
   local data = mem:read_bytes(0x8000, 256)
   ```

4. **Avoid frequent table creation**:
   ```lua
   -- Reuse tables instead of creating new ones
   local result = {}
   for i = 1, 1000 do
       table.insert(result, mem:read(0x8000 + i))
   end
   ```

## Error Handling

```lua
-- Protected call
local success, result = pcall(function()
    local emu = get_emulator()
    emu:init()
    -- ... operations that might fail
end)

if not success then
    print("Error: " .. result)
end

-- Error with context
local function safe_read(addr)
    if addr < 0 or addr > 0xFFFF then
        error("Invalid address: " .. addr)
    end
    return emu:get_memory():read(addr)
end
```

## Debugging Lua Scripts

### Print Debugging
```lua
print("Debug: PC = " .. cpu:get_pc())
print(string.format("Debug: 0x%04X", value))
```

### Interactive Debugging
```lua
-- Simple REPL
while true do
    io.write("> ")
    local line = io.read()
    if line == "quit" then break end
    
    local func, err = load("return " .. line)
    if func then
        print(func())
    else
        print("Error: " .. err)
    end
end
```

### Using MobDebug (with ZeroBrane Studio)
```lua
-- Install MobDebug
require("mobdebug").start()

-- Script will pause here for debugger
local emu = get_emulator()
-- Set breakpoints in ZeroBrane
```

## Integration with Emulator

### Loading Scripts at Startup
```bash
unreal-emulator --lua-init init.lua --lua-script main.lua
```

### Hot Reload
```lua
-- reload.lua
function reload_script(script_name)
    package.loaded[script_name] = nil
    require(script_name)
    print("Reloaded: " .. script_name)
end

-- Usage
reload_script("my_module")
```

### Persistent State
```lua
-- Save Lua state with snapshot
emu:save_snapshot("save.z80", {
    lua_state = true,
    include_scripts = true
})

-- Load restores Lua state
emu:load_snapshot("save.z80")
-- Scripts automatically resumed
```

## See Also

### Interface Documentation
- **[Command Interface Overview](./command-interface.md)** - Core command reference and architecture
- **[CLI Interface](./cli-interface.md)** - TCP-based text protocol for interactive debugging
- **[WebAPI Interface](./webapi-interface.md)** - HTTP/REST API for web integration
- **[Python Bindings](./python-interface.md)** - Direct C++ bindings for automation and AI/ML

### Advanced Interfaces (Future)
- **[GDB Protocol](./gdb-protocol.md)** - Professional debugging with standard GDB/LLDB clients
- **[Universal Debug Bridge](./udb-protocol.md)** - High-performance analysis and profiling

### Navigation
- **[Interface Documentation Index](./README.md)** - Overview of all control interfaces

### External Resources
- **[Lua 5.4 Reference Manual](https://www.lua.org/manual/5.4/)** - Lua programming language specification
- **[sol2 Documentation](https://sol2.readthedocs.io/)** - C++/Lua binding library
