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
  - `_selectedEmulatorId`: Currently active emulator for this session
- **Note**: WebAPI is stateless; emulator ID must be in each request path

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
| `status` | | | Show the runtime status (Running/Paused/Stopped/Debug) of all emulator instances, including ID, symbolic name, uptime, and current state. |
| `list` | | | List all available emulator instances with their UUIDs, symbolic IDs (if set), and creation timestamps. Used to identify which emulator to `select`. |
| `select <id>` | | `<emulator-id>` | Select the active emulator instance for subsequent commands. The `<id>` can be either the UUID or symbolic ID. All following commands (reset, pause, registers, etc.) will operate on this selected instance. |
| `open <file>` | | `<file-path>` | Open and load a file into the selected emulator. Supports multiple formats: tape images (.tap, .tzx), snapshots (.z80, .sna), disk images (.trd, .scl, .fdi). File type is auto-detected by extension. |
| `exit` | `quit` | | Terminate the control session. For CLI, closes the TCP connection. Does not stop the emulator instances themselves. |
| `dummy` | | | No-operation command used for connection initialization and testing. Returns a simple acknowledgment. Useful for verifying the connection is alive. |

**Notes**:
- Most commands require an emulator to be selected first via `select <id>`
- CLI sessions auto-select the most recently created emulator if only one exists
- WebAPI requests must include the emulator ID in the URL path (stateless design)

### 2. Execution Control

Commands to control the CPU execution flow for the selected emulator instance. These commands are fundamental for debugging and precise control over emulation.

| Command | Aliases | Arguments | Description |
| :--- | :--- | :--- | :--- |
| `pause` | | | Immediately pause emulation execution. The CPU stops at the next instruction boundary. Current register state and memory are preserved. Returns confirmation when paused. Useful before inspecting state or setting breakpoints. |
| `resume` | | | Resume emulation execution from the paused state. The CPU continues from the current PC register value. If not paused, returns an error. |
| `reset` | | | Perform a hardware reset of the emulator. Equivalent to pressing the physical reset button. Resets CPU registers to power-on state (PC=0x0000), reinitializes peripherals, and reloads ROM. RAM contents may be preserved depending on configuration. |
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

Commands to view and control emulator runtime features for the selected emulator instance. Features can be toggled at runtime without restarting the emulator.

| Command | Aliases | Arguments | Description |
| :--- | :--- | :--- | :--- |
| `feature list` | | | Display all available features with their current enabled/disabled state. Features control various emulator subsystems and rendering layers. |
| `feature <name> on` | | `<feature-name> on` | Enable a specific feature by name. Common features include:<br/>• `screen` - screen rendering<br/>• `border` - border rendering<br/>• `beeper` - beeper sound<br/>• `ay` - AY-3-8912 sound chip<br/>• `tape` - tape I/O<br/>• `disk` - disk I/O<br/>• `keyboard` - keyboard input<br/>Changes take effect immediately. |
| `feature <name> off` | | `<feature-name> off` | Disable a specific feature. Useful for isolating subsystem behavior or improving performance during debugging. |
| `feature all on` | | | Enable all features at once. |
| `feature all off` | | | Disable all features (effectively freezes emulation). Use with caution. |

**Implementation Details**:

**Feature Manager**:
- Implemented via `FeatureManager` class
- Features are registered at emulator initialization
- Each feature has:
  - Unique string name (case-insensitive)
  - Boolean enabled/disabled state
  - Optional callback for state change notification
  - Optional dependency on other features

**Common Features**:

| Feature Name | Subsystem | Purpose | Performance Impact |
| :--- | :--- | :--- | :--- |
| `screen` | Video | Render screen bitmap (256x192 pixels) | High (~30% CPU) |
| `border` | Video | Render border area | Low (~5% CPU) |
| `attr` | Video | Process color attributes | Low (~3% CPU) |
| `beeper` | Audio | 1-bit beeper sound emulation | Medium (~10% CPU) |
| `ay` | Audio | AY-3-8912 PSG emulation | Medium (~15% CPU) |
| `tape` | I/O | Tape loading/saving | Low (~2% CPU when active) |
| `disk` | I/O | FDD/HDD emulation | Low (~5% CPU when active) |
| `keyboard` | Input | Keyboard matrix scanning | Minimal (<1% CPU) |

**Use Cases**:

1. **Performance Profiling**: Disable features to identify bottlenecks
   ```
   feature all off
   feature screen on  # Test screen rendering overhead
   ```

2. **Audio Debugging**: Isolate sound subsystems
   ```
   feature ay off      # Disable AY chip, keep beeper
   ```

3. **Headless Emulation**: Run without rendering for automation
   ```
   feature screen off
   feature border off
   ```

4. **I/O Troubleshooting**: Disable tape/disk to test programs without I/O
   ```
   feature tape off
   feature disk off
   ```

**Notes**:
- Feature states persist across pause/resume
- Some features may have dependencies (e.g., `screen` requires `video` subsystem)
- Disabling core features (like `cpu`) may hang the emulator
- Feature changes logged to debug output
- Python/Lua bindings provide programmatic feature control

### 6. Emulator Instance Settings

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
