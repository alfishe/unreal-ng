# CLI Automation Module

This module provides a command-line interface (CLI) for controlling the emulator via a TCP socket. It's built using the CLI11 library for command parsing and provides a simple, scriptable interface for automation.

## Features

- **Interactive Command Shell**: Connect via telnet or netcat to interact with the emulator
- **Scriptable**: Can be used in scripts for automated testing and control
- **Thread-Safe**: Safe to use while the emulator is running
- **Extensible**: Easy to add new commands

## Building

The CLI module is enabled by default when building with `ENABLE_AUTOMATION=ON`. To disable it, set `ENABLE_CLI_AUTOMATION=OFF` during CMake configuration.

## Usage

1. Start the emulator with CLI automation enabled
2. Connect to the CLI server using a TCP client (default port: 8765):
   ```
   telnet localhost 8765
   ```
   or
   ```
   nc localhost 8765
   ```

## Available Commands

- `help [command]` - Show help for commands
- `status` - Show emulator status
- `reset` - Reset the emulator
- `pause` - Pause emulation
- `resume` - Resume emulation
- `step [count]` - Execute a single instruction (or N instructions)
- `break [address]` - Set/clear breakpoint at address
- `memory <address> [count]` - Dump memory at address
- `registers` - Show CPU registers
- `exit` or `quit` - Disconnect from the CLI

## Example Session

```
$ telnet localhost 8765
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.
Unreal Emulator CLI - Type 'help' for available commands
> status
Emulator status: Running
> break 0x1000
Breakpoint set at 0x1000
> step
Executed 1 instruction
> memory 0x1000 16
Memory dump at 0x1000 (16 bytes):
00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
> quit
Goodbye!
Connection closed by foreign host.
```

## Extending

To add a new command:

1. Add a new handler method to the `AutomationCLI` class
2. Register the command in the `registerCommands()` method
3. Update the help text in the `handleHelp()` method if needed

## Dependencies

- CLI11 (included in the `lib/cli11` directory)
- C++17 or later
- POSIX sockets (for the TCP server)
