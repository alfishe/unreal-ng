# Emulator Control Interface (ECI)

## Overview

The Emulator Control Interface (ECI) provides a unified high-level abstraction for interacting with the emulator core. It is designed to be interface-agnostic, supporting multiple transport layers including:

- **CLI** (TCP-based Command Line Interface)
- **WebAPI** (HTTP/REST endpoints)
- **Scripting** (Lua, Python bindings)
- **Debugger Protocols** (Future: GDB Remote Serial Protocol, ADB-style universal debugging)

Each interface layer translates its protocol-specific requests into standardized internal commands that operate on the emulator core.

### Design Philosophy

The ECI architecture is built on these key principles:

1. **Protocol Agnostic**: High-level commands are independent of transport protocol
2. **Session Management**: Each client maintains its own session context and selected emulator instance
3. **Multi-Instance Support**: Commands can target different emulator instances running simultaneously
4. **Extensibility**: New interfaces can be added without modifying core command logic
5. **Consistency**: Same command semantics across all interfaces (CLI, WebAPI, Python, Lua)

### Command Interfaces

The command interfaces are responsible for handling the specific protocol (e.g., TCP socket, HTTP request). Each interface provides access to the same underlying emulator control functionality but optimized for different use cases.

| Interface | Purpose | Protocol | Implementation Status | Characteristics |
| :--- | :--- | :--- | :--- | :--- |
| **CLI** | Human interaction & Simple Automation | TCP Socket (Text-based) | ✅ Implemented | Stateful connection. Interactive (REPL-like) or scripted via netcat/telnet. Human-readable text protocol. Session maintains selected emulator context. Default port: TBD |
| **WebAPI** | UI Frontends & Remote Management | HTTP/1.1 REST (JSON) | ✅ Partially Implemented | Stateless request/response. Standard web compatibility. CORS-enabled. Ideal for web dashboards, SPAs, and microservice integration. Uses Drogon framework. |
| **Python** | Deep Integration & AI/ML | Direct C++ Bindings (pybind11) | ✅ Implemented | In-process execution. Zero-copy data access. Full Python ecosystem available. Ideal for ML/AI agents, data analysis, test automation, and complex scripting. Embedded interpreter or module import. |
| **Lua** | Embedded Logic & Macros | Direct C++ Bindings (sol2) | ✅ Implemented | In-process execution. Minimal overhead. Sandboxed execution. Best for game scripts, hotkey macros, event handlers, and rapid prototyping. |
| **GDB Protocol** | Source-level Debugging | GDB Remote Serial Protocol (Binary) | 🔮 Planned | Standard GDB/MI protocol. Attach with GDB, LLDB, or IDE debuggers (VS Code, CLion). Full breakpoint/watchpoint support. Z80 register mapping. |
| **Universal Debug Bridge** | Advanced Debugging & Analysis | ADB-inspired protocol (Binary) | 🔮 Planned | Extended debugging capabilities beyond GDB. Memory streaming, performance profiling, real-time tracing. Ideal for deep system analysis and reverse engineering. |

**Legend**: ✅ Implemented | 🔧 In Progress | 🔮 Planned

**Documentation References**:
- CLI Interface: See [cli-interface.md](./cli-interface.md)
- WebAPI Interface: See [webapi-interface.md](./webapi-interface.md)
- Python Bindings: See [python-interface.md](./python-interface.md)
- Lua Bindings: See [lua-interface.md](./lua-interface.md)
- GDB Protocol: See [gdb-protocol.md](./gdb-protocol.md)
- Universal Debug Bridge: See [udb-protocol.md](./udb-protocol.md)


## Architecture

The control architecture follows a layered Command Dispatcher pattern with clear separation of concerns:

```
┌─────────────────────────────────────────────────────────────┐
│                    Client Applications                      │
│  (CLI Client, Web Browser, Python Script, Lua Script, IDE)  │
└─────────┬───────────────────┬───────────────────┬───────────┘
          │                   │                   │
          ▼                   ▼                   ▼
┌──────────────────┐  ┌──────────────┐  ┌─────────────────────┐
│  Transport Layer │  │  Transport   │  │   Transport Layer   │
│   (TCP Socket)   │  │  (HTTP/REST) │  │  (Direct Bindings)  │
│                  │  │              │  │                     │
│  AutomationCLI   │  │AutomationWeb │  │ Automation Python/  │
│                  │  │     API      │  │       Lua           │
└────────┬─────────┘  └──────┬───────┘  └──────────┬──────────┘
         │                   │                     │
         └───────────────────┴─────────────────────┘
                             │
                             ▼
                 ┌────────────────────────┐
                 │   Session Context      │
                 │   (ClientSession)      │
                 │                        │
                 │ • Selected Emulator ID │
                 │ • Client Socket/Handle │
                 │ • Session State        │
                 └───────────┬────────────┘
                             │
                             ▼
                 ┌────────────────────────┐
                 │   Command Processor    │
                 │   (CLIProcessor)       │
                 │                        │
                 │ • Command Parsing      │
                 │ • Argument Validation  │
                 │ • Handler Dispatch     │
                 │ • Error Handling       │
                 └───────────┬────────────┘
                             │
                             ▼
            ┌─────────────────────────────────┐
            │      Emulator Manager           │
            │                                 │
            │  • Multi-instance management    │
            │  • Instance lifecycle           │
            │  • ID resolution                │
            └────────────────┬────────────────┘
                             │
                             ▼
            ┌─────────────────────────────────┐
            │       Emulator Instance         │
            │                                 │
            │  • Z80 CPU Core                 │
            │  • Memory Manager               │
            │  • Debug Manager                │
            │  • Breakpoint Manager           │
            │  • I/O Subsystems               │
            │  • Feature Manager              │
            └─────────────────────────────────┘
```

### Layer Responsibilities

#### 1. Transport Layer
- **Purpose**: Protocol-specific communication handling
- **Responsibilities**:
  - Accept client connections (CLI: TCP sockets, WebAPI: HTTP requests)
  - Manage connection lifecycle
  - Serialize/deserialize protocol messages
  - Handle network errors and timeouts
- **Implementation**:
  - `AutomationCLI`: TCP server on configurable port
  - `AutomationWebAPI`: Drogon-based HTTP/REST server
  - `AutomationPython`: pybind11 embedded interpreter
  - `AutomationLua`: sol2 embedded interpreter

#### 2. Session Context
- **Purpose**: Maintain per-client state across commands
- **Responsibilities**:
  - Track selected emulator instance for subsequent commands
  - Store client-specific settings
  - Provide response routing back to client
- **Key Data**:
  - `_clientSocket`: Network handle for response delivery
- **Note**: WebAPI is stateless; emulator ID must be in each request path

#### 2.5 Global Selection State ⭐

**Critical Design Change (2026-01-09)**: Selection state is **globally shared** across all interfaces (CLI, WebAPI, UI), not per-session.

**Architecture**:
- **Single Source of Truth**: `EmulatorManager` maintains the globally selected emulator ID
- **Automatic Notification**: Selection changes trigger `NC_EMULATOR_SELECTION_CHANGED` notifications
- **Synchronized Binding**: All components (CLI display, UI screen/audio/keyboard) bind to the same emulator

**Implementation**:
```cpp
class EmulatorManager {
    std::string _selectedEmulatorId;  // Global selection state
    std::mutex _selectionMutex;       // Thread-safe access
    
    std::string GetSelectedEmulatorId();        // CLI, UI, WebAPI use this
    bool SetSelectedEmulatorId(const std::string& id);  // Updates + notifies
};
```

**Selection Update Rules**:
1. **Manual Selection**: `select <id>` command updates global state
2. **Auto-Selection**: Starting the first/only running emulator auto-selects it
3. **Removal**: Removing the selected emulator clears selection
4. **Notification**: All selection changes send `NC_EMULATOR_SELECTION_CHANGED`

**UI Binding**:
- `MainWindow` subscribes to `NC_EMULATOR_SELECTION_CHANGED`
- On notification, atomically rebinds all components to the same emulator:
  - Screen (framebuffer)
  - Audio (callback)
  - Keyboard (UUID tagging)
  - Menus (state)
- Thread-safe: UI updates marshaled to Qt main thread via `QMetaObject::invokeMethod`

**Example Flow**:
```
1. WebAPI: POST /emulator/start → creates instance #2, starts it
2. EmulatorManager: Detects #2 is only running emulator
3. EmulatorManager::StartEmulatorAsync → SetSelectedEmulatorId("#2")
4. SetSelectedEmulatorId → sends NC_EMULATOR_SELECTION_CHANGED(prev="", new="#2")
5. MainWindow receives notification on MessageCenter thread
6. MainWindow marshals to Qt main thread, atomically binds screen/audio/keyboard to #2
7. CLI: list → shows #2 with "*" marker (reads from EmulatorManager::GetSelectedEmulatorId())
8. Result: CLI, UI, and WebAPI all see #2 as selected
```

**Benefits**:
- ✅ Consistent selection across all interfaces
- ✅ UI always binds to the correct running emulator
- ✅ Keyboard events route to the same emulator shown on screen
- ✅ Thread-safe with proper mutex protection

**Migration Note**: Previously, `ClientSession` maintained per-session selection. This caused inconsistencies when WebAPI created emulators vs. CLI commands. Now all interfaces share the same global selection state.

#### 3. Command Processor
- **Purpose**: Parse and route commands to appropriate handlers
- **Responsibilities**:
  - Parse command strings into command + arguments
  - Handle quoted arguments and escape sequences
  - Validate argument count and types
  - Map command names (including aliases) to handler functions
  - Format and return responses
  - Handle exceptions and error conditions
- **Implementation**: `CLIProcessor` class with command handler map
- **Command Format**: `<command> [arg1] [arg2] ... [argN]`

#### 4. Emulator Manager
- **Purpose**: Manage multiple concurrent emulator instances
- **Responsibilities**:
  - Create and destroy emulator instances
  - Assign unique IDs (UUID + optional symbolic ID)
  - Resolve emulator references by ID
  - Track instance lifecycle and activity
- **Multi-Instance Support**: Allows multiple ZX Spectrum instances with independent state

#### 5. Emulator Core
- **Purpose**: Execute actual emulation and debugging operations
- **Subsystems**:
  - **CPU Core** (`Z80`): Instruction execution, stepping, register access
  - **Memory Manager** (`Memory`): RAM/ROM banking, memory read/write
  - **Debug Manager** (`DebugManager`): High-level debugging coordination
  - **Breakpoint Manager** (`BreakpointManager`): Execution/memory/port breakpoints
  - **I/O Subsystems**: Tape, disk (FDD/HDD), keyboard, sound
  - **Feature Manager** (`FeatureManager`): Runtime feature toggles (layers, audio, etc.)

### Command Flow Example

```
1. User → CLI Client: "bp 0x8000"
2. CLI Client → TCP Socket → AutomationCLI
3. AutomationCLI → CLIProcessor.ProcessCommand(session, "bp 0x8000")
4. CLIProcessor:
   a. Parse: command="bp", args=["0x8000"]
   b. Lookup handler: _commandHandlers["bp"] → HandleBreakpoint
   c. Get selected emulator from session
   d. Validate address argument
5. HandleBreakpoint → Emulator.GetDebugManager()
6. DebugManager → BreakpointManager.AddExecutionBreakpoint(0x8000)
7. Response ← "Breakpoint set at 0x8000"
8. AutomationCLI → Send response to client socket
9. CLI Client displays: "Breakpoint set at 0x8000"
```

### Error Handling Strategy

- **Invalid Command**: Unknown command returns help message suggestion
- **Missing Emulator**: Commands requiring emulator context return error if none selected
- **Invalid Arguments**: Argument parsing errors return usage information
- **Emulator Errors**: Core exceptions are caught and returned as error messages
- **Connection Errors**: Transport layer handles disconnections gracefully

## Implemented Commands

The following commands are currently implemented and available in the core automation modules. Command availability may vary slightly between interfaces (CLI, WebAPI, Python, Lua) - see interface-specific documentation for details.

### 1. Session & Lifecycle Management

Commands to manage the connection and emulator instances. These commands are essential for establishing context before issuing emulator-specific commands.

| Command | Aliases | Arguments | Description |
| :--- | :--- | :--- | :--- |
| `help` | `?` | `[command]` | Display available commands and their usage. If `command` is specified, show detailed help for that command. |
| `start` | | `[model]` | Create and start a new emulator instance. Optional `model` parameter specifies model (default: 48K). Returns the new instance ID. Triggers `NC_EMULATOR_INSTANCE_CREATED` notification. | 🔮 Planned |
| `start <model>` | | `<model-name>` | Start a new emulator instance with specific Spectrum model (48k, 128k, +2, +3, pentagon, etc.). Returns the new instance ID. | 🔮 Planned |
| `start <config-file>` | | `<config-path>` | Start a new emulator instance using configuration from file. Returns the new instance ID. | 🔮 Planned |
| `create [model]` | | `[model-name]` | Create a new emulator instance without starting it. If `model` is specified, creates an emulator of that model type (e.g., `48K`, `128K`, `Pentagon`). If no model is specified, creates a default 48K emulator. Instance remains in initialized state until explicitly started with `resume`. Returns the UUID of the created instance. Automatically selects if first instance. Triggers `NC_EMULATOR_INSTANCE_CREATED` notification. |
| `stop [id]` | | `[emulator-id|index|all]` | Stop and destroy emulator instance(s). If only one emulator is running, can be called without parameters. Can specify UUID, index from `list` command (1-based), or `all` to stop all instances. Triggers `NC_EMULATOR_INSTANCE_DESTROYED` and `NC_EMULATOR_STATE_CHANGE` notifications. | 🔮 Planned |
| `stop all` | | | Stop and destroy all emulator instances. | 🔮 Planned |
| `status` | | | Show the runtime status (Running/Paused/Stopped/Debug) of all emulator instances, including ID, symbolic name, uptime, and current state. |
| `list` | | | List all managed emulator instances with their UUIDs, status (Running/Paused/Stopped), and debug state. Stopped instances are removed from this list. |
| `select <id>` | | `<emulator-id>` | Select the active emulator instance for subsequent commands. The `<id>` can be either the UUID or symbolic ID. All following commands (reset, pause, registers, etc.) will operate on this selected instance. |
| `open <file>` | | `<file-path>` | Open and load a file into the selected emulator. Supports multiple formats: tape images (.tap, .tzx), snapshots (.z80, .sna), disk images (.trd, .scl, .fdi). File type is auto-detected by extension. |
| `exit` | `quit` | | Terminate the control session. For CLI, closes the TCP connection. Does not stop the emulator instances themselves. |
| `dummy` | | | No-operation command used for connection initialization and testing. Returns a simple acknowledgment. Useful for verifying the connection is alive. |

**Implementation Details**:

**`start` Commands**:
- Creates a new emulator instance via `EmulatorManager::CreateEmulator()`
- Assigns unique UUID and optional symbolic name
- Initializes with specified configuration or defaults
- Automatically starts the emulator in paused state
- Returns instance ID for immediate use
- Triggers `NC_EMULATOR_INSTANCE_CREATED` Message Center notification

**`stop <id>`**:
- Locates emulator instance by ID (UUID or symbolic name)
- Calls `EmulatorManager::StopEmulator()` then `EmulatorManager::RemoveEmulator()`
- Cleans up all resources (memory, threads, peripherals)
- If stopping the currently selected instance, selection becomes invalid
- Triggers `NC_EMULATOR_INSTANCE_DESTROYED` Message Center notification

**`stop all`**:
- Iterates through all emulator instances
- Stops each instance individually
- Triggers individual `NC_EMULATOR_INSTANCE_DESTROYED` notifications
- Useful for cleanup before application exit

**Message Center Integration**:
- All instance lifecycle changes trigger notifications:
  - `NC_EMULATOR_INSTANCE_CREATED`: New instance created and started (payload: instance ID)
  - `NC_EMULATOR_INSTANCE_DESTROYED`: Instance destroyed (payload: instance ID)
  - `NC_EMULATOR_STATE_CHANGE`: Instance state changes (payload: instance ID, new state)
- GUI components, automation scripts, and other interfaces receive notifications
- Enables reactive UI updates and automated workflows
- WebAPI clients can subscribe to notifications via WebSocket for real-time updates

**Configuration Options**:
- **Default**: Standard 48K Spectrum with BASIC ROM
- **Model-based**: `start 128k`, `start pentagon`, `start +3`
- **Config file**: `start /path/to/config.json` (JSON configuration)
- **Inline config**: Future support for `--option=value` parameters

**Multi-Instance Scenarios**:
- **Testing**: Run multiple instances with different ROMs for compatibility testing
- **Comparison**: Compare behavior between different Spectrum models
- **Debugging**: Debug interaction between programs (e.g., loader + main program)
- **Automation**: Parallel processing of multiple tasks

**Example Usage**:
```bash
# Start default 48K instance
> start
Started emulator instance: emu-12345678-abcd-1234-5678-123456789abc

# Start specific model
> start pentagon
Started emulator instance: emu-pentagon-87654321-dcba-4321-8765-987654321fed

# List all instances
> list
┌─────────────────────────────────────┬────────────┬─────────────────────┐
│ Instance ID                         │ Model      │ Status              │
├─────────────────────────────────────┼────────────┼─────────────────────┤
│ emu-12345678-abcd-1234-5678-123456789abc │ 48K        │ Paused              │
│ emu-pentagon-87654321-dcba-4321-8765-987654321fed │ Pentagon  │ Paused              │
└─────────────────────────────────────┴────────────┴─────────────────────┘

# Select and work with specific instance
> select emu-pentagon-87654321-dcba-4321-8765-987654321fed
Selected emulator: emu-pentagon-87654321-dcba-4321-8765-987654321fed

# Stop specific instance
> stop emu-12345678-abcd-1234-5678-123456789abc
Stopped emulator instance: emu-12345678-abcd-1234-5678-123456789abc

# Stop all instances
> stop all
Stopped 1 emulator instance(s)
```

**Notes**:
- Most commands require an emulator to be selected first via `select <id>`
- CLI sessions auto-select the most recently created emulator if only one exists
- WebAPI requests must include the emulator ID in the URL path (stateless design)
- Instance management operations are thread-safe and can be performed while other instances are running

### 2. Execution Control

Commands to control the CPU execution flow for the selected emulator instance. These commands are fundamental for debugging and precise control over emulation.

| Command | Aliases | Arguments | Description |
| :--- | :--- | :--- | :--- |
| `pause [id]` | | `[emulator-id\|index]` | Immediately pause emulation execution. The CPU stops at the next instruction boundary. Current register state and memory are preserved. Returns confirmation when paused. Useful before inspecting state or setting breakpoints. If no ID specified, auto-selects when exactly one emulator exists. |
| `resume [id]` | | `[emulator-id\|index]` | Resume emulation execution from the paused state. The CPU continues from the current PC register value. If not paused, returns an error. If no ID specified, auto-selects when exactly one emulator exists. |
| `reset [id]` | | `[emulator-id\|index]` | Perform a hardware reset of the emulator. Equivalent to pressing the physical reset button. Resets CPU registers to power-on state (PC=0x0000), reinitializes peripherals, and reloads ROM. RAM contents may be preserved depending on configuration. If no ID specified, auto-selects when exactly one emulator exists. |
| `step` | `stepin` | | Execute exactly one CPU instruction and return. The emulator pauses automatically after execution. Returns the new PC value and disassembled instruction that was executed. Used for line-by-line debugging. |
| `steps <N>` | | `<count>` | Execute exactly `<N>` CPU instructions, then pause. Equivalent to calling `step` N times but more efficient. Returns execution summary including final PC. Useful for stepping through loops. |
| `stepover` | | | Execute one instruction, treating subroutine calls (`CALL`, `RST`) as atomic operations. If the instruction is a call, execution continues until the corresponding `RET`, then pauses. If not a call instruction, behaves like `step`. Essential for stepping over function calls without diving into them. |

**Implementation Details**:

**`step` / `stepin`**:
- Disables real-time execution temporarily
- Executes exactly 1 instruction via `Z80::Execute(1)`
- Captures and returns disassembly of executed instruction
- Updates UI with new register state
- Checks for breakpoint hits after execution

**`steps <N>`**:
- Validates `<N>` is a positive integer (hex or decimal)
- Executes N instructions in a tight loop
- More efficient than N separate `step` commands
- Still respects breakpoints (may stop before N instructions)

**`stepover`**:
- Analyzes the opcode at current PC
- If `CALL`, `RST`, or interrupt call:
  - Sets temporary breakpoint at PC + instruction_length
  - Resumes execution
  - Waits for breakpoint hit or timeout
  - Removes temporary breakpoint
- If not a call: behaves like `step`
- **Limitation**: May not work correctly if the call doesn't return

**`reset`**:
- Calls `Emulator::Reset()`
- Reloads ROM from disk if configured
- Resets all peripherals: tape, disk, keyboard, sound
- Clears interrupt state
- Does NOT clear breakpoints or watchpoints

**Performance Notes**:
- `step` commands disable real-time rendering for precision
- Frequent stepping may impact overall emulation performance
- Use `resume` to return to full-speed emulation

### 3. State Inspection

Commands to view and analyze the internal state of the selected emulator instance. Essential for debugging, reverse engineering, and understanding program behavior.

| Command | Aliases | Arguments | Description |
| :--- | :--- | :--- | :--- |
| `registers` | `regs`, `r` | | Display complete Z80 CPU register state. Shows main registers (AF, BC, DE, HL, IX, IY, SP, PC), alternate register set (AF', BC', DE', HL'), special registers (I, R, IFF1, IFF2), and flags (S, Z, Y, H, X, P/V, N, C). Output formatted for readability with hex and decimal values. |
| `memory <addr> [len]` | `mem`, `m` | `<address> [length]` | Display hex dump of memory contents starting at `<address>`. Default length is 128 bytes if not specified. Shows hex values and ASCII representation side-by-side. Addresses in Z80 address space (0x0000-0xFFFF) with automatic bank resolution. |
| `debugmode <on\|off>` | | `on` or `off` | Enable or disable detailed memory access tracking. When ON, the emulator records every memory read/write for analysis via `memcounters`. **Warning**: Significant performance impact (~50% slower). Use only when analyzing specific memory access patterns. |
| `memcounters [reset]` | `memstats` | `[reset]` | Display memory access statistics when debug mode is active. Shows: <br/>• Read/write/execute access counts per address<br/>• Hotspot identification (most frequently accessed)<br/>• Access type breakdown (code vs data)<br/>If `reset` is specified, clears all accumulated statistics. Requires `debugmode on` to collect data. |
| `calltrace [depth]` | | `[max-entries]` | Display execution call trace history (CALL/RET tracking). Shows:<br/>• Call stack with return addresses<br/>• Function entry points<br/>• Nesting level<br/>• Symbolic names if loaded from .map file<br/>Default shows last 50 entries. Specify `depth` to override (max 1000). The trace buffer is circular and maintained during execution. |

**Implementation Details**:

**`registers`**:
- Reads CPU state via `Z80::GetState()`
- Formats output with register names aligned for readability
- Flags are shown as individual bits with mnemonics
- Example output:
  ```
  Main:  AF=0x44C4 BC=0x3F00 DE=0x0000 HL=0x5C00
  Alt:   AF'=0x0044 BC'=0x174B DE'=0x369B HL'=0x107F
  Index: IX=0x03D4 IY=0x5C3A
  Stack: SP=0xFF24  PC=0x0605
  Int:   I=0x3F R=0x1B IFF1=1 IFF2=1 IM=1
  Flags: SZ-H-PNC [S=0 Z=1 H=0 P=1 N=0 C=0]
  ```

**`memory <addr> [len]`**:
- Parses address as hex (0x8000) or decimal (32768)
- Validates address range (0x0000-0xFFFF)
- Reads from current memory bank configuration
- Hex dump format: 16 bytes per line
- Example output:
  ```
  0x8000: 00 00 C3 CB 11 FF 00 00 | FE 03 20 1A 07 A9 6F 10 | .... .... .. . .o.
  0x8010: F7 C9 FE 06 38 11 A9 6F | CB 27 CB 15 30 09 07 A9 | ....8..o .'..0....
  ```

**`debugmode`**:
- Implemented via `MemoryAccessTracker` component
- When enabled, intercepts all `Memory::Read()`, `Memory::Write()`, `Z80::Execute()` calls
- Maintains per-address access counters in hash table
- Memory overhead: ~2-4 MB for full 64KB tracking
- Performance overhead: ~50% slower (depends on tracking granularity)
- Useful for:
  - Finding code entry points
  - Identifying self-modifying code
  - Locating data structures
  - Understanding memory access patterns

**`memcounters`**:
- Only works when `debugmode on`
- Statistics include:
  - Total reads/writes/executes
  - Per-address frequency counts
  - Access type classification
  - Temporal access patterns
- Output sorted by access frequency (hottest addresses first)
- `reset` clears counters but keeps debug mode active

**`calltrace`**:
- Implemented via `CallTraceManager`
- Automatically tracks all `CALL`, `RST`, `RET`, `RETI`, `RETN` instructions
- Maintains circular buffer (default 10,000 entries)
- Shows call stack depth and relationships
- Can load symbolic names from `.map` files (Pasmo, TASM, Z80ASM format)
- Example output:
  ```
  Depth  PC      Target   Type    Symbol
  -----  ------  ------   -----   ------
  0      0x0605  0x09C0   CALL    cls_screen
  1      0x09C0  0x0E9B   CALL    fill_attr
  1      0x0E9E  ----     RET
  0      0x0608  ----     RET
  ```

**Tips**:
- Use `registers` after each `step` to see instruction effects
- Enable `debugmode` only for short debugging sessions
- Use `calltrace` to understand program flow without stepping through each instruction
- Combine `memory` with breakpoints to watch memory regions change

### 4. Breakpoints & Watchpoints

Advanced debugging features for the selected emulator instance. The emulator supports sophisticated breakpoint management including execution breakpoints, memory watchpoints, I/O port monitoring, and logical grouping.

#### 4.1 Breakpoint Management Commands

| Command | Aliases | Arguments | Description |
| :--- | :--- | :--- | :--- |
| `bp <addr>` | `break`, `breakpoint` | `<address>` | Set an execution breakpoint at `<address>`. Emulation pauses when PC reaches this address. Address can be hex (0x8000) or decimal (32768). Returns breakpoint ID for future reference. |
| `wp <addr> <type>` | `watchpoint` | `<address> <r\|w\|rw>` | Set a memory watchpoint at `<address>`:<br/>• `r` - break on read access<br/>• `w` - break on write access<br/>• `rw` - break on read OR write<br/>Useful for tracking when memory is accessed. |
| `bport <port> <type>` | `portbreak` | `<port> <in\|out\|both>` | Set an I/O port breakpoint:<br/>• `in` - break on port IN operation<br/>• `out` - break on port OUT operation<br/>• `both` - break on either IN or OUT<br/>Essential for debugging I/O operations (keyboard, sound, disk, ports). |
| `bplist` | `breakpoints` | | List all active breakpoints and watchpoints with their IDs, addresses/ports, types, activation status, and group membership. Shows:<br/>• Breakpoint ID<br/>• Type (exec/read/write/port)<br/>• Address/Port<br/>• Active/Inactive<br/>• Group name<br/>• Optional annotation |
| `bpclear [id\|all]` | `bc` | `[breakpoint-id \| all]` | Clear breakpoints:<br/>• No argument: clear ALL breakpoints and watchpoints<br/>• `<id>`: clear specific breakpoint by ID<br/>• `all`: explicitly clear all (same as no argument)<br/>**Warning**: This permanently deletes breakpoints. Use `bpoff` to temporarily disable instead. |

#### 4.2 Breakpoint Group Management

Breakpoint groups allow logical organization of related breakpoints (e.g., "graphics", "loader", "game_loop") and bulk enable/disable operations.

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `bpgroup list` | | List all breakpoint group names. |
| `bpgroup add <id> <group>` | `<breakpoint-id> <group-name>` | Add breakpoint `<id>` to group `<group>`. If group doesn't exist, creates it automatically. |
| `bpgroup remove <id>` | `<breakpoint-id>` | Remove breakpoint from its current group (moves to "default" group). |
| `bpgroup show <group>` | `<group-name>` | List all breakpoints in the specified group. |
| `bpgroup delete <group>` | `<group-name>` | Delete entire group and remove all its breakpoints. |

#### 4.3 Breakpoint Activation Control

Breakpoints can be temporarily disabled without deleting them. This is useful for conditional debugging workflows.

| Command | Aliases | Arguments | Description |
| :--- | :--- | :--- | :--- |
| `bpon <target>` | `enable` | `<id \| group \| all>` | Enable (activate) breakpoints:<br/>• `<id>`: enable specific breakpoint<br/>• `<group-name>`: enable all breakpoints in group<br/>• `all`: enable all breakpoints<br/>Enabled breakpoints will pause execution when hit. |
| `bpoff <target>` | `disable` | `<id \| group \| all>` | Disable (deactivate) breakpoints:<br/>• `<id>`: disable specific breakpoint<br/>• `<group-name>`: disable all breakpoints in group<br/>• `all`: disable all breakpoints<br/>Disabled breakpoints remain in the list but won't trigger. Useful for temporarily ignoring breakpoints without deletion. |

**Implementation Details**:

**Breakpoint Architecture**:
- Managed by `BreakpointManager` class
- Each breakpoint has unique ID (auto-incrementing, never reused)
- Breakpoints stored in hash maps for O(1) lookup during execution
- Three separate hash tables: memory address, I/O port, breakpoint ID

**Breakpoint Types**:

1. **Execution Breakpoints** (`bp`):
   - Checked every CPU instruction before execution
   - Implemented as hash lookup: `_breakpointMapByAddress[PC]`
   - Zero overhead when no breakpoints set
   - Minimal overhead (<1% per breakpoint) when active

2. **Memory Watchpoints** (`wp`):
   - Intercept `Memory::Read()` and `Memory::Write()` calls
   - Can differentiate between code execution and data access
   - Types: `BRK_MEM_READ`, `BRK_MEM_WRITE`, `BRK_MEM_EXECUTE`
   - Overhead: ~2-5% depending on memory access frequency

3. **I/O Port Breakpoints** (`bport`):
   - Intercept `Z80::In()` and `Z80::Out()` operations
   - Track I/O to specific ports (0x00-0xFF)
   - Types: `BRK_IO_IN`, `BRK_IO_OUT`
   - Essential for debugging peripheral communication

**Breakpoint Groups**:
- Default group: "default" (all breakpoints without explicit group)
- Group names are case-sensitive strings
- Groups are created implicitly when first breakpoint added
- Useful organization examples:
  - "loader" - breakpoints in tape/disk loading routines
  - "interrupt" - IM1/IM2 interrupt handlers
  - "graphics" - screen rendering routines
  - "keyboard" - keyboard scanning code

**Breakpoint Hit Behavior**:
1. CPU executes instruction or memory access
2. BreakpointManager checks for matching breakpoint
3. If active breakpoint found:
   - Pause emulation immediately
   - Emit breakpoint hit event
   - Return breakpoint ID
   - Update UI with breakpoint info
4. If inactive or no breakpoint: continue execution

**Persistence**:
- Breakpoints are per-emulator instance
- Survive pause/resume and reset
- Cleared only by `bpclear` or emulator destruction
- NOT saved with snapshots (by design)

**Advanced Usage Examples**:

```bash
# Set execution breakpoint at ROM routine
bp 0x0562  # tape loading routine

# Watch for writes to screen memory
wp 0x4000 w  # top-left screen pixel
wp 0x5800 w  # color attributes

# Monitor ULA port access
bport 0xFE both  # border, tape, speaker

# Organize by functionality
bpgroup add 1 loader
bpgroup add 2 loader
bpgroup add 3 graphics

# Enable only graphics debugging
bpoff all
bpon graphics

# List all breakpoints in a group
bpgroup show loader
```

**Limitations**:
- Maximum breakpoints: 65535 (uint16_t ID limit)
- Breakpoints match Z80 address space only (no bank-specific matching yet)
- Port breakpoints limited to 256 ports (0x00-0xFF)
- No conditional breakpoints (e.g., "break if A=5") - planned for future

**Performance Notes**:
- Execution breakpoints: minimal overhead (~0.5% per 100 breakpoints)
- Memory watchpoints: moderate overhead (~2-5% per watchpoint)
- Port breakpoints: minimal overhead (~1%)
- Recommend <100 active breakpoints for real-time debugging
- Disable unused breakpoints rather than deleting for faster re-enabling

### 5. Feature Management & Configuration

Commands to view and control emulator runtime features for the selected emulator instance. Features control debugging and analysis capabilities and can be toggled at runtime without restarting the emulator.

| Command | Aliases | Arguments | Description | Implementation Status |
| :--- | :--- | :--- | :--- | :--- |
| `feature` | `feature list` | | Display all available features with their current state (on/off). Shows feature name, state, mode, and description in table format. | ✅ Implemented |
| `feature <name> on` | | `<feature-name> on` | Enable a specific feature by name. Available features:<br/>• `calltrace` - collect call trace information for debugging<br/>• `breakpoints` - enable breakpoint handling<br/>• `memorytracking` - collect memory access counters and statistics<br/>• `debugmode` - master debug mode (enables/disables all debug features)<br/>Changes take effect immediately. | ✅ Implemented |
| `feature <name> off` | | `<feature-name> off` | Disable a specific feature. Useful for improving performance when feature is not needed. | ✅ Implemented |
| `feature reset` | | | Reset all features to their default state (typically all off). Useful for returning to standard configuration after debugging. | 🔮 Planned |

**Implementation Details**:

**Feature Manager**:
- Implemented via `FeatureManager` class in the emulator core
- Features are registered at emulator initialization
- Each feature has:
  - Unique string name (case-insensitive)
  - Boolean enabled/disabled state
  - Optional mode string (future: "light", "full", etc.)
  - Description text
  - Optional callback for state change notification

**Available Features**:

| Feature Name | Default State | Purpose | Performance Impact |
| :--- | :--- | :--- | :--- |
| `calltrace` | OFF | Collect call trace information (CALL/RET/RST tracking). Records function calls, return addresses, and call depth for debugging. Maintains circular buffer of recent calls. | Medium (~10-15% CPU overhead) |
| `breakpoints` | OFF | Enable breakpoint handling. When enabled, the emulator checks for breakpoint hits on every instruction. Required for execution breakpoints, watchpoints, and port breakpoints to work. | Low (~2-5% CPU overhead with <100 breakpoints) |
| `memorytracking` | OFF | Collect memory access counters and statistics. Records read/write/execute counts per address, identifies hotspots, and tracks access patterns. Required for `memcounters` command. | High (~40-50% CPU overhead, significant memory usage) |
| `debugmode` | OFF | Master debug mode switch. When enabled, activates all debug features (calltrace, breakpoints, memorytracking). When disabled, deactivates all debug features for maximum performance. Convenient toggle for entering/exiting debug sessions. | High (sum of all enabled debug features) |
| `sound` | ON | Enable/disable all sound generation. When disabled, skips all audio processing including AY chip emulation and beeper. Essential for headless mode, videowall, and turbo mode where audio is not needed. | Medium (~18% CPU savings when OFF) |
| `soundhq` | ON | High-quality DSP mode. When enabled, uses 192-tap FIR filters and 8x oversampling for audiophile-grade sound. When disabled, uses direct chip output for faster but lower quality audio. Only affects AY chip output. | Low-Medium (~15% CPU savings when OFF) |
| `screenhq` | ON | High-quality video mode. When enabled, uses per-t-state rendering for cycle-accurate "racing the beam" multicolor effects in demos. When disabled, uses batch 8-pixel rendering (25x faster) but breaks demo multicolor effects. | Very High (~25x faster screen rendering when OFF) |
| `recording` | OFF | Enable recording subsystem (video, audio, GIF capture). When enabled, the RecordingManager is active and ready for recording commands. When disabled, all recording API calls early-exit with zero overhead. Heavy functionality - enable explicitly when needed. | Varies (zero when OFF, depends on codec when recording) |
| `sharedmemory` | OFF | Export emulator memory via POSIX/Windows shared memory for external tool access. Enables real-time memory inspection by debuggers, analyzers, or visualization tools. Memory content preserved during enable/disable transitions. Alias: `shm`. | Low (startup overhead when enabled, minimal runtime impact) |

**Feature Dependencies**:

- `debugmode` is a master switch that controls other features
- When `debugmode on` is executed:
  - Enables `calltrace`
  - Enables `breakpoints`
  - Enables `memorytracking`
- When `debugmode off` is executed:
  - Disables all debug features
  - Restores normal execution speed

**Use Cases**:

1. **Enable Call Tracing**: Track function calls during execution
   ```
   feature calltrace on
   # Run program
   calltrace 50  # Show last 50 calls
   feature calltrace off
   ```

2. **Breakpoint Debugging**: Enable breakpoint system
   ```
   feature breakpoints on
   bp 0x8000        # Set breakpoint
   resume           # Run until breakpoint
   # ... debug ...
   feature breakpoints off  # Disable when done
   ```

3. **Memory Hotspot Analysis**: Find most frequently accessed memory
   ```
   feature memorytracking on
   # Run program for a while
   memcounters      # Show access statistics
   memcounters reset
   feature memorytracking off
   ```

4. **Full Debug Session**: Enable all debug features at once
   ```
   feature debugmode on     # Enable everything
   # Set breakpoints, run, analyze...
   feature debugmode off    # Disable everything, return to full speed
   ```

5. **Reset After Debugging**: Return to default configuration
   ```
   feature reset    # All features off (default state)
   ```

**Performance Notes**:

- **Production Use**: Keep all features OFF for maximum emulation speed
- **Light Debugging**: Enable only `breakpoints` for minimal overhead
- **Deep Analysis**: Enable `memorytracking` only when analyzing memory access patterns
- **Full Debug**: Use `debugmode on` when you need comprehensive debugging (accepts significant slowdown)
- **Feature Toggle Overhead**: Minimal (<1ms to enable/disable a feature)

**Output Format**:

```
> feature

--------------------------------------------------------------------------------
| Name           | State  | Description
--------------------------------------------------------------------------------
| debugmode      | off    | Master debug mode, enables/disables all debug features for performance
| breakpoints    | off    | Enable or disable breakpoint handling
| memorytracking | off    | Collect memory access counters and statistics
| calltrace      | off    | Collect call trace information for debugging
| sound          | on     | Enable or disable sound generation
| soundhq        | on     | Enable high-quality DSP (FIR filters, oversampling). Disable for low-quality/faster audio.
| screenhq       | on     | Enable per-t-state video for demo multicolor effects. Disable for 25x faster batch rendering.
| recording      | off    | Enable recording subsystem (video, audio, GIF capture).
| sharedmemory   | off    | Export emulator memory via shared memory for external tool access.
--------------------------------------------------------------------------------
```

**Notes**:
- Feature states persist across pause/resume but NOT across reset
- Emulator reset returns all features to default (off) state
- Feature changes are logged to debug output
- Python/Lua bindings provide programmatic feature control
- CLI: `CLIProcessor::HandleFeature()`
- WebAPI: `/api/v1/emulator/{id}/feature` endpoint
- Python: `emulator.feature(name, state)` or `emulator.feature.list()`
- Lua: `emu:feature(name, state)` or `emu:feature_list()`

### 6. System State Inspection

Commands to inspect the runtime hardware configuration and peripheral state of the selected emulator instance. All state inspection commands are organized under the `state` command hierarchy for consistency and discoverability.

**Command Structure**: `state <subsystem> [subcommand] [args]`

**Quick Reference**:

| Subsystem | Base Command | Purpose |
| :--- | :--- | :--- |
| `memory` | `state memory` | Memory configuration (ROM/RAM banking, paging) |
| `ports` | `state ports` | All available I/O ports (model-specific) |
| `port` | `state port <port>` | Specific I/O port values and monitoring |
| `sysvars` | `state sysvars` | ZX Spectrum system variables (0x5C00-0x5CB5) |
| `tape` | `state tape` | Tape device status and position |
| `disk` | `state disk [drive]` | Disk drive status and FDC state |
| `screen` | `state screen` | Screen configuration and video mode |
| `ula` | `state ula` | ULA chip state and timing |
| `audio` | `state audio` | Audio devices (beeper, AY chip) |

**Example Usage**:
```bash
# Show all memory information (ROM + RAM + paging)
state memory

# Check only RAM configuration
state memory ram
# or shorter alias
state ram

# Check only ROM configuration
state memory rom
# or shorter alias
state rom

# List all available ports for current model/config
state ports

# Read specific ULA port value
state port 0xFE

# Check BASIC program pointer
state sysvars PROG

# Show tape status
state tape

# Show disk catalog for drive A
state disk catalog A

# Show video mode
state screen mode

# Show brief AY chip overview
state audio ay

# Show detailed info for first AY chip
state audio ay 0

# Show detailed decoding for AY register 0 of chip 0
state audio ay 0 register 0

# Show beeper state
state audio beeper

# Show all audio channels mixer state
state audio channels
```

#### 6.1 Memory Configuration & Paging

Inspect current memory configuration including ROM/RAM bank mapping. Critical for understanding 128K memory models, clones with extended memory, and troubleshooting paging issues.

| Command | Aliases | Arguments | Description | Implementation Status |
| :--- | :--- | :--- | :--- | :--- |
| `state memory` | | | Display complete memory configuration:<br/>• **ROM**: Active ROM page, signature, version<br/>• **RAM**: All RAM banks mapping to Z80 address space<br/>• **Paging**: Current paging mode and lock state<br/>• **Contention**: Contended memory regions<br/>• **Model-specific**: Pentagon/Scorpion extended modes, +2A/+3 special modes<br/>Shows comprehensive memory layout in one view. | 🔮 Planned |
| `state memory ram` | `state ram` | | Show RAM bank mapping detail:<br/>• Bank 0 (0x0000-0x3FFF): RAM page or ROM<br/>• Bank 1 (0x4000-0x7FFF): RAM page<br/>• Bank 2 (0x8000-0xBFFF): RAM page<br/>• Bank 3 (0xC000-0xFFFF): RAM page<br/>Includes page numbers, read/write state, contention status.<br/>Shows Z80 address space to physical RAM page mapping. | 🔮 Planned |
| `state memory rom` | `state rom` | | Show ROM configuration:<br/>• Active ROM page (0 or 1 for 128K, 0-3 for +2A/+3)<br/>• ROM signature detection (48K ROM, 128K Editor, +2/+3 ROM)<br/>• ROM version and checksum (CRC32)<br/>• Custom ROM indicator if non-standard<br/>• ROM size and bank count | 🔮 Planned |
| `state memory history [N]` | | `[count]` | Show memory paging history - last N paging changes (port 0x7FFD/0x1FFD writes):<br/>• Timestamp (T-states)<br/>• Port address and value written<br/>• Resulting RAM/ROM configuration<br/>• PC address that made the change<br/>• Change type (RAM remap, ROM switch, paging lock)<br/>Default: last 10 changes. Useful for tracking paging bugs. | 🔮 Planned |

**Implementation Notes**:

**Implementation Notes**:

**128K Memory Paging** (Port 0x7FFD):
```
Bit 0-2: RAM page mapped to bank 3 (0xC000-0xFFFF) [0-7]
Bit 3:   Active screen (0 = screen 0 at 0x4000, 1 = screen 1 at 0x7000)
Bit 4:   ROM select (0 = 128K Editor, 1 = 48K BASIC)
Bit 5:   Paging lock (1 = disable further paging until reset)
Bit 6-7: Reserved
```

**Pentagon/Scorpion Extended Paging**:
- Pentagon: 128KB-512KB RAM with extended port 0x7FFD decoding
- Scorpion: 256KB RAM with additional port schemes

**+2A/+3 Special Modes** (Port 0x1FFD):
```
Bit 0:   Paging mode (0 = normal, 1 = special)
Bit 1:   All-RAM mode configuration
Bit 2:   Disk motor control
Bit 3:   Printer strobe
Bit 4-7: Reserved
```

**ROM Signature Detection**:
- CRC32 checksum of ROM contents
- Known ROM signatures database (48K, 128K, +2, +3, custom ROMs)
- Version detection from ROM header/copyright strings
- Custom ROM flagging for non-standard images

#### 6.2 I/O Port Inspection

Monitor and query I/O port values. Ports control all peripheral devices, memory banking, and hardware configuration. Available ports are **model-specific** and **configuration-dependent** - determined by the active clone model and connected peripherals.

| Command | Aliases | Arguments | Description | Implementation Status |
| :--- | :--- | :--- | :--- | :--- |
| `state ports` | | | List all available I/O ports for the current model and configuration:<br/>• **Port address** (hex and binary)<br/>• **Port decoding** (how hardware decodes the address - from PortDecoder)<br/>• **Device/Function** name<br/>• **Current value** (hex and binary)<br/>• **Value decoding** (bit-by-bit interpretation of value)<br/>• **Direction** (IN/OUT/BOTH)<br/>• **Last access** (timestamp and PC)<br/>Fetches port map from **PortMapper** which maintains model-specific port definitions based on:<br/>• Base model (48K, 128K, +2, +2A, +3, Pentagon, Scorpion, etc.)<br/>• Connected interfaces (Beta Disk, DivIDE, Kempston, etc.)<br/>• Active peripherals (AY chips, Covox, etc.)<br/>Port address decoding rules from **PortDecoder** (e.g., ULA = `xxxxxxx0`, Beta = `xxx11111`). | 🔮 Planned |
| `state port <port>` | | `<port-number>` | Read and display current value of specific I/O port. Port number in hex (0x00-0xFFFF). Shows:<br/>• **Port address** (hex/binary)<br/>• **Port decoding** (hardware address decoding logic from PortDecoder)<br/>• **Aliases** (other addresses that decode to same port)<br/>• **Raw port value** (hex/binary)<br/>• **Value decoding** (bit-by-bit with labels)<br/>• **Last IN/OUT operations** to this port<br/>• **Device/function name** from PortMapper | 🔮 Planned |
| `state port watch <port>` | | `<port-number>` | Monitor port for IN/OUT operations in real-time. Displays:<br/>• Direction (IN/OUT)<br/>• Value transferred (hex/binary/decoded)<br/>• PC address that accessed port<br/>• Timestamp (T-states)<br/>Updates continuously until stopped. | 🔮 Planned |
| `state port history <port> [N]` | | `<port-number> [count]` | Show last N access operations to specific port. Default: 20. Shows:<br/>• Timestamp (T-states)<br/>• Direction (IN/OUT)<br/>• Value (hex/binary/decoded)<br/>• PC address<br/>Useful for tracing peripheral communication patterns. | 🔮 Planned |
| `state port decode <port> <value>` | | `<port-number> <value>` | Decode arbitrary port value bit-by-bit with meaning (doesn't read from emulator):<br/>**Example: state port decode 0xFE 0x12**<br/>• Bit 0-2: Border color = 2 (red)<br/>• Bit 3: MIC output = 0<br/>• Bit 4: Beeper output = 1<br/>• Bit 5-7: Unused<br/>Provides human-readable interpretation. Useful for understanding port values in dumps/logs. | 🔮 Planned |

**PortMapper & PortDecoder Architecture**:

The **PortMapper** is a central registry that maintains port definitions, working together with **PortDecoder** which handles hardware-level address decoding logic.

**PortMapper** maintains port definitions based on:

1. **Base Model Ports**: Standard ports for each Spectrum model
   - 48K: ULA (0xFE), Kempston (0xDF if enabled)
   - 128K: +ULA, +Paging (0x7FFD), +AY (0xFFFD/0xBFFD)
   - +2A/+3: +Special (0x1FFD), +FDC (0x2FFD, 0x3FFD)
   - Pentagon: +Extended paging variants
   - Scorpion: +Model-specific ports

2. **Interface Ports**: Dynamically added when interfaces are attached
   - Beta Disk: WD1793 ports (0x1F, 0x3F, 0x5F, 0x7F, 0xFF)
   - DivIDE/DivMMC: IDE ports (0xE3, 0xE7, 0xEB, etc.)
   - Kempston Mouse: Mouse ports (0xFADF, 0xFBDF, 0xFFDF)
   - Multiface: Control ports (varies by model)

3. **Peripheral Ports**: Added when peripherals are enabled
   - TurboSound (dual AY): Additional AY ports (0xBFFD, 0xFFFD variants)
   - Covox: DAC ports (0xFB, 0xDF variants)
   - General Sound: Sound card ports (0xBB3B-0xBB3F)

4. **Clone-Specific Ports**: Unique to specific clones
   - Pentagon: Port 0x7FFD extensions, EFF7 for printer
   - Scorpion: Extended memory ports
   - TSConf: Modern graphics and DMA ports

**PortDecoder** handles hardware address decoding logic:

Different Spectrum models and interfaces decode port addresses differently - some use full 16-bit decoding, others use partial decoding where only specific bits matter. The **PortDecoder** provides this information for each port.

**Port Address Decoding Examples**:

| Port | Device | Decode Pattern | Description | Aliases |
| :--- | :--- | :--- | :--- | :--- |
| 0xFE | ULA (48K) | `xxxxxxx0` | Only bit 0 matters (must be 0) | 0x00, 0x02, 0x04, ..., 0xFE (all even ports) |
| 0x7FFD | 128K Paging | `01xxxxxxxxxxxxxx` | Bits 15-14 = 01, others don't matter | 0x7FFD, 0x7FFC, 0x7EFD, etc. |
| 0x1FFD | +2A/+3 Special | `0001xxxxxxxxxxxx` | Bits 15-13 = 000, bit 12 = 1 | 0x1FFD, 0x1FFC, 0x1EFD, etc. |
| 0xFFFD | AY Register | `11xxxxxxxxxxxx01` | Bits 15-14 = 11, bits 1-0 = 01 | 0xFFFD, 0xFEFD, 0xFDFD, etc. |
| 0xBFFD | AY Data | `10xxxxxxxxxxxx01` | Bits 15-14 = 10, bits 1-0 = 01 | 0xBFFD, 0xBEFD, 0xBDFD, etc. |
| 0x1F | Beta Disk System | `xxx11111` | Bits 7-0 = 0x1F exactly | Only 0x1F, 0x011F, 0x021F, etc. (low byte fixed) |
| 0x3F | Beta Disk Track | `xxx11111` | Bits 7-0 = 0x3F exactly | Only 0x3F, 0x013F, 0x023F, etc. |
| 0xFF | Beta Disk Command | `xxx11111` | Bits 7-0 = 0xFF exactly | Only 0xFF, 0x01FF, 0x02FF, etc. |
| 0xDF | Kempston Joystick | `xx011111` | Bits 7-0 = 0xDF, upper bits vary | 0x00DF, 0x01DF, ..., 0xFFDF |

**Decoding Logic Types**:

1. **Partial Decoding** (Most Common):
   - Only specific bits checked, others ignored
   - Multiple addresses map to same port (aliases)
   - Examples: ULA (bit 0 only), 128K paging (bits 15-14)

2. **Full Decoding**:
   - All 16 bits matter
   - Single address per port
   - Examples: Some modern interfaces (DivIDE specific ports)

3. **Low-Byte Decoding**:
   - Only bits 7-0 matter, high byte ignored
   - Examples: Beta Disk ports, Kempston

4. **Pattern Matching**:
   - Complex bit patterns
   - Examples: AY chips (specific bit combinations)

**PortDecoder Methods**:

```cpp
class PortDecoder
{
public:
    struct DecodingInfo
    {
        uint16_t portAddress;           // Primary port address
        uint16_t decodeMask;            // Bits that matter (1 = checked, 0 = ignored)
        uint16_t decodePattern;         // Expected pattern for checked bits
        std::string decodeString;       // Human-readable (e.g., "xxxxxxx0")
        std::vector<uint16_t> aliases;  // Other addresses that decode to this port
    };
    
    // Get decoding info for a port
    DecodingInfo GetPortDecoding(uint16_t port, const std::string& modelId);
    
    // Check if address maps to this port
    bool AddressMatchesPort(uint16_t address, uint16_t port, const std::string& modelId);
    
    // Get all addresses that decode to this port (up to limit)
    std::vector<uint16_t> GetPortAliases(uint16_t port, const std::string& modelId, size_t maxAliases = 10);
};
```

**Example Port List Output** (128K with Beta Disk):

```
Port    Decoding        Device          Dir     Value    Value Decoded
------  --------------  --------------  ------  -------  -----------------------------
0xFE    xxxxxxx0        ULA             IN/OUT  0x12     Border:2 MIC:0 EAR:1 Beeper:1
        (bit 0 = 0)                                      Tape IN:1
        Aliases: all even ports (0x00, 0x02, ..., 0xFE)

0x7FFD  01xxxxxx        Paging (128K)   OUT     0x10     RAM:0 Screen:0 ROM:1 Lock:0
        (bits 15-14=01)
        Aliases: 0x7FFD, 0x7FFC, 0x7EFD, ...

0x1F    xxx11111        Beta FDC Sys    IN      0x80     DRQ:1 INTRQ:0 Drive:A Disk:1
        (bits 7-0 = 0x1F)
        Aliases: 0x001F, 0x011F, 0x021F, ...

0x3F    xxx11111        Beta FDC Track  IN/OUT  0x00     Track: 0
        (bits 7-0 = 0x3F)

0x5F    xxx11111        Beta FDC Sector IN/OUT  0x01     Sector: 1
        (bits 7-0 = 0x5F)

0x7F    xxx11111        Beta FDC Data   IN/OUT  0x00     Data: 0x00
        (bits 7-0 = 0x7F)

0xFF    xxx11111        Beta FDC Cmd    OUT     0x00     Idle (no command)
        (bits 7-0 = 0xFF)

0xBFFD  10xxxxxx        AY Data         IN/OUT  0x00     Register 0 data: 0x00
        xxxx01          (Channel A Period Fine)
        (bits 15-14=10, 1-0=01)

0xFFFD  11xxxxxx        AY Register     OUT     0x00     Selected: R0
        xxxx01
        (bits 15-14=11, 1-0=01)

0xDF    xx011111        Kempston Joy    IN      0x00     Fire:0 Up:0 Down:0 Left:0
        (bits 7-0 = 0xDF)                                Right:0
```

**Enhanced `state port <port>` Output Example**:

```bash
> state port 0xFE

Port: 0xFE (ULA)
================

Address Decoding (PortDecoder):
  Pattern: xxxxxxx0 (only bit 0 checked, must be 0)
  Decode Mask: 0x0001
  Decode Pattern: 0x0000
  Full Match: Any address with bit 0 = 0
  
Port Aliases (other addresses that decode to this port):
  0x0000, 0x0002, 0x0004, 0x0006, ..., 0x00FE
  0x0100, 0x0102, 0x0104, 0x0106, ..., 0x01FE
  ... (all even addresses from 0x0000 to 0xFFFE)
  Total: 32768 possible addresses

Device: ULA (Uncommitted Logic Array)
Direction: IN/OUT
Model: 48K/128K/+2/+2A/+3 (all models)

Current Value: 0x12 (0b00010010)
================================

Bit 7:   1   (IN)  Tape input (EAR) - HIGH
Bit 6:   0   (IN)  Reserved (always 1 on read)
Bit 5:   0   (IN)  Keyboard data (depends on selected row)
Bit 4:   1   (OUT) Beeper output - ON
Bit 3:   0   (OUT) MIC output (tape save) - OFF
Bit 2-0: 010 (OUT) Border color - RED (2)

Decoded:
  Border: RED (2)
  Beeper: ON
  MIC: OFF
  Tape Input: HIGH

Last Access:
  Direction: IN
  Value: 0x12
  PC: 0x053F (ROM tape loading routine)
  T-state: 1234567
  Time: 24.691 seconds ago
```

**Implementation Notes**:

**PortMapper vs PortDecoder Roles**:
- **PortMapper**: Tracks *which ports exist* for current model/configuration (registry of available ports)
- **PortDecoder**: Knows *how hardware decodes addresses* for each port (address pattern matching logic)
- Together they provide: "Port 0xFE exists (PortMapper) and responds to pattern `xxxxxxx0` (PortDecoder)"

**Dynamic Behavior**:
- `state ports` output is **dynamic** - reflects current hardware configuration
- Port availability changes when interfaces are connected/disconnected
- Decoding is context-aware (e.g., Beta Disk ports only shown when interface active)
- PortMapper validates port addresses before access
- Custom interfaces can register their ports with PortMapper at runtime
- PortDecoder provides model-specific address decoding patterns for each port

**Address Aliases**:
- Many ports respond to multiple addresses due to partial decoding
- `state port <port>` shows aliases to help understand hardware behavior
- Useful for debugging software that uses non-standard port addresses
- Example: Writing to 0x00FE, 0x02FE, or 0xFFFE all affect the ULA border color

#### 6.3 System Variables

Inspect ZX Spectrum system variables area. These variables control BASIC, screen, I/O, and system state.

| Command | Aliases | Arguments | Description | Implementation Status |
| :--- | :--- | :--- | :--- | :--- |
| `state sysvars` | `state vars` | | Display all system variables (23552-23733 / 0x5C00-0x5CB5) with decoded values:<br/>• BASIC program pointers<br/>• Screen/cursor position<br/>• System flags<br/>• I/O channel info<br/>• Error codes<br/>Formatted table with addresses, names, raw values, and interpretations. | 🔮 Planned |
| `state sysvars <name>` | `state vars <name>` | `<variable-name>` | Display specific system variable by name:<br/>**Example: `state sysvars PROG`**<br/>Shows address, value, and meaning. Supports all standard 48K/128K variable names. | 🔮 Planned |
| `state sysvars addr <addr>` | | `<address>` | Display system variable at address with interpretation. Automatically identifies variable if in system area. | 🔮 Planned |
| `state sysvars basic` | | | Show BASIC-related system variables:<br/>• PROG - BASIC program start<br/>• VARS - Variables area<br/>• E_LINE - Edit line<br/>• STKEND - Stack end<br/>• Memory map visualization | 🔮 Planned |
| `state sysvars screen` | | | Show screen-related variables:<br/>• ATTR_T/P - Temporary/permanent attributes<br/>• P_FLAG - Print flags<br/>• DF_CC/DF_SZ - Display file counters<br/>• S_POSN - Screen position<br/>• SCR_CT - Scroll counter | 🔮 Planned |
| `state sysvars io` | | | Show I/O-related variables:<br/>• BORDCR - Border color<br/>• FRAMES - Frame counter (uptime in 1/50 sec)<br/>• LAST_K - Last key pressed<br/>• FLAGS/FLAGS2 - System flags<br/>• Tape/Disk state variables | 🔮 Planned |
| `state sysvars watch <name>` | | `<variable-name>` | Monitor system variable for changes. Updates displayed when value changes. Useful for tracking FRAMES counter, key presses, etc. | 🔮 Planned |

**Key System Variables** (48K/128K):

| Address | Name | Size | Description |
| :--- | :--- | :--- | :--- |
| 0x5C00 | KSTATE | 8 | Keyboard state (repeated key info) |
| 0x5C08 | LAST_K | 1 | Last key pressed |
| 0x5C09 | REPDEL | 1 | Repeat delay |
| 0x5C0A | REPPER | 1 | Repeat period |
| 0x5C3B | FRAMES | 3 | Frame counter (50Hz, 24-bit) |
| 0x5C48 | BORDCR | 1 | Border color |
| 0x5C4A | P_FLAG | 1 | Print flags |
| 0x5C4B | VARS | 2 | Variables area address |
| 0x5C4D | DEST | 2 | Destination for LOAD/VERIFY |
| 0x5C4F | CHANS | 2 | Channel data address |
| 0x5C51 | CURCHL | 2 | Current channel address |
| 0x5C53 | PROG | 2 | BASIC program start address |
| 0x5C55 | NXTLIN | 2 | Next line address |
| 0x5C57 | DATADD | 2 | Terminator of last DATA item |
| 0x5C59 | E_LINE | 2 | Edit line address |
| 0x5C5B | K_CUR | 2 | Cursor address |
| 0x5C5D | CH_ADD | 2 | Character address |
| 0x5C5F | X_PTR | 2 | Syntax error address |
| 0x5C61 | STKBOT | 2 | Calculator stack bottom |
| 0x5C63 | STKEND | 2 | Calculator stack end |
| 0x5C65 | BREG | 1 | Calculator B register |
| 0x5C6A | FRAMES (Duplicate) | 3 | Alternative reference |
| 0x5C7F | P_RAMT | 2 | Printer position (128K) |
| 0x5C81 | NMIADD | 2 | NMI routine address |

#### 6.4 Tape Device State

Inspect tape device status, position, and current operation. Essential for debugging tape loading issues.

| Command | Aliases | Arguments | Description | Implementation Status |
| :--- | :--- | :--- | :--- | :--- |
| `state tape` | | | Show tape device status:<br/>• Tape inserted: filename<br/>• Format: TAP, TZX, CSW<br/>• State: Stopped, Playing, Recording<br/>• Position: Block N of M, byte offset<br/>• Current block info (header/data, length)<br/>• Playback speed (if turbo) | 🔮 Planned |
| `state tape blocks` | | | List all blocks in current tape:<br/>• Block number<br/>• Type (Header/Data/Turbo/Pure Tone)<br/>• Length in bytes<br/>• Name (if header block)<br/>• Checksum status<br/>Highlights current block. | 🔮 Planned |
| `state tape block <N>` | | `<block-number>` | Show detailed info about specific block:<br/>• Block type and flags<br/>• Header details (if applicable)<br/>• Data preview (hex dump)<br/>• Timing parameters (for TZX)<br/>• Checksum verification | 🔮 Planned |
| `state tape position` | | | Show precise tape position:<br/>• Block index<br/>• Byte offset within block<br/>• Time position (for CSW)<br/>• Pilot/sync/data phase<br/>• Edge counter | 🔮 Planned |

#### 6.5 Disk Device State

Inspect disk drive status, disk geometry, and current operations. Supports Beta Disk, +3, and modern interfaces.

| Command | Aliases | Arguments | Description | Implementation Status |
| :--- | :--- | :--- | :--- | :--- |
| `state disk [drive]` | | `[A\|B\|C\|D]` | Show disk status for specified drive (or all drives):<br/>• Disk inserted: filename<br/>• Format: TRD, SCL, FDI, UDI, DSK<br/>• Geometry: tracks, sides, sectors per track<br/>• Write protection status<br/>• Current track/sector position<br/>• Motor state<br/>• FDC busy/idle state | 🔮 Planned |
| `state disk geometry [drive]` | | `[A\|B\|C\|D]` | Show disk geometry details:<br/>• Total tracks<br/>• Heads (sides)<br/>• Sectors per track<br/>• Bytes per sector<br/>• Total capacity<br/>• Format identification | 🔮 Planned |
| `state disk catalog [drive]` | `state disk dir` | `[A\|B\|C\|D]` | Show disk catalog (file list) if DOS detected:<br/>• **TR-DOS**: File name, type, length, start address<br/>• **+3DOS**: CP/M directory listing<br/>• **ESXDOS**: FAT directory listing<br/>Automatically detects DOS type. | 🔮 Planned |
| `state disk fdc` | | | Show FDC (Floppy Disk Controller) state:<br/>• Controller type (WD1793, µPD765A)<br/>• Current command<br/>• Status register<br/>• Track/sector registers<br/>• DMA state (if applicable) | 🔮 Planned |
| `state disk dos` | | | Detect and identify disk operating system:<br/>• DOS type: TR-DOS, +3DOS, G+DOS, ESXDOS, etc.<br/>• Version<br/>• Boot sector signature<br/>• System tracks<br/>• References detected DOS type documentation | 🔮 Planned |

#### 6.6 Screen Configuration

Inspect active screen buffer, video mode, and rendering state. Handles 48K single screen, 128K dual screen (switchable via port 0x7FFD), and clone-specific modes.

| Command | Aliases | Arguments | Description | Implementation Status |
| :--- | :--- | :--- | :--- | :--- |
| `state screen` | | | Show screen configuration (brief mode by default):<br/>• Model name (48K/128K/Pentagon/+3)<br/>• Video mode (Standard, Timex, etc.)<br/>• Active screen number and RAM page (for 128K models)<br/>• Border color<br/>• Hint to use `verbose` for detailed info | ✅ Implemented |
| `state screen verbose` | | | Show detailed screen configuration:<br/>• **48K**: Screen at RAM page 5, offset 0x0000 (Z80 access: 0x4000-0x7FFF)<br/>• **128K**: Active screen (0 or 1 via port 0x7FFD bit 3), shows both screen locations<br/>  - Screen 0 (normal): ULA reads from RAM page 5, offset 0x0000<br/>  - Screen 1 (shadow): ULA reads from RAM page 7, offset 0x0000 (always, regardless of Z80 mapping)<br/>• Physical RAM page locations (where ULA reads from)<br/>• Z80 address space mapping (where CPU can access screen)<br/>• Port 0x7FFD bit values and decoding<br/>• Contention state<br/>• Full memory layout for both screens | ✅ Implemented |
| `state screen mode` | | | Show detailed video mode information for current model:<br/>• **Standard Spectrum**: 256×192, 2 colors per 8×8 attribute block<br/>• **Timex/TC2068**: Hi-Res (512×192 mono), Hi-Color (256×192, 8×1 attrs), Dual Screen<br/>• **Pentagon**: GigaScreen (dual-screen interlace), SuperHiRes (512×384 on modified models)<br/>• **SAM Coupé**: Modes 1-4 (256×192 to 256×192×16 colors per pixel)<br/>• **ATM Turbo**: 320×200, 640×200, 16-color modes<br/>• **ZX Next**: Layer 2 (256×192×256 colors), 640×256, Timex compatibility<br/>• **eLeMeNt ZX**: HGFX modes (720×546, 512×384, 256-color)<br/>Shows: resolution, color depth, attribute block size, memory layout, active layers | ✅ Implemented |
| `state screen flash` | | | Show flash state and counter:<br/>• Flash phase (normal/inverted)<br/>• Flash counter (frames until toggle, 16 frame cycle)<br/>• Frame position in flash cycle<br/>Useful for timing-sensitive flash effects in demos | ✅ Implemented |

**Brief vs Verbose Mode**:

The `state screen` command supports two output modes:

1. **Brief Mode (Default)**: 
   - Activated by: `state screen`
   - Shows: Essential information only (model, video mode, active screen, border color)
   - Use case: Quick checks, scripting, when you need just the active screen info
   - Output: 5-6 lines

2. **Verbose Mode**:
   - Activated by: `state screen verbose`
   - Shows: Complete details including both screen buffers (for 128K), physical RAM locations, Z80 address mappings, port decoding, contention states
   - Use case: Deep debugging, understanding dual-screen setups, analyzing memory configurations
   - Output: 20+ lines with full hardware details

**WebAPI**: Add `?verbose=true` query parameter to get verbose output:
- Brief: `GET /api/v1/emulator/test/state/screen`
- Verbose: `GET /api/v1/emulator/test/state/screen?verbose=true`

**128K Screen Switching**:

The 128K Spectrum models can switch between two screen buffers by writing to **port 0x7FFD bit 3**:

- **Bit 3 = 0**: ULA displays **Screen 0** (normal screen)
  - ULA reads from **RAM page 5**, offset 0x0000-0x1FFF
  - Pixel data: page 5 offset 0x0000-0x17FF (6144 bytes)
  - Attributes: page 5 offset 0x1800-0x1AFF (768 bytes)
  - For Z80 CPU access: typically mapped at 0x4000-0x7FFF (bank 1)

- **Bit 3 = 1**: ULA displays **Screen 1** (shadow screen)
  - ULA reads from **RAM page 7**, offset 0x0000-0x1FFF
  - Pixel data: page 7 offset 0x0000-0x17FF (6144 bytes)
  - Attributes: page 7 offset 0x1800-0x1AFF (768 bytes)
  - **Important**: ULA always renders from page 7 offset 0x0000, regardless of whether page 7 is mapped in Z80 address space or not

**Key Hardware Detail**:

The ULA has **direct access to physical RAM pages** and does NOT read through the Z80's address space mapping. This means:

- Screen 0 is always rendered from RAM page 5, offset 0x0000
- Screen 1 is always rendered from RAM page 7, offset 0x0000
- The ULA reads the active screen independently of which pages are mapped where in the Z80's 64KB address space
- The Z80 can modify screen data even if that page is not currently mapped (by temporarily paging it in)

**Z80 Access to Screen Buffers**:

To modify screen data, the Z80 must have the corresponding RAM page mapped into its address space:

| Screen | Physical Location | Z80 Access | Notes |
| :--- | :--- | :--- | :--- |
| Screen 0 | RAM page 5, offset 0x0000 | Typically at 0x4000-0x7FFF (bank 1) | Standard configuration, always accessible |
| Screen 1 | RAM page 7, offset 0x0000 | Depends on port 0x7FFD bits 0-2 | Must be paged into bank 3 (0xC000-0xFFFF) for Z80 access |

**Example**: When port 0x7FFD = 0x0F (binary 00001111):
- Bits 0-2 = 111: RAM page 7 mapped to bank 3 (0xC000-0xFFFF)
- Bit 3 = 1: ULA displays Screen 1 from page 7
- Result: Z80 can write to screen at 0xC000-0xDFFF, ULA displays from same page

**Common Usage in Demos**:

Demos frequently use screen switching for:
1. **Double Buffering**: Draw to shadow screen (page 7) while normal screen is displayed, then switch instantly
   - Page 7 must be mapped into Z80 address space (typically bank 3 at 0xC000) for CPU to draw
   - ULA displays from page 7 offset 0x0000 when port 0x7FFD bit 3 = 1
2. **Page Flipping**: Alternate between screens for smooth animation (50Hz flicker-free)
   - No tearing or flickering since switch happens during vertical blank
3. **Split-Screen Effects**: Switch screen mid-frame by writing to 0x7FFD during display
   - Can show different content in top and bottom halves
4. **Prerendering**: Prepare complex graphics on shadow screen before revealing
   - Draw to page 7 (via 0xC000-0xDFFF when paged in) while page 5 is displayed
   - Instant switch when ready by setting bit 3 of port 0x7FFD

**Important**: To modify shadow screen (page 7), it must be paged into Z80 address space. The ULA can display it regardless of where it's mapped.

**Enhanced Graphics Modes (Clone-Specific)**:

Several ZX Spectrum clones introduced enhanced graphics modes to overcome the original's limitations (256×192 resolution and "attribute clash" from 8×8 color blocks).

**Timex Sinclair 2068 / TC 2048 / TC 2068 / UK 2086** (USA/Portuguese/Polish/UK variants):
- **Hi-Res Mode**: 512×192 monochrome (for 64-column or 80-column text)
- **Hi-Color Mode**: 256×192 with 8×1 attribute blocks (reduced from 8×8, less attribute clash)
- **Dual Screen Mode**: Two 256×192 screens in memory for double-buffering
- **Control**: Extended port 0xFF controls mode selection

**SAM Coupé** (1989 "super-clone"):
- **Mode 1**: 256×192 standard Spectrum-compatible
- **Mode 2**: 256×192 with 8×1 attributes (similar to Timex Hi-Color)
- **Mode 3**: 512×192 with 4 colors per line
- **Mode 4**: 256×192 with each pixel from 16 colors (no attribute blocks)
- **Control**: VMPR register (port 0xFC) selects mode

**Pentagon** (Russian clone series):
- **Standard**: 256×192 Spectrum-compatible mode
- **GigaScreen**: Hardware modification combining two screens with interlacing for more colors (non-flickering dual-screen effect)
- **SuperHiRes**: 512×384 on modified Pentagon models (rare hardware mod)
- **Note**: Pentagon does NOT have Timex modes despite being sometimes confused with them

**ATM Turbo** (Russian high-performance clone):
- **320×200**: 16-color "chunky" pixels
- **640×200**: High-resolution mode
- **Text modes**: Enhanced text display modes
- **Extended palette**: Hardware palette control for enhanced colors

**Scorpion 256** (Russian 128K clone):
- Mostly standard 128K modes
- Advanced hardware expansions available for enhanced video
- Compatible with many Pentagon mods

**ZX Spectrum Next** (Modern FPGA-based, 2017+):
- **Timex Compatibility**: Full support for Timex Hi-Res/Hi-Color modes
- **Pentagon Compatibility**: GigaScreen and other Pentagon modes
- **Layer 2**: 256×192 with 256 colors per pixel (8-bit color)
- **640×256**: High-resolution mode
- **Sprites**: Hardware sprites (128 sprites, 16×16 pixels)
- **Tilemap**: Hardware tilemap layer
- **Control**: Extended Next registers and ports

**eLeMeNt ZX / MB03+ Interface** (Modern):
- **HGFX modes**: Multiple high-resolution graphics modes
- **720×546**: Full PAL resolution
- **512×384**: SuperHiRes mode
- **256-color modes**: True color modes with 256-color palette

**Bobo64** (Rare Czech clone):
- **256×256**: Graphics with 8×1 attributes
- **512×256**: High-resolution mode

**TSConf** (ZX Evolution):
- **Modern graphics modes**: Up to 360×288 resolution
- **Hardware acceleration**: Sprites and blitter
- **Extended palette**: True color support

**state screen mode Detection**:

The `state screen mode` command will:
1. **Auto-detect** current model from emulator configuration
2. **Read mode control ports** (0xFF for Timex, 0xFC for SAM, model-specific for others)
3. **Display active mode** with full specifications:
   - Resolution (width × height)
   - Color depth (colors per pixel/attribute block)
   - Attribute block size (8×8, 8×1, per-pixel)
   - Memory layout (page usage, address ranges)
   - Active layers (for multi-layer systems like Next)
4. **Show compatibility** with other modes if applicable

**state screen mode Output Examples**:

**Standard 128K**:
```
> state screen mode

Model: ZX Spectrum 128K
Video Mode: Standard
============================================
Resolution:      256 × 192 pixels
Color Depth:     2 colors per attribute block
Attribute Size:  8 × 8 pixels
Memory Layout:
  Pixel Data:    6144 bytes (32 lines × 192 pixels)
  Attributes:    768 bytes (32 × 24 blocks)
  Total:         6912 bytes per screen
Active Screen:   Screen 0 (RAM page 5)
Compatibility:   48K/128K/+2/+2A/+3 standard
```

**Timex Hi-Color Mode**:
```
> state screen mode

Model: Timex Sinclair 2068
Video Mode: Hi-Color (Extended Color Mode)
============================================
Resolution:      256 × 192 pixels
Color Depth:     8 colors per attribute line
Attribute Size:  8 × 1 pixels (reduced from 8×8)
Memory Layout:
  Pixel Data:    6144 bytes
  Attributes:    6144 bytes (one byte per line instead of per block)
  Total:         12288 bytes
Port 0xFF:       0x02 (Hi-Color mode enabled)
Advantages:      Significantly reduced attribute clash
Compatibility:   Timex/TC2068 family only
```

**Pentagon GigaScreen**:
```
> state screen mode

Model: Pentagon 128K
Video Mode: GigaScreen (Dual-Screen Interlace)
============================================
Resolution:      256 × 192 pixels (per frame)
Effective:       256 × 192 × enhanced colors
Color Depth:     Interlaced dual-screen for more colors
Frame Rate:      25 Hz effective (50 Hz alternating)
Memory Layout:
  Screen 0:      RAM page 5, offset 0x0000 (6912 bytes)
  Screen 1:      RAM page 7, offset 0x0000 (6912 bytes)
  Total:         13824 bytes (both screens)
Technique:       Rapid alternation between screens (port 0x7FFD bit 3)
                 Creates non-flickering multicolor effect
Advantages:      More colors without attribute clash
Compatibility:   Pentagon hardware/software specific
```

**ZX Spectrum Next Layer 2**:
```
> state screen mode

Model: ZX Spectrum Next
Video Mode: Layer 2 (256-Color Bitmap)
============================================
Resolution:      256 × 192 pixels
Color Depth:     256 colors per pixel (8-bit color)
Attribute Size:  1 × 1 (per-pixel color)
Memory Layout:
  Pixel Data:    49152 bytes (256 × 192 × 1 byte)
  Banks Used:    3 × 16KB banks (pages 8, 9, 10)
  Palette:       256-entry palette, 9-bit RGB (512 colors)
Active Layers:
  Layer 2:       ENABLED (this layer)
  ULA Layer:     Enabled (below Layer 2)
  Sprites:       Enabled (128 hardware sprites available)
  Tilemap:       Disabled
Port 0x123B:     0x03 (Layer 2 enabled, visible)
Advantages:      No attribute clash, 256 simultaneous colors
Compatibility:   ZX Spectrum Next only (2017+ models)
```

**SAM Coupé Mode 4**:
```
> state screen mode

Model: SAM Coupé
Video Mode: Mode 4 (16-Color Per-Pixel)
============================================
Resolution:      256 × 192 pixels
Color Depth:     16 colors per pixel (4-bit color)
Attribute Size:  1 × 1 (no attribute blocks)
Memory Layout:
  Pixel Data:    49152 bytes (2 pixels per byte, 4 bits each)
  Palette:       16-entry palette from 128 colors
VMPR Register:   0x?? (Mode 4 selected)
Advantages:      No attribute clash, free color choice per pixel
Compatibility:   SAM Coupé only
```

**Port 0x7FFD Screen Control**:

```
Port 0x7FFD (Memory Paging and Screen Control):
  Bit 3: Screen select
    0 = Display Screen 0 (RAM page 5)
    1 = Display Screen 1 (RAM page 7)

Example:
  OUT (0x7FFD), 0x08  ; Switch to Screen 1 (bit 3 = 1)
  OUT (0x7FFD), 0x00  ; Switch to Screen 0 (bit 3 = 0)
```

**state screen Output Examples**:

**Brief Mode (Default)**:

```
> state screen

Screen Configuration (Brief)
============================

Model:        ZX Spectrum 128K
Video Mode:   Standard (256×192, 2 colors per 8×8 block)
Active Screen: Screen 0 (RAM page 5)
Border Color: 7

Use 'state screen verbose' for detailed information
```

**Verbose Mode**:

```
> state screen verbose

Screen Configuration (Verbose)
==============================

Model: ZX Spectrum 128K
Active Screen: Screen 0 (normal)

Screen 0 (Normal - RAM Page 5):
  Physical Location: RAM page 5, offset 0x0000-0x1FFF
  Pixel Data:        Page 5 offset 0x0000-0x17FF (6144 bytes)
  Attributes:        Page 5 offset 0x1800-0x1AFF (768 bytes)
  Z80 Access:        0x4000-0x7FFF (bank 1 - always accessible)
  ULA Status:        CURRENTLY DISPLAYED
  Contention:        Active when accessed via 0x4000-0x7FFF

Screen 1 (Shadow - RAM Page 7):
  Physical Location: RAM page 7, offset 0x0000-0x1FFF
  Pixel Data:        Page 7 offset 0x0000-0x17FF (6144 bytes)
  Attributes:        Page 7 offset 0x1800-0x1AFF (768 bytes)
  Z80 Access:        Not currently mapped (page 5 at bank 3)
  ULA Status:        Not displayed
  Contention:        N/A (not mapped)

Port 0x7FFD:  0x05 (bin: 00000101)
  Bits 0-2: 5 (RAM page 5 mapped to bank 3)
  Bit 3:    0 (ULA displays Screen 0)
  Bit 4:    0 (ROM: 128K Editor)
  Bit 5:    0 (Paging enabled)

Note: ULA reads screen from physical RAM page, independent of Z80 address mapping.

Display Mode: Standard (256×192, 2 colors per 8×8)
Border Color: 7
```

#### 6.7 ULA State

Inspect ULA (Uncommitted Logic Array) state - the main chip controlling video, contention, and I/O timing.

| Command | Aliases | Arguments | Description | Implementation Status |
| :--- | :--- | :--- | :--- | :--- |
| `state ula` | | | Show ULA state:<br/>• Border color (3-bit)<br/>• MIC output (tape save)<br/>• Beeper output<br/>• Flash phase<br/>• Contention state<br/>• Current scanline<br/>• Horizontal position in line | 🔮 Planned |
| `state ula timing` | | | Show ULA timing information:<br/>• Frame T-states (48K: 69888, 128K: 70908)<br/>• Current T-state in frame<br/>• Scanline timing (224 T-states/line)<br/>• Contention pattern<br/>• Next interrupt timing | 🔮 Planned |
| `state ula contention` | | | Show memory contention details:<br/>• Contended memory range (0x4000-0x7FFF)<br/>• Current contention state (enabled/disabled)<br/>• T-states added by contention this frame<br/>• Contention pattern (6:5 snowflake) | 🔮 Planned |

**Implementation Notes**:

**Memory Contention**:
- Occurs when CPU accesses screen memory (0x4000-0x7FFF) during active display
- Pattern: 6 T-states contended, 5 T-states non-contended (repeats)
- Only affects specific T-states within each scanline
- Critical for accurate timing of demos and loaders

**ULA Port 0xFE**:
```
OUT:
  Bit 0-2: Border color (0-7)
  Bit 3:   MIC output (tape save)
  Bit 4:   Beeper output
  Bit 5-7: Not used

IN:
  Bit 0-4: Keyboard row data (active low)
  Bit 6:   Tape input (EAR)
  Bit 7:   Not used (always 1)
```

#### 6.8 Audio Device State

Inspect audio hardware state including beeper, AY-3-8912 PSG, General Sound, and Covox devices.

| Command | Aliases | Arguments | Description | Implementation Status |
| :--- | :--- | :--- | :--- | :--- |
| `state audio beeper` | | | Show beeper state:<br/>• Current output level (0/1)<br/>• Last toggle timestamp<br/>• Toggle frequency estimate<br/>• Output waveform visualization<br/>• Whether sound was played since reset via this device | 🔮 Planned |
| `state audio ay` | | | Show brief state for all AY chips available:<br/>• Number of AY chips (1 = standard, 2 = TurboSound, 3 = ZX Next)<br/>• Basic info for each AY: type, active channels, envelope state<br/>• Whether sound was played since reset via each device | 🔮 Planned |
| `state audio ay <index>` | | `<chip-index>` | Show detailed information about selected AY chip (0-based indexing):<br/>• Chip index (0=first chip, 1=second chip for TurboSound)<br/>• Chip type (AY-3-8912, YM2149, etc.)<br/>• All register values (0-15) with decoding<br/>• Channel A/B/C: frequency, volume, mixer state<br/>• Envelope shape, period, and current phase<br/>• Noise period and LFSR state<br/>• I/O ports A/B values and direction<br/>• Whether sound was played since reset via this device | 🔮 Planned |
| `state audio ay <chip> register <N>` | | `<chip-index> <register>` | Show specific AY register (0-15) of specified chip with full decoding and frequency calculations:<br/>**Example: `state audio ay 0 register 0`**<br/>• Register 0: Channel A fine period = 0x123<br/>• Frequency: 432 Hz<br/>• Note: A4 (440 Hz approximately)<br/>• Bit-by-bit decoding with meaning for each register type | 🔮 Planned |
| `state audio gs` | | | Show General Sound device state (if available):<br/>• Device type and model<br/>• Current register values<br/>• Active channels and volume levels<br/>• Sample playback state<br/>• DMA status (if applicable)<br/>• Whether sound was played since reset via this device | 🔮 Planned |
| `state audio covox` | | | Show Covox DAC state:<br/>• DAC model (Covox, SounDrive, etc.)<br/>• Current output level (8-bit value)<br/>• Sample rate and buffer status<br/>• Port address being used<br/>• Whether sound was played since reset via this device | 🔮 Planned |
| `state audio channels` | | | Show audio mixer state for all sound sources:<br/>• Beeper: ON/OFF, level<br/>• AY chips: per-channel ON/OFF, volume<br/>• General Sound: active channels, levels<br/>• Covox: current level<br/>• Master output level and mute state | 🔮 Planned |

**AY-3-8912 Registers**:

| Register | Function | Bits |
| :--- | :--- | :--- |
| 0-1 | Channel A period (fine/coarse) | 12-bit |
| 2-3 | Channel B period | 12-bit |
| 4-5 | Channel C period | 12-bit |
| 6 | Noise period | 5-bit |
| 7 | Mixer control/enable | 8-bit |
| 8-10 | Channel A/B/C amplitude | 5-bit each |
| 11-12 | Envelope period | 16-bit |
| 13 | Envelope shape | 4-bit |
| 14-15 | I/O ports A/B | 8-bit each |

#### 6.9 Implementation Strategy

**Command Handler Architecture**:

All `state` commands will be implemented in a dedicated `StateInspector` class that aggregates data from various emulator subsystems:

```cpp
class StateInspector
{
public:
    StateInspector(EmulatorContext* context, PortMapper* portMapper, PortDecoder* portDecoder);
    
    // Memory Configuration & Paging
    std::string GetMemoryInfo();           // Complete memory state (ROM + RAM + paging)
    std::string GetRAMInfo();               // RAM banks only
    std::string GetROMInfo();               // ROM configuration and signature
    std::vector<MemoryChange> GetMemoryHistory(size_t count);  // Paging history
    
    // Port Inspection
    std::string GetAllPorts();              // All available ports from PortMapper with decoding info
    std::string GetPortValue(uint16_t port);  // Specific port with full decoding (address + value)
    std::string DecodePortValue(uint16_t port, uint8_t value);  // Decode arbitrary port value
    
    // System Variables
    std::string GetSystemVariables();
    std::string GetSystemVariable(const std::string& name);
    std::string GetSystemVariablesByCategory(const std::string& category);
    
    // Tape State
    std::string GetTapeStatus();
    std::string GetTapeBlocks();
    std::string GetTapeBlockInfo(size_t blockIndex);
    
    // Disk State
    std::string GetDiskStatus(char drive);
    std::string GetDiskGeometry(char drive);
    std::string GetDiskCatalog(char drive);
    std::string GetFDCState();
    
    // Screen State
    std::string GetScreenConfig();
    std::string GetScreenMode();
    std::string GetScreenAttrs();
    
    // ULA State
    std::string GetULAState();
    std::string GetULATiming();
    std::string GetULAContention();
    
    // Audio State
    std::string GetBeeperState();
    std::string GetAYState();                    // Brief state for all AY chips
    std::string GetAYState(uint8_t chipIndex);   // Detailed state for specific AY chip
    std::string GetAYRegister(uint8_t reg);      // Specific register decoding
    std::string GetGeneralSoundState();          // General Sound device state
    std::string GetCovoxState();                 // Covox DAC state
    
private:
    EmulatorContext* _context;
};
```

**Integration Points**:

1. **CLI Interface** (`CLIProcessor`):
   - New handler: `HandleState(const ClientSession& session, const std::vector<std::string>& args)`
   - Parses subsystem and subcommand
   - Delegates to `StateInspector` methods
   - Formats output for text display

2. **WebAPI Interface** (`AutomationWebAPI`):
   - **Base URL**: `http://localhost:8090/api/v1/emulator`
   - **Two endpoint patterns supported:**
     - `GET /api/v1/emulator/{id}/state/audio/*` - Target specific emulator by UUID or index
     - `GET /api/v1/emulator/state/audio/*` - Use active/most recent emulator (no ID required)
   - Returns JSON-formatted audio state data
   - **Emulator Addressing**: `{id}` can be either:
     - **UUID**: Full emulator UUID string (e.g., "550e8400-e29b-41d4-a716-446655440000")
     - **Index**: Zero-based numeric index (e.g., "0", "1", "2")
   - **Emulator Selection**: If specified ID/index doesn't exist, falls back to most recent emulator
   - Examples:
     - `GET http://localhost:8090/api/v1/emulator/0/state/audio/ay` - AY overview (emulator index 0)
     - `GET http://localhost:8090/api/v1/emulator/550e8400-e29b-41d4-a716-446655440000/state/audio/ay` - AY overview (by UUID)
     - `GET http://localhost:8090/api/v1/emulator/state/audio/ay` - AY overview (active emulator)
     - `GET http://localhost:8090/api/v1/emulator/0/state/audio/ay/0/register/0` - AY register 0 (emulator 0)
     - `GET http://localhost:8090/api/v1/emulator/state/audio/beeper` - Beeper state (active emulator)
     - **OpenAPI Specification**:
     - **Available at**: `GET http://localhost:8090/api/v1/openapi.json`
     - Provides machine-readable API documentation in OpenAPI 3.0 format
     - **⚠️ Manually Maintained**: This specification is NOT auto-generated. All API changes must be manually reflected in the OpenAPI JSON.
     - **CORS Support**: Fully enabled for cross-origin requests from web interfaces
     - **Usage with Swagger UI**:
       1. Pull and run Swagger UI: `docker pull swaggerapi/swagger-ui && docker run --name unreal-speccy-swagger-ui -p 8081:8080 -e SWAGGER_JSON_URL=http://localhost:8090/api/v1/openapi.json swaggerapi/swagger-ui`
       2. Open browser: `http://localhost:8081`
       3. Swagger UI automatically loads the API spec - explore and test all endpoints interactively!
     - **Usage with Postman**:
       1. Open Postman → Import → Link
       2. Enter: `http://localhost:8090/api/v1/openapi.json`
       3. Import as collection for full API testing
     - **Usage with curl**:
       ```bash
       curl http://localhost:8090/api/v1/openapi.json | jq .
       ```
     - **Benefits**:
       - Automatic API discovery
       - Interactive testing interface
       - Request/response schema validation
       - Code generation for multiple languages
       - Always up-to-date documentation
       - CORS enabled for web integration

3. **Python Bindings**:
   ```python
   # Python API examples
   state = emulator.state
   print(state.memory())          # All memory info
   print(state.memory.rom())      # ROM only
   print(state.ram())             # Shortcut for RAM
   print(state.ports())           # All available ports
   print(state.port(0xFE))        # Specific port
   print(state.sysvars.PROG)      # System variable
   print(state.tape())            # Tape status
   print(state.screen.mode())     # Video mode
   print(state.audio.ay())        # All AY chips overview
   print(state.audio.ay(0))       # Detailed AY chip 0 info
   print(state.audio.ay(0).register(0))  # AY chip 0 register 0
   print(state.audio.beeper())    # Beeper state
   ```

4. **Lua Bindings**:
   ```lua
   -- Lua API examples
   print(emu.state.memory())      -- All memory info
   print(emu.state.memory.rom())  -- ROM only
   print(emu.state.ram())         -- Shortcut for RAM
   print(emu.state.ports())       -- All available ports
   print(emu.state.port(0xFE))    -- Specific port
   print(emu.state.sysvars.PROG)  -- System variable
   print(emu.state.tape())        -- Tape status
   print(emu.state.screen.mode()) -- Video mode
   print(emu.state.audio.ay())    -- All AY chips overview
   print(emu.state.audio.ay(0))   -- Detailed AY chip 0 info
   print(emu.state.audio.ay(0).register(0)) -- AY chip 0 register 0
   print(emu.state.audio.beeper()) -- Beeper state
   ```

**Data Sources**:

Each state command reads from specific emulator components:

| State Subsystem | Primary Data Source | Additional Sources |
| :--- | :--- | :--- |
| `memory` | `Memory` class, `CONFIG` struct | Port 0x7FFD/0x1FFD monitor, paging history buffer |
| `ports` | `PortMapper` registry, `PortDecoder` | Model configuration, interface list, peripheral registry |
| `port` | `IO` class, port I/O history buffer | `PortMapper` for device info, `PortDecoder` for address decoding, device-specific value decoders |
| `sysvars` | `Memory` (0x5C00-0x5CB5) | System variable definitions table |
| `tape` | `Tape` class | Current tape image metadata, block list |
| `disk` | `BetaDisk`/`PlusDisk` classes | FDC registers, disk image, DOS detector |
| `screen` | `Screen` class, `ULA` | Port 0x7FFD bit 3, memory banking, video mode flags |
| `ula` | `ULA` class | T-state counter, scanline position, contention logic |
| `audio` | `Beeper`, `TurboSound` classes | AY register state, mixer configuration |

**Output Formatting**:

- **CLI**: Human-readable tables with aligned columns
- **WebAPI**: JSON with full type information
- **Python/Lua**: Native objects (dicts, tables) for programmatic access

**Performance Considerations**:

- State queries are read-only, no emulation side effects
- Most queries complete in <1ms (simple memory/register reads)
- History queries may take longer for large buffers
- `watch` subcommands use event-driven updates (no polling)

**Future Enhancements**:

- **Live Watch Mode**: Real-time terminal UI with auto-refresh for all state subsystems
- **State Diff**: Compare state between two points in time (e.g., before/after ROM switch)
- **State Export**: JSON/XML export for external analysis and archiving
- **State Alerts**: Trigger on specific state changes (e.g., memory paging changed, port accessed, ROM switched)

### 7. Emulator Instance Settings

Commands to configure emulator instance behavior and performance characteristics. These settings control how the emulator handles I/O operations and execution speed.

| Command | Aliases | Arguments | Description | Implementation Status |
| :--- | :--- | :--- | :--- | :--- |
| `setting list` | `settings` | | Display all emulator settings with their current values | ✅ Implemented |
| `setting <name> <value>` | `set` | `<setting-name> <value>` | Change a specific setting value | ✅ Implemented |
| `setting fast_tape <on\|off>` | | `on` or `off` | Enable/disable fast tape loading. When enabled, tape operations execute at maximum speed without audio emulation, significantly reducing loading times. | ✅ Implemented |
| `setting fast_disk <on\|off>` | | `on` or `off` | Enable/disable fast disk loading. When enabled, FDD operations bypass timing delays for near-instant disk access. | 🔮 Planned |
| `setting turbo_fdc <on\|off>` | | `on` or `off` | Enable/disable turbo FDC mode. Accelerates WD1793 FDC operations for faster disk I/O. | 🔮 Planned |
| `setting max_cpu_speed <value>` | | `<multiplier>` or `unlimited` | Set maximum CPU speed multiplier. Values: `1` (3.5MHz), `2` (7MHz), `4` (14MHz), `8` (28MHz), `16` (56MHz), or `unlimited`. Affects execution speed for loading and intensive operations. | 🔮 Planned |
| `setting cpu_frequency <value>` | | `<MHz>` | Set exact CPU frequency in MHz. Alternative to multiplier setting. Valid range: 3.5 - 112.0 MHz. | 🔮 Planned |
| `setting reset` | | | Reset all settings to default values | 🔮 Planned |

**Implementation Details**:

**Setting Categories**:

1. **I/O Acceleration Settings**:
   - `fast_tape`: Bypasses audio emulation and timing for tape operations
   - `fast_disk`: Accelerates FDD seek times and data transfer
   - `turbo_fdc`: Removes WD1793 command delays

2. **CPU Performance Settings**:
   - `max_cpu_speed`: Controls CPU clock multiplier (relative to 3.5MHz base)
   - `cpu_frequency`: Direct frequency control in MHz
   - Affects: instruction timing, video frame timing, audio sample rate

3. **Compatibility Settings** (Future):
   - `timing_model`: `accurate` | `fast` | `compatible`
   - `contention_model`: `full` | `simplified` | `none`
   - `interrupt_timing`: `exact` | `frame` | `scanline`

**Common Settings**:

| Setting | Type | Default | Valid Values | Description |
| :--- | :--- | :--- | :--- | :--- |
| `fast_tape` | Boolean | `off` | `on`, `off` | Fast tape loading mode |
| `fast_disk` | Boolean | `off` | `on`, `off` | Fast disk access mode |
| `turbo_fdc` | Boolean | `off` | `on`, `off` | Turbo FDC operations |
| `max_cpu_speed` | Integer/String | `1` | `1`, `2`, `4`, `8`, `16`, `unlimited` | CPU speed multiplier |
| `cpu_frequency` | Float | `3.5` | `3.5` - `112.0` | CPU frequency in MHz |
| `timing_model` | String | `accurate` | `accurate`, `fast`, `compatible` | Timing emulation model |

**Use Cases**:

1. **Fast Loading**: Reduce load times for development
   ```
   setting fast_tape on
   setting fast_disk on
   # Load tape/disk, then disable for accurate emulation
   setting fast_tape off
   setting fast_disk off
   ```

2. **Turbo Mode**: Maximum speed for automated testing
   ```
   setting max_cpu_speed unlimited
   setting turbo_fdc on
   ```

3. **Accurate Emulation**: Precise timing for demos/games
   ```
   setting max_cpu_speed 1
   setting fast_tape off
   setting fast_disk off
   ```

4. **Custom CPU Speed**: Test at specific frequency
   ```
   setting cpu_frequency 7.0    # 7MHz (Pentagon timing)
   setting cpu_frequency 14.0   # 14MHz (ultra-fast)
   ```

**Notes**:
- Setting changes take effect immediately
- Fast modes may affect timing-sensitive software (demos, loaders)
- Settings persist for the emulator instance lifetime
- Different instances can have different settings
- Settings are NOT saved between sessions (use configuration files for persistence)
- `unlimited` CPU speed runs as fast as host allows (useful for automated tests)
- Python/Lua bindings provide programmatic setting control

**Implementation**:
- Settings managed via `EmulatorContext::config` structure
- Changes trigger appropriate subsystem updates
- CLI: `CLIProcessor::HandleSetting()`
- WebAPI: `/api/v1/emulator/{id}/settings` endpoint
- Python: `emulator.set_setting(name, value)`
- Lua: `emulator:set_setting(name, value)`

---

## Future Capabilities

The following commands and interfaces are planned for future implementation. This section documents the roadmap for expanding the ECI to support more advanced debugging, analysis, and automation workflows.

### 1. Advanced Debugger Protocol Support

Standard debugging protocols to enable integration with professional development tools and IDEs.

#### 1.1 GDB Remote Serial Protocol (RSP)

**Status**: 🔮 Planned  
**Priority**: High  
**Target**: Q2 2026

**Description**: Implement GDB Remote Serial Protocol to allow standard GDB/LLDB clients and IDEs to attach to the emulator as if debugging a real Z80 target.

**Capabilities**:
- Full GDB protocol implementation (packets: `g`, `G`, `m`, `M`, `c`, `s`, `z`, `Z`, etc.)
- Z80 register mapping to GDB register format
- Memory read/write via GDB memory access packets
- Breakpoint/watchpoint management via GDB Z-packets
- Step/continue/interrupt control
- Thread/process simulation (single-threaded Z80 as process 1)

**Benefits**:
- Use GDB/LLDB command-line debuggers
- VS Code integration via GDB protocol
- CLion/IntelliJ IDEA Z80 debugging
- Scriptable via GDB Python API
- Leverage existing GDB ecosystem

**Implementation**:
- New transport: `AutomationGDB` listening on TCP port (default: 1234)
- Packet parser for GDB RSP protocol
- Z80 register serialization to GDB format
- Integration with existing `BreakpointManager`

**Example Usage**:
```bash
# Connect GDB to emulator
z80-elf-gdb
(gdb) target remote localhost:1234
(gdb) break *0x8000
(gdb) continue
(gdb) info registers
(gdb) x/16xb 0x4000
```

#### 1.2 Universal Debug Bridge (UDB)

**Status**: 🔮 Planned  
**Priority**: Medium  
**Target**: Q3 2026

**Description**: Custom high-performance binary protocol inspired by Android Debug Bridge (ADB) but tailored for retro computer emulation. Extends beyond GDB capabilities with streaming, profiling, and analysis features.

**Capabilities Beyond GDB**:
- **Streaming**: Continuous memory/register streaming for live visualization
- **Performance Profiling**: CPU hotspot analysis, instruction frequency counters
- **Trace Recording**: Record full execution trace to disk (100K+ instructions/sec)
- **Replay Debugging**: Load and replay execution traces for time-travel debugging
- **Scripting**: Embedded Lua/Python for breakpoint conditions and actions
- **Batch Operations**: Bulk memory read/write, atomic multi-command execution

**Protocol Features**:
- Binary protocol (no text parsing overhead)
- Multiplexed channels (commands + streaming data)
- Built-in compression for large data transfers
- Authentication and encryption (TLS optional)
- Version negotiation for forward compatibility

**Use Cases**:
- Deep reverse engineering of unknown software
- Performance analysis and optimization
- Automated testing with complex scenarios
- Cheat/trainer development
- Educational tools with live visualization

**Example Commands** (hypothetical):
```
udb profile start          # Begin CPU profiling
udb trace record out.trace # Record execution trace
udb stream memory 0x4000 0x1800  # Stream screen memory
udb breakpoint condition 0x8000 "a == 5"  # Conditional BP
udb script run analyze.py  # Run analysis script
```

### 2. Snapshot & State Management

Commands for saving, loading, and manipulating complete emulator state.

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `snapshot save <file>` | `<filename>` | Save complete emulator state to file. Includes CPU registers, memory contents, peripheral states, and execution history. Supports standard formats (.z80, .sna, .szx) plus native extended format. | 🔮 Planned |
| `snapshot load <file>` | `<filename>` | Load previously saved snapshot. Restores all state including breakpoints (if saved in native format). Auto-detects format from file header. | 🔮 Planned |
| `snapshot slot save <N>` | `<slot-number>` | Quick-save to numbered slot (1-10). Slots stored in emulator session directory. | 🔮 Planned |
| `snapshot slot load <N>` | `<slot-number>` | Quick-load from numbered slot. | 🔮 Planned |
| `snapshot slot list` | | List all saved slots with timestamps and annotations. | 🔮 Planned |
| `snapshot auto <on\|off>` | `on\|off [interval]` | Enable periodic auto-save (default: every 60 seconds). | 🔮 Planned |
| `history rewind <N>` | `<instructions>` | Reverse execution by N instructions (requires history buffer enabled). | 🔮 Planned |
| `history forward <N>` | `<instructions>` | Forward execution by N instructions (during rewind mode). | 🔮 Planned |
| `history save <file>` | `<filename>` | Export execution history to file for later replay. | 🔮 Planned |

**Implementation Notes**:
- **History Buffer**: Ring buffer storing last 1M instructions with CPU state deltas
- **Delta Compression**: Only store changed registers/memory for efficiency
- **Rewind Mechanism**: Reconstruct state by applying deltas in reverse
- **Performance**: ~5-10% overhead when history enabled

### 3. Input Injection & Automation

Programmatic control of emulator input devices for automation and testing.

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `key press <key>` | `<key-name>` | Press a key on the ZX Spectrum keyboard. Key names: `a-z`, `0-9`, `space`, `enter`, `shift`, `symbol`, etc. | 🔮 Planned |
| `key release <key>` | `<key-name>` | Release a previously pressed key. | 🔮 Planned |
| `key tap <key> [duration]` | `<key-name> [ms]` | Press and release key. Default duration: 20ms (1 frame). | 🔮 Planned |
| `type <text>` | `<string>` | Automatically type a string of text. Handles shift/symbol modifiers automatically. Rate-limited to realistic typing speed. | 🔮 Planned |
| `key combo <keys>` | `<key1+key2+...>` | Press multiple keys simultaneously (e.g., `CAPS+SHIFT+A` for graphics). | 🔮 Planned |
| `joystick <action>` | `up\|down\|left\|right\|fire` | Simulate joystick input (Kempston, Sinclair, Cursor). | 🔮 Planned |
| `mouse move <x> <y>` | `<x> <y>` | Move mouse cursor (for Kempston Mouse interface). | 🔮 Planned |
| `mouse click <button>` | `left\|right\|middle` | Simulate mouse click. | 🔮 Planned |

**Use Cases**:
- Automated game testing
- BASIC program entry without manual typing
- Game trainer/cheat input injection
- TAS (Tool-Assisted Speedrun) creation
- Accessibility automation

**Example Automation**:
```bash
# Automatically load and run a BASIC program
type 'LOAD "MYGAME"'
key tap enter
pause 5000  # Wait for load
type 'RUN'
key tap enter
```

### 4. Media & Tape/Disk Operations

Enhanced control over peripheral media devices.

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `tape load <file>` | `<filename>` | Insert tape file (.tap, .tzx, .csw) into virtual tape drive. | 🔧 Partially implemented via `open` |
| `tape eject` | | Eject current tape. | 🔮 Planned |
| `tape play` | | Start tape playback (if paused). | 🔮 Planned |
| `tape stop` | | Stop tape playback. | 🔮 Planned |
| `tape rewind` | | Rewind tape to beginning. | 🔮 Planned |
| `tape position <block>` | `<block-number>` | Seek to specific tape block. | 🔮 Planned |
| `tape info` | | Show tape information (format, blocks, current position). | 🔮 Planned |
| `disk insert <drive> <file>` | `<A\|B\|C\|D> <filename>` | Insert disk image into specified drive. Supports .trd, .scl, .fdi, .udi formats. | 🔧 Partially implemented via `open` |
| `disk eject <drive>` | `<A\|B\|C\|D>` | Eject disk from drive. | 🔮 Planned |
| `disk info <drive>` | `<A\|B\|C\|D>` | Show disk information (format, tracks, sectors, files). | 🔮 Planned |
| `disk catalog <drive>` | `<A\|B\|C\|D>` | List files on disk (TRDOS, CP/M format parsing). | 🔮 Planned |
| `disk protect <drive> <on\|off>` | `<A\|B\|C\|D> on\|off` | Toggle write protection. | 🔮 Planned |

### 5. Audio & Video Capture

Capture and export audio/video output for recording and analysis.

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `screenshot <file>` | `<filename>` | Capture current screen frame to PNG file. Includes border. Resolution: 320x240 (with border) or 256x192 (screen only). | 🔮 Planned |
| `screenshot screen <file>` | `<filename>` | Capture only screen area (no border). | 🔮 Planned |
| `video record <file>` | `<filename>` | Start recording video to file (MP4, WebM, or AVI). Frame rate: 50 FPS (PAL) or 60 FPS (NTSC). | 🔮 Planned |
| `video stop` | | Stop video recording and finalize file. | 🔮 Planned |
| `audio record <file>` | `<filename>` | Start recording audio to WAV file. Captures beeper + AY output. | 🔮 Planned |
| `audio stop` | | Stop audio recording. | 🔮 Planned |
| `audio spectrum` | | Display real-time audio spectrum analyzer. | 🔮 Planned |

**Format Support**:
- **Images**: PNG, BMP, JPEG
- **Video**: MP4 (H.264), WebM (VP9), AVI (uncompressed)
- **Audio**: WAV (16-bit PCM), FLAC, MP3

### 6. Scripting & Automation Integration

Execute external scripts and dynamic expressions for advanced automation.

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `script run <file>` | `<filename>` | Execute external Lua or Python script. Script has full access to emulator API. File extension determines interpreter (.lua or .py). | 🔮 Planned |
| `script eval <expr>` | `<expression>` | Evaluate single-line Lua/Python expression and return result. Useful for quick calculations or state queries. | 🔮 Planned |
| `script stop` | | Interrupt currently running script (useful for infinite loops). | 🔮 Planned |
| `watch add <expr>` | `<expression>` | Add a watch expression evaluated after each step. Example: `watch add "mem[0x5C6A]"` to monitor frame counter. | 🔮 Planned |
| `watch list` | | List all active watch expressions with current values. | 🔮 Planned |
| `watch remove <id>` | `<watch-id>` | Remove watch expression. | 🔮 Planned |

**Script Capabilities**:
- Full emulator control (all commands accessible as API)
- Register/memory read/write
- Breakpoint management with callbacks
- Event handling (frame rendered, breakpoint hit, etc.)
- Test automation and assertions
- Custom analysis and statistics

**Example Python Script**:
```python
# Find all CALLs in ROM
for addr in range(0x0000, 0x4000):
    opcode = emu.memory.read(addr)
    if opcode == 0xCD:  # CALL instruction
        target = emu.memory.read_word(addr + 1)
        print(f"CALL at 0x{addr:04X} to 0x{target:04X}")
```

### 7. Disassembly & Reverse Engineering

Enhanced code analysis and disassembly features.

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `disasm <addr> [count]` | `<address> [lines]` | Disassemble Z80 code starting at address. Default: 10 lines. Shows address, hex bytes, mnemonics, and operands. | 🔮 Planned |
| `disasm range <from> <to>` | `<start> <end>` | Disassemble address range. | 🔮 Planned |
| `disasm function <addr>` | `<address>` | Disassemble entire function (until RET found). | 🔮 Planned |
| `symbol load <file>` | `<filename>` | Load symbol file (.map, .sym, .labels). Enables symbolic names in disassembly and breakpoints. | 🔮 Planned |
| `symbol list [filter]` | `[pattern]` | List loaded symbols, optionally filtered by pattern. | 🔮 Planned |
| `symbol find <name>` | `<symbol-name>` | Find address of symbol by name. | 🔮 Planned |
| `label set <addr> <name>` | `<address> <label>` | Manually set label at address. | 🔮 Planned |
| `comment set <addr> <text>` | `<address> <comment>` | Add comment annotation at address. | 🔮 Planned |

### 8. Content Analyzers & Extractors

Intelligent analysis and extraction of various content types from memory or files. These analyzers help with reverse engineering, archiving, and understanding software structure.

#### 8.1 BASIC Program Extractor

**Status**: ✅ Implemented  
**Implementation**: `core/src/debugger/analyzers/basicextractor.h/cpp`

Extract and detokenize ZX Spectrum BASIC programs from memory or files.

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `basic extract` | | Extract BASIC program from current memory (using PROG/VARS system variables). Returns detokenized BASIC listing. | ✅ Implemented |
| `basic extract <addr> <len>` | `<address> <length>` | Extract BASIC program from specific memory region. | 🔮 Planned |
| `basic extract file <file>` | `<filename>` | Extract BASIC from .tap, .tzx, or raw file. | 🔮 Planned |
| `basic save <file>` | `<filename>` | Save extracted BASIC to text file. | 🔮 Planned |
| `basic load <file>` | `<filename>` | Load ASCII BASIC from text file: tokenize into memory and adjust system variables (PROG, VARS, E_LINE). | 🔮 Planned |

**Features**:
- Detokenizes all Spectrum 48K/128K BASIC tokens (0xA3-0xFF)
- Handles numeric constants (integral and floating-point formats)
- Extracts directly from memory using system variables
- Supports malformed programs (e.g., invalid line lengths)
- Line-by-line parsing with proper formatting

**Use Cases**:
- Archive BASIC programs from tape/disk images
- Analyze game loaders and protection routines
- Convert tokenized BASIC to editable text
- Study programming techniques in commercial software

#### 8.2 Music Detector & Ripper

**Status**: 🔮 Planned (Q2-Q3 2026)

Detect music players, identify formats, and extract music data (RIP).

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `music detect` | | Scan memory for known music player signatures (AY, Beeper engines). | 🔮 Planned |
| `music identify` | | Identify music format: AY (Vortex, PT3, ASC), Beeper (Special FX, Phaser1). | 🔮 Planned |
| `music extract <addr>` | `<address>` | Extract music data starting at address. Auto-detects format. | 🔮 Planned |
| `music rip <file>` | `<filename>` | Rip music to standard format (.pt3, .ay, .sna with player). | 🔮 Planned |
| `music info` | | Display music metadata (title, author, format details). | 🔮 Planned |

**Supported Formats**:
- **AY Players**: Vortex Tracker II, ProTracker 3, ASC Sound Master
- **Beeper Engines**: Special FX, Phaser1, Octode, 1-bit engines
- **Sample Players**: SpecialFX, Covox, MegaBlaster
- **Custom**: Game-specific music routines

**Detection Methods**:
- Signature scanning (known player code patterns)
- Heuristic analysis (interrupt handlers, port writes)
- Frequency analysis (audio output patterns)
- Structure recognition (pattern tables, note data)

#### 8.3 Sprite Ripper

**Status**: 🔮 Planned (Q3 2026)

Extract graphics sprites from memory.

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `sprite scan <addr> <len>` | `<address> <length>` | Scan memory region for sprite data. | 🔮 Planned |
| `sprite extract <addr> <w> <h>` | `<address> <width> <height>` | Extract sprite at address with dimensions. | 🔮 Planned |
| `sprite save <file>` | `<filename>` | Save extracted sprites to image file (PNG, GIF). | 🔮 Planned |
| `sprite sheet <file>` | `<filename>` | Save all sprites as sprite sheet. | 🔮 Planned |
| `sprite detect format` | | Detect sprite format: standard, mask, multi-color. | 🔮 Planned |

**Detection Capabilities**:
- Standard 8x8, 16x16, 32x32 sprites
- Masked sprites (AND/OR masks)
- Multi-color sprites
- Attributed sprites (color-per-char)
- Compiled sprites
- RLE/packed sprites

#### 8.4 Self-Modifying Code Detector

**Status**: 🔮 Planned (Q3 2026)

Detect and analyze self-modifying code - code that modifies its own instructions during execution, then loops back to execute the newly modified code.

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `smc detect` | | Scan for self-modifying code (writes to recently executed code). | 🔮 Planned |
| `smc watch <addr> <len>` | `<address> <length>` | Monitor code region for self-modifications during execution. | 🔮 Planned |
| `smc trace` | | Record all code modifications with execution context. | 🔮 Planned |
| `smc analyze` | | Analyze self-modification patterns and generate report. | 🔮 Planned |

**Analysis Features**:
- Detects writes to recently executed code regions
- Identifies code that modifies itself then re-executes modified parts
- Tracks modification-execution cycles and patterns
- Logs modification sequences with timing information
- Generates before/after disassembly comparisons

**Use Cases**:
- **Demo Effects**: Understand optimized raster effects and display tricks
- **Protection Schemes**: Analyze decryption and obfuscation techniques
- **Packers/Crunchers**: Study unpacking and decompression routines
- **Code Optimization**: Learn advanced Z80 programming techniques

#### 8.5 Compressor/Packer Detector

**Status**: 🔮 Planned (Q2-Q3 2026)

Detect and unpack compressed/protected software.

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `pack detect` | | Detect known compression/protection: Turbo, RCS, Hrust, etc. | 🔮 Planned |
| `pack identify` | | Identify packer version and parameters. | 🔮 Planned |
| `pack unpack <addr>` | `<address>` | Unpack compressed data at address. | 🔮 Planned |
| `pack trace` | | Trace unpacking process (breakpoint on unpack completion). | 🔮 Planned |
| `pack save <file>` | `<filename>` | Save unpacked data to file. | 🔮 Planned |

**Supported Packers/Protectors**:
- **Compressors**: Turbo, Hrust 1.x/2.x, RCS, ASC, CharPres
- **Protectors**: Speedlock, Alkatraz, Bleepload, Microsphere
- **Crunchers**: Exomizer, ZX7, MegaLZ, MS-Pack
- **Encryptors**: XOR variants, custom encryption schemes

**Detection Methods**:
- Signature scanning (known depacker code)
- Entropy analysis
- Code pattern recognition
- Loader behavior analysis

#### 8.6 Tape Loader Detector

**Status**: 🔮 Planned (Q3 2026)

Detect and analyze non-standard tape loading routines. Tape loaders use audio signal processing to load data from cassette tapes.

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `tape loader detect` | | Detect custom tape loaders in current code. | 🔮 Planned |
| `tape loader identify` | | Identify loader type: turbo, custom sync, headerless. | 🔮 Planned |
| `tape loader params` | | Extract loader parameters (timing, pilot tone, sync patterns). | 🔮 Planned |
| `tape loader trace` | | Trace tape loader execution and audio I/O operations. | 🔮 Planned |

**Loader Types Detected**:
- **Standard ROM Loader**: Built-in Spectrum ROM tape routines (0x056B, 0x04C2)
- **Turbo Loaders**: Faster custom loaders (various speeds: 1500-3000 baud)
- **Custom Sync Patterns**: Non-standard pilot tones and sync sequences
- **Headerless Loaders**: No header block, direct data loading
- **Multi-Stage Loaders**: Chained loading with multiple custom routines
- **Protection Schemes**: Anti-copy tape tricks, timing-based protection

**Audio Analysis Features**:
- Pilot tone duration and frequency detection
- Sync pattern recognition
- Bit timing analysis (0 and 1 pulse lengths)
- Baud rate calculation
- Custom border effects during loading

**Use Cases**:
- Understand commercial game loaders
- Archive protected software
- Document loading algorithms
- Optimize tape image conversion

#### 8.7 Disk System Detector

**Status**: 🔮 Planned (Q3 2026)

Detect and analyze disk operating systems and custom disk routines. Context-aware detection based on emulated disk interface(s).

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `disk system detect` | | Detect which disk operating system is in use. | 🔮 Planned |
| `disk system identify` | | Identify DOS type and version (TR-DOS, +3DOS, ESXDOS, etc.). | 🔮 Planned |
| `disk loader detect` | | Detect custom disk loading routines. | 🔮 Planned |
| `disk loader params` | | Extract loader parameters (FDC commands, sector access patterns). | 🔮 Planned |
| `disk loader trace` | | Trace disk I/O operations and FDC command sequences. | 🔮 Planned |

**Supported Disk Interfaces**:

**Classic External Interfaces**:
- **Beta Disk Interface (1984/1985)**: TR-DOS operating system, standard in Eastern Europe/Russia. Uses WD1793 FDC, single/double density 3.5"/5.25" drives.
- **Beta 128 (1987)**: Updated for ZX Spectrum 128 with adjusted memory paging.
- **DISCiPLE & +D (MGT, 1987-1988)**: Popular in UK, Shugart standard drives, snapshot feature, G+DOS operating system.
- **MB02+ (Late 1990s)**: Advanced interface, HD 1.8MB support, Z80DMA controller for high-speed transfers.

**Integrated Interface**:
- **+3DOS (ZX Spectrum +3, 1987)**: Built-in 3-inch disk drive, CP/M compatible, Amstrad-era standard.

**Modern Interfaces**:
- **ESXDOS** ([esxdos.org](https://www.esxdos.org)): For DivIDE/DivMMC interfaces, FAT16/FAT32 support, virtual disk, .TRD emulation, TAP file support.
- **ZX-Next DOS**: For ZX Spectrum Next, modern SD card support.
- **NEMO IDE**: HDD/CompactFlash interface with IDE protocol.
- **DivIDE/DivMMC**: IDE and SD/MMC card interfaces.

**Operating Systems Detected**:
- **TR-DOS**: Beta Disk standard (multiple versions: 4.xx, 5.xx, 6.xx)
- **iS-DOS**: RAM-resident DOS (Iskra Soft, 1990-1991, Saint Petersburg), supports FDD/HDD/CD-ROM
- **+3DOS**: Spectrum +3 built-in DOS
- **G+DOS**: DISCiPLE/+D operating system
- **ESXDOS**: Modern FAT filesystem DOS
- **CP/M**: CP/M 2.2 and Plus variants
- **Custom DOS**: Game-specific or commercial DOS variants

**Detection Methods**:
- ROM signature scanning (known DOS entry points)
- System variable analysis (DOS workspace locations)
- FDC command pattern recognition
- Filesystem structure detection (boot sector, FAT, directory structure)
- I/O port access patterns (specific to interface type)

**FDC Analysis Features**:
- WD1793/WD1773 command sequence analysis
- Sector read/write patterns
- Track seek optimization detection
- Multi-sector operations
- DMA transfer detection (MB02+)
- Copy protection schemes:
  - Weak bits (intentional read errors)
  - Fuzzy bits (variable data on reads)
  - Protection tracks (non-standard formats)
  - Bad sector maps
  - Custom sector sizes

**Context-Aware Operation**:
The disk detector operates based on the currently emulated interface(s):

```bash
# Example: Detect DOS on Beta Disk system
> disk system detect
Detected: TR-DOS 5.03
Interface: Beta Disk (WD1793)
Tracks: 80, Sides: 2, Sectors/Track: 16
Format: 720KB (TR-DOS standard)

# Example: Detect on +3
> disk system detect
Detected: +3DOS 1.0
Interface: Amstrad +3 (µPD765A)
Format: 3-inch disk, 180KB per side

# Example: Detect ESXDOS
> disk system detect
Detected: ESXDOS 0.8.9
Interface: DivMMC (SD Card)
Filesystem: FAT32
Virtual Disks: 2x .TRD mounted
```

**Use Cases**:
- Identify which DOS is running
- Analyze commercial disk protection
- Understand FDC access patterns
- Document disk format structures
- Debug custom loaders
- Convert between disk formats

#### 8.8 Graphics Effects Detector

**Status**: 🔮 Planned (Q4 2026)

Detect advanced graphics techniques.

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `gfx detect` | | Detect special graphics effects in use. | 🔮 Planned |
| `gfx identify` | | Identify effect type: multicolor, gigascreen, hardware scroll. | 🔮 Planned |
| `gfx parameters` | | Extract effect parameters (frame timing, color patterns). | 🔮 Planned |

**Effects Detected**:
- **Multi-color**: Attribute flashing techniques, color cycling
- **Gigascreen**: Interlaced frame techniques
- **Hardware Scrolling**: ULA tricks, border manipulation
- **Raster Effects**: Split-screen, raster bars, parallax
- **3D Graphics**: Polygon renderers, raycasting engines

#### 8.9 Analyzer Framework

**Common Features**:
- **Batch Analysis**: Run multiple analyzers simultaneously
- **Confidence Scoring**: Rate detection accuracy (0-100%)
- **Report Generation**: JSON, XML, or text reports
- **Pattern Database**: User-extensible signature database
- **Python/Lua API**: Create custom analyzers in scripts

**Example Usage**:
```bash
# Detect everything
analyze all --format json --output report.json

# Chain analyzers
basic extract | analyze
music detect | music rip game.pt3

# Custom analyzer in Python
python my_analyzer.py --memory 0x8000 0x4000
```

### 9. LLM Integration Interfaces (MCP/A2A)

**Status**: 🔮 Planned (Q3-Q4 2026)

Model Context Protocol (MCP) and Agent-to-Agent (A2A) interfaces enable Large Language Models and AI agents to interact with the emulator programmatically for automated analysis, testing, and content generation.

#### 9.1 Model Context Protocol (MCP)

**Overview**: MCP provides a standardized way for LLMs to access emulator context, state, and capabilities as part of their reasoning process.

**Use Cases**:
- **Automated Testing**: LLM generates test cases and validates behavior
- **Code Analysis**: AI analyzes assembly code and suggests optimizations
- **Documentation**: Auto-generate documentation from code behavior
- **Debugging**: AI-assisted debugging with natural language queries
- **Retro Game AI**: Train agents to play and master classic games

#### 9.2 MCP Commands

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `mcp enable` | | Enable MCP interface on specified port/endpoint. | 🔮 Planned |
| `mcp query <prompt>` | `<natural-language>` | Send natural language query to configured LLM about emulator state. | 🔮 Planned |
| `mcp context set <type>` | `<full\|minimal\|custom>` | Configure context detail level sent to LLM. | 🔮 Planned |
| `mcp analyze <target>` | `<code\|memory\|behavior>` | Request LLM analysis of specific emulator aspect. | 🔮 Planned |
| `mcp suggest` | | Get AI suggestions for current debugging context. | 🔮 Planned |

#### 9.3 Agent-to-Agent (A2A) Protocol

**Overview**: A2A enables autonomous agents to control the emulator and coordinate complex multi-step tasks.

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `a2a register <agent>` | `<agent-id>` | Register an AI agent with the emulator. | 🔮 Planned |
| `a2a task assign <task>` | `<task-definition>` | Assign task to agent (e.g., "find all CALLs to ROM"). | 🔮 Planned |
| `a2a task status` | | Query task progress and results. | 🔮 Planned |
| `a2a collaborate <agents>` | `<agent-list>` | Enable multi-agent collaboration on complex tasks. | 🔮 Planned |
| `a2a unregister <agent>` | `<agent-id>` | Unregister agent and cleanup resources. | 🔮 Planned |

#### 9.4 MCP Context Types

**Full Context** (for deep analysis):
```json
{
  "emulator_state": {
    "cpu": { "registers": {...}, "pc": "0x8000", "flags": {...} },
    "memory": { "prog_start": "0x5C53", "vars_start": "0x5C4B" },
    "devices": { "tape": "playing", "disk": "idle" },
    "breakpoints": [...],
    "call_stack": [...]
  },
  "code_context": {
    "current_instruction": "LD A, (HL)",
    "disassembly": [...],
    "symbols": {...}
  },
  "analysis": {
    "basic_program": "10 PRINT \"Hello\"\n20 GOTO 10",
    "detected_patterns": ["infinite loop", "basic program"]
  }
}
```

**Minimal Context** (for quick queries):
```json
{
  "pc": "0x8000",
  "af": "0x44C4",
  "instruction": "LD A, (HL)"
}
```

#### 9.5 Example LLM Workflows

**Automated Game Testing**:
```bash
# AI agent plays game and reports bugs
a2a register game-tester
a2a task assign "Play game for 5 minutes, find edge cases"
# Agent uses key injection, monitors crashes, logs anomalies
a2a task status
```

**Code Reverse Engineering**:
```bash
# LLM analyzes unknown routine
mcp query "What does the code at 0x8000 do?"
# Response: "This appears to be a sprite drawing routine..."

# Follow-up analysis
mcp analyze code --range 0x8000-0x8100
# Generates detailed analysis with pseudocode
```

**Natural Language Debugging**:
```bash
# Developer asks question in natural language
mcp query "Why is the screen flickering?"
# LLM analyzes interrupt timing, screen updates, identifies issue
# Response: "Interrupt handler at 0x0038 is taking 25000 T-states..."
```

**Content Generation**:
```bash
# AI generates test content
a2a task assign "Create BASIC program to test all graphics modes"
# Agent generates tokenized BASIC, loads it, validates output
```

#### 9.6 Integration Points

**Language Models**:
- OpenAI GPT-4/GPT-4o (API)
- Anthropic Claude (API)
- Local LLMs (Ollama, LM Studio)
- Specialized code models (CodeLlama, StarCoder)

**Agent Frameworks**:
- LangChain
- AutoGPT
- BabyAGI
- Custom agent implementations

**Protocols**:
- REST API endpoints (for stateless queries)
- WebSocket (for streaming context)
- gRPC (for high-performance agent communication)
- JSON-RPC (for structured commands)

#### 9.7 Safety & Sandboxing

**Resource Limits**:
- Query rate limiting
- Context size limits
- Agent execution timeouts
- Memory usage caps

**Permission Model**:
- Read-only mode (safe exploration)
- Read-write mode (testing, requires confirmation)
- Admin mode (full control, authentication required)

**Audit Logging**:
- All LLM queries logged
- Agent actions tracked
- Decision reasoning recorded
- Compliance with AI safety guidelines

#### 9.8 Implementation Architecture

```
┌─────────────────────────────────────────┐
│  LLM / AI Agent                         │
│  (GPT-4, Claude, Local Model)           │
└──────────────┬──────────────────────────┘
               │ MCP Protocol (JSON)
               ▼
┌─────────────────────────────────────────┐
│  MCP/A2A Interface Layer                │
│  • Context serialization                │
│  • Query processing                     │
│  • Response generation                  │
│  • Safety checks                        │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│  Emulator Control Interface (ECI)       │
│  All standard commands available        │
└─────────────────────────────────────────┘
```

**Context Serialization**:
- Automatic state extraction
- Relevance filtering (only send what's needed)
- Compression (large contexts compressed)
- Caching (avoid redundant context generation)

**Query Processing**:
- Natural language → Command translation
- Intent recognition
- Parameter extraction
- Error handling with suggestions

**Response Generation**:
- Command results → Natural language
- Code formatting and syntax highlighting
- Visualization hints (for GUI clients)
- Confidence scores

#### 9.9 Example Use Cases

**Scenario 1: Automated Bug Finder**
```python
# Python agent using MCP
agent = A2A_Agent("bug-finder")
agent.task = "Find buffer overflows in this game"

# Agent methodology:
# 1. Analyze BASIC program structure
# 2. Set watchpoints on variables
# 3. Inject edge-case inputs
# 4. Monitor memory corruption
# 5. Generate bug report

results = agent.execute()
print(results.bugs_found)
```

**Scenario 2: Interactive Tutor**
```bash
# Student asks question
> mcp query "How do I implement PRINT in my BASIC interpreter?"

# LLM analyzes ROM PRINT routine, explains:
# - Token parsing
# - Character output via RST 10h
# - Screen positioning
# - Scroll handling
# - Provides working example code
```

**Scenario 3: Code Optimizer**
```bash
# AI suggests optimizations
> mcp analyze code --optimize --range 0x8000-0x8100

# LLM identifies:
# - Redundant loads: "LD A, (HL); LD A, (HL)" → optimize
# - Loop unrolling opportunities
# - Register allocation improvements
# - Generates optimized version with explanation
```

### 10. Performance & Profiling

Commands for analyzing emulator and emulated code performance.

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `profile start` | | Begin CPU profiling. Tracks instruction frequency and execution time per address. | 🔮 Planned |
| `profile stop` | | Stop profiling and display results. | 🔮 Planned |
| `profile report [top]` | `[N]` | Show top N hottest code regions. Default: top 20. | 🔮 Planned |
| `profile export <file>` | `<filename>` | Export profiling data to CSV for external analysis. | 🔮 Planned |
| `benchmark` | | Run standard benchmark (e.g., empty loop timing) and report performance. | 🔮 Planned |
| `fps` | | Display current frames-per-second and frame time statistics. | 🔮 Planned |

### 11. Network & Multi-Emulator Operations

Experimental features for networked emulation and multi-instance orchestration.

| Command | Arguments | Description | Status |
| :--- | :--- | :--- | :--- |
| `link create <id1> <id2>` | `<emu1> <emu2>` | Create virtual link between two emulator instances (simulated serial or network connection). | 🔮 Planned |
| `link send <data>` | `<hex-data>` | Send data through established link. | 🔮 Planned |
| `sync all pause` | | Pause all emulator instances simultaneously. | 🔮 Planned |
| `sync all resume` | | Resume all emulator instances simultaneously. | 🔮 Planned |

---

## Implementation Priority

### Phase 1: Core Functionality (Q1-Q2 2026)
**Focus**: Essential debugging and state management
- ✅ **BASIC Extractor** - Already implemented
- **Emulator Instance Management** - `start`, `stop`, `list`, `status` commands with Message Center notifications
- Snapshot save/load
- Input injection (key press, type text)
- Screenshot capture
- Disassembly commands
- Symbol file loading

### Phase 2: Advanced Debugging (Q2-Q3 2026)
**Focus**: Professional tools and content analysis
- **GDB Remote Serial Protocol** - Industry-standard debugging
- **Music Detector & Ripper** - Extract and identify music
- **Compressor/Packer Detector** - Identify and unpack protection
- Enhanced tape/disk control
- Audio/video recording
- Script execution (Lua/Python command sequences)

### Phase 3: Intelligent Analysis (Q3 2026)
**Focus**: Automated content extraction and analysis
- **Sprite Ripper** - Extract graphics assets
- **Self-Modifying Code Detector** - Analyze dynamic code
- **Tape/Disk Loader Detector** - Identify custom loaders
- **Graphics Effects Detector** - Identify multicolor, gigascreen
- Performance profiling
- History rewind/replay

### Phase 4: AI Integration & Advanced Features (Q3-Q4 2026)
**Focus**: LLM integration and cutting-edge capabilities
- **Model Context Protocol (MCP)** - LLM integration
- **Agent-to-Agent (A2A) Protocol** - Autonomous agents
- **Universal Debug Bridge Protocol** - High-performance analysis
- Analyzer framework extensibility (Python/Lua custom analyzers)
- Multi-emulator networking
- Collaborative debugging features

### Implementation Notes

**Analyzer Framework**:
- Core analyzer infrastructure (Q2 2026)
- Plugin system for custom analyzers (Q3 2026)
- Machine learning-based detection (Q4 2026)

**MCP/A2A Interfaces**:
- Basic MCP query support (Q3 2026)
- Context serialization and optimization (Q3 2026)
- Agent registration and task management (Q4 2026)
- Multi-agent collaboration (Q4 2026+)

**Performance Targets**:
- Analyzers should process 48KB in <1 second
- MCP context generation <100ms
- Pattern database: 1000+ signatures by end of 2026

---

## See Also

### Interface Implementations
- **[CLI Interface](./cli-interface.md)** - TCP-based text protocol for interactive debugging
- **[WebAPI Interface](./webapi-interface.md)** - HTTP/REST API for web integration
- **[Python Bindings](./python-interface.md)** - Direct C++ bindings for automation and AI/ML
- **[Lua Bindings](./lua-interface.md)** - Lightweight scripting and embedded logic
- **[GDB Protocol](./gdb-protocol.md)** - Professional debugging with standard GDB/LLDB clients
- **[Universal Debug Bridge](./udb-protocol.md)** - High-performance analysis and profiling

### Navigation
- **[Interface Documentation Index](./README.md)** - Overview of all control interfaces

## Contributing

Interface implementations and command additions are welcome! Please see:
- Architecture documentation: [ARCHITECTURE.md](../../ARCHITECTURE.md)
- Contribution guidelines: [CONTRIBUTING.md](../../../../CONTRIBUTING.md)
- Command implementation guide: [COMMAND_IMPLEMENTATION.md](./COMMAND_IMPLEMENTATION.md)
