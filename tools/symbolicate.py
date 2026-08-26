#!/usr/bin/env python3
"""
Crash Report Symbolication Tool

Cross-platform tool to convert raw crash report addresses into human-readable
function names and line numbers using debug symbols.

Supports:
- Windows MSVC (PDB files)
- Windows MinGW (DWARF .debug files)
- Linux (DWARF .debug files)
- macOS (dSYM bundles)

Usage:
    python symbolicate.py crash_report.txt --symbols path/to/symbols/
    python symbolicate.py --addresses 0x1234 0x5678 --exe unreal-qt --symbols ./symbols/
"""

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


@dataclass
class CrashInfo:
    """Parsed crash report information."""
    platform: str  # 'windows-msvc', 'windows-mingw', 'linux', 'macos'
    executable: str
    addresses: list[str]
    load_address: Optional[str] = None
    raw_content: str = ""


def detect_platform() -> str:
    """Detect current platform for tool selection."""
    system = platform.system().lower()
    if system == "darwin":
        return "macos"
    elif system == "linux":
        return "linux"
    elif system == "windows":
        return "windows"
    return "unknown"


def find_tool(tool_name: str) -> Optional[str]:
    """Find a tool in PATH or common locations."""
    path = shutil.which(tool_name)
    if path:
        return path

    # Common locations
    common_paths = []
    if platform.system() == "Windows":
        common_paths = [
            r"C:\msys64\mingw64\bin",
            r"C:\msys64\usr\bin",
            r"C:\Program Files\LLVM\bin",
        ]
    elif platform.system() == "Darwin":
        common_paths = [
            "/usr/bin",
            "/usr/local/bin",
            "/opt/homebrew/bin",
        ]
    else:
        common_paths = [
            "/usr/bin",
            "/usr/local/bin",
        ]

    for base in common_paths:
        full_path = os.path.join(base, tool_name)
        if os.path.isfile(full_path):
            return full_path
        # Try with .exe on Windows
        if platform.system() == "Windows":
            full_path_exe = full_path + ".exe"
            if os.path.isfile(full_path_exe):
                return full_path_exe

    return None


def parse_crash_report(content: str) -> CrashInfo:
    """Parse a crash report and extract addresses and metadata."""
    lines = content.strip().split('\n')
    addresses = []
    executable = "unreal-qt"  # default
    load_address = None
    crash_platform = None

    # Detect platform from crash report content
    if "EXCEPTION_" in content or ".pdb" in content.lower():
        crash_platform = "windows"
    elif "SIGSEGV" in content or "SIGBUS" in content:
        if "dSYM" in content or ".app" in content:
            crash_platform = "macos"
        else:
            crash_platform = "linux"

    # Extract addresses from stack trace
    # Patterns: [N] 0xADDRESS, 0xADDRESS symbol, etc.
    addr_pattern = re.compile(r'0x([0-9a-fA-F]{8,16})')

    for line in lines:
        # Skip register dumps (they have = signs)
        if '=' in line and any(reg in line for reg in ['RAX', 'RBX', 'EAX', 'X0', 'PC=']):
            continue

        matches = addr_pattern.findall(line)
        for match in matches:
            addr = f"0x{match}"
            if addr not in addresses:
                addresses.append(addr)

    # Try to detect executable name
    exe_pattern = re.compile(r'(unreal-qt|unreal-screen-viewer|unreal-videowall)')
    exe_match = exe_pattern.search(content)
    if exe_match:
        executable = exe_match.group(1)

    # Try to detect load address (first frame or explicit)
    load_pattern = re.compile(r'load[_ ]?addr(?:ess)?[:\s]+0x([0-9a-fA-F]+)', re.IGNORECASE)
    load_match = load_pattern.search(content)
    if load_match:
        load_address = f"0x{load_match.group(1)}"

    return CrashInfo(
        platform=crash_platform or detect_platform(),
        executable=executable,
        addresses=addresses,
        load_address=load_address,
        raw_content=content
    )


def symbolicate_addr2line(addresses: list[str], symbol_file: Path) -> dict[str, str]:
    """Symbolicate using addr2line (Linux/MinGW)."""
    tool = find_tool("addr2line") or find_tool("llvm-addr2line")
    if not tool:
        print("Error: addr2line not found. Install binutils or llvm.", file=sys.stderr)
        return {}

    results = {}
    try:
        cmd = [tool, "-e", str(symbol_file), "-Cfpi"] + addresses
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)

        lines = result.stdout.strip().split('\n')
        for addr, line in zip(addresses, lines):
            results[addr] = line.strip() if line.strip() else "(no symbol)"
    except subprocess.TimeoutExpired:
        print("Error: addr2line timed out", file=sys.stderr)
    except Exception as e:
        print(f"Error running addr2line: {e}", file=sys.stderr)

    return results


def symbolicate_llvm(addresses: list[str], symbol_file: Path) -> dict[str, str]:
    """Symbolicate using llvm-symbolizer."""
    tool = find_tool("llvm-symbolizer")
    if not tool:
        print("Error: llvm-symbolizer not found. Install LLVM.", file=sys.stderr)
        return {}

    results = {}
    try:
        for addr in addresses:
            cmd = [tool, f"--obj={symbol_file}", addr]
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
            output = result.stdout.strip()
            # llvm-symbolizer outputs function\nfile:line, combine them
            lines = output.split('\n')
            if len(lines) >= 2:
                results[addr] = f"{lines[0]}   at {lines[1]}"
            elif lines:
                results[addr] = lines[0]
            else:
                results[addr] = "(no symbol)"
    except Exception as e:
        print(f"Error running llvm-symbolizer: {e}", file=sys.stderr)

    return results


def symbolicate_atos(addresses: list[str], dsym_path: Path,
                     load_address: Optional[str] = None,
                     arch: str = "arm64") -> dict[str, str]:
    """Symbolicate using atos (macOS)."""
    tool = find_tool("atos")
    if not tool:
        print("Error: atos not found. This tool is macOS-only.", file=sys.stderr)
        return {}

    # Find the DWARF file inside dSYM bundle
    dwarf_path = dsym_path
    if dsym_path.suffix == ".dSYM":
        dwarf_dir = dsym_path / "Contents" / "Resources" / "DWARF"
        if dwarf_dir.exists():
            dwarf_files = list(dwarf_dir.iterdir())
            if dwarf_files:
                dwarf_path = dwarf_files[0]

    results = {}
    try:
        cmd = [tool, "-o", str(dwarf_path), "-arch", arch]
        if load_address:
            cmd.extend(["-l", load_address])
        cmd.extend(addresses)

        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)

        lines = result.stdout.strip().split('\n')
        for addr, line in zip(addresses, lines):
            results[addr] = line.strip() if line.strip() else "(no symbol)"
    except Exception as e:
        print(f"Error running atos: {e}", file=sys.stderr)

    return results


def symbolicate_pdb(addresses: list[str], pdb_path: Path, exe_path: Optional[Path] = None) -> dict[str, str]:
    """Symbolicate using Windows PDB tools."""
    # Try llvm-symbolizer first (cross-platform)
    tool = find_tool("llvm-symbolizer")
    if tool and exe_path:
        return symbolicate_llvm(addresses, exe_path)

    # Try using dbghelp via a simple lookup
    # For full PDB support, recommend using Visual Studio or WinDbg
    print("Note: Full PDB symbolication requires Visual Studio or WinDbg.", file=sys.stderr)
    print(f"PDB file: {pdb_path}", file=sys.stderr)
    print("Use: cdb -z <exe> -y <pdb_dir> -c \".reload; ln <addr>; q\"", file=sys.stderr)

    return {addr: "(use Visual Studio/WinDbg for PDB)" for addr in addresses}


def find_symbol_file(symbols_dir: Path, executable: str, platform: str) -> Optional[Path]:
    """Find the appropriate symbol file for the given executable and platform."""
    symbols_dir = Path(symbols_dir)

    if not symbols_dir.exists():
        return None

    # Look in subdirectories too (symbols/ from archive)
    search_dirs = [symbols_dir]
    if (symbols_dir / "symbols").exists():
        search_dirs.append(symbols_dir / "symbols")

    for search_dir in search_dirs:
        if platform == "macos":
            # Look for .dSYM bundle
            dsym = search_dir / f"{executable}.dSYM"
            if dsym.exists():
                return dsym
        elif platform in ("linux", "windows-mingw", "mingw"):
            # Look for .debug file
            debug = search_dir / f"{executable}.debug"
            if debug.exists():
                return debug
        elif platform in ("windows", "windows-msvc", "msvc"):
            # Look for .pdb file
            pdb = search_dir / f"{executable}.pdb"
            if pdb.exists():
                return pdb

    # Try to find any matching file
    for search_dir in search_dirs:
        for pattern in [f"{executable}.*", f"{executable}.exe.*"]:
            matches = list(search_dir.glob(pattern))
            if matches:
                return matches[0]

    return None


def symbolicate(crash_info: CrashInfo, symbols_dir: Path,
                arch: str = "arm64") -> dict[str, str]:
    """Main symbolication dispatcher."""
    symbol_file = find_symbol_file(symbols_dir, crash_info.executable, crash_info.platform)

    if not symbol_file:
        print(f"Error: Could not find symbol file for {crash_info.executable} "
              f"(platform: {crash_info.platform}) in {symbols_dir}", file=sys.stderr)
        return {}

    print(f"Using symbol file: {symbol_file}", file=sys.stderr)

    if crash_info.platform == "macos":
        return symbolicate_atos(
            crash_info.addresses,
            symbol_file,
            crash_info.load_address,
            arch
        )
    elif crash_info.platform in ("linux", "windows-mingw", "mingw"):
        return symbolicate_addr2line(crash_info.addresses, symbol_file)
    elif crash_info.platform in ("windows", "windows-msvc", "msvc"):
        exe_path = symbols_dir / f"{crash_info.executable}.exe"
        return symbolicate_pdb(crash_info.addresses, symbol_file, exe_path)
    else:
        # Try addr2line as fallback
        return symbolicate_addr2line(crash_info.addresses, symbol_file)


def format_output(crash_info: CrashInfo, symbols: dict[str, str]) -> str:
    """Format the symbolicated output."""
    output_lines = ["=" * 60, "SYMBOLICATED CRASH REPORT", "=" * 60, ""]

    # Re-process the original content, replacing addresses with symbols
    for line in crash_info.raw_content.split('\n'):
        new_line = line
        for addr, symbol in symbols.items():
            if addr in line and symbol != "(no symbol)":
                # Replace "(no symbol)" or just the address with the symbol
                new_line = re.sub(
                    rf'{re.escape(addr)}\s*(?:\(no symbol\))?',
                    f'{addr}  {symbol}',
                    new_line
                )
        output_lines.append(new_line)

    output_lines.extend(["", "=" * 60])
    return '\n'.join(output_lines)


def main():
    parser = argparse.ArgumentParser(
        description="Symbolicate crash reports using debug symbols",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s crash.log --symbols ./symbols/
  %(prog)s crash.log --exe unreal-qt --symbols ./symbols/ --platform linux
  %(prog)s --addresses 0x1234 0x5678 --exe unreal-qt --symbols ./symbols/
        """
    )

    parser.add_argument(
        "crash_report",
        nargs="?",
        help="Path to crash report file"
    )
    parser.add_argument(
        "--addresses", "-a",
        nargs="+",
        help="Addresses to symbolicate (instead of crash report file)"
    )
    parser.add_argument(
        "--symbols", "-s",
        required=True,
        help="Path to directory containing symbol files"
    )
    parser.add_argument(
        "--exe", "-e",
        default="unreal-qt",
        help="Executable name (default: unreal-qt)"
    )
    parser.add_argument(
        "--platform", "-p",
        choices=["linux", "macos", "windows-msvc", "windows-mingw", "auto"],
        default="auto",
        help="Target platform (default: auto-detect)"
    )
    parser.add_argument(
        "--arch",
        choices=["arm64", "x86_64", "x64"],
        default="arm64",
        help="Architecture for macOS (default: arm64)"
    )
    parser.add_argument(
        "--load-address", "-l",
        help="Load address for macOS symbolication"
    )
    parser.add_argument(
        "--output", "-o",
        help="Output file (default: stdout)"
    )

    args = parser.parse_args()

    # Validate arguments
    if not args.crash_report and not args.addresses:
        parser.error("Either crash_report file or --addresses must be provided")

    symbols_dir = Path(args.symbols)
    if not symbols_dir.exists():
        print(f"Error: Symbols directory not found: {symbols_dir}", file=sys.stderr)
        sys.exit(1)

    # Build crash info
    if args.crash_report:
        crash_file = Path(args.crash_report)
        if not crash_file.exists():
            print(f"Error: Crash report not found: {crash_file}", file=sys.stderr)
            sys.exit(1)

        content = crash_file.read_text()
        crash_info = parse_crash_report(content)

        # Override with command line args
        if args.exe:
            crash_info.executable = args.exe
        if args.platform != "auto":
            crash_info.platform = args.platform
        if args.load_address:
            crash_info.load_address = args.load_address
    else:
        # Direct address mode
        crash_info = CrashInfo(
            platform=args.platform if args.platform != "auto" else detect_platform(),
            executable=args.exe,
            addresses=args.addresses,
            load_address=args.load_address,
            raw_content="\n".join(args.addresses)
        )

    if not crash_info.addresses:
        print("Error: No addresses found to symbolicate", file=sys.stderr)
        sys.exit(1)

    print(f"Platform: {crash_info.platform}", file=sys.stderr)
    print(f"Executable: {crash_info.executable}", file=sys.stderr)
    print(f"Addresses: {len(crash_info.addresses)} found", file=sys.stderr)

    # Symbolicate
    symbols = symbolicate(crash_info, symbols_dir, args.arch)

    if not symbols:
        print("Error: Symbolication failed", file=sys.stderr)
        sys.exit(1)

    # Format output
    if args.crash_report:
        output = format_output(crash_info, symbols)
    else:
        # Simple address list mode
        output_lines = []
        for addr in crash_info.addresses:
            symbol = symbols.get(addr, "(not found)")
            output_lines.append(f"{addr}  {symbol}")
        output = '\n'.join(output_lines)

    # Write output
    if args.output:
        Path(args.output).write_text(output)
        print(f"Output written to: {args.output}", file=sys.stderr)
    else:
        print(output)


if __name__ == "__main__":
    main()
