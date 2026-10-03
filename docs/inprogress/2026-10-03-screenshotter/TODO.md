# Screenshotter - TODO

Status: design agreed 2026-10-03 (decisions 1-7 in [design.md](design.md)), no code yet.

Done:
- Survey of every screenshot/capture path (WebAPI, MCP, CLI, Lua, Python, Qt, recording, TTD,
  test helpers) and of `RasterDescriptor`; findings are in [design.md](design.md).
- Owner decisions: source = presented frame (`source=live` also offered, served on the emulation
  thread); default area = whole frame with border, `area=screen` = the machine's working picture;
  ZX-Poly = the frame the user sees; default format PNG (GIF opt-in); index frames stay on
  `/capture/framebuffer`.

Verified live 2026-10-03 (TSL-VDAC2, R-Type VDAC2 running): `GET /capture/screen?format=png` returns
256x192, `mode=full` and `/capture/framebuffer` return 1024x768. The FT812 default-crop bug is real.

Phase 2 landed on master (cbf214711): `Screenshotter` (`core/src/emulator/video/screenshotter.h/.cpp`:
`Render` / `TakeFrom` / `Take`, strict `ParseArea` / `ParseFormat`, error kinds, unique GIF temp names, file save with
folder creation), `ScreenCapture` is now a shim over it (presented frame, the frame's own window: the FT812 default
crop bug is gone), tests `Screenshotter_Test.*` and `ScreenshotterMachine_Test.*` (18, mutation-checked).
Left in phase 2: `source=live` (the one-shot request served by the emulation thread, design section 6).

Phase 3 (surfaces), verified on a live app 2026-10-03: WebAPI `GET /capture/screen` (area / format / path, `mode` kept as a deprecated alias, strict words, typed statuses, the geometry in the answer; the FT812 default is now the whole 1024x768 picture) and its OpenAPI entry, MCP `capture_media` screenshot (`area`, encoded `path`, the geometry in the summary; `UrlEncodeSegment` moved to `mcp-tool-utils.h`), CLI `capture screen [--area=] [--format=] [file]` (the session's own emulator), Lua `screenshot{}`, Python `capture_screen(format="png", full=None, area="", path="")`, Qt Tools > Save Screenshot As (whole frame, PNG / GIF). `Screenshotter::ParseRequestWords` holds the request-word rules (core-tested; `EmulatorAPI` is not linked into core-tests). Docs: command-interface.md, webapi / cli / lua / python interface docs, CLI and MCP READMEs. Python automation is OFF in this build: `capture_screen` was only syntax-checked against the vendored pybind11 (it compiles; three older warnings in the same header, lines 316 / 2601 / 2604, fail a `-Werror` Python build and are not from this work).

Phase 1 and 2 leftovers, working tree (not committed): marker checks for ATM, Profi, Sprinter; the ZX-Poly composed frame
(`ZXPolyGroup::SnapshotDisplay`, `FrameSource::Composed`, `Screenshotter::SourceName`); `source=live`
(`Screen::SnapshotLive` / `ServeLiveRequest` called from `MainLoop::OnFrameEnd` before the latch,
`Emulator::IsEmulationParked`, `ScreenshotSource`, a `source` word on WebAPI / MCP / CLI / Lua / Python, the beam
position and `partial` of a paused machine; checked live on a running and a paused machine); recording
`MainScreen` takes the frame's own window, locked at the start; docs and recipes for all of it.

Remaining:
- Phases 1-5 of the design (geometry foundation, `Screenshotter`, surfaces, docs and recipes,
  recording/viewport).
- Documentation deliverables of [design.md](design.md): `command-interface.md`, the docs of every
  automation module (WebAPI, CLI, MCP, Lua, Python; check gdb/dezog/zesarux), the OpenAPI
  manifest, recipes.
- Phase 1 foundation landed on master (e7b6f60fd): `PictureGeometry`, `FrameSnapshot`,
  `Screen::SnapshotPresented`, per-slot geometry latch, `Screen::WorkingWindow()` (TS-Conf override);
  tests `FrameGeometry_Test.*` and `ScreenTSConf_Test.GEOM1_*`. Left in phase 1: marker tests for ATM,
  Profi, Sprinter and ZX-Poly (the composed display frame as its own geometry).
- P384 WAS a bug (an earlier note here said the opposite, from a measurement on the batch renderer only):
  the per-T-state renderer (the default) draws the paper at (48,64), the batch one drew it at (48,48), the
  table said 48 and the Qt viewport presets assumed 56. Fixed in the working tree: table row (48,64), batch
  renderer (reads the row), `TransformTstateToFramebufferCoords`, the video mapper (no double +16), the
  viewport presets and their test, the `zxgeometry.h` and `screenzx.cpp` comments. The owner checked the
  overscan picture in the running app.
