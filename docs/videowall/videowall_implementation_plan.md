# Video Wall Application - Implementation Plan

## Overview

A new Qt application that displays multiple ZX Spectrum emulator instances in a grid-based video wall layout. Each tile represents one emulator instance showing its 256x192 pixel screen output.

## Requirements

### Core Functionality
- Link to `core` and [automation](core/automation) libraries (similar to `unreal-qt`)
- Display multiple emulator instances as tiles in a grid
- Each tile shows ZX Spectrum screen (256x192 pixels, no border)
- 1:1 pixel mapping (no scaling initially)
- All tiles render video simultaneously

### Window Modes
- **Full-screen mode**: Fills screen with maximum number of tiles
- **Frameless window**: Borderless window with tile grid
- **Docked tiles**: Individual tiles that move together as a group

### Layout Management
- Auto-arrange tiles to maintain near-square aspect ratio
- Dynamically adjust layout when emulators start/stop
- Tiles should not block interaction with OS or other windows

### 4K Resolution Support
For **4K UHD (3840×2160)** displays:
- **15×11 grid = 165 tiles** (3840×2112 pixels)
- Perfect horizontal fit (15 × 256 = 3840)
- Vertical: 11 × 192 = 2112 (48px gap at top/bottom - center content)
- Full-screen mode on 4K displays automatically uses this configuration

### Interaction
- Drag and drop files (snapshots, disk images) to individual tiles
- Focused tile receives keyboard input
- Click tile to focus

## Architecture

### Application Structure

```
unreal-videowall/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── videowall/
│   │   ├── VideoWallWindow.h/cpp        # Main window
│   │   ├── EmulatorTile.h/cpp           # Single emulator tile widget
│   │   ├── TileLayoutManager.h/cpp      # Grid layout logic
│   │   └── TileGrid.h/cpp               # Container for tiles
│   └── resources/
│       └── videowall.qrc
└── README.md
```

### Key Components

#### 1. VideoWallWindow
Main application window managing the overall video wall.

**Responsibilities:**
- Window mode management (full-screen, frameless, windowed)
- Emulator lifecycle coordination
- Global keyboard shortcuts
- Application settings

#### 2. EmulatorTile
Widget representing a single emulator instance.

**Responsibilities:**
- Render 256x192 framebuffer from emulator
- Accept drag-and-drop file operations
- Handle keyboard focus
- Display tile state (running, paused, stopped)
- Subscribe to emulator's `NC_VIDEO_FRAME_REFRESH` notification

**Qt Features:**
- `QWidget` subclass
- Accept drops via `setAcceptDrops(true)`
- Override [dragEnterEvent](unreal-qt/src/mainwindow.cpp#712-720), [dropEvent](unreal-qt/src/mainwindow.h#97-98)
- Override `paintEvent` for rendering
- Override `focusInEvent`, `focusOutEvent`

#### 3. TileLayoutManager
Calculates optimal grid layout.

**Responsibilities:**
- Calculate grid dimensions (rows × columns) for N emulators
- Optimize for near-square aspect ratio
- Return tile positions and window size
- Handle layout updates on emulator count changes

**Algorithm:**
```
For N emulators:
  cols = ceil(sqrt(N))
  rows = ceil(N / cols)
  
Window size = (cols × 256, rows × 192)
```

#### 4. TileGrid
Container widget managing the tile grid.

**Responsibilities:**
- Layout tiles in calculated grid positions
- Handle tile addition/removal
- Coordinate focus management
- Manage tile visibility

## Implementation Phases

### Phase 1: Basic Structure
- [ ] Create CMake project structure
- [ ] Link core and automation libraries
- [ ] Implement basic `VideoWallWindow`
- [ ] Create empty `EmulatorTile` widget
- [ ] Implement `TileLayoutManager`

### Phase 2: Emulator Integration
- [ ] Integrate `EmulatorManager`
- [ ] Create emulator instances with optimized settings:
  - All features disabled (no debugger, no breakpoints, etc.)
  - Debugging OFF
  - All modular logging OFF (similar to automation instances)
  - Minimal overhead for 165 simultaneous instances
- [ ] Subscribe to video frame notifications
- [ ] Render framebuffer in `EmulatorTile::paintEvent`
- [ ] Handle emulator lifecycle events

**Emulator Creation Pattern:**
```cpp
// Reference: unreal-qt MainWindow::on_actionStart_triggered
// Use same pattern as automation/headless instances
emulatorManager->CreateEmulator(symbolicId, config);
// Disable all logging after creation
emulator->DisableAllModularLogging();
```

### Phase 3: Tile Rendering
- [ ] Implement framebuffer → QImage conversion
- [ ] Render 256x192 screen in tile
- [ ] Handle video frame refresh notifications
- [ ] Optimize rendering performance

### Phase 4: Layout & Window Modes
- [ ] Implement auto-layout calculation
- [ ] Full-screen mode
- [ ] Frameless window mode
- [ ] Docked tiles mode (separate windows)
- [ ] Dynamic layout updates on instance changes

### Phase 5: Interaction
- [ ] Implement keyboard focus per tile
- [ ] Route keyboard events to focused emulator
- [ ] Drag-and-drop file handling
- [ ] Load snapshots/disk images on drop

### Phase 6: Presets & Polish
- [ ] Implement preset system (no auto-save)
- [ ] Create default presets:
  - Full-screen 4K (15×11 = 165 tiles)
  - Full-screen windowed frameless
  - 2×2 (4 tiles)
  - 3×3 (9 tiles)
  - 4×3 (12 tiles)
- [ ] Load/save presets from settings
- [ ] Add visual focus indicator
- [ ] Tile borders/spacing
- [ ] Error handling

## Code Reuse from unreal-qt

The following components should be **reused or adapted** from `unreal-qt`:

### Build System (CMakeLists.txt)
- Copy CMake structure from [unreal-qt/CMakeLists.txt](unreal-qt/CMakeLists.txt)
- Link same libraries (core, automation)
- Use same Qt configuration

### Keyboard Handling
Reference: `unreal-qt/src/keyboard/` (if exists) or [MainWindow](unreal-qt/src/mainwindow.cpp#32-207) keyboard event handling
- **Reuse**: Keyboard mapping logic
- **Reuse**: Key event translation to ZX Spectrum matrix
- **Adapt**: Route events only to focused tile

### Emulator Instance Binding
Reference: [unreal-qt/src/mainwindow.cpp](unreal-qt/src/mainwindow.cpp)
- **Reuse**: `EmulatorManager` integration pattern
- **Reuse**: Notification subscription pattern (`NC_VIDEO_FRAME_REFRESH`, etc.)
- **Reuse**: Framebuffer access and conversion
- **Adapt**: Multiple instances instead of single `_emulator`

### Example Code References
```cpp
// From unreal-qt - keyboard event handling
void MainWindow::keyPressEvent(QKeyEvent* event) { ... }
void MainWindow::keyReleaseEvent(QKeyEvent* event) { ... }

// From unreal-qt - emulator binding
void MainWindow::bindEmulator(std::shared_ptr<Emulator> emulator) { ... }
void MainWindow::subscribeToPerEmulatorEvents() { ... }

// From unreal-qt - framebuffer rendering  
void DeviceScreen::init(int width, int height, uint8_t* buffer) { ... }
```

## Technical Decisions

### Emulator Instance Management

**Option 1: Create emulators on-demand**
- Start with 0 emulators
- User adds emulators via menu/hotkey
- Auto-arrange as instances are added

**Option 2: Pre-allocate grid**
- Specify grid size (e.g., 4×3 = 12 emulators)
- Create all instances at startup
- Empty tiles show placeholder

> **Recommendation**: Start with Option 1 for flexibility

### Window Mode Implementation

**Docked Tiles Mode:**
- Create separate `QWidget` windows for each tile
- Use `Qt::Window | Qt::FramelessWindowHint`
- Track windows in collection
- Move all windows together on drag

**Full-Screen/Frameless:**
- Single window with internal tile grid
- Use `QGridLayout` or manual positioning

### Rendering Strategy

**Direct QPainter rendering:**
```cpp
void EmulatorTile::paintEvent(QPaintEvent* event)
{
    if (!_emulator) return;
    
    FramebufferDescriptor fb = _emulator->GetFramebuffer();
    
    // Convert framebuffer to QImage (256x192, RGB888 or similar)
    QImage image = convertFramebuffer(fb);
    
    QPainter painter(this);
    painter.drawImage(0, 0, image);
}
```

**Frame notification:**
```cpp
// Subscribe to NC_VIDEO_FRAME_REFRESH
messageCenter.AddObserver(NC_VIDEO_FRAME_REFRESH, this, 
    &EmulatorTile::handleVideoFrameRefresh);

void EmulatorTile::handleVideoFrameRefresh(int id, Message* message)
{
    // Check if notification is for our emulator
    if (matches our emulator ID) {
        update(); // Trigger repaint
    }
}
```

## Dependencies

### Required Libraries
- Qt 6 (Widgets module)
- `core` library (emulator, notifications)
- [automation](core/automation) library (CLI, WebAPI - optional)

### CMake Integration
```cmake
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(unreal-videowall
    src/main.cpp
    src/videowall/VideoWallWindow.cpp
    src/videowall/EmulatorTile.cpp
    src/videowall/TileLayoutManager.cpp
    src/videowall/TileGrid.cpp
)

target_link_libraries(unreal-videowall
    Qt6::Widgets
    emulator-core
    # automation-cli  # Optional
)
```

## User Stories

### Story 1: Launch and Add Emulators
1. User launches `unreal-videowall`
2. Empty window appears
3. User presses `Ctrl+N` to add emulator → 1 tile appears (256×192)
4. User presses `Ctrl+N` again → 2 tiles in 2×1 grid (512×192)
5. User presses `Ctrl+N` again → 3 tiles in 2×2 grid (512×384), one empty slot
6. User presses `Ctrl+N` again → 4 tiles in 2×2 grid (512×384)

### Story 2: Drag and Drop
1. User drags `game.sna` file
2. Hovers over tile #3
3. Tile highlights (focus indicator)
4. User drops file
5. Emulator loads snapshot
6. Screen updates with game graphics

### Story 3: Keyboard Focus
1. User clicks tile #2
2. Tile #2 gets visual focus border
3. User presses keyboard keys
4. Keys are sent to emulator #2's keyboard matrix
5. Other tiles continue rendering but don't receive input

## Open Questions

1. **Audio**: Which emulator's audio plays? All of them mixed? Focused tile only?
   - **Recommendation**: Focused tile only

2. **Performance**: Can we render 16+ emulators at 50 FPS simultaneously?
   - **Approach**: Profile and optimize; consider frame skipping if needed

3. **Emulator configuration**: All tiles use same model/settings, or per-tile?
   - **Recommendation**: Same configuration initially; per-tile in Phase 6

4. **Preset System**: Manual save/load vs auto-save?
   - **Decision**: Manual presets only (no auto-save)
   - Default presets: Full-screen 4K, Frameless, 2×2, 3×3, 4×3
   - Saved as JSON in user settings directory

## Preset System Design

**Preset Structure (JSON):**
```json
{
  "name": "4K Full-Screen",
  "grid": { "cols": 15, "rows": 11 },
  "windowMode": "fullscreen",
  "emulatorCount": 165,
  "model": "48K"
}
```

**Default Presets:**
- `4k-fullscreen` → 15×11 (165 tiles, full-screen)
- `4k-frameless` → 15×11 (165 tiles, frameless window)
- `2x2` → 2×2 (4 tiles)
- `3x3` → 3×3 (9 tiles)
- `4x3` → 4×3 (12 tiles)

**UI:**
- Menu: Presets → Load Preset → [list]
- Menu: Presets → Save Current As...
- Hotkeys: `Ctrl+1` through `Ctrl+5` for quick preset switching

## Next Steps

1. Create project structure in `unreal-ng/unreal-videowall/`
2. Set up CMakeLists.txt
3. Implement basic `VideoWallWindow` and `EmulatorTile`
4. Test single emulator rendering
5. Implement grid layout
