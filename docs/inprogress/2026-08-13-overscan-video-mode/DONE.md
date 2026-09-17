# DONE — Overscan video mode (2026-08-13)

**Status:** complete.

## What landed
- Pentagon overscan mode (384×304, extra border) per the technical design, plus the
  Xpeccy-vs-unreal rendering comparison that settled the geometry.

## Evidence
- `Emulator::SetOverscanMode/IsOverscanMode` (`core/src/emulator/emulator.cpp:3059+`),
  `Screen::SetOverscanForced`, feature `overscan`/`osc` in `FeatureManager`
  (`core/src/base/featuremanager.h:34`).

## Follow-ups
- None.
