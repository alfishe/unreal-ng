# Video Wall Improvements (2026-01-11)

Improvements to the video wall application including dynamic grid layout and feature shortcuts.

## Changes

### 1. Dynamic Grid Layout

Centralized tile configuration in `TileLayoutManager.h` - only tile size constants need to be changed:

```cpp
// TILE CONFIGURATION - Only change these to adjust display
static constexpr int TILE_WIDTH = 256;   // 1x, 512 for 2x, 768 for 3x
static constexpr int TILE_HEIGHT = 196;  // 1x, 384 for 2x, 576 for 3x
```

Grid dimensions are calculated dynamically from actual screen resolution via `getFullscreenLayout()`:

```cpp
static GridLayout getFullscreenLayout(int screenWidth, int screenHeight)
{
    GridLayout layout;
    layout.cols = screenWidth / TILE_WIDTH;
    layout.rows = screenHeight / TILE_HEIGHT;
    layout.totalTiles = layout.cols * layout.rows;
    // ...
}
```

**Example configurations:**

| Tiles | 4K (3840×2160) | QHD (2560×1600) | FHD (1920×1080) |
|-------|----------------|-----------------|-----------------|
| 1x (256×196) | 15×11 = 165 | 10×8 = 80 | 7×5 = 35 |
| 2x (512×384) | 7×5 = 35 | 5×4 = 20 | 3×2 = 6 |
| 3x (768×576) | 5×3 = 15 | 3×2 = 6 | 2×1 = 2 |

---

### 2. Screen HQ Toggle (Cmd+S)

Added keyboard shortcut `Cmd+S` (Ctrl+S on Windows/Linux) to toggle the `screenhq` feature for all emulator tiles.

**Default**: Screen HQ enabled for all tiles.

```cpp
// In createMenus():
QAction* screenHQAction = viewMenu->addAction(tr("Toggle Screen &HQ"));
screenHQAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));
connect(screenHQAction, &QAction::triggered, this, &VideoWallWindow::toggleScreenHQForAllTiles);
```

---

## Files Modified

| File | Changes |
|------|---------|
| `TileLayoutManager.h` | Centralized `TILE_WIDTH`/`TILE_HEIGHT` constants, `getFullscreenLayout()` |
| `EmulatorTile.h` | Uses constants from TileLayoutManager |
| `TileGrid.cpp` | Uses `TILE_WIDTH`/`TILE_HEIGHT` for positioning |
| `VideoWallWindow.h` | Added `toggleScreenHQForAllTiles()`, `_screenHQEnabled` |
| `VideoWallWindow.cpp` | `getFullscreenLayout()` usage, Cmd+S shortcut, toggle implementation |
| `test_load_pattern_snapshots.py` | Dynamic grid detection for any screen size |

## Verification

1. Build: `ninja` in cmake-build-release
2. Launch fullscreen: `Cmd+Shift+F`
3. Toggle screen HQ: `Cmd+S`
4. Change tile size in `TileLayoutManager.h`, rebuild, verify correct grid
