# ZX Profi frame timing: what the emulator sources actually say

Date: 2026-09-21. Read-only research over `/Volumes/TB4-4Tb/Projects/emulators/github/` (called `$C` below).
Follow-up to section 3.2 of `existing-emulators-review.md`, which was inconclusive.
Karabas-Pro (FPGA clone board) is deliberately not used as evidence here; it is a clone, and its own generator is not the original Profi's.

## 0. Short answer

- **Nobody has an authoritative Profi timing.** Every emulator that carries a number traces back (directly or by copying) to two sources: the UnrealSpeccy preset `PRESET.PROFI=69888,12580,224,50,28` ("thanks to DDp", ~2009, no measurement described) and ZXMAK2's `UlaProfi3XX` (paper at 56*224+39, INT 32+7, marked `TODO: needs approve`). `xpeccy-plus` and `pico-spec` say outright that their numbers are unconfirmed or copied.
- All of them agree on **224 T/line x 312 lines = 69888 T at 3.5 MHz, no contention.** This is the 48K/Scorpion frame, not the Pentagon's 71680 / 320 lines. No source states a reason for that choice; there is no comment citing a measurement of a real Profi frame.
- **Hi-res DS80 (DFFD.7): three emulators do not change any timing** (UnrealSpeccy, xpeccy, ZXMAK2 CPU side). Two change only the *drawing geometry*: ZXMAK2 (192 T/line raster, INT offset 19 T) and pico-spec, which copied ZXMAK2 and then re-calibrated it against **photos of a real Profi** (the only real-hardware evidence in the corpus, see 2.4). Frame length in T-states never changes in any of them.
- **There is no "compatibility mode" / Pentagon-vs-48K timing switch on Profi in any source.** The only variants found are: board revision (3.xx vs 5.xx, which differs in palette/extended ports, not timing), RAM size (512 vs 1024), a user-editable ULA preset in UnrealSpeccy (independent of the machine model!) and a `PROFI_TURBO` preset in Unreal_NS.

## 1. Standard mode, per emulator

### 1.1 UnrealSpeccy (0.39.0, `$C/unreal-speccy`; identical presets in `$C/zx-evo/pentevo/unreal` and `$C/pentevo/tools/unreal_fix`)

Preset line, `unreal-speccy/x32/unreal.ini:126`:

```
; PRESET.NAME=FRAME,PAPER,LINE,INT,INTLEN,EvenM1,4TBorder,floatBus,floatDOS,PortFF,   (:119)
PRESET.PENTAGON=71680,17989,224,50,32,0,0,0,0,0,320,240,24,32,384,288,48,64          (:121)
PRESET.SCORPION=69888,14344,224,50,32,1,1,0,0,1,...   ; thanks to Faster              (:122)
PRESET.ATM1_2_3.5MHz=69888,14395,224,50,32,0,0,0,0,1,... ; thanks to DDp              (:124)
PRESET.PROFI=69888,12580,224,50,28,0,0,0,0,0,320,240,24,32,384,288,48,64 ; thanks to DDp  (:126)
```

Decoded for Profi: frame 69888, paper (first-paper T from INT) 12580 = 56*224 + 36, line 224, INT freq 50, **INT length 28 T**, even-M1 off, 4T border off, floatbus off, port FF off. Border window fields 320x240 small / 384x288 full are the same as every other preset.

Facts about how this is consumed (`unreal-speccy/config.cpp`):

- `load_ula_preset()` (:198-215) reads `PRESET.<name>` and fills `conf.frame`, `conf.paper`, `conf.t_line`, `conf.intfq`, `conf.intlen`, ... Defaults when a key is missing (:397-406): paper 17989, line 224, intlen 32.
- **The ULA preset is chosen by `Preset=` (`x32/unreal.ini:131`, "don't use above settings and load preset"), not by the memory model.** Selecting `MM_PROFI` (`config.cpp:899`, ROM layout only) does not select the Profi preset. A user with `Preset=PENTAGON` and `mem model = PROFI` gets Pentagon timing on a Profi. Nothing in the source couples them.
- CPU clock is derived, not stored: `cpufq = conf.intfq * conf.frame` (`sound.cpp:211`) = 50 * 69888 = 3 494 400 Hz, i.e. 50.000 Hz frames, not 3.5 MHz / 69888 = 50.08 Hz.
- No contention for Profi: there is no `MM_PROFI` in any contention/`floatbus` path; preset `floatBus=0`.
- Original INI from 0.37.9 (`zx-evo/pentevo/unreal/Unreal/cfg/Unreal.ini:139`) has the same row with only 10 fields: `PRESET.PROFI=69888,12580,224,50,28,0,0,0,0,0 ; thanks to DDp`. So the numbers are at least as old as 0.37.x. `git log -S12580` in `pentevo`/`zx-evo` only goes back to import commits (2009-11-21 "Added some project directories", svn era); `unreal-speccy` history is "Initial code drop for version 0.38.4" so nothing older is available. The `history.txt` (`zx-evo/pentevo/unreal/Unreal/doc/history.txt:975-981`) credits *molodcov_alex* with Profi IDE, clock, memory layout "changed to match the real Profi", extended ports for 5.xx boards, palette, monochrome hi-res; it says nothing about timing.

**Unreal_NS variant** (`pentevo/tools/unreal_fix/0.39.0/Unreal_NS/release/Unreal.ini:225-226`, added in commit `f0a5a769` 2023-07-16 "+Unreal_NS", author `alone`):

```
PRESET.PROFI =        69888,12580,224,50,28,0,0,0,0,0,320,240,24,32,384,288,48,64 ; thanks to DDp
PRESET.PROFI_TURBO = 116920,12580,375,50,28,0,0,0,0,0,320,240,24,32,384,288,48,64 ; по данным левого тактометра
```

The comment reads "per data from a third-party (literally 'left') tact meter". 116920 T/frame at 50 Hz = 5.85 MHz effective, 375 T/line (375*312 = 117000, so frame is not an exact 312 multiple). This is the only "turbo" datum in the corpus and it is a rough number from an unnamed tool, not a measurement of Profi timing.

Geometry of INT vs paper in Unreal: INT at frame T=0, paper starts at T=12580 (line 56, +36 T). Small-border window (`b_top_small=24`) puts the visible top at line 32.

### 1.2 ZXMAK2 (`$C/ZXMAK2`, machines `MACHINES/PROFI3XX.VMZ` = "PROFI+ 512 [V3.XX]", `PROFI5XX.VMZ` = "PROFI+ 1024 [V5.XX]")

`src/ZXMAK2.Hardware/Profi/UlaProfi3XX.cs:126-152` (5XX inherits it):

```
// PROFI 3.2
// Total Size:          768 x 312
// Visible Size:        640 x 240 (64+512+64 x 0+240+0)
// SYNCGEN: SAMX6 (original)
timing.c_frameTactCount = 69888;	// 59904 for profi mode (312x192)
timing.c_ulaLineTime = 224;
timing.c_ulaFirstPaperLine = 56;
timing.c_ulaFirstPaperTact = 39;//42;
timing.c_ulaBorder4T = false;   // TODO: check?
timing.c_ulaIntBegin = 0;
timing.c_ulaIntLength = 32 + 7;	// TODO: needs approve
timing.c_ulaFlashPeriod = 25;   // TODO: check?
```

- Frame 69888, 224 T/line, first paper at line 56 tact 39 (= 12583 T from INT; comment shows an earlier value 42 was replaced by 39), INT length **39 T** marked "needs approve".
- Border geometry: T-based, top/bottom 32 lines, left/right 16 T (32 px).
- The header comment says the board is **Profi 3.2 with syncgen "SAMX6 (original)"** and the raster is 768 x 312. This is the only place in the corpus naming a sync-generator part; it implies a fixed horizontal total of 768 pixel-slots per line, but does not say the pixel clock. Under the 224 T/line standard mode this is 3.43 px/T (7 MHz clock = 2 px/T gives 448, so 768 must be a hi-res 12-14 MHz figure). Note 768/224 = 3.43, 768/192 = 4.0. See 2.2.
- CPU frequency is not overridden by the Profi classes (grep of `3500000|Frequency` in `Profi/*.cs`: no hits), so the machine uses the framework default 3.5 MHz.
- No contention: `UlaProfi3XX` does not enable the `UlaDeviceBase` contention path; the standard-mode `SpectrumRenderer.ReadFreeBus` is used.
- `git log` has only 35 commits and starts at "Move from Codeplex" (2018-07-20); no earlier history for these constants.

### 1.3 xpeccy-plus (`$C/xpeccy-plus`; upstream `$C/Xpeccy` has the hardware but only ships Pentagon/Scorpion layouts, its Profi hardware was added 2015-06-15 "Profi enabled. Very experimental...")

Machine `res/machines/profi.conf`: `hw = Profi`, `memory = 1024`, `cpu.frq = 3500000`, `geometry = ULA.Profi`, `ide = profi`.

Layout `res/layouts.conf:13` (units: dots, 2 dots = 1 T; `name:full.x:full.y:bord.x:bord.y:blank.x:blank.y:intSize:intpos.y:intpos.x:scr.x:scr.y`):

```
layout = ULA.Profi:448:312:64:56:64:16:64:72:64:256:192
layout = ULA.48:448:312:64:56:64:16:64:8:120:256:192
layout = ULA.ATM2:448:312:64:56:64:16:64:8:32:256:192
```

Decoded (I verified the decoding on ULA.48 and ULA.ATM2 against known values, see below): 224 T x 312 lines = 69888, INT size 64 dots = **32 T**, contention pattern 0, 4T-border off.
INT-to-first-paper = (paperRow - intRow)*224 + (paperX - intX)/2 with paperRow = bord.y+blank.y = 72, paperX = bord.x+blank.x = 128:
- ULA.48: (72-8)*224 + (128-120)/2 = 14340 (real 48K 14335, sane);
- ULA.ATM2: 64*224 + 48 = 14384 (Unreal 14395, doc says "11 T out", consistent);
- **ULA.Profi: (72-72)*224 + (128-64)/2 = 32 T after INT.**
  Xpeccy-plus therefore puts the first paper dot 32 T after INT while Unreal/ZXMAK2 put it about 12580 T (56 lines) after. That is a very large disagreement in what happens *between INT and the visible picture*, although both draw the same 312-line raster; the phase of the picture relative to the interrupt differs by ~12550 T (about 56 lines). No source explains why.

`docs/machines-reference.md:32,43-50` states it directly:

```
| Profi | 3 500 000 | Profi | 224 | 312 | 69 888 | 32 **open** | 0 (none) | no | no | no | no |
The **open** INT lengths are the ones nobody has confirmed a figure for. Three of them (Profi, ATM, TSConf) were deliberately left at 32 T ...
Also still open from that work: Profi's `intpos` puts the first paper dot 32 T after the interrupt where UnrealSpeccy's preset says 12 580 ...
Neither has an official figure behind it.
```

Git: the Profi layout row was created in `45a7faa0` (2026-08-07, "bundle ZX-compatible ROMs and romsets"); commit `c9500413` (2026-08-30, "align ZX layouts and profiles with Fuse and UnrealSpeccy") re-set Pentagon/Scorpion/ATM geometry but left the Profi row byte-identical and only added "4t-border off on ... Profi". So the Profi row was never audited against anything.

### 1.4 pico-spec (`$C/pico-spec`, "Profi 1024K", claims "100% cycle accurate")

`src/CPU.h:45,51,61-62`, `src/Video.h:62,68`:

```
#define TSTATES_PER_FRAME_PROFI 69888
#define MICROS_PER_FRAME_PROFI 19968        // 69888 / 3.5 MHz
#define INT_START_PROFI 0
#define INT_END_PROFI 39
#define TSTATES_PER_LINE_PROFI 224
#define TS_SCREEN_PROFI        12583  // START OF ULA DRAW PAPER PROFI (56*224+39)
```

These are exactly the ZXMAK2 numbers (56*224+39 = 12583, INT 39). It is a derivative, not an independent source, for standard mode. No contention for Profi.

### 1.5 Others in the corpus

- UnrealSpeccyP: no Profi machine at all.
- ZX-M8XXX (`Z80_SPECTRUM_EMULATION_GUIDE.md:1293`): table row `Profi | Scorpion | 1MB RAM, CP/M compatible`, i.e. "timing family: Scorpion". This is a prose guide (AI-style overview), no source, no code; weak evidence that others also class Profi with 69888.
- jnext, zxsp, NedoOS, Scorpion256TPlus: only incidental mentions (Next FPGA VHDL `profi='0'` DFFD gating, TZX block ID, loader code).
- zx-evo/pentevo docs (`docs/ZX/zx-ports-full-table.txt:328`): only lists "Profi-1 (v3.x)" as a computer type for the port table. No Russian docs, schematics or manuals with timing text exist in `pentevo/docs`, `zx-evo/pentevo/specs` or the Unreal `doc/*.txt` (grep for "Профи"/"profi" done in cp1251 and UTF-8).

## 2. Hi-res 512x240 (DFFD.7 = DS80)

### 2.1 UnrealSpeccy

`dxrend.cpp:145`: `if ((comp.pDFFD & 0x80) && conf.mem_model == MM_PROFI) { rend_profi(dst, pitch); return; }`. This is a pure render switch at frame-draw time.

- No change to frame, line, INT, CPU frequency. `pDFFD & 0x80` is not looked at by any timing code (`grep pDFFD` in `mainloop.cpp`/`z80.cpp`: none).
- `dxr_prof.cpp` draws 512 px wide, up to `min(240, temp.scy)` lines (lines 156,166,176), centred inside the 240-line "small border" window, using the screen pair pages `+0x2000` and attribute pages `+0x34*PAGE` (`:23-30`).
- In the newer TSConf-era fork (`zx-evo/pentevo/unreal/Unreal/draw.cpp:18,766-769`) the raster table has `{ R_512_240, 56, 296, 70, 198, 0 }` and `vid.raster = raster[R_512_240]` when `MM_PROFI && (pDFFD & 0x80)`; i.e. 240 lines from raster row 56 to 296, columns 70..198 (128 T = 512 px at 4 px/T). Row 56 is the same line where the preset's paper starts (56*224), so hi-res is drawn starting at the *same* line as standard mode's paper, giving 240 rows beginning at line 56 (not 24 rows earlier as a centred 192-line paper would). This is inconsistent with `dxr_prof.cpp` (centred) and with xpeccy (centred), so treat the vertical position of the hi-res screen as unverified. Frame/INT are unchanged.
- Monochrome option (`ProfiMonochrome` in ini, `dxr_prof.cpp`) is presentation only.

### 2.2 ZXMAK2 (the only emulator with a different hi-res *timing raster*)

`ProfiRenderer.cs:232-254` (`CreateParams`, "PROFI 3.2", same header comment as above):

```
timing.c_frameTactCount = 69888;	// 59904 for profi mode (312x192)
timing.c_ulaIntBegin = 16+3;
timing.c_ulaIntLength = 32 + 7;	// TODO: needs approve
// profi mode timings...
timing.c_ulaLineTime = 192;			// tacts per line
timing.c_ulaFirstPaperLine = 72;    // 192	// tact for left top pixel
timing.c_ulaFirstPaperTact = 8+16;
timing.c_ulaBorderTop = 8; timing.c_ulaBorderBottom = 8;
timing.c_ulaBorderLeftT = 16;	// real 3.xx=6   (right: real 3.xx=10)
```

- Hi-res draws with a **192 T line pitch** and **4 px per T** (`lineTact*4`, `:298`), 640x256 output; standard mode uses a 224 T line. The frame counter stays 69888 (the constant is used, 59904 is only a comment). 69888 / 192 = 364 raster lines, so the hi-res picture is not a self-consistent 312-line raster; the comment "59904 for profi mode (312x192)" suggests the author believed the true DS80 frame is 312 lines x 192 T = 59904 T but did not implement it. **No source implements 59904.**
- `SetPageMappingProfi(ds80, ...)` (`UlaProfi3XX.cs:170-190`) swaps `Renderer` between `SpectrumRenderer` and `ProfiRenderer`, so only the raster/INT-offset (19 T vs 0) and free-bus reads change. The CPU sees the same `c_frameTactCount = 69888`.
- Border in hi-res uses the "real 3.xx = 6 / 10" T comment, showing the author looked at board behaviour but no numbers documenting how.
- First paper in hi-res: line 72, tact 24 on a 192 T line, minus IntBegin 19: about 13829 T after INT, vs 12583 T in standard mode. Difference ~1250 T (5.6 standard lines).

### 2.3 xpeccy-plus

`src/libxpeccy/video/video.c:1088-1114` (`vidProfiScr`): `yscr = vid->ray.y - vid->bord.y + 24; // (240-192)/2`, `xscr = ray.x - bord.x` 0..255 with 4-px chunks (double dot). Same `ULA.Profi` layout, same 224 x 312 frame; hi-res is a draw-time mode (`vid_set_mode(VID_PRF_MC)`, `hardware/profi.c:81`). Border colour inverted in hi-res (`profi.c:47-49`, `nextbrd ^= 7`). No timing change; screen centred (24 lines above/below the 192-line paper).

### 2.4 pico-spec: hi-res timing re-measured against real hardware

`src/Video.h:71-84` (long comment, quoted in part):

```
// Profi DS80 (512x240) sync-gen runs 192 T-states/line (not 224!) - ZXMAK2 ProfiRenderer: ...
// First paper line = 48 after INT, NOT ZXMAK2's 72: mcprofi2016's border effect is exactly 4608 OUTs x 12T = 55296T = 288 lines
//   starting at T~4574 after INT - it tiles the full 288-line visible window (24+240+24) only with paper at line 48.
// ZXMAK2's c_ulaIntBegin=19 ... is NOT subtracted: real-hw photo of mcprofi2016 shows our border pattern ~19T right of the true position with it.
// -2T net calibration ... locked the pattern 3T left of centre.
#define TSTATES_PER_LINE_PROFI_DS80 192
#define TS_SCREEN_PROFI_DS80   9238   // 48*192 + 24 - 2 (calibration)
```

`Video.cpp:623-660` (`applyDS80BorderGeometry`) applies these when DFFD.7 is set and restores 224 T/line and `TS_SCREEN_PROFI` (12583) when it is cleared; frame T-states stay `TSTATES_PER_FRAME_PROFI` (69888) throughout (`CPU.h:45`, `CPU.cpp:120`).

Meaning: a 2026 implementer, comparing against a **real Profi** running a border-effect demo (mcprofi2016) via photos/screenshots, concluded that in DS80 the syncgen runs **192 T/line**, and that the raster placed paper at about line 48 (9238 T after INT), not 12583 T. That is the only evidence in the corpus derived from real hardware for anything Profi-timing-related, and it contradicts standard-mode values in every other emulator applying to DS80. It is, however, a single demo, a photo-based calibration ("-2T net", "3T left of centre"), and pico-spec's own author calls parts of it calibration constants. The frame length in DS80 is still left at 69888 by pico-spec, which is inconsistent with 192 T lines only if the frame is 312 lines; if the true DS80 frame were 312 x 192 = 59904 T (ZXMAK2's comment), the pico-spec loop would be running 10 000 T too many per frame. Not resolved by any source.

## 3. Modes and variants

| Item | Where | Effect on timing |
|---|---|---|
| Board revision Profi 3.xx vs 5.xx | ZXMAK2 `MACHINES/PROFI3XX.VMZ` / `PROFI5XX.VMZ`, `UlaProfi5XX.cs` | 5XX adds palette regs on `xx7E` and extended-port decoding (`history.txt:980`, "boards v5.xx"); **inherits the same `CreateSpectrumRendererParams`** (no timing difference); only `c_ulaProfiColor = true` |
| RAM 512K vs 1024K | ZXMAK2 `MemoryProfi512`/`1024`; xpeccy `hardware.c:105` (`MEM_512K \| MEM_1M`); unreal `doc/unreal_e.txt:30` "Profi 1024 RAM/ROM and memory mapper" | none |
| Pentagon vs 48K-like "compatibility" timing | not present anywhere | - |
| CPU speed / turbo switch | Unreal generic turbo (`emulkeys.cpp` frame/paper tweak keys); xpeccy `cpu.frq = 3500000` fixed; only `PRESET.PROFI_TURBO` in Unreal_NS (116920/375, "by a left tact-meter") | Unreal_NS only, unverified |
| DFFD.4 (7FFD lock / 48K mode), DFFD.3, DFFD.6 | all | memory mapping only |
| DS80 (DFFD.7) | see section 2 | draw-only except ZXMAK2/pico-spec raster |
| "ULA variants" | UnrealSpeccy: user-selected preset (`Preset=`), unrelated to `MM_PROFI` | The preset is the *only* place a user can pick Profi/Pentagon/Scorpion timing independent of the memory model |
| Sync generator | ZXMAK2 comment "SYNCGEN: SAMX6 (original)" | hints that the original 3.2 board used a dedicated sync generator rather than the Spectrum ULA counters (unverified reading) |

How the emulators pick frame length per model (all static): UnrealSpeccy by INI preset name; ZXMAK2 by ULA class (`UlaProfi3XX` fixed 69888); xpeccy by the `geometry = ULA.Profi` layout string in the machine file; pico-spec by `Config::arch == "Profi"` selecting `TSTATES_PER_FRAME_PROFI`.

## 4. Pentagon-like or 48K/Scorpion-like?

Evidence, in order of weight:

1. Every source that defines a Profi frame picks **69888 / 224 / 312** (Unreal preset, ZXMAK2 `UlaProfi3XX`, xpeccy layout, pico-spec). None picks 71680/320.
2. Structural clues: ZXMAK2's own comment describes the original board's raster as **768 x 312**, meaning 312 lines (same as 48K/Scorpion, not Pentagon's 320). ZX-M8XXX guide files Profi under "Scorpion" timing. Unreal's Profi preset differs from the Scorpion preset (paper 12580 vs 14344; INT 28 vs 32) but shares the 69888 frame.
3. Provenance of the numbers: Unreal's line is credited to "DDp" (a known Unreal contributor) with no description of method; ZXMAK2's 39 is a "TODO: needs approve" edited from 42; xpeccy-plus explicitly labels its INT length and INT-to-paper offset "open ... nobody has confirmed"; pico-spec copies ZXMAK2. **No comment anywhere cites a real-hardware measurement of the standard Profi frame** (pico-spec's real-hw calibration is DS80-only, see 2.4).
4. History hints: no git history reaches before the code drops (unreal-speccy 0.38.4 drop; pentevo svn import 2009; ZXMAK2 Codeplex move 2018). Nothing else to mine. In `xpeccy-plus` the Profi layout was never reviewed after creation (only the Pentagon/Scorpion/ATM rows were).

Conclusion: the 69888 / 224 / 312 frame is a consistent, if unverified, consensus. It is "48K/Scorpion-like" in size. The INT length (28/32/39 T) and the INT-to-paper offset (12580 vs 12583 vs 32 T) are not settled.

## 5. Comparison

| | UnrealSpeccy PROFI preset | ZXMAK2 Profi 3XX/5XX (std) | ZXMAK2 hi-res | xpeccy-plus | pico-spec std | pico-spec DS80 | Unreal_NS PROFI_TURBO |
|---|---|---|---|---|---|---|---|
| CPU clock | derived 3.4944 MHz (50*69888) | 3.5 MHz | same | 3.5 MHz | 3.5 MHz | 3.5 MHz | ~5.85 MHz derived |
| T / line | 224 | 224 | 192 | 224 | 224 | 192 | 375 |
| Lines / frame | 312 | 312 | 364 (69888/192) | 312 | 312 | 364 (frame unchanged) | ~312 |
| T / frame | 69888 | 69888 | 69888 | 69888 | 69888 | 69888 | 116920 |
| INT length | **28** | **39** ("needs approve") | 39 | **32** ("open") | **39** | 39 | 28 |
| INT to first paper | 12580 (56*224+36) | 12583 (56*224+39) | ~13829 (72*192+24-19+...) | **32 T** (row 72, x 128 vs INT x 64) | 12583 | 9238 (48*192+24-2, calibrated to real hw) | 12580 |
| Contention | none (floatbus 0) | none | none | none (pattern 0) | none | none | none |
| Border geometry | preset 320x240 small (24 top,32 left) / 384x288 full (48,64) | 32 top/bottom lines, 16 T left/right | 8 top/bottom lines, 16 T sides | 448x312 raster, bord 64x56 dots, blank 64x16 | 24-line small / 288 full modes | 24+240+24 rows, 16 T sides | same as Unreal |
| Hi-res timing change | none (draw switch only) | - | raster only | none | - | line 192 T, first paper 9238 T | none |
| Evidence quality | "thanks to DDp", ~2009, no method | "TODO", copied from Codeplex | same | self-declared open | copy of ZXMAK2 | one demo, real-hw photos | "left tact meter" |
| Other-emulator INT for context (from same table) | Pentagon 32 (Unreal) / 36 (Fuse, xpeccy) ; Scorpion 32 (Unreal) / 36 (xpeccy); 48K 32 | | | | 48K 32, 128K 36, Pentagon 36 | | |

## 6. Recommendation (for an emulator that wants a defensible default)

1. **Frame: 224 T/line x 312 lines = 69888 T, 3.5 MHz, no contention.** Supported by every emulator; label it "consensus, unverified against a Profi". Do not use 71680 / 320 lines (no source supports it for Profi).
2. **INT length: use 32 T as default** (it is the 48K/Scorpion/Unreal-default figure and the middle of the 28-39 T spread), and document it as unverified. It only matters for code that samples the INT line late in a long instruction. If the emulator has a "match UnrealSpeccy" goal, 28 T; if "match ZXMAK2", 39 T. Recording all three in the doc's parameter table is preferable to hiding the disagreement.
3. **INT to first paper: use 12580-12583 T (first paper on line 56).** Two emulators (Unreal, ZXMAK2) agree within 3 T and pico-spec inherits it; xpeccy's 32 T looks like an unaudited placeholder (its own documentation flags it). Treat 56*224 + 36 as the best-available and mark unverified.
4. **Hi-res DS80: keep frame length, INT and CPU timing unchanged (three emulators agree), and do not adopt a 192 T/line or a 59904 T frame** unless an implementation goal explicitly requires cycle-exact border effects in hi-res. The pico-spec evidence shows the real DS80 raster very likely differs (192 T lines, paper about 9238 T after INT), but it rests on a single demo photo-calibration, conflicts with the unchanged 69888 frame, and is not corroborated by a second source. Make it an opt-in "DS80 raster timing" experiment, not the default. Draw the 512x240 window centred on the 192-line paper (24 above/below), as Unreal `dxr_prof.cpp` and xpeccy do.
5. **No compatibility modes exist to expose.** Do not invent a "Pentagon-like vs 48K-like" switch for Profi. If a variant is wanted, a board revision (3.xx / 5.xx) and RAM size (512K / 1024K) are the only real, documented axes, and neither changes timing. A turbo speed would be a user option, not a Profi mode (only the low-quality `PROFI_TURBO` data point exists).
6. If a real Profi is reachable, the three quantities to measure (all currently unverified): INT pulse width, INT to first paper pixel, frame length in T (also in DS80).

## 7. Unverified / open items

- Everything in the standard-mode numbers except the consensus 224 x 312 x 69888 is unverified.
- Whether 59904 (312 x 192) is the real DS80 frame length: raised only by a comment in ZXMAK2, never implemented anywhere.
- Whether the sync generator (SAMX6 in the ZXMAK2 comment) really runs the standard mode at 224 T/line and the hi-res at 192 T/line (pico-spec) or something else.
- Vertical placement of the 512x240 window: Unreal fork raster table says row 56 (`draw.cpp:18`), Unreal 0.39/xpeccy centre it on the paper, ZXMAK2 uses 8 border lines around paper at line 72.
- No real-hardware documentation (schematics, Russian manuals) exists in the local corpus; the Karabas-Pro FPGA sources were excluded as a clone.
