# CLI (Command Line Interface) - TCP-based

## Overview

The CLI interface provides a human-friendly text-based protocol for interacting with the emulator over TCP sockets. It implements a REPL (Read-Eval-Print Loop) model similar to telnet/netcat sessions.

**Status**: ✅ Fully Implemented  
**Implementation**: `core/automation/cli/`  
**Port**: Configurable (default: TBD)  
**Protocol**: TCP socket, line-oriented text  

## Architecture

```
┌─────────────────┐
│  Client         │  (telnet, netcat, custom CLI tool)
│  Application    │
└────────┬────────┘
         │ TCP Connection
         ▼
┌─────────────────┐
│ AutomationCLI   │  TCP server, connection handling
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ ClientSession   │  Per-connection state
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ CLIProcessor    │  Command parsing & dispatch
└─────────────────┘
```

## Connection Flow

1. **Server Start**: AutomationCLI opens TCP socket on configured port
2. **Client Connect**: Client establishes TCP connection (telnet, netcat, etc.)
3. **Session Init**: ClientSession created with unique socket handle
4. **Auto-Select**: If single emulator exists, auto-selected for convenience
5. **Command Loop**: Client sends commands, receives responses
6. **Disconnection**: Client closes connection, session cleaned up

## Protocol Specification

### Message Format

**Command Format**:
```
<command> [arg1] [arg2] ... [argN]\n
```

**Response Format**:
```
<response-text>\r\n
```

### Line Endings
- **Client → Server**: LF (`\n`) or CRLF (`\r\n`) accepted
- **Server → Client**: CRLF (`\r\n`) always

### Argument Parsing
- Arguments separated by whitespace
- Quoted strings supported: `"argument with spaces"`
- Escape sequences: `\"` for literal quote
- Hex numbers: `0x` prefix (e.g., `0x8000`)
- Decimal numbers: no prefix (e.g., `32768`)

### Special Characters
- `#` - Comment (ignored to end of line)
- `;` - Command separator (not yet implemented)

## Session Management

### Session State
Each TCP connection maintains:
- Selected emulator ID
- Client socket handle
- Connection timestamp

### Auto-Selection Behavior
```
if (no emulator selected && emulator exists):
    auto-select most recent emulator
```

This allows immediate use without explicit `select` command when only one emulator is running.

## Command Reference

See [command-interface.md](./command-interface.md) for complete command reference. All commands listed there are available via CLI.

### CLI-Specific Behavior

**Interactive Help**:
```
> help
Available commands:
  help [command]    - Show this help or help for specific command
  status           - Show emulator status
  list             - List all emulators
  ...
```

**Command Aliases**:
The CLI supports command aliases for convenience:
- `?` → `help`
- `r` → `registers`
- `m` → `memory`
- `bp` → `breakpoint`
- `quit` → `exit`

**Error Messages**:
```
> invalid_command
Unknown command: invalid_command
Type 'help' for available commands.
```

## Connection Examples

### Using Telnet
```bash
telnet localhost <port>
> help
> list
> select 0
> registers
> bp 0x8000
> resume
```

### Using Netcat
```bash
nc localhost <port>
> status
> memory 0x8000 256
```

### Scripted Automation
```bash
#!/bin/bash
{
    echo "select 0"
    echo "bp 0x8000"
    echo "resume"
    sleep 5
    echo "pause"
    echo "registers"
    echo "exit"
} | nc localhost <port>
```

## Advanced Usage

### Batch Commands (Future)
```bash
# Load commands from file
cat commands.txt | nc localhost <port>
```

### Programmatic Integration
```python
import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('localhost', port))

def send_command(cmd):
    sock.sendall(f"{cmd}\n".encode())
    return sock.recv(4096).decode()

print(send_command("list"))
print(send_command("select 0"))
print(send_command("registers"))

sock.close()
```

## Performance Characteristics

- **Latency**: ~1-5ms per command (local connection)
- **Throughput**: ~1000 commands/second
- **Concurrent Connections**: Unlimited (one thread per connection)
- **Overhead**: Minimal (~0.1% CPU per connection)

## Security Considerations

⚠️ **Warning**: Current implementation has NO authentication or encryption.

**Current State**:
- Binds to all interfaces (0.0.0.0)
- No password required
- Plain text protocol
- No rate limiting

**Recommended Deployment**:
- Bind to localhost only for local-only access
- Use SSH tunneling for remote access:
  ```bash
  ssh -L local_port:localhost:remote_port user@remote_host
  ```
- Firewall rules to restrict access
- Consider VPN for trusted remote access

**Future Security Enhancements** (planned):
- Authentication (username/password)
- TLS/SSL encryption
- Token-based auth
- Rate limiting and flood protection
- Audit logging

## Error Handling

### Connection Errors
- **Port in use**: Server fails to start, error logged
- **Connection refused**: Check server is running and port is correct
- **Connection reset**: Server crashed or was stopped
- **Timeout**: No response from server (check network)

### Command Errors
- **Unknown command**: Returns suggestion to use `help`
- **Invalid arguments**: Returns usage information
- **No emulator selected**: Returns error message
- **Emulator not found**: Check `list` output

### Recovery Strategies
1. **Connection lost**: Reconnect and re-select emulator
2. **Command failed**: Check syntax with `help <command>`
3. **Hung connection**: Close and reconnect
4. **Server unresponsive**: Restart emulator application

## Configuration

**Configuration File**: `config.ini` (location TBD)

```ini
[CLI]
enabled = true
port = 9999
bind_address = 127.0.0.1
max_connections = 10
command_timeout = 30000
```

**Environment Variables**:
```bash
UNREAL_CLI_PORT=9999
UNREAL_CLI_BIND=127.0.0.1
```

## Implementation Details

### Source Files
- `core/automation/cli/src/automation-cli.cpp` - Server main loop
- `core/automation/cli/src/cli-processor.cpp` - Command processing
- `core/automation/cli/include/cli-processor.h` - Command handlers

### Key Classes
- `AutomationCLI` - TCP server, connection management
- `ClientSession` - Per-connection state
- `CLIProcessor` - Command parser and dispatcher

### Threading Model
- Main thread: TCP accept loop
- Worker threads: One per client connection
- Synchronization: Mutex on emulator access

### Dependencies
- Platform sockets (Winsock on Windows, BSD sockets on Unix)
- C++ standard library (std::thread, std::string, std::vector)
- CLI11 library (command-line argument parsing)

## Troubleshooting

### "Connection refused"
**Cause**: Server not running or wrong port  
**Solution**: 
1. Check emulator is running with CLI enabled
2. Verify port with `netstat -an | grep <port>`
3. Check firewall rules

### "No emulator selected"
**Cause**: No emulator instance available  
**Solution**:
1. Create emulator instance in main application
2. Use `list` to see available emulators
3. Use `select <id>` to choose one

### Commands hang
**Cause**: Emulator deadlock or processing long operation  
**Solution**:
1. Wait for operation to complete
2. Disconnect and reconnect
3. Restart emulator if hung

### Unexpected responses
**Cause**: Command syntax error or unsupported feature  
**Solution**:
1. Check command syntax with `help <command>`
2. Verify command is implemented (see status in docs)
3. Check emulator log for errors

## Best Practices

1. **Always select emulator first** (unless auto-selected)
2. **Use `status` to verify state** before control commands
3. **Pause before inspecting state** for consistent results
4. **Close connections properly** with `exit`
5. **Script repetitive tasks** using shell scripts or Python
6. **Test commands interactively** before scripting
7. **Monitor connection status** in production automation

## See Also

### Interface Documentation
- **[Command Interface Overview](./command-interface.md)** - Core command reference and architecture
- **[WebAPI Interface](./webapi-interface.md)** - HTTP/REST API for web integration
- **[Python Bindings](./python-interface.md)** - Direct C++ bindings for automation and AI/ML
- **[Lua Bindings](./lua-interface.md)** - Lightweight scripting and embedded logic

### Advanced Interfaces (Future)
- **[GDB Protocol](./gdb-protocol.md)** - Professional debugging with standard GDB/LLDB clients
- **[Universal Debug Bridge](./udb-protocol.md)** - High-performance analysis and profiling

### Navigation
- **[Interface Documentation Index](./README.md)** - Overview of all control interfaces
