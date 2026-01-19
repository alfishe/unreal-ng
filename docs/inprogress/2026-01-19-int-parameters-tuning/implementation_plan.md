# INT Parameters Dialog Implementation Plan

## Overview

Add a new non-blocking dialog to the unreal-qt UI for configuring Z80 interrupt timing parameters. The dialog will be accessible via a new "INT Parameters..." menu item in the Tools menu.

## User Review Required

> [!IMPORTANT]
> The config file uses the parameter name `instart` internally, but the UI will display it as `intpos` as requested by the user.

## Proposed Changes

### UI Components

#### [NEW] [intparametersdialog.h](unreal-qt/src/ui/intparametersdialog.h)
- Dialog class with two slider controls:
  - **intpos** slider (0-2000, step 1) - maps to `config.intstart`
  - **intlen** slider (1-512, step 1) - maps to `config.intlen`
- Each slider paired with QSpinBox for manual value entry
- Non-blocking (modeless) dialog
- Apply button to commit changes to emulator config
- Close button to dismiss dialog

#### [NEW] [intparametersdialog.cpp](unreal-qt/src/ui/intparametersdialog.cpp)
- [loadValues()](unreal-qt/src/ui/intparametersdialog.cpp#110-133) - Load current values from active emulator's `config.intstart` and `config.intlen`
- [applyValues()](unreal-qt/src/ui/intparametersdialog.cpp#134-162) - Write updated values back to emulator config
- Slider/spinbox synchronization
- Success confirmation message after Apply

---

### Menu Integration

#### [MODIFY] [menumanager.h](unreal-qt/src/menumanager.h)
- Add `QAction* _intParametersAction` field to Tools menu actions section

#### [MODIFY] [menumanager.cpp](unreal-qt/src/menumanager.cpp)
- Add "INT Parameters..." menu item in [createToolsMenu()](unreal-qt/src/menumanager.cpp#328-352)
- Position after Settings action, before screenshot/video actions
- Connect to signal `intParametersRequested()`

#### [MODIFY] [mainwindow.h](unreal-qt/src/mainwindow.h)
- Add slot `handleIntParametersRequested()`
- Add field `IntParametersDialog* _intParametersDialog = nullptr`

#### [MODIFY] [mainwindow.cpp](unreal-qt/src/mainwindow.cpp)  
- Implement `handleIntParametersRequested()` to create/show dialog
- Connect MenuManager signal to slot
- Clean up dialog in destructor if exists

---

### Build System

#### [MODIFY] [CMakeLists.txt](unreal-qt/CMakeLists.txt)
- Add [src/ui/intparametersdialog.h](unreal-qt/src/ui/intparametersdialog.h) to UNREAL_QT_HEADERS
- Add [src/ui/intparametersdialog.cpp](unreal-qt/src/ui/intparametersdialog.cpp) to UNREAL_QT_SOURCES

## Verification Plan

### Manual Testing

1. **Build the updated unreal-qt application:**
   ```bash
   cd /Volumes/TB4-4Tb/Projects/Test/unreal-ng/build-unified
   cmake --build . --target unreal-qt
   ```

2. **Launch unreal-qt and create an emulator instance:**
   - Run the application
   - Click "Start" to create an emulator

3. **Open INT Parameters dialog:**
   - Navigate to Tools → INT Parameters...
   - Verify dialog opens and is non-blocking

4. **Test slider controls:**
   - Move intpos slider - verify label updates and spinbox syncs
   - Move intlen slider - verify label updates and spinbox syncs
   - Type values directly into spinboxes - verify sliders sync

5. **Test value loading:**
   - Check that initial values match config (default intstart=0, intlen=32)
   - Close and reopen dialog - values should persist

6. **Test Apply functionality:**
   - Change intpos to 100
   - Change intlen to 64
   - Click Apply
   - Verify success message appears
   - Close dialog, reopen - verify new values are loaded

7. **Test edge cases:**
   - Set intpos to 0 (minimum)
   - Set intpos to 2000 (maximum)
   - Set intlen to 1 (minimum)
   - Set intlen to 512 (maximum)
   - Verify all values are accepted
