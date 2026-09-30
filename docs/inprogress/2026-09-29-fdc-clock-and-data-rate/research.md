# FDC clock vs. data rate: "turbo VG93" and Sprinter HD floppies

Research note, 2026-09-29. Scope: what happens when the WD1793 / KR1818VG93 floppy
controller ("VG93") is clocked at 2 MHz instead of 1 MHz on Spectrum-family machines,
and what unreal-ng must model. No code was changed.

> **Status (2026-09-29): implemented** in commits `64756638` and `f304dde1`. The text below is the
> research as written; §5 describes the code *before* those commits. The landed design differs in
> naming: the policies are `Fixed1MHz`, `AutoStepTurbo` and `Latched` (in place of the four proposed
> in §5 (b)), the head settle is now 30 ms at 1 MHz, and the rate check is always on. What landed,
> the follow-ups and the open questions: [DONE.md](DONE.md). The model as built:
> [WD1793_Clock_And_Data_Rate.md](../../WD1793/WD1793_Clock_And_Data_Rate.md).

Source conventions: repository files are given as repo-relative paths
(`core/src/...`); the local emulator/RTL corpus is referred to as `emulators/...`;
web sources by URL. The full list is in section 7.

---

## 1. Summary

1. **The controller clock and the disk data rate are two separate things.** The clock
   (CLK, pin 24) paces the chip's own timers: step rate, head settle, and the bit rate
   the chip *writes*. The bit rate it *reads* comes from the external data separator
   (the RCLK / RAW READ pins), which the board builds from its own oscillator. [DS p.3, p.7, p.18-19]
2. **Doubling CLK halves the step times (6/12/20/30 ms -> 3/6/10/15 ms) and the head
   settle time (30 ms -> 15 ms).** It is not "15 -> 7.5 ms"; 15 ms is already the 2 MHz
   figure. [DS p.6]
3. **The owner's recollection is correct in outcome, but the mechanism is different.** On
   standard clones, "turbo VG" is used only for head positioning. It is switched
   **automatically by hardware**, not by software:
   - The STEP pulse puts CLK at 2 MHz.
   - The first DRQ (ZX-Evo, Karabas-Pro) or the write gate (Pentagon mods) puts it back
     to 1 MHz.
   - TR-DOS is unchanged; no ROM or patch toggles it. [RTL: ZX-Evo, Karabas, Sprinter; articles]
4. **Why data transfer stays at 1 MHz:**
   - **Writing** at 2 MHz lays down a 500 kbit/s track on a 250 kbit/s disk. That
     destroyed disks with the first Pentagon turbo mod (Spectrofon #10).
   - **Reading** is unaffected either way, because the separator stays at 250 kbit/s.
   - It is **not** mainly a Z80-speed limit. With a DD disk there is simply no 500 kbit/s
     stream to read.
5. **Real HD (1.44 MB) needs three things doubled together:** CLK (2 MHz), the separator
   (500 kbit/s), and an HD drive/medium. Sprinter Sp2000 does all three through a PLD
   latch (port #BD with A13, i.e. `LD A,#21 : OUT (#BD),A`). The Pentagon "AXLR" HD mod
   does the same with port #FF bit 7.
6. **A 3.5 MHz Z80 cannot serve HD.**
   - At 500 kbit/s a byte arrives every 16 µs = 56 T-states at 3.5 MHz.
   - The fastest TR-DOS transfer loop costs 58 T per byte, so DRQ is missed and Lost Data
     is set.
   - At 7 MHz the budget is 112 T, which works. AXLR: "без турбы это не работает"
     ("without turbo it does not work").
7. **Rate/medium mismatch means no address marks are recognized, which ends in Record
   Not Found** after the revolution limit. The Sprinter BIOS relies on exactly this: it
   issues READ ADDRESS, flips the density on timeout, and retries.
8. **Lost Data** is set when the CPU has not taken the previous byte before the next
   one is assembled.
9. **unreal-ng today** models only a fixed 1 MHz clock and 250 kbit/s (FM/MFM). It uses
   15 ms instead of the datasheet's 30 ms settle, and has no clock-scale or data-rate
   parameter. Details and gaps are in section 5.

---

## 2. The physics in plain terms

### 2.1 Four independent quantities

| Quantity | Set by | What it affects |
|---|---|---|
| **Controller clock (CLK)** | The board's clock divider feeding VG93 pin 24 (1 MHz for 5.25"/3.5" "mini" drives, 2 MHz for 8" drives per the datasheet) | Step pulse spacing, head settle delay, write-data bit timing, write-precompensation timing, the RCLK period the chip *expects* |
| **Read data rate (separator)** | The external data separator ("ФАПЧ" / digital PLL) that turns disk pulses into RAW READ + RCLK | Which bit rate the controller can decode when reading |
| **Recorded bit rate on the disk** | Whoever wrote the track (the controller's write clock at the time) and what the medium can hold (DD vs HD coating) | What the separator must match |
| **Rotation speed (rpm)** | The drive (3.5" DD/HD: 300 rpm; 5.25" HD: 360 rpm) | Bytes per track, index pulse period, the time limits counted in revolutions |

Datasheet evidence:
- The pin description says "CLK … 2 MHz for 8" drives, 1 MHz for mini-drives" [DS p.3].
- "When the clock is at 2 MHz, the stepping rates of 3, 6, 10, and 15 ms are obtainable.
  When CLK equals 1 MHz these times are doubled" [DS p.6].
- "…an additional 15 milliseconds of head settling time … this time doubles to 30 ms for
  a 1 MHz clock" [DS p.6].
- Write data is "a series of 500 ns pulses in FM … and 250 ns pulses in MFM" at 2 MHz,
  and "WRITE DATA TIMING: (ALL TIMES DOUBLE WHEN CLK = 1 MHz)" [DS p.7, p.19].

  > In other words: **the write bit rate is CLK-derived**.

- For reading, the chip needs "a Read clock (RCLK) signal … provided by some drives but if
  not it may be derived externally by Phase lock loops, one shots, or counter techniques"
  [DS p.7].

  > In other words: **the read bit rate is set outside the chip**.

- The nominal RCLK table [DS p.18] pairs them: 8" MFM at 2 MHz, RCLK period 2 µs;
  5" MFM at 1 MHz, RCLK period 4 µs.

  > In other words: the datasheet *assumes* CLK and the separator change together, but
  > nothing inside the chip forces it.

### 2.2 Worked numbers: bytes, tracks, T-states

MFM stores 1 data bit per bit cell. A byte is 8 cells.

| Mode | Bit rate | Bit cell | Byte period | Raw bytes/track @300 rpm (200 ms) | @360 rpm (166.7 ms) |
|---|---|---|---|---|---|
| FM (single density) on DD | 125 kbit/s | 8 µs | 64 µs | 3 125 | 2 604 |
| MFM DD (TR-DOS disks) | 250 kbit/s | 4 µs | 32 µs | **6 250** | 5 208 |
| MFM HD (1.44 MB) | 500 kbit/s | 2 µs | **16 µs** | **12 500** (3.5" HD) | 10 416 (5.25" HD) |

Example: 500 000 bit/s × 0.2 s = 100 000 bits = 12 500 bytes. That is enough for
18 × 512-byte sectors plus gaps (the 1.44 MB format).

**Byte period in Z80 T-states** (how long the CPU has to take one byte before the next
arrives):

| CPU clock | DD 32 µs | HD 16 µs |
|---|---|---|
| 3.5 MHz | 112 T | **56 T** |
| 7 MHz | 224 T | 112 T |
| 14 MHz (ZX-Evo, Sprinter 97) | 448 T | 224 T |
| 21 MHz (Sprinter 2000) | 672 T | 336 T |

**TR-DOS transfer loop.** TR-DOS 5.03 and 5.04T use the same bytes, found by searching
`data/rom/trdos504t.rom` and `data/rom/trdos503.rom` for `DB FF E6 C0`. The read loop
at #3FE5 is:

```
#3FE5  IN A,(#FF)   11 T   ; bit 7 = INTRQ, bit 6 = DRQ
       AND #C0       7 T
       JR Z,#3FE5    7 T (12 if looping)
       RET M         5 T   ; INTRQ -> done
       INI          16 T   ; take the byte from #7F
       JR #3FE5     12 T
```

The write loop at #3FCA is the same with `OUTI`.

- **Best case:** 11+7+7+5+16+12 = **58 T per byte**.
- **Worst case:** if DRQ rises just after the `IN`, one extra 30 T poll pass is added,
  giving about 88 T from DRQ to the byte actually moving.

What that means at each speed:

- **DD at 3.5 MHz:** 58-88 T against a 112 T budget. This works, with about 24 T to spare.
- **HD at 3.5 MHz:** the loop needs 58 T but a new byte arrives every 56 T. The CPU falls
  2 T further behind on every byte. It starts at most about 28 T ahead, so it loses a
  byte within the first ~15-30 bytes of every sector: **Lost Data, guaranteed**.
- **HD at 7 MHz:** 88 T worst case against 112 T. This works. It matches AXLR's "у Z80
  по-прежнему остается около 112 тактов на цикл обмена" ("the Z80 still has about
  112 T-states per exchange cycle") [AXLR].

### 2.3 What CLK changes, and what it does not

| Item | 1 MHz | 2 MHz | Changes with CLK? |
|---|---|---|---|
| Step rate r1r0=00/01/10/11 | 6/12/20/30 ms | 3/6/10/15 ms | yes [DS p.6] |
| Verify settle (Type I, V=1) | 30 ms | 15 ms | yes [DS p.6] |
| E-flag delay (Type II/III, E=1) | 30 ms | 15 ms | yes. "E = 15 ms Delay (2MHz)" [DS p.8]; the flowchart footnote says "If TEST = 1 AND CLK = 1 MHZ 30 MS DELAY" |
| Step pulse width | 4 µs (MFM) | 2 µs (MFM) | yes, but irrelevant to software |
| Write bit rate (MFM) | 250 kbit/s | 500 kbit/s | **yes** [DS p.7, p.19] |
| Read bit rate | whatever the separator delivers | same | **no** (external RCLK) [DS p.7] |
| Timeouts counted in index pulses (RNF after 4-5 revolutions, head unload after 15 revolutions) | revolutions | revolutions | no, they depend on rpm |
| Data address mark window (bytes after the ID CRC) | bytes | bytes | no, counted in bit cells from RCLK |

Worked example, seek over 40 tracks with r1r0=00 and verify on:
- At 1 MHz: 40 × 6 ms + 30 ms settle = **270 ms**.
- With turbo VG: 40 × 3 ms + 15 ms = **135 ms**.

Positioning is the only part that gets faster. Reading a 16-sector track still takes at
least one revolution (200 ms) either way.

### 2.4 Rate / medium mismatch

The separator locks onto pulses spaced at its nominal cell. A DD track read with a
500 kbit/s separator, or an HD track read with a 250 kbit/s one, produces a garbage bit
stream. The A1/sync pattern (MFM `0x4489`) never appears, so no ID address mark is
recognized. The command then ends with **Record Not Found** (Type II/III) or **Seek
Error** (Type I verify) after the revolution limit.

MAME behaves this way implicitly: its PLL cell comes from the scaled clock, and it has no
special case (`emulators/github/mame/src/devices/machine/wd_fdc.cpp:2496-2505`, sync
compare at `:1829`). The Sprinter BIOS depends on this outcome for its density probe
(section 3).

A write mismatch is worse. Writing with a 2 MHz CLK onto a DD track puts a 500 kbit/s
sector into a 250 kbit/s track. The DD separator cannot read that sector back. The write
splice also no longer lines up with the neighboring ID/gap bytes. This is the documented
disk-killing failure of the first Pentagon turbo mod [Spectrofon #10 via TSLabs].

### 2.5 CPU service deadline: Lost Data

- **Read:** "If the Computer has not read the previous contents of the DR before a new
  character is transferred that character is lost and the Lost Data Status bit is set.
  This sequence continues until the complete data field has been inputted" [DS p.11]. The
  command is **not** terminated.
- **Write Sector:** if the first DRQ is not served before the data field starts, "the
  command is terminated and the Lost Data status bit is set". If a later byte is missed,
  "a byte of zeros is written on the disk. The command is not terminated" [DS p.12].
- **Write Track:** if no first byte arrives by the index pulse, the command terminates
  with Lost Data. Later misses write zeros [DS p.14].

---

## 3. Per-machine table

| Machine | How turbo VG is switched | What is doubled | Used for | Software involvement | Sources |
|---|---|---|---|---|---|
| **Pentagon 128 (stock) / Beta 128** | none | nothing; CLK = 8 MHz / 8 = 1 MHz | - | - | MAME `beta_m.cpp:317`; KoE Pentagon 1024SL v2.2 CPLD `emulators/svn/KoE_projects/pentagon_2.2/CPLD/p1024sl2.tdf:47-59` (fixed 1 MHz CLK, RCLK = 250 kHz) |
| **Pentagon + "turbo VG" mods** (Spectrofon #10/#12/#14, 1995; Black Crow #02, 1998) | Hardware multiplexer (КП11) choosing 1 MHz or 2 MHz onto pin 24. Controlled by a flip-flop driven by VG93 pins: WG/WSTB pin 30, WF/VFOE pin 33, later DRQ | CLK only; the separator is unchanged | Stepping and settle, plus reading at 2 MHz CLK. Drops to 1 MHz **before writing** | None ("Формат чтения и записи при этом остаётся прежним" — "the read and write format stays the same") | Spectrofon #12 (Larkov) zxpress.ru/article.php?id=4543; Black Crow #02 zxpress.ru/article.php?id=18101; TSLabs forum t=644 |
| **Pentagon 1024SL 1.4** | Inherited the raw Spectrofon #10 scheme (switched by the write strobe) | CLK | Stepping, but **writes corrupted disks** | none | TSLabs forum t=644 |
| **Pentagon + AXLR HD mod** (Deja Vu #07/#09, 1999) | Port #FF bit 7 (0 = normal, 1 = HD). Coexists with a turbo-VG mod ("При позиционировании на доработку TURBO-ВГ всегда идет 2МГц" — "with the TURBO-VG mod, 2 MHz is always used during positioning") | CLK **and** separator (adds a 16 MHz oscillator for the 556РТ4 PLL) | Real HD data, 1.44 MB (18×512) / 1.6 MB (10×1024) | Needs CPU turbo 7 MHz; old software keeps bit 7 = 0 | zxdn.narod.ru/hardware/dv07vghd.htm, dv09fdhd.htm |
| **ZX-Evo BaseConf / TSConf** (real VG93, CLK/RCLK/RAWR from the FPGA) | Automatic: STEP rising edge sets `turbo_state`, first DRQ rising edge clears it. No port bit | CLK only (28 MHz/7/4 = 1 MHz, or /2 = 2 MHz). RCLK stays fixed at 250 kHz (56-count loop at 28 MHz) | Stepping and Type I verify/settle | none | `emulators/github/zxevo.pentevo/fpga/baseconf/trunk/vg93/vg93.v:118-167`, `fapch_zek.v`, `fapch_counter.v:48`; TSConf `emulators/github/zx-evo-tsconf/pentevo/fpga/current/vg93/vg93.v:78-84`; schematic note "STEP goes to FPGA (for turbo-VG)" (`emulators/github/zx-evo-docs/revC/zxevo_sch_revc.pdf`) |
| **Karabas-Pro** (MB8877A) | Same automatic STEP/DRQ scheme in the CPLD. It can be **disabled** by software (port #028B bit 2 "TURBOFDC_OFF") or from the OSD (Menu+F5) | CLK only (8 MHz/4 = 2 MHz vs /8 = 1 MHz). RCLK stays 250 kHz | Stepping | on/off only | `emulators/github/karabas-pro/firmware/src/cpld/rtl/fdd_controller.vhd:65-92` (clock), `:94-130` (fixed RCLK); `firmware/src/fpga/profi/rtl/karabas_pro.vhd:987,1361`; dev manual (port #028B) |
| **Sprinter Sp2000, 720 KB mode** | PLD: `TURBING` is set by STE (step) and held until WSTB/RSTB (FDC strobes, write and read) | CLK only while positioning. The separator runs from 7 MHz (250 kbit/s) | Stepping | none | `emulators/gitlab/sprinter-computer-hard/MAX/SP2_MAX.TDF:272-306` |
| **Sprinter Sp2000, 1.44 MB mode** | `OUT (#BD),A` with A=#21. A lands on A13, so port #21BD vs #01BD, which presets or clears latch `10K_D0` = `FDD_1440` | CLK **permanently** 2 MHz (`TURBING` forced) **and** separator from 14 MHz (500 kbit/s) | Real HD data | BIOS density auto-probe; the 21 MHz CPU has 336 T per byte | `SP2_MAX.TDF:272, 285, 396-402`; doc.sprinter.ru/blocks/fdd.html:74-82; BIOS `emulators/zxgit/Sprinter-BIOS/bios/exp/EXTENDED/FDD_DRIVER.asm:597-653`; older `emulators/gitlab/sprinter-computer-bios/SETUP/FDRIVER2.ASM:155-195, 719-742` |
| **Sprinter 97** | Same HD select, but through the Z84C15 SIO channel B, WR5 = #60 (HD) / #E0 (DD) | as Sp2000 | HD | BIOS | `FDRIVER2.ASM:719-731`; "14 МГц процессор + 2 МГц на ВГ93" (zx-news.narod.ru Sprinter-97 article, search snippet only) |
| **Scorpion ZS256 (Turbo+)** | none found; "ВГ93 has its own 1 MHz clock" | - | - | - | `emulators/github/Scorpion256TPlus/doc/files/Scorpion_Turbo_Mode.md:148`, `Scorpion_FDC_Digital_PLL.md:243` |
| **ATM Turbo 2+** | none found; 1 MHz from D90 pin 12 (the 8 MHz crystal chain) | - | - | CPU-turbo WAIT note only | `emulators/svn/atmturbo/doc/ver_7_10/TURBO 2+ Assembly and Configuration Manual.doc` |
| **KAY-1024, Profi** | no evidence found in the corpus or on the web | ? | ? | ? | - |

**Verdict on the owner's recollection.** "Turbo VG on standard clones was only for head
positioning" is **confirmed**:
- Every RTL design and every article drops back to 1 MHz for data. The switch-back
  point is DRQ (ZX-Evo, Karabas, Spectrofon #14), the write gate (Black Crow), or the
  read/write strobes (Sprinter DD).
- None of them changes the separator.

"Because the Z80 cannot keep up" is **not the reason** on these machines. With a DD disk
the separator still delivers 250 kbit/s, and a 2 MHz write would corrupt the track.
The Z80 limit is real, but it only matters for true HD (AXLR mod, Sprinter), and the
AXLR mod requires 7 MHz for exactly that reason.

"Software switched it off around transfers" is **refuted**:
- On every machine found, the switch is automatic hardware.
- Only Karabas-Pro has a software *disable*, and that is a user preference, not per
  transfer.
- No TR-DOS patch in the corpus touches a turbo-VG bit.

---

## 4. What the emulators and RTL do

### 4.1 Emulators

| Emulator | Turbo / clock doubling | Data rate / HD | Lost Data | Rate mismatch | Evidence |
|---|---|---|---|---|---|
| **MAME** | `set_clock_scale()` scales the whole device clock: all delays (`delay_cycles`) **and** the PLL cell. Step table in clocks `{6000,12000,20000,30000}`. Settle `60000` clocks (60 ms at 1 MHz, disagrees with the datasheet) | The PLL cell derives from the clock (2 clocks MFM), so turbo = 500 kbit/s. Only Sprinter calls `turbo_w` (port #BD, via A13 in the DCP table). `tsconf_beta.cpp` has an unwired copy. The default Beta drive is 525qd; 35hd is an option | `set_drq()` sets `S_LOST` if DRQ is still pending | implicit: the sync pattern never matches, giving RNF | `emulators/github/mame/src/devices/machine/wd_fdc.cpp:1480-1482, 2485-2505, 2395-2403, 2842-2852`; `wd_fdc.h:116`; `mame/sinclair/beta_m.cpp:196-201, 317-320`; `mame/sinclair/sprinter.cpp:727-734`; `imagedev/floppy.cpp:477-484, 1722, 1994` |
| **Unreal Speccy** (incl. zx-evo-unreal) | "Fast" (`wd93_nodelay`, default ON) removes delays; it is not 2×. Steps `{6,12,20,30}` ms, E delay 15 ms | Byte time = `Z80FQ/(trklen*FDD_RPS)`, max 6250. No HD | `WDS_LOST` if DRQ is still pending | none | `emulators/github/unreal-speccy/config.cpp:514`, `wd93cmd.cpp:112-113, 527-529, 597-599, 276-277`, `wd93trk.cpp:51`, `wd93.h:3-6` |
| **UnrealSpeccyP** | fixed real delays (`wd93_nodelay=false`) | 6400-byte track, no HD | yes | none | `emulators/github/UnrealSpeccyP/devices/fdd/wd1793.cpp:28, 500-503`, `fdd.cpp:226` |
| **Xpeccy** | `fdcturbo` gives near-instant steps and a shortened settle (unit bug: step table in ns) | `fdc_set_hd()` sets 16 µs/byte and a 17 700-byte track, **PC only**. Spectrum models force DD | turbo holds DRQ; normal mode sets lost data | none | `emulators/github/Xpeccy/src/libxpeccy/vg93.c:8, 68-125, 235, 249`, `diskif.c:41-48`, `hardware/common.c:143` |
| **xpeccy-plus** | `fdcturbo`: 20 µs steps, skips verify settle, keeps the E settle (15 ms) | DD only | turbo lost-data after a hold | none | `emulators/github/xpeccy-plus/src/libxpeccy/wd1793.c:8-14, 80-125, 201, 334` |
| **ZXMAK2** | `NoDelay` like Unreal. Sprinter port #BD **stored but never used** | byte time = revolution / track length (grows past 6250 if needed) | `WDS_LOST` | none | `emulators/github/ZXMAK2/src/ZXMAK2.Hardware/Sprinter/SprinterFdd.cs:184, 226-233`; `Circuits/Fdd/Wd1793.cs:834-836`; `ZXMAK2.Model.Disk/Track.cs:23, 107, 123-143` |
| **pico-spec** | has a 2 MHz step table behind `kRVMWD177XCLK`, never set | DD | - | - | `emulators/github/pico-spec/src/wd1793.cpp:113-122, 292` |
| **SprintEm** | no WD1793 (high-level BIOS/DSS to host files) | - | - | - | `emulators/gitlab/sprintem/bios.cpp:72-73` |
| Fuse, ZEsarUX, SpecEmu, EmuZWin, Spectaculator | **not in corpus** (no FDC sources) | | | | |

**Consensus:**
- No Spectrum emulator in the corpus models the automatic STEP-to-DRQ turbo VG.
- Only MAME models a 2 MHz FDC, and only for Sprinter HD. There, scaling the whole clock
  (PLL included) matches the Sprinter hardware, which doubles the separator too.
- MAME's approach would be **wrong for the Pentagon/Evo/Karabas scheme**, where the
  separator stays at 250 kbit/s. It is not wired for those machines anyway.
- Emulator "fast/turbo" options are all instant-mode shortcuts, not hardware models.

**Head settle at 1 MHz — sources disagree:**
- datasheet: 30 ms
- MAME: 60 ms
- Unreal: 15 ms
- xpeccy-plus: 15 ms
- unreal-ng: 15 ms

The datasheet wins here; MAME's 60000-clock value is unexplained.

### 4.2 RTL

| Design | Turbo | Separator (RCLK) | Evidence |
|---|---|---|---|
| ZX-Evo BaseConf / TSConf / Scorpion-Evo | STEP -> 2 MHz, first DRQ -> 1 MHz, automatic | fixed 250 kHz (`fapch_zek`, 56-count loop at 28 MHz) | `vg93.v:118-167` (baseconf), `emulators/github/zxevo.pentevo/scorpevo/fpga/current/vg93/vg93.v:108-148, 276-309` |
| Karabas-Pro | same, with a software disable | fixed 250 kHz (8 MHz counter PLL) | `fdd_controller.vhd:65-130` |
| Sprinter Sp2000 | DD: STEP until R/W strobe. HD: always 2 MHz | 250 kHz (7 MHz source) / 500 kHz (14 MHz source) | `SP2_MAX.TDF:272-306` |
| Pentagon 1024SL v2.2 (KoE) | none | fixed | `p1024sl2.tdf:47-59` |
| MiSTer ZX-Spectrum `wd1793.sv`, u16/ReVerSE, ZX-Next | **not in corpus** | | |

One fact matters for unreal-ng. On ZX-Evo, a Type I verify and the ID search of a Type II
command that follows a step run with **CLK = 2 MHz and RCLK = 250 kHz**, because turbo
ends only at the first DRQ. The same holds for all reads on the Black Crow Pentagon mod.
These boards work in the field, so **reading DD data with a 2 MHz CLK and a DD separator
works in practice**, even though it is outside the datasheet's nominal RCLK table.

---

## 5. Consequences for unreal-ng

Current implementation (`core/src/emulator/io/fdc/wd1793.{h,cpp}`):

- **Timebase:**
  - Everything is in Z80 T-states at a fixed `Z80_FREQUENCY = 3.5 MHz` (`wd1793.h:554`).
  - `WD93_FREQUENCY = 1 MHz` (`:558`) is declared but only enters `WD93_CLK_CYCLES_PER_Z80_CLK` (`:559`).
  - Both step tables exist (`:600-601`), but `getPositioningRateForType1CommandMs()` always uses the 1 MHz one (`wd1793.cpp:1242-1248`).
- **Settle:**
  - Verify settle is `WD93_VERIFY_DELAY_MS = 15` (`wd1793.h:585`, used at `wd1793.cpp:2127`).
  - The E-flag delay is a hard-coded 15 ms (`wd1793.cpp:1459, 1586`).
- **Data rate:**
  - The byte cell is one revolution divided by the track's raw size (`byteCellTStates`, `wd1793.h:963-968`): 6250 bytes gives 112 T, 3125 (FM) gives 224 T.
  - Rotation is fixed at 5 rev/s (`fdc.h:12`, `fdd.h:26`).
- **Mismatch:** only FM vs MFM (port #FF bit 6) is checked (`locateSectorForType2`, `wd1793.cpp:2346-2351`). There is no notion of data rate.
- **Lost Data:**
  - Reads set `WDS_LOSTDATA` and continue (`wd1793.cpp:2584-2588`), which matches the datasheet.
  - Writes set it and **terminate** (`wd1793.cpp:2739-2747`). The datasheet terminates only on the first byte; later bytes are written as zeros and the command continues.
- **Fast-disk mode:** a separate, deliberate non-authentic shortcut ("rotation teleport", `wd1793.h:973-1008`). Out of scope here.

Now the cases:

**(a) Standard clock, DD disk.** This is the normal Spectrum case.
- Correct: step table (6/12/20/30 ms), byte cell (32 µs = 112 T), Lost Data on reads,
  and RNF timing.
- **Gap:** head settle. V=1 verify and the E=1 delay should be **30 ms** at 1 MHz
  (105 000 T), not 15 ms. The repo's own `docs/WD1793/WD1793_Timeouts.md` already
  states 30 ms at 1 MHz. Changing it alters TR-DOS timing, so it needs a check against
  the TTD fixtures.

**(b) Turbo VG clock on a DD machine with a DD disk** (Pentagon mods, ZX-Evo, Karabas,
Sprinter DD).

What real hardware does:
- STEP switches CLK to 2 MHz, so step times and the verify settle halve.
- Reading IDs and data still happens at 250 kbit/s, because the separator is unchanged.
- The clock is back at 1 MHz before any byte is written: at the first DRQ, or at the
  write gate / read-write strobe.
- So from the software's point of view **only the positioning timing changes**. Data
  timing is identical to case (a).

What unreal-ng should do:
- Add a per-machine **FDC clock policy**, independent of the data rate:
  - `Fixed1MHz`
  - `AutoTurboStepToDrq` (ZX-Evo, Karabas with its disable bit)
  - `AutoTurboUntilStrobe` (Sprinter DD)
  - `Fixed2MHz` (Sprinter HD)
- The policy scales the step and settle delays only.
- A hypothetical "2 MHz write on a DD disk" (the broken Spectrofon #10 mod) is not worth
  modeling.
- **Current gap:** no clock policy at all. Turbo-VG machines seek at half speed compared
  with the real hardware.

**(c) Sprinter HD mode with an HD disk.**
- Expected behavior: CLK is 2 MHz and the separator runs at 500 kbit/s. The medium is
  12 500 raw bytes per track at 300 rpm, so a byte arrives every 16 µs, which is 336 T at
  21 MHz.
- Needed:
  - a data-rate property on the controller (the separator);
  - a recorded-rate property on the track;
  - a byte cell derived from the rate, not assumed at 6250 bytes per revolution.
- **Current gap:**
  - no Sprinter #BD latch and no rate property;
  - `byteCellTStates()` would produce 16 µs cells only by accident, if an image supplied
    12 500-byte raw tracks;
  - whether `DiskImage` accepts such tracks and HD image formats (IMG/DSK 1.44) was not
    checked.
- The T-state constants assume 3.5 MHz T-states. **How `_time` behaves under CPU turbo
  was not verified** (section 6).

**(d) Rate/medium mismatch** (the BIOS density probe).
- Expected behavior: a controller rate different from the track's recorded rate means no
  ID is recognized. That gives RNF (Type II/III), or no DRQ/INTRQ within the BIOS polling
  timeout for READ ADDRESS. The BIOS then flips `FDD_1440` and retries: 4 attempts in
  `FDRIVER2.ASM:175-195`, and two passes ("fast" and "slow" drives) in
  `FDD_DRIVER.asm:661-708`.
- Emulation rule: treat a rate mismatch exactly like the existing FM/MFM mismatch in
  `locateSectorForType2`, and also in READ ADDRESS / verify. The track reads as having no
  marks.
- **Current gap:** only encoding is compared, not rate.

**(e) CPU too slow, giving Lost Data.**
- Expected behavior: HD at 3.5 MHz with the TR-DOS loop (58 T against 56 T) must produce
  Lost Data within each sector. At 7 MHz it must not.
- The existing `_drq_served` mechanism models this correctly, **provided** the byte cell
  is expressed in real time and converted to the current CPU clock. A 16 µs cell has to
  become 56 T at 3.5 MHz but 112 T at 7 MHz.
- **Current gap:** the byte cell and all FDC delays are computed from the constant
  3.5 MHz. If CPU turbo makes T-states run faster in real time, the FDC would speed up
  with the CPU, and this case could never fail or succeed correctly. To be verified.

Side note (out of scope, one line):
- `docs/WD1793/WD1793_Command_Type_I.md:27-34` has a step table claiming "2 MHz, FM:
  6/12/20/30 ms", taken from a column-shifted OCR. The original datasheet has no DDEN
  dependence.
- `docs/WD1793/WD1793_Timeouts.md` Q&A §1 says the data speed does not change with CLK.
  That is true for reading, but false for writing.

---

## 6. Open questions

1. **Reading at 2 MHz CLK with a 250 kHz RCLK is outside the datasheet's nominal table**
   [DS p.18].
   - Field designs show it works for ID and data reads: ZX-Evo reads IDs this way, and
     Black Crow reads everything this way.
   - Whether any CLK-counted internal timer shortens with it is not documented. Examples
     are the MFM "find address mark within 16 bytes after Read Gate" window, or VFOE
     timing.
   - Low risk for emulation: model as "reading unaffected by CLK".
2. **Sprinter `WSTB`/`RSTB` pin identity.** They are inputs to the PLD. Here they are
   read as the VG93 write-gate and read-strobe outputs, but not traced on the schematic.
   This affects only *when* DD-mode turbo ends, not data correctness.
3. **Head settle at 1 MHz:**
   - datasheet: 30 ms
   - MAME: 60 ms
   - Unreal family: 15 ms

   No Spectrum hardware measurement was found. The recommendation is the datasheet
   value, confirmed against TTD fixture timing.
4. **KAY-1024 and Profi:** no evidence either way on turbo VG.
5. **MiSTer `wd1793.sv`, ZX-Next and u16:** not in the corpus, so not checked.
6. **zx-pk.ru threads on "Турбирование ВГ93"** (t-9255 and others) now return 404; only
   search-snippet text was available. One snippet reports "2 MHz for reading, 1 MHz for
   writing", which is consistent with the Black Crow scheme.
7. **unreal-ng FDC time under CPU turbo.** `_time` comes from `emulatorState.t_states`
   plus the frame T-state (`wd1793.h:1126-1131`). Whether those are normalized to
   3.5 MHz when `current_z80_frequency_multiplier` > 1 was not checked. This decides
   whether case (e) can be modeled at all.
8. **Sprinter's VG93 clock value is not stated in any primary document.** It is derived
   here only from the PLD equations (`SP2_MAX.TDF:272-280`) plus the one Sprinter-97
   snippet. MAME's `turbo_w(1)` (a ×2 scale) agrees.

---

## 7. Sources

**Datasheet** (repository): `docs/WD1793/datasheeets/FD179X-01_Data_Sheet_Oct1979.pdf`
("DS"). Pages cited:
- p.3: pin list, CLK / RCLK / WD
- p.6: clock vs step rates and settle
- p.7: read/write operation
- p.8: flag summary
- p.11-14: command descriptions (Read/Write Sector, Write Track)
- p.18-19: input and write data timing, nominal RCLK table

Text extracted with PyMuPDF. The OCR in `docs/WD1793/datasheeets/ocr/WD179X_OCR.md:252-275`
matches, except that its Table 1 columns are shifted.

**unreal-ng:**
- `core/src/emulator/io/fdc/wd1793.h`
- `core/src/emulator/io/fdc/wd1793.cpp`
- `core/src/emulator/io/fdc/fdc.h`
- `core/src/emulator/io/fdc/fdd.h`
- `data/rom/trdos504t.rom`, `data/rom/trdos503.rom`
- `docs/WD1793/WD1793_Timeouts.md`, `docs/WD1793/WD1793_Command_Type_I.md`

**Emulator corpus:**
- `emulators/github/mame/src/devices/machine/wd_fdc.{h,cpp}`, `emu/device.cpp:398-438`,
  `mame/sinclair/beta_m.cpp`, `mame/sinclair/sprinter.cpp`,
  `mame/sinclair/evo/tsconf_beta.cpp:192-195`, `devices/imagedev/floppy.cpp`,
  `lib/formats/trd_dsk.cpp:89-90`
- `emulators/github/unreal-speccy/`, `emulators/github/zx-evo-unreal/Unreal/`,
  `emulators/github/UnrealSpeccyP/`
- `emulators/github/Xpeccy/src/libxpeccy/`, `emulators/github/xpeccy-plus/src/libxpeccy/`
- `emulators/github/ZXMAK2/src/`
- `emulators/github/pico-spec/src/`, `emulators/github/Murmulator_rp2040/src/wd1793.c`
- `emulators/gitlab/sprintem/`

**RTL / hardware:**
- `emulators/github/zxevo.pentevo/fpga/baseconf/trunk/vg93/` (`vg93.v`, `fapch_zek.v`, `fapch_counter.v`)
- `emulators/github/zxevo.pentevo/scorpevo/fpga/current/vg93/vg93.v`
- `emulators/github/zx-evo-tsconf/pentevo/fpga/current/vg93/`
- `emulators/github/zx-evo-docs/revC/zxevo_sch_revc.pdf`
- `emulators/github/karabas-pro/firmware/src/cpld/rtl/fdd_controller.vhd`,
  `firmware/src/fpga/profi/rtl/karabas_pro.vhd`, `docs/karabas-pro-dev-manual-v1_01.pdf`,
  `docs/karabas-pro-user-manual-v1-en.pdf`
- `emulators/gitlab/sprinter-computer-hard/MAX/SP2_MAX.TDF`
- `emulators/zxgit/Sprinter-BIOS/bios/exp/EXTENDED/FDD_DRIVER.asm`, `bios/exp/FUNC_SYS.ASM:340-358`
- `emulators/gitlab/sprinter-computer-bios/SETUP/FDRIVER2.ASM`
- `emulators/doc.sprinter.ru/blocks/fdd.html`, `emulators/doc.sprinter.ru/blocks/ports/map.html`
- `emulators/svn/KoE_projects/pentagon_2.2/CPLD/p1024sl2.tdf`
- `emulators/svn/atmturbo/doc/ver_7_10/TURBO 2+ Assembly and Configuration Manual.doc`
- `emulators/github/Scorpion256TPlus/doc/files/Scorpion_Turbo_Mode.md`, `Scorpion_FDC_Digital_PLL.md`

**Web:**
- AXLR, "HD на Пентагоне", Deja Vu #07 (1999): https://zxdn.narod.ru/hardware/dv07vghd.htm ; follow-up Deja Vu #09: https://zxdn.narod.ru/hardware/dv09fdhd.htm
- V. Larkov, turbo-VG93 scheme, Spectrofon #12: https://zxpress.ru/article.php?id=4543
- Black Crow #02 (1998), turbo VG93 via WSTB (pin 30): http://zxpress.ru/article.php?id=18101
- TSLabs forum, Pentagon 1024SL and the Spectrofon #10/#12/#14 turbo schemes: https://forum.tslabs.info/viewtopic.php?t=644
- FD1791 datasheet text: https://archive.org/stream/WesternDigitalFD1791Datasheet/Western%20Digital%20FD1791%20datasheet_djvu.txt
- Ivan Mak, Sprinter Sp2000 programming guide (2003), §10.1: http://winglion.ru/sprinter/sp2000.pdf
- Sinclair Club FAQ, Sprinter: https://zxpress.ru/en/ezines/sinclair-club/05/sprinter-is-a-universal-z80-based-computer-with-pld-architecture-this-faq-covers-specifications
- zx-pk.ru "Исходник турбо-диск-драйвера ВГ93" (software-only, not a clock mod): https://zx-pk.ru/threads/18727-iskhodnik-turbo-disk-drajvera-vg93.html

---

## Glossary

- **FDC**: floppy disk controller, here the WD1793 or its Soviet clone КР1818ВГ93 ("VG93").
- **CLK**: the controller's own clock input (pin 24). It paces the chip's timers and its
  write output.
- **Data separator / ФАПЧ / PLL**: the circuit that turns the drive's raw pulses into
  RAW READ plus a read clock (RCLK) that marks bit-cell boundaries. Its frequency decides
  which bit rate can be read.
- **DD / HD**: double density (250 kbit/s, 720 KB on 3.5") and high density (500 kbit/s,
  1.44 MB on 3.5").
- **FM / MFM**: single- and double-density recording methods. MFM packs twice the data
  into the same flux spacing.
- **DRQ**: "data request", the controller telling the CPU a byte is ready (or needed).
- **Lost Data**: status bit 2. The CPU missed a DRQ deadline.
- **RNF**: Record Not Found, status bit 4. No matching ID field was found within the
  revolution limit.
- **Settle time**: a wait after moving the head, before reading, to let it stop vibrating.
- **T-state**: one Z80 clock cycle (0.286 µs at 3.5 MHz).
