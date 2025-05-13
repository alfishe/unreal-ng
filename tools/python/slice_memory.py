#!/usr/bin/env python3
"""
Slice memory dump into 16KB pages with appropriate naming conventions.

This script takes a memory dump file and slices it into 16KB pages,
naming them according to the ZX Spectrum memory layout.
"""

import os
import sys
import argparse
import shutil
import hashlib
from pathlib import Path
from typing import Dict, Optional, Tuple

# Memory layout constants
PAGE_SIZE = 0x4000  # 16KB
MAX_RAM_PAGES = 256
MAX_CACHE_PAGES = 2
MAX_MISC_PAGES = 1
MAX_ROM_PAGES = 64

# Calculate memory region offsets
RAM_OFFSET = 0
CACHE_OFFSET = RAM_OFFSET + (MAX_RAM_PAGES * PAGE_SIZE)
MISC_OFFSET = CACHE_OFFSET + (MAX_CACHE_PAGES * PAGE_SIZE)
ROM_OFFSET = MISC_OFFSET + (MAX_MISC_PAGES * PAGE_SIZE)

def parse_arguments():
    parser = argparse.ArgumentParser(
        description='Slice memory dump into 16KB pages. Each page is saved as a separate file.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    
    # Required arguments
    parser.add_argument('input_file', 
                      help='Path to the memory dump file')
    
    # Optional arguments
    parser.add_argument('-o', '--output-dir', 
                      default='pages',
                      help='Output directory for sliced pages')
    
    parser.add_argument('--skip-empty-pages',
                      choices=['yes', 'no'],
                      default='yes',
                      help="Skip writing pages that contain only zeros ('yes' by default)")
    
    parser.add_argument('-v', '--verbose', 
                      action='store_true',
                      help='Enable verbose output')
    
    return parser.parse_args()

def ensure_output_dir(output_dir, clear=False):
    """Ensure the output directory exists and is empty.
    
    Args:
        output_dir: Path to the output directory
        clear: If True, remove and recreate the directory if it exists
        
    Returns:
        Path to the output directory
    """
    if clear and os.path.exists(output_dir):
        try:
            shutil.rmtree(output_dir)
        except OSError as e:
            print(f"Error: Failed to clear directory {output_dir}: {e}", file=sys.stderr)
            raise
    
    os.makedirs(output_dir, exist_ok=not clear)
    return output_dir

def get_page_name(page_num, address, is_rom=False, description=None):
    """Generate a page name with appropriate prefix and zero-padding.
    
    Args:
        page_num: Page number
        address: Start address of the page
        is_rom: Whether this is a ROM page
        description: Optional description to include in the filename
        
    Returns:
        Formatted filename
    """
    prefix = 'rom' if is_rom else 'ram'
    base_name = f"{prefix}_{page_num:03d}"
    
    # Add description if provided and this is a ROM page
    if is_rom and description:
        # Sanitize the description to be filesystem-safe
        safe_description = "".join(c if c.isalnum() or c in ' -_' else '_' for c in description)
        safe_description = safe_description.strip().replace(' ', '_')
        base_name = f"{base_name} - {safe_description}"
    
    return f"{base_name}.bin"

def is_page_empty(page_data):
    """Check if a memory page contains only zeros."""
    # Check if all bytes in the page are zero
    return all(b == 0 for b in page_data)

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

def slice_memory(input_file, output_dir, skip_empty_pages=True, verbose=False):
    """Slice the memory dump into 16KB pages."""
    # Initialize counters
    stats = {
        'ram': 0,
        'cache': 0,
        'misc': 0,
        'rom': 0,
        'rom_identified': 0,
        'total': 0,
        'skipped': 0,
    }
    
    # Track identified ROMs as list of tuples (page_num, hash, description)
    identified_roms = []
    
    try:
        with open(input_file, 'rb') as f:
            data = f.read()
    except IOError as e:
        print(f"Error reading input file: {e}", file=sys.stderr)
        return False, stats
    
    total_size = len(data)
    if total_size < ROM_OFFSET + (MAX_ROM_PAGES * PAGE_SIZE):
        print(f"Warning: Input file is smaller than expected memory size ({total_size} bytes)", file=sys.stderr)
    
    # Create output directory structure
    ram_dir = os.path.join(output_dir, 'ram')
    cache_dir = os.path.join(output_dir, 'cache')
    misc_dir = os.path.join(output_dir, 'misc')
    rom_dir = os.path.join(output_dir, 'rom')
    
    os.makedirs(ram_dir, exist_ok=True)
    os.makedirs(cache_dir, exist_ok=True)
    os.makedirs(misc_dir, exist_ok=True)
    os.makedirs(rom_dir, exist_ok=True)
    
    # Process RAM pages
    ram_pages = MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES
    for i in range(ram_pages):
        start = i * PAGE_SIZE
        end = start + PAGE_SIZE
        
        if end > len(data):
            if verbose:
                print(f"Skipping incomplete RAM page {i} (end of file)")
            stats['skipped'] += 1
            continue
            
        page_data = data[start:end]
        
        # Skip empty pages if requested
        if skip_empty_pages and is_page_empty(page_data):
            if verbose:
                print(f"Skipping empty page {i:3d} (0x{start:08x}-0x{end:08x})")
            stats['skipped'] += 1
            continue
        
        # Determine page type and output directory
        if i < MAX_RAM_PAGES:
            page_dir = ram_dir
            page_type = 'RAM'
            stats['ram'] += 1
        elif i < MAX_RAM_PAGES + MAX_CACHE_PAGES:
            page_dir = cache_dir
            page_type = 'CACHE'
            stats['cache'] += 1
        else:
            page_dir = misc_dir
            page_type = 'MISC'
            stats['misc'] += 1
            
        page_name = get_page_name(i, start, is_rom=False)
        output_path = os.path.join(page_dir, page_name)
        
        try:
            with open(output_path, 'wb') as f:
                f.write(page_data)
            
            stats['total'] += 1
            
            if verbose:
                print(f"Saved {page_type} page {i:3d} (0x{start:08x}-0x{end:08x}) to {output_path}")
        except IOError as e:
            print(f"Error writing {output_path}: {e}", file=sys.stderr)
            stats['skipped'] += 1
    
    # Process ROM pages
    print("\nProcessing ROM pages...")
    for i in range(MAX_ROM_PAGES):
        start = ROM_OFFSET + (i * PAGE_SIZE)
        end = start + PAGE_SIZE - 1
        
        # Skip if this page would go beyond the end of the input data
        if start >= len(data):
            if verbose:
                print(f"Skipping ROM page {i:3d} (0x{start:08x}-0x{end:08x}) - beyond end of input data")
            stats['skipped'] += 1
            continue
            
        # Get the page data, padding with zeros if needed
        page_end = min(end + 1, len(data))
        page_data = data[start:page_end]
        
        # Pad with zeros if needed
        if len(page_data) < PAGE_SIZE:
            page_data += b'\x00' * (PAGE_SIZE - len(page_data))
            if verbose:
                print(f"Padded ROM page {i:3d} with {PAGE_SIZE - len(page_data)} zeros")
        
        # Skip empty pages if requested
        if skip_empty_pages and is_page_empty(page_data):
            if verbose:
                print(f"Skipping empty ROM page {i:3d} (0x{start:08x}-0x{end:08x})")
            stats['skipped'] += 1
            continue
            
        # Check if this is a known ROM
        rom_hash, rom_description = identify_rom(page_data)
        
        # Generate page name with description if available
        page_name = get_page_name(
            i, 
            start, 
            is_rom=True, 
            description=rom_description if rom_hash else None
        )
        output_path = os.path.join(rom_dir, page_name)
        
        try:
            with open(output_path, 'wb') as f:
                f.write(page_data)
            
            stats['rom'] += 1
            stats['total'] += 1
            
            if rom_hash:
                stats['rom_identified'] += 1
                identified_roms.append((i, rom_hash, rom_description, os.path.basename(output_path)))
                if verbose:
                    print(f"Saved ROM  page {i:3d} (0x{start:08x}-0x{end:08x}) to {output_path}")
            else:
                if verbose:
                    print(f"Saved ROM  page {i:3d} (0x{start:08x}-0x{end:08x}) to {output_path}")
        except IOError as e:
            print(f"Error writing {output_path}: {e}", file=sys.stderr)
            stats['skipped'] += 1
    
    return True, stats, identified_roms

def main():
    args = parse_arguments()
    
    # Validate input file
    if not os.path.isfile(args.input_file):
        print(f"Error: Input file '{args.input_file}' does not exist", file=sys.stderr)
        return 1
    
    # Ensure output directory exists and is empty
    try:
        output_dir = ensure_output_dir(args.output_dir, clear=True)
    except OSError as e:
        print(f"Error preparing output directory: {e}", file=sys.stderr)
        return 1
    
    # Process the memory dump
    skip_empty = args.skip_empty_pages.lower() == 'yes'
    if skip_empty and args.verbose:
        print("Skipping empty pages (use --skip-empty-pages=no to include them)")
        
    success, stats, identified_roms = slice_memory(
        args.input_file,
        output_dir,
        skip_empty_pages=skip_empty,
        verbose=args.verbose
    )
    
    if success:
        print(f"\nSuccessfully sliced memory dump into 16KB pages in '{output_dir}'")
        print("\nMemory Page Statistics:")
        print(f"  RAM pages:      {stats['ram']:4d} / {MAX_RAM_PAGES}")
        print(f"  Cache pages:    {stats['cache']:4d} / {MAX_CACHE_PAGES}")
        print(f"  Misc pages:     {stats['misc']:4d} / {MAX_MISC_PAGES}")
        print(f"  ROM pages:      {stats['rom']:4d} / {MAX_ROM_PAGES}")
        if stats['rom'] > 0:
            print(f"  ROMs identified: {stats['rom_identified']:4d} / {stats['rom']}")
        print(f"  Total saved:    {stats['total']:4d} pages")
        if stats['skipped'] > 0:
            print(f"  Skipped:        {stats['skipped']:4d} pages")
        
        # Print identified ROMs
        if identified_roms:
            print("\nIdentified ROMs:")
            for page_num, rom_hash, description, filename in sorted(identified_roms, key=lambda x: x[0]):
                print(f"  Page {page_num:3d}: {description}")
                print(f"       {rom_hash}")
                print(f"       Saved as: {filename}")
        
        print(f"\nOutput directories:")
        print(f"  RAM:    {os.path.join(output_dir, 'ram')}")
        print(f"  Cache:  {os.path.join(output_dir, 'cache')}")
        print(f"  Misc:   {os.path.join(output_dir, 'misc')}")
        print(f"  ROM:    {os.path.join(output_dir, 'rom')}")
        
        return 0
    else:
        return 1

if __name__ == "__main__":
    sys.exit(main())
