# Screen Viewer Implementation Log

## Overview
Implementation log for the `unreal-screen-viewer` application as specified in [tdd.md](./tdd.md).

**Source Location**: `/unreal-screen-viewer/`

---

## 2026-01-17: Project Kickoff

### Session 1: Foundation Setup

#### ✅ Completed
- [x] TDD created and approved
- [x] Implementation log created
- [x] Project directory structure created
- [x] CMakeLists.txt with Qt6 Widgets/Network
- [x] main.cpp entry point
- [x] MainWindow with menu bar, status bar, splitter layout
- [x] WebAPIClient with HTTP REST calls
- [x] EmulatorList widget with auto-selection
- [x] ScreenViewer widget with:
  - [x] Cross-platform shared memory attachment (POSIX + Windows)
  - [x] ZX Spectrum screen rendering from raw RAM
  - [x] Click-to-toggle between Page 5/7
  - [x] Aspect ratio preservation with scaling
  - [x] 50Hz refresh timer
- [x] **BUILD SUCCESSFUL** on macOS

#### 🔧 Technical Notes
- Used `::close()` instead of `close()` to avoid Qt namespace conflict
- ZX Spectrum constants verified against `core/src/emulator/platform.h`:
  - `PAGE_SIZE = 0x4000` (16KB)
  - `MAX_PAGES = 323` (256 RAM + 2 cache + 1 misc + 64 ROM)
  - Screen size: 6912 bytes (6144 bitmap + 768 attributes)

#### 📋 Pending
- [ ] Test with running emulator instance
- [ ] Settings dialog for endpoint configuration
- [ ] Connection error handling improvements
- [ ] Windows build verification
- [ ] Linux build verification

### Phase 3: Emulator List
| Task | Status | Notes |
|:-----|:-------|:------|
| EmulatorList widget | ⏳ | |
| Selection handling | ⏳ | |
| Auto-select single instance | ⏳ | |
| Periodic refresh | ⏳ | |

### Phase 4: Shared Memory Integration
| Task | Status | Notes |
|:-----|:-------|:------|
| Cross-platform shm helper | ⏳ | POSIX + Windows |
| Attach/detach lifecycle | ⏳ | |
| Memory layout handling | ⏳ | |

### Phase 5: Screen Rendering
| Task | Status | Notes |
|:-----|:-------|:------|
| ScreenViewer widget | ⏳ | |
| Raw RAM rendering | ⏳ | Copy from core/video |
| Click-to-toggle (Page 5/7) | ⏳ | |
| Ratio-constrained scaling | ⏳ | |

### Phase 6: Polish
| Task | Status | Notes |
|:-----|:-------|:------|
| Settings dialog | ⏳ | Port configuration |
| Connection error handling | ⏳ | |
| Status bar | ⏳ | |
| Cross-platform testing | ⏳ | macOS, Linux, Windows |

---

## Technical Notes

### Shared Memory Name Format
- POSIX: `/unreal-ng-{uuid}`
- Windows: `Local\\unreal-ng-{uuid}`

### Screen Memory Offsets
- Page 5: `5 * 0x4000 = 0x14000` (main screen)
- Page 7: `7 * 0x4000 = 0x1C000` (shadow screen)
- Screen size: 6912 bytes (6144 bitmap + 768 attributes)

### Rendering Reference
Copy from `core/src/video/` — do NOT link core library.

---

## Build Commands

```bash
# From unreal-screen-viewer directory
mkdir -p build && cd build
cmake ..
make -j$(nproc)
./bin/unreal-screen-viewer
```

---

## 2026-01-18: Dual Screen Mode Feature

### Session 1: Feature Planning

#### ✅ Design Decisions
- [x] Dual screen mode displays Bank 5 and Bank 7 simultaneously
- [x] Two layout options: horizontal (side-by-side) and vertical (stacked)
- [x] Mode toolbar at bottom of emulator list panel
- [x] Icons: `[1▢]` single, `[▢▢]` dual, `═` horizontal, `║` vertical
- [x] Layout buttons only visible in dual mode
- [x] Persist mode/layout via QSettings
- [x] Click-to-toggle disabled in dual mode

#### 🔧 Implementation Scope
| Component | Action | Notes |
|:----------|:-------|:------|
| `ModeToolbar.h/cpp` | NEW | Toolbar widget with toggle buttons |
| `ScreenViewer.h/cpp` | MODIFY | Add ViewMode, DualLayout enums; dual rendering |
| `MainWindow.h/cpp` | MODIFY | Add toolbar, connect signals, QSettings |
| `CMakeLists.txt` | MODIFY | Add new source files |

#### 📋 Pending
- [ ] Implement ModeToolbar widget
- [ ] Add ViewMode/DualLayout enums to ScreenViewer
- [ ] Implement dual rendering in paintEvent()
- [ ] Add screen labels for dual mode
- [ ] Persist mode/layout via QSettings
- [ ] Test with running emulator

### Session 2: Implementation Complete

#### ✅ Completed
- [x] Created `ModeToolbar.h/cpp` with toggle buttons ([1] single, [2] dual, ═ horizontal, ║ vertical)
- [x] Added `ViewMode` and `DualLayout` enums to ScreenViewer
- [x] Implemented dual rendering in `paintEvent()` with horizontal/vertical layouts
- [x] Added "Bank 5"/"Bank 7" labels in dual mode
- [x] Connected ModeToolbar signals to ScreenViewer
- [x] Added QSettings persistence for mode/layout
- [x] Updated CMakeLists.txt with new source file
- [x] **BUILD SUCCESSFUL** on macOS

#### 🔧 Technical Notes
- Dual horizontal: 8:3 aspect ratio (512×192 effective)
- Dual vertical: 4:6 aspect ratio (256×384 effective)
- Click-to-toggle disabled in dual mode (both screens visible)
- Settings saved to `~/Library/Preferences/com.UnrealNG.ScreenViewer.plist` on macOS

#### 📋 Ready for Testing
- Manual testing with running emulator required
- Verify mode switching, layout toggle, and persistence


