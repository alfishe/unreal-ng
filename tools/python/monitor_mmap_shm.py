#!/usr/bin/env python3
"""
Memory Map Monitor for ZX-Spectrum Emulator

This script monitors the memory map file (/tmp/zxspectrum_memory<pid>) for changes
and displays which memory pages are being modified in real-time.
"""

import os
import sys
import time
import signal
import glob
import psutil
import mmap
import posix_ipc
from pathlib import Path
from typing import Optional, Dict, Set, Tuple, Callable, Any

# Console helper functions
def clear_screen() -> None:
    """Clear the screen and move cursor to top-left."""
    print("\033[2J\033[H", end='', flush=True)

def move_cursor(line: int, column: int = 0) -> None:
    """Move cursor to the specified position (1-based)."""
    print(f"\033[{line};{column}H", end='', flush=True)

def clear_line(line: Optional[int] = None) -> None:
    """Clear the current line or a specific line if specified (1-based)."""
    if line is not None:
        move_cursor(line)
    print("\033[K", end='', flush=True)

def clear_lines(start_line: int, end_line: int) -> None:
    """Clear multiple lines (inclusive, 1-based)."""
    for line in range(start_line, end_line + 1):
        clear_line(line)

def print_at_line(line: int, text: str, clear: bool = True, flush: bool = True) -> None:
    """Print text at the specified line (1-based).
    
    Args:
        line: Line number (1-based)
        text: Text to print
        clear: Whether to clear the line before printing
        flush: Whether to flush the output
    """
    if clear:
        clear_line(line)
    move_cursor(line, 0)
    end = '\n' if flush else ''
    print(text, end=end, flush=flush)

def with_cursor_pos(line: int, column: int = 0) -> Callable[[Callable], Callable]:
    """Decorator to execute a function with cursor at specified position."""
    def decorator(func: Callable) -> Callable:
        def wrapper(*args, **kwargs) -> Any:
            move_cursor(line, column)
            return func(*args, **kwargs)
        return wrapper
    return decorator

# Memory layout constants (must match slice_memory.py)
PAGE_SIZE = 0x4000  # 16KB per page
MAX_RAM_PAGES = 256
MAX_CACHE_PAGES = 2
MAX_MISC_PAGES = 1
MAX_ROM_PAGES = 64

# Memory region page ranges (calculated from sizes)
RAM_START_PAGE = 0
RAM_END_PAGE = MAX_RAM_PAGES - 1
CACHE_START_PAGE = RAM_END_PAGE + 1
CACHE_END_PAGE = CACHE_START_PAGE + MAX_CACHE_PAGES - 1
MISC_START_PAGE = CACHE_END_PAGE + 1
MISC_END_PAGE = MISC_START_PAGE + MAX_MISC_PAGES - 1
ROM_START_PAGE = MISC_END_PAGE + 1
ROM_END_PAGE = ROM_START_PAGE + MAX_ROM_PAGES - 1

# Memory regions
class MemoryRegion:
    def __init__(self, name: str, start_page: int, end_page: int):
        self.name = name
        self.start_page = start_page
        self.end_page = end_page
        self.size = (end_page - start_page + 1) * PAGE_SIZE

# Define memory regions (page numbers are inclusive)
MEMORY_REGIONS = [
    MemoryRegion("ROM",   ROM_START_PAGE,   ROM_END_PAGE),    # 64 pages (0x000000-0x3FFFFF)
    MemoryRegion("RAM",   RAM_START_PAGE,   RAM_END_PAGE),    # 256 pages (0x400000-0x7FFFFF)
    MemoryRegion("CACHE", CACHE_START_PAGE, CACHE_END_PAGE),  # 2 pages (0x800000-0x80FFFF)
    MemoryRegion("MISC",  MISC_START_PAGE,  MISC_END_PAGE)    # 1 page (0x810000-0x81FFFF)
]

def get_region_for_page(page_num: int) -> Optional[MemoryRegion]:
    """Get the memory region for a given page number."""
    for region in MEMORY_REGIONS:
        if region.start_page <= page_num <= region.end_page:
            return region
    return None

def get_page_info(page_num: int) -> Tuple[str, int]:
    """Get region name and relative page number."""
    region = get_region_for_page(page_num)
    if region is None:
        return "UNK", page_num
    
    # Calculate relative page number within the region
    if region.name == "ROM":
        rel_page = page_num - ROM_START_PAGE
    elif region.name == "RAM":
        rel_page = page_num - RAM_START_PAGE
    elif region.name == "CACHE":
        rel_page = page_num - CACHE_START_PAGE
    elif region.name == "MISC":
        rel_page = page_num - MISC_START_PAGE
    else:
        rel_page = page_num
    
    return region.name, rel_page

def find_shared_memory() -> Optional[Tuple[str, int]]:
    """Find the shared memory region and verify the owner PID."""
    try:
        import posix_ipc
        import subprocess
        import re
        
        # First, find all emulator processes
        emulator_processes = []
        for proc in psutil.process_iter(['pid', 'name', 'cmdline']):
            try:
                cmdline = ' '.join(proc.info['cmdline'] or [])
                if 'unreal-ng' in cmdline:
                    emulator_processes.append({
                        'pid': proc.info['pid'],
                        'cmdline': cmdline
                    })
            except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                continue
        
        if not emulator_processes:
            print("No running emulator process found. Please start the emulator first.")
            return None
            
        print(f"Found emulator processes: {[p['pid'] for p in emulator_processes]}")
        
        # On macOS, we'll try to directly open the shared memory for each emulator process
        shm_name = "/zxspectrum"
        
        # First, try to find any shared memory with our name
        try:
            # Try to open the shared memory
            shm = posix_ipc.SharedMemory(shm_name, 0)  # 0 for read-only
            
            # Get the owner PID of the shared memory
            try:
                # On macOS, we can use lsof to find the process using this shared memory
                result = subprocess.run(['lsof', f'/dev/shm{shm_name}'], capture_output=True, text=True)
                if result.returncode == 0 and len(result.stdout.split('\n')) > 1:
                    # The second line contains the process info
                    parts = re.split(r'\s+', result.stdout.split('\n')[1].strip())
                    if len(parts) >= 2:
                        owner_pid = int(parts[1])
                        
                        # Verify the owner PID matches one of our emulator PIDs
                        for proc in emulator_processes:
                            if proc['pid'] == owner_pid:
                                print(f"Found matching shared memory: {shm_name}, PID={owner_pid}")
                                print(f"Process command: {proc['cmdline']}")
                                shm.close_fd()  # We'll reopen it in the monitor function
                                return shm_name, owner_pid
            except (subprocess.SubprocessError, ValueError, IndexError) as e:
                print(f"Warning: Could not verify shared memory owner: {e}")
                
            # If we couldn't verify the owner, just use the first emulator process
            print(f"Using first emulator process (could not verify shared memory owner)")
            pid = emulator_processes[0]['pid']
            shm.close_fd()

            if pid != 0:
                return shm_name, pid
            else:
                return None
            
        except posix_ipc.ExistentialError:
            print(f"Shared memory not found: {shm_name}")
            print("Make sure the emulator is running with shared memory enabled.")
            return None
        except Exception as e:
            print(f"Error accessing shared memory: {e}")
            return None
            
        except FileNotFoundError:
            print("'ipcs' command not found. Cannot verify shared memory ownership.")
            # Fall back to the old method if ipcs is not available
            shm_name = "/zxspectrum"
            try:
                shm = posix_ipc.SharedMemory(shm_name, 0)  # 0 for read-only
                print(f"Successfully connected to shared memory (without PID verification): {shm_name}")
                pid = emulator_processes[0]['pid']  # Use first found PID
                shm.close_fd()
                return shm_name, pid
            except Exception as e:
                print(f"Error accessing shared memory: {e}")
                return None
        
    except Exception as e:
        print(f"Error finding shared memory: {e}")
        import traceback
        traceback.print_exc()
        return None
        
    except ImportError:
        print("Error: The 'posix_ipc' module is required for shared memory access.")
        print("Install it with: pip install posix_ipc")
        return None
    except Exception as e:
        print(f"Error finding shared memory: {e}")
        return None

def is_process_running(pid: int) -> bool:
    """Check if a process with the given PID is running."""
    return psutil.pid_exists(pid)

# clear_screen() is now defined in the console helpers section above

def print_header(file_path: Path, pid: int, start_time: float):
    """Print the header information."""
    global prev_pages
    uptime = time.time() - start_time
    hours, remainder = divmod(int(uptime), 3600)
    minutes, seconds = divmod(remainder, 60)
    
    print(f"ZX-Spectrum Memory Map Monitor")
    print(f"File: {file_path} (PID: {pid})")
    print(f"Uptime: {hours:02d}:{minutes:02d}:{seconds:02d}  |  "
          f"Updates: {int(uptime * 50):,}  |  "
          f"Pages: {len(prev_pages)} tracked")
    print("-" * 80)

# Global variable to track previous page states
prev_pages = {}

def get_page_display_name(page_num: int) -> str:
    """Get a consistent display name for a page."""
    region, rel_page = get_page_info(page_num)
    return f"{region}{rel_page:3d} (0x{page_num * PAGE_SIZE:06x})"

def monitor_shared_memory(shm_name: str, pid: int, update_interval: float = 0.02):  # 50Hz = 20ms
    """Monitor the shared memory region for changes with a clean, updating display."""
    global prev_pages
    
    # Constants for memory layout
    MAX_PAGES = 256  # Maximum number of pages to track
    
    # Initialize data structures for tracking page changes
    prev_pages = [None] * MAX_PAGES  # Previous page data for comparison
    page_checksums = [0] * MAX_PAGES  # Checksums for quick comparison
    page_active = [False] * MAX_PAGES  # Whether a page is active (has data)
    page_changed = [False] * MAX_PAGES  # Whether page has changed in current check
    page_change_count = [0] * MAX_PAGES  # Count of changes for each page
    page_display_line = [-1] * MAX_PAGES  # Display line for each page (-1 = not displayed)
    last_update_time = {}  # Last time a page was updated
    next_display_line = 0  # Next available display line
    
    # Track page states
    page_checksums = [0] * MAX_PAGES  # Current checksum for each page
    page_changed = [False] * MAX_PAGES  # Whether page has changed in current check
    page_active = [False] * MAX_PAGES  # Whether page is active (has been modified at least once)
    page_change_count = [0] * MAX_PAGES  # Number of times page has changed
    page_display_line = [-1] * MAX_PAGES  # Display line for each active page (-1 = not displayed)
    next_display_line = 0  # Next available display line
    
    # Initialize prev_pages with None - will be filled on first read
    prev_pages = {i: None for i in range(MAX_PAGES)}
    
    # Track last update time for each page
    page_last_update = [0.0] * MAX_PAGES
    

    # Get terminal size
    import shutil
    term_height = shutil.get_terminal_size().lines
    
    # Calculate available lines for memory pages (reserve 3 lines for header/footer)
    max_display_lines = term_height - 3
    
    # Hide cursor and clear screen
    print("\033[?25l\033[2J\033[H", end="")
    
    def get_page_checksum(data: bytes) -> int:
        """Calculate a simple checksum for the page data."""
        return sum(data) & 0xFFFF  # Simple 16-bit checksum
    
    def get_terminal_width() -> int:
        """Get the current terminal width."""
        try:
            return shutil.get_terminal_size().columns
        except:
            return 80  # Default width if we can't determine terminal size
    
    def print_delimiter(line_num: int, char: str = '='):
        """Print a delimiter at the specified line number."""
        width = get_terminal_width()
        print(f"\033[{line_num};0H\033[K{char * width}")
    
    def print_header():
        """Print the header section on line 1."""
        print_at_line(1, f"Monitoring {shm_name} (PID: {pid})", clear=True)
    
    def print_time():
        """Print the current time on line 2."""
        active_count = sum(1 for active in page_active if active)
        print_at_line(2, f"Time: {time.strftime('%H:%M:%S')}  Active pages: {active_count}")
    
    def print_footer():
        """Print the footer section."""
        # Print delimiter on last line
        print_delimiter(term_height - 1)
        # Print exit message on the line above the bottom delimiter
        print_at_line(term_height - 2, "Press Ctrl+C to exit")
    
    # Initial screen setup
    clear_screen()
    
    # Print header on line 1
    print_header()
    
    # Print time on line 2
    print_time()
    
    # Print delimiter on line 3
    print_delimiter(3)
    
    # Clear remaining lines for memory pages (starting from line 4)
    clear_lines(4, term_height - 3)
    
    # Print footer and exit message
    print_footer()
    
    # Move cursor to line 4 (first line after delimiters)
    move_cursor(4, 0)
    
    # Track last time update and file check
    last_time_update = last_file_check = 0  # Force immediate updates
    last_file_mod_time = 0
    initial_scan = True  # Flag to track initial scan
    
    try:
        while True:
            current_time = time.time()
            needs_refresh = False
            
            # Update time every second on line 2
            if current_time - last_time_update >= 1.0:
                print_time()
                last_time_update = current_time
            
            # Check for memory changes more frequently (every 20ms = 50Hz)
            if current_time - last_file_check >= 0.02:  # 50Hz update rate
                last_file_check = current_time
                
                # For shared memory, we'll always check for changes
                needs_refresh = True
                
                if needs_refresh:
                    # Check if process is still running
                    if not is_process_running(pid):
                        print("\n\033[91mProcess is no longer running!\033[0m")
                        break
                    
                    # Read current memory state from shared memory
                    try:
                        import mmap
                        shm = posix_ipc.SharedMemory(shm_name, 0)  # 0 for read-only
                        mm = mmap.mmap(shm.fd, shm.size, mmap.MAP_SHARED, mmap.PROT_READ)
                        current_data = mm.read()
                        
                        # Process each page in the memory
                        actual_pages = len(current_data) // PAGE_SIZE
                        for page_num in range(0, min(actual_pages, MAX_PAGES)):
                            page_start = page_num * PAGE_SIZE
                            if page_start >= len(current_data):
                                continue
                                
                            # Get page data (with padding if needed)
                            page_end = min(page_start + PAGE_SIZE, len(current_data))
                            page_data = current_data[page_start:page_end]
                            if len(page_data) < PAGE_SIZE:
                                page_data += b'\x00' * (PAGE_SIZE - len(page_data))
                            
                            # Calculate checksum for this page
                            current_checksum = get_page_checksum(page_data)
                            
                            # Check if this page has changed
                            if prev_pages[page_num] is None:
                                # First time seeing this page
                                page_active[page_num] = True
                                page_checksums[page_num] = current_checksum
                                prev_pages[page_num] = page_data
                                continue
                            
                            # Check for changes using checksum first (faster)
                            if current_checksum != page_checksums[page_num]:
                                # Count actual byte differences
                                changed_bytes = sum(1 for a, b in zip(prev_pages[page_num], page_data) if a != b)
                                
                                # Always update the display if there are changes
                                if changed_bytes > 0:
                                    page_changed[page_num] = True
                                    page_change_count[page_num] += 1
                                    
                                    # Assign display line if this is a new change
                                    if page_display_line[page_num] == -1:
                                        page_display_line[page_num] = next_display_line
                                        next_display_line = (next_display_line + 1) % (term_height - 6)
                                    
                                    # Update the display for changed pages
                                    display_line = page_display_line[page_num] + 4  # Account for header
                                    if display_line < term_height - 2:  # Don't write over footer
                                        page_name = get_page_display_name(page_num)
                                        print_at_line(
                                            display_line,
                                            f"{page_name}: {time.strftime('%H:%M:%S')} - {changed_bytes:4d} bytes changed (x{page_change_count[page_num]})"
                                        )
                                
                                # Update checksum and previous data for next comparison
                                page_checksums[page_num] = current_checksum
                                prev_pages[page_num] = page_data
                        
                        mm.close()
                        shm.close_fd()
                    except Exception as e:
                        print(f"\n\033[91mError reading shared memory: {e}\033[0m")
                        break
                    
                    # Process each page
                    for page_num in range(0, min(MAX_PAGES, len(current_data) // PAGE_SIZE + 1)):
                        page_start = page_num * PAGE_SIZE
                        if page_start >= len(current_data):
                            continue
                            
                        # Get page data (with padding if needed)
                        page_data = current_data[page_start:page_start + PAGE_SIZE]
                        if len(page_data) < PAGE_SIZE:
                            page_data += b'\x00' * (PAGE_SIZE - len(page_data))
                        
                        # Calculate checksum for the page
                        current_checksum = get_page_checksum(page_data)
                        
                        # For the first time we see a page, just store the data
                        if prev_pages[page_num] is None:
                            page_active[page_num] = True
                            page_checksums[page_num] = current_checksum
                            prev_pages[page_num] = page_data
                            continue
                        
                        # Check for changes using checksum first (faster)
                        if current_checksum != page_checksums[page_num]:
                            # Count actual byte differences
                            changed_bytes = 0
                            if prev_pages[page_num] is not None:
                                changed_bytes = sum(1 for a, b in zip(prev_pages[page_num], page_data) if a != b)
                            
                            # Always update the display if there are changes or if this is the first time
                            if changed_bytes > 0 or page_change_count[page_num] == 0:
                                page_changed[page_num] = True
                                page_change_count[page_num] += 1
                                
                                # Assign display line if this is a new change
                                if page_display_line[page_num] == -1:
                                    page_display_line[page_num] = next_display_line
                                    next_display_line = (next_display_line + 1) % max_display_lines
                                
                                # Always update the display for changed pages
                                display_line = page_display_line[page_num] + 4  # Account for header
                                if display_line < term_height - 2:  # Don't write over footer
                                    page_name = get_page_display_name(page_num)
                                    print_at_line(
                                        display_line,
                                        f"{page_name}: {time.strftime('%H:%M:%S')} - {changed_bytes:4d} bytes changed (x{page_change_count[page_num]})"
                                    )
                            
                            # Update checksum and previous data for next comparison
                            page_checksums[page_num] = current_checksum
                            prev_pages[page_num] = page_data
            
                # After first full scan, update the time to show we're running
                if initial_scan:
                    initial_scan = False
            
                # Track last update time for each page
                current_time = time.time()
                for page_num in range(MAX_PAGES):
                    if page_active[page_num] and page_change_count[page_num] > 0:
                        # Only clear if no changes for 5 seconds
                        if current_time - page_change_count[page_num] * 0.02 > 5.0:
                            # Clear the display line
                            if page_display_line[page_num] != -1:
                                display_line = page_display_line[page_num] + 4
                                clear_line(display_line)
                            
                            # Reset page state but keep the display line assignment
                            page_active[page_num] = False
                            page_changed[page_num] = False
                            page_change_count[page_num] = 0
                            # Don't reset page_display_line to keep the same line for this page
            
                # Reset display lines if no active pages
                if not any(page_active):
                    clear_screen()
                    print_header()
                    print_time()
                    print_delimiter(3)
                    print_footer()
                    next_display_line = 0
            
            # Small delay to prevent CPU hogging
            time.sleep(update_interval)
            
    except KeyboardInterrupt:
        print("\n\033[?25h\033[0mMonitoring stopped by user")
    except Exception as e:
        print(f"\n\033[91mError: {e}\033[0m")
    finally:
        # Show cursor and reset terminal
        print("\033[?25h\033[0m")

def main():
    """Main function."""
    print("ZX-Spectrum Memory Map Monitor")
    print("----------------------------\n")
    
    # Find and monitor the shared memory
    result = find_shared_memory()
    if result is None:
        return 1
        
    shm_name, pid = result
    monitor_shared_memory(shm_name, pid)
    return 0

if __name__ == "__main__":
    sys.exit(main())
