# Control surfaces: testing

| | |
|---|---|
| **Date** | 2026-10-18 |
| **Status** | Design, for review |
| **Related** | [controller manager testing](../2026-10-17-controller-manager/testing.md), `core/tests/README.md`, [MCP testing guidelines](../../guidelines/mcp-testing-guidelines.md) |

## 1. Unit (GTest)

| Area | Tests |
|:--|:--|
| `ttd.seek_by` curve | rate 0, `rate_lo`, midpoint, `rate_hi`, above; seconds → frames before rounding (0.02 s on Pentagon = 1 frame); fractional delta. |
| Clamp and availability | target below `first` and above `end` clamped, `edge` mark; while recording the recording pauses for browsing and the seek goes on; refused without history; no target past the end ever sent. |
| Timeline marks | second and 10 s crossings forward and back, exactly on a boundary, empty interval, bookmarks, the strongest mark wins; Pentagon and 128K frame periods. |
| `run.step_by` | forward needs a pause; backward needs history; the 16 cap; a breakpoint stops the loop. |
| Registry and worker | order per instance; a delta merges only into the last queued job with equal id, origin, session and params (a `ctx.set` between two deltas stays between them); every merged call answered; gates per origin (implicit session for automation, `CheckAction` for companions, run-control claim for host and GUI); GUI actions absent headless; worker stop answers queued jobs. |
| Context | `ctx.set` posts `NC_SURFACE_CONTEXT`; `SetContext` makes the router pick `@ttd`, `@debug.*` (longest match); a debug profile with `@` layers, `curve` params and per-layer `haptics` parses; other layers still refused. |
| Encoder speed | the bridge's rule on synthetic host times, with BLE-style bunching (two reports 0.1 ms apart, then 30 ms); real intervals come from the POC 030 capture without the repeat filter. |
| Surface Dial driver | every report of the capture fixture parsed (`32 02` ignored); feature and output bytes per request; writer thread keeps only the latest steps request; reapply after reconnect. |
| Published cursor | readers never see a torn value while seeks and frames write it. |

Every test under 50 ms, no `sleep_for` (`TestWait`), scratch paths by `TestPathHelper`.

## 2. Conformance (`tools/surfaces/conformance/`)

Python against a headless instance with `UNREAL_COMPANION_TOKENS`:

- `auth` then `join`; `companion_required` without; `companion_not_for_agents` with an agent token; `companion_unknown` with a revoked token; `companion_forbidden`
  with a token lacking `rewind`; `held_by_session` while a test agent holds a session; `bad_request` cases.
- Record 10 s on a fresh Pentagon, stop; `ttd.seek_by` both ways at low and high rate; check positions, marks,
  `edge` at both ends; readouts follow; `ctx.set` from one session reaches another.

## 3. Integration

- `streamdeck-bridge` e2e against its own instance: page → `ctx`; dial → `ttd.seek_by`; strip from readouts.
- Qt: the Dial's context layer follows debugger focus; the Dial is listed in the controllers dialog in role
  `debugger-remote`.

## 4. Live (owner's hardware)

| Check | How |
|:--|:--|
| POC 030 A2-A7 | one change at a time, felt by the owner, bytes recorded |
| ACC-1 | fresh Pentagon instance, 3-minute recording, 60 s of scrubbing with the Dial |
| ACC-2 | Stream Deck page TTD → Time code while turning the Dial; the strip and the last result agree |
| Sleep | Dial left 10 min, touched: steps and clicks reapplied |

Only own instances are driven; the owner's GUI instance is never a test target.
