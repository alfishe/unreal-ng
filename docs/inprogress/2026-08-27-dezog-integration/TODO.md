# TODO — DeZog integration (2026-08-27)

**Status:** one close-out item remains (the user guide) — the DZRP module,
ZRCP server and verification suite shipped, and the manual end-to-end session
with the real DeZog extension passed on 2026-09-16 ([action-plan.md](action-plan.md)).

## Progress
- Phases 1–4 essentially ☑ in [action-plan.md](action-plan.md): foundation,
  execution control, ZX-specific features (memory banks, TS paging), host
  integration (`dezog-emulator-host 48K` verified), launch.json examples.
- Full doc set: DZRP spec analysis ([dzrp-protocol-spec.md](dzrp-protocol-spec.md)),
  module design, workflows (1089 lines), reverse-debugging plan,
  verification plan, ZRCP server design.
- 48/49 GDB-side hardening items from the 2026-08-26 plan landed alongside.
- 2026-09-16 user-verified: manual end-to-end session from the DeZog VS Code
  extension (zesarux launch mode against the unreal ZRCP/DZRP server) —
  **fully functional, including backward debugging** (the previously-optional
  [reverse-debugging](reverse-debugging.md) scope, formerly gated on TD-1,
  works against TTD sessions as-is).

## Remaining
1. **User guide** — end-user documentation for setting up DeZog + unreal
   (action-plan ☐; `tools/verification/dezog/README.md` currently serves as
   the launch.json reference). The only close-out item left.

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — DeZog completion (T3).
