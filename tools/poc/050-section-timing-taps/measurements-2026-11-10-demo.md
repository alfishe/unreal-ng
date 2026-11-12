# 050 — measurements 2026-11-10: the section-timing demo

## Setup

- macOS arm64 (M-series, 20 cores), unreal-qt Release of github/master `df480185f`, offscreen, muted, WebAPI on port
  28700 (`python/launch.py`).
- The demo of `data/examples/01-section-timing` (`demo.sna`, `demo.sym`): sections Frame, Plasma, Sparkles, Music,
  Isr; IM 2 through the `JP` at `#FDFD`, the handler ends with `EI; RETI` (`Isr.End`).
- Each run: a new instance, the demo loaded with its labels, 50 frames run paused, then the variant
  (`python/run_taps.py`). Tap variants run with `POST /run_frames` (full host speed, no stop).
- The dense marker is `Plasma.cell` (558 hits a frame); `Sparkles.inside` (12 a frame) in the first cost run.
- The host was shared and loaded: load average 60-160 on 20 cores during the gap runs (other sessions' builds and
  emulators). Wall times spread by up to 2x; the comparisons that decide are within interleaved rounds, and the
  per-call times come from the emulator's own callback statistics (`total_us / calls`, measured on the emulation
  thread).
- Raw data: `scratch/2026-11-10-poc-050/` of the worktree (`r1` exactness, `bench`, `bench2`, `bench3`, `matrix`,
  `ab`).

## Exactness (G1)

200 frames, the passes of every section compared as (section, frame, T at entry, gross, net, interrupts) with
`python/compare.py`.

| Model | Reference passes | `b-hook-sync` | `c-hook-frame` | `d-deferred` |
|:--|--:|:--|:--|:--|
| 48K | 814 | 814 equal | 0 equal at first, 814 equal with the hits sorted | 19 equal at first, 814 equal with the hits sorted |
| Pentagon | 832 | 832 equal (+3 passes ending after the reference's last frame) | 832 equal, sorted | 832 equal, sorted |

- `sync` gets the hits in machine order and matches pass for pass at once, interrupts and net time included.
- `frame` and `deferred` first got everything wrong: every tap has its own callback and ring, and the rings are
  drained one callback after another, so the hits of different markers arrive out of machine order (an exit before
  its entry). Kept and sorted by global T before use, they match too.
- The interrupt return is computed, not seen: the hook at the handler's return instruction (`RETI` at `Isr.End`,
  same SP as at the landing) adds the instruction's length (14 T). The reference sees the return with a breakpoint
  set at run time on the interrupted address. The same numbers come out here because the handler, its stack and
  the `RETI` are in uncontended memory.
- The patched builds (below) gave the same 814 passes in all six configurations.

Not run: the IM 1 program with the ROM's handler. The method needs the handler's exits as addresses (the ROM's
`EI; RET`); see the gaps in [analysis.md](analysis.md).

## Speed of a run against the reference (G2)

| Model | Reference: wall for 200 frames | Stops | Per stop | `b-hook-sync`: wall | Gain |
|:--|--:|--:|--:|--:|--:|
| 48K | 6.33 s | 1892 | 3.35 ms | 0.100 s | 63x |
| Pentagon | 6.14 s | 1924 | 3.19 ms | 0.108 s | 57x |

## Cost on master (G2), before the load rose

`bench2`: 1000 frames, 5 interleaved rounds, medians; spreads within 10 % but one outlier.

| Variant | Demo markers (9.3 hits / frame): frames/s | Over no taps, µs / frame | Dense (558 hits / frame): frames/s | Over no taps, µs / frame | Over no taps, per hit |
|:--|--:|--:|--:|--:|--:|
| `none` | 3643 | 0 | 3700 | 0 | - |
| `e-count-floor` | 2141 | 193 | 2151 | 195 | 0.35 µs (all fixed cost) |
| `sync-empty` (an empty sync hook) | 2293 | 162 | 435 | 2029 | 3.6 µs |
| `b-hook-sync` | 1824 | 274 | 118 | 8214 | 14.7 µs |
| `deferred-empty` | 2122 | 197 | 2090 | 208 | 0.37 µs |
| `d-deferred` | 2149 | 191 | 2153 | 194 | 0.35 µs, but the dense tap suspended (below) |

- Any exec tap costs about 190 µs a frame on this demo, with no hook and whatever the hit count: 1.7x slower.
- An empty sync hook costs 3.3 µs a call over that; the timer's own code another 11 µs a call.
- `c-hook-frame` (first cost run, `bench`): 601 frames/s on the demo markers, 254 dense. It keeps every hit until
  the end to sort them, so its state grows with the run: no use without a flush per frame, which the frame mode
  cannot order across callbacks.
- `d-deferred` dense: the dense tap's callback was **suspended** by the budget policy ("3 calls over 1000 us within
  50 frames") after 60 160 calls, 120 320 hits dropped. The demo's own taps stayed active, so the passes were still
  right. The script thread cannot take more than a few hundred thousand hits a second.

## The gaps, measured (G3)

Two causes were found and tried as temporary patches (`gap-experiments.patch`, switched by environment variables,
not on master):

1. **A GC step per hook call.** `LuaHookHost::Call` ends with `lua_gc(L, LUA_GCSTEP, 0)`. A hook that keeps state
   (the timer keeps its open passes and the passes) makes each call's step walk a growing heap. `sync-grow` (one
   two-number table kept per call) on master: 44 µs a call after 4700 calls; dense, 500 frames did not finish in
   60 s. `UNREAL_POC050_HOOK_GC=none` drops the step, `gen` drops it and switches the hook state to the generational
   collector.
2. **No HALT idle while an exec tap is armed.** `Z80::IdleStepsInert` refuses the halted idle whenever
   `execTapHold` is set, even when no tap is at the HALT's address, so a program waiting in HALT runs its HALT
   instruction by instruction. `UNREAL_POC050_TAP_IDLE=1` refuses it only when a tap is at that address (the check
   debug mode already makes, `ExecIsInertAt`).

Time per hook call from the callback statistics (`matrix`, 500 frames, 3 rounds per cell, medians; loaded host, so
higher than the quiet `bench2` figures):

| Hook GC | `sync-empty` demo | `sync-empty` dense | `b-hook-sync` demo | `b-hook-sync` dense |
|:--|--:|--:|--:|--:|
| `step` (master) | 23.9 µs | 16.0 µs | 57.6 µs | 50.8 µs |
| `none` | 5.0 µs | 4.4 µs | 11.7 µs | 5.1 µs |
| `gen` | 2.3 µs | 2.5 µs | 4.0 µs | 2.4 µs |

Master against both patches (`gen` and the HALT idle), interleaved per round, 7 rounds, 1000 frames, medians
(`ab`, load average 80-130):

| Variant | Master, demo | Patched, demo | Master, dense | Patched, dense |
|:--|--:|--:|--:|--:|
| `none` | 1225 frames/s | 1278 | 1168 | 1506 |
| `e-count-floor` | 878 | 1064 | 804 | 1075 |
| `b-hook-sync` | 612 | 1310 (as `none`) | 38 | 303 (8x) |

- With both patches, the timer on the demo's markers costs nothing measurable: the HALT idle it gets back pays for
  its hooks.
- Dense, the patched hook costs about 4.7 µs a hit on this loaded host (2.4 µs a call in the statistics); 558 hits a
  frame are then about 1.3-2.6 ms a frame: a 50 Hz machine still runs far above real time.
- `e-count-floor` keeps a cost in both: the driver switched the `breakpoints` feature on for its count-only
  breakpoints (as the reference script does), which puts the machine in debug mode (the memory debug paths). The
  callbacks' taps run without it. So `e` is not the floor it was meant to be; `sync-empty` with both patches is.

## The native analyzer against the Lua timer (after the fixes landed)

`native`: the `section-timer` analyzer (`core/src/debugger/analyzers/sectiontimer/`, POC branch only): the same
markers as exec taps of its own sink, the interrupts from the CPU's `int_accept` / `int_return` events (master
`5c2351274`), so no handler exit is given. `sync`: the Lua timer on master with both gap fixes (`a6f99c20d`).
`python/native.sh`; raw data `scratch/2026-11-10-poc-050/native/`. Load average 110-220 during the runs.

Exactness, 200 frames, against the reference:

| Model | Reference | `sync` | `native` |
|:--|--:|:--|:--|
| 48K | 814 | 814 equal | 814 equal |
| Pentagon | 832 | 832 equal (+3 after the reference's end: the dump's 2 extra frames) | 832 equal |

Cost, 1000 frames, 5 interleaved rounds, medians (walls spread up to 2x with the load):

| Variant | Demo markers (9.3 hits / frame): frames/s | Dense (558 hits / frame): frames/s |
|:--|--:|--:|
| `none` | 1196 | 1113 |
| `sync` | 1158 | 302 |
| `native` | 1266 | 1408 |

- `native` is within the noise of no taps on both: the demo's 9 hits a frame and the dense marker's 558 cost it
  nothing this host can measure (the medians are below `none`'s, the spreads overlap).
- `sync` is within the noise on the demo and 3.7x slower than no taps on the dense marker (about 4.3 µs a hit on
  this loaded host, 2.4 µs in the hook statistics of the earlier runs): `native` is 4.7x faster there.
