# DONE — CPU optimization proposals & feature toggles (2026-01-08)

**Status:** complete — superseded/absorbed by the [2026-01-10](../2026-01-10-performance-optimizations/) and
[2026-01-11](../2026-01-11-performance-optimizations/) performance programs.

## What landed
- Feature-toggle infrastructure proposed here (`tdd-feature-toggles.md`) became the standard
  gating mechanism: `FeatureManager` + `features.ini` + runtime toggle on every surface.
- CPU optimization proposals and profiler analysis fed phases 1–5 of the January program.

## Evidence
- `core/src/base/featuremanager.{h,cpp}`, `features.ini` at repo root, `/features` API
  (`core/automation/webapi/src/api/features_api.cpp`).

## Follow-ups
- None. Later performance work is tracked in the 2026-01-10/01-11 folders (also done).
