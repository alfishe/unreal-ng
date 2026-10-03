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

Remaining:
- Phases 1-5 of the design (geometry foundation, `Screenshotter`, surfaces, docs and recipes,
  recording/viewport).
- Documentation deliverables of [design.md](design.md): `command-interface.md`, the docs of every
  automation module (WebAPI, CLI, MCP, Lua, Python; check gdb/dezog/zesarux), the OpenAPI
  manifest, recipes.
- Phase 1 foundation landed in the working tree (not committed): `PictureGeometry`, `FrameSnapshot`,
  `Screen::SnapshotPresented`, per-slot geometry latch, `Screen::WorkingWindow()` (TS-Conf override);
  tests `FrameGeometry_Test.*` and `ScreenTSConf_Test.GEOM1_*`. Left in phase 1: marker tests for ATM,
  Profi, Sprinter and ZX-Poly (the composed display frame as its own geometry).
- P384 is NOT a bug: measured, the table row (paper at 48,48) is right. The survey's claim and the
  comment in `zxgeometry.h` ("the paper is 16 lines lower") are wrong; that comment should be corrected.
  The Qt viewport presets (paper y=56) do not match the measured 48: noted, not in scope.
