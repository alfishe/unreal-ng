# TODO: TTD Qt Toolbar Toggle Icon & Control Widget

- [x] Create `timetravel.svg` icon in `unreal-qt/resources/icons/` and update `icons.qrc`.
- [x] Add right-aligned toggle action with dynamic spacer in `ToolBarManager`.
- [x] Implement `TtdWidget` (`unreal-qt/src/widgets/ttdwidget.h` and `ttdwidget.cpp`).
- [x] Integrate `TtdWidget` panel in `MainWindow` layout below transport toolbar.
- [x] Implement real-time scrubbing handler with immediate `SeekTo` and screen refresh in `DeviceScreen`.
- [x] Implement `.ttd` file export / import dialogs in `TtdWidget`.
- [x] Add `ttdwidget.cpp` / `ttdwidget.h` to `unreal-qt/src/CMakeLists.txt`.
- [x] Build project and run `core-tests`.
- [x] Verify UI behavior and screen updates during scrubbing in `unreal-qt`.
