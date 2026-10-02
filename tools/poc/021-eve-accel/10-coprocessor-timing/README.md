# 10 - Coprocessor commands: executed natively, charged real time (study)

Feeds: **OPTIMAL** (point c: the coprocessor's commands run as native code, as eve-emu
does now, but take about as long as on the chip).

No new measurement is possible here (no VDAC2 board on this machine): this is a reading of
eve-emu's code and documentation, and a measurement plan for the board.

## Goal

Describe what eve-emu already models of the time coprocessor commands take, what is
missing for software that depends on that time, and what has to be measured on a card.

## Why it matters

Software on the host waits for the coprocessor: it polls `REG_CMD_READ` /
`REG_CMDB_SPACE`, waits for `INT_CMDEMPTY`, or reads `REG_CLOCK` around a command (Zuma's
profiler does). If commands complete instantly, such software runs faster than on the card,
loading screens are shorter, and anything timed against the coprocessor (streaming data
through `CMD_INFLATE` while the picture runs, for example) behaves differently. TS-Labs'
position: a full timing model of the chip and coprocessor is overshoot; software must hit
its own timings, frame-level timing is enough.

## What eve-emu models today

From `src/eve-copro*.cpp`, `src/eve-tunables.h` and the behavior spec
(`docs/inprogress/2026-10-01-tsconf-vdac2/ft812-behavior-spec.md` §7.2):

| Aspect | Status |
|---|---|
| The mechanism | complete: every command is planned as steps, each step has a cost in system clocks (`StepPlan{units, cost}`); the coprocessor stalls for that many clocks before the step is applied (`CoproState.stall`), interleaved with the scan by `EveAdvance`; `EveClocksToNextEvent` includes the coprocessor |
| Ring consumption | modeled: `REG_CMD_READ` advances as words are consumed, including the data words of a command still running (needed for `CMD_INFLATE` / `CMD_LOADIMAGE` / `CMD_MEMWRITE` longer than the ring) |
| Waiting commands | modeled: `CMD_DLSTART` waits for a pending swap; `CMD_SWAP` does not wait |
| Durations | **all zero**: `kCostCommand`, `kCostDisplayListWord`, `kCostMemoryPerByte`, `kCostMemcrcPerByte`, `kCostInflatePerOutputByte`, `kCostLoadImagePerPixel` = 0 (TO VERIFY, spec V6); a host or test may set them (`chip.costs`) |
| Media FIFO, video | functional; no per-frame decode cost beyond the per-pixel cost above |
| `INT_CMDEMPTY` | raised when the last command completes |

So the gap is the numbers, not the design: a cost table per command class, applied by the
existing step machinery, gives "approximately the time it takes" with no change to the
architecture and no measurable CPU cost (one subtraction per step).

## What to measure on a VDAC2 board

Method (the spec's V6, and the one Zuma's profiler uses): read `REG_CLOCK` (system clocks)
immediately before writing `REG_CMD_WRITE` and poll `REG_CMD_READ` until it equals the
write pointer; the difference, minus the polling overhead measured with an empty command
(`CMD_NOP` equivalent), is the command's duration. Repeat each size several times, at
`CLKSEL` default and at 72 MHz.

| Command class | Sizes to sweep | Fits |
|---|---|---|
| a simple command (`CMD_DLSTART`, `CMD_COLDSTART`, `CMD_BGCOLOR`) | - | base cost per command |
| commands that emit display list words (`CMD_BUTTON`, `CMD_TEXT`, `CMD_NUMBER`, `CMD_GRADIENT` widgets) | text length 1, 10, 100; widths | per emitted word |
| `CMD_MEMSET` / `CMD_MEMZERO` / `CMD_MEMCPY` / `CMD_APPEND` | 64 B ... 256 KB | base + per byte |
| `CMD_MEMCRC` | 64 B ... 256 KB | base + per byte |
| `CMD_INFLATE` | compressible and incompressible data, 1-256 KB output | base + per output byte (and per input byte) |
| `CMD_LOADIMAGE` (JPEG, PNG) | 64x64 ... 800x480 | base + per pixel, per format |
| `CMD_MEDIAFIFO` + video frame | the video sizes software uses | per frame |
| `CMD_SETMATRIX`, `CMD_ROTATE`, `CMD_SCALE` | - | base |

Also worth one look on the card: whether a command's duration depends on what the graphics
engine is doing at the same time (memory contention with the line renderer). If it does,
the model needs a per-line charge too; the line cost of 08 provides the engine's load.

## Conclusions

- eve-emu already executes coprocessor commands procedurally with a cost model whose
  constants are zero; filling the table from a board measurement is the whole job for the
  OPTIMAL profile. Accuracy: within the spread of the measured fits; frame-level timing of
  software is preserved.
- Windows / Linux: nothing platform-specific; the cost table is data.
