# ZX Spectrum Next: video timing

**Date:** 2026-10-07 · part of [README.md](README.md) · layers in [design-core.md](design-core.md) section 6

The Next derives its timing from the 28 MHz clock. The video pixel counters run in the 7 MHz domain (4 ticks of
28 MHz per pixel tick), and a 3.5 MHz T-state is 8 ticks of 28 MHz, i.e. 2 pixel ticks (jnext `timing.h`). Everything here is from jnext's
[`timing.h`](https://github.com/jorgegv/jnext/blob/main/src/video/timing.h) and its
`TASK-VIDEOTIMING-EXPANSION-PLAN.md`, which cite the VHDL `zxula_timing.vhd`, and from MAME's
[`specnext.cpp`](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/next/specnext.cpp). The VHDL is the
arbiter and is read in N0/N3.

## 1. Machine timings

NR `#03` bits 6:4 pick the timing (001 = 48K, 010 = 128K / +2, 011 = +2A / +2B / +3, 100 = Pentagon 50 Hz only);
bit 3 locks the user choice. The firmware writes it from `menu.def` / `config.ini` (the "timing=" setting is a
display-mode selector in the firmware config, a different thing).

| Timing | Pixel ticks per line (7 MHz domain) | T per line at 3.5 MHz | Lines | T per frame | Source |
|:--|:--|:--|:--|:--|:--|
| 48K | 448 (`c_max_hc` = 447) | 224 | 312 | 69888 | jnext `timing.h` |
| 128K / +2 | 456 (`c_max_hc` = 455) | 228 | **311** (`c_max_vc` = 310, VHDL; MAME's screen is 312 high and is wrong here) | 70908 (228 x 311) | VHDL `zxula_timing.vhd` |
| +3 / +2A | as 128K, with the interrupt column `c_int_h` = `136+2-12` instead of `136+4-12` (two pixel clocks earlier) | 228 | 311 | 70908 | VHDL |
| Pentagon | 448 | 224 | 320 | 71680 | jnext |
| 60 Hz variant (NR `#05` bit 2; not Pentagon) | as the machine | | 264 | 59136 for 48K (224 x 264) | jnext |

Q5 is closed by the VHDL: 128K and +3 are 228 T x 311 lines = 70908, the same as the classic 128K tables; the full
constant table (hblank, hsync, active start, interrupt point, per timing and 50/60 Hz) is in [research-fpga-vhdl.md](research-fpga-vhdl.md) section 1.

## 2. Raster counters and positions

| Name | Meaning | Source |
|:--|:--|:--|
| `hc`, `vc` | horizontal pixel-tick and vertical line counters; the display window is `hc` 128..383 and `vc` 64..255 for 48K (256x192) | jnext `timing.h` |
| `c_min_hactive`, `c_min_vactive` | per machine: 48K 128 / 64; 128K / +3 136; Pentagon `min_vactive` 80 | jnext tasks S13.05-S13.06 |
| ULA prefetch | `hc_ula` = 0 at `c_min_hactive - 12` (a 12-pixel prefetch), later at `-11` for the delayed section | jnext |
| Interrupt position | 48K `hc` 116, `vc` 0; 128K `hc` 128, `vc` 1; Pentagon `hc` 439, `vc` 319 | jnext S14.01-S14.03 (VHDL `zxula_timing.vhd:547-559`) |
| Line interrupt | NR `#22` bit 1 enables, value in `#22` bit 0 + `#23`; target 0 fires on the frame-boundary line (`c_max_vc`), target N on line N-1 of the counter | jnext (`int_line_num`) |
| `NR #64` offset | added to the vertical line counter for the copper, the line interrupt and `#1E/#1F`; takes effect when the ULA reaches row 0 (up to one frame) | NR-TXT |
| `#1E/#1F` | the active video line | NR-TXT |

The machine keeps `framePos` (CPU T-states at 3.5 MHz equivalent since the frame start); `hc`/`vc` are derived
from it with the line table. All the registers above read the derived values, so a speed change does not move the
raster ([design-cpu.md](design-cpu.md) section 3).

## 3. Output geometry

Framebuffer: border plus display in Next pixels (the 320x256 super-hires pixel grid is the common denominator
of Layer 2 and the tilemap: 256x192 ULA pixels are doubled). jnext uses 320x256 with a 48-pixel side border;
hi-res (Timex 512, Layer 2 640x256) doubles the horizontal resolution. The first renderer produces a
**640x256 line buffer** (two samples per super-pixel, ULA pixels doubled once, 320-wide layers doubled) and the
Qt/window layer scales it, as the Sprinter does with its 736x288 raster. Border extents: N-read in N3 from the
VHDL and the existing TSConf/Sprinter frames; the first cut uses jnext's numbers.

## 4. Interrupt generation

| Source | When |
|:--|:--|
| ULA frame interrupt | at (`c_int_h`, `c_int_v`) of the machine timing, unless disabled by NR `#22` bit 2 or `#FF` bit 6; the pulse length is part of the pulse-mode model (N-read) |
| Line interrupt | when the counter reaches the programmed line (position within the line: start of the line, N-read) |
| Both | into `NextInterruptSource` ([design-peripherals.md](design-peripherals.md) section 2) |

## 5. Contention and the floating bus

| Item | State |
|:--|:--|
| Contention | **Q6 closed by the VHDL** ([research-fpga-vhdl.md](research-fpga-vhdl.md) section 2): only at 3.5 MHz (`cpu_speed = 00`), never in Pentagon timing, off when NR `#08` bit 6 is set; memory contention only for 16K banks 0-7 (48K: bank 5; 128K: odd banks; +3: banks 4-7); port contention on `A0 = 0`, `#7FFD`, `#BF3B`, `#FF3B`; the delay window is 12 of every 16 pixel clocks while `hc < 256` on paper lines (+3 adds two more). In 48K / 128K timing it holds the CPU clock; in +3 timing it is a real WAIT on memory accesses only. The existing `UlaContention` of unreal-ng can serve the 48K / 128K patterns only if its alignment matches the Next's one-clock-early decision; a Next-specific table of 16 entries per 8 T is simpler and exact |
| Floating bus | `#FF` read (NR `#08` bit 2 = 0) and the +3 floating-bus ports; 48K/128K patterns from the ULA fetch; jnext `TASK-FLOATING-BUS-PLAN.md` |
| ULA snow | not modeled for the Next (the FPGA ULA has no Ferranti snow) |

## 6. The copper and the raster

The copper waits on (`hpos` in units of 8 ticks? **N-read**: the field is 6 bits `[14:9]` of the WAIT word,
"H column in units of 8"; `vpos` 9 bits `[8:0]`) against `hc`/`vc` including the `#64` offset. The machine keeps
`copperNextEvent` (a framePos value) updated by `NextRegs` whenever the copper control or address changes, and the
step loop compares it with the current frame position at the same point where the line boundary is checked; no
timer thread. A MOVE executes at the WAIT position and takes the cycle count the VHDL gives (N-read; jnext
test-plan `COPPER-TEST-PLAN-DESIGN.md` lists it).

## 7. Open points

| Point | Next step |
|:--|:--|
| 128K and +3 exact line count and INT position | VHDL read in N3 (Q5) |
| 60 Hz variants for 128K/+3 | VHDL (jnext S13.08 covers the 48K case) |
| Pulse width of the ULA interrupt in pulse mode | VHDL / ZXSpectrumNextTests `Interrupts` |
| Refresh rate of HDMI vs VGA timings (NR `#11`) | not modeled; only the 50/60 choice |
| Start-up and `hc` reset on a machine-timing change at run time | jnext "re-derive at the frame edge" is the starting rule |
