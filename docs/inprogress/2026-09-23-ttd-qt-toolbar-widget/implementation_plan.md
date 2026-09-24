# Implementation Plan: Unreal-QT Toolbar TTD Toggle & Control Widget

This plan outlines the implementation steps to add a right-aligned toggle icon to the `unreal-qt` main transport toolbar and embed a Time Travel Debugging (`TtdWidget`) control panel in the desktop UI.

The primary design specification is documented in [ttd-qt-toolbar-widget-design.md](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/docs/inprogress/2026-09-23-ttd-qt-toolbar-widget/ttd-qt-toolbar-widget-design.md).

## User Review Required

> [!IMPORTANT]
> **Real-Time Scrubbing Behavior**: Any scrubbing or slider position movement in `TtdWidget` will pause the emulator if running, execute `TimeTravelManager::SeekTo({targetFrame, 0})`, and immediately refresh the `DeviceScreen` viewport so the visual output reflects the exact target frame instantly.

## Proposed Changes

### Icons & Resources

#### [NEW] [timetravel.svg](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-qt/resources/icons/timetravel.svg)
- Add 16x16 theme-aware vector SVG icon for Time Travel Debugging (counter-clockwise timeline arc with 22.5° rotated arrowhead).

#### [MODIFY] [icons.qrc](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-qt/resources/icons.qrc)
- Add `<file>icons/timetravel.svg</file>`.

---

### Toolbar & Main Window UI

#### [MODIFY] [toolbarmanager.h](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-qt/src/toolbarmanager.h)
#### [MODIFY] [toolbarmanager.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-qt/src/toolbarmanager.cpp)
- Add a dynamic expanding spacer widget (`QSizePolicy::Expanding`) before the TTD action in `_toolBar`.
- Add `_ttdAction` (`QAction` with `tintedSvgIcon("timetravel")`, checkable).
- Connect `_ttdAction::toggled(bool)` signal to show/hide the `TtdWidget` panel.

#### [NEW] [ttdwidget.h](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-qt/src/widgets/ttdwidget.h)
#### [NEW] [ttdwidget.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-qt/src/widgets/ttdwidget.cpp)
- Create `TtdWidget` inheriting from `QWidget`.
- Top Row: Record/Stop, Load `.ttd`, Export `.ttd`, Clear session buttons, plus live session state & heap memory telemetry label (`TTDSessionInfo`).
- Bottom Row: Jump Start (`|<`), Step Back (`-1F`), Timeline Slider (`QSlider`), Step Forward (`+1F`), Jump End (`>|`), and Resume Recording from Here buttons.
- `QTimer` (`100ms`) updates slider range (`sessionStartFrame` to `currentEndFrame`) and session memory statistics dynamically while recording.

#### [MODIFY] [mainwindow.h](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-qt/src/mainwindow.h)
#### [MODIFY] [mainwindow.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-qt/src/mainwindow.cpp)
- Embed `TtdWidget` into `MainWindow` layout below the transport toolbar.
- Connect toolbar toggle action to show/hide `TtdWidget`.
- Connect timeline slider scrubbing to `TimeTravelManager::SeekTo` and trigger `DeviceScreen` update for instant video frame repainting.

#### [MODIFY] [CMakeLists.txt](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/unreal-qt/src/CMakeLists.txt)
- Register `src/widgets/ttdwidget.h` and `src/widgets/ttdwidget.cpp`.

---

## Verification Plan

### Automated Tests
- Build `unreal-qt` and `core-tests`:
  ```bash
  cmake -S . -B cmake-build-release -G Ninja -DTESTS=ON
  ninja -C cmake-build-release
  ./cmake-build-release/bin/core-tests --gtest_filter="*TTD*"
  ```

### Manual Verification
1. Launch `unreal-qt`, verify right-aligned TTD icon on the toolbar.
2. Toggle icon on/off to expand and collapse `TtdWidget`.
3. Record a live session, drag the timeline scrubber back, and verify the video screen repaints the target frame immediately.
4. Export and load a `.ttd` file, verifying timeline controls and scrubbing on loaded sessions.
