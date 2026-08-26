# GDB RSP Server

GDB Remote Serial Protocol server for debugging Z80 code running in the emulator.

## Design Documents

- [GDB Protocol Specification](../../../docs/emulator/design/control-interfaces/gdb-protocol.md) - Protocol overview and command reference
- [GDB Reverse Debugging TDD](../../../docs/emulator/design/debugger/time-travel-debug/gdb-reverse-debugging-tdd.md) - Detailed technical design

## Client Compatibility

### Important: Z80 is NOT in upstream GDB

Stock GDB does not support Z80 architecture. To use this server, clients must be:

| Client | Status | Notes |
|--------|--------|-------|
| **z88dk z80-elf-gdb** | Primary target | Community Z80 GDB build; full support |
| **IDA Pro 9.x** | Must-work | Remote GDB debugger + Z80 processor module |
| **Ghidra** | Should-work | Native RSP client with Z80 language module |
| **VS Code/CLion** | Best-effort | Requires z80-gdb binary backend |
| **Stock GDB** | NOT supported | No Z80 architecture - cannot work |

### IDA Pro Setup

1. Install Z80 processor module
2. Configure `dbg_gdb.cfg` with target XML path (or use `monitor tdesc export`)
3. Set *Debugger → Setup → Set specific options* for remote GDB
4. Connect: `Debugger → Attach → Remote GDB debugger`

If IDA has trouble with instance selection:
```
(IDA) monitor gdbport 1
> Allocated dedicated port: 2001
(IDA) Reconnect to localhost:2001
```

### Compatibility Shims

The server automatically handles these client differences:

1. **Flattened XML**: Clients that fail on `xi:include` get `target-flat.xml`
2. **Register types**: Pseudo-registers use `int` type for conservative clients
3. **Ephemeral ports**: `monitor gdbport <pid>` for legacy `vAttach` issues
4. **Quirk flags**: Per-session flags for client-specific behavior (e.g., `watch:ADDR=VALUE`)

## Port Configuration

- Default: `2000` (127.0.0.1 only - unauthenticated protocol!)
- Config: `unreal.ini` → `[automation]` → `gdb_port`, `gdb_bind`, `gdb_autoattach`

## Files

```
include/
  automation-gdb.h      # Transport lifecycle (AutomationGDB class)
  gdbserver.h           # TCP listener + sessions
  gdbpacket.h           # RSP framing ($data#cs, ack, escaping)
  gdbdispatcher.h       # Packet → handler routing
  gdbtarget_z80.h       # Z80 register layout, target.xml

src/
  *.cpp                 # Implementations
```

## Build

Enable with CMake option:
```
cmake -DENABLE_GDB_AUTOMATION=ON ...
```

## Implementation Status

See [action-plan.md](../../../docs/inprogress/2026-08-26-automation-gaps/action-plan.md) Phase 1A for task tracking.
