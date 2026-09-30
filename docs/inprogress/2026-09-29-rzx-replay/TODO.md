# TODO — RZX replay integration (2026-09-29)

**Status:** R0-R3 (playback) done 2026-09-29; R4 (recording) in progress separately; R5 (TTD interop) possibly later, low priority. PLAN row **#27** (T2).

## Documents
- [requirements.md](requirements.md) — why a loader is not enough (four run-time mechanisms), scope, functional (RZ-F1…F21), non-functional (RZ-N1 zero cost when off … RZ-N7), automation / UI, tests and acceptance.
- [design.md](design.md) — the design, the decisions (§17) and what was built (§18 As built).
- [step-work-gate.md](step-work-gate.md) — how RZX sits on master's per-step gate (applied).

## Done
- [x] §17 decisions (recommended options; miniz shared with SZX).
- [x] R0: `RzxReader` / `rzx::File`, bounded inflate, parser tests and fuzzing.
- [x] R1: player on the per-step gate (`kStepWorkRzx`), `IN` substitution, fetch count from R, forced interrupts, strict / tolerant desync, shortcut and live-input locks, start snapshot of every format (SZX included), model check.
- [x] R2: `RzxLauncher` (model switch), WebAPI + OpenAPI, MCP `rzx_playback`, CLI `rzx`, Lua, Python, Qt (open, stop, status bar), `NC_RZX_PLAYBACK`, docs, recipe `.recipe/media/play-rzx.md`.
- [x] Seek: keyframes owned by the player (budget, thinning, freed on stop), `SeekRzx` on every surface, Qt popover on the status bar label (user request).
- [x] Qt status bar `TTD nn%` while positioned in TTD history (user request).
- [x] R3: the SNA and Z80 loaders read from memory too (`Emulator::LoadSnapshotData`, `ApplySnapshotData`); start snapshots without a temporary file; snapshot blocks between input blocks applied at the frame boundary (RZ-F9), checked against SkoolKit on a two-block recording (`cases/multiload-join.rzx`, `rzxjoin.py`).
- [x] Fixtures `testdata/loaders/rzx` (6 archive recordings, SkoolKit expected states, external snapshot, MEMPTR case); tools `tools/verification/rzx`.

## Next
- [ ] Benchmark gate (RZ-N1 zero cost when off, RZ-N2 overhead while playing) - measured when the shared machine is quiet (result goes to design §18).
- [ ] R4: recording (another agent).
- [ ] R5: TTD interop (player state as a TTD blob, TTD while playing, import, export) - possibly later, low priority.
- [ ] More real recordings for +3 and Scorpion (none found yet; RZX Archive reachable only through the Internet Archive, rate-limited).

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — #27, #64, #40.
- SZX: [../2026-09-29-szx-snapshots/](../2026-09-29-szx-snapshots/TODO.md).
- TTD port journals: [ttd-port-read-journal.md](../../emulator/design/debugger/time-travel-debug/ttd-port-read-journal.md).
- Research: `docs/inprogress/2026-09-28-debugger-family/rzx-ttd.md` (not yet in master).
