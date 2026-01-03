# Universal Debug Bridge (UDB) Protocol

## Overview

The Universal Debug Bridge is a custom high-performance binary protocol inspired by Android Debug Bridge (ADB) but designed specifically for retro computer emulation. It extends beyond GDB capabilities to provide streaming, profiling, advanced analysis, and time-travel debugging.

**Status**: 🔮 Planned (Q3-Q4 2026)  
**Inspiration**: Android Debug Bridge (ADB)  
**Protocol**: Custom binary protocol over TCP  
**Port**: Configurable (default: 5555)  
**Design Goal**: Advanced debugging, profiling, and analysis for power users  

## Architecture

```
┌───────────────────────────────────────┐
│  UDB Client Tools                     │
│  • udb-cli (command-line tool)        │
│  • udb-gui (graphical debugger)       │
│  • IDE plugins                        │
│  • Analysis scripts                   │
└────────────┬──────────────────────────┘
             │ UDB Protocol (Binary/TCP)
             ▼
┌───────────────────────────────────────┐
│  AutomationUDB                        │
│  • Protocol multiplexer               │
│  • Stream manager                     │
│  • Command dispatcher                 │
│  • Compression engine                 │
└────────────┬──────────────────────────┘
             │
             ▼
┌───────────────────────────────────────┐
│  Emulator Core + Extensions           │
│  • Execution tracer                   │
│  • Performance profiler               │
│  • Memory analyzer                    │
│  • Event recorder                     │
└───────────────────────────────────────┘
```

## Why UDB vs GDB?

### Advantages Over GDB

| Feature | GDB RSP | UDB Protocol |
| :--- | :--- | :--- |
| **Protocol** | Text-based | Binary |
| **Performance** | ~1MB/sec | ~100MB/sec |
| **Streaming** | No | Yes (real-time) |
| **Compression** | No | Zstandard |
| **Multiplexing** | No | Yes (multiple streams) |
| **Profiling** | Limited | Built-in |
| **Tracing** | No | Full execution trace |
| **Time-Travel** | No | Reverse execution |
| **Batch Ops** | No | Yes (atomic) |
| **Scripting** | Python (external) | Embedded Lua/Python |

### Use Cases

1. **Reverse Engineering**: Deep analysis of unknown software
2. **Performance Optimization**: Profile Z80 code, find hotspots
3. **Quality Assurance**: Automated testing with complex scenarios
4. **Research**: Academic study of retro computing
5. **Tool Development**: Build custom analysis tools
6. **Game Hacking**: Cheat/trainer development
7. **Education**: Advanced debugging courses

## Protocol Specification

### Packet Structure

```
┌───────────────────────────────────────────┐
│  Header (16 bytes)                        │
├───────────────────────────────────────────┤
│  Magic      (4 bytes)   0x55444200 "UDB" │
│  Version    (2 bytes)   Protocol version  │
│  Flags      (2 bytes)   Compression, etc. │
│  Command    (2 bytes)   Command ID        │
│  Channel    (2 bytes)   Stream channel    │
│  Length     (4 bytes)   Payload length    │
├───────────────────────────────────────────┤
│  Payload (variable)                       │
│  • Command parameters                     │
│  • Stream data                            │
│  • Response data                          │
├───────────────────────────────────────────┤
│  Checksum (4 bytes)     CRC32             │
└───────────────────────────────────────────┘
```

### Command Categories

| Category | ID Range | Purpose |
| :--- | :--- | :--- |
| System | 0x0000-0x00FF | Connection, version, capabilities |
| Execution | 0x0100-0x01FF | Start, stop, step, reset |
| Memory | 0x0200-0x02FF | Read, write, search, compare |
| Breakpoints | 0x0300-0x03FF | Set, remove, enable, disable |
| Streaming | 0x0400-0x04FF | Start/stop data streams |
| Profiling | 0x0500-0x05FF | CPU profiling, hotspot analysis |
| Tracing | 0x0600-0x06FF | Execution trace recording |
| Scripting | 0x0700-0x07FF | Execute scripts, callbacks |
| Batch | 0x0800-0x08FF | Atomic multi-command execution |

### Flags

```
Bit 0:  Compressed     (Zstandard compression)
Bit 1:  Encrypted      (Reserved for future)
Bit 2:  Fragmented     (Multi-packet message)
Bit 3:  Requires ACK   (Wait for acknowledgment)
Bit 4:  Stream Start   (Begin streaming)
Bit 5:  Stream End     (End streaming)
Bit 6-15: Reserved
```

## Core Commands

### System Commands

#### HELLO (0x0001)
Establish connection and negotiate capabilities.

**Request**:
```
Protocol version:  uint16
Client ID:         string (max 64 bytes)
Client version:    string (max 32 bytes)
```

**Response**:
```
Server version:    uint16
Server ID:         string
Features:          uint64 (bitmask)
Max packet size:   uint32
```

#### CAPABILITIES (0x0002)
Query supported features.

**Response**:
```
Feature flags:     uint64
  Bit 0:  Streaming
  Bit 1:  Compression
  Bit 2:  Profiling
  Bit 3:  Tracing
  Bit 4:  Time-travel
  Bit 5:  Batch operations
  Bit 6:  Scripting
  Bit 7-63: Reserved
```

### Execution Commands

#### STEP (0x0101)
Execute N instructions.

**Request**:
```
Count:            uint32  (instruction count)
Mode:             uint8   (0=normal, 1=into, 2=over)
```

**Response**:
```
PC:               uint16  (final PC)
Cycles:           uint32  (CPU cycles elapsed)
```

#### RUN_UNTIL (0x0102)
Run until condition met.

**Request**:
```
Condition type:   uint8   (0=address, 1=cycle, 2=frame)
Condition value:  uint64
Timeout:          uint32  (milliseconds)
```

**Response**:
```
Status:           uint8   (0=met, 1=timeout)
PC:               uint16
```

### Memory Commands

#### MEM_READ_FAST (0x0200)
High-speed bulk memory read (compressed).

**Request**:
```
Address:          uint16
Length:           uint32
```

**Response**:
```
Data:             bytes (compressed if flag set)
```

#### MEM_STREAM (0x0201)
Continuous memory streaming on channel.

**Request**:
```
Address:          uint16
Length:           uint32
Rate:             uint16  (updates per second)
Channel:          uint16
```

**Stream Data** (on specified channel):
```
Timestamp:        uint64  (microseconds)
Changed bytes:    variable (delta encoding)
```

#### MEM_SEARCH (0x0202)
Search memory for pattern.

**Request**:
```
Start address:    uint16
End address:      uint16
Pattern:          bytes
Mask:             bytes (optional)
Max results:      uint16
```

**Response**:
```
Count:            uint16
Addresses:        uint16[] (array of matches)
```

### Profiling Commands

#### PROFILE_START (0x0500)
Begin CPU profiling.

**Request**:
```
Mode:             uint8
  0: Execution count per address
  1: Time spent per address
  2: Call graph
  3: Full trace
```

**Response**:
```
Session ID:       uint32
```

#### PROFILE_STOP (0x0501)
Stop profiling and retrieve results.

**Request**:
```
Session ID:       uint32
```

**Response**:
```
Duration:         uint64  (microseconds)
Instructions:     uint64  (total executed)
Profile data:     variable (format depends on mode)
```

**Profile Data Format (Mode 0)**:
```
Entry count:      uint32
For each entry:
  Address:        uint16
  Hit count:      uint32
  Percentage:     float32
```

#### HOTSPOT_ANALYZE (0x0502)
Identify CPU hotspots.

**Request**:
```
Top N:            uint16
Minimum %:        float32
```

**Response**:
```
Entry count:      uint16
For each entry:
  Address:        uint16
  Instructions:   uint32
  Time (µs):      uint64
  % of total:     float32
```

### Tracing Commands

#### TRACE_RECORD_START (0x0600)
Begin recording execution trace.

**Request**:
```
Capture mode:     uint8
  Bit 0: Instructions
  Bit 1: Memory reads
  Bit 2: Memory writes
  Bit 3: Port I/O
  Bit 4: Interrupts
Buffer size:      uint32  (MB)
Filename:         string  (for disk recording)
```

**Response**:
```
Session ID:       uint32
```

#### TRACE_RECORD_STOP (0x0601)
Stop recording trace.

**Response**:
```
Total entries:    uint64
File size:        uint64  (bytes)
```

#### TRACE_REPLAY_LOAD (0x0602)
Load trace for time-travel debugging.

**Request**:
```
Filename:         string
```

**Response**:
```
Total entries:    uint64
Start state:      serialized CPU/memory state
```

#### TRACE_SEEK (0x0603)
Seek to specific point in trace.

**Request**:
```
Position:         uint64  (entry index)
Mode:             uint8   (0=absolute, 1=relative)
```

**Response**:
```
New position:     uint64
Current state:    serialized CPU state
```

### Scripting Commands

#### SCRIPT_EXECUTE (0x0700)
Execute Lua/Python script with full access.

**Request**:
```
Language:         uint8   (0=Lua, 1=Python)
Script:           string  (script code)
Async:            bool    (run async)
```

**Response**:
```
Result:           string  (return value)
Output:           string  (stdout)
Error:            string  (stderr if failed)
```

#### SCRIPT_CALLBACK_SET (0x0701)
Register script callback for event.

**Request**:
```
Event type:       uint8
  0: Breakpoint hit
  1: Memory write
  2: Port I/O
  3: Frame complete
  4: Custom condition
Event filter:     variable (depends on type)
Script:           string  (callback code)
```

**Response**:
```
Callback ID:      uint32
```

### Batch Commands

#### BATCH_EXECUTE (0x0800)
Execute multiple commands atomically.

**Request**:
```
Command count:    uint16
For each command:
  Command ID:     uint16
  Payload length: uint32
  Payload:        bytes
```

**Response**:
```
Results count:    uint16
For each result:
  Status:         uint8   (0=success, error code)
  Result length:  uint32
  Result data:    bytes
```

## Streaming Architecture

### Channel Multiplexing

UDB supports up to 256 simultaneous stream channels:

| Channel | Purpose |
| :--- | :--- |
| 0 | Control (commands/responses) |
| 1 | Screen memory stream |
| 2 | Register updates |
| 3 | Audio samples |
| 4-255 | User-defined |

### Stream Types

#### Memory Stream
Continuous monitoring of memory region with delta encoding.

```
Start stream:     MEM_STREAM command
Stream data:      Only changed bytes transmitted
Stop stream:      STREAM_STOP command
```

**Bandwidth**: ~1-5 MB/sec with compression (for screen memory)

#### Register Stream
Real-time register updates (every N instructions).

```
Frequency:        1-1000 updates/sec
Data size:        ~20 bytes per update
Bandwidth:        ~20 KB/sec @ 1000 Hz
```

#### Trace Stream
Live execution trace (for real-time analysis).

```
Entry format:
  PC:             uint16
  Opcode:         uint8
  Operands:       uint16 (optional)
  Cycles:         uint8
```

**Bandwidth**: ~100-500 KB/sec @ 3.5 MHz Z80

## Compression

### Zstandard Compression

- **Algorithm**: Zstandard (Facebook)
- **Level**: Configurable (1-22), default 3
- **Ratio**: ~10:1 for memory dumps, ~5:1 for traces
- **Speed**: ~500 MB/sec compression, ~1000 MB/sec decompression

**Automatic Compression**:
- Enabled for packets > 1KB
- Indicated by compression flag in header
- Transparent to application layer

## Example Usage

### Command-Line Tool

```bash
# Connect to emulator
udb connect localhost:5555

# Start profiling
udb profile start execution

# Run for 1 second
udb run-for 1s

# Stop profiling and show hotspots
udb profile stop
udb hotspots --top 10

# Output:
# Address   Instructions  Time (µs)  % Total  Symbol
# 0x0E9B    1,234,567    523,192    12.5%   print_char
# 0x3C00    987,654      401,234    9.6%    keyboard_scan
# ...
```

### Streaming Example

```bash
# Stream screen memory to file
udb stream memory 0x4000 6912 --channel 1 --output screen.dat

# In another terminal: analyze stream
cat screen.dat | udb-analyze --type screen --fps 50
```

### Python Client Library

```python
import udb

# Connect
client = udb.Client("localhost", 5555)
client.connect()

# Start memory stream
stream = client.stream_memory(0x4000, 6912, channel=1)

# Process updates
for update in stream:
    print(f"Changed bytes: {len(update.changes)}")
    # Process screen data...

# Batch operations (atomic)
with client.batch() as batch:
    batch.set_breakpoint(0x8000)
    batch.set_breakpoint(0x9000)
    batch.resume()

# Profile code region
with client.profile() as prof:
    client.run_for(seconds=5)

print(f"Top hotspots:")
for addr, stats in prof.hotspots(top=10):
    print(f"  0x{addr:04X}: {stats.count:,} instructions")
```

### Trace Recording

```bash
# Record full execution trace
udb trace record --output game.trace --duration 10s

# Replay trace (time-travel debugging)
udb trace replay game.trace

# Seek to instruction 1,000,000
udb trace seek 1000000

# Examine state at that point
udb registers
udb memory 0x8000 256

# Step backwards
udb trace step -1

# Continue backwards
udb trace continue-back
```

### Scripting Example

```lua
-- Lua script: find all CALLs
local mem = udb.memory
local results = {}

for addr = 0x4000, 0xFFFF do
    local opcode = mem:read(addr)
    if opcode == 0xCD then  -- CALL instruction
        local target = mem:read_word(addr + 1)
        table.insert(results, {from = addr, to = target})
    end
end

-- Return results
return results
```

```bash
udb script execute find_calls.lua --output calls.json
```

## Performance Characteristics

### Throughput
- **Command Rate**: 10,000-100,000 commands/sec
- **Memory Transfer**: 100-500 MB/sec (compressed)
- **Stream Bandwidth**: 1-10 MB/sec per stream
- **Trace Recording**: 1M instructions/sec to disk

### Latency
- **Local Connection**: <1ms per command
- **Remote Connection**: 5-50ms (depends on network)
- **Stream Latency**: 1-10ms (real-time)

### Resource Usage
- **Memory**: ~10-50 MB (depending on buffers)
- **CPU**: ~5-10% (1 core for protocol handling)
- **Disk**: ~1-10 GB/min (trace recording)

## Security

### Authentication (Planned)
```
Token-based:      Client sends auth token
Certificate:      TLS with client cert
API Key:          Long-lived API key
```

### Encryption (Planned)
```
TLS 1.3:          Encrypted transport
End-to-end:       Payload encryption
Key exchange:     ECDHE
```

### Access Control
```
Read-only:        Query only (no execution control)
Debug:            Full debugging access
Admin:            Server configuration
```

## Client Tools

### udb-cli
Command-line interface (similar to adb CLI).

### udb-gui
Graphical debugger with:
- Live disassembly view
- Memory editor
- Register inspector
- Breakpoint manager
- Performance profiler
- Trace viewer

### IDE Plugins
- VS Code extension
- IntelliJ IDEA plugin
- Sublime Text plugin

### Analysis Tools
- `udb-analyze`: Statistical analysis
- `udb-trace`: Trace file viewer
- `udb-profile`: Profiler visualizer
- `udb-export`: Export to various formats

## Protocol Extensions

### Future Capabilities

1. **Multi-Emulator Sync**: Coordinate multiple emulators
2. **Network Emulation**: Simulated serial/network links
3. **Hardware Simulation**: Attach virtual peripherals
4. **Collaborative Debugging**: Multiple clients, shared session
5. **Cloud Integration**: Remote debugging over internet

## See Also

- [Command Interface Overview](./command-interface.md)
- [GDB Protocol](./gdb-protocol.md)
- [CLI Interface](./cli-interface.md)
- [Python Bindings](./python-interface.md)
- [Android Debug Bridge (ADB)](https://developer.android.com/studio/command-line/adb)
- [Zstandard Compression](https://facebook.github.io/zstd/)
