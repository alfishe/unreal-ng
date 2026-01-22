# Unreal Video Wall

A multi-instance ZX Spectrum emulator display application that arranges multiple running emulator instances in a grid layout—perfect for demonstrations, testing, retro computing exhibitions, or creating impressive visual displays of classic software running simultaneously.

![ZX Spectrum Video Wall](../docs/images/videowall-preview.png)

## Overview

**Unreal Video Wall** is a Qt6-based application that spawns and manages multiple independent ZX Spectrum emulator instances, each displayed as a tile in an auto-arranged grid. Built on top of the Unreal-NG emulator core, it provides a visually striking way to run many emulators simultaneously with full WebAPI and CLI automation support.

### Key Features

- **Multi-Instance Display**: Run dozens of ZX Spectrum emulators simultaneously in a grid layout
- **Dynamic Grid Layout**: Tiles auto-arrange based on window size; supports 1x, 2x, and 3x scaling
- **Window Modes**: Windowed, frameless, and fullscreen modes with automatic grid optimization
- **Audio Binding**: Click any tile to bind audio output—only one tile plays sound at a time
- **Drag-and-Drop Loading**: Drop `.sna`, `.z80`, or `.trd` files directly onto tiles to load software
- **Keyboard Focus**: Click a tile to give it keyboard input focus for interactive control
- **Preset Configurations**: Save and load tile arrangements for quick setup
- **4K Display Support**: Up to 180 tiles at native 1x resolution on 4K displays
- **Automation Ready**: Full WebAPI and CLI automation for external control

---

## Window Modes

### Windowed Mode (Default)
Standard window with title bar and controls. Manually add/remove tiles as needed.

### Frameless Mode (`Cmd/Ctrl+F`)
Removes window decorations for a cleaner appearance. The grid automatically fills the available space.

### Fullscreen Mode (`Cmd/Ctrl+Shift+F`)
Maximizes to fill the entire screen with an automatically calculated optimal grid layout.

---

## Tile Scaling & Grid Capacity

Tile size is configurable in `TileLayoutManager.h`. Default is 2x scaling (512×384 pixels per tile).

| Resolution | 1x (256×196) | 2x (512×384) | 3x (768×576) |
|------------|--------------|--------------|--------------|
| 4K (3840×2160) | 15×12 = 180 | 8×6 = 48 | 5×4 = 20 |
| QHD (2560×1600) | 10×9 = 90 | 5×5 = 25 | 4×3 = 12 |
| FullHD (1920×1080) | 8×6 = 48 | 4×3 = 12 | 3×2 = 6 |

---

## User Guide

### Getting Started

1. Launch `unreal-videowall`
2. Press `Cmd+N` (macOS) or `Ctrl+N` (Windows/Linux) to add emulator tiles
3. Click a tile to give it focus and bind audio
4. Drag and drop snapshot files onto tiles to load software

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Cmd/Ctrl+N` | Add new emulator tile |
| `Cmd/Ctrl+W` | Remove last tile |
| `Cmd/Ctrl+F` | Toggle frameless mode |
| `Cmd/Ctrl+Shift+F` | Toggle fullscreen mode |
| `Cmd/Ctrl+S` | Toggle Screen HQ for all tiles |
| `Cmd/Ctrl+Q` | Quit application |
| `Click tile` | Focus tile + bind audio |

### Loading Software

**Drag and Drop**: Drag `.sna`, `.z80`, or `.trd` files directly onto any tile.
- Green flash = successful load
- Red flash = load failed

**Via Automation**: Use WebAPI or CLI commands to load files programmatically (see Developer section).

### Audio Management

Only one tile can play audio at a time. Click any tile to:
1. Focus that tile for keyboard input
2. Bind audio output to that tile's emulator
3. Unbind audio from the previously focused tile

---

## Building

### Prerequisites

- **Qt6** (6.5.0 or later)
- **CMake** 3.16+
- **C++17** compatible compiler

### Build from Root (Recommended)

```bash
cd unreal-ng
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target unreal-videowall -j8
```

### Standalone Build

```bash
cd unreal-ng/unreal-videowall
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j8
```

### Run

```bash
./bin/unreal-videowall
```

---

## Developer Guide

### Architecture

```
unreal-videowall/
├── src/
│   ├── main.cpp                    # Application entry point
│   ├── emulator/
│   │   └── soundmanager.cpp/h      # Audio device management
│   ├── keyboard/
│   │   └── keyboardmanager.cpp/h   # Keyboard input handling
│   └── videowall/
│       ├── VideoWallWindow.cpp/h   # Main window controller
│       ├── TileGrid.cpp/h          # Grid container widget
│       ├── EmulatorTile.cpp/h      # Individual tile widget
│       └── TileLayoutManager.h     # Layout calculation utilities
```

### Core Classes

| Class | Responsibility |
|-------|----------------|
| `VideoWallWindow` | Main window, menu handling, mode switching, audio binding |
| `TileGrid` | Container managing tile collection and layout updates |
| `EmulatorTile` | Individual tile widget with emulator binding, rendering, input |
| `TileLayoutManager` | Static layout calculation for optimal grid arrangement |

### Adding Tile Scaling Options

Edit `TileLayoutManager.h` to change default tile size:

```cpp
// ZX Spectrum native: 256×192
static constexpr int TILE_WIDTH = 512;   // 2x scaling
static constexpr int TILE_HEIGHT = 384;
```

### Automation Integration

The videowall supports WebAPI and CLI automation via the `Automation` class:

```cpp
// VideoWallWindow.h
std::unique_ptr<Automation> _automation;  // WebAPI + CLI support
```

WebAPI endpoints can query and control individual emulator instances by their UUID.

### Emulator Lifecycle

Each tile owns a `shared_ptr<Emulator>` created via `EmulatorManager`:

```cpp
auto emu = EmulatorManager::GetInstance().CreateInstance();
auto tile = new EmulatorTile(emu, _tileGrid);
_tileGrid->addTile(tile);
```

Tiles subscribe to emulator frame updates for rendering and automatically unsubscribe on destruction.

### Async Batch Creation

For performance, tiles are created in batches when entering fullscreen mode:

```cpp
void VideoWallWindow::createEmulatorsAsync(int total);
void VideoWallWindow::createNextBatch();  // BATCH_SIZE = 4, BATCH_DELAY_MS = 100
```

This prevents UI freezing when spawning many emulator instances.

---

## Configuration

### Preset System

Presets save the current tile arrangement and can be loaded later:

```cpp
void loadPreset(const QString& presetName);
void savePreset(const QString& presetName);
```

Presets are stored in `QSettings` and persist across sessions.

### Command Line Options

```bash
./unreal-videowall [options]

Options:
  --fullscreen    Start in fullscreen mode
  --frameless     Start in frameless mode
  --tiles N       Start with N tiles
```

---

## Troubleshooting

### No Audio Output
- Click a tile to bind audio to it
- Check system audio output device selection
- Verify sound feature is enabled (`Cmd+S` toggles HQ mode)

### Tiles Not Rendering
- Ensure ROM files are present in `bin/rom/` directory
- Check that `unreal.ini` configuration exists in `bin/`

### High CPU Usage
- Reduce number of tiles
- Switch to 1x tile scaling (smaller tiles, but more efficient)
- Disable Screen HQ mode with `Cmd+S`

---

## License

Part of the [Unreal-NG](https://github.com/alfishe/unreal-ng) ZX Spectrum emulator project.
