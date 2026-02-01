# Python Bindings - Direct C++ Integration

## Overview

The Python bindings provide direct in-process access to the emulator core through pybind11. This enables powerful automation, testing, AI/ML integration, and complex scripting scenarios.

**Status**: ✅ Implemented
**Implementation**: `core/automation/python/`
**Binding Library**: pybind11
**Python Version**: 3.10+

**Note**: See [command-interface.md](./command-interface.md) for the complete command reference that these Python bindings implement.  

## Architecture

```
┌──────────────────────────────┐
│   Python Script/REPL         │
│   (test.py, notebook, etc.)  │
└───────────┬──────────────────┘
            │ Python C API
            ▼
┌──────────────────────────────┐
│   pybind11 Bindings          │
│   • Emulator class           │
│   • Memory class             │
│   • Z80 class                │
│   • BreakpointManager        │
└───────────┬──────────────────┘
            │ Direct calls (zero-copy)
            ▼
┌──────────────────────────────┐
│   C++ Emulator Core          │
│   (native performance)       │
└──────────────────────────────┘
```

## Key Advantages

1. **In-Process Execution**: No IPC overhead, direct memory access
2. **Zero-Copy Data Access**: Read memory buffers without serialization
3. **Python Ecosystem**: NumPy, Pandas, Matplotlib, PyTorch, TensorFlow
4. **Interactive Development**: IPython, Jupyter notebooks
5. **Full Language Features**: Classes, exceptions, decorators, context managers
6. **Type Hints**: Modern Python with type checking support

## Installation

### Module Import
```python
import unreal_emulator as ue

# Create emulator instance
emu = ue.Emulator()
emu.init()

# Access subsystems
cpu = emu.get_cpu()
memory = emu.get_memory()
debug = emu.get_debug_manager()
```

### Embedded Python
The emulator can embed Python interpreter internally:
```cpp
// C++ side
AutomationPython* python = new AutomationPython();
python->start();
python->executePython("print('Hello from embedded Python!')");
```

## API Reference

### Emulator Class

```python
class Emulator:
    """Main emulator instance"""
    
    def __init__(self, symbolic_id: str = "", log_level: LogLevel = LogLevel.Trace):
        """Create emulator instance"""
        
    def init(self) -> bool:
        """Initialize emulator hardware"""
        
    def release(self):
        """Cleanup and release resources"""
        
    def reset(self):
        """Hardware reset (equivalent to pressing reset button)"""
        
    def pause(self):
        """Pause emulation"""
        
    def resume(self):
        """Resume emulation"""
        
    def step(self) -> int:
        """Execute one instruction, return PC"""
        
    def steps(self, count: int) -> int:
        """Execute N instructions, return final PC"""
        
    def get_id(self) -> str:
        """Get emulator UUID"""
        
    def get_symbolic_id(self) -> str:
        """Get symbolic ID (if set)"""
        
    def set_symbolic_id(self, id: str):
        """Set symbolic ID"""
        
    def get_state(self) -> EmulatorState:
        """Get current state (Running/Paused/Stopped)"""
        
    def get_cpu(self) -> Z80:
        """Get Z80 CPU instance"""
        
    def get_memory(self) -> Memory:
        """Get memory subsystem"""
        
    def get_debug_manager(self) -> DebugManager:
        """Get debug manager"""
        
    def get_breakpoint_manager(self) -> BreakpointManager:
        """Get breakpoint manager"""
```

### Z80 CPU Class

```python
class Z80:
    """Z80 CPU emulation"""
    
    def get_af(self) -> int:
        """Get AF register pair"""
        
    def set_af(self, value: int):
        """Set AF register pair"""
        
    def get_bc(self) -> int:
        """Get BC register pair"""
        
    # Similar for DE, HL, IX, IY, SP, PC
    
    def get_pc(self) -> int:
        """Get program counter"""
        
    def set_pc(self, value: int):
        """Set program counter"""
        
    def get_flags(self) -> dict:
        """Get flags as dictionary"""
        # Returns: {'S': bool, 'Z': bool, 'H': bool, 'P': bool, 'N': bool, 'C': bool}
        
    def execute(self, instructions: int = 1) -> int:
        """Execute N instructions, return cycles"""
```

### Memory Class

```python
class Memory:
    """Memory subsystem with banking"""
    
    def read(self, address: int) -> int:
        """Read byte from Z80 address space (0x0000-0xFFFF)"""
        
    def write(self, address: int, value: int):
        """Write byte to Z80 address space"""
        
    def read_word(self, address: int) -> int:
        """Read 16-bit word (little-endian)"""
        
    def write_word(self, address: int, value: int):
        """Write 16-bit word (little-endian)"""
        
    def read_bytes(self, address: int, length: int) -> bytes:
        """Read byte array (zero-copy view)"""
        
    def write_bytes(self, address: int, data: bytes):
        """Write byte array"""
        
    def fill(self, address: int, length: int, value: int):
        """Fill memory region with byte value"""
        
    def get_ram(self) -> memoryview:
        """Get direct view of RAM (zero-copy)"""
        
    def get_rom(self) -> memoryview:
        """Get direct view of ROM (read-only)"""
```

### Physical Page Access

> **Status**: ✅ Implemented (2026-01)

Access physical memory pages directly, bypassing Z80 paging. Useful for ROM patching, shadow memory inspection, and cache/misc memory access.

```python
# Page types: "ram", "rom", "cache", "misc"

# Read/write single byte from physical page
value = emu.page_read("ram", 5, 0x100)        # page 5, offset 0x100
emu.page_write("ram", 5, 0x100, 0xFF)

# Block operations
data = emu.page_read_block("rom", 2, 0, 256)  # Read 256 bytes from ROM page 2
emu.page_write_block("ram", 7, 0x1000, data)  # Write block to RAM page 7

# Get memory configuration
info = emu.memory_info()
# Returns: {
#   'pages': {'ram_count': 256, 'rom_count': 64, 'cache_count': 4, 'misc_count': 4},
#   'z80_banks': [{'bank': 0, 'start': 0, 'end': 16383, 'mapping': 'ROM0'}, ...]
# }
```

### BreakpointManager Class

```python
class BreakpointManager:
    """Breakpoint and watchpoint management"""
    
    def add_execution_breakpoint(self, address: int) -> int:
        """Set execution breakpoint, return ID"""
        
    def add_memory_read_breakpoint(self, address: int) -> int:
        """Set memory read watchpoint"""
        
    def add_memory_write_breakpoint(self, address: int) -> int:
        """Set memory write watchpoint"""
        
    def add_port_in_breakpoint(self, port: int) -> int:
        """Set port IN breakpoint"""
        
    def add_port_out_breakpoint(self, port: int) -> int:
        """Set port OUT breakpoint"""
        
    def remove_breakpoint(self, bp_id: int) -> bool:
        """Remove breakpoint by ID"""
        
    def clear_breakpoints(self):
        """Remove all breakpoints"""
        
    def activate_breakpoint(self, bp_id: int) -> bool:
        """Enable breakpoint"""
        
    def deactivate_breakpoint(self, bp_id: int) -> bool:
        """Disable breakpoint"""
        
    def get_breakpoints(self) -> list:
        """Get list of all breakpoints"""
```

### DebugManager Class

```python
class DebugManager:
    """High-level debugging coordination"""
    
    def get_breakpoint_manager(self) -> BreakpointManager:
        """Get breakpoint manager"""
        
    def get_label_manager(self) -> LabelManager:
        """Get label manager (symbols)"""
        
    def get_disassembler(self) -> Z80Disassembler:
        """Get disassembler"""
```

### Analyzer Management

```python
# List registered analyzers
names = emu.analyzer_list()  # → ["trdos"]

# Enable/disable analyzer
emu.analyzer_enable("trdos")  # → True
emu.analyzer_disable("trdos")  # → True

# Get analyzer status
status = emu.analyzer_status("trdos")
# → {"enabled": True, "state": "IN_TRDOS", "event_count": 42}

# Get captured events (optional limit parameter)
events = emu.analyzer_events("trdos", limit=50)
# → ["[0001234] TR-DOS Entry (PC=$3D00)", ...]

# Clear analyzer events
emu.analyzer_clear("trdos")
```

**TRDOSAnalyzer States:**
- `IDLE` - Not in TR-DOS ROM
- `IN_TRDOS` - In TR-DOS ROM but not executing command
- `IN_COMMAND` - Executing TR-DOS command
- `IN_SECTOR_OP` - Sector read/write in progress
- `IN_CUSTOM` - Custom sector operation

### Debug Commands (Direct Methods)

> **Status**: ✅ Implemented (2026-01)

The `Emulator` class exposes these debug methods directly:

```python
# Debug Mode Control
emu.debugmode_on()         # Enable debug mode
emu.debugmode_off()        # Disable debug mode
emu.debugmode()            # Returns True if debug mode enabled

# Memory Access Counters
emu.memcounters()          # Returns dict with:
# {
#     'total_reads': int, 'total_writes': int, 'total_executes': int,
#     'total_accesses': int,
#     'banks': [{'bank': 0, 'reads': int, 'writes': int, 'executes': int}, ...]
# }
emu.memcounters_reset()    # Reset all counters

# Call Trace
emu.calltrace(limit=50)    # Returns list of recent control flow events

# Breakpoint Status (Last Triggered)
emu.bp_status()            # Returns dict with type-specific fields:
# Memory: {'valid': bool, 'id': int, 'type': 'memory', 'address': int,
#          'execute': bool, 'read': bool, 'write': bool, 'active': bool, 'note': str, 'group': str}
# Port:   {'valid': bool, 'id': int, 'type': 'port', 'address': int,
#          'in': bool, 'out': bool, 'active': bool, 'note': str, 'group': str}
emu.bp_clear_last()        # Clear last triggered breakpoint ID

# Disassembly
emu.disasm()                                    # Disassemble from PC (default 10 lines)
emu.disasm(address=0x8000, count=20)           # Disassemble from address
emu.disasm_page("rom", 2, offset=0, count=20)  # Disassemble physical ROM page (e.g., TR-DOS)
emu.disasm_page("ram", 5, offset=0x100, count=10)  # Disassemble physical RAM page
# Returns: list of dicts with keys: address/offset, bytes, mnemonic, size, target (if jump)
```

### Opcode Profiler

> **Status**: ✅ Implemented (2026-01)

Track Z80 opcode execution statistics and capture execution traces for crash forensics.

```python
# Session control
emu.profiler_start()    # Enable feature, clear data, start capture
emu.profiler_stop()     # Stop capture (data preserved)
emu.profiler_pause()    # Pause capture (retain data)
emu.profiler_resume()   # Resume paused capture
emu.profiler_clear()    # Clear all profiler data

# Status query
status = emu.profiler_status()
# Returns: {
#     'feature_enabled': True,
#     'capturing': True,
#     'session_state': 'capturing',  # 'stopped', 'capturing', 'paused'
#     'total_executions': 15234567,
#     'trace_size': 10000,
#     'trace_capacity': 10000
# }

# Get opcode execution counters (top N by count)
counters = emu.profiler_counters(100)

# Get recent execution trace (for crash forensics)
trace = emu.profiler_trace(500)
```

### Memory Profiler

> **Status**: ✅ Implemented (2026-01)

Track memory access patterns (reads/writes/executes) across all physical memory pages.

```python
# Session control
emu.memory_profiler_start()    # Enable feature, start capture
emu.memory_profiler_stop()     # Stop capture
emu.memory_profiler_pause()    # Pause capture (retain data)
emu.memory_profiler_resume()   # Resume paused capture
emu.memory_profiler_clear()    # Clear all data

# Status query
status = emu.memory_profiler_status()
# Returns: {'feature_enabled': True, 'capturing': True, 'session_state': 'capturing', 'tracking_mode': 'physical'}

# Data retrieval
pages = emu.memory_profiler_pages(limit=20)   # Get per-page access summaries
counters = emu.memory_profiler_counters(page=5, mode='physical')  # Address-level counters
regions = emu.memory_profiler_regions()       # Monitored region statistics
emu.memory_profiler_save('/path/output', format='yaml')  # Save data to file
```

### Call Trace Profiler

> **Status**: ✅ Implemented (2026-01)

Track CPU control flow events (CALL, RET, JP, JR, RST, etc.).

```python
# Session control
emu.calltrace_profiler_start()    # Enable feature, start capture
emu.calltrace_profiler_stop()     # Stop capture
emu.calltrace_profiler_pause()    # Pause capture (retain data)
emu.calltrace_profiler_resume()   # Resume paused capture
emu.calltrace_profiler_clear()    # Clear all data

# Status query
status = emu.calltrace_profiler_status()
# Returns: {'feature_enabled': True, 'capturing': True, 'session_state': 'capturing', 
#           'entry_count': 450, 'buffer_capacity': 10000}

# Data retrieval
entries = emu.calltrace_profiler_entries(count=100)  # Get trace entries
stats = emu.calltrace_profiler_stats()     # Get call/return statistics
```

### Unified Profiler Control

> **Status**: ✅ Implemented (2026-01)

Control all profilers (opcode, memory, calltrace) simultaneously.

```python
# Start all profilers (enables features automatically)
emu.profilers_start_all()

# Pause all profilers (retain data)
emu.profilers_pause_all()

# Resume all paused profilers
emu.profilers_resume_all()

# Stop all profilers
emu.profilers_stop_all()

# Clear all profiler data
emu.profilers_clear_all()

# Get status of all profilers
status = emu.profilers_status_all()
# Returns: {
#     'opcode': {'session_state': 'capturing', 'total_executions': 15234567},
#     'memory': {'session_state': 'capturing', 'feature_enabled': True},
#     'calltrace': {'session_state': 'capturing', 'entry_count': 450}
# }
```

### Enumerations

```python
class EmulatorState(Enum):
    Unknown = 0
    Initialized = 1
    Run = 2
    Paused = 3
    Resumed = 4
    Stopped = 5

class LogLevel(Enum):
    Trace = 0
    Debug = 1
    Info = 2
    Warning = 3
    Error = 4
    Fatal = 5
```

## Usage Examples

### Basic Emulator Control

```python
import unreal_emulator as ue

# Create and initialize
emu = ue.Emulator("test_instance")
emu.init()

# Reset and run
emu.reset()
emu.resume()

# Pause after 1 second
import time
time.sleep(1)
emu.pause()

# Inspect state
cpu = emu.get_cpu()
print(f"PC: 0x{cpu.get_pc():04X}")
print(f"AF: 0x{cpu.get_af():04X}")
print(f"Flags: {cpu.get_flags()}")

# Cleanup
emu.release()
```

### Memory Analysis

```python
import unreal_emulator as ue
import numpy as np

emu = ue.Emulator()
emu.init()

mem = emu.get_memory()

# Read screen memory as NumPy array (zero-copy!)
screen_data = np.frombuffer(
    mem.read_bytes(0x4000, 6912),
    dtype=np.uint8
)

# Analyze pixel distribution
print(f"Mean: {screen_data.mean():.2f}")
print(f"Std: {screen_data.std():.2f}")
print(f"Unique values: {len(np.unique(screen_data))}")

# Find all non-zero pixels
pixels = screen_data[:6144].reshape(192, 256//8)
print(f"Active pixels: {np.count_nonzero(pixels)}")
```

### Automated Testing

```python
import unreal_emulator as ue

def test_basic_boot():
    """Test emulator boots correctly"""
    emu = ue.Emulator()
    assert emu.init(), "Failed to initialize"
    
    cpu = emu.get_cpu()
    assert cpu.get_pc() == 0x0000, "PC not at reset vector"
    
    # Execute boot code
    emu.steps(1000)
    
    # Check we're past ROM initialization
    assert cpu.get_pc() > 0x1000, "Boot sequence didn't progress"
    
    emu.release()

def test_memory_write():
    """Test memory write operations"""
    emu = ue.Emulator()
    emu.init()
    
    mem = emu.get_memory()
    
    # Write test pattern
    mem.write(0x8000, 0x42)
    assert mem.read(0x8000) == 0x42
    
    # Write word
    mem.write_word(0x8001, 0x1234)
    assert mem.read_word(0x8001) == 0x1234
    
    emu.release()

if __name__ == "__main__":
    test_basic_boot()
    test_memory_write()
    print("All tests passed!")
```

### Breakpoint-Based Automation

```python
import unreal_emulator as ue

emu = ue.Emulator()
emu.init()

bp_mgr = emu.get_breakpoint_manager()

# Set breakpoint at ROM print routine
bp_id = bp_mgr.add_execution_breakpoint(0x0010)  # RST 10h

# Run until breakpoint
emu.resume()

# Wait for breakpoint hit (simplified)
while emu.get_state() != ue.EmulatorState.Paused:
    time.sleep(0.01)

cpu = emu.get_cpu()
print(f"Breakpoint hit at PC=0x{cpu.get_pc():04X}")

# Inspect character being printed (in A register)
char_code = cpu.get_af() >> 8
print(f"Character: {chr(char_code)}")

# Continue
bp_mgr.remove_breakpoint(bp_id)
emu.resume()
```

### Machine Learning Integration

```python
import unreal_emulator as ue
import numpy as np
import torch

class EmulatorEnvironment:
    """OpenAI Gym-style environment for RL"""
    
    def __init__(self):
        self.emu = ue.Emulator()
        self.emu.init()
        self.mem = self.emu.get_memory()
        
    def reset(self):
        self.emu.reset()
        return self.get_state()
        
    def step(self, action):
        # Execute action (e.g., key press)
        self.execute_action(action)
        
        # Run for one frame
        self.emu.steps(69888)  # One frame @ 3.5 MHz
        
        # Get new state and reward
        state = self.get_state()
        reward = self.compute_reward()
        done = self.is_done()
        
        return state, reward, done, {}
        
    def get_state(self):
        """Extract screen as state"""
        screen = np.frombuffer(
            self.mem.read_bytes(0x4000, 6144),
            dtype=np.uint8
        )
        return screen.reshape(192, 32)
        
    def compute_reward(self):
        # Game-specific reward logic
        score_addr = 0x5C00  # Example
        return self.mem.read_word(score_addr)
        
    def execute_action(self, action):
        # Simulate key press
        pass

# Train agent
env = EmulatorEnvironment()
state = env.reset()

for episode in range(1000):
    action = select_action(state)
    state, reward, done, _ = env.step(action)
    # Train neural network...
```

### Jupyter Notebook Integration

```python
# Cell 1: Setup
import unreal_emulator as ue
import matplotlib.pyplot as plt
import numpy as np

emu = ue.Emulator()
emu.init()
mem = emu.get_memory()

# Cell 2: Run emulation
emu.reset()
emu.steps(100000)

# Cell 3: Visualize screen
screen_data = np.frombuffer(mem.read_bytes(0x4000, 6144), dtype=np.uint8)
screen = decode_zx_screen(screen_data)  # Custom function
plt.imshow(screen, cmap='gray')
plt.title('ZX Spectrum Screen')
plt.show()

# Cell 4: Register dump
cpu = emu.get_cpu()
regs = {
    'AF': f"0x{cpu.get_af():04X}",
    'BC': f"0x{cpu.get_bc():04X}",
    'DE': f"0x{cpu.get_de():04X}",
    'HL': f"0x{cpu.get_hl():04X}",
    'PC': f"0x{cpu.get_pc():04X}",
}
print(regs)
```

## Performance Characteristics

- **Function Call Overhead**: ~50-100ns per call (negligible)
- **Memory Access**: Zero-copy via `memoryview` (native speed)
- **Array Operations**: NumPy integration (native C speed)
- **Python Interpreter**: GIL released during long-running C++ operations

## Threading and Concurrency

### Global Interpreter Lock (GIL)
- Python's GIL limits true parallelism
- Long C++ operations release GIL automatically
- Use multiprocessing for true parallelism:

```python
from multiprocessing import Process

def run_emulator(instance_id):
    emu = ue.Emulator(instance_id)
    emu.init()
    # Run independently
    
# Create multiple processes
processes = [Process(target=run_emulator, args=(f"emu_{i}",)) 
             for i in range(4)]
for p in processes:
    p.start()
```

### Thread Safety
- Emulator instances are NOT thread-safe
- Use one emulator per thread
- Synchronize access with locks if sharing

```python
import threading

lock = threading.Lock()

def safe_read_memory(emu, addr):
    with lock:
        return emu.get_memory().read(addr)
```

## Exception Handling

Python exceptions are propagated from C++ exceptions:

```python
try:
    emu = ue.Emulator()
    emu.init()
    
    mem = emu.get_memory()
    mem.write(0xFFFFFF, 0x42)  # Invalid address
    
except ue.EmulatorError as e:
    print(f"Emulator error: {e}")
    
except Exception as e:
    print(f"Unexpected error: {e}")
    
finally:
    emu.release()
```

## Type Hints and IDE Support

```python
from typing import Optional
import unreal_emulator as ue

def analyze_code(emu: ue.Emulator, start: int, end: int) -> list[int]:
    """Analyze code region"""
    mem: ue.Memory = emu.get_memory()
    cpu: ue.Z80 = emu.get_cpu()
    
    # IDE provides autocompletion and type checking
    data: bytes = mem.read_bytes(start, end - start)
    return list(data)
```

## Debugging Python Scripts

### Using pdb
```python
import pdb
import unreal_emulator as ue

emu = ue.Emulator()
emu.init()

pdb.set_trace()  # Breakpoint here

cpu = emu.get_cpu()
print(cpu.get_pc())
```

### Using VS Code
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Python: Emulator Script",
            "type": "python",
            "request": "launch",
            "program": "${file}",
            "console": "integratedTerminal"
        }
    ]
}
```

## Installation & Distribution

### Building Python Module
```bash
cd core/automation/python
mkdir build && cd build
cmake .. -DENABLE_PYTHON_AUTOMATION=ON
make
```

### Installing Module
```bash
pip install ./dist/unreal_emulator-1.0-cp310-cp310-linux_x86_64.whl
```

### Requirements
- Python 3.10+
- NumPy (optional, for array operations)
- Matplotlib (optional, for visualization)

## See Also

### Interface Documentation
- **[Command Interface Overview](./command-interface.md)** - Core command reference and architecture
- **[CLI Interface](./cli-interface.md)** - TCP-based text protocol for interactive debugging
- **[WebAPI Interface](./webapi-interface.md)** - HTTP/REST API for web integration
- **[Lua Bindings](./lua-interface.md)** - Lightweight scripting and embedded logic

### Advanced Interfaces (Future)
- **[GDB Protocol](./gdb-protocol.md)** - Professional debugging with standard GDB/LLDB clients
- **[Universal Debug Bridge](./udb-protocol.md)** - High-performance analysis and profiling

### Navigation
- **[Interface Documentation Index](./README.md)** - Overview of all control interfaces

### External Resources
- **[pybind11 Documentation](https://pybind11.readthedocs.io/)** - C++/Python binding library
