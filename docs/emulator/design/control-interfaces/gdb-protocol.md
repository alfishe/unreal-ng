# GDB Remote Serial Protocol (RSP)

## Overview

The GDB Remote Serial Protocol support enables compatible GDB/LLDB-based debuggers and IDEs to connect to the emulator as if debugging a physical Z80 target. Because Z80 is not an architecture in upstream GDB, client compatibility is conditional (see [Client Compatibility](#client-compatibility) below). When a compatible client is available, this provides professional debugging workflows using industry-standard tools.

**Status**: 🔮 Planned (Q2 2026)
**Protocol**: GDB Remote Serial Protocol (RSP)
**Transport**: TCP socket (default port: 1234)
**Standard**: [GDB Remote Protocol Documentation](https://sourceware.org/gdb/current/onlinedocs/gdb/Remote-Protocol.html)
**Client support**: Conditional — requires a Z80-capable GDB build (e.g. the `gdb-z80` community fork) or IDA Pro 9.x with a Z80 processor module consuming the served `target.xml`. Upstream stock GDB has no Z80 architecture. See [Client Compatibility](#client-compatibility) below.

**Note**: This interface provides GDB protocol compatibility for professional debugging tools. See [command-interface.md](./command-interface.md) for the underlying emulator command capabilities that GDB clients will access through this protocol.  

## Architecture

```
┌─────────────────────────────┐
│  GDB/LLDB Client            │
│  or IDE (VS Code, CLion)    │
└──────────┬──────────────────┘
           │ GDB RSP (TCP)
           ▼
┌─────────────────────────────┐
│  AutomationGDB              │
│  • Packet parser            │
│  • Command dispatcher       │
│  • Z80 → GDB register map   │
└──────────┬──────────────────┘
           │
           ▼
┌─────────────────────────────┐
│  Emulator Core              │
│  • Z80 CPU                  │
│  • Memory                   │
│  • Breakpoint Manager       │
└─────────────────────────────┘
```

## Why GDB Protocol?

### Benefits

1. **Industry Standard**: Use professional debugging tools
2. **IDE Integration**: VS Code, CLion, IntelliJ IDEA support
3. **Familiar Workflow**: Standard GDB commands and experience
4. **Remote Debugging**: Debug emulator on different machine
5. **Scriptable**: GDB Python API for automation
6. **Multi-Platform**: Works on Windows, Linux, macOS

### Use Cases

- **Professional Development**: Debug Z80 assembly with full IDE support
- **Remote Development**: Debug emulator running on server
- **Automated Testing**: GDB Python scripts for test automation
- **Team Collaboration**: Share debugging sessions
- **Education**: Teach debugging with familiar tools

## Client Compatibility

Z80 is not an architecture in upstream GDB, and not all GDB-compatible IDEs ship Z80-aware debugger modules. The stub implements the protocol correctly; compatibility is gated by the client side.

| Client | Status | Notes |
| :--- | :--- | :--- |
| Upstream stock GDB (any version) | ❌ | Z80 is not in the upstream architecture tree; no `org.gnu.gdb.z80.*` feature is recognized. |
| `gdb-z80` community fork | ⚠️ Best-effort | Unmaintained; protocol matches but `target.xml` parsing may regress between GDB releases. |
| LLDB | ⚠️ Partial | Register / memory / breakpoint packets work. **Reverse execution is unsupported** (LLDB has no reverse mode in its GDB-remote client). |
| IDA Pro 9.x — Remote GDB Debugger + Z80 processor module | ✅ Conditional | Works when IDA's Z80 processor module is installed and configured (`dbg_gdb.cfg`); the stub must serve a correct `target.xml`. |
| Ghidra — GDB "Remote" launcher | ⚠️ Conditional | Requires a locally-installed Z80-capable GDB (e.g. the fork above); set via `set architecture` before `target remote`. |
| `gdbgui` / `gdb-frontend` and similar | ⚠️ Conditional | Same upstream-GDB limitation as Ghidra.

**Implication for the stub implementer:** do not assume the client understands Z80-specific quirks. Always serve a complete `target.xml` so the client can learn the register layout dynamically, and prefer standard RSP packets over extensions.

## Protocol Specification

### Connection Flow

1. **Server Start**: Emulator opens TCP socket (port 1234)
2. **Client Connect**: GDB connects to `localhost:1234`
3. **Handshake**: GDB queries target capabilities
4. **Session**: Exchange RSP packets
5. **Disconnect**: GDB detaches or emulator stops

### Packet Format

**Basic Packet Structure**:
```
$<data>#<checksum>
```

**Examples**:
```
$g#67                    - Read all registers
$m8000,100#XX            - Read 256 bytes from 0x8000
$Z0,8000,1#XX            - Set breakpoint at 0x8000
$c#63                    - Continue execution
```

**Acknowledgment**:
```
+                        - ACK (packet received correctly)
-                        - NACK (retransmit)
```

### Target XML Description (`qXfer:features:read:target.xml`)

Modern debuggers (IDA Pro 9.x, Ghidra) and multi-arch GDB builds require a dynamic register layout definition. The emulator provides this via the `qXfer:features:read:target.xml` capability (assembled at client connect based on the active model).

### Register Mapping

Since GDB does not define a single canonical Z80 layout, the register map is explicitly defined by what our `target.xml` advertises. It includes the standard registers, shadow registers, and uses 16-bit sizing for pairs/index registers:

| GDB Reg # | Z80 Register | Size | Description |
| :--- | :--- | :--- | :--- |
| 0 | AF | 16-bit | Accumulator and Flags |
| 1 | BC | 16-bit | BC register pair |
| 2 | DE | 16-bit | DE register pair |
| 3 | HL | 16-bit | HL register pair |
| 4 | AF' | 16-bit | Alternate AF |
| 5 | BC' | 16-bit | Alternate BC |
| 6 | DE' | 16-bit | Alternate DE |
| 7 | HL' | 16-bit | Alternate HL |
| 8 | IX | 16-bit | Index register X |
| 9 | IY | 16-bit | Index register Y |
| 10 | SP | 16-bit | Stack pointer |
| 11 | PC | 16-bit | Program counter |
| 12 | I | 8-bit | Interrupt vector |
| 13 | R | 8-bit | Refresh register |

**Register Packet Format** (for `g` command):
```
AFAABBCCDDEEHHLLAFAABBCCDDEEHHLLIXIXIYIYSPSPPCPCIIRR
```
All values in hex, little-endian where applicable.

## Supported GDB Commands

### Implemented Packets (Planned)

#### Essential Commands
| Packet | Command | Description |
| :--- | :--- | :--- |
| `?` | Status | Query halt reason |
| `g` | Read registers | Read all registers |
| `G<data>` | Write registers | Write all registers |
| `m<addr>,<len>` | Read memory | Read memory block |
| `M<addr>,<len>:<data>` | Write memory | Write memory block |
| `c` | Continue | Resume execution |
| `s` | Step | Single-step instruction |
| `k` | Kill | Terminate connection |

#### Breakpoint Commands
| Packet | Command | Description |
| :--- | :--- | :--- |
| `Z0,<addr>,<kind>` | Set SW breakpoint | Execution breakpoint |
| `z0,<addr>,<kind>` | Remove SW breakpoint | Remove execution BP |
| `Z2,<addr>,<kind>` | Set write watchpoint | Memory write |
| `z2,<addr>,<kind>` | Remove write WP | Remove write WP |
| `Z3,<addr>,<kind>` | Set read watchpoint | Memory read |
| `z3,<addr>,<kind>` | Remove read WP | Remove read WP |
| `Z4,<addr>,<kind>` | Set access watchpoint | Read or write |
| `z4,<addr>,<kind>` | Remove access WP | Remove access WP |

#### Stop Reply Commands
| Packet | Command | Description |
| :--- | :--- | :--- |
| `T<sig><key:val>` | Stop Reply | Formatted stop reason (e.g., `T05hwbreak:;thread:1;`) |

#### Query Commands
| Packet | Command | Description |
| :--- | :--- | :--- |
| `qSupported` | Supported features | Capability negotiation (advertises `qXfer:features:read+`, `qXfer:osdata:read+`, `ReverseStep+`, `ReverseContinue+`, `multiprocess-`) |
| `qAttached` | Attached status | Is process attached? |
| `qC` | Current thread | Current thread ID (1) |
| `qfThreadInfo` | Thread info | First thread (1) |
| `qsThreadInfo` | Thread info | Subsequent threads (none) |
| `qOffsets` | Section offsets | Text/data offsets (0) |
| `qXfer:features:read` | Read features | Read `target.xml` Z80 description |
| `qXfer:osdata:read` | Read OS data | Read emulator instance list (`processes`) |
| `qRcmd` | Remote command | Execute emulator commands (e.g., `bankinfo`) |

### Extended Features

#### Reverse Execution (Time-Travel Debugging)

Reverse-execution packets let a GDB/LLDB client navigate the TTD timeline exposed by the emulator. They are forwarded to the same `TimeTravelManager` that powers the CLI / WebAPI / Python / Lua surfaces (see [command-interface.md §8](./command-interface.md#8-time-travel-debugging-ttd) and the [time-travel TDD](../debugger/time-travel-debug/time-travel-debugging-tdd.md) §10.4).

**Capability negotiation:** the GDB server advertises reverse-exec support in its `qSupported` reply only when both conditions hold:
1. The `ENABLE_GDB_AUTOMATION` build-time gate is on (so the server is built).
2. The runtime `timetravel` feature flag is ON *and* an active recording session exists.

If either condition is false, `ReverseStep`/`ReverseContinue` are omitted from `qSupported` and the packets below return `\x00` (unsupported), so GDB hides its `reverse-step`/`reverse-continue` commands — matching the user-visible state of the emulator.

| Packet | Command | Description |
| :--- | :--- | :--- |
| `bc` | Backward continue (`reverse-continue`, `rc`) | Resume backwards; stop at the next breakpoint/watchpoint hit *before* the current position, or at the session's first frame. |
| `bs` | Backward step (`reverse-step`, `rs`) | Step one instruction backwards. Implemented via `StepBackInstruction()` (TDD §8.1). |

**Stop replies** in reverse mode use the same `T<sig>` format as forward execution. The emulator reports halt reasons that mirror the CLI/WebAPI `halt_reason` field:

- `target` → standard stop reply (e.g. `T05hwbreak:;`).
- `external_event` → stop reply with standard `T05` (SIGTRAP). The TTD-specific reason is surfaced via `monitor ttd status`.
- `out_of_range` → `E22` (EINVAL); the client stays at its current position.

**`monitor` commands** (via `qRcmd`) map the CLI `ttd` surface into GDB:

| GDB `monitor` command | Equivalent CLI verb |
| :--- | :--- |
| `monitor ttd status` | `ttd status` |
| `monitor ttd start` | `ttd start` |
| `monitor ttd stop` | `ttd stop` |
| `monitor ttd clear` | `ttd clear` |
| `monitor ttd seek <frame> [<tstate>]` | `ttd seek --frame N [--tstate T]` |
| `monitor ttd step-back [instruction\|frame]` | `ttd step-back [--unit ...]` |
| `monitor ttd find-last <addr> <write\|read\|execute\|out> [value=V] [pc=A..B]` | `ttd find-last ...` |
| `monitor ttd bookmark add\|remove\|list ...` | `ttd bookmark ...` |
| `monitor ttd resume-from-here` | `ttd resume-from-here` |

`monitor ttd find-last` additionally positions the client at the match (equivalent to `ttd seek --frame N --tstate T` after the search) so the user can immediately inspect registers / memory.

**GDB-side usage example** (recording a demo run and stepping backwards):

```
(gdb) target remote | /path/to/emu-gdbserver --gdb --emulator 1
Remote debugging using ...

(gdb) monitor ttd start
[gdbserver] armed: capture begins at next frame boundary

(gdb) continue
Continuing.
^C
Program received signal SIGINT, Interrupt.
0x4a21 in ?? ()

(gdb) monitor ttd find-last 0x5b00 write
[gdbserver] frame=4823 tstate=14982 pc=0x4a21 value=0x07 physpage=5
0x4a21 in ?? ()

(gdb) reverse-step
0x4a1e in ?? ()

(gdb) info registers
```

**Run-control claim:** the GDB server holds the run-control claim (Sprint 0 mechanism) while the client is connected and the emulator is paused. Reverse-exec packets are refused with `E01` if another surface (e.g. CLI) holds the claim; GDB surfaces this as a generic "Remote failure" error.

**Session invalidation:** if the recording session is invalidated mid-reverse-exec (e.g. by a `load snapshot` from another surface), the in-flight `bc`/`bs` is aborted with `E01` (generic error). The explicit reason ("TTD session invalidated") can be retrieved via `monitor ttd status`. The client must re-issue `monitor ttd start` to resume recording.

#### Multi-Process (multiple emulators)
For v1, multi-process is not natively supported via GDB multi-process extensions (`qSupported` advertises `multiprocess-`). Instead, multi-instance attach is managed externally (e.g., via IDA's separate emulator attach).

| Packet | Command | Description |
| :--- | :--- | :--- |
| `H<op><thread>` | Set thread | Select emulator instance. Not legacy — GDB sends this before every `g`/`m`/`c` series even in single-process mode; the stub accepts and returns `OK`. |
| `vAttach;<pid>` | Attach | Attach to emulator instance. Only sent by clients when `multiprocess+` is advertised; since v1 advertises `multiprocess-`, this packet will not arrive in practice but is handled defensively. |

## Connection Examples

### Using GDB Command Line

```bash
# Start GDB with Z80 target
z80-elf-gdb

# Connect to emulator
(gdb) target remote localhost:1234
Remote debugging using localhost:1234
0x0000 in ?? ()

# Set breakpoint
(gdb) break *0x8000
Breakpoint 1 at 0x8000

# Continue execution
(gdb) continue
Continuing.

# Breakpoint hit
Breakpoint 1, 0x8000 in ?? ()

# Examine registers
(gdb) info registers
a              0x44    68
f              0xc4    196
bc             0x3f00  16128
de             0x0000  0
hl             0x5c00  23552
pc             0x8000  32768

# Examine memory
(gdb) x/16xb 0x8000
0x8000: 0x00 0x00 0xc3 0xcb 0x11 0xff 0x00 0x00
0x8008: 0xfe 0x03 0x20 0x1a 0x07 0xa9 0x6f 0x10

# Disassemble
(gdb) disassemble 0x8000,+32
Dump of assembler code from 0x8000 to 0x8020:
   0x8000:  nop
   0x8001:  nop
   0x8002:  jp 0xcbc3
   ...

# Single step
(gdb) stepi
0x8001 in ?? ()

# Watch memory location
(gdb) watch *0x5c00
Hardware watchpoint 2: *0x5c00

# Continue until watchpoint
(gdb) continue
Continuing.
Hardware watchpoint 2: *0x5c00
Old value = 0
New value = 42

# Backtrace (if call stack tracking enabled)
(gdb) backtrace
#0  0x8000 in ?? ()
#1  0x0605 in ?? ()
#2  0x0000 in ?? ()

# Disconnect
(gdb) detach
(gdb) quit
```

### Using LLDB

```bash
lldb

# Connect
(lldb) gdb-remote localhost:1234

# Same commands as GDB
(lldb) breakpoint set --address 0x8000
(lldb) continue
(lldb) register read
(lldb) memory read 0x8000
```

### VS Code Integration

`.vscode/launch.json`:
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Z80 Emulator Debug",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/program.z80",
            "miDebuggerServerAddress": "localhost:1234",
            "miDebuggerPath": "/usr/bin/z80-elf-gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ],
            "preLaunchTask": "start-emulator"
        }
    ]
}
```

`.vscode/tasks.json`:
```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "start-emulator",
            "type": "shell",
            "command": "unreal-emulator --gdb-server",
            "isBackground": true
        }
    ]
}
```

### CLion / IntelliJ IDEA

**Run Configuration**:
1. Run → Edit Configurations
2. Add "Remote Debug"
3. Set target: `localhost:1234`
4. Set symbol file (if available)
5. Debug → Start Remote Debug

## GDB Python Scripting

### Automated Testing

```python
# test_script.py
import gdb

# Connect
gdb.execute("target remote localhost:1234")

# Set breakpoint
bp = gdb.Breakpoint("*0x8000")

# Continue
gdb.execute("continue")

# Check register value
af = gdb.parse_and_eval("$af")
assert af == 0x44C4, f"Expected AF=0x44C4, got {af}"

# Read memory
mem = gdb.selected_inferior().read_memory(0x8000, 16)
print(f"Memory at 0x8000: {mem.hex()}")

# Step through code
for i in range(10):
    gdb.execute("stepi")
    pc = gdb.parse_and_eval("$pc")
    print(f"Step {i}: PC=0x{pc:04X}")

print("Test passed!")
```

**Run Script**:
```bash
z80-elf-gdb -batch -x test_script.py
```

### Live Monitoring

```python
# monitor.py
import gdb
import time

class PCMonitor(gdb.Command):
    """Monitor PC register"""
    
    def __init__(self):
        super().__init__("monitor-pc", gdb.COMMAND_USER)
    
    def invoke(self, arg, from_tty):
        while True:
            pc = gdb.parse_and_eval("$pc")
            print(f"PC = 0x{pc:04X}")
            time.sleep(0.1)
            gdb.execute("continue")

PCMonitor()
```

## Implementation Details

### Server Architecture

```cpp
class AutomationGDB {
    // TCP server
    SOCKET _serverSocket;
    SOCKET _clientSocket;
    
    // Packet handling
    std::string receivePacket();
    void sendPacket(const std::string& data);
    bool verifyChecksum(const std::string& packet);
    
    // Command handlers
    void handleReadRegisters();
    void handleWriteRegisters(const std::string& data);
    void handleReadMemory(uint16_t addr, uint16_t len);
    void handleWriteMemory(uint16_t addr, const std::vector<uint8_t>& data);
    void handleContinue();
    void handleStep();
    void handleSetBreakpoint(uint16_t addr);
    void handleRemoveBreakpoint(uint16_t addr);
    
    // State management
    EmulatorState _lastState;
    bool _isAttached;
    
    // Emulator reference
    std::shared_ptr<Emulator> _emulator;
};
```

### Packet Parser

```cpp
std::string AutomationGDB::receivePacket() {
    std::string packet;
    char ch;
    
    // Wait for '$'
    do {
        recv(_clientSocket, &ch, 1, 0);
    } while (ch != '$');
    
    // Read until '#'
    while (true) {
        recv(_clientSocket, &ch, 1, 0);
        if (ch == '#') break;
        packet += ch;
    }
    
    // Read checksum
    char checksum[3] = {0};
    recv(_clientSocket, checksum, 2, 0);
    
    // Verify checksum
    if (verifyChecksum(packet + checksum)) {
        send(_clientSocket, "+", 1, 0);  // ACK
        return packet;
    } else {
        send(_clientSocket, "-", 1, 0);  // NACK
        return "";
    }
}
```

### Register Serialization

```cpp
std::string AutomationGDB::serializeRegisters() {
    Z80* cpu = _emulator->GetCPU();
    
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    auto emit16 = [&ss](uint16_t val) {
        ss << std::setw(2) << (val & 0xFF); // Little-endian
        ss << std::setw(2) << (val >> 8);
    };

    // Main register pairs: AF, BC, DE, HL
    emit16(cpu->GetAF());
    emit16(cpu->GetBC());
    emit16(cpu->GetDE());
    emit16(cpu->GetHL());

    // Alternate (shadow) register pairs: AF', BC', DE', HL'
    emit16(cpu->GetAFPrime());
    emit16(cpu->GetBCPrime());
    emit16(cpu->GetDEPrime());
    emit16(cpu->GetHLPrime());
    
    // IX, IY
    emit16(cpu->GetIX());
    emit16(cpu->GetIY());
    
    // SP, PC
    emit16(cpu->GetSP());
    emit16(cpu->GetPC());
    
    // I, R (8-bit)
    ss << std::setw(2) << cpu->GetI();
    ss << std::setw(2) << cpu->GetR();
    
    return ss.str();
}
```

## Performance Characteristics

- **Connection Latency**: ~1-5ms (local)
- **Packet Overhead**: ~10-20 bytes per packet
- **Step Performance**: ~100-1000 steps/second
- **Throughput**: ~100KB/sec memory transfers

## Security Considerations

### Current Plan
- Bind to localhost by default (127.0.0.1)
- No authentication (local-only by default)
- Plain text protocol

### Production Recommendations
1. **SSH Tunneling**: For remote debugging
   ```bash
   ssh -L 1234:localhost:1234 remote_host
   ```
2. **Firewall**: Block port 1234 from external access
3. **VPN**: Use VPN for trusted remote access

## Limitations

1. **Single Thread**: Z80 is single-threaded (GDB expects thread ID 1)
2. **No MMU**: Z80 has a flat 64KB logical memory space but uses banking. Memory layout is currently queried via `qRcmd bankinfo`. However, because GDB breakpoint placement across pages is genuinely ambiguous for banked Z80 models, `qXfer:memory-map:read` support should be considered as some clients natively consume it to disambiguate banking and hardware/software breakpoints.
3. **No FPU**: Z80 has no floating-point (GDB FP commands not applicable)
4. **Limited Symbols**: Symbol support requires external .map files
5. **No Source-Level Debug**: Without debug info, only assembly debugging

## Troubleshooting

### Connection Refused
- Check emulator is running with GDB server enabled
- Verify port 1234 is not in use
- Check firewall rules

### Register Values Wrong
- Verify Z80 register mapping matches GDB expectations
- Check endianness (little-endian for 16-bit values)
- Ensure emulator is paused before reading

### Breakpoints Not Hit
- Verify address is correct (hex format)
- Check breakpoint is enabled
- Ensure emulator is running (`continue` command)

### Slow Performance
- Reduce frequency of register/memory reads
- Use hardware breakpoints instead of single-stepping
- Disable unnecessary GDB features

## See Also

### Interface Documentation
- **[Command Interface Overview](./command-interface.md)** - Core command reference and architecture
- **[CLI Interface](./cli-interface.md)** - TCP-based text protocol for interactive debugging
- **[WebAPI Interface](./webapi-interface.md)** - HTTP/REST API for web integration
- **[Python Bindings](./python-interface.md)** - Direct C++ bindings for automation and AI/ML
- **[Lua Bindings](./lua-interface.md)** - Lightweight scripting and embedded logic
- **[Universal Debug Bridge Protocol](./udb-protocol.md)** - High-performance analysis and profiling

### Navigation
- **[Interface Documentation Index](./README.md)** - Overview of all control interfaces

### External Resources
- **[GDB Remote Protocol Specification](https://sourceware.org/gdb/current/onlinedocs/gdb/Remote-Protocol.html)** - Official GDB protocol documentation
- **[Z80 Architecture Guide](https://en.wikipedia.org/wiki/Zilog_Z80)** - Z80 processor architecture reference
