# GDB Remote Serial Protocol (RSP)

## Overview

The GDB Remote Serial Protocol support enables standard GDB/LLDB debuggers and IDEs to connect to the emulator as if debugging a physical Z80 target. This provides professional debugging workflows using industry-standard tools.

**Status**: 🔮 Planned (Q2 2026)  
**Protocol**: GDB Remote Serial Protocol (RSP)  
**Transport**: TCP socket (default port: 1234)  
**Standard**: [GDB Remote Protocol Documentation](https://sourceware.org/gdb/current/onlinedocs/gdb/Remote-Protocol.html)  

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

### Register Mapping

Z80 registers mapped to GDB register format:

| GDB Reg # | Z80 Register | Size | Description |
| :--- | :--- | :--- | :--- |
| 0 | A | 8-bit | Accumulator |
| 1 | F | 8-bit | Flags |
| 2 | B | 8-bit | B register |
| 3 | C | 8-bit | C register |
| 4 | D | 8-bit | D register |
| 5 | E | 8-bit | E register |
| 6 | H | 8-bit | H register |
| 7 | L | 8-bit | L register |
| 8 | IXH | 8-bit | IX high byte |
| 9 | IXL | 8-bit | IX low byte |
| 10 | IYH | 8-bit | IY high byte |
| 11 | IYL | 8-bit | IY low byte |
| 12 | SP | 16-bit | Stack pointer |
| 13 | PC | 16-bit | Program counter |
| 14 | I | 8-bit | Interrupt vector |
| 15 | R | 8-bit | Refresh register |

**Register Packet Format** (for `g` command):
```
AAFFBBCCDDEEHHLLIXHIXLIYHIYLSPSPPPCPCIIRR
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

#### Query Commands
| Packet | Command | Description |
| :--- | :--- | :--- |
| `qSupported` | Supported features | Capability negotiation |
| `qAttached` | Attached status | Is process attached? |
| `qC` | Current thread | Current thread ID (1) |
| `qfThreadInfo` | Thread info | First thread (1) |
| `qsThreadInfo` | Thread info | Subsequent threads (none) |
| `qOffsets` | Section offsets | Text/data offsets (0) |

### Extended Features

#### Reverse Execution (if history enabled)
| Packet | Command | Description |
| :--- | :--- | :--- |
| `bc` | Backward continue | Continue backwards |
| `bs` | Backward step | Step backwards |

#### Multi-Process (multiple emulators)
| Packet | Command | Description |
| :--- | :--- | :--- |
| `H<op><thread>` | Set thread | Select emulator instance |
| `vAttach;<pid>` | Attach | Attach to emulator instance |

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
    
    // A, F, B, C, D, E, H, L
    ss << std::setw(2) << cpu->GetA();
    ss << std::setw(2) << cpu->GetF();
    ss << std::setw(2) << cpu->GetB();
    ss << std::setw(2) << cpu->GetC();
    ss << std::setw(2) << cpu->GetD();
    ss << std::setw(2) << cpu->GetE();
    ss << std::setw(2) << cpu->GetH();
    ss << std::setw(2) << cpu->GetL();
    
    // IX, IY (split into high/low)
    uint16_t ix = cpu->GetIX();
    ss << std::setw(2) << (ix >> 8);
    ss << std::setw(2) << (ix & 0xFF);
    
    uint16_t iy = cpu->GetIY();
    ss << std::setw(2) << (iy >> 8);
    ss << std::setw(2) << (iy & 0xFF);
    
    // SP, PC (little-endian)
    uint16_t sp = cpu->GetSP();
    ss << std::setw(2) << (sp & 0xFF);
    ss << std::setw(2) << (sp >> 8);
    
    uint16_t pc = cpu->GetPC();
    ss << std::setw(2) << (pc & 0xFF);
    ss << std::setw(2) << (pc >> 8);
    
    // I, R
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
2. **No MMU**: Z80 has flat memory space (no virtual memory)
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

- [Command Interface Overview](./command-interface.md)
- [Universal Debug Bridge Protocol](./udb-protocol.md)
- [GDB Remote Protocol Specification](https://sourceware.org/gdb/current/onlinedocs/gdb/Remote-Protocol.html)
- [Z80 Architecture Guide](https://en.wikipedia.org/wiki/Zilog_Z80)
