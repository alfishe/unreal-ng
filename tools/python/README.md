# Unreal-NG Python Tools

This directory contains Python utilities for monitoring and debugging the Unreal-NG emulator. These tools provide real-time memory monitoring, screen capture, and memory analysis capabilities.

## Prerequisites

1. **Python 3.6 or higher**
2. **Unreal-NG Emulator** compiled with shared memory support (see [Emulator Integration](#emulator-integration) section)

## Installation

1. Create and activate a virtual environment (recommended):
   ```bash
   python3 -m venv venv
   source venv/bin/activate  # On Windows: venv\Scripts\activate
   ```

2. Install required dependencies:
   ```bash
   pip install -r requirements.txt
   ```

   This will install:
   - `psutil>=5.9.0` - Process management and system monitoring
   - `Pillow>=9.0.0` - Image processing for screen capture
   - Platform-specific dependencies:
     - `pywin32>=305` (Windows only) - Windows API access
     - `posix_ipc>=1.0.5` (macOS/Linux) - POSIX shared memory

## Available Tools

### 1. Memory Monitor (`monitor_mmap_shm.py`)

A real-time memory monitoring tool that displays memory page access patterns and modifications in the emulator's address space.

**Features:**
- Tracks read/write/execute access to memory pages
- Displays memory regions with color-coded access patterns
- Updates in real-time with configurable refresh rate
- Supports all major platforms (Windows, macOS, Linux)

**Usage:**
```bash
python3 monitor_mmap_shm.py [options]

Options:
  -h, --help            show help message and exit
  --interval SECONDS     Update interval in seconds (default: 0.1)
  --no-color            Disable colored output
  --debug               Enable debug output
```

**Example:**
```bash
# Monitor with 200ms update interval
python3 monitor_mmap_shm.py --interval 0.2
```

### 2. Memory Dump Analyzer (`dump_shared_memory.py`)

A utility for capturing and analyzing the emulator's memory state.

**Features:**
- Captures complete memory dumps from running emulator
- Extracts and saves ZX-Spectrum screen to PNG
- Dumps memory regions to binary files
- Can identify known ROM versions

**Usage:**
```bash
python3 dump_shared_memory.py [options]

Options:
  -h, --help            show help message and exit
  -o DIR, --output DIR  Output directory (default: memory_dump)
  --no-screens         Do not extract screen images
  --no-skip-empty      Do not skip empty memory pages
  -v, --verbose        Enable verbose output
```

**Examples:**
```bash
# Basic memory dump with screenshots
python3 dump_shared_memory.py -o my_dump

# Dump memory without screenshots
python3 dump_shared_memory.py --no-screens

# Verbose output with all pages
python3 dump_shared_memory.py --no-skip-empty -v
```

### 3. Memory Slicer (`slice_memory.py`)

A tool for analyzing memory dumps by slicing them into 16KB pages.

**Features:**
- Splits memory dumps into 16KB pages
- Names pages according to ZX Spectrum memory layout
- Identifies empty pages to save space
- Can recognize known ROM versions

**Usage:**
```bash
python3 slice_memory.py INPUT_FILE [options]

Positional arguments:
  INPUT_FILE            Input memory dump file

Options:
  -h, --help            show help message and exit
  -o DIR, --output-dir DIR
                        Output directory (default: pages)
  --skip-empty-pages {yes,no}
                        Skip writing pages that contain only zeros (default: yes)
  -v, --verbose         Enable verbose output
```

**Examples:**
```bash
# Basic usage
python3 slice_memory.py memory.bin

# Specify output directory
python3 slice_memory.py memory.bin -o my_pages

# Include empty pages in output
python3 slice_memory.py memory.bin --skip-empty-pages no
```

## Emulator Integration

For these tools to work, the Unreal-NG emulator must be compiled with shared memory support enabled. This is controlled by the `SHARED_MEMORY_ENABLED` CMake option.

### Compilation Requirements

1. **CMake Option**:
   ```cmake
   -DSHARED_MEMORY_ENABLED=ON
   ```

2. **Memory Configuration** (in `memory.cpp`):
   - The emulator must be compiled with memory mapping enabled
   - Memory regions must be properly exported to shared memory segments
   - The emulator creates memory-mapped files in platform-specific locations:
     - Windows: `Global\\unreal_memory_<PID>`
     - macOS/Linux: `/tmp/unreal_memory_<PID>`

### Verifying Support

1. Start the emulator with debug logging enabled
2. Look for messages about shared memory initialization:
   ```
   [INFO] Allocated and exported memory to shared memory
   [DEBUG] Shared memory handle: <handle>
   ```

## Platform-Specific Notes

### Windows
- Requires running as Administrator for shared memory access
- Uses Windows named shared memory objects
- May require firewall exceptions

### macOS/Linux
- Uses POSIX shared memory
- May require adjusting system limits:
  ```bash
  # Check current limits
  sysctl kern.sysv.shmall kern.sysv.shmmax
  
  # Increase limits temporarily (as root)
  sysctl -w kern.sysv.shmall=16777216
  sysctl -w kern.sysv.shmmax=17179869184
  ```

## Troubleshooting

**Emulator not found**
```
No running emulator process found. Please start the emulator first.
```
- Ensure the emulator is running
- Verify the emulator was compiled with shared memory support
- Check that you have sufficient permissions to access the emulator process

**Permission denied**
- On Windows: Run the script as Administrator
- On macOS/Linux: Check shared memory permissions and SELinux/AppArmor settings

**Screen capture not working**
- Ensure Pillow is installed: `pip install pillow`
- Check that the emulator's screen buffer is properly exposed in shared memory

## License

This project is part of the Unreal-NG emulator and is distributed under the same license.
