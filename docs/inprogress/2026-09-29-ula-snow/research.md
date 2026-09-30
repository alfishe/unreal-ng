# ULA snow: research

**Date:** 2026-09-29 · **Belongs to:** [requirements.md](requirements.md), [tdd.md](tdd.md) · **Status:** [TODO.md](TODO.md)

## 1. What snow is

On the Sinclair machines with a Ferranti ULA (16K, 48K, 128K, +2) the CPU's DRAM refresh and the ULA's screen
fetch can hit the same memory in the same clock tick. Every opcode fetch (M1) ends with a refresh: in T3-T4 the
Z80 puts `I` on the high address byte and `R` on the low byte and pulses MREQ. When `I` points into the slow
(contended) memory, that refresh goes to the same DRAM chips the ULA is reading the picture from. If the two
coincide, the ULA takes part of its address from the refresh, and the screen shows other bytes than the ones in
the screen memory. Because it looks like interference on a TV it is called "snow". Games normally keep `I` out of
#40-#7F for this reason (the ROM uses `I` = #3F).

Terms used below:

| Term | Meaning |
|:--|:--|
| M1, T1-T4 | The opcode fetch; T1-T2 read the opcode, T3-T4 are the refresh (address `I`:`R`) |
| ULA cycle | The ULA fetches in groups of 8 ticks (16 pixels, two character cells): pixel byte 1, attribute byte 1, pixel byte 2, attribute byte 2, then 4 ticks without a fetch |
| Snow | The first cell of the group shows bytes whose low address bits come from `R` |
| Double | The second cell of the group shows the first cell's bytes again |

## 2. The hardware model

Weiv's description (2022, zx-pk.ru thread 34737, copied on the zxe.io test wiki page "Snow effect"), with
TheMartian's explanation of the DRAM strobes:

- **Condition:** `I` points into slow memory: #40-#7F on the 16K / 48K; on the 128K / +2 also #C0-#FF when an odd
  page (1, 3, 5, 7) is paged there. The +2A / +3 (gate array) and the clones do not snow.
- **Snow:** the refresh's 4th tick (T4 of the M1) coincides with the 3rd tick of the ULA's 8-tick cycle, the first
  pixel / attribute burst. The low address bits of pixel byte 1 and attribute byte 1 are replaced by `R`, "already
  increased in this opcode fetch".
- **Double:** T4 coincides with the 5th tick, the second burst. Pixel byte 2 and attribute byte 2 are not read, and
  the second cell shows the first cell's bytes again.
- **Anything else:** no effect.
- **Which bits of `R`:** bits 6..0. The 4116 DRAM of the 48K takes a 7-bit row; Snow R (Neo Spectruman, 2022) and
  Weiv both find that bit 7 does not take part. For the 4164 chips of the 128K nobody has measured it.
- Some 128K machines hang or reset under snow (ULA Snow Crash, Panov 2018).

In unreal-ng's terms the ULA cycle is the one its floating bus already models (`UlaContention::FetchedByte`,
validated by Butler's hardware-measured floating-bus tests 36 and 37): 0-based phase 2 = pixel byte 1, 3 =
attribute 1, 4 = pixel byte 2, 5 = attribute 2. Weiv's "3rd" and "5th" ticks are the first ticks of the two bursts,
phases 2 and 4.

## 3. Where the sources disagree

- The value of `R`: Weiv says after the increment of this M1; ZX-M8XXX (`core/ula-snow.js`) uses the value before it.
- TheMartian's address bits: the wiki copy says 7-0 / 13-8, the zx-pk copy 6-0 / 13-7.
- The absolute tick: xpeccy-plus (`video.c:705-760`) anchors the group on its 14335 contention origin; ZX-M8XXX
  fitted its offset against the SpecEmu emulator, not against hardware.
- An FPGA snippet quoted on zx-pk says the 128K ULA does not snow; Snow128N (Weiv) and videos of a real +2
  contradict it.

## 4. Test programs

Downloaded 2026-09-28 from the zxe.io test depot (listed on its wiki); not in the tree.

| Test | Author, year | Machines | What it shows | Usable as a reference? |
|:--|:--|:--|:--|:--|
| Snow Hold | Woodmass, 2025, GPL, with source | 48K | Syncs to an exact T-state (`execcycle`), `I`=#40, `R`=0 each frame; each line runs 16 x `LD A,0` (7 T), so the refresh walks through every phase. Static stripes | **Yes: photos from three real machines** (below) |
| Snow48 (b to c0) | azesmbog, 2021 | 48K | `I`=#7E, a NOP string per line, keys patch in delays | No: waits in a 4 T `JP (HL)` loop, so the picture depends on how it was loaded |
| Snow | Hikaru, 2014 | 48K | NOP string, `R` stepped per line | No: not frame-aligned |
| Snow Tests (snow48+ / snow128+ / ula128) | Weiv, 2022 | 48K / 128K / +2 | Hikaru's test plus a tuning table: every 40 lines the loop is 1 T longer, so the four bands cover all phases | Yes in principle; hardware only as videos |
| Snow128N | Weiv, 2023 | 128K / +2 | The snow's colour tells which bank the ULA read | Later (the 128K #C0-#FF case) |
| Snow R | Neo Spectruman, 2022 | 48K | Whether bit 7 of `R` reaches the address | - |
| Snow Spy | Woodmass, 2021 | 48K | Snow read back through the floating bus, as a table | Analytic idea; no hardware results published |
| ULA Snow Crash | Panov, 2018 | 128K | The snow-induced crash | - |

## 5. The hardware reference: Snow Hold on three machines

Photos by Marta Sevillano Mancilla, 2025-05-27, of the Snow Hold beta (zxe.io test wiki, "Snow Hold"); small
copies in [photos/](photos/):

| Photo | Machine |
|:--|:--|
| [snowhold-hw-1.jpg](photos/snowhold-hw-1.jpg) | ZX Spectrum 48K, Issue 3, NEC D780C-1 CPU, ULA 6C001E-6 |
| [snowhold-hw-2.jpg](photos/snowhold-hw-2.jpg) | ZX Spectrum 48K, Issue 3B, Zilog CPU |
| [snowhold-hw-3.jpg](photos/snowhold-hw-3.jpg) | ZX Spectrum + (Spain), Issue 6A, NEC D780C-1 CPU |

All three show the same picture: three bands of the test pattern, and in each band two "ladders" of snow at the
same columns that run down into the empty lines below the band. The same result on two CPU makes and three
boards makes it a good anchor for the model's absolute tick.

## 6. Anchoring the model on the photos (2026-09-29)

unreal-ng got the model with the tick and `R` as parameters, and the Snow Hold beta was rendered for each
choice and compared with the photos:

| Choice | Ladder columns | Ladder colours (top band to bottom) | Photos |
|:--|:--|:--|:--|
| T4 on the first burst, `R` after the increment (Weiv's wording read literally) | 4 and 18 | - | columns 2 and 16: no |
| **T3 on the first burst (pixel byte 1's fetch)**, `R` after the increment | 2 and 16 | green, cyan, red | top band pink / magenta: no |
| **T3 on the first burst, `R` before the increment** | **2 and 16** | **magenta, green, cyan** | **yes, on all three machines** |

The photos show bright magenta, green and cyan lines over-exposed to pink and white; the ladders' spacing (one
lit line every 4 screen lines) and length match as well. So: **snow when the refresh's T3 falls on the ULA's
fetch of pixel byte 1, double when it falls on pixel byte 2's; the address's bits 6..0 are `R` before this
M1's increment** (as the Z80 puts `R` on the bus, then increments it; ZX-M8XXX uses this value too). Weiv's
"4th tick of the fetch" and "3rd / 5th tick of the ULA cycle" count one tick later on both sides, which gives
the same instant.

## 7. Cross-check against the MiSTer ZX-Spectrum core

`rtl/ula.sv` (MiSTer ZX-Spectrum core; the snow timing fix of 2026-07-24, commit `a2bc564`, samples MREQ and
RFSH through the ULA's transparent latches on the CPU clock's low phase):

```
if (mZX & ~m128 & ~mreqt23 & ~rfsht23 & contendAddr & snow_ena) case(hc_next[3:0])
    'h8,'hC: vaddr[6:0] <= addr[6:0]; // R register corrupts DRAM row address
```

`hc` counts pixels (two per T-state); `h8` / `hC` load the row address of the first / second cell's bitmap, and
the attribute fetch at `hA` / `hE` changes only bits 14..7, so bits 6..0 are shared by a cell's pixel byte and
attribute.

| Point | MiSTer | unreal-ng | Evidence |
|:--|:--|:--|:--|
| Condition | `I` in #40-#7F (the address bus in slow memory) | the same | agree |
| Bits taken from the refresh | 6..0, pixel byte and attribute of the cell | the same | agree |
| First cell's tick | the RAS of cell 0 at `hc_next` = 8: INT + 14338, the floating bus's first-bitmap tick | T3 on the floating bus's pixel byte 1 tick | agree |
| R | the address bus during the refresh | R before the increment (what the bus carries) | agree; the photos exclude "after" |
| Second cell (`hC`) | snow: the second cell's row from R | double: the second cell repeats the first (Weiv) | **the photos side with Weiv**: with MiSTer's rule Snow Hold also shows ladders at columns 13 and 27 in every band (checked in unreal-ng with the rule switched), none of the three machines shows them |
| 128K / +2 | no snow (`~m128`: "ULA-128 has no snow bug") | snow, #C0-#FF with an odd page too | Weiv: Snow128N and videos of a real +2 show snow; no hardware source for the MiSTer rule. Open until a photo of snowtest on a 128K / +2 |

## 8. unreal-ng before this work

No snow: `I` reached the bus only for the no-MREQ contention of internal cycles (`Z80::IR()`), the renderer read
the screen memory directly, and [the M1 contention design](../2026-09-28-m1-contention/design.md) listed snow as a
non-goal.
