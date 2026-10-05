# TS-Conf audit against the RTL and the TS-Labs Unreal fork

**Date:** 2026-10-05 · **Checked:** unreal-ng master `cf3adb714` · **Status:** [TODO.md](TODO.md)

## Why

TS-Conf programs that use the machine hard (`zifi.spg` switching video modes per line from line interrupts,
sprite multiplexers, DMA during the picture) showed several emulation bugs in October 2026, each found only when a
program happened to hit it. This audit checks the emulation rule by rule instead, against both references at once:

- **RTL** - the TS-Conf Verilog, the hardware itself: [tslabs/zx-evo](https://github.com/tslabs/zx-evo)
  `pentevo/fpga/current` (local clone at `9ce7544a`, 2026-09-27), plus the VDAC CPLDs in `pentevo/vdac`.
- **TS-Labs Unreal** - the TS-Labs fork of Unreal Speccy, the emulator TS-Conf was developed with:
  [tslabs/zx-evo-unreal](https://github.com/tslabs/zx-evo-unreal) `Unreal/` (`86fd99b`). How to run it:
  [reference-emulator-wine.md](../2026-09-27-tsconf/reference-emulator-wine.md).

For every behavior the audit records what the RTL does, what TS-Labs Unreal does and what unreal-ng does (each with
file:line), which unreal-ng test covers it, and a verdict:

| Verdict | Meaning |
|:--|:--|
| `match` | all three agree |
| `ng-matches-RTL, Unreal differs` | unreal-ng is right, the fork is not (no action) |
| `ng-matches-Unreal, RTL differs` | unreal-ng copied the fork's deviation: **unreal-ng bug** |
| `ng differs from both` | **unreal-ng bug** (or a documented approximation) |
| `unclear` | the sources do not settle it; the row says what would |

The RTL decides. Where the fork differs from the RTL, unreal-ng follows the RTL (`hardware-spec.md` §0).

## Documents

| File | Area | Rows |
|:--|:--|--:|
| [interrupts.md](interrupts.md) | frame / line / DMA interrupts, vectors, mask, /WAIT, CPU clock, cache, vdos | 41 |
| [video.md](video.md) | frame timing, windows, register latching, modes, border, mixing, CRAM, VDAC / PWM | 48 |
| [tsu.md](tsu.md) | tiles and sprites: layers, LEAP, descriptors, tile maps, prefetch, DRAM budget | 54 |
| [dma.md](dma.md) | DMA registers, devices, cycle costs, the DRAM arbiter | 50 |
| [memory-ports.md](memory-ports.md) | memory windows, `#xxAF` registers, legacy ports, reset values, FMAPS, ports of the devices | 63 |
| [TODO.md](TODO.md) | the bugs and gaps found, ranked, with their status | |

Each area file has the full table and a "Bugs and gaps" section with a suggested test (name and assertion) for every
bug and for every behavior that has no test yet.

## Result

| Area | match | ng = RTL, fork differs | **ng = fork, RTL differs** | **ng differs from both** | unclear |
|:--|--:|--:|--:|--:|--:|
| Interrupts, waits, clock | 21 | 8 | 5 | 5 | 2 |
| Video | 30 | 11 | 1 | 4 | 2 |
| TSU | 36 | 11 | 1 | 5 | 1 |
| DMA, arbiter | 33 | 9 | 2 | 5 | 1 |
| Memory, ports, reset | 31 | 24 | 4 | 3 | 1 |
| **Total (256)** | **151** | **63** | **13** | **22** | **7** |

Of the 35 rows in the two bug columns, some are approximations unreal-ng documents on purpose (the TSU register
snapshot at `ts_start`, the cache fill rule, the out-of-scope wait-port DMA); [TODO.md](TODO.md) lists what is a
real bug and what is a known approximation.

## Findings outside the code

- **The ZiFi "Please update TS Conf." message** in TS-Labs Unreal is not about the FPGA version: the 0.733 client
  writes `#F1`, `#FF` to `#C7EF` and reads `#C7EF` back; `#FF` means "no ZiFi in the AVR". TS-Labs Unreal answers
  only when a host COM port is configured (`[MISC] ZiFi=COMn`), so the client refuses to start there; the TS AVR and
  unreal-ng answer 1. `#00AF` STATUS carries only the VDAC version (0 IDE build, 3 VDAC, 7 VDAC2) in all three.
- **Corrections to [hardware-spec.md](../2026-09-27-tsconf/hardware-spec.md):** §4.4 (the first tile starts at
  `-(Xoffs & 7)`, not `-(Xoffs & 7) - 8`), §4.2 (G_X_OFFS in ZX and TXT loads the fetch column, it is not a pixel
  scroll), §4.3 (5-bit VDAC: the VDAC1 CPLD maps a channel like VDAC2 - `{level, 3'b0}` with PAL_SEL set, white =
  248, and the rounded 0..24 table without), §3.4 (the border from OUT `#FE` uses the PAL_SEL latched at line start),
  §9 (`#BFF7` reads `#FF` inside vdos), §12 (TS-Labs Unreal counts CPU reads and writes in `memcpucyc`).
- **The zifi pointer** vanishing under the bottom strip is the client's (its 0..239 clamp with a 16-line sprite);
  see [the ZiFi TODO](../2026-10-02-tsconf-zifi/TODO.md), Z5 follow-up 7.
