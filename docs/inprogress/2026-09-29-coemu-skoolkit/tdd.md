# SkoolKit co-emulation runner: technical design

**Date:** 2026-09-29 · **Requirements:** [requirements.md](requirements.md) · **Status:** [DONE.md](DONE.md)

## 1. Flow

```
run.sh <machines>
  coemu_init skoolkit            common/common.sh: machines, PROGRAM_*, OUT, MAX_FRAMES
  find a Python with skoolkit    SKOOLKIT_PYTHON, python3, the interpreter of tap2sna.py
  per machine:
    48k / 128k -> skoolkit-run.py --machine 48|128 --tap ... --done/--start/--end ... --out $OUT/<m>
                  1. load: tap2sna (simulated LOAD "", ROM loader) until PC = START
                  2. run:  contention simulator, interrupts on, until memory[DONE] = 1
                  3. write <m>.bin, <m>.screen.txt, <m>.log
                  coemu_compare <m>
    others     -> coemu_result <m> skipped "SkoolKit simulates only the 48K and the 128K"
  coemu_finish
```

## 2. Loading (R4)

`skoolkit.tap2sna.main()` with `-c machine=48|128 -s <START> <tap> <tmp>.szx`. The simulated LOAD types the
loader (`LOAD ""` on the 48K; the 128K menu's tape loader on the 128K), plays the tape through the ROM's
LD-BYTES and stops when the program counter reaches `START`, the address the BASIC loader's
`RANDOMIZE USR` jumps to. The snapshot then holds the machine exactly as the program would find it on a
real machine: BASIC's system variables, the 128K's `BANK_M`, interrupt mode 1 enabled.

Loading uses SkoolKit's defaults (fast loading of ROM-loader blocks, no contention while loading). The
probe measures only what happens after it starts, so how long the load took does not matter.

## 3. Running (R5)

SkoolKit's `trace.py` stops on a T-state count, an instruction count or one program-counter address. None
of them means "the `DONE` byte became 1": the probe returns to BASIC through a `RET` whose target is the ROM's
`STACK-BC`, which the probe also calls to print numbers.

The driver therefore uses SkoolKit's Python API, the same classes `trace.py` uses:

- `Snapshot.get()` reads the snapshot; `simutils.from_snapshot()` builds the simulator, the C one
  (`CCMIOSimulator`) when SkoolKit's C extension is present, else the Python `CMIOSimulator`;
- `trace.Tracer` supplies port reads and writes (the 128K's `#7FFD` paging, the AY, `#FE`);
- `Tracer.run()` runs 50 frames at a time, interrupts on; between the slices the driver reads `DONE`.

A slice ends on a T-state count, and the next one starts at the program counter where the last one
stopped, with all state kept in the simulator. So the slicing does not change what the program sees: two
runs give the same dump (AC5), whatever the slice size. After `DONE` the driver runs 50 more frames, as the
unreal-ng runner does, so the closing lines are on the screen.

The API is internal to SkoolKit. The driver checks `skoolkit.VERSION` against the versions it was tested
with (10.1) and the major version it accepts (10); anything else is an `error` naming the version (R7).

## 4. Output (R6)

- `<m>.bin`: `memory[START..PROBEEND)`, read through the simulator's memory object (paged on the 128K).
- `<m>.screen.txt`: the screen read back as text. Each 8x8 cell is compared with the character set the
  ROM prints with (`CHARS` + 256, normally `#3D00`), plain and inverse; anything else becomes a space. 24
  lines of 32 characters.
- `<m>.log`: SkoolKit version, simulator class, machine, frames to `DONE`, seconds.

## 5. Discovery (R2)

1. `SKOOLKIT_PYTHON`, if set: used as given; if it cannot `import skoolkit`, every machine is `skipped`.
2. `python3`, if it can `import skoolkit`.
3. `tap2sna.py` on `PATH`: its `#!` line names the interpreter (a `pip install --user` or a virtual
   environment).

The README tells how to install it into a virtual environment under `tools/verification/coemu/skoolkit/build/`
(ignored by git), and `SKOOLKIT_PYTHON` points at it.

## 6. What SkoolKit is expected to disagree on

From a first run (2026-09-29, SkoolKit 10.1): on both machines only the floating-bus check P-02 differs.
SkoolKit returns `#FF` for an unused port at any time; a real 48K / 128K returns the byte the ULA is
fetching during the picture (Butler tests 36 / 37, Rak, FUSE, ZXMAK2, MAME). Everything else, including the
disputed P-05 (a port whose high byte points at an odd page at `#C000` on the 128K), matches the expected
values, so SkoolKit sides with FUSE and unreal-ng on P-05.

## 7. Tests

| What | How |
|:--|:--|
| AC1, AC4 | `run.sh 48k 128k`; `run-all.sh 48k 128k` |
| AC2 | `SKOOLKIT_PYTHON=/nonexistent run.sh 48k; echo $?` = 3 |
| AC3 | `run.sh plus3 pentagon`; `run.sh` |
| AC5 | two runs, `cmp` of the dumps |
| AC6 | `time run.sh 48k 128k` |
