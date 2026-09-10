# GDB Protocol Verification

Automated verification tool for the GDB Remote Serial Protocol (RSP) implementation.

## Known Issues

### GDB 17.2 Z80 Frame Unwinding Bug

GDB 17.2 has a bug in Z80 frame unwinding that causes a crash:

```
../../gdb/trad-frame.h:144: internal-error: addr: Assertion `m_kind == trad_frame_saved_reg_kind::ADDR' failed.
```

This crash occurs when:
- Running `bt` (backtrace)
- Running `stepi` or `step`
- Any operation that triggers frame unwinding

**Workarounds:**
- Use GDB 15.x or earlier
- Use IDA Pro's GDB debugger
- Avoid backtrace operations

This is a GDB bug, not an issue with this server implementation.

## Usage

1. Start the emulator with GDB server enabled (default port 2000)
2. Run the verification script:

```bash
# Basic run
python3 verify_gdb_protocol.py

# With custom host/port
python3 verify_gdb_protocol.py --host 127.0.0.1 --port 2000

# Verbose output
python3 verify_gdb_protocol.py -v
```

## What It Tests

### Basic Handshake
- `qSupported` capability negotiation
- `QStartNoAckMode` enable

### Query Packets
- `qC` - current thread ID
- `qAttached` - attachment status
- `qOffsets` - section offsets
- `qSymbol::` - symbol lookup

### Trace Packets
Tests that trace-related packets return empty response (not timeout):
- `qTStatus`, `qTfV`, `qTsV`, `qTfP`, `qTsP`

### Thread Packets
- `qfThreadInfo` / `qsThreadInfo` - thread enumeration
- `Hg0` - thread selection
- `T01` - thread alive query

### Register Packets
- `?` - stop reason query
- `g` - read all registers
- `p0` - read single register

### Memory Packets
- `m0000,10` - read memory
- Error handling for invalid requests

### Execution Control
- `vCont?` - supported actions query

### V Packets
- `vMustReplyEmpty` - protocol compliance test

## Exit Codes

- `0` - All tests passed
- `1` - One or more tests failed

## Protocol Reference

- [GDB Remote Serial Protocol](https://sourceware.org/gdb/onlinedocs/gdb/Remote-Protocol.html)
- [Stop Reply Packets](https://sourceware.org/gdb/onlinedocs/gdb/Stop-Reply-Packets.html)

## Common Issues

### Timeout on trace packets
The server must return an empty packet `$#00` (not silence) for unsupported trace queries.

### Invalid hex digit in thread ID
Thread IDs in stop replies must be hex format (e.g., `thread:01;` not `thread:1;`).

### vMustReplyEmpty timeout
This packet specifically requires an empty packet response to pass GDB's compatibility check.
