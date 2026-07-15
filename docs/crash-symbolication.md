# Crash Report Symbolication Guide

This guide explains how to convert raw crash report addresses into human-readable function names and line numbers for debugging.

## Overview

Release builds are stripped of debug symbols to reduce binary size. When a crash occurs, the crash handler captures:
- Exception/signal type and code
- Fault address
- Stack trace (raw addresses)
- CPU register state

To analyze the crash, you need to **symbolicate** these addresses using the debug symbols from the matching build.

## Obtaining Debug Symbols

Debug symbols are packaged separately for **rolling snapshot builds** (not milestone releases).

Download the symbol archive matching your build from the [GitHub Releases](https://github.com/alfishe/unreal-ng/releases/tag/continuous):

| Platform | Symbol Archive |
|----------|---------------|
| Linux | `UnrealNG-Symbols-Linux.tar.gz` |
| Windows MSVC | `UnrealNG-Symbols-Windows-MSVC.zip` |
| Windows MinGW | `UnrealNG-Symbols-Windows-MinGW.zip` |
| macOS ARM64 | `UnrealNG-Symbols-macOS-ARM64.tar.gz` |

**Important:** Symbols must match the exact build. The version string in the crash report (e.g., `0.1.0-123-gabcdef0`) identifies the build.

## Symbol File Formats

| Platform | Format | Files |
|----------|--------|-------|
| Windows MSVC | PDB (Program Database) | `*.pdb` |
| Windows MinGW | DWARF (in separate file) | `*.debug` |
| Linux | DWARF (in separate file) | `*.debug` |
| macOS | dSYM bundle | `*.dSYM/` |

## Using the Symbolication Script

The easiest way to symbolicate crash reports is using the provided Python script:

```bash
# Basic usage - auto-detects platform from crash report
python tools/symbolicate.py crash_report.txt --symbols path/to/symbols/

# Specify executable explicitly
python tools/symbolicate.py crash_report.txt --exe unreal-qt --symbols ./symbols/

# Process addresses directly
python tools/symbolicate.py --addresses 0x100012abc 0x100013def --exe unreal-qt --symbols ./symbols/
```

The script automatically:
- Detects the platform and executable from the crash report
- Finds the appropriate symbolication tool
- Processes all addresses in the stack trace
- Outputs a symbolicated report

## Manual Symbolication

### Windows MSVC (PDB files)

Using Visual Studio Developer Command Prompt:
```cmd
dumpbin /symbols unreal-qt.pdb | findstr <address>
```

Using WinDbg:
```cmd
cdb -z unreal-qt.exe -y "C:\path\to\symbols" -c ".reload; ln 0x7ff612345678; q"
```

### Windows MinGW / Linux (DWARF)

Using `addr2line` (from binutils):
```bash
# Single address
addr2line -e unreal-qt.debug -Cfpi 0x12345678

# Multiple addresses
addr2line -e unreal-qt.debug -Cfpi 0x12345678 0x12345abc 0x12345def

# Process from file
cat crash.log | grep -oP '0x[0-9a-f]+' | xargs addr2line -e unreal-qt.debug -Cfpi
```

Options:
- `-C`: Demangle C++ symbols
- `-f`: Show function names
- `-p`: Pretty print (one line per address)
- `-i`: Unwind inlined functions

Using `llvm-symbolizer`:
```bash
llvm-symbolizer --obj=unreal-qt.debug 0x12345678
```

### macOS (dSYM bundles)

Using `atos`:
```bash
# You need the load address from the crash report
atos -o unreal-qt.dSYM -arch arm64 -l 0x100000000 0x100012abc

# Multiple addresses
atos -o unreal-qt.dSYM -arch arm64 -l 0x100000000 0x100012abc 0x100013def 0x100014567
```

Using `llvm-symbolizer`:
```bash
llvm-symbolizer --obj=unreal-qt.dSYM/Contents/Resources/DWARF/unreal-qt 0x12345678
```

**Finding the load address:** Look for the base address in the crash report, typically shown as the first stack frame address with offset 0, or in module load information.

## Understanding Crash Reports

### Example Crash Report (Windows)

```
*** UNHANDLED EXCEPTION ***
  Exception : 0xC0000005  (Access Violation)
  Address   : 0x00007FF6B8A12345
  PID / TID : 12345 / 6789

Stack Trace:
  [ 0] 0x00007FF6B8A12345  (no symbol)
  [ 1] 0x00007FF6B8A11234  (no symbol)
  [ 2] 0x00007FF6B8A10ABC  (no symbol)

Registers:
  RAX=0000000000000000  RBX=00007FF6B8B00000  ...
```

### Example Symbolicated Output

```
Stack Trace:
  [ 0] 0x00007FF6B8A12345  EmulatorCore::execute()   at core/src/emulator.cpp:234
  [ 1] 0x00007FF6B8A11234  Z80::step()               at core/src/z80/z80.cpp:567
  [ 2] 0x00007FF6B8A10ABC  MainWindow::runFrame()    at unreal-qt/src/mainwindow.cpp:89
```

## Troubleshooting

### "No symbol found" for all addresses

1. **Wrong symbols**: Ensure symbols match the exact build version
2. **Address format**: Addresses should be in hexadecimal (0x prefix)
3. **Relative vs absolute**: Some tools need relative addresses (subtract load address)

### "File not found" errors

1. Check the symbol file path is correct
2. For dSYM, point to the `.dSYM` bundle, not the internal DWARF file
3. For MinGW/Linux, use the `.debug` file, not the stripped executable

### macOS "atos" shows wrong symbols

Ensure you're using the correct:
- Architecture (`-arch arm64` or `-arch x86_64`)
- Load address (`-l` flag)
- dSYM bundle path

## Collecting Crash Reports from Users

When requesting crash reports from users, ask for:

1. **The crash log** - Usually written to `/tmp/unreal_crash_*.log` (Linux/macOS) or `%TEMP%\unreal_crash_*.dmp` (Windows)
2. **Application version** - From Help → About, or the window title
3. **Steps to reproduce** - What they were doing when it crashed
4. **System info** - OS version, CPU architecture
