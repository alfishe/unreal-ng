# unreal-ng — Qt6 main window

Cross-platform (Windows / macOS / Linux) Qt6 Widgets front end matching the
approved mockup: native menu bar, optional transport toolbar, emulator screen
with border / 1:1 / fullscreen modes, and a status bar with blinking
tape / disk / HDD / sound indicators and an FPS readout.

All icons are bundled SVGs in `resources/assets.qrc` — the binary needs no
external asset files. On Linux the freedesktop theme icon is used when the
running theme provides one, with the bundled SVG as fallback.

## Build

    cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/<compiler>
    cmake --build build --parallel

    # run
    ./build/unreal-ng-ui              # Linux / Windows
    open build/unreal-ng-ui.app       # macOS

Requires Qt 6.2 or newer and a C++17 compiler. Qt Creator opens
`CMakeLists.txt` directly.

## Layout

    CMakeLists.txt
    resources/assets.qrc          bundled icon set
    resources/icons/*.svg         monochrome 16x16 icons, tinted at runtime
    src/main.cpp
    src/MainWindow.{h,cpp}        menus, toolbar, status bar, actions
    src/ScreenWidget.{h,cpp}      framebuffer painting, border + integer scaling
    src/StatusIndicator.{h,cpp}   one blinking device LED

## Platform behaviour

* **macOS** — Qt moves the menu bar into the system menu automatically;
  Quit and About get their standard menu roles.
* **Windows / Linux** — the menu bar is drawn inside the window, as in the mockup.
* Window geometry plus the View → Toolbar / Status bar choices persist through
  `QSettings`.

## Hooking up an emulator core

`ScreenWidget::setFrame(const QImage &)` takes a 256x192 image; call it once
per emulated frame (a `QTimer` at 50 Hz, or from the core's vblank callback).
`setBorderColor()` follows port 0xFE. The transport actions
(`onStart`, `onPause`, `onRestart`) and media toggles are where the core's
run-state calls belong — they currently only update the UI.
