# 050 — section-timing-taps

Feature: section timing, the "time between two markers" request (min / avg / max T-states per pass, the interrupts
inside left out, a pause when a pass is too long). Today it is the WebAPI script `data/examples/01-section-timing`
(`profile_sections.py`): the machine stays paused, `POST /steps` runs it to the next marker, `GET /debug/snapshot`
reads the clock. Exact, but every marker hit is two HTTP requests.

The open decision (owner, 2026-10-11): a native `section-timer` analyzer in the core, or the breakpoint machinery
that exists by now: exec taps (breakpoints that never stop), conditions, and Lua hooks that run on the emulation
thread ([script-callbacks.md](../../../docs/features/script-callbacks.md)). This POC measures the second against the
first before anything is built.

## Goals

1. **G1: exact on taps.** Can exec taps on the markers plus a Lua hook compute the same passes as the reference script,
   pass by pass: gross T-states, nesting, interrupts inside a pass and the net time, with the machine never stopped?
2. **G2: cost.** What does one marker hit cost on the emulation thread in each tap variant, and what does a run cost
   end to end (emulated frames per second at full speed) against the reference and against a machine with no taps?
3. **G3: gaps.** What do the taps and hooks lack for a universal section timer (the return from an interrupt, an
   interrupt handler ending with `RET`, results out of the hook state, a pause on a slow pass), and is closing those
   gaps smaller than a native analyzer?

## Variants

| Variant | How |
|:--|:--|
| `a-webapi-steps` | the reference: `profile_sections.py` as it is on master |
| `b-hook-sync` | exec taps at the entries, exits, the interrupt landing and the handler's exits; one Lua chunk in `sync` mode keeps the open passes and the statistics in the hook state, `shared_set` publishes them |
| `c-hook-frame` | the same chunk in `frame` mode: the frame's hits at the frame end, only the hits' `t` (no live clock) |
| `d-deferred` | a deferred Lua function (the script's interpreter thread) gets the hits from the ring: `frame`, `t`, `regs` |
| `e-count-floor` | the same taps as count-only breakpoints (`stop: false`, no sink): what the tap matching costs alone, the floor of any tap-based timer and a lower bound of a native one |

## Requirements

- The demo of `data/examples/01-section-timing` (sections Frame, Plasma, Sparkles, Music, Isr; IM 2, `RETI`) on a 48K
  and a Pentagon, 200 frames; a dense marker (`Sparkles.inside`, hundreds of hits per frame) for the cost per hit.
- An IM 1 program whose handler is the ROM's (`#0038`, ends with `EI; RET`), to see what the taps miss there.
- unreal-qt Release on macOS arm64, headless (`QT_QPA_PLATFORM=offscreen`), driven over the WebAPI and `POST
  /lua/exec`, muted.
- Distributions where it matters (per-frame hook time p50 / max), interleaved rounds against the no-tap run.

## Constraints

- No change to `core/` for the measurements; a gap found is written down with what closing it would take.
- The machine is shared: interleaved rounds, medians.
- POC code (Python driver, Lua chunks) lives here; raw data in `scratch/2026-11-10-poc-050/` of the worktree.

## Acceptance criteria / expected results

| Question | Expected | Decides |
|:--|:--|:--|
| `b-hook-sync` passes against `a-webapi-steps` | the same list of passes: gross, net, interrupts, on 48K and Pentagon | taps can replace the stepping script |
| `c-hook-frame`, `d-deferred` | gross exact; the interrupt return not exact (no live clock after the `RETI`) | which mode a timer must use |
| run speed, demo (≈ 8 hits per frame), `b` | within 5 % of no taps | the timer can stay on while the program runs |
| one hit, `b` | ≤ 2 µs (a Lua call of a few table operations) | dense markers are affordable |
| one hit, `e` | ≤ 50 ns | the floor; a native timer would gain at most `b` − `e` per hit |
| run speed, `a` | ≈ 2-8 s per 100 frames (README limits) | the gain of `b` over today |
| gaps | listed with sizes | native analyzer vs closing the gaps |

**Decision rule.** If `b-hook-sync` is exact and its per-hit cost keeps a dense marker under a few percent of a frame,
section timing ships as a tap-based tool (a Lua library and the scripts) and the native analyzer is dropped, with the
gaps of G3 closed in the tap machinery where every tool gains from them. If it is not exact, or the per-hit cost is
far over the budget, the native analyzer is designed with the numbers of `e` as its target.
