# Task: Implementing INT Parameters Dialog

## Overview
Add a new non-blocking dialog to unreal-qt UI for configuring INT timing parameters (intpos and intlen).

## Checklist

### Dialog Implementation
- [ ] Create `IntParametersDialog` header file (`unreal-qt/src/ui/intparametersdialog.h`)
- [ ] Create `IntParametersDialog` implementation file (`unreal-qt/src/ui/intparametersdialog.cpp`)
- [ ] Implement dialog with two sliders:
  - [ ] `intpos` slider (0-2000, step 1) with manual input spinbox
  - [ ] `intlen` slider (1-512, step 1) with manual input spinbox
  - [ ] Apply button
- [ ] Load current values from emulator config on dialog open
- [ ] Apply changes to emulator config when Apply is clicked

### Menu Integration
- [ ] Add "INT Parameters..." menu item to Tools menu in [MenuManager](unreal-qt/src/menumanager.cpp#10-38)
- [ ] Connect menu action to dialog opening handler in [MainWindow](unreal-qt/src/mainwindow.h#52-56)
- [ ] Create dialog instance handler in [MainWindow](unreal-qt/src/mainwindow.h#52-56)

### Build System
- [ ] Update [CMakeLists.txt](CMakeLists.txt) to include new dialog files

### Testing
- [ ] Verify dialog opens from menu
- [ ] Test slider controls and manual input
- [ ] Verify values are correctly loaded from config
- [ ] Verify Apply button updates config values
- [ ] Test that dialog is non-blocking
