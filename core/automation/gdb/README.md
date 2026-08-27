# GDB RSP Server

GDB Remote Serial Protocol server for debugging Z80 code running in the emulator.

## Quick Start

1. Start emulator and load your program
2. Connect with: `z80-elf-gdb -ex "target remote localhost:2000"`
3. Use standard GDB commands: `break`, `step`, `continue`, `info registers`

## Design Documents

- [GDB Protocol Specification](../../../docs/emulator/design/control-interfaces/gdb-protocol.md)
- [GDB Reverse Debugging TDD](../../../docs/emulator/design/debugger/time-travel-debug/gdb-reverse-debugging-tdd.md)

## Client Setup

### z88dk z80-elf-gdb (Recommended)

```bash
# Install z88dk toolchain which includes z80-elf-gdb
# https://github.com/z88dk/z88dk

z80-elf-gdb your_program.bin
(gdb) target remote localhost:2000
(gdb) break main
(gdb) continue
```

### IDA Pro 9.x

1. **Processor module**: Select Z80 processor when loading the binary
2. **Debugger → Switch debugger**: Choose "Remote GDB debugger"
3. **Debugger → Process options**:
   - Application: (leave blank for remote)
   - Hostname: `localhost`
   - Port: `2000`
4. **Debugger → Attach to process** or **Start process**

If IDA has trouble with instance selection, use dedicated port:
```
(IDA) monitor gdbport 1
> Allocated dedicated port: 2001
(IDA) Reconnect to localhost:2001
```

### Ghidra

1. Open your program in Ghidra
2. **Debugger → Debug Program → Connect to GDB**
3. Set connection: `localhost:2000`
4. Select Z80 language module if prompted

### VS Code + Native Debug

1. Install "Native Debug" extension
2. Configure `.vscode/launch.json`:
```json
{
    "type": "gdb",
    "request": "attach",
    "name": "Z80 Debug",
    "executable": "./your_program.bin",
    "target": "localhost:2000",
    "remote": true,
    "gdbpath": "z80-elf-gdb"
}
```

## Supported Commands

### Standard GDB

| Command | Description |
|---------|-------------|
| `g` | Read all registers |
| `G` | Write all registers |
| `p`/`P` | Read/write single register |
| `m`/`M` | Read/write memory |
| `c`/`s` | Continue / single step |
| `Z0`/`z0` | Set/remove execution breakpoint |
| `Z2-4`/`z2-4` | Set/remove watchpoints (write/read/access) |
| `?` | Query stop reason |
| `bs`/`bc` | Backward step / backward continue (TTD) |

### Monitor Commands

```
(gdb) monitor help
  monitor help          - show available commands
  monitor model         - show emulator model
  monitor status        - show emulator status
  monitor reset         - reset emulator (paused only)
  monitor instances     - list emulator instances
  monitor frame         - show frame/tstate info
  monitor bankinfo      - show memory bank info
  monitor load <path>   - load snap/tape/disk (paused only)
  monitor ttd status    - show TTD session info
  monitor ttd start     - start TTD recording
  monitor ttd stop      - stop TTD recording
  monitor ttd seek <frame> - seek to frame
  monitor ttd findlast <w|r|x> <addr> - find last access
  monitor bport <in|out> <port> - set port breakpoint
  monitor bport clear <id> - remove port breakpoint
```

## TTD (Time Travel Debugging)

Record execution and step backward through history:

```
(gdb) monitor ttd start          # Start recording
(gdb) continue                   # Run program
^C                               # Stop
(gdb) monitor ttd status         # Check recording
(gdb) bs                         # Step backward
(gdb) bc                         # Continue backward to breakpoint
(gdb) monitor ttd seek 50        # Jump to frame 50
```

## Physical Memory Access

Access RAM pages directly using extended address format `0x01PPAAAA`:
- `PP` = RAM page number (00-FF)
- `AAAA` = Offset within 16KB page (0000-3FFF)

```
(gdb) x/16xb 0x01050000    # Read 16 bytes from RAM page 5, offset 0
(gdb) set {char}0x01070100 = 0xFF  # Write to RAM page 7, offset 0x100
```

## Port Configuration

- Default port: `2000` (127.0.0.1 only - unauthenticated!)
- Config: `unreal.ini` → `[automation]` → `gdb_port`, `gdb_bind`, `gdb_autoattach`

## Build

```bash
cmake -DENABLE_GDB_AUTOMATION=ON ..
ninja unreal-qt
```

## Implementation Status

See [action-plan.md](../../../docs/inprogress/2026-08-26-automation-gaps/action-plan.md) Phase 1A.

| Feature | Status |
|---------|--------|
| RSP packet framing | Done |
| Register access | Done |
| Memory access | Done |
| Breakpoints/watchpoints | Done |
| TTD reverse debugging | Done |
| Multi-instance support | Done |
| Run-control claim | Done |
| Physical memory view | Done |
| Port breakpoints | Done |
