# Which machines slow the CPU down, and why: contention by circuit design

**Date:** 2026-09-29 · **Belongs to:** [design.md](design.md) (PLAN #61) · **Question:** "Does the Scorpion
have contention at all? Which machines should have it, by their circuit design?"

## 1. Short answer

- **Real memory contention** (the CPU is held while the video logic reads the screen) exists only where the
  video chip and the CPU fight for the *same* memory with the video given priority: the Sinclair machines
  (Ferranti ULA 48K / 128K / +2, Amstrad gate array +2A / +3), the Timex SCLD machines, and faithful 48K
  replicas (Harlequin). The ZX Spectrum Next and the ZX-Evo BaseConf reproduce it on purpose, and only at
  3.5 MHz in their Sinclair timing modes.
- **The Soviet clones** (Pentagon, Scorpion, Profi, ATM, Kay, Quorum, ZX-Evo, Sprinter) give the video and the
  CPU **fixed, separate time slots** on the memory bus. The CPU never waits for screen data at 3.5 MHz.
- **The Scorpion has no memory contention.** It has another delay: a **one-T-state WAIT on opcode fetches
  (M1) from RAM** that makes every such fetch start on an even T-state ("Even M1"). Data reads, data
  writes, I/O and fetches from ROM do not wait. In 7 MHz turbo (Turbo+ board), RAM accesses wait for a free
  memory slot, and more often during the paper area. This is the only case where a Scorpion slows down
  because of the screen.
- **unreal-ng today:** contention only on the five Sinclair models (correct). The Scorpion's Even M1 is
  configured (`EvenM1=1`) but **never applied**: the field the CPU tests is never set (§12).

## 2. Terms

| Term | Meaning |
|:--|:--|
| T-state (T) | One CPU clock period: 1/3.5 MHz ≈ 286 ns at normal speed. |
| M1 | The opcode fetch cycle, the first bus cycle of every instruction and of every prefix (`CB`, `DD`, `ED`, `FD`). 4 T long: 3 T to read, 1 T for DRAM refresh. |
| Memory contention | The video logic owns the memory for a while, and a CPU access to that memory waits until the video logic lets go. How long it waits depends on the T-state, so it follows a repeating pattern such as 6,5,4,3,2,1,0,0. |
| I/O contention | The same, applied to port accesses. The Ferranti ULA does it, because the CPU's address bus reaches the ULA even for I/O. |
| Clock stretching | The video logic holds the CPU clock high, so the CPU simply stops for a few T. The Ferranti ULA works this way. |
| WAIT | A Z80 input pin. While it is low at the sampling edge, the CPU inserts an extra T-state ("Tw") in the current memory or I/O cycle. The gate array, the Scorpion and most FPGA machines use it. |
| Even M1 | A WAIT inserted so that an opcode fetch never starts on an odd T-state. The cost is 0 or 1 T per fetch. |
| Time slots / interleaving | The memory runs at 2x or 4x the CPU speed, and each memory cycle belongs to either the video or the CPU in a fixed order. When the slots are laid out so that the CPU always finds its slot in time, it never waits. |
| Paper | The 256x192 picture area, during which the video logic reads screen bytes. |

Worked example (48K, code in contended RAM): `NOP` at #4000 starting 14335 T after the interrupt meets the
ULA pattern at delay 6. It takes 4 + 6 = 10 T instead of 4. The same `NOP` at #8000 takes 4 T. On a Pentagon
both take 4 T.

## 3. Summary table

"Other CPU delays" lists everything that is not ordinary memory contention. Confidence: **high** = circuit or
RTL source read directly; **medium** = designer text or careful reverse engineering, or RTL of a re-creation
rather than of the original board; **low** = forum statements or secondary sources only.

| Machine | Memory contention | I/O contention | Other CPU delays | How video shares memory | Confidence | Key sources |
|:--|:--|:--|:--|:--|:--|:--|
| ZX Spectrum 16/48K (Ferranti ULA, issues 1-6) | yes: #4000-#7FFF, 6,5,4,3,2,1,0,0, from 14335/14336 T, also on internal (no-MREQ) cycles | yes: even ports and high byte #40-#7F (4 patterns) | none; the issues differ only by up to 1 T of onset ("early/late timing") | lower 16K is a separate DRAM owned by the ULA while it fetches; the ULA stretches the CPU clock | high | sinclair.wiki Contended memory / Contended I/O; FUSE; zxdesign.info |
| 128K / +2 grey (ULA 5C/7K) | yes: pages 1, 3, 5, 7 wherever mapped, from 14361 T, 228 T/line | yes, as 48K; the high byte is checked against the current mapping (#C000 with an odd page) | none | as 48K | high | sinclair.wiki; FUSE |
| +2A / +3 (gate array 40077) | yes: pages 4-7 in any slot, 1,0,7,6,5,4,3,2 (129 T window per line) | **no** | none; no contention of internal cycles (MREQ only) | shared DRAM, the gate array pulls WAIT | high | sinclair.wiki; Next `zxula.vhd` via jnext `src/memory/contention.cpp:318-362`; this folder's Rak Timing Test results |
| Timex TC2048 / TS2068 (SCLD) | yes, assumed ULA-like on the screen 16K (#4000-#7FFF), different onset | presumably as 48K | none known | as 48K (SCLD replaces the ULA) | medium-low: no circuit-level description found | sinclair.wiki "Timex 2000 series"; zxsp `Source/Uni/ZxInfo/z/info_tc2048.h:45-47` ("TODO: verify!") |
| Harlequin (open 48K replica) | yes: reproduces the 48K ULA | yes | none | copies the ULA's DRAM interface | medium-high | zxdesign.info/harlequin.shtml |
| Leningrad-1 | disputed: ru.wikipedia says "the same CPU slowdown at #4000-#8000"; ZXMAK2 models Even M1 on all fetches instead | unknown | possibly Even M1 (emulator only) | discrete logic, details not found | low | ru.wikipedia "Клоны ZX Spectrum"; ZXMAK2 `Clone/UlaLeningrad.cs:19-29` |
| Pentagon 128 / 512 / 1024SL | **no** | **no** | none at 3.5 MHz; 1024SL 7 MHz turbo: no wait documented | fixed interleaved slots (14 MHz master clock, CPU at 14/4) | high for "no contention", low for the slot details | Pentagon FAQ (zxspectrum.hal.varese.it/static/documenti/pentagon.txt): "128k of NOT-CONTENDED memory (no slow areas)" |
| **Scorpion ZS-256** (yellow, green, ProfROM) | **no** | **no** | **Even M1 on opcode fetches from RAM** (0 or 1 T), not ROM, not data, not I/O | fixed slots on the H0/H1 phase counter (7 MHz); the CPU gets RAM in the RAS phase | medium-high (see §6) | EPLD equations SC15.1 (zx-pk.ru thread 940, post #40); zx-pk.ru thread 13345 post #86 |
| Scorpion Turbo+ (7 MHz) | **yes, of a different kind:** in turbo every RAM access waits for a free slot, more often in the paper area | no | turbo on by `IN` from #7FFD, off by `IN` from #1FFD or reset | as above; in turbo the CPU slots are halved during the paper | medium | same EPLD equations; MAME `scorpion.cpp:635-640` |
| Scorpion GMX | no (by descent from Turbo+) | no | turbo via #7EFD bit 7 (MAME); waits not documented | as Turbo+ presumably | low | MAME `scorpion.cpp:786-789` |
| Profi (1024) | no (original) | no | none known at 3.5 MHz; turbo in later revisions | discrete slot scheme | medium: consensus, no original schematic read | [technical-design.md](../2026-09-21-profi/technical-design.md); ZXMAK2 `Profi/UlaProfi3XX.cs` |
| Karabas-Pro (Profi re-creation, FPGA) | only in its optional "classic" screen mode at 3.5 MHz (#4000-#7FFF and even ports, clock gated) | same condition | 14 MHz: ~400 ns WAIT on every MREQ cycle; turbo drops to 3.5 MHz after FDC access | video uses the second RAM slot of each 7 MHz cycle | high (RTL) | `karabas-pro/firmware/src/fpga/profi/rtl/karabas_pro.vhd:1200-1232`, `memory.vhd:196-210, 273-284` |
| ATM Turbo 2+ (v7.10) | no | no | 7 MHz turbo (#77 D3); keyboard `IN (#FE)` holds WAIT until the 8031 answers | discrete interleave, not documented in detail | medium | `zx-evo-docs/ATM/atm2_arch.pdf` (port #FE section) |
| ATM Turbo 1 (4.50) | no known | no known | 7 MHz turbo; nothing on waits found | not documented | low | none found |
| ZX-Evo BaseConf (ATM3) | no in the Pentagon raster (default); **emulated** 48K-style contention in the 48K and 128K rasters, only at 3.5 MHz | same condition (even ports) | 14 MHz: variable waits per access (M1 +3..+6, read +2..+5 fclk), external I/O at 7 MHz; DOS switch stall; WAIT ports #BFF7 / #xxEF (AVR) | DRAM arbiter, 8-cycle blocks, video takes 1/8 or 1/4, CPU the rest | high (RTL) | `pentevo/fpga/base_trdemu/trunk/z80/zclock.v:265-282`, `video/video_sync_h.v:250-283`, `z80/zmem.v:254-305`, `dram/arbiter.v:55-86` |
| ZX-Evo TS-Conf | no | no | 14 MHz: waits on cache misses; CPU stalls when video + DMA + sprites use the whole DRAM bandwidth; CPU priority lowered for DMA | same arbiter with TS / TM / DMA clients | high (RTL) | `zx-evo-tsconf/pentevo/fpga/current/z80/zmem.v:132-208`, `dram/arbiter.v:40-50, 170-190` |
| Kay-1024 (NEMO) | no at 3.5 MHz ("WAIT-free NORMAL mode") | no | turbo: IORQ stretched; effective RAM clock 6.3-7.0 MHz (arbitration losses); Kay-256 had waits at 3.5 MHz | discrete arbitration | medium (designer article) | zxpress.ru/article.php?id=15217 |
| Quorum 128/1024 | no known | no known | nothing found | not documented | low | schematics exist: github.com/UncleRus/quorum-reborn |
| Sprinter | no ULA-style | no | at 21 MHz: waits on main DRAM (7 MHz); none from the 64K fast RAM | separate video RAM (own bus) | medium-low | Aspect #3 review (zxpress.ru); MAME `sprinter.cpp:1720-1730` |
| ZX Spectrum Next | yes, in 48K / 128K / +3 timing at 3.5 MHz only (bank 5 / odd banks / banks ≥ 4); clock stretch (48/128) or WAIT (+3) | 48K / 128K timing yes; +3 timing no | 28 MHz: 1 wait on every memory read from SRAM / bank-5 BRAM; any turbo turns contention off; Pentagon timing has none | FPGA, video BRAM separate; contention is emulated by rule | high (VHDL) | `zxnext.vhd:4481-4492, 3171-3181`, `zxula.vhd:587-600` as cited in `jnext/src/memory/contention.cpp:106-110, 318-362`, `mmu.h:1548-1561` |

## 4. Why the circuit decides it

A memory chip can serve one reader at a time. What differs between machines is who waits when both want it.

- **Ferranti ULA.** The ULA reads two bytes (bitmap, attribute) per 4 T during the paper, in bursts of 8 T with
  4 T gaps in the Ferranti fetch order. Its lower 16K DRAM is on its own bus. When the CPU puts an address in
  #4000-#7FFF (or, on the 128K, an address that the ULA decodes as a contended page) while the ULA needs the
  bus, the ULA stops the CPU clock until its burst is done. The ULA looks only at the address lines and MREQ /
  IORQ, not at the kind of cycle, so internal cycles that merely put a contended address on the bus also
  stop, and I/O cycles with such a high byte stop too.
- **Amstrad gate array.** Same idea, but the gate array pulls WAIT, and only when MREQ is active. So no I/O
  contention and no contention of internal cycles.
- **Soviet discrete clones.** The designers used memory fast enough to be accessed twice (or more) per CPU
  memory cycle and wired a counter that hands out the slots: video, CPU, video, CPU. The CPU's memory
  cycle always finds its slot, so nothing waits. The price is exact phase: the CPU's slot must coincide with
  the part of the Z80 bus cycle when it latches data. That is where the Scorpion's Even M1 comes from (§6).
- **FPGA machines** (ZX-Evo, Next, Karabas-Pro) have far more bandwidth than a 3.5 MHz Z80 needs and have no
  physical reason to contend. They add contention deliberately, by rule, in their Sinclair-compatible modes,
  and turn it off in turbo.

## 5. Sinclair and Sinclair-compatible machines

### 5.1 48K (Ferranti ULA, issues 1-6)

- Contended range #4000-#7FFF. Pattern 6,5,4,3,2,1,0,0 per 8 T for the 128 T of each paper line, starting
  14335 T after the interrupt in FUSE's convention (14336 in others, the same event counted differently),
  224 T per line.
- I/O: four patterns by (high byte in #40-#7F) x (port bit 0 = 0, the ULA's own port): N:4, N:1 C:3,
  C:1 C:3, C:1 x4.
- No-MREQ cycles: contended by address, T by T.
- Board issues: no structural difference in contention. The wiki reports "early / late timing", a shift of up to
  one T attributed to ULA temperature, not to the issue.
- Source: sinclair.wiki.zxnet.co.uk/wiki/Contended_memory and /wiki/Contended_I/O. This project implements
  and tests all of it (phases 1-3 of [design.md](design.md), Butler 48K 72 of 72).

### 5.2 128K / +2 grey (ULA 5C / 7K)

- Same pattern, from 14361 T, 228 T per line. Contended pages 1, 3, 5, 7, in whichever slot they appear.
- I/O rule as 48K, the high byte checked against the current mapping.

### 5.3 +2A / +3 (gate array 40077)

- Contended pages 4-7 in any slot (so the all-RAM layouts can contend at #0000).
- Pattern 1,0,7,6,5,4,3,2 by the consensus of emulators and the Next VHDL; the sinclair.wiki page writes
  "1,7,6,5,4,3,2,1,0,0", which is the same sequence read from a different starting point. Photos of Rak's
  Timing Test on real hardware show a 129 T window per line ([test-programs.md](test-programs.md) §2.5).
- WAIT, MREQ only: no I/O contention, no contention of internal cycles.

### 5.4 Timex TC2048 / TC2068 / TS2068 (SCLD)

- The SCLD replaces the ULA and drives the same kind of shared DRAM. The wiki: "timings for these machines are
  unknown but should be similar to that for the 48K machine, except that the pattern starts at a different
  number of T-states after the interrupt." The TS2068 runs at 3.528 MHz, 60 Hz.
- zxsp uses the 48K pattern on #4000-#7FFF for all three and marks both values "TODO: verify!"
  (`zxsp/Source/Uni/ZxInfo/z/info_tc2048.h:45-47`, `info_ts2068.h:46-48`).
- Not creatable in unreal-ng. Needs a hardware measurement before implementation.

### 5.5 Harlequin

Chris Smith's open 48K design reproduces the ULA, contention included. Tested with fusetest, floatspy and
contention-sensitive demos, which "all worked as intended ... confirming that the CPLD implementation and its
dynamic RAM interface demonstrates a true 48K ZX Spectrum ULA behaviour" (zxdesign.info/harlequin.shtml).
It behaves as a 48K.

### 5.6 Leningrad-1

Conflicting secondary sources. ru.wikipedia ("Клоны ZX Spectrum"): "Такое же торможение процессора в
адресах с #4000 по #8000" ("the same CPU slowdown at addresses #4000 to #8000"). ZXMAK2 instead models
**Even M1 on every fetch** and no contention (`ZXMAK2/src/ZXMAK2.Hardware/Clone/UlaLeningrad.cs:19-29`,
`CPU.Tact += CPU.Tact & 1`). No schematic-level description found. Open question; not creatable in
unreal-ng.

## 6. Scorpion in detail

### 6.1 How the Scorpion shares memory

The Scorpion's video logic is discrete: a counter chain clocked by 7 MHz (DD3 ИЕ7) produces phase signals H0 and
H1. The DRAM row strobe for the CPU is taken from H0 (`RAS_.D = H0`), and the video reads in the other
phase. So in normal (3.5 MHz) mode the CPU's data read and write slots always exist, and **there is no
memory contention and no I/O contention on any board**. Every emulator agrees; unreal-ng's rule for the
Scorpion is `none`.

### 6.2 The Even M1 WAIT: what the logic says

The best primary source found is Scorpion's own EPLD equations for the turbo board, a JED file
reverse-compiled to ABEL and posted on zx-pk.ru. The header reads "MODULE SC15_1 … TITLE TURBO PLATA VER2.0 …
AUTHOR ZS COMPANY SCORPION DATE 01.01.96 … device '85c220'" (post #40 by deathsoft,
https://zx-pk.ru/threads/940-scorpion-zs-256-turbo-(skhema)/page4.html). The WAIT equation, with `TRB.Q` =
turbo on and `RAM_` = active-low "memory read from RAM":

```
WAIT_.D = IORQ_&M1_&!H0&!H1M # IORQ_&!WR_EN&RAM_ # H0&!H1M&!TRB.Q # M1_&!TRB.Q
        # IORQ_&!M1_&H0&!H1M&!WAIT_.Q # !H0&H1M&!TRB.Q # RAM_&!TRB.Q # Pin13.Q
```

`WAIT_` is active low, so the CPU is released whenever any product term is 1. In normal mode (`TRB.Q = 0`)
the terms `M1_&!TRB.Q`, `RAM_&!TRB.Q`, `H0&!H1M&!TRB.Q` and `!H0&H1M&!TRB.Q` release it for:

- every cycle that is not M1 (`M1_ = 1`): **data reads, writes and I/O never wait**;
- every cycle that does not read RAM (`RAM_ = 1`): **fetches from ROM never wait**, nor does the interrupt
  acknowledge (an M1 with IORQ, no MREQ read);
- every M1 from RAM whose phase already fits (`H0 ≠ H1M`).

What is left is **an opcode fetch from RAM that starts in the wrong phase**. It gets one wait state, and in the
next T the phase fits. The net effect: every fetch from RAM starts on an even T-state, at a cost of 0 or 1 T.

Why only M1: the Z80 samples the data bus at a different point in an M1 cycle (on the rising edge of T3) than
in a data read (on the falling edge of T3). If the CPU's memory slot is fixed to one clock phase, the ordinary
read fits either way, but the fetch only lines up on one parity. This is our reading of the equations, not
something a Scorpion document states. Confidence: low for the reason, medium-high for the behavior.

The `RAM_` signal is a function of the chip selects, not of the address. It covers RAM mapped at #0000
(`#1FFD` bit 0) and excludes ROM. This agrees with the one independent reading of the yellow board's
schematic. molodcov_alex in the ScorpEvo thread, post #86
(https://zx-pk.ru/threads/13345-scorpevo-(scorpion-zs-na-baze-zx-evolution)/page9.html): "если идет обращение к
озу, а M1 == 0, то формируется wait на один такт" ("if there is a RAM access and M1 = 0, a one-clock wait is
generated"). He hedges it with "чтоле?" ("or so?").

### 6.3 Which boards

| Board | Even M1 | Evidence |
|:--|:--|:--|
| Yellow (1991-92) | yes, on RAM fetches (probable) | the yellow schematic as read in the ScorpEvo thread #86; ZXMAK2 models it on the yellow only |
| Green | yes, per the SC15.1 equations in normal mode (the green board carries the turbo EPLD as standard) | spensor, thread 940 post #34: "прошивки и схемы включения было минимум две — под доработку желтого Скорпа и штатная для зеленого" ("at least two firmwares and wiring schemes: one for the yellow-board modification, one standard for the green") |
| Turbo+ upgrade of a yellow board | as green: the EPLD provides it | deathsoft, post #38: the yellow-board EPLD differs on pins 3 (/RAS), 4 (/TRB_OFF) and 5 (/TRB_ON) |
| 2007 re-creation GAL "TURBO 15.3" (`Scorpion256TPlus/GAL/turbo.jed`) | **no**: M1 (pin 10) appears in no product term of the decoded fuse map, and WAIT is always released in normal mode | decoded with MAME jedutil's 22V10 map in this research; `RAS_ := H0` and the CPU clock match the ABEL, which checks the map. The two write-ups in that repo (`doc/turbo.md`, `doc/files/Scorpion_Turbo_Mode.md` §2.2) use different column maps and contradict each other |
| Early boards | unknown | deathsoft, post #35: "Первые скорпы были сделаны по какойто упрощенной схеме, где многие задержки выставлялись RC цепочками" ("the first Scorpions used a simplified circuit where many delays were set by RC chains") |

Software authors confirm the effect on real machines. introspec, "Тайминги Pentagon 128" thread
(https://zx-pk.ru/threads/21212-tajmingi-pentagon-128/page3.html, #25): "Не работает на Scorpion c Even M1 …
На скорпионе … всегда получается чётный такт на выходе из halt" ("does not work on a Scorpion with Even M1 …
on a Scorpion the T-state is always even on exit from HALT").

### 6.4 Worked example

Code in RAM at #8000, starting on an even T-state:

| Instruction | Length on a Pentagon | Next fetch starts on | Wait | Length on a Scorpion |
|:--|--:|:--|--:|--:|
| `NOP` | 4 | even | 0 | 4 |
| `LD A,n` | 7 | odd | 1 | 8 |
| `INC HL` | 6 | even | 0 | 6 |
| `LD A,(IX+d)` (`DD` prefix M1 4 T + M1 4 T + 11 T) | 19 | odd | 1 | 20 |
| `OUT (n),A` | 11 | odd | 1 | 12 |

The same code in ROM, for example the 48 BASIC ROM, runs at Pentagon speed. Because every prefix M1 is 4 T,
a prefixed instruction whose first fetch is even has its second fetch even too. So "round each instruction
in RAM up to an even length" gives the same result as "align each M1 from RAM", as long as execution stays
in RAM.

### 6.5 Turbo (Turbo+ board, green, GMX)

- **Switching.** An `IN` from the #7FFD decode turns turbo on; an `IN` from #1FFD or a reset turns it off. The
  byte read is meaningless. The flip-flop DD9 feeds the EPLD's TRB input. Sources: MAME
  `mame/src/mame/sinclair/scorpion.cpp:635-640`, Xpeccy `src/libxpeccy/hardware/scorpion.c:78-86`. The
  Scorpion ROM leaves turbo on, which this project's probe had to work around
  ([test-programs.md](test-programs.md) §3.7).
- **Waits in turbo.** The CPU clock becomes 7 MHz (`CLK_CPU = CLK_7MHZ & TRB.Q # RAS_.Q & !TRB.Q`). RAM
  reads and writes now wait unless `H0 = 0` and `H1M = 0`, where `H1M = BORDER_ & H1` in turbo. During the
  border the CPU gets twice the slots it gets during the paper. Writes go through a posted-write latch
  (DD38 ИР22), so most of the cost is on reads and fetches. So **the turbo Scorpion is screen-contended in its
  own way**: a pattern tied to the paper area, not the Sinclair one. Confidence: medium (our analysis of the
  equations; no timing measurement found).
- Not to be confused with the "TURBO" modification in Oberon #3 (1997, DR.DEATH), a cut on DD4 pin 15 that
  makes each line 228 T. It changes the frame, not the clock.

### 6.6 How emulators model it

| Emulator | What it does | Against the circuit |
|:--|:--|:--|
| ZXMAK2 / Kozynax, yellow (`ZXMAK2/src/ZXMAK2.Hardware/Scorpion/UlaScorpionYellow.cs:17-23, 55-58`) | on an M1 at #4000-#FFFF: `CPU.Tact += CPU.Tact & 1` | closest. It uses the address instead of the RAM select, so RAM at #0000 (`#1FFD` bit 0) is missed |
| ZXMAK2, green (`UlaScorpionGreen.cs`) | no Even M1; 70784 T frame | disagrees with the SC15.1 equations |
| MAME, `scorpio` (`scorpion.cpp:275-297`, M1 map `:359-364`, `m_is_m1_even = 1` at `:385`) | every M1, **ROM included**: `if (total_cycles & 1) eat_cycles(1)` | over-applies to ROM. Also inherited by the `profi`, `kay1024`, `quorum` and `bestzx` entries (same state class), an artifact |
| MAME, `scorpiontb` / `scorpiongmx` (`:597`) | Even M1 off; turbo doubles the clock, no turbo waits | misses the normal-mode Even M1 and the turbo slot waits |
| Xpeccy / xpeccy-plus (`Xpeccy/src/libxpeccy/spectrum.c:577-579`; `xpeccy-plus/res/machines/scorp.conf` `scrp.wait = yes`) | after each instruction, rounds an odd T count up to even | equal to per-M1 alignment for code in RAM (§6.4); wrong for ROM code and for interrupt entry |
| Unreal Speccy (0.39 and the NedoPC fork) | `EvenM1` in the ini and the `SCORPION` preset; `temp.evenM1_C0 = conf.even_M1 ? 0xC0 : 0` (`pentevo/tools/unreal_fix/0.39.0/nedopc/draw.cpp:825`) | the mask means "PC ≥ #4000" (any of A15, A14 set), the ZXMAK2 rule. No reader of the field was found in the trees checked |
| zxpoly (`zxpoly-emul/.../ZxPolyModule.java:486, 765-773`) | aligns every M1 when the timing profile says so; the flag is set for its `SPECTRUM128` profile, not a Scorpion | unrelated to the Scorpion |
| pico-spec | no Scorpion machine (Scorpion ROMs only); Pentagon and Profi uncontended | - |

## 7. Pentagon family

- The designer-era FAQ says: "128k of NOT-CONTENDED memory (no slow areas)"; "Z80 works at 3.5MHz (14MHz
  crystal, 14E6/4)"; "71680 t-states per each video frame, 320 lines, 224 t-states per line"
  (zxspectrum.hal.varese.it/static/documenti/pentagon.txt, lines 85-88, 136-139, 484-487).
- No WAIT and no M1 alignment. The CPU and the video read the same DRAM in fixed slots derived from the 14 MHz
  clock. A document describing the slot order was not found; the "no contention" part is not in doubt.
- Pentagon 512 / 1024SL add memory and a 7 MHz turbo. No turbo wait is documented.

## 8. Profi and Karabas-Pro

- **Profi (original).** No contention, by consensus of emulators and of this project's Profi research
  ([technical-design.md](../2026-09-21-profi/technical-design.md) around line 198). No original schematic
  was read.
- **Karabas-Pro** is an FPGA re-creation, so its RTL shows its own design, not necessarily the original's:
  - WAIT_n is tied high (`karabas_pro.vhd:1200`); delays are made by gating the CPU clock.
  - Contention exists only in the optional "classic" screen mode (`kb_screen_mode = "01"`), at 3.5 MHz, outside
    DS80 mode (`karabas_pro.vhd:1206`). It covers #4000-#7FFF whatever page is there, and even ports
    (`memory.vhd:273-284`).
  - At 14 MHz a counter stretches every MREQ cycle, "400нс вейта проца для работы периферии в турбе" ("400 ns
    of CPU wait so that peripherals work in turbo", `karabas_pro.vhd:1213-1232`).
  - Turbo falls back to 3.5 MHz for a while after each FDC access (`:1207-1211`).
  - Video uses the second RAM slot of each 7 MHz cycle, so the default (Pentagon) mode never stalls the CPU
    (`memory.vhd:196-210`).

## 9. ATM Turbo and ZX-Evo

### 9.1 ATM Turbo 2+ (v7.10) and ATM Turbo 1 (4.50)

- Turbo 7 MHz by port #77 D3. The only documented WAIT is the keyboard: on `IN A,(#FE)` "процессор
  останавливается сигналом WAIT" ("the CPU is stopped by the WAIT signal") until the 8031 keyboard
  controller answers (`zx-evo-docs/ATM/atm2_arch.pdf`, port #FE section).
- No memory contention in either speed. The discrete DRAM interleave is not described in the documents found.
- ATM Turbo 1: nothing specific found; no emulator models a delay.

### 9.2 ZX-Evo BaseConf (the ATM3 configuration)

Read from the current released RTL (`pentevo/fpga/base_trdemu/trunk`):

- **The DRAM arbiter** (`dram/arbiter.v:55-86`) works in blocks of 8 DRAM cycles. The video takes 1, 2, 4 or 8
  of them, spread out so the CPU gets a cycle as soon as possible. BaseConf's modes need 1/8 (ZX modes) or 1/4
  (the others) of the bandwidth (`video/video_modedecode.v:146-149`). At 3.5 and 7 MHz the CPU stalls only
  when no cycle is left in the block (`zmem.v:305`), which these bandwidths never cause in practice.
- **Emulated Sinclair contention** (`z80/zclock.v:265-282`): in the 48K and 128K rasters (chosen in the AVR
  setup) and **only at 3.5 MHz**, the clock is stalled with the 48K pattern
  (`video/video_sync_h.v:250-283`: contended in 6 of every 8 T). The contended addresses are #4000-#7FFF, plus
  #C000-#FFFF with an odd page in the 128K raster. Even ports are contended too. The comment says "only 48k by
  now, TODO 128k pages and +2a/+3", and the +2A/+3 pattern is marked as probably incorrect. In the default
  Pentagon raster there is no contention.
- **14 MHz**: variable waits per access, from the table in `zmem.v:254-275`. An M1 waits 3-6 fclk (28 MHz)
  and a read 2-5 fclk, depending on the DRAM phase; writes do not wait. External I/O drops to 7 MHz
  (`zclock.v` header: "14MHz rulez: 1. do variable stalls for memory access. 2. do fallback on 7mhz for
  external IO accesses. 3. clock switch 14-7-3.5 only at RFSH").
- **Other stalls**: a short stall when the DOS ROM switches in or out; WAIT ports #BFF7 / #BEF7 (Gluk
  clock) and #xxEF (RS-232), held until the AVR services them (`z80/zwait.v:57-79`).

### 9.3 ZX-Evo TS-Conf

Same arbiter family, with more clients (TS sprites and tiles, TM, DMA) and a CPU priority that can be lowered
(`zx-evo-tsconf/pentevo/fpga/current/dram/arbiter.v:40-50`). At 3.5 and 7 MHz the CPU stalls only if the
video and DMA leave no cycle (`cpu_next`, `:170-190`). At 14 MHz a cache hit costs nothing and a miss waits for
the next DRAM cycle (`z80/zmem.v:132-208`). Unreal Speccy's TS-Conf models the miss
(`zx-evo-unreal/Unreal/vars.cpp:55-80`). No Sinclair-style contention.

## 10. Other clones

- **Kay-1024 (NEMO).** Designer article (zxpress.ru/article.php?id=15217): "Без'WAIT'овый режим в NORMAL
  (3.5MHz) дает потенциальную возможность работы в режиме multicolor" ("the WAIT-free NORMAL (3.5 MHz) mode
  makes multicolor possible"). This implies the older Kay-256 did insert waits at 3.5 MHz. In turbo, IORQ is
  stretched ("В KAY-256 IORQ удлинялся от полутора до двух раз": "on the Kay-256 IORQ was stretched 1.5 to 2
  times"; stabilized on the 1024). The effective clock in RAM is 6.3-7.0 MHz in turbo, a sign of
  arbitration losses. The CPU is not contended at 3.5 MHz on the Kay-1024.
- **Quorum.** Nothing found on contention or turbo waits. KiCad schematics exist
  (github.com/UncleRus/quorum-reborn) and would settle it.
- **Sprinter.** Z84C15 at up to 21 MHz. Video RAM is separate. Main DRAM is clocked at 7 MHz, so accesses
  to it wait, and the 64K fast RAM "makes it possible to run the CPU cycles without waiting" (Aspect #3
  review). MAME computes the waits in `sprinter.cpp:1720-1730` (turbo only).
- **ZX Spectrum Next** (comparison). Contention is on only when not disabled by NR 08, not in Pentagon timing,
  and at 3.5 MHz (`zxnext.vhd:4481`). It covers bank 5 in 48K timing, odd banks in 128K timing and banks ≥ 4
  in +3 timing (`:4489-4492`). 48K / 128K timing stretch the clock; +3 timing uses WAIT on memory only
  (`zxula.vhd:587-600`). At 28 MHz every memory read from SRAM or the bank-5 BRAM gets one wait; writes, refresh
  and I/O do not (`zxnext.vhd:3171-3181`). All as cited in `jnext/src/memory/contention.cpp:106-110, 318-362`
  and `jnext/src/memory/mmu.h:1548-1561`.

## 11. Which machines should have which rule

| Rule | Machines |
|:--|:--|
| Ferranti ULA 48K | 48K, Harlequin, Timex TC2048 / TS2068 (with their own onset; to be measured), ZX-Evo BaseConf in the 48K raster at 3.5 MHz (partial), Next in 48K timing |
| Ferranti ULA 128K | 128K, +2 grey, Next in 128K timing; BaseConf 128K raster (partial) |
| Gate array | +2A, +3, Next in +3 timing |
| Even M1 on RAM fetches | Scorpion ZS-256 (yellow, green, Turbo+ in normal mode) |
| Slot waits in turbo | Scorpion Turbo+ at 7 MHz (paper-dependent), ZX-Evo at 14 MHz, Karabas-Pro at 14 MHz, Sprinter, Next at 28 MHz, Kay in turbo |
| None | Pentagon 128 / 512 / 1024, Profi, ATM Turbo 1 / 2+, ZX-Evo (Pentagon raster), TS-Conf at 3.5 / 7 MHz, Kay-1024 at 3.5 MHz, Quorum (unverified) |

## 12. What unreal-ng models today vs this

| Model | unreal-ng rule (`UlaContention::GetRule`, set in `core/src/emulator/video/screen.cpp:555-610`) | Other delays | Verdict |
|:--|:--|:--|:--|
| 48K | `ula48`, M1 + data + internal cycles + 4 I/O patterns | - | matches |
| 128K, +2 | `ula128` | - | matches |
| +2A, +3 | `gatearray`, MREQ only, 129 T window | - | matches |
| Pentagon (128 / 512) | `none` | - | matches |
| **Scorpion, ProfScorp** | `none` | **Even M1 configured but not applied** | memory: matches. Even M1: missing |
| Profi | `none` | - | matches the original Profi. Karabas-Pro's "classic" mode is not modeled, and does not need to be |
| ATM710 | `none` | turbo modeled as a clock rate only | matches at 3.5 / 7 MHz |
| ATM3 (ZX-Evo BaseConf) | `none` | 14 MHz waits not modeled; the optional 48K / 128K raster contention not modeled | matches the default Pentagon raster |
| TS-Conf (`data/configs/ts-conf`) | `none` | `tsconf.h:245` has a `cache_miss` field; the 14 MHz wait is not applied | matches at 3.5 / 7 MHz |

**The Even M1 path in detail.**

- `data/configs/scorpion/unreal.ini:116` and `data/configs/profscorp/unreal.ini:116` set `EvenM1=1`.
  `core/src/emulator/config.cpp:268` reads it into `config.even_M1`.
- The CPU tests a different field: `core/src/emulator/cpu/z80.cpp:440-441`

  ```cpp
  if (cpu.pch & temporary.evenM1_C0)
      cpu.tt += (cpu.tt & cpu.rate);
  ```

  `temporary.evenM1_C0` (`core/src/emulator/platform.h:788`, "C0 for scorpion, 00 for pentagon") is only
  zero-initialized (`emulatorcontext.cpp:17`). Nothing copies `even_M1` into it; Unreal Speccy did that in
  `draw.cpp` (`temp.evenM1_C0 = conf.even_M1 ? 0xC0 : 0x00`), which was not ported. **So the Scorpion runs
  without Even M1 today.**
- If it were set, the mask `0xC0` against the high byte of PC means "PC ≥ #4000" (A15 or A14 set), not "PC ≥
  #C000". That is the ZXMAK2 rule. The adjustment `tt += tt & rate` adds one CPU T-state when `tt` is odd
  at the current rate, so it also works at 7 MHz. On the real Turbo+ board, though, turbo uses the slot waits
  of §6.5 instead.
- Differences from the circuit: the decision should follow "fetch from RAM" (the slot's mapping), not the
  address, so RAM at #0000 (`#1FFD` bit 0) is included and ROM is not. The check happens before the whole
  `m1_cycle`, for the first M1 only; prefixed instructions' second M1 is already even, so this is enough for
  `DD` / `FD` / `CB` / `ED`. Interrupt acknowledge does not wait. The `HALT` loop fetches from RAM when the
  `HALT` is in RAM, and each of its 4 T fetches stays even.
- A natural home in the new design is the bus interface selector of [design.md](design.md) §6: a Scorpion
  interface whose fetch wrapper adds `tt & rate` when the slot holds RAM. Machines without it keep paying
  nothing (R1).

## 13. Open questions

1. **Scorpion yellow vs green.** The SC15.1 equations (turbo board, 1996) include Even M1; the 2007 GAL
   re-creation does not. Which one reflects a stock green board? A real-hardware measurement settles it:
   run a `LD A,n` loop from RAM and from ROM and time a frame. The `ctprobe` suite ([test-programs.md](test-programs.md)
   §3.7) can grow a case for it.
2. **Even M1 in turbo.** Does the turbo slot scheme still align M1, or do the ordinary slot waits replace it?
   The equations suggest the latter; no measurement found.
3. **Turbo wait pattern during the paper**, exactly: needs a simulation of the SC15.1 equations or a hardware
   trace. Relevant only if 7 MHz Scorpion timing is ever required.
4. **Leningrad-1:** contention like the 48K (ru.wikipedia) or Even M1 (ZXMAK2)? Needs its schematic.
5. **Timex SCLD:** the onset and whether I/O is contended. zxsp's values are marked unverified.
6. **Quorum, ATM Turbo 1, Pentagon 1024SL turbo:** no primary description of the memory slots was found.
   "No contention" is the consensus of emulators, not a documented fact.
7. **MAME's** Even M1 on ROM fetches and on the Profi, Kay and Quorum entries looks like an artifact of sharing
   one state class. A MAME issue could be raised once item 1 is settled.
8. Sources that could not be reached: nedopc.org (connection refused), speccy.info (blocks automated access),
   some zx-pk.ru archive URLs (404; the `/threads/` URLs work), zxdesign.info subpages on ULA timing.
