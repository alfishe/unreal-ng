# 02 · Constant time: does every path take exactly N T-states

The code in this example must take the same number of T-states on every path, whatever data it gets. Examples of
such code: a multicolour engine, a sample player, a border effect, a dispatcher whose handlers have to fit one slot.
Your table of instruction timings says 354. A typo in the code, or a branch nobody balanced, makes some passes
longer or shorter, and you only notice it as a glitch on screen.

Mark the entry and the exit and say what the time must be. Every pass that differs is listed, together with the
register that chose its path. A wrong handler then shows up as "every pass through it is off", and a wrong branch
as "off only with this data".

| File | What it is |
|:--|:--|
| `constant.asm` | The program. `Dispatch` is the entry: `EX AF,AF' : ADD A,B : EXX : JR C,... : EX AF,AF' : JP (IY)` into one of eight handlers H0..H7 of mixed code, then `JP Dispatch.End`. The exit is `Dispatch.End: EXX`. Every path is counted to 354 T-states in the comments; 8 dispatches per frame, interrupts off. |
| `constant.sna`, `.sym` | The default build. It has two timing bugs (see below). |
| `constant-fixed.sna`, `.sym` | The build with `-DFIXED`: both bugs corrected. |
| `check.py` | Runs `../01-section-timing/profile_sections.py` with this example's options. |
| `build.sh` | Rebuilds both programs with sjasmplus. |

## Run it

Start unreal-qt (the script finds it, or waits for it), then:

```bash
python3 check.py --fixed               # 400 passes, every one 354
python3 check.py                       # the buggy build: what is off, and where
python3 check.py --break-on-mismatch   # stop at the first pass that is off
```

`check.py` runs the generic tool with these options. Use the same options for your own code:

```bash
python3 ../01-section-timing/profile_sections.py --model PENTAGON \
    --snapshot constant.sna --sym constant.sym \
    --section Dispatch --expect Dispatch=354 \
    --group-by iy --show-regs a_,b --frames 400 --passes 400
```

| Option | Here |
|:--|:--|
| `--section Dispatch` | Entry at label `Dispatch`, exit at `Dispatch.End`. For other labels: `--section name=ENTRY:EXIT`. |
| `--expect Dispatch=354` | Every pass must take 354 T-states, interrupts left out (net). The report lists the passes that do not. |
| `--group-by iy` | Notes IY at every entry, i.e. which handler runs, and groups the passes by it. Values that match a label show its name. |
| `--show-regs a_,b` | Notes A' and B at the entry and shows them next to every pass that is off. These are the data the JR C depends on. |
| `--break-on-mismatch` | Pauses at the exit of the first pass that is off. The registers in the debugger are then those of that pass. |
| `--model PENTAGON` | No contended memory, so the time depends on the code alone. On a 48K the same check works for code outside `#4000`-`#7FFF`. |

## What the buggy build shows

From `constant-report.md` after `python3 check.py` (400 passes):

```text
Dispatch: 228 pass(es) did not take 354 T-states (-5: 187, -2: 24, +3: 17)

| IY         | Passes | Min | Max | Different |
| #805D (H0) |     51 | 349 | 354 |        24 |
| ...        |        |     |     |           |
| #8102 (H5) |     41 | 352 | 357 |        41 |
| #8122 (H6) |     53 | 349 | 354 |        31 |
```

- **H5 is off on every pass.** It is either 3 T-states long (357) or 2 short (352), so the bug is inside H5. There,
  `LD B,(HL)` takes 7 T-states where the timing table counted the 4 of `LD B,A`.
- **Every handler is off by −5 on about half of its passes.** In the pass list, A' + B never carries on those
  passes, so the bug is in the prologue, on the no-carry path. That path lost its `RET C`: 5 T-states that are never
  taken and exist only to balance `JR C` taken (12) against not taken (7).
- **H5 without a carry** is +3 − 5 = −2, which is the third value in the list.

`python3 check.py --fixed` gives `400 passes, 354 .. 354, 0 off`.

## Limits

- **Speed.** Two HTTP requests per marker, so the example measures about 400 passes in a few seconds. For code that
  runs thousands of times per frame, measure a sample of passes (`--passes`), not every one.
- **Interrupts.** This program runs the timed code with interrupts off. With them on, an interrupt that lands in a
  pass makes it longer, and the *Interrupts* column counts it; `--expect` compares the net time, from which the
  interrupt is taken out exactly (acknowledge, vector, handler, `RETI`), so an interrupted pass of correct code is
  still 354 and is not reported.
