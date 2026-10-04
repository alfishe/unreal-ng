# ZX-MultiSound card logic vs the card's CPLD (Verilator)

The ZX-MultiSound's whole bus behavior lives in one CPLD whose Verilog is public
([UzixLS/zx-multisound](https://github.com/UzixLS/zx-multisound) `cpld/rtl/top.v`). This folder runs that Verilog in
Verilator next to the emulator's `MultiSoundLogic` (`core/src/emulator/slots/cards/multisound/`) and compares them bus
cycle by bus cycle. What they agree on is frozen as test data for `core-tests`, so Verilator is never a build
dependency. Design: [tdd-card-logic.md](../../../docs/inprogress/2026-10-03-zx-multisound/tdd-card-logic.md).

## Quick start

```bash
tools/verification/multisound/fetch-rtl.sh      # pinned top.v (commit d7f3ac2, SHA-256 checked) -> refs/ (git-ignored)
tools/verification/multisound/build.sh          # Verilator + c++ -> build/mscosim (in the machine-wide build slot)
build/mscosim run testdata/sound/multisound/scenarios/l03-control-pro.msc --dump
tools/verification/multisound/regenerate.sh     # re-derive all frozen test data (about 2 minutes)
```

Needs Verilator 5.x (`/opt/homebrew/bin/verilator` on the dev Mac) and a C++20 compiler. Nothing here is part of the
CMake build.

| File | Purpose |
|---|---|
| `fetch-rtl.sh` | Downloads `top.v` at the pinned commit and derives `refs/top-pro.v` and `refs/top-classic.v` (below) |
| `build.sh` | Verilates four variants (pro / classic control mask x 1 MB / 2 MB GS RAM build) into `build/` and links `build/mscosim` with `multisoundlogic.cpp` and the shared scenario helper |
| `tb/mscosim.cpp` | The testbench and the comparison driver |
| `regenerate.sh` | Writes `testdata/sound/multisound/scenarios/*.expected` and `core/tests/emulator/slots/cards/multisound/multisoundrtltables.h`; refuses to write anything where the logic disagrees with the RTL |

## The two edits to the RTL

`refs/top.v` stays pristine. The testbench variants change exactly this (the script fails if a pattern does not match
once):

1. `fm1_ena` / `fm2_ena` are `output reg` assigned `1'bz` inside a clocked block (an open-drain pin). Verilator has
   no Z inside registers and would read "released" as 0 ("muted"), so the variants write `1'b1` for it. The testbench
   reports `fm=mute` when the pin is 0.
2. `top-classic.v` adds the unofficial issue [#11](https://github.com/UzixLS/zx-multisound/issues/11) patch
   (`ctrlMask = classic`): five compared bits (`d[7:3] = 11111`) for the YM latches while the SAA DIP is off, four
   while it is on. The patch was posted against an older revision with an inverted chip select; it is applied with
   the current non-inverted `ym_chip_sel <= zxd[0]`.

The 2 MB GS RAM firmware is the same source with `+define+GS_RAM_2MB`.

## The testbench

- **Time:** one tick is half a 32 MHz period (1/64 µs); `clk32` rises on even ticks and every bus cycle starts on an
  even tick. Inputs change before the clock edge of their tick.
- **Host cycles** follow the Z80 datasheet in half T-states at the scenario's clock (default 3.5 MHz, `cpu 14` for a
  ZX-Evo turbo): M1 (M1 low T1-T2, MREQ + RD T1↓-T3↑, refresh MREQ T3↓-T4↓), memory read / write (3 T), I/O read /
  write (4 T with the automatic wait state, IORQ + RD / WR from T2↑ to T3↓). The card itself ignores IORQ: it detects
  I/O as "RD or WR without MREQ and M1" (`ioreq`), which is exactly what these cycles give it.
- **GS cycles** are driven the same way on the GS bus at 16 MHz.
- **Stand-ins:** the YM2203 pair drives `ad` with `#C0 | chip << 4 | A0` when chip-selected and read (so a read shows
  which chip and which A0 answered); the GS RAM / ROM drives `gd` with the scenario's value on a GS memory read.
- **Observed per cycle:** IORQGE during the IORQ window; a YM write = a chip whose CS is low when `awr_n` rises (the
  YM2203 latches on that edge), with `aa0` and `ad`; the same for the SAA (`saa_cs_n`); SounDrive channel writes
  (`sd_dac*_cs` with /WR low on a clock edge); the value on `zxd` (host IN) or `gd` (GS IN) when RD rises, if the CPLD
  drives all eight bits; the GS memory chip select and `gma` in the middle of a GS memory cycle. At the end of the
  cycle the latches: chip select, read mode, `fm*_ena`, the SAA clock (cross-checked against `saa_clk` toggling in the
  cycle's last 8 ticks), ROM lock, GS registers and flags, `dac*` and `vol*`.
- **Notes** (`; line N: ...`) report what the logic does not model: a CS + WR overlap on a chip that does not latch
  (see "Findings"), a partly driven data bus, IORQGE changing inside the IORQ window.

## Scenario format (`.msc`)

One bus cycle per line, hex operands, `#` starts a comment. Header directives (before the first cycle): `mask
pro|classic`, `ram 1m|2m`, `cpu <MHz>`, `dip <list>`. Later `dip` lines change the switches mid-run.

| Line | Cycle |
|---|---|
| `m1 <addr>` | host opcode fetch (sets the ROM lock) |
| `mr <addr>`, `mw <addr> <val>` | host memory read / write |
| `out <port> <val>`, `in <port>` | host I/O |
| `gout <port> <val>`, `gin <port>` | GS Z80 I/O |
| `gmr <addr> <val>`, `gmw <addr> <val>` | GS Z80 memory read (the RAM returns `val`) / write |
| `reset` | bus /RESET |
| `dip ym,saa,gs,sd` / `dip none` | DIP functions enabled |
| `par <host cycle> \| <GS cycle> @<ticks>` | both at once, the GS cycle starting `ticks` later (DAC arbitration) |

Each line becomes one record, the same text on both sides:

```
out FFFD F1 => ge=1 yw=2:0:F1 sw=- sd=- rd=- map=- | sel=1 st=1 fm=on saa=on lock=0 gs=00,00,00,00 fl=00 dac=00/00,...
```

`ge` IORQGE, `yw` YM write (chip mask 1 / 2 : A0 : value), `sw` SAA write (A0 : value), `sd` SounDrive channel mask,
`rd` value driven on a read, `map` GS memory (chip 0 ROM / 1-4 RAM << 4 | gma); after `|`: chip select, status read
mode, FM, SAA clock, ROM lock, GS data / command / page / output registers, data and command flags, `dac/vol` x 4.

`mscosim run <file> [--dump] [--expect <out>]` prints the first differing cycle and field.

## Frozen test data (core-tests `MultiSoundLogic_Test`)

| Data | What it pins |
|---|---|
| `testdata/sound/multisound/scenarios/*.msc` + `.expected` | hand-written cases for L1-L17 and the real-program trace (CL-2), RTL records line by line |
| `multisoundrtltables.h` `MultiSoundSweepRows` | the decode sweep: for each control mask and each of the 16 DIP settings, every port 0-#FFFF written (value = low byte ^ #5A, so #xxAD carries control bytes) and read, ROM lock off and on: 262 148 cycles, as an FNV-1a hash of all records plus event counts |
| `MultiSoundWitnessRows` | the all-enabled pro sweep's event codes at the ports with A12-A9 = 0, so a hash failure can name a port |
| `MultiSoundGsMapHash1Mb` / `2Mb` | GS page register 0-127 x ten addresses through the GS bus controller |

The sequences themselves are in `core/tests/_helpers/multisoundscenario.h`, shared by the testbench and the tests.

## Other measurements

`mscosim dac`: mean of `dac0_out` over many 32 MHz periods for samples x volumes against `0.5 + 0.5 x
SampleLevel / 128 x VolumeGain64 / 64` (exact, worst error 0). `mscosim gsint`: the GS INT period and width.

## Findings

Recorded in the [hardware reference](../../../docs/inprogress/2026-10-03-zx-multisound/hardware-reference.md) and
[tdd-card-logic.md](../../../docs/inprogress/2026-10-03-zx-multisound/tdd-card-logic.md) §7.
