# Unreal Screen Viewer

A cross-platform Qt6-based verification utility for viewing ZX Spectrum emulator screens via shared memory IPC. This tool allows external monitoring of multiple running emulator instances without affecting their performance.

## Features

- **Multi-instance discovery** - Automatically discovers running Unreal-NG emulator instances via WebAPI
- **Real-time display** - View emulator screen at 50fps using zero-copy shared memory IPC
- **Screen switching** - Toggle between main screen (RAM page 5) and shadow screen (RAM page 7)
- **Multi-emulator support** - Switch between multiple running emulator instances instantly
- **Configurable scaling** - Resize window with aspect ratio preservation
- **Cross-platform** - Works on macOS, Linux, and Windows

---

## User Guide

### Quick Start

1. **Start the emulator** - Launch `unreal-qt` (WebAPI is enabled by default on port 8090)
2. **Launch Screen Viewer** - Run `unreal-screen-viewer`
3. **Select an emulato instancer** - Click on an emulator in the list to view its screen (ID is visible in the list)
4. **View the screen** - The emulator's video output appears in real-time

### Emulator List Panel

The left panel shows all discovered emulator instances:

| Column | Description |
|--------|-------------|
| **ID** | Unique emulator UUID (truncated) |
| **Symbolic ID** | User-defined name (if set via CLI/API) |
| **SHM** | Shared memory status: ✓ enabled, ✗ disabled |

- **Click** an emulator to select it and view its screen
- **SHM auto-enables** when you select an emulator without shared memory

### Screen Display

The right panel shows the selected emulator's screen:

- **Click on screen** - Toggle between Page 5 (main) and Page 7 (shadow) screen
- **Resize window** - Screen scales with aspect ratio preserved
- **Refresh rate** - Updates at 50Hz (matches ZX Spectrum frame rate)

### Multi-Emulator Workflow

To monitor multiple emulators:

1. Create additional emulators via CLI:
   ```bash
   # In unreal-qt console or via netcat to port 8765:
   emulator create
   ```

2. Switch between emulators by clicking in the list
3. The viewer automatically:
   - Disables shared memory on the previous emulator
   - Enables shared memory on the newly selected emulator
   - Starts displaying the new emulator's screen

---

## Building

### Prerequisites

- Qt6 (Widgets, Network modules)
- CMake 3.16+
- C++17 compatible compiler

### Build Commands

```bash
# From project root
cd unreal-screen-viewer
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### Running

```bash
./bin/unreal-screen-viewer
```

Or from the project build directory:
```bash
./unreal-screen-viewer/build/bin/unreal-screen-viewer
```

---

## Configuration

Access **Settings** menu to configure:

| Setting | Default | Description |
|---------|---------|-------------|
| WebAPI Host | `localhost` | Emulator host address |
| WebAPI Port | `8090` | WebAPI port number |

### Command-Line Options

```bash
unreal-screen-viewer [options]

Options:
  --host <addr>    WebAPI host (default: localhost)
  --port <port>    WebAPI port (default: 8090)
  --help           Show help
```

---

## Troubleshooting

### Emulator Not Appearing in List

1. **Check WebAPI is running** - Verify port 8090 is listening:
   ```bash
   curl http://localhost:8090/api/v1/emulator/list
   ```

2. **Check firewall** - Ensure port 8090 is not blocked

3. **Verify host/port** - Use Settings to check WebAPI endpoint

### Screen Not Updating

1. **Check shared memory** - SHM column should show ✓
2. **Verify emulator is running** - The emulator must be in "Run" state
3. **Check permissions** - macOS may require additional permissions for shared memory

### "Connection Refused" Error

The emulator is not running or WebAPI is disabled. Start the emulator first.

### Blank/Black Screen

1. The emulator may be displaying a black screen (normal during loading)
2. Try toggling between Page 5 and Page 7 by clicking on the screen
3. Ensure something is loaded in the emulator

---

## Architecture

This application is an **IPC-based external observation tool**:

```
┌─────────────────┐         ┌──────────────────┐
│  unreal-qt      │         │  screen-viewer   │
│  (Emulator)     │         │  (Observer)      │
├─────────────────┤         ├──────────────────┤
│ Z80 Emulation   │         │ Screen Display   │
│ Memory Manager  │◄───────►│ Shared Memory    │
│ WebAPI Server   │   SHM   │ Reader           │
│ Port 8090       │◄───────►│ WebAPI Client    │
└─────────────────┘   HTTP  └──────────────────┘
```

### Key Design Principles

- **Zero-copy rendering** - Direct read from emulator's memory buffer via shared memory
- **Non-intrusive** - Does not link emulator core library; purely network + IPC based
- **Lightweight** - Minimal overhead on emulator performance
- **Hot-swappable** - Can connect/disconnect without restarting emulator

### Communication Protocols

| Protocol | Purpose |
|----------|---------|
| **HTTP REST** | Instance discovery, feature toggle (enable/disable shared memory) |
| **POSIX shm** | Real-time screen data (5MB memory region at 50Hz) |

### Shared Memory Layout

The shared memory region (`/zxspectrum_memory-{uuid}`) contains the full emulator memory:
- **RAM pages** - 128K to 4MB depending on model
- **Screen data** - Page 5 (offset 0x4000) and Page 7 (offset 0x8000)
- **ROM pages** - Read-only, at end of region

---

## Technical Details

### Files

| File | Purpose |
|------|---------|
| `main.cpp` | Application entry point |
| `MainWindow.cpp/h` | Main window with splitter layout |
| `EmulatorList.cpp/h` | Left panel - emulator discovery and selection |
| `ScreenViewer.cpp/h` | Right panel - screen rendering from shared memory |
| `WebAPIClient.cpp/h` | HTTP client for WebAPI communication |

### Dependencies

- **Qt6::Widgets** - UI framework
- **Qt6::Network** - HTTP client for WebAPI
- **POSIX shm_open** (macOS/Linux) or **CreateFileMapping** (Windows) - Shared memory

---

## See Also

- [Technical Design Document](../docs/inprogress/2016-01-17-screen-viewer/tdd.md)
- [WebAPI Documentation](../docs/emulator/design/control-interfaces/command-interface.md)
- [Shared Memory Feature](../docs/emulator/design/features.md)
