# TODO — HUD layer (2026-09-07)

**Status:** partially done — r1 shipped in full under `unreal-qt/src/hud/`;
Phase 4 (detach into core + automation surface) remains.

## Progress
- **r1 complete:** HudModel/layout/animator/theme/compositor with SIMD blend
  tiers under `hud/core/` (Qt-free, enforced by a Qt-less unit-test target),
  Qt overlay/host/settings under `hud/qt/` — 18 files, all landed.
- Three compositing paths, feature flag `hud`, themes/reduced-motion per
  [design.md](design.md)/[rendering.md](rendering.md).
- TSFM/GS audio-activity notifications wired through the model (2026-09
  commits).

## Remaining
1. **Phase 4: detachment** — the exact move-only recipe is §4 of
  [README.md](README.md): move `hud/core/` → `core/src/presentation/hud/`,
  `EmulatorContext` ownership, `NC_HUD_CHANGED` wake-up, tests to
  `core/tests/presentation/`.
2. **Automation surface** — `api.md` §6 (`hud notify`/`hud state`, WebAPI
  `/hud/...`, Lua/Python/MCP) — deliberately deferred until the model lives
  in core.
3. Second-client validation (SDL3/screen-viewer) per README §5 — optional.

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — HUD Phase 4 (T4).
