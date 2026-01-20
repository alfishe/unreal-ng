# Dual Screen Mode Implementation Plan

## Summary

Add a **dual screen mode** to the Screen Viewer that displays both Bank 5 (main) and Bank 7 (shadow) screens simultaneously. Users can switch between single and dual modes via a compact toolbar at the bottom of the emulator list panel.

## Proposed Changes

### 1. New `ModeToolbar` Widget
- **[NEW]** `ModeToolbar` class (QWidget)
- Location: Bottom of emulator list panel, above status bar
- Icons:
  - `[1▢]` - Single screen mode (toggle)
  - `[▢▢]` - Dual screen mode (toggle)
  - `═` - Horizontal layout (side-by-side) — visible in dual mode
  - `║` - Vertical layout (stacked) — visible in dual mode

---

### Component Changes

#### [MODIFY] [MainWindow.cpp](unreal-screen-viewer/src/MainWindow.cpp)

1. Add `ModeToolbar*` member and create it in [setupUI()](unreal-screen-viewer/src/MainWindow.cpp#63-83)
2. Position between emulator list and status bar
3. Connect signals to [ScreenViewer](unreal-screen-viewer/src/ScreenViewer.cpp#36-50) for mode changes
4. Save/restore mode+layout via `QSettings`

#### [MODIFY] [MainWindow.h](unreal-screen-viewer/src/MainWindow.h)

1. Add `ModeToolbar*` member
2. Add `ViewMode` enum: `Single`, `DualHorizontal`, `DualVertical`

---

#### [MODIFY] [ScreenViewer.cpp](unreal-screen-viewer/src/ScreenViewer.cpp)

1. Add `ViewMode _viewMode` and `DualLayout _dualLayout` members
2. Modify [paintEvent()](unreal-screen-viewer/src/ScreenViewer.cpp#289-339):
   - Single mode: render one screen (current behavior)
   - Dual mode: render both screens with appropriate layout
3. Modify [refreshScreen()](unreal-screen-viewer/src/ScreenViewer.cpp#189-207): fetch both pages when in dual mode
4. Add labels for each screen ("Bank 5", "Bank 7")
5. Disable click-to-toggle in dual mode

#### [MODIFY] [ScreenViewer.h](unreal-screen-viewer/src/ScreenViewer.h)

1. Add enums: `ViewMode { Single, Dual }`, `DualLayout { Horizontal, Vertical }`
2. Add slots: `setViewMode()`, `setDualLayout()`
3. Add signal: `viewModeChanged()`

---

#### [NEW] [ModeToolbar.h](unreal-screen-viewer/src/ModeToolbar.h)

```cpp
class ModeToolbar : public QWidget {
    Q_OBJECT
public:
    explicit ModeToolbar(QWidget* parent = nullptr);

signals:
    void singleModeSelected();
    void dualModeSelected();
    void horizontalLayoutSelected();
    void verticalLayoutSelected();

public slots:
    void setMode(bool isDual);
    void setLayout(bool isHorizontal);
};
```

#### [NEW] [ModeToolbar.cpp](unreal-screen-viewer/src/ModeToolbar.cpp)

- Create toolbar with `QToolButton` icons
- Use `QButtonGroup` for radio-style selection
- Layout buttons shown/hidden based on mode

---

### Dual Mode Rendering

```
┌────────────────┬────────────────┐     ┌────────────────┐
│    Bank 5      │    Bank 7      │     │    Bank 5      │
│                │                │     │                │
│                │                │     ├────────────────┤
│                │                │     │    Bank 7      │
└────────────────┴────────────────┘     │                │
      Horizontal (side-by-side)         └────────────────┘
                                              Vertical
```

---

## Verification Plan

### Manual Testing

Since this is a Qt GUI feature, manual testing is required:

1. **Build the application:**
   ```bash
   cd /Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-screen-viewer
   mkdir -p cmake-build-debug && cd cmake-build-debug
   cmake -DCMAKE_BUILD_TYPE=Debug ..
   make -j8
   ```

2. **Launch with running emulator:**
   - Start `unreal-qt` emulator
   - Launch `./unreal-screen-viewer`
   - Verify emulator appears in list and is auto-selected

3. **Test mode switching:**
   - Click `[1▢]` button → single screen displayed
   - Click `[▢▢]` button → both screens displayed side-by-side
   - Click `═` button → horizontal layout (should already be active)
   - Click `║` button → vertical layout (stacked)

4. **Test persistence:**
   - Set to dual mode + vertical layout
   - Close and reopen the application
   - Verify it remembers the setting

5. **Test labels:**
   - In dual mode, verify "Bank 5" and "Bank 7" labels are visible on each screen

6. **Test click behavior:**
   - In single mode: click toggles Bank 5 ↔ Bank 7
   - In dual mode: click does nothing

---

## Files to Create/Modify

| File | Action |
|------|--------|
| `src/ModeToolbar.h` | NEW |
| `src/ModeToolbar.cpp` | NEW |
| `src/ScreenViewer.h` | MODIFY |
| [src/ScreenViewer.cpp](unreal-screen-viewer/src/ScreenViewer.cpp) | MODIFY |
| [src/MainWindow.h](unreal-screen-viewer/src/MainWindow.h) | MODIFY |
| [src/MainWindow.cpp](unreal-screen-viewer/src/MainWindow.cpp) | MODIFY |
| [CMakeLists.txt](unreal-screen-viewer/CMakeLists.txt) | MODIFY (add new source files) |
