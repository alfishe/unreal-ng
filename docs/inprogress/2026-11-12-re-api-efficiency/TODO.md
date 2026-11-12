# TODO — RE agents vs MCP / WebAPI efficiency (2026-11-12)

**Status:** analysis done, no proposal started. PLAN row **#122** (T2).

## Documents
- [analysis.md](analysis.md) — every fault, slow path, missing verb and ergonomics gap the reverse-engineering agents hit, with evidence in the reverse cases repo, the master code places, and proposals (§6 summary table).

## Proposals, in priority order (none started)

### P1
- [ ] F1: no silent clamp in `run_frames` / `/steps` / `run_tstates`; answers carry `frames_run`, `frame`, `t`, `stop`; MCP text from the answer (S).
- [ ] F2: `RunNFrames` aims at an absolute frame boundary; split-independent runs, with a test (M).
- [ ] F3: TTD dump failure reasons; chunk records over 1 GiB; partial file handling (M).
- [ ] F4: analysis of sessions recorded during RZX playback (bundle carries the RZX; PLAN #27 R5) (M).
- [ ] S1: `memory_series` verb (frame range / step, several ranges, `changes_only`, base64) (M).

### P2
- [ ] F5: tape play after `$2A` / pause; play answer reports state and block; `$2A` 48K decision; last-block edge (M).
- [ ] F6: `find_event` exec sentinel hits (S).
- [ ] S2: job model / long-poll for long verbs; clients drop `sleep(5)` (M).
- [ ] S3: server-side input script player with TTD recording (M).
- [ ] V1: L1 over several sessions (M).
- [ ] V2: `screen_at` (shown / normal / shadow) at a past frame (S).
- [ ] V3: lab machine preset in one call; `keys set` (M).
- [ ] E1: unknown action / value lists the valid ones, "did you mean" (S).
- [ ] E2: features in MCP (`kempstonmouse` off), mouse / Kempston notes in tool descriptions, Bumpy retry (S).

### P3
- [ ] F7: GS COMRG value 0; NeoGS / GS in SZX (verify first) (M).
- [ ] F8: port history flush and frame counter (verify first) (S).
- [ ] F9: L1 `root` at a snapshot's first PC (verify first) (S).
- [ ] S4: history / trace filters, event-only trace (M).
- [ ] V4: counting breakpoints with per-hit frames (with PLAN #6) (M).
- [ ] V5: L1 first / last frame per instruction; reader-PC filter (S).
- [ ] V6: GS firmware state view (M).
- [ ] V7: `--port-base auto` and a ports file (S).
- [ ] E3: `frame` in `/registers` and run answers (S).
- [ ] E4: `memory_at` states the moment it read (S).
- [ ] E5: `capture.json` script start frame (S).
- [ ] E6: key script error explains the separator (S).
- [ ] E7: RZX seek discoverability (S).
- [ ] E8: capability check before a session (S).
