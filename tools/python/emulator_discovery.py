#!/usr/bin/env python3
"""
Emulator Discovery and Shared Memory Connection Module

Provides WebAPI-based discovery of running emulator instances and connection
to their shared memory regions using instance-specific identifiers.
"""

import argparse
import mmap
import os
import platform
import sys
from dataclasses import dataclass
from typing import List, Optional, Tuple

# Platform detection
IS_WINDOWS = platform.system() == 'Windows'
IS_MACOS = platform.system() == 'Darwin'
IS_LINUX = platform.system() == 'Linux'

# Platform-specific imports
if IS_WINDOWS:
    import ctypes
    from ctypes.wintypes import HANDLE, DWORD, LPCWSTR, BOOL
else:
    try:
        import posix_ipc
    except ImportError:
        posix_ipc = None

# Memory layout constants (must match emulator)
PAGE_SIZE = 0x4000  # 16KB per page
MAX_RAM_PAGES = 256
MAX_CACHE_PAGES = 2
MAX_MISC_PAGES = 1
MAX_ROM_PAGES = 64
TOTAL_MEMORY_SIZE = (MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES + MAX_ROM_PAGES) * PAGE_SIZE


@dataclass
class EmulatorInfo:
    """Information about a running emulator instance."""
    id: str              # Full UUID
    symbolic_id: str     # User-friendly name (may be empty)
    state: str           # Running, Paused, etc.
    index: int           # 1-based index for CLI selection
    
    @property
    def display_name(self) -> str:
        """Get a display-friendly name for this emulator."""
        if self.symbolic_id:
            return f"{self.symbolic_id} ({self.id[-12:]})"
        return self.id[-12:]
    
    @property
    def shm_suffix(self) -> str:
        """Get the shared memory name suffix (last 12 chars of UUID)."""
        return self.id[-12:] if len(self.id) >= 12 else self.id


def get_webapi_url(host: str = "localhost", port: int = 8090) -> str:
    """Build the WebAPI base URL."""
    return f"http://{host}:{port}"


def discover_emulators(host: str = "localhost", port: int = 8090) -> List[EmulatorInfo]:
    """
    Discover running emulator instances via WebAPI.
    
    Args:
        host: WebAPI host
        port: WebAPI port
        
    Returns:
        List of EmulatorInfo objects
    """
    try:
        import requests
    except ImportError:
        print("Error: 'requests' module is required for WebAPI discovery.")
        print("Install it with: pip install requests")
        return []
    
    url = f"{get_webapi_url(host, port)}/api/v1/emulator"
    
    try:
        response = requests.get(url, timeout=5)
        response.raise_for_status()
        data = response.json()
        
        emulators = []
        for idx, item in enumerate(data.get('emulators', data if isinstance(data, list) else []), start=1):
            # Handle different response formats
            if isinstance(item, dict):
                emu_id = item.get('id', item.get('uuid', ''))
                symbolic = item.get('symbolicId', item.get('symbolic_id', ''))
                state = item.get('state', item.get('status', 'unknown'))
            else:
                emu_id = str(item)
                symbolic = ''
                state = 'unknown'
            
            emulators.append(EmulatorInfo(
                id=emu_id,
                symbolic_id=symbolic,
                state=state,
                index=idx
            ))
        
        return emulators
        
    except requests.exceptions.ConnectionError:
        print(f"Error: Cannot connect to WebAPI at {url}")
        print("Make sure the emulator is running with WebAPI enabled.")
        return []
    except requests.exceptions.Timeout:
        print(f"Error: WebAPI request timed out")
        return []
    except Exception as e:
        print(f"Error discovering emulators: {e}")
        return []


def select_emulator(
    emulators: List[EmulatorInfo],
    emulator_id: Optional[str] = None,
    wait: bool = False
) -> Optional[EmulatorInfo]:
    """
    Select an emulator from the list.
    
    Args:
        emulators: List of discovered emulators
        emulator_id: Specific ID/index to select (None for auto-select)
        wait: If True and no emulators, wait for one
        
    Returns:
        Selected EmulatorInfo or None
    """
    if not emulators:
        if wait:
            print("No emulators found. Waiting for an emulator to start...")
            return None  # Caller should implement wait loop
        print("No emulators found. Please start an emulator first.")
        return None
    
    # Single emulator - auto-select
    if len(emulators) == 1 and emulator_id is None:
        print(f"Auto-selecting single emulator: {emulators[0].display_name}")
        return emulators[0]
    
    # Multiple emulators - need explicit selection
    if emulator_id is None:
        print("Multiple emulators running. Please specify one:")
        print()
        for emu in emulators:
            state_marker = "*" if emu.state.lower() in ('running', 'run') else " "
            print(f"  [{emu.index}]{state_marker} {emu.display_name} ({emu.state})")
        print()
        print("Use --emulator <index|id> to select one")
        return None
    
    # Find by index or ID
    for emu in emulators:
        # Try index match
        try:
            if int(emulator_id) == emu.index:
                return emu
        except ValueError:
            pass
        
        # Try ID match (partial or full)
        if emulator_id == emu.id or emu.id.endswith(emulator_id):
            return emu
        
        # Try symbolic ID match
        if emu.symbolic_id and emulator_id.lower() in emu.symbolic_id.lower():
            return emu
    
    print(f"Error: Emulator '{emulator_id}' not found")
    return None


def get_shm_name(emulator: EmulatorInfo) -> str:
    """
    Get the shared memory name for an emulator instance.
    
    Args:
        emulator: EmulatorInfo object
        
    Returns:
        Platform-appropriate shared memory name
    """
    suffix = emulator.shm_suffix
    if IS_WINDOWS:
        return f"Local\\zxspectrum_memory-{suffix}"
    else:
        return f"/zxspectrum_memory-{suffix}"


def connect_to_shared_memory(emulator: EmulatorInfo, read_only: bool = True) -> Optional[mmap.mmap]:
    """
    Connect to an emulator's shared memory.
    
    Args:
        emulator: EmulatorInfo object
        read_only: Whether to open in read-only mode
        
    Returns:
        mmap object if successful, None otherwise
    """
    shm_name = get_shm_name(emulator)
    print(f"Connecting to shared memory: {shm_name}")
    
    if IS_WINDOWS:
        return _connect_shm_windows(shm_name, read_only)
    else:
        return _connect_shm_posix(shm_name, read_only)


def _connect_shm_windows(shm_name: str, read_only: bool) -> Optional[mmap.mmap]:
    """Connect to shared memory on Windows."""
    try:
        kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel32.OpenFileMappingW.argtypes = [DWORD, BOOL, LPCWSTR]
        kernel32.OpenFileMappingW.restype = HANDLE
        kernel32.CloseHandle.argtypes = [HANDLE]
        kernel32.CloseHandle.restype = BOOL
        
        FILE_MAP_READ = 0x0004
        handle = kernel32.OpenFileMappingW(FILE_MAP_READ, False, shm_name)
        
        if not handle:
            error_code = ctypes.get_last_error()
            if error_code == 2:
                print("Shared memory not found. Is 'sharedmemory' feature enabled?")
                print("Enable it with: feature sharedmemory on")
            elif error_code == 5:
                print("Access denied. Try running as Administrator.")
            else:
                print(f"Windows error: {error_code}")
            return None
        
        kernel32.CloseHandle(handle)
        
        access = mmap.ACCESS_READ if read_only else mmap.ACCESS_WRITE
        shm = mmap.mmap(-1, TOTAL_MEMORY_SIZE, tagname=shm_name, access=access)
        shm.seek(0)
        
        print(f"Connected to shared memory ({TOTAL_MEMORY_SIZE:,} bytes)")
        return shm
        
    except Exception as e:
        print(f"Error connecting to Windows shared memory: {e}")
        return None


def _connect_shm_posix(shm_name: str, read_only: bool) -> Optional[mmap.mmap]:
    """Connect to shared memory on POSIX systems (Linux/macOS)."""
    if posix_ipc is None:
        print("Error: 'posix_ipc' module is required for shared memory access.")
        print("Install it with: pip install posix_ipc")
        return None
    
    try:
        flags = 0 if read_only else posix_ipc.O_RDWR
        shm = posix_ipc.SharedMemory(shm_name, flags)
        
        print(f"Shared memory size: {shm.size:,} bytes")
        
        access = mmap.ACCESS_READ if read_only else mmap.ACCESS_WRITE
        mapped = mmap.mmap(
            fileno=shm.fd,
            length=shm.size,
            access=access,
            flags=mmap.MAP_SHARED,
            offset=0
        )
        
        shm.close_fd()
        return mapped
        
    except posix_ipc.ExistentialError:
        print(f"Shared memory not found: {shm_name}")
        print("Make sure 'sharedmemory' feature is enabled:")
        print("  feature sharedmemory on")
        return None
    except Exception as e:
        print(f"Error connecting to POSIX shared memory: {e}")
        import traceback
        traceback.print_exc()
        return None


def add_emulator_args(parser: argparse.ArgumentParser) -> None:
    """
    Add common emulator discovery arguments to an argument parser.
    
    Args:
        parser: ArgumentParser to add arguments to
    """
    parser.add_argument(
        '-e', '--emulator',
        metavar='ID',
        help='Emulator ID, index, or symbolic name (required if multiple emulators)'
    )
    parser.add_argument(
        '--host',
        default='localhost',
        help='WebAPI host (default: localhost)'
    )
    parser.add_argument(
        '--port',
        type=int,
        default=8090,
        help='WebAPI port (default: 8090)'
    )
    parser.add_argument(
        '--wait',
        action='store_true',
        help='Wait for an emulator to start if none found'
    )


def discover_and_select(args: argparse.Namespace) -> Optional[EmulatorInfo]:
    """
    Convenience function to discover emulators and select one based on args.
    
    Args:
        args: Parsed arguments with host, port, emulator, wait fields
        
    Returns:
        Selected EmulatorInfo or None
    """
    emulators = discover_emulators(args.host, args.port)
    return select_emulator(emulators, args.emulator, args.wait)


def cleanup_shared_memory(shm: Optional[mmap.mmap], verbose: bool = False) -> None:
    """
    Safely close a shared memory handle.
    
    Args:
        shm: Shared memory object to close
        verbose: Whether to print status messages
    """
    if not shm:
        return
    
    try:
        if verbose:
            print("Cleaning up shared memory resources...")
        
        if hasattr(shm, 'close') and callable(getattr(shm, 'close')):
            if not getattr(shm, 'closed', True):
                shm.close()
                
    except Exception as e:
        print(f"Warning: Error during shared memory cleanup: {e}")


if __name__ == "__main__":
    # Test discovery
    print("Testing emulator discovery...")
    emulators = discover_emulators()
    
    if not emulators:
        print("No emulators found.")
        sys.exit(1)
    
    print(f"\nFound {len(emulators)} emulator(s):")
    for emu in emulators:
        shm_name = get_shm_name(emu)
        print(f"  [{emu.index}] {emu.display_name}")
        print(f"      State: {emu.state}")
        print(f"      SHM:   {shm_name}")
