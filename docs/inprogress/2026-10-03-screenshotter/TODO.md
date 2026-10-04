# Screenshotter - TODO

Status: implemented and on `origin/master` (2026-10-03); what is left is verification that could not be done.

Done (owner decisions 1-7 in [design.md](design.md)):
- Survey of every screenshot / capture path and of `RasterDescriptor`.
- Phase 1, geometry (`e7b6f60fd`, `9e2354050`): `PictureGeometry` latched with every presented frame,
  `Screen::SnapshotPresented`, `Screen::WorkingWindow` (TS-Conf window from V_CONFIG), marker tests of the working
  window: the ZX family and Pentagon overscan on BOTH renderers (batch and per-T-state), the four TS-Conf windows,
  the FT812 picture, and the ATM, Profi and Sprinter suites. Pentagon overscan: the paper is at (48,64); the table,
  the batch renderer, the beam mapping, the video mapper and the Qt viewport presets were corrected to it.
- Phase 2, `Screenshotter` (`cbf214711`): crop and encode of one snapshot, typed errors, strict words,
  unique GIF temp files.
- Phase 3, surfaces (`82119b826`): WebAPI + OpenAPI, MCP, CLI, Lua, Python, Qt Save Screenshot As.
- `21a8c2633`: `source=live` (served by the emulation thread, or read directly from a parked machine, with the beam
  position), the ZX-Poly composed frame, the recording MainScreen window locked at start, docs, recipes, tool scripts.
- Checked on a running app: WebAPI (all words, statuses, save, FT812 1024x768 by default), CLI, Lua, live on a running
  and a paused machine, recording sizes (Pentagon 256x192, TS-Conf 640x480).

Dropped by the owner (2026-10-03): live runs of the Python `capture_screen`, the Qt "Save Screenshot As" dialog and the
ZX-Poly composed screenshot, and `Gif_Test` on Windows (all built and covered by tests, not exercised live). If one of
them misbehaves it comes back as a bug.

Follow-up tasks (owner: valid, one at a time; each is a suspicion from reading the code, to be confirmed first, then
fixed with a test; none was reproduced yet):

1. **ULA beam widget in Pentagon overscan.** `unreal-qt/src/debugger/widgets/ulabeamwidget.cpp` takes the paper offset and
   the visible height from the TIMING row (`GetTimingDescriptor`, the Pentagon row: paper at line 48, 288 lines) and
   knows nothing about the 16 extra top lines of P384 (`ZxGeometry::kP384ExtraTopLines`, paper at (48,64) in the 384x304
   frame). Suspected: in P384 the beam marker is 16 lines off the picture. Confirm in the running app in overscan, fix
   from the frame geometry (`Screen::DescribeCurrentFrame` / the raster storage row) rather than a P384 special case.
2. **Video recording of configurations whose resolution changes: DONE 2026-10-04** (owner decision: `full` is the
   default everywhere, `screen` on request). `full` was never affected: the TS-Conf frame is always 720x288. `screen`
   keeps the size of the working window at the start of the recording (the file has one size) and, for every frame,
   crops the CURRENT working window (`Screen::DescribeCurrentFrame().screenWindow`: the TS-Conf `V_CONFIG` window, the
   TSU window, the Pentagon overscan window) and fits it into that size: nearest pixel, aspect kept, opaque black bars
   (`RecordingManager::FitPicture`, the same idea as the Profi display). Nothing is cut and no frame is dropped when
   the window grows, shrinks or the overscan toggles; a jump of scale at a mode change is the price. All three start
   paths (`StartRecording`, `StartRecordingEx`, `StartRecordingWithEncoder`) lock the start geometry in
   `PrepareVideoGeometry()`. Tests: `TsConfAspect_Test.ScreenRegionFitsALaterWindowIntoTheFilesSize`,
   `RecordingManager_Test.FitPicture_*`. The CLI `videorecord` has no `region` option (WebAPI, MCP, Lua, Python and
   the Qt widgets have): not added here.
3. **`source=live` when the emulation thread serves no frame: DONE 2026-10-03.** Checked by reading `MainLoop::OnFrameEnd`:
   the request was served only on rendered, non-replay frames. A TTD seek needs no fix (the machine is parked, the buffer is read
   directly); a throwaway replay pass is not a real frame and keeps waiting for the next one. Turbo render decimation was
   the real wait (up to the next rendered frame, many at unlimited speed): a frame that was not rendered now serves the request
   with the last rendered, finished frame (what the window shows). Not run live (port 8090 was held by another session's app).
4. **GIF writer and non-ASCII paths (Windows): DONE 2026-10-03.** The vendored gif-h (`core/src/3rdparty/gif`) opened its
   file with the narrow `fopen`, which reads a path in the ANSI code page on Windows; the recording GIF encoder had the
   same exposure (`GifBegin(..., filename.c_str())` with a UTF-8 path). Owner allowed a local change: `GifBegin` takes a
   UTF-8 path on every platform (Windows: UTF-16 and `_wfopen_s`, an invalid-UTF-8 string falls back to the ANSI open as
   before) and `GifBeginFile` starts a GIF on a file the caller opened (`FileHelper::OpenFile`). The screenshotter's temp
   file path is UTF-8 (`u8string`) and the file is read back through `FileHelper`. Tests: `Gif_Test` (Cyrillic, CJK and
   emoji folder and file names, `GifBeginFile`, null file, missing folder), `Screenshotter_Test.GifWorksWithANonAsciiTempFolder...`
   (TMPDIR with a non-ASCII name, the temp file removed). The Windows branch is not executed anywhere here (the tests
   pass on macOS where UTF-8 is native): it was compiled warning-free with MinGW (`x86_64-w64-mingw32-g++`); run `Gif_Test`
   on a Windows build when one is at hand.
5. **Found by the MinGW check of item 4: `FrameRect` broke the Windows build.** My phase-1 type `FrameRect` clashes with the
   Win32 GDI function `FrameRect` in `winuser.h` (any `windows.h` before `screen.h` turns `FrameRect screenWindow;` into
   an error). It was on master since `e7b6f60fd`; renamed to `PictureRect` everywhere, every touched translation unit of
   the screenshotter work compiles with MinGW.

6. **TS-Conf mid-frame mode switches, recording sizes, TSU window: DONE 2026-10-04** (found with `zifi.spg`, whose
   screen is a 256C header, a TXT list and a 256C status bar switched by line interrupts):
   - A `V_CONFIG` mode change ran the full `Screen::SetVideoMode`: it cleared the framebuffer and the presented frames
     and posted `NC_VIDEO_MODE_CHANGED` (the GUI re-attached its screen, twice a frame). Result: a black screenshot
     with only the last segment, black recordings, a flickering window. Fixed twice: `ScreenTSConf::SetVideoMode`
     keeps everything inside the TS family (`f40928ee0`, test `VID6`), and the generic `Screen::SetVideoMode` /
     `AllocateFramebuffer` keep the frames and stay quiet for any same-size switch (AlCo via EFF7, ZX <-> 128K,
     test `ScreenModeSwitch_Test`). A switch that changes the frame size (ATM 320/640 wide, P384) still reallocates
     and notifies; a frame drawn mid-frame in another geometry cannot be kept.
   - `RecordingManager` derived the picture size once and never again: the second recording on the same emulator
     (screen after full, or the reverse) inherited the first size and its frames were dropped (`ae4582595`, test
     `VideoSize_EachRecordingDerivesItsOwn`). Only a size given to `SetVideoResolution` is kept.
   - `WorkingWindow()` of TS-Conf now is the whole 360x288 window when `T_CONFIG[0]`, a TSU layer (`T_CONFIG[7:5]`)
     and not `NOTSU` make the TSU show over the border (test `GEOM2`).
   Not checked on a running GUI after the fixes (the flicker was explained from the code, not seen on the fix).

Possible follow-ups, not part of this work: the Qt "Take Screenshot" (clipboard) still crops to the window's
viewport by design; `StoresHalfHeightLines` doubling in recordings of TS-Conf.
