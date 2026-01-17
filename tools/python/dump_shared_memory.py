#!/usr/bin/env python3
"""
Dump shared memory pages from ZX-Spectrum Emulator

This script connects to the emulator's shared memory and dumps all memory pages
into separate files, organized by memory region type (ROM, RAM, CACHE, MISC).

Usage:
    # Auto-select single emulator
    python dump_shared_memory.py
    
    # Specify emulator by index or ID
    python dump_shared_memory.py --emulator 1
    python dump_shared_memory.py --emulator abc123def456
"""

import os
import sys
import argparse
import mmap
import hashlib
import platform
import traceback
from datetime import datetime
from pathlib import Path
from typing import Optional, Dict, List, Tuple, BinaryIO

# Import platform-specific modules
IS_WINDOWS = platform.system() == 'Windows'
IS_MACOS = platform.system() == 'Darwin'
IS_LINUX = platform.system() == 'Linux'

# Import the emulator discovery module
try:
    from emulator_discovery import (
        discover_emulators, select_emulator, connect_to_shared_memory,
        add_emulator_args, discover_and_select, cleanup_shared_memory,
        EmulatorInfo
    )
    USE_WEBAPI_DISCOVERY = True
except ImportError:
    print("Warning: emulator_discovery module not found, using legacy PID-based discovery")
    USE_WEBAPI_DISCOVERY = False
    import psutil
    if IS_WINDOWS:
        import win32con
        import win32api
        import pywintypes
    else:
        import posix_ipc

# Import PIL for image processing
try:
    from PIL import Image
except ImportError:
    print("Warning: PIL/Pillow not installed. Screen extraction will be disabled.")
    print("Install with: pip install pillow")

def _connect_to_shared_memory_windows(pid: int):
    """Connect to the emulator's shared memory on Windows."""
    shm_name = f"Local\\zxspectrum_memory{pid}"
    
    try:
        print(f"Connecting to Windows shared memory: {shm_name}")
        
        # Calculate the expected size based on memory regions
        MAP_SIZE = (MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES + MAX_ROM_PAGES) * PAGE_SIZE
        
        try:
            # First check if the shared memory exists using Windows API
            import ctypes
            from ctypes.wintypes import HANDLE, DWORD, LPCWSTR, BOOL
            
            # Define Windows API constants
            FILE_MAP_READ = 0x0004
            
            # Get handle to kernel32.dll
            kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
            
            # Define function prototypes
            kernel32.OpenFileMappingW.argtypes = [DWORD, BOOL, LPCWSTR]
            kernel32.OpenFileMappingW.restype = HANDLE
            kernel32.CloseHandle.argtypes = [HANDLE]
            kernel32.CloseHandle.restype = BOOL
            
            # Try to open the file mapping
            handle = kernel32.OpenFileMappingW(FILE_MAP_READ, False, shm_name)
            
            if not handle:
                error_code = ctypes.get_last_error()
                if error_code == 2:  # ERROR_FILE_NOT_FOUND
                    print("The shared memory does not exist. Is the emulator running with shared memory enabled?")
                    return None
                elif error_code == 5:  # ERROR_ACCESS_DENIED
                    print("Access denied. Try running this script as Administrator.")
                    return None
                else:
                    print(f"Windows error: {error_code}")
                    return None
            
            # Close the handle as we'll use mmap instead
            kernel32.CloseHandle(handle)
            
            # Now open the shared memory with mmap
            shm = mmap.mmap(-1, MAP_SIZE, tagname=shm_name, access=mmap.ACCESS_READ)
            
            # Reset the position after any checks
            shm.seek(0)
            
            print(f"Successfully connected to shared memory ({MAP_SIZE} bytes)")
            return shm
            
        except WindowsError as we:
            if we.winerror == 2:  # File not found
                print("The shared memory does not exist. Is the emulator running with shared memory enabled?")
            elif we.winerror == 5:  # Access denied
                print("Access denied. Try running this script as Administrator.")
            else:
                print(f"Windows error: {we}")
            return None
            
    except Exception as e:
        print(f"Error accessing shared memory: {e}")
        if 'shm' in locals() and shm and not shm.closed:
            shm.close()
        return None

def _connect_to_shared_memory_macos(pid: int):
    """Connect to the emulator's shared memory on macOS."""
    try:
        import posix_ipc
        
        shm_name = f"/zxspectrum_memory-{pid}"
        print(f"Connecting to macOS shared memory: {shm_name}")
        
        try:
            # Try to open the shared memory
            shm = posix_ipc.SharedMemory(shm_name, 0)  # 0 for read-only
            
            # Get the size of the shared memory
            try:
                size = shm.size
                print(f"Shared memory size: {size} bytes")

                # Memory map the shared memory object
                mapped_memory = mmap.mmap(
                    fileno=shm.fd,
                    length=shm.size,
                    access=mmap.ACCESS_READ,
                    flags=mmap.MAP_SHARED,
                    offset=0
                )

                # Close the shared memory object as we don't need it anymore
                # The file descriptor is now owned by the mmap object
                shm.close_fd()
                
                return mapped_memory
                
            except Exception as e:
                print(f"Failed to map shared memory: {e}")
                shm.close_fd()
                return None
                
        except posix_ipc.ExistentialError:
            print(f"Shared memory not found: {shm_name}")
            print("Make sure the emulator is running with shared memory enabled.")
            return None
            
    except ImportError:
        print("Error: The 'posix_ipc' module is required for shared memory access.")
        print("Install it with: pip install posix_ipc")
        return None
    except Exception as e:
        print(f"Error accessing shared memory: {e}")
        import traceback
        traceback.print_exc()
        return None
    
    return None

def _connect_to_shared_memory_linux(pid):
    """Connect to the emulator's shared memory on Linux."""
    try:
        import posix_ipc

        shm_name = f"/zxspectrum_memory-{pid}"
        print(f"Connecting to Linux shared memory: {shm_name}")

        try:
            # Try to open the shared memory
            shm = posix_ipc.SharedMemory(shm_name, 0)  # 0 for read-only

            # Get the size of the shared memory
            try:
                size = shm.size
                print(f"Shared memory size: {size} bytes")

                # Memory map the shared memory object
                mapped_memory = mmap.mmap(
                    fileno=shm.fd,
                    length=shm.size,
                    access=mmap.ACCESS_READ,
                    flags=mmap.MAP_SHARED,
                    offset=0
                )

                # Close the shared memory object as we don't need it anymore
                # The file descriptor is now owned by the mmap object
                shm.close_fd()

                return mapped_memory

            except Exception as e:
                print(f"Failed to map shared memory: {e}")
                shm.close_fd()
                return None

        except posix_ipc.ExistentialError:
            print(f"Shared memory not found: {shm_name}")
            print("Make sure the emulator is running with shared memory enabled.")
            return None

    except ImportError:
        print("Error: The 'posix_ipc' module is required for shared memory access.")
        print("Install it with: pip install posix_ipc")
        return None
    except Exception as e:
        print(f"Error accessing shared memory: {e}")
        import traceback
        traceback.print_exc()
        return None

    return None

def _connect_to_shared_memory_legacy(pid: int):
    """Connect to the emulator's shared memory.
    
    Args:
        pid: Process ID of the emulator
        
    Returns:
        mmap.mmap object if successful, None otherwise
    """
    if IS_WINDOWS:
        return _connect_to_shared_memory_windows(pid)
    elif IS_MACOS:
        return _connect_to_shared_memory_macos(pid)
    elif IS_LINUX:
        return _connect_to_shared_memory_linux(pid)

def get_shared_memory_size(shm_handle) -> int:
    """Get the size of the shared memory segment.
    
    Args:
        shm_handle: Open shared memory handle (mmap.mmap object on Windows, file descriptor on Unix)
        
    Returns:
        int: Size of the shared memory segment in bytes, or 0 if unable to determine
    """
    try:
        if platform.system() == 'Windows':
            # On Windows, we can use the size of the memory-mapped file
            import msvcrt
            import os
            
            # Get the file handle from the mmap object
            file_handle = msvcrt.get_osfhandle(shm_handle.fileno())
            
            # Get file size using Windows API
            import ctypes
            from ctypes.wintypes import DWORD, LARGE_INTEGER
            
            class LARGE_INTEGER(ctypes.Structure):
                _fields_ = [("QuadPart", ctypes.c_longlong)]
            
            file_size = LARGE_INTEGER()
            ctypes.windll.kernel32.GetFileSizeEx(
                file_handle,
                ctypes.byref(file_size)
            )
            
            return file_size.QuadPart
            
        else:
            # On Unix, use fstat to get the size of the shared memory file
            import os
            return os.fstat(shm_handle.fileno()).st_size
            
    except Exception as e:
        print(f"Warning: Could not determine shared memory size: {e}")
        return 0


from typing import Optional, Dict, List, Tuple, BinaryIO, NamedTuple


class RegionStats:
    """Class to track statistics for a memory region."""
    def __init__(self, name: str, total_pages: int):
        self.name = name
        self.total_pages = total_pages
        self.saved_pages = 0
        self.skipped_pages = 0
        self.empty_pages = 0
        self.identified_roms = 0  # Track number of identified ROMs

    def add_saved(self):
        """Increment the count of saved pages."""
        self.saved_pages += 1

    def add_skipped(self, empty: bool = False):
        """Increment the count of skipped pages.
        
        Args:
            empty: If True, also increments the empty pages counter
        """
        self.skipped_pages += 1
        if empty:
            self.empty_pages += 1

    def __str__(self) -> str:
        """Return a string representation of the statistics."""
        return (
            f"{self.name}: {self.saved_pages} saved, "
            f"{self.skipped_pages} skipped ({self.empty_pages} empty), "
            f"{self.total_pages} total"
        )


PAGE_SIZE = 0x4000  # 16KB per page
MAX_RAM_PAGES = 256
MAX_CACHE_PAGES = 2
MAX_MISC_PAGES = 1
MAX_ROM_PAGES = 64

# Memory regions
class MemoryRegion:
    def __init__(self, name: str, start_page: int, end_page: int):
        self.name = name
        self.start_page = start_page
        self.end_page = end_page
        self.size = (end_page - start_page + 1) * PAGE_SIZE

# Define memory regions (page numbers are inclusive)
# Note: This must match the memory layout in slice_memory.py
MEMORY_REGIONS = [
    MemoryRegion("RAM",   0, MAX_RAM_PAGES - 1),                    # 256 pages (starts at 0x000000)
    MemoryRegion("CACHE", MAX_RAM_PAGES, 
                 MAX_RAM_PAGES + MAX_CACHE_PAGES - 1),                # 2 pages
    MemoryRegion("MISC",  MAX_RAM_PAGES + MAX_CACHE_PAGES, 
                 MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES - 1), # 1 page
    MemoryRegion("ROM",   MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES, 
                 MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES + MAX_ROM_PAGES - 1)  # 64 pages
]

def get_region_for_page(page_num: int) -> Optional[MemoryRegion]:
    """Get the memory region for a given page number."""
    for region in MEMORY_REGIONS:
        if region.start_page <= page_num <= region.end_page:
            return region
    return None

def ensure_output_dir(output_dir: str, clear: bool = False) -> None:
    """Ensure the output directory exists and is empty if requested."""
    path = Path(output_dir)
    
    if clear and path.exists():
        # Remove existing directory and all its contents
        import shutil
        shutil.rmtree(path)
    
    # Create the directory (and parent directories if needed)
    path.mkdir(parents=True, exist_ok=True)

# Known ROM signatures (SHA-256 hashes and their descriptions)
KNOWN_ROMS = {
    # ZX-Spectrum 128K (Toaster)
    'd55daa439b673b0e3f5897f99ac37ecb45f974d1862b4dadb85dec34af99cb42': 'Original 48K',
    '3ba308f23b9471d13d9ba30c23030059a9ce5d4b317b85b86274b132651d1425': '128k 0 (128k editor & menu)',
    '8d93c3342321e9d1e51d60afcd7d15f6a7afd978c231b43435a7c0757c60b9a3': '128k 1 (48k BASIC)',
    'c1ff621d7910105d4ee45c31e9fd8fd0d79a545c78b66c69a562ee1ffbae8d72': '128k (Toaster)',
    
    # ZX-Spectrum 128K +2
    'dae0690d8b433ea22b76b62520341f784071dbde0d02f50af0e3fd20fc6bca4a': '128k +2',
    
    # ZX-Spectrum 128K +3
    'ee8218fa43ecb672ed45370114294228213a82318c6d1b007ec86bee3293d1f2': '128k +3',
    
    # Other ROMs
    '39973c2ca4f573cf6188f8eb16338d669c8fd0a78d2683fe059ce56002e7b246': 'Gluck service',
    '9d4bf28f2d1a9acac9907c918be3c3070f7250bc677919cface5e253a199fc7a': 'HRom boot',
    
    # Pentagon
    '7b88abff5964f0cf38481ac51bf035be2c01b8827569876b3d15eb3ac340fef3': 'Pentagon 128k',
    '633343620691a592c706d18c927fd539b7069a5d0fb7011bcd3bfc94be418681': 'Pentagon 128k (128k with TR-DOS in menu)',
    '110020ff7a4e350999777261000442426838cc391be93ba3146abc9477dcc05f': 'Pentagon 48k (48k for 128k)',
    
    # TR-DOS
    '91259fca6a8ded428cc24046f5b48b31d4043f2afbd9087d8946eaf4e10d71a5': 'TR-DOS v5.03',
    'e21d37271d087eab5ef8f88d8f3a58c8c19da1fa857b9790eaa974b231db9e13': 'TR-DOS v5.04T',
    '1ef928538972ed8f0425c4469f3f471267393f7635b813f000de0fec4ea39fa3': 'TR-DOS v5.04TM',
    '075c87ddb55a2fb633373e2d7c834f03e5d44b9b70889499ece732f377f5d224': 'TR-DOS v5.13f',
}

def calculate_sha256(data: bytes) -> str:
    """Calculate SHA-256 hash of the given data."""
    return hashlib.sha256(data).hexdigest()

def identify_rom(data: bytes) -> Tuple[Optional[str], Optional[str]]:
    """
    Try to identify ROM by its content.
    Returns a tuple of (hash, description) if identified, (None, None) otherwise.
    """
    if not data:
        return None, None
        
    # Calculate SHA-256 hash
    hash_value = calculate_sha256(data)
    
    # Check against known ROMs
    if hash_value in KNOWN_ROMS:
        return hash_value, KNOWN_ROMS[hash_value]
    
    return None, None

def get_page_filename(region_name: str, page_num: int, rel_page: int, description: Optional[str] = None) -> str:
    """Generate a filename for a memory page.
    
    Args:
        region_name: Name of the memory region (RAM, ROM, etc.)
        page_num: Absolute page number
        rel_page: Relative page number within the region
        description: Optional description (e.g., for identified ROMs)
        
    Returns:
        str: Generated filename
    """
    if region_name == "ROM" and description:
        # For ROM pages with descriptions, use a more descriptive name
        # Sanitize the description to be filesystem-safe
        safe_description = "".join(c if c.isalnum() or c in ' -_' else '_' for c in description)
        safe_description = safe_description.strip().replace(' ', '_')
        return f"rom_{rel_page:03d}_{safe_description}.bin"
    
    # For other pages, just use the region name and page number
    return f"{region_name.lower()}_{rel_page:03d}.bin"

def is_page_empty(data: bytes) -> bool:
    """Check if a memory page contains only zeros.
    
    Args:
        data: Page data to check
        
    Returns:
        bool: True if the page contains only zeros
    """
    return all(b == 0 for b in data)

def extract_zx_spectrum_screen(data: bytes, output_path: str, verbose: bool = False) -> bool:
    """
    Extract a ZX-Spectrum screen from memory data and save as PNG.
    
    The ZX-Spectrum screen is 256x192 pixels with a complex memory layout:
    - First 6144 bytes: Pixel data (bitmap)
    - Next 768 bytes: Attribute data (colors)
    
    Args:
        data: Memory page data (16KB)
        output_path: Path to save the PNG file
        verbose: Print verbose output
        
    Returns:
        bool: True if successful, False otherwise
    """
    try:
        # Check if PIL is available
        if 'Image' not in globals():
            print("PIL/Pillow not available. Cannot extract screen.")
            return False
            
        # Ensure we have enough data (at least 6912 bytes for the screen)
        if len(data) < 6912:
            print(f"Not enough data for screen extraction: {len(data)} bytes")
            return False
            
        # Create a new RGB image (256x192 pixels)
        img = Image.new('RGB', (256, 192))
        pixels = img.load()
        
        # ZX-Spectrum colors (RGB values)
        zx_colors = [
            (0, 0, 0),       # Black
            (0, 0, 215),     # Blue
            (215, 0, 0),     # Red
            (215, 0, 215),   # Magenta
            (0, 215, 0),     # Green
            (0, 215, 215),   # Cyan
            (215, 215, 0),   # Yellow
            (215, 215, 215), # White
            (0, 0, 0),       # Bright Black
            (0, 0, 255),     # Bright Blue
            (255, 0, 0),     # Bright Red
            (255, 0, 255),   # Bright Magenta
            (0, 255, 0),     # Bright Green
            (0, 255, 255),   # Bright Cyan
            (255, 255, 0),   # Bright Yellow
            (255, 255, 255)  # Bright White
        ]
        
        # Process the screen data
        for y in range(192):
            # Calculate the pixel data address for this line
            # ZX-Spectrum screen has a non-linear memory layout
            y_addr = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2)
            
            # Process each character cell in the line (32 cells per line)
            for x_cell in range(32):
                # Get the 8 pixels for this cell
                pixel_addr = y_addr + x_cell
                pixel_byte = data[pixel_addr]
                
                # Get the attribute byte for this cell
                attr_addr = 6144 + ((y >> 3) << 5) + x_cell
                attr_byte = data[attr_addr]
                
                # Extract colors from attribute byte
                ink = attr_byte & 0x07
                paper = (attr_byte >> 3) & 0x07
                bright = (attr_byte >> 6) & 0x01
                
                # Apply brightness
                if bright:
                    ink += 8
                    paper += 8
                
                # Get the actual RGB colors
                ink_color = zx_colors[ink]
                paper_color = zx_colors[paper]
                
                # Draw the 8 pixels in this cell
                for bit in range(8):
                    x = (x_cell << 3) | bit
                    pixel_value = (pixel_byte >> (7 - bit)) & 0x01
                    pixels[x, y] = ink_color if pixel_value else paper_color
        
        # Save the image
        img.save(output_path, 'PNG')
        
        if verbose:
            print(f"Saved ZX-Spectrum screen to {output_path}")
        
        return True
        
    except Exception as e:
        print(f"Error extracting ZX-Spectrum screen: {e}")
        import traceback
        traceback.print_exc()
        return False

def save_page(output_dir: str, region: MemoryRegion, page_num: int, data: bytes, 
             skip_empty: bool = True, verbose: bool = False) -> Tuple[bool, Optional[str], Optional[str]]:
    """Save a memory page to a file.
    
    Args:
        output_dir: Output directory
        region: Memory region information
        page_num: Absolute page number
        data: Page data
        skip_empty: Skip saving pages that contain only zeros
        verbose: Print verbose output
        
    Returns:
        Tuple[bool, Optional[str], Optional[str]]: (success, ROM description if identified, skip reason)
    """
    rel_page = page_num - region.start_page
    is_rom = region.name == "ROM"
    is_empty = is_page_empty(data)
    rom_description = None
    
    # Handle empty pages
    if is_empty and skip_empty:
        if verbose:
            print(f"Skipping empty {region.name} page: {rel_page:03d}")
        return False, None, 'empty'  # Indicate this was an empty page skip
    
    # Try to identify ROMs
    if is_rom:
        rom_hash, rom_description = identify_rom(data)
        if not rom_description:
            if verbose:
                print(f"Skipping unknown ROM page: {rel_page:03d}")
            return False, None, 'unknown_rom'  # Indicate this was an unknown ROM skip
        if verbose:
            print(f"Identified ROM page {rel_page:03d} as: {rom_description}")
    
    # Generate filename and ensure output directory exists
    filename = get_page_filename(region.name, page_num, rel_page, rom_description)
    region_dir = os.path.join(output_dir, region.name)
    os.makedirs(region_dir, exist_ok=True)
    
    # Save the page data
    filepath = os.path.join(region_dir, filename)
    try:
        with open(filepath, 'wb') as f:
            f.write(data)
        if verbose:
            print(f"Saved {filename} ({len(data)} bytes)")
            if rom_description:
                print(f"  ROM identified as: {rom_description}")
        return True, rom_description, None  # Success, no skip reason
    except Exception as e:
        print(f"Error saving {filename}: {e}", file=sys.stderr)
        return False, None, 'error'  # Indicate this was an error


def cleanup_shared_memory(shm, verbose: bool = False) -> None:
    """Safely close the shared memory handle in a cross-platform manner.
    
    Handles both Windows and Unix-style shared memory objects.
    
    Args:
        shm: Shared memory object to close (mmap.mmap on Windows, posix_ipc.SharedMemory on Unix)
        verbose: Whether to print status messages
    """
    if not shm:
        return
        
    try:
        if verbose:
            print("Cleaning up shared memory resources...")
            
        # Handle Windows mmap object
        if hasattr(shm, 'close') and callable(getattr(shm, 'close')):
            if not getattr(shm, 'closed', True):
                if verbose:
                    print("  Closing memory map...")
                shm.close()
                
        # Handle Unix shared memory file descriptor
        if hasattr(shm, 'close_fd') and callable(getattr(shm, 'close_fd')):
            if verbose:
                print("  Closing shared memory file descriptor...")
            shm.close_fd()
            
        # Handle Unix shared memory unlink if available
        if hasattr(shm, 'unlink') and callable(getattr(shm, 'unlink')):
            try:
                if verbose:
                    print("  Unlinking shared memory...")
                shm.unlink()
            except Exception as e:
                if verbose:
                    print(f"  Note: Could not unlink shared memory (may be already unlinked): {e}")
                    
    except Exception as e:
        print(f"Warning: Error during shared memory cleanup: {e}", file=sys.stderr)


def dump_shared_memory(output_dir: str, shm: mmap.mmap, skip_empty: bool = True, verbose: bool = False, extract_screens: bool = True) -> bool:
    """Dump all memory pages from shared memory to files.
    
    Note: This function does not close the shared memory handle. The caller is responsible
    for cleaning up the shared memory after use.
    
    Args:
        output_dir: Output directory for saved pages
        shm: Shared memory object to read from
        skip_empty: Skip saving pages that contain only zeros
        verbose: Print verbose output
        extract_screens: Whether to extract ZX-Spectrum screens from RAM
        
    Returns:
        bool: True if successful, False otherwise
    """
    # Initialize statistics
    stats = {}
    total_saved = 0
    total_skipped = 0
    total_pages = 0
    
    try:
        # Initialize region stats and calculate total pages
        for region in MEMORY_REGIONS:
            region_pages = region.end_page - region.start_page + 1
            stats[region.name] = RegionStats(region.name, region_pages)
            total_pages += region_pages
            
        # Ensure output directory exists
        ensure_output_dir(output_dir, clear=True)
        
        # Process each memory region
        for region in MEMORY_REGIONS:
            if verbose:
                print(f"\nProcessing {region.name} region (pages {region.start_page}-{region.end_page})...")
            
            for page_num in range(region.start_page, region.end_page + 1):
                # Calculate offset and read page data
                offset = page_num * PAGE_SIZE
                if offset + PAGE_SIZE > shm.size():
                    if verbose:
                        print(f"  Page {page_num:3d}: Offset 0x{offset:08x} out of range (max 0x{shm.size():08x})")
                    continue
                
                shm.seek(offset)
                page_data = shm.read(PAGE_SIZE)
                
                # Save the page (handles empty pages and ROM identification)
                success, rom_description, skip_reason = save_page(output_dir, region, page_num, page_data, skip_empty, verbose)
                
                # Update statistics
                if success:
                    stats[region.name].saved_pages += 1
                    total_saved += 1
                    if region.name == "ROM" and rom_description:
                        stats[region.name].identified_roms += 1
                    
                    # Extract ZX-Spectrum screen from RAM pages 5 and 7
                    if extract_screens and region.name == "RAM" and not is_page_empty(page_data):
                        rel_page = page_num - region.start_page
                        if rel_page in (5, 7):
                            screen_path = os.path.join(output_dir, f"screen_{rel_page:03d}.png")
                            if extract_zx_spectrum_screen(page_data, screen_path, verbose) and verbose:
                                print(f"Extracted ZX-Spectrum screen from RAM page {rel_page} to {screen_path}")
                else:
                    stats[region.name].skipped_pages += 1
                    total_skipped += 1
                    if skip_reason == 'empty':
                        stats[region.name].empty_pages += 1
        
        # Print statistics for each region
        print("\nMemory Region Statistics:")
        for region_name, region_stats in stats.items():
            print(f"\n{region_name} Region:")
            print(str(region_stats))
        
        # Print overall statistics
        print("\nOverall Statistics:")
        print(f"  Total:{total_pages:3d} Saved:{total_saved:3d} Skipped:{total_skipped:3d}")
        
        if verbose:
            print("\nMemory dump completed successfully!")
            
        return True
        
    except Exception as e:
        print(f"\nError in dump_shared_memory: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return False


def main() -> int:
    """Main function."""
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_folder = f"memory_dump_{timestamp}"

    parser = argparse.ArgumentParser(
        description='Dump ZX-Spectrum emulator memory to files',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                          # Auto-select if single emulator
  %(prog)s --emulator 1             # Select first emulator
  %(prog)s --emulator abc123        # Select by ID suffix
  %(prog)s -o my_dump -v            # Custom output, verbose
"""
    )
    parser.add_argument('-o', '--output', default=output_folder,
                      help='Output directory (default: memory_dump_<timestamp>)')
    parser.add_argument('--no-screens', action='store_true',
                      help='Do not extract screen images')
    parser.add_argument('--no-skip-empty', dest='skip_empty', action='store_false',
                      help='Do not skip empty memory pages')
    parser.set_defaults(skip_empty=True)
    parser.add_argument('-v', '--verbose',
                      action='store_true',
                      help='Enable verbose output (shows progress and details)')
    
    # Add WebAPI emulator discovery arguments if available
    if USE_WEBAPI_DISCOVERY:
        add_emulator_args(parser)
    
    args = parser.parse_args()
    
    # Initialize shm to None for the finally block
    shm = None
    
    try:
        if USE_WEBAPI_DISCOVERY:
            # Use WebAPI-based discovery
            emulator = discover_and_select(args)
            if not emulator:
                return 1
            
            if args.verbose:
                print(f"Selected emulator: {emulator.display_name}")
            
            # Connect to shared memory using instance ID
            shm = connect_to_shared_memory(emulator)
            if not shm:
                print("\nTip: Make sure the 'sharedmemory' feature is enabled:")
                print("     feature sharedmemory on")
                return 1
        else:
            # Legacy: Find emulator by PID
            pid = find_emulator_process()
            if not pid:
                print("Error: Could not find the emulator process.")
                print("Please make sure the emulator is running.")
                return 1
            
            if args.verbose:
                print(f"Found emulator process with PID: {pid}")
            
            # Connect to shared memory using PID (legacy)
            shm = _connect_to_shared_memory_legacy(pid)
            if not shm:
                return 1
            
        # Dump memory pages
        if args.verbose:
            print(f"\nOutput directory: {args.output}")
            print(f"Skipping empty pages: {'Yes' if args.skip_empty else 'No'}")
            
        if not dump_shared_memory(args.output, shm, skip_empty=args.skip_empty, 
                              verbose=args.verbose, extract_screens=not args.no_screens):
            return 1
            
        if args.verbose:
            print("\nMemory dump completed successfully!")
            
        return 0
            
    except Exception as e:
        print(f"\nError: {e}")
        traceback.print_exc()
        return 1
        
    finally:
        # Ensure shared memory is properly closed
        cleanup_shared_memory(shm, verbose=args.verbose if hasattr(args, 'verbose') else False)

if __name__ == "__main__":
    sys.exit(main())
