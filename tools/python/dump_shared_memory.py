#!/usr/bin/env python3
"""
Dump shared memory pages from ZX-Spectrum Emulator

This script connects to the emulator's shared memory and dumps all memory pages
into separate files, organized by memory region type (ROM, RAM, CACHE, MISC).
"""

import os
import sys
import time
import argparse
import psutil
import posix_ipc
import mmap
import hashlib
from pathlib import Path
from typing import Optional, Dict, List, Tuple, BinaryIO

# Memory layout constants (must match emulator)
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

def save_page(output_dir: str, region: MemoryRegion, page_num: int, data: bytes, 
             skip_empty: bool = True, verbose: bool = False) -> Tuple[bool, Optional[str]]:
    """Save a memory page to a file.
    
    Args:
        output_dir: Output directory
        region: Memory region information
        page_num: Absolute page number
        data: Page data
        skip_empty: Skip saving pages that contain only zeros
        verbose: Print verbose output
        
    Returns:
        Tuple[bool, Optional[str]]: (success, ROM description if identified)
    """
    # Calculate relative page number
    rel_page = page_num - region.start_page
    
    # Check if page is empty
    if skip_empty and is_page_empty(data):
        if verbose:
            print(f"Skipping empty page: {region.name}_{rel_page:03d}")
        return False, None
    
    # For ROM pages, try to identify them
    rom_description = None
    if region.name == "ROM":
        _, rom_description = identify_rom(data)
        if verbose:
            if rom_description:
                print(f"Identified ROM page {rel_page:03d} as: {rom_description}")
            else:
                print(f"Could not identify ROM page {rel_page:03d}")
    
    # Generate filename
    filename = get_page_filename(region.name, page_num, rel_page, rom_description)
    
    # Create region subdirectory
    region_dir = os.path.join(output_dir, region.name)
    os.makedirs(region_dir, exist_ok=True)
    
    # Save the page
    filepath = os.path.join(region_dir, filename)
    try:
        with open(filepath, 'wb') as f:
            f.write(data)
        if verbose:
            print(f"Saved {filename} ({len(data)} bytes)")
            if rom_description:
                print(f"  ROM identified as: {rom_description}")
        return True, rom_description
    except IOError as e:
        print(f"Error saving {filename}: {e}", file=sys.stderr)
        return False, None

def find_emulator_processes() -> List[dict]:
    """Find running emulator processes."""
    emulator_processes = []
    
    for proc in psutil.process_iter(['pid', 'name', 'cmdline']):
        try:
            # Look for the emulator process
            if 'unreal-ng' in ' '.join(proc.info['cmdline'] or []):
                emulator_processes.append({
                    'pid': proc.info['pid'],
                    'name': proc.info['name'],
                    'cmdline': ' '.join(proc.info['cmdline'] or [])
                })
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            continue
    
    return emulator_processes

def connect_to_shared_memory(pid: Optional[int] = None) -> Optional[Tuple[str, int]]:
    """Connect to the shared memory segment used by the emulator.
    
    Args:
        pid: Optional process ID to connect to. If None, will try to find the emulator process.
        
    Returns:
        Tuple of (shared_memory_name, owner_pid) or None if not found
    """
    shm_name = "/zxspectrum"
    
    if pid is None:
        # Try to find the emulator process
        emulator_processes = find_emulator_processes()
        if not emulator_processes:
            print("No emulator processes found")
            return None
        
        # Use the first emulator process found
        pid = emulator_processes[0]['pid']
        print(f"Using emulator process: {pid}")
    
    try:
        # Try to open the shared memory
        shm = posix_ipc.SharedMemory(shm_name, 0)  # 0 for read-only
        print(f"Connected to shared memory: {shm_name}")
        return shm_name, pid
    except posix_ipc.ExistentialError:
        print(f"Shared memory not found: {shm_name}")
        print("Make sure the emulator is running with shared memory enabled.")
        return None
    except Exception as e:
        print(f"Error accessing shared memory: {e}")
        return None

def get_shared_memory_size(shm_name: str) -> int:
    """Get the size of the shared memory segment."""
    try:
        # On macOS, we can use the `ls` command to get the size of the shared memory file
        result = subprocess.run(['ls', '-l', f'/dev/shm{shm_name}'], 
                              capture_output=True, text=True)
        if result.returncode == 0:
            # Extract the size from the ls -l output
            parts = result.stdout.strip().split()
            if len(parts) >= 5:
                return int(parts[4])
    except Exception as e:
        print(f"Warning: Could not determine shared memory size: {e}")
    
    return 0

class RegionStats:
    def __init__(self, name: str, total_pages: int):
        self.name = name
        self.total_pages = total_pages
        self.saved_pages = 0
        self.skipped_pages = 0
        self.identified_roms = 0

    def print_stats(self):
        """Print statistics for this memory region."""
        stats = f"Total:{self.total_pages:3d} Saved:{self.saved_pages:3d} Skipped:{self.skipped_pages:3d}"
        if self.name == "ROM" and self.identified_roms > 0:
            stats += f" ROMs:{self.identified_roms:3d}"
        print(f"  {stats}")

def dump_shared_memory(output_dir: str, skip_empty: bool = True, verbose: bool = False) -> bool:
    """Dump all memory pages from shared memory to files.
    
    Args:
        output_dir: Output directory for saved pages
        skip_empty: Skip saving pages that contain only zeros
        verbose: Print verbose output
        
    Returns:
        bool: True if successful, False otherwise
    """
    try:
        # Connect to shared memory
        result = connect_to_shared_memory()
        if not result:
            return False
        
        shm_name, _ = result
        
        # Get shared memory size for debugging
        shm_size = get_shared_memory_size(shm_name)
        if shm_size > 0:
            print(f"Shared memory size: {shm_size} bytes")
        
        # Open the shared memory
        print(f"Opening shared memory: {shm_name}")
        shm = posix_ipc.SharedMemory(shm_name, 0)  # 0 for read-only
        
        try:
            # Print shared memory info
            print(f"Shared memory opened successfully. Size: {shm.size} bytes")
            
            # Map the shared memory
            print("Mapping shared memory...")
            mm = mmap.mmap(shm.fd, shm.size, mmap.MAP_SHARED, mmap.PROT_READ)
            
            # Ensure output directory exists
            ensure_output_dir(output_dir, clear=True)
            
            # Initialize statistics
            stats = {}
            total_saved = 0
            total_skipped = 0
            total_pages = 0
            
            # Process each memory region
            for region in MEMORY_REGIONS:
                region_pages = region.end_page - region.start_page + 1
                stats[region.name] = RegionStats(region.name, region_pages)
                total_pages += region_pages
                
                if verbose:
                    print(f"\nProcessing {region.name} region (pages {region.start_page}-{region.end_page})...")
                
                for page_num in range(region.start_page, region.end_page + 1):
                    # Calculate offset and size
                    offset = page_num * PAGE_SIZE
                    if offset + PAGE_SIZE > mm.size():
                        if verbose:
                            print(f"  Page {page_num:3d}: Offset 0x{offset:08x} out of range (max 0x{mm.size():08x})")
                        continue
                    
                    # Read page data
                    mm.seek(offset)
                    page_data = mm.read(PAGE_SIZE)
                    
                    # Save the page
                    try:
                        success, rom_description = save_page(output_dir, region, page_num, page_data, skip_empty, verbose)
                        if success:
                            stats[region.name].saved_pages += 1
                            total_saved += 1
                            
                            # Track identified ROMs
                            if region.name == "ROM" and rom_description:
                                stats[region.name].identified_roms += 1
                        else:
                            stats[region.name].skipped_pages += 1
                            total_skipped += 1
                    except Exception as e:
                        print(f"  Error processing page {page_num} (offset 0x{offset:X}): {e}")
                        continue
            
            # Print statistics for each region
            print("\nMemory Region Statistics:")
            for region_name, region_stats in stats.items():
                print(f"\n{region_name} Region:")
                region_stats.print_stats()
            
            # Print overall statistics
            print("\nOverall Statistics:")
            print(f"  Total:{total_pages:3d} Saved:{total_saved:3d} Skipped:{total_skipped:3d}")
            
            # Clean up
            print("\nCleaning up...")
            mm.close()
            shm.close_fd()
            
            print(f"\nDump complete. Output directory: {output_dir}")
            return True
            
        except Exception as e:
            print(f"Error during memory mapping: {e}", file=sys.stderr)
            import traceback
            traceback.print_exc()
            return False
            
        finally:
            # Always close the shared memory handle
            if 'shm' in locals():
                shm.close_fd()
                
    except Exception as e:
        print(f"Error in dump_shared_memory: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return False

def main() -> int:
    """Main function."""
    parser = argparse.ArgumentParser(
        description='Dump shared memory pages from ZX-Spectrum Emulator',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    
    # Optional arguments
    parser.add_argument('-o', '--output-dir', 
                      default='pages',
                      help='Output directory for saved pages')
    
    parser.add_argument('--skip-empty-pages',
                      action='store_true',
                      default=True,
                      help="Skip writing pages that contain only zeros")
    
    parser.add_argument('--no-skip-empty',
                      dest='skip_empty_pages',
                      action='store_false',
                      help="Don't skip empty pages")
    
    parser.add_argument('-v', '--verbose', 
                      action='store_true',
                      help='Enable verbose output')
    
    args = parser.parse_args()
    
    # Dump the shared memory
    success = dump_shared_memory(
        output_dir=args.output_dir,
        skip_empty=args.skip_empty_pages,
        verbose=args.verbose
    )
    
    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
