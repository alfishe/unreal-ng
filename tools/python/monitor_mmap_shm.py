#!/usr/bin/env python3
"""
Memory Map Monitor for ZX-Spectrum Emulator

This script monitors the memory map file (/tmp/zxspectrum_memory<pid>) for changes
and displays which memory pages are being modified in real-time.
"""

import sys
import time
import psutil
import mmap
import posix_ipc
from pathlib import Path
from typing import Optional, Dict, Set, Tuple, Callable, Any

# Debug mode flag - set to True to enable detailed debug output
DEBUG_MODE = False

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
MEMORY_REGIONS = [
    MemoryRegion("RAM",   0, MAX_RAM_PAGES - 1),  # 256 pages (starts at 0x000000)
    MemoryRegion("CACHE", MAX_RAM_PAGES, 
                 MAX_RAM_PAGES + MAX_CACHE_PAGES - 1),  # 2 pages
    MemoryRegion("MISC",  MAX_RAM_PAGES + MAX_CACHE_PAGES, 
                 MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES - 1),  # 1 page
    MemoryRegion("ROM",   MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES, 
                 MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES + MAX_ROM_PAGES - 1)  # 64 pages
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
    rel_page = page_num - region.start_page
    return region.name, rel_page

def find_shared_memory() -> Optional[Tuple[str, int]]:
    """Find the shared memory region and verify the owner PID."""
    try:
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
        
        # Try to connect to shared memory
        shm_name = "/zxspectrum"
        
        try:
            # Try to open the shared memory
            shm = posix_ipc.SharedMemory(shm_name, 0)  # 0 for read-only
            
            # If we successfully opened the shared memory, use the first process ID
            pid = emulator_processes[0]['pid']
            print(f"Successfully connected to shared memory: {shm_name}, using PID={pid}")
            
            # Close the file descriptor - we'll reopen it in the monitor function
            shm.close_fd()
            
            return shm_name, pid
            
        except posix_ipc.ExistentialError:
            print(f"Shared memory not found: {shm_name}")
            print("Make sure the emulator is running with shared memory enabled.")
            return None
        except Exception as e:
            print(f"Error accessing shared memory: {e}")
            return None
            
    except ImportError:
        print("Error: The 'posix_ipc' module is required for shared memory access.")
        print("Install it with: pip install posix_ipc")
        return None
    except Exception as e:
        print(f"Error finding shared memory: {e}")
        import traceback
        traceback.print_exc()
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
    page_checksums = [0] * MAX_PAGES  # Checksums for quick comparison
    page_active = [False] * MAX_PAGES  # Whether a page is active (has data)
    page_changed = [False] * MAX_PAGES  # Whether page has changed in current check
    page_change_count = [0] * MAX_PAGES  # Count of changes for each page
    page_display_line = [-1] * MAX_PAGES  # Display line for each page (-1 = not displayed)
    page_last_update = [0.0] * MAX_PAGES  # Last time a page was updated
    next_display_line = 0  # Next available display line
    
    # Initialize prev_pages with None - will be filled on first read
    prev_pages = {i: None for i in range(MAX_PAGES)}
    
    # Get terminal size
    import shutil
    term_height = shutil.get_terminal_size().lines
    
    # Calculate available lines for memory pages (reserve 6 lines for header/footer)
    max_display_lines = term_height - 6
    
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
    
    def clear_content_area():
        """Clear the monitoring content area and redraw UI elements."""
        # Clear monitoring area (lines 4 to term_height-3)
        clear_lines(4, term_height - 3)
        print_time()  # Update the time display
        print_delimiter(3)  # Redraw the delimiter
        print_footer()  # Redraw the footer
        move_cursor(4, 0)  # Move cursor back to start of monitoring area
    
    # Initial screen setup
    clear_screen()
    print_header()
    print_time()
    print_delimiter(3)
    clear_lines(4, term_height - 3)
    print_footer()
    move_cursor(4, 0)
    
    # Track last time update and memory check
    last_time_update = 0
    last_memory_check = 0
    initial_scan = True  # Flag to track initial scan
    
    shm = None
    try:
        # Try to open the shared memory
        try:
            print(f"Attempting to connect to shared memory: '{shm_name}'")
            shm = posix_ipc.SharedMemory(shm_name, 0)  # 0 for read-only
            print(f"Connected to shared memory '{shm_name}' (size: {shm.size} bytes)")
            
            # Print memory region information (only in debug mode)
            if DEBUG_MODE:
                print("\nMemory Regions:")
                for region in MEMORY_REGIONS:
                    region_size = (region.end_page - region.start_page + 1) * PAGE_SIZE
                    print(f"  {region.name}: pages {region.start_page}-{region.end_page} (0x{region.start_page*PAGE_SIZE:06x}-0x{(region.end_page+1)*PAGE_SIZE-1:06x}, {region_size} bytes)")
                print(f"\nTotal expected memory: {(MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES + MAX_ROM_PAGES) * PAGE_SIZE} bytes")
            
        except posix_ipc.ExistentialError:
            print(f"\n\033[91mError: Shared memory '{shm_name}' not found\033[0m")
            print("Make sure the emulator is running with shared memory enabled.")
            print("Check if the emulator was started with the correct shared memory settings.")
            return
        except Exception as e:
            print(f"\n\033[91mError opening shared memory: {e}\033[0m")
            import traceback
            traceback.print_exc()
            return
        
        while True:
            current_time = time.time()
            
            # Update time display every second
            if current_time - last_time_update >= 1.0:
                print_time()
                last_time_update = current_time
            
            # Check for memory changes at the specified rate
            if current_time - last_memory_check >= update_interval:
                last_memory_check = current_time
                
                # Check if process is still running
                if not is_process_running(pid):
                    print("\n\033[91mProcess is no longer running!\033[0m")
                    break
                
                # Read current memory state from shared memory
                try:
                    # Map the shared memory and read its content
                    try:
                        # Map and read shared memory
                        mm = mmap.mmap(shm.fd, shm.size, mmap.MAP_SHARED, mmap.PROT_READ)
                        current_data = mm.read()
                        mm.close()
                        
                        # Clean monitoring area and reset on first successful scan
                        if initial_scan:
                            clear_content_area()  # Clear and redraw the monitoring area
                            initial_scan = False
                        
                    except Exception as e:
                        print(f"\n\033[91mError mapping/reading shared memory: {e}\033[0m")
                        import traceback
                        traceback.print_exc()
                        break
                    
                    # Verify we got some data
                    if not current_data:
                        print("\n\033[91mError: No data read from shared memory\033[0m")
                        break
                        
                    # Make sure we have enough data
                    if len(current_data) < (MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES + MAX_ROM_PAGES) * PAGE_SIZE:
                        print(f"\n\033[91mError: Not enough data in shared memory (got {len(current_data)} bytes, expected at least {(MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES + MAX_ROM_PAGES) * PAGE_SIZE} bytes)\033[0m")
                        break
                    
                    # Process each memory region
                    for region in MEMORY_REGIONS:
                        # Ensure we don't exceed MAX_PAGES or shared memory size
                        end_page = min(region.end_page, MAX_PAGES - 1)
                        max_possible_page = (len(current_data) // PAGE_SIZE) - 1
                        end_page = min(end_page, max_possible_page)
                        
                        if region.start_page > end_page:
                            continue
                            
                        for page_in_region in range(region.start_page, end_page + 1):
                            page_start = page_in_region * PAGE_SIZE
                            if page_start >= len(current_data):
                                continue
                                
                            # Get page data (with padding if needed)
                            page_end = min(page_start + PAGE_SIZE, len(current_data))
                            page_data = current_data[page_start:page_end]
                            if len(page_data) < PAGE_SIZE:
                                page_data += b'\x00' * (PAGE_SIZE - len(page_data))
                            
                            # Calculate checksum for this page
                            current_checksum = get_page_checksum(page_data)
                            
                            # For the first time we see a page, just store the data
                            if prev_pages[page_in_region] is None:
                                page_active[page_in_region] = True
                                page_checksums[page_in_region] = current_checksum
                                prev_pages[page_in_region] = page_data
                                page_last_update[page_in_region] = current_time
                                continue
                            
                            # Check for changes using checksum first (faster)
                            if current_checksum != page_checksums[page_in_region]:
                                # Count actual byte differences
                                changed_bytes = sum(1 for a, b in zip(prev_pages[page_in_region], page_data) if a != b)
                                
                                # Update the display if there are changes
                                if changed_bytes > 0:
                                    page_changed[page_in_region] = True
                                    page_change_count[page_in_region] += 1
                                    page_last_update[page_in_region] = current_time
                                    
                                    # Update display for changed pages
                                    if page_display_line[page_in_region] == -1:
                                        page_display_line[page_in_region] = next_display_line
                                        next_display_line = (next_display_line + 1) % max_display_lines
                                    
                                    # Only show changes in the terminal if not in debug mode
                                    if not DEBUG_MODE:
                                        display_line = page_display_line[page_in_region] + 4  # Account for header
                                        if display_line < term_height - 2:  # Don't write over footer
                                            region_name, rel_page = get_page_info(page_in_region)
                                            page_name = f"{region_name}[{rel_page:02X}]"
                                            print_at_line(
                                                display_line,
                                                f"{page_name}: {time.strftime('%H:%M:%S')} - {changed_bytes:4d} bytes changed (x{page_change_count[page_in_region]})"
                                            )
                                    elif DEBUG_MODE:
                                        region_name, rel_page = get_page_info(page_in_region)
                                        print(f"{region_name}[{rel_page:02X}]: {changed_bytes} bytes changed (x{page_change_count[page_in_region]})")
                                
                                # Update checksum and previous data for next comparison
                                page_checksums[page_in_region] = current_checksum
                                prev_pages[page_in_region] = page_data
                            #else:
                            #    print(f"  Page {page_in_region}: No change (checksum: {current_checksum:08x})")
                    
                except Exception as e:
                    print(f"\n\033[91mError reading shared memory: {e}\033[0m")
                    break
                
                # After first full scan, update the time to show we're running
                if initial_scan:
                    initial_scan = False
                    print_time()
                
                # Check for pages that haven't been updated in a while (5 seconds)
                for page_num in range(MAX_PAGES):
                    if page_active[page_num] and page_change_count[page_num] > 0:
                        # Only clear if no changes for 5 seconds
                        if current_time - page_last_update[page_num] > 5.0:
                            # Clear the display line
                            if page_display_line[page_num] != -1:
                                display_line = page_display_line[page_num] + 4
                                clear_line(display_line)
                            
                            # Reset page state but keep the display line assignment
                            page_active[page_num] = False
                            page_changed[page_num] = False
                            page_change_count[page_num] = 0
            
            # Small delay to prevent CPU hogging
            time.sleep(0.01)  # Small sleep even between checks
            
    except KeyboardInterrupt:
        print("\n\033[?25h\033[0mMonitoring stopped by user")
    except Exception as e:
        print(f"\n\033[91mError: {e}\033[0m")
        import traceback
        traceback.print_exc()
    finally:
        # Close the shared memory if it's open
        if shm is not None:
            try:
                shm.close_fd()
            except:
                pass
        
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
