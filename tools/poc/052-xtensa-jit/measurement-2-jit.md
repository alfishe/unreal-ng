# POC 052 - measurement 2: block JIT and region JIT (arm64)

Host: M3 Ultra (.20), release build, `TSL-VDAC4`, headless unreal-qt driven over WebAPI, muted. DOOM-VDAC4 1.1 demo
loop (6000 warm-up frames, 600 timed, picture hash at the end); R-Type VDAC4 1.0 attract loop (8 pictures, one
every 1500 frames, 12000 frames timed). Interleaved rounds, interpreter (`UNREAL_XTENSA_JIT=0`) and JIT
(`UNREAL_XTENSA_JIT=1`) from the same binary.

## Variants

| Variant | What runs natively |
|---------|--------------------|
| a-interp | nothing: the predecoded-block interpreter (computed goto) |
| b-jit-blocks | a native prefix of one interpreter block (ALU, shifts, multiplies, loads / stores with a fast-path page check, branches, calls); back to `Exec` after every block |
| b2-jit-regions | a region: every block reachable from a hot block by direct branches, jumps and fall-throughs, up to calls, returns and window changes. Native jumps between its blocks, guest registers in host registers across the region, the budget in a register, single-precision float with the interpreter's NaN rules, zero-overhead loops (`LOOP*`, the loop-back check at LEND) |

Every variant is bit-exact against the interpreter: `XtensaJit_Test.RandomProgramsMatchTheInterpreter` (600 random
programs x 40 runs with random budgets, float registers with NaNs and infinities, live loops; state, memory and run
result compared after every run) and the 23 `XtensaCpu` tests with a region compiled at every first entry.

## Results

| Variant | DOOM ms/frame | R-Type ms/frame | Pictures | Native share (DOOM) |
|---------|---------------|-----------------|----------|---------------------|
| a-interp | 3.67 | 3.41 | reference | 0 |
| b-jit-blocks | 3.20 (-13 %) | 3.03-3.40 (-12 %) | identical | - |
| b2-jit-regions, no loops | 3.28 (-11 %) | 2.86 (-16 %) | identical | 36.6 % |
| b2-jit-regions with loops | **2.88 (-22 %)** | **2.73 (-20 %)** | identical | **91.0 %** |

Region entries refused in the run without loops (DOOM, 600 frames after warm-up): 222 M for a live loop end inside
the region against 43 M native entries. With loops: 23 M refused, 91 % of all guest instructions run natively.

## Where the time goes now (DOOM, 3000 frames sampled, busy ms)

| Thread | interpreter | b2 with loops |
|--------|-------------|---------------|
| emulation (Z80, TS-Conf arbiter, interrupts) | 5545 | 4997 |
| ESP worker | 6662 | 3118 (native code 816, `Exec` 491, FT812 bus ~1000) |
| FT812 renderer | 3440 | 3319 |

The ESP worker is no longer the longest thread: the emulation thread is, and the threads still wait for each other
(a frame takes longer than any thread is busy). A faster JIT alone moves the frame little from here.

## Notes

- The first run after switching games (DOOM -> R-Type, or the first run after a build) gives other pictures than the
  following runs, with the interpreter too: unreal-qt keeps some state across launches. Comparisons use the runs
  after the first one.
- Left to the interpreter: `ENTRY` / `RETW` / `CALLn` boundaries (a region ends there), special registers, divisions,
  `CLAMPS` / `NSA`, MAC16, float compares into `MOVF.S`-style moves, the window check (a region is not entered
  when its register groups are not all free: 0.2 M entries).
