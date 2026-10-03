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

Not verified:
- Python `capture_screen`: Python automation is OFF in this build; it was only syntax-checked against the vendored
  pybind11 (three older warnings in `python_emulator.h`, lines 316 / 2601 / 2604, fail a `-Werror` Python build and are
  not from this work).
- The Qt Tools > Save Screenshot As dialog was built, not clicked.
- The ZX-Poly composed screenshot is covered by a test, not run in the app.

Follow-up tasks (owner: valid, one at a time; each is a suspicion from reading the code, to be confirmed first, then
fixed with a test; none was reproduced yet):

1. **ULA beam widget in Pentagon overscan.** `unreal-qt/src/debugger/widgets/ulabeamwidget.cpp` takes the paper offset and
   the visible height from the TIMING row (`GetTimingDescriptor`, the Pentagon row: paper at line 48, 288 lines) and
   knows nothing about the 16 extra top lines of P384 (`ZxGeometry::kP384ExtraTopLines`, paper at (48,64) in the 384x304
   frame). Suspected: in P384 the beam marker is 16 lines off the picture. Confirm in the running app in overscan, fix
   from the frame geometry (`Screen::DescribeCurrentFrame` / the raster storage row) rather than a P384 special case.
2. **Video recording of configurations whose resolution changes: POSTPONED (owner, 2026-10-03).** Needs a universal
   algorithm for what a recording does when the picture size or window changes while it runs (TS-Conf `V_CONFIG`
   windows 256x192 / 320x200 / 320x240 / 360x288, the Pentagon overscan toggle, an external FT812 picture switching on
   or off, ZX-Poly); decide it once for all of them, not per machine. What is known: `RecordingManager` locks the
   MainScreen window at the start (`_mainScreen*`) so the encoder size stays constant, which for TS-Conf (the frame is
   always 720x288) does not skip frames as first suspected; the symptom is a picture cut to the old rectangle when the
   window grows, and the whole frame (window plus a wide border) when it shrinks. Options discussed: record the maximal
   window the machine can have (for TS-Conf the whole frame; constant size, nothing cut, a wide border around small
   windows); follow the current window and scale into the locked size with bars (tight crop, a jump of scale at every
   mode change, scaling cost per frame); document the limit. Start with a test that reproduces the cut picture (record
   from the 256x192 window, switch `V_CONFIG` to 360x288, compare the recorded frame with the screen), then choose.
3. **`source=live` when the emulation thread serves no frame:** during a TTD replay (`ttdReplayActive` skips the serve
   call) and under heavy turbo render decimation (`_renderThisFrame` false) the request waits out its 1 s timeout and
   the answer is 409 `no-frame`. Confirm, then serve the request from those paths too (or answer at once with a
   specific reason instead of waiting).
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

Possible follow-ups, not part of this work: the Qt "Take Screenshot" (clipboard) still crops to the window's
viewport by design; `StoresHalfHeightLines` doubling in recordings of TS-Conf.
