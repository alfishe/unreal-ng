# Scorpion ZS 256 + GMX: hardware spec from emulators, firmware and software

**Date:** 2026-10-30 - part of [README.md](README.md). Adds to [research-reference-consensus.md](research-reference-consensus.md)
(2026-10-07); where this research **corrects** the earlier files it says so (marked **CORRECTION**). The manual [MAN]
is kept in [references/](references/).

Tags: **H** two or more independent sources agree; **M** one source, no contrary one; **O** open. Conflicts between
sources are marked **CONFLICT**.

Short names used below (full paths in [Sources](#sources)):

| Tag | Source |
|:--|:--|
| MAME | `mame/src/mame/sinclair/scorpion.cpp` (classes `scorpion_state`, `scorpiontb_state`, `scorpiongmx_state`) |
| UNR | tslabs/zx-evo-unreal `Unreal/` (io.cpp, memory.cpp, draw.cpp, drawers.cpp, vars.cpp, config.cpp, z80.cpp) - files are cp1251 |
| PICO | DnCraptor/pico-speccy at `5fb5470c` (2026-10-29): `src/speccy/core/Ports.cpp`, `machines/Scorpion.cpp`, `video/Video.cpp`, `roms/scorpion/scorpion_gmx_banks.h` (fetched into `pico-speccy@5fb5470c:`) |
| ZXBY | the zx.by web Unreal build in `zxby.org/unreal/ (fetched 2026-10-30) ` (`page.html`, `js/app.js`, `x/unreal.wasm`); its GMX code (`web/web_gmx.cpp`) is not available as source |
| OSZ | izzx-git/OSZ (multitasking OS for the GMX, 2024-2026) in `OSZ@2e1c9fd:` |
| EG | izzx-git/EdgeGrinder GMX port (2021) in `EdgeGrinder@6639aa1:` |
| LW | ProfROM changelog by LW/PLM, `Scorpion256TPlus/ROM/LW/changes.md` |
| F644 | firmware image `data/rom/gmx644.rom` (524288 B, CRC32 `0F84E542`; "TMgmx Loader V2.00 ... (c) 2024 LW/PLM, Compiled 17 Mar 2024", "Shadow service monitor v6.44su, #25AB, 21.08.2025") |
| F125 | `data/rom/gmx.rom` in the repo (CRC32 `00DF8568`, MAME "GMX Boot Rom 1.2 V. 5.00") |
| D07 | the repo's design folder `docs/inprogress/2026-10-07-gmx/` |

No other emulator has a GMX: Xpeccy, xpeccy-plus (Turbo+ only, `src/libxpeccy/hardware/scorpion.c`), ZXMAK2
(ProfROM 256 only, `MemoryScorpionProfRom256.cs`), ZEsarUX (no Scorpion machine at all), UnrealSpeccyP,
unreal-speccy 0.39 (only a commented-out "gmx" idea), Zero-Emulator, ZX-M8XXX, zxsp, Spectral, eve-emu,
karabas-pro, pico-spec (drewpo28), the svn/gitlab trees (scorpevo FPGA has ProfROM only). All `gmx` hits in them are
resource/netlist noise.

---

## 0. Resolutions from the original manual [MAN] (read this first)

[MAN] = "GMX (Graphic Memory eXpander). Руководство по подключению, программированию и работе", Фирма Скорпион,
СПб 1998, loader chapter "(c) 1998 MOA (Андрей Ларченко)", Word file "Документация к GMX271198.doc" (22.11.1998) in
https://web.archive.org/web/2006id_/http://www.scorpion.ru:80/spectrum/hard/gmx.zip (the Word file is in
[references/GMX271198.doc](references/GMX271198.doc), its text in [references/GMX271198.txt](references/GMX271198.txt);
section III holds the port descriptions). Where [MAN] conflicts with an emulator, [MAN] wins unless noted.

| Topic | [MAN] statement | Effect on the conflicts below |
|:--|:--|:--|
| Reset | "При включении питания все порты сброшены в ноль. Сигнал RESET действует на все порты кроме порта глобальных установок xx00" | RESET clears all GMX latches except `#00` (MAME/UNR clear `#00` on reset too: a deviation) |
| `#00` | fully decoded (A0-A7 = 0). D0-D2 CONF0-2 (FLEX8000 scheme number), D3 RECONF (reload the chosen scheme), D4 FIXROM (freezes `#7EFD` A16-A18; then reset neither clears them nor reloads the scheme), D5 BLKEXT (GMX ports off = plain ZS-256 Turbo+), D6 BLKEX2 (with BLKEXT: all xxFD ports off, `#7FFD` decoded "по FD при A15=0", standard ROM needed), **D7 ST/PENT: 0 = INT at the end of frame sync, 312-line frame; 1 = INT at its start, 320-line frame**. "Никогда не записывайте в него данные!" | RECONF reloads the FPGA (emulators: CPU reset, magic code = CONF). **Answers Q6/Q11: `#00` bit 7 selects Scorpion/Pentagon INT and frame length**; it is the `#7EFD` read bit 4 (ST/PENT) |
| `#7EFD` write | D0 EWR (28F400 write enable), D1 Vpp, **D2 MagEnable: 0 = Magic button works, 1 = blocked**, D3 256x192 / 640x200, D4-D6 flash A16-A18, D7 Normal/Turbo | MAME layout confirmed |
| `#7EFD` D2 and the lock | section 3.1: "действие разряда BLKPK1 ... отключается при установке разряда D2 порта 7EFD (MGEN)" | **Both MAME (Magic blocked) and UNR (`#7FFD` lock lifted) are right; an implementation needs both effects** |
| `#7EFD` read | D0 BLKPK1, D1 SCR, D2 Turbo, D3 Graphic, D4 ST/PENT, D5 BLKEXT, D6 BLK1, D7 BRD2 | matches MAME/UNR/PICO |
| `#78FD` | D0-D6 = SYS0, /SYS1, SYS2, SYS3, SM0-2; "аппаратная инверсия бита D1 ... чтобы после сброса области #8000-#BFFF соответствовал банк #2"; read D7 = BRD1 | XOR 2 confirmed |
| `#DFFD` | D0-D2 SM0-2 = page bits 4-6; page = SYS0-2, SYS3 (`#1FFD` D4), SM0-2; with two 256K SIMMs the banks are 0-#F and #40-#4F | formula confirmed; SIMM-dependent holes (Q10) |
| `#1FFD` D2 | "TRDOS - принудительный выбор ПЗУ TRDOS" | confirmed |
| Scroller | `#7AFD` low (8 bits) + `#7CFD` high (6 bits) = one 14-bit register; one pixel line = 80, one text row = 640; "младшие четыре бита аппаратно сбрасываются"; **write-only**; set only through the shadow monitor's RST 8 so Magic does not break the screen | **a byte start address with the low 4 bits forced to 0**: UNR's byte-offset model plus MAME's `& #F0`. MAME's `/80` line model is wrong for non-multiples of 80. (ZX-Format #07's "1 = one pixel line" is the earlier, superseded text) |
| Screen pages | "графический экран расположен в банке **#39 (SCR=0) или #3A (SCR=1)** ... атрибуты ... (SM2=1) т.е. **#79 или #7A**"; linear from #C000, 80 bytes per line, 16000 bytes | **Screen 1 = `#3A/#7A`**, agreeing with OSZ, LW and Trident; MAME/UNR/PICO/EG `#3B` disagree (EG may run only on emulators, or `#3B` may alias on boards with one SIMM set - O) |
| Magic latch | some read-back bits are latched on Magic; "снова открываются ... после «холостого» чтения порта #FF" | MAME's lock/IN #FF release confirmed |
| Flash map | #00000 loader, #02000 start-up scheme, #04000 working scheme, #06000 parameters, #08000-0FFFF 4 more schemes, #10000-1FFFF standard 48+128+TR-DOS 5.03, #20000-2FFFF GMX test, #30000 free, #40000-7FFFF Scorpion with shadow monitor | = planes 0 / 1 / 2 / 3 / 4-7, matches the F644 scan (§1.5) |
| Pentagon scheme | 320-line frame, INT at the start of frame sync, 80 lines from INT to the active screen; `#7FFD` decoded at A1,A15 = 0, border at A0 = 0 | timing for the Pentagon INT profile |
| Loader | DELETE during the 1-2 s prompt opens the menu, else the shadow monitor starts; schemes 1 start-up, 2 working, 4 Pentagon128, 5 Composit, 6 test, 7 reserve (3 skipped); the choice survives RESET except 6; "P" = flash from disk A: (needs +12 V on X7, jumper X8 for the loader block); yellow border stripes = hardware fault, red + beep = error; jumper X3 = Turbo button | boot flow §6 |

Other web facts (same sub-agent):

- ZX-Format #07 (official Scorpion text): "640*200(208)"; RAM 256K/512K/1M/1256K/2M, colour needs 2 SIMMs; **turbo
  "1.8-1.9 при исполнении программ из ОЗУ. При работе с ПЗУ ровно в 2 раза"** - so RAM accesses in turbo do have
  waits (Q on contention, §4, is real; magnitude unknown).
- Trident (My Speccy #01, 2004, zxpress id=10696): turbo by a system port or the monitor, "в режиме турбо INT
  вытягивается до 132000 тактов" (meaning unclear: 2 x 69888 = 139776, not 132000); early boards had swapped
  bits in the second-window port (fixed; scheme 6 tests it).
- Apollonov (ZX-Pilot #31 / City #17-18): GS does not work, no line sync output, 320x200 never made, 640 mode = 25 x 80
  characters "во весь экран телевизора" (border nearly gone). The official site later says firmware of 20.04.99
  fixes GS problems (**CONFLICT**).
- Flash chip: [MAN] bit names say 28F400, the official site says "28f040" (**CONFLICT**, 28F040 = 512K x 8, fits
  the 512 KB map better).
- CPLD Altera EPM7064SLC44 plus a FLEX8000 FPGA (scheme); rev.2022 docs and schematic on Yandex disk (zx-pk 31334).

---

## 1. Memory

### 1.1 RAM

| Fact | Source | Tag |
|:--|:--|:--|
| 2048 KB = 128 pages of 16 KB | MAME `scorpion.cpp:816` (`set_default_size("2M")`), UNR `vars.cpp:274` (`MM_GMX, 2048, RAM_2048`), PICO `Ports.cpp:728` (128 pages), ZXBY `app.js` ("2048 KB") | H |
| `#0000-#3FFF`: ROM, or RAM page 0 when `#1FFD` bit 0 = 1 | MAME `:194-199`, UNR `memory.cpp:107-108`, PICO `Scorpion.cpp:91` | H |
| `#4000-#7FFF`: always page 5 | MAME `:316` (bank 1 never re-pointed), UNR `memory.cpp:12` | H |
| `#8000-#BFFF`: page `(#78FD & #7F) XOR 2` (so value 0 = page 2) | MAME `:661`, UNR `memory.cpp:101`, PICO `Ports.cpp:2521-2528` (out of range -> page 2); software: OSZ `Drv/zsgmx.asm` `PageSlot2: xor 2 / out (#78FD)`, EG `EG_Interrupts2.asm:24-27`, OSZ `os_main.asm:2303-2305` reads `#78FD` back and XORs 2 | H |
| `#C000-#FFFF`: page `((#DFFD & 7) << 4) \| ((#1FFD & #10) >> 1) \| (#7FFD & 7)` (7 bits) | MAME `:663`, UNR `memory.cpp:100-102`, PICO `Scorpion.cpp:37-38`; software: OSZ `PageSlot3` (page bit 3 -> `#1FFD` bit 4 via `rlca/and #10`, bits 4-6 -> `#DFFD`) | H |
| `#1FFD` bits 6-7 (the ZS-1024 page bits) are **not** used by GMX | MAME `:663` replaces the base `:211` formula; UNR GMX branch ignores them | H |
| Normal screen: page 5, or 7 with `#7FFD` bit 3 | MAME `:264` (inherited) | H |
| Page use by the firmware: `#77-#7F` belong to the shadow monitor; "for GMX pages 8 and #78 must not be given" to a resident | OSZ `_Doc/ReadMe.txt` ("#77-#7F - заняты теневым монитором"), OSZ `os_main.asm` comment before `Start_warm`; LW `changes.md:294` ("настройки монитора перенесены в страницу #78"), `:372` (navigator uses page `#7B` for FAT catalogs) | H |
| Pages are wrapped to the fitted size (`% pages`); SIMM sets of 256/512/1024/1280/2048 KB are mentioned by the 1999 article | MAME `:661,663`; D07 Q10 | M |

**CORRECTION (D07 README / consensus §1 "UNR: its memory.cpp has no GMX paging"):** it has. The
zx-evo-unreal `memory.cpp` holds a full GMX branch (lines 32-37 plane bases, 99-112 bank formula); the earlier grep
missed it because the file is cp1251 and grep treated it as binary. UNR therefore agrees with MAME and PICO on the RAM
formula, so the memory facts above are three-source.

### 1.2 `#7FFD` (as Scorpion)

Bits 0-2 RAM page bits 0-2 for `#C000`; bit 3 screen 5/7 (and in the extended mode the GMX screen 0/1, §3); bit 4 ROM
A14 (128/48); bit 5 lock (MAME `:168-174, 254-265`). **CONFLICT on the lock:** UNR lifts the lock when `#7EFD` bit 2 = 1
(`io.cpp:716-717`); MAME has no such rule and uses `#7EFD` bit 2 as "Magic disabled" (`:807`, `:708-712`). PICO follows
MAME (ignored). See §2 `#7EFD`.

### 1.3 `#1FFD` (Scorpion + GMX bit 2)

| Bit | Meaning | Source |
|:--|:--|:--|
| 0 | RAM page 0 at `#0000` | MAME `:179`, UNR `memory.cpp:107` |
| 1 | service ROM page (page 2 of the current plane) at `#0000` | MAME `:180, 202-203`, UNR `memory.cpp:105` |
| 2 | **GMX only:** page 3 (TR-DOS) of the current plane at `#0000` with the Beta (FDC) ports on; overrides bits 0 and 1 | MAME `:654-660`, UNR `memory.cpp:109-113` (`CF_DOSPORTS`, `base_dos_rom`), PICO `Ports.cpp:2417-2421` (plus: the DOS latch drops on the falling edge of bit 2 when PC >= `#4000`, `Scorpion.cpp:76-88`, a real-firmware trace). UNR comments it as "the improvement from the GMX book" (`memory.cpp:83`). | H |
| 3 | RS-232 TxD | MAME `:182` |
| 4 | RAM page bit 3 | MAME `:183`, all |
| 5 | Centronics strobe | MAME `:185` |
| 6-7 | unused on GMX | above |

### 1.4 ROM / flash

| Fact | Source | Tag |
|:--|:--|:--|
| 512 KB flash, 8 planes of 64 KB, each plane = 4 x 16 KB pages in Scorpion order 0 = 128, 1 = 48, 2 = service, 3 = TR-DOS | MAME `:579, 656` (`(prof_plane << 2) \| page`), UNR `memory.cpp:32-37` (`ROM_BASE_M + plane*64K + n*PAGE`), PICO `Ports.cpp:2442`, F644 page scan (§1.5) | H |
| Plane = `#7EFD` bits 6-4 (flash A18-A16); frozen while port `#00` bit 4 ("fixrom") = 1 | MAME `:791-795` ("D4..6 - A16..18: 28F400"), UNR `io.cpp:562-566` (`(val & #8F) \| (p7EFD & #70)` when `p00 & #10`), PICO `Ports.cpp:2566-2573` | H |
| Chip: Intel 28F400 | MAME `:793, 809-810` comments only | M |
| Plane after power-on = 0 (the loader) | MAME `:593` (`m_prof_plane = 0`), UNR `z80.cpp:97` (all GMX latches 0), PICO | H |
| ProfROM `#0100-#010F` read strobe (the Turbo+ ProfROM GAL that moves the plane by A3:A2): **CONFLICT** | MAME inherits it (`:602-631`, only on reads of `#0100/#0104/#0108/#010C` in service page); PICO **disables it on GMX** after a hardware trace: the service monitor (plane 4 page 2) checksums its own 16 KB with a CPI loop through `#0100-#010F` and the strobe threw the CPU into plane 0 (`Ports.cpp:2337-2353`); the firmware switches planes only through `#7EFD` from RAM thunks at `#E3FD/#E429/#E4B0` (same comment); UNR has no strobe for GMX | M (off) |
| Flash write: `#7EFD` bit 1 = Vpp (power to the 28F400), bit 0 = EWR (write enable) | MAME `:809-810` (comment, not implemented) | M |
| Flash programming is done by the loader ("P - program from disk" in the boot menu), the loader is re-flashed only by an older built-in loader; an external loader from the same disk flashes ROM without touching the loader and scheme 1 | LW `changes.md:498-500`, D07 ART1 | M |

No source implements the 28F400 command protocol (read array / program / block erase / status). For an emulator the
flash is read-only unless the firmware update path must run (D07 Q9). The Intel 28F400 command set (`#FF` read array,
`#40/#10` program, `#20/#D0` block erase, `#70` status, `#90` ID) is the datasheet's, not checked against the loader.

### 1.5 What the flash blocks hold (page scan of F644)

| Plane | Content (strings / identity) | Ports referenced (`LD BC,nnnn` counts) |
|:--|:--|:--|
| 0 | page 0 "TMgmx(r) Loader V2.00 (c) 1997,1998 by Andrew MOA (c) 2024 LW/PLM, Compiled 17 Mar 2024"; pages 1-3 FPGA scheme data with names "DK_test", "Pentagon", "PentaGON", "Komposit", "Work Sch" and a second copy "Loader V2.00.334" | `#7EFD`, `#78FD` in page 0 |
| 1 | the Pentagon scheme's ROMs (page 0 CRC `124AD9E0` = Pentagon 128 editor, page 1 = 48 BASIC); pages 2-3 duplicates | `#7FFD` |
| 2 | a GMX program with its own 8x8 font (page 0 uses `#7AFD`, `#7CFD`, `#78FD`, `#DFFD`, `#1FFD`): probably the 640x200 test/utility | all GMX ports |
| 3 | 128 / 48 pages and a page with `/RRL/BOOT.$C`, `/RRL/FASTBOOT.INI`, `/RRL/SHELL.RRL` (a FAT boot for an OS) | |
| 4 | the Scorpion scheme ROMs (128 editor, 48 BASIC, service, TR-DOS); this is the plane the 128 menu runs in (`#7EFD = #C0` = turbo + plane 4, ZXBY `app.js` reset macro "until its flash block 4 runs the 128 ROM") | `#7EFD`, `#78FD`, `#DFFD` |
| 5 | "1993-1998 MOA, 2022-2025 PLM Shadow service monitor v6.44s", "Professional Extention. Shadow service monitor with Analyser ... v6.44su, #25AB, 21.08.2025", FAT32 driver ("ZS-ScorpionFAT32") | `#78FD`, `#7AFD`, `#DFFD` |
| 6 | utilities: "Copy disk to image", "For run, you need GMX with firmware", NEMO/SMUC strings | |
| 7 | a second Scorpion plane (pages 0 and 2 identical to plane 4, pages 1 and 3 differ) | |

F125 (repo `gmx.rom`, loader V1.20) has the same layout per D07 `roms.md` §2 (plane 0 loader, 1 Pentagon, 4 Scorpion,
5 shadow monitor). PICO ships "ProfRom_GMX v5.44.9643 - TMgmx(r) Loader V2.00 over a patched GMX 5.01, with ProfROM
5.44s in planes 4-7" (`scorpion_gmx_banks.h:2-4`). MAME lists 18 GMX images (`:978-1018`), default "ProfROM +GMX
V.6.xx.043.9226su". LW names the firmware families: v4 = Turbo+ ProfROM, v5 = GMX with the monitor on the standard
screen, v6 = GMX with the monitor on the 640x200 screen (`changes.md:419, 480, 497`; PICO `scorpion_gmx_banks.h:4`
"v5.44s Shadow monitor on the STANDARD ZX screen").

---

## 2. GMX ports

### 2.1 Decode

| Port | MAME | UNR | PICO | Black_Cat (via D07) |
|:--|:--|:--|:--|:--|
| `#00` (global config) | `0x0000` mirror `#FF00` = low byte 0, any high byte, installed on the base map so it works even with BLKEXT (`:861`) | `(port & #FF) == 0` before everything (`io.cpp:534-548`) | `(address & #FF) == 0` before the BLKEXT test (`Ports.cpp:2508`) | `xx00` "global settings, always available" |
| `#78FD #7AFD #7CFD #7EFD #DFFD` | exact 16-bit address, in a view that `#00` bit 5 disables (`:863-872`) | low byte `(port & #23) == #21`, high byte exact (`io.cpp:550-582` write, `1302-1326` read) | exact 16-bit (`Ports.cpp:2516`) | `0x1xx` + A10-8 (high byte), low `xx1xxx01` |

**Interaction with the host decoder.** In MAME, when BLKEXT (`#00` bit 5) is set, the GMX view is gone and the five
addresses fall through to the Turbo+ map: `#78FD/#7AFD/#7CFD/#7EFD` then hit the `#7FFD` decode (`0x4021` mirror
`#3FDC`, `:327-328`), i.e. they become `#7FFD` mirrors as on a plain ZS 256; `#DFFD` matches nothing. That is what LW's
"Scorpion ZS 256 - ports blocked" boot profile relies on (§6). **Implementation note for unreal-ng:** the Scorpion
decoder's `IsPort_7FFD` (`01x1xxxx xx1xx101`, `portdecoder_scorpion256.cpp:743-760`) matches all four `#7xFD`
ports, so the GMX arm must run first (as the ProfROM `#7EFD` arm already does, `:504-519`); and its AY `#FFFD` decode
`(port & #C002) == #C000` (`:556`) also matches `#DFFD`, which MAME's AY decode (`111xxxxxxx1xxx01`, `:332`) does
not. The GMX `#DFFD` must be decoded before the AY arm.

### 2.2 Registers

| Port | Dir | Bits | Sources | Tag |
|:--|:--|:--|:--|:--|
| `#00` | W | bit 3: arm the magic byte `#88 \| (v & 7)` and, if bit 4 = 0, **reset the CPU** (RAM and latches stay); bit 4 "fixrom": plane frozen and no reset; bit 5 "BLKEXT": GMX register file off; bit 7: read back in `#7EFD` bit 4; bits 0-2: magic code; bit 6: stored, no known effect | MAME `:723-740`, UNR `io.cpp:536-547`, PICO `Ports.cpp:2508-2530` | H |
| `#78FD` | W | bits 0-6: page of `#8000` stored XOR 2 (MAME and PICO keep 7 bits; UNR keeps 8) | MAME `:865`, UNR `io.cpp:571-573`, PICO `:2516-2529` | H |
| `#78FD` | R | bit 7 = `#FE` bit 1 (BRD1); bits 0-6 = stored value; bit 0 OR-ed with the magic bit; every read shifts the magic byte right | MAME `:742-752`, UNR `io.cpp:1321-1325`, PICO `:2910-2921` | H |
| `#7AFD` | W | scroll offset low byte. **CONFLICT:** MAME and PICO `& #F0` (`:867`, `Ports.cpp:2532`); UNR all 8 bits (`io.cpp:575-577`). Software writes only multiples of 640 (low nibble 0) | | O |
| `#7AFD` | R | bits 0-2 = `#7FFD` 0-2, bit 3 = `#1FFD` bit 4, bits 4-6 = `#DFFD` 0-2, bit 7 = `#FE` bit 0 (BRD0); i.e. the 7-bit `#C000` page + BRD0. OSZ reads it to learn the current page (`os_main.asm:2299-2302`, `and %01111111`) | MAME `:754-765`, UNR `io.cpp:1306-1311`, PICO `:2923-2934` | H |
| `#7CFD` | W | scroll offset high byte, 6 bits (`& #3F`) (UNR stores 8, uses `& #3FFF` of the 16-bit value) | MAME `:869`, PICO `:2533`, UNR `io.cpp:578-580`, drawers.cpp:594 | H |
| `#7EFD` | W | bit 7 turbo 7 MHz; bits 6-4 plane; bit 3 extended 640x200 mode; bit 2 see below; bit 1 Vpp; bit 0 EWR | MAME `:786-811`, UNR `io.cpp:561-569` + `draw.cpp:772-775`, PICO `:2535-2580` | H except bit 2 |
| `#7EFD` bit 2 | W | **CONFLICT:** MAME "Magic disabled" (NMI ignored, `:807, 708-712`); UNR "the `#7FFD` lock is lifted" (`io.cpp:716-717`); PICO ignores it ("D2 = magic_disabled", `:2578`). The loader V2.00 writes `#84` = turbo + bit 2 at its first instruction (F644 plane 0 page 0 `#00B5`: `LD A,#84 / LD BC,#7EFD / OUT (C),A`) | | O |
| `#7EFD` | R | bit 0 = `#7FFD` bit 5 (lock), 1 = `#7FFD` bit 3 (screen), 2 = turbo (W bit 7), 3 = extended mode (W bit 3), 4 = `#00` bit 7, 5 = `#00` bit 5 (BLKEXT), 6 = `#1FFD` bit 0 (RAM0), 7 = `#FE` bit 2 (BRD2). The plane is **not** readable | MAME `:767-784`, UNR `io.cpp:1312-1320`, PICO `:2936-2947` | H |
| `#DFFD` | W | bits 0-2 = `#C000` page bits 4-6 (MAME/PICO `& 7`, UNR 8 bits) | MAME `:871-872`, UNR `io.cpp:558-560`, PICO `:2582-2588` | H |
| Magic freeze | R | at an accepted NMI MAME freezes the `#7AFD` (`& #7F`) and `#7EFD` (`& #4F`) read-backs; the next `IN #FF` (non-DOS) releases them, so the shadow monitor reads the interrupted program's paging | MAME `:715-719, 246-251, 757-758, 770-771` | M |

The three BRD bits (`#FE` bits 0-2 in `#7AFD`.7, `#78FD`.7, `#7EFD`.7) let the monitor read the border back (LW
`changes.md:293`: "GMX: configuration detection (page, screen type and number, border) by direct port reading").

### 2.3 What the magic byte is for (new)

The loader reads it: F644 plane 0 page 0 at `#00F1`: `XOR A / LD BC,#78FD / OUT (C),A` (page 2 at `#8000`), then
eight times `IN D,(C) / SRL D / RRA`, which assembles bit 0 of the eight reads into A (first read = bit 0), then
`IN D,(#7AFD)`, and passes A to the depacked loader at `#5C01` (`#00C3-#00E1`). So `OUT (#00), #08 | n` (bit 4 = 0) is a
"soft power-on": it resets only the CPU and hands the loader the boot code `#88 | n`; without it the loader reads 0
(cold boot). PICO's trace comment says the same: "The loader reads this port eight times and assembles bit 0 of each
read into its BOOT MODE (magic & 7), which is what picks the profile descriptor - and hence the plane it hands over to"
(`Ports.cpp:2914-2916`). This answers D07 Q6. The 8-bit width also explains `#88`: bits 7 and 3 mark a valid code.

---

## 3. Video

### 3.1 Standard mode

The Scorpion ZX screen (page 5/7) with the Scorpion raster (MAME inherits, UNR `PRESET.SCORPION=69888,14344,224,...`).

### 3.2 The extended 640x200 mode (`#7EFD` bit 3)

| Item | Value | Source | Tag |
|:--|:--|:--|:--|
| Pixels | 80 bytes per line, linear, 200 lines = 16000 bytes, MSB = leftmost pixel | MAME `:679-694`, UNR `drawers.cpp:594-612`, PICO `Video.cpp:6367-6370`; OSZ driver (`table_addr_scr` = row x 640, 8 lines of a text row 80 bytes apart) | H |
| Attributes | one byte per 8 pixels (per pixel byte), same offset, page + `#40` | MAME `:685` (`+ (0x40 << 14)`), UNR `drawers.cpp:610` (`+ PAGE*0x40`), OSZ (`con_scr_real #39`, `con_atr_real #79`), EG (`#39` pixels, `#79` attributes) | H |
| Attribute bits | 0-2 ink, 3-5 paper, 6 bright (both ink and paper), 7 flash (swaps ink/paper on the flash phase, 16 frames) | MAME `:686-688`, UNR `drawers.cpp:611-615` (`frame_counter & #10`) | H |
| Screen 0 | pixels `#39`, attributes `#79` (`#7FFD` bit 3 = 0) | MAME `:676`, UNR `drawers.cpp:595`, PICO, OSZ, EG | H |
| Screen 1 (`#7FFD` bit 3 = 1) | **CONFLICT:** `#3B / #7B` in MAME `:676`, UNR `drawers.cpp:595`, PICO `Video.cpp:6367`, ZXBY (debugger "page #39 / #3B") and EG (`LoaderGMX.asm:45, 987-997`: "screen 0 and page #7B", "screen 1 and page #79", flipped each frame); **`#3A / #7A`** in OSZ (`_Doc/ReadMe.txt`: "5, 7, #39, #3a, #79, #7a - video pages"; `os_main.asm:1550-1556`; its `set_scr3a` label but comment "включить экран 3b"), LW (`changes.md:419-420`: debugger `SCReen` takes "#39(57)/#3A(58)"), and the 2004 Trident article (D07 ART2: buffer `#3A / #7A`). LW's navigator using page `#7B` as a FAT buffer (`changes.md:372`) also argues against `#7B` being a screen | O (code: `#3B`; firmware author and the newer OS: `#3A`) |
| Scroll | 14-bit offset `(#7CFD & #3F) << 8 \| #7AFD`. **CONFLICT:** MAME/PICO = line scroll, first line `offset / 80 mod 200` (`:679`, `Video.cpp:6370`); UNR = byte offset added to the linear address, wrapping at 16000 (`drawers.cpp:594, 604-605`). Real software (OSZ `gmxscroll`, `curscrl` "0-15999", stepped by 640 = 8 lines, wrapped at 16000; it computes a cell address as `row*640 + col + curscrl mod 16000`) is consistent with **both**: every value it writes is a multiple of 80 and of 16. Combined with MAME's `& #F0` the hardware likely has a 10-bit start address in 16-byte units (16000 / 16 = 1000 positions); a non-multiple of 80 would then shift the picture horizontally by 128-pixel steps (UNR model), which MAME's `/80` cannot express | O |
| Line timing | the 640 pixels take 160 T of the 224 T line (4 pixels per T at 3.5 MHz): UNR draws one byte per 2 T (`drawers.cpp:600`, `n -= 2`) inside `R_320_200` = lines 76-275, columns 54-214 in 2-T units (`draw.cpp:14`); MAME doubles the dot clock (`:802`, line = 896 dots) | H |
| Vertical position | UNR: 200 lines from raster line 76 (ZX paper starts at 80), i.e. 4 lines higher than the ZX paper; MAME: area `{80,719,80,279}` with a 24-pixel side and 40-line top/bottom border (`:699-704, 798-805`). Note MAME's INT timer is computed from `get_screen_area()` (`:217`), which changes in the extended mode: an artifact, not a hardware statement | M / O |
| Border | `#FE` bits 0-2, Scorpion border colours (all three emulators); width in the extended mode unknown (MAME 24 px each side) | O |
| Palette | the fixed 16 ZX colours; no palette port in any source | M |
| Mode switch | takes effect immediately in MAME/UNR (`init_raster()` on the write, `io.cpp:568`); PICO applies it at the frame end (implementation choice, `Ports.cpp:2576`) | M |
| Double buffering / gigascreen | software alternates screens 0/1 per frame (EG fade, ZXBY "Mix Frames (GMX only)": "gigascreen pictures (two screens shown in turn) and objects drawn every other frame") | M |
| 320x200 one-colour-per-pixel mode | announced, never made (1999 article, D07 §2.5); OSZ Tetris uses "pseudo 160x200" on the 640x200 screen | M |

---

## 4. Turbo

| Fact | Source | Tag |
|:--|:--|:--|
| 7 MHz = 2 x 3.5 MHz | MAME `:789` (`set_clock_scale(1 << m_turbo)`), UNR `io.cpp:567` (`turbo(2/1)`), PICO `:2560-2565` | H |
| Switched by `#7EFD` bit 7, read back in `#7EFD` bit 2; firmware sets it: loader `#84` at reset, the 128 plane runs with `#C0`, OSZ/EG write `#C8`/`#C0` (turbo always on), the monitor menu "S. Set Up -> V. Computer speed" and LW's "O. Other Settings -> T. Turbo HIGH" | MAME, UNR, PICO; F644 `#00B5`; ZXBY `app.js`; OSZ `zsgmx.asm` `gmxscron/gmxscroff`; LW `changes.md:83`; PICO `:2541-2555` | H |
| Turbo kept over a warm reset (firmware) | LW `changes.md:335` | M |
| Turbo+ IN strobes (`IN` from the `#7FFD` family = on, `#1FFD` family = off): **CONFLICT** | MAME inherits them for GMX (`:633-641`, but its GMX view shadows `#78FD/#7AFD/#7EFD` reads); UNR applies them only to `MM_SCORP/MM_PROFSCORP`, not GMX (`io.cpp:1329-1330`); PICO keeps them for the Turbo+ only (`Ports.cpp:1250-1268`) | O |
| Waits / contention in turbo | no source models any for GMX (MAME none; UNR none for GMX). The card has its own RAM and CPU, so the host board's Turbo+ EPLD wait logic (repo `research-scorpion-turbo.md`) probably does not apply; D07 assumed it does | O |
| Even M1 | MAME turns it off for Turbo+ and GMX (`:597`) | M |
| "INT stretched to 132000 clocks in turbo" (Trident article) | not understood (D07 Q13) | O |

---

## 5. Peripherals of the configuration

| Device | Facts | Source |
|:--|:--|:--|
| Beta 128 (WD1793) | host board; DOS ports `#1F/#3F/#5F/#7F/#FF` while DOS is active (MAME `:349-354`); `#1FFD` bit 2 forces it on (GMX). LW firmware variants `se/su` "emulate the VG93" from HDD images (`#3D2F` trap) | MAME, LW README / `changes.md:286` |
| AY-3-8912 | `#FFFD/#BFFD` (MAME `:330-333`, decode `1x1xxxxxxx1xxx01`); TurboSound supported by the firmware (LW `changes.md:480`); AY port A (reg 14) reads back `#7FFD`/`#1FFD` on Turbo+ (MAME `:540-545`; LW `:249, 467` uses it for screen detection) | MAME, LW |
| SMUC (IDE + 24C16 NVRAM + MC146818/DS12887 clock) | `#5FBA` version, `#5FBE` revision, `#7FBA` virtual-FDD latch, `#DFBA` clock (SYS bit 7 = address/data), `#FFBA` SYS (I2C NVRAM bits, HDD reset), `#F8BE-#FFBE` ATA registers by A8-A10, `#D8BE-#DFBE` high data latch; firmware keeps settings (INT type, port block) in NVRAM (LW `:334`) | PICO `Ports.cpp:2592-2615` (verified against UnrealSpeccy 0.37, ZXMAK2 `IdeSmuc.cs` and the ProfROM driver in GMX plane 5 page 3); unreal-ng already has it (`portdecoder_scorpion256.h:22-32`) |
| Z-Controller (SD) | supported by the monitor "by the KOE standard when no SMUC" (LW `changes.md` v4.xx.023/39, menu "H. Hard disk utility -> X. Set Up SD Card"); the monitor detects the card at power-on only (ZXBY `app.js` `web_gmx_cold`) | LW, ZXBY |
| Kempston joystick `#1F` (DOS off), Kempston mouse `#FADF/#FBDF/#FFDF` (buttons, X, Y, wheel nibble) | MAME `:342-346` | MAME |
| Covox `#DD`, Centronics, RS-232 (`#1FFD` bit 3) | MAME `:182-185, 644`; xpeccy-plus `scorpion.c` (`#DD` covox) | MAME, Xpeccy |
| Keyboard | ZX matrix; the ProfROM "Ext" key is CS+SS+key (ZXBY: "Tab = Ext", F8/F9/F10 = Ext+1/9/0 for OSZ); MAME carries the Scorpion PC-keyboard controller ROM `scrpkey.rom` (`:906-907`) but does not emulate it | ZXBY, MAME |
| General Sound | does not work with the card (1999 article, D07 Q14); MAME has a GS ROM region marked TODO (`:909-911`) | D07, MAME |
| ZX-bus | MAME: two `zxbus_gmx_cards` slots on all Scorpion classes (`:513-515`) | MAME |
| ESP-12 Wi-Fi card, BomgeMoon OPL3 | used by OSZ (`_Doc/ReadMe.txt`) | OSZ |

---

## 6. Reset and boot

1. **Power-on / RESET:** all GMX latches 0 (MAME `:836-851`, UNR `z80.cpp:97`), so plane 0 page 0 (the loader) is at
   `#0000`, `#8000` = page 2, turbo off, extended mode off, registers unblocked (MAME `:838`).
2. **Loader V2.00** (F644 plane 0 page 0): `DI; JP #00B5`; `OUT (#7EFD), #84` (turbo + bit 2); short delay;
   `SP = #5BFF`; `OUT (#78FD), 0`; read the magic byte (§2.3) and `#7AFD`; depack the main loader into `#5C01` (a
   bit-stream LZ unpacker at `#012A`) and `CALL #5C01` with the magic byte in A. The NMI vector at `#0066` jumps to
   `#0106` (a short handler ending in `RETN`). F125 starts the same way (D07 `roms.md` §2). ZXBY: "GMX Loader 2.00
   (Delete at start: Set Up)" - holding Delete at power-on opens the loader's setup menu.
3. The loader picks a **scheme** (FPGA configuration file in the flash: "DK_test" board test, "Pentagon",
   "PentaGON", "Komposit", "Work Sch[eme]" = Scorpion) and a **profile**: LW `changes.md:338-344` lists four after
   a reset - "Scorpion GMX 2048" (Scorpion INT, GMX ports open, 2048 KB), "Scorpion ZS 256" (Scorpion INT, ports
   blocked, 256 KB), "Pentagon GMX 2048", "Pentagon ZS 256" (Pentagon INT). "Ports blocked" = `#00` bit 5 (BLKEXT);
   how the INT type is selected in hardware is not in any source (O; `#00` bit 6 or 7 are the only unexplained stored
   bits, and bit 7 is read back in `#7EFD` bit 4 - a candidate, unverified). The setting lives in NVRAM (`:334`).
   Before starting a scheme the loader sets `I = 0` (`:504`). The emulators do not model the FPGA load: MAME and UNR
   run the loader as plain Z80 code and the scheme data pages are just read.
4. The Scorpion plane (4) runs the 128 menu with `#7EFD = #C0` (ZXBY reset macro waits for `(state & #FF) == #C0`
   for up to 1500 frames: "the loader, the monitor's test (and the SD card) first").
5. **NMI (Magic button) -> Shadow Service Monitor** (plane 5, "1993-1998 MOA, 2022-2025 PLM Shadow service monitor
   v6.44"). MAME: if `#7EFD` bit 2 = 0 the NMI is taken, the `#7AFD/#7EFD` read-backs are frozen, the Beta/DOS ROM
   path of the Scorpion is entered (`:706-721, 225-230`); the monitor then pages itself via `#1FFD`/`#7EFD`. A RAM0
   stub copied by OSZ to page 0 (it runs with RAM at `#0000`) shows the firmware's entry protocol: at `#0066` it saves
   AF, R, IFF, reads `IN H,(#7EFD)` and `IN L,(#7AFD)`, then `OUT (#1FFD), #12` (service ROM + page bit 3) from
   `#0033` so that execution continues in the ROM monitor; the exit at `#0084` restores `#1FFD`, `#7FFD` and port `#00`
   (`OUT (#0000), A`) (`os_main.asm:855-900`). This implies the NMI can land in RAM page 0 when `#1FFD` bit 0 is set,
   while MAME forces ROM during a pending NMI (`:194`) (O). Holding Symbol Shift on NMI enters the navigator
   (LW `changes.md:237`); in the monitor menu NMI calls the navigator (`:348`).
6. **Warm reset**: turbo kept, `I = 0`, the SD card is re-initialised only when "O. on warm reset" is on (LW
   `:335, 356, 455`).

---

## 7. Plain ZS 256 vs ZS 256 ProfROM (Turbo+) vs Scorpion + GMX

| Item | ZS 256 (yellow) | ZS 256 Turbo+ with ProfROM | Scorpion + GMX |
|:--|:--|:--|:--|
| RAM | 256 KB (MAME `scorpio` `:489`) | 256 KB, 1 MB option (`:572`, `#1FFD` bits 6-7) | 2 MB on the card, `#DFFD` + `#78FD` |
| `#8000` window | page 2 fixed | page 2 fixed | any page (`#78FD` XOR 2) |
| ROM | 64 KB (4 pages) | 128-256 KB ProfROM (v3.9f 128K, v4.x 256K; MAME `:914-976`), plane switched by reading `#0100-#010F` in the service page (GAL) | 512 KB flash, 8 planes, plane = `#7EFD` bits 6-4, frozen by `#00` bit 4 |
| Turbo | none | 7 MHz, `IN` strobes of the `#7FFD`/`#1FFD` families, EPLD waits | 7 MHz by `#7EFD` bit 7 (strobes O) |
| Video | ZX | ZX | ZX + 640x200x16 with attributes, hardware scroll |
| Extra ports | - | (AY port A read-back) | `#00`, `#78FD`, `#7AFD`, `#7CFD`, `#7EFD`, `#DFFD` |
| DOS forcing | magic/NMI | magic/NMI | also `#1FFD` bit 2 |
| Even M1 | yes (yellow) | no / SC15.1 dependent | off in MAME |
| Firmware | ROM 2.9x | ProfROM 3.9-4.xx (MOA, then LW 4.xx) | GMX loader + ProfROM 5.xx/6.xx (MOA, then LW/PLM 2022-2025) |
| Schemes | - | - | FPGA schemes: Scorpion, Pentagon, Komposit, board test |

unreal-ng note: `MM_PROFSCORP` already uses `#7EFD` as a "ProfROM window latch" with bits 5:4 (and 6 as an emulator
extension) selecting 256 KB quadrants of a >256 KB ProfROM image (`memory/scorpion/scorpionromwindow.h:10-19`,
`portdecoder_scorpion256.cpp:504-519`). That is an emulator convention for large ProfROM images, not the GMX
register: on GMX the full `#7EFD` byte is the mode register above.

---

## 8. Where a GMX variant plugs into unreal-ng (paths under `core/src/emulator/`)

| Piece | Today | Where |
|:--|:--|:--|
| Model row / enum | `MM_GMX` exists, "ZS Scorpion + GMX", 2048K | `platform.h:345`, `config.h:69` |
| ROM path | `[ROM] GMX=` read into `gmx_rom_path` | `config.cpp:542` |
| ROM loader | `case MM_GMX: assert("Not implemented")` twice; 32-bank size check exists | `memory/rom.cpp:156-158, 321-323, 405-411` |
| Memory class | Core creates `ScorpionMemory` only for `MM_SCORP/MM_PROFSCORP` | `cpu/core.cpp:117-118` |
| Bank translation | `ScorpionMemory::UpdateModelBanks` (bank3 formula, bank0 chain, ProfROM quadrant) - add the GMX branch here | `memory/scorpion/scorpionmemory.cpp:74-190` |
| ROM plane | `ScorpionRomWindow` (GAL strobe + `#7EFD` window) - GMX needs plane = `#7EFD` 6:4 without the strobe | `memory/scorpion/scorpionromwindow.{h,cpp}` |
| Port decoder | factory case shares `PortDecoder_Scorpion256` for SCORP/PROFSCORP; `IsModelSupported` lacks GMX | `ports/portdecoder.cpp:126-150, 197-202` |
| Decoder arms | IN turbo strobe switch `:245-256`; ProfROM `#7EFD` IN `:345-352` and OUT `:504-519`; `#7FFD`/`#1FFD` masks `:743-797`; AY `:556` | `ports/models/portdecoder_scorpion256.cpp` |
| State | `ScorpionState` (turbo, p7EFD, profromBank, dosTrigger, SMUC) inside `EmulatorState::scorpion`; GMX needs p00, p78FD, scroll lo/hi, pDFFD, magic shift, magic lock + 2 frozen bytes | `platforms/scorpion/scorpionstate.h`, `platform.h:1346-1351` |
| Turbo | `SyncTurboWaits` / `ScorpionTurboOverlay`, `hw_turbo_ratio` | `portdecoder_scorpion256.cpp:136-214`, `memory/scorpion/scorpionturbooverlay.*` |
| Timing / Even M1 | Scorpion cases | `config.cpp:1727-1920` (`:1774-1775, 1843, 1868-1869`) |
| Creatable | needs `data/configs/gmx/` (no folder today; `profscorp/unreal.ini` is the template) | `config.cpp:1606`, `data/configs/` |
| Video detect | `DetectModeGMX`: `scorpion.p7EFD & 8 -> {M_GMX, R_320_200}` (the comment says "320x200", the mode is 640x200) | `video/screen.cpp:293-294, 465-473` |
| Renderer | `Screen::DrawGMX` empty; `M_GMX` geometry row is a ZX copy | `video/screen.cpp:2522-2525`, `video/screen.h:568, 665` |
| Raster table | `R_320_200 = {76, 276, 54, 214}` = UNR's numbers, usable for the 200 lines | `video/screen.h:494` |
| Snapshot / TTD | "GMX cannot be created yet" text; ksy mentions the GMX group | `loaders/snapshot/snapshotcapture.cpp:169`, `debugger/ttd/ttd.ksy:484` |

Precedents for an extended-video model on the ZX screen class:

- **Profi** (`MM_PROFI/MM_PROFI3`): own decoder `ports/models/portdecoder_profi.cpp` + board object
  `profiboard.{h,cpp}`, state `platforms/profi/profistate.h`, a renderer that is **not** a `Screen` subclass but is owned
  by `ScreenZX` (`video/profi/screenprofi.h:7-11`, created lazily in `ScreenZX::DrawRangeProfi`,
  `video/zx/screenzx.cpp:873-878`) with a geometry/addressing helper (`video/profi/profigeometry.h`) and a video
  mapper for the debugger (`profivideomapper.*`). Mode `M_PROFIHR`/`R_512_240`.
- **ATM** (`M_ATM16/M_ATMHR/M_ATMTX`, 640x200 hi-res already): `video/atm/screenatm.*`, `atmgeometry.h`,
  `atmvideomapper.*`, state `platforms/atm/atmstate.*`, dispatched by `VideoFamily::Atm` in
  `ScreenZX::SelectRangeRenderer` (`screenzx.cpp:95-118`); `RenderFrameBatch` runs the per-T renderer for these
  modes (`screenzx.cpp:1329-1342`). The `M_ATMHR` geometry row is already 640x200 (`video/screen.h:561`).
- **TS-Conf**: its own `Screen` subclass `ScreenTSConf` (`video/videocontroller.cpp:14-15`), own memory class
  `TsConfMemory` (`cpu/core.cpp:119-120`) and a whole `platforms/tsconf/` - the heavy route, not needed for GMX.

The GMX fits the Profi/ATM pattern: a `VideoFamily::Gmx` (or reuse of the ATM hi-res family with GMX addressing), a
`video/gmx/screengmx.*` owned by `ScreenZX`, a `GmxState` next to `ScorpionState`, and a GMX branch in
`ScorpionMemory` and `PortDecoder_Scorpion256` (D07 §3 has the same plan).

---

## Sources

Local (read-only):

- MAME `local clone of mame/src/mame/sinclair/scorpion.cpp` (1069 lines; git log of the
  file shows one squashed commit `f43983b6`, no separate GMX history)
- zx-evo-unreal `local clone of zx-evo-unreal/Unreal/` (tslabs, HEAD `86fd99b`
  2025-08-26): `io.cpp:534-582, 700-720, 1297-1336`, `memory.cpp:30-113`, `draw.cpp:11-14, 772-775`,
  `drawers.cpp:591-640`, `vars.cpp:274`, `config.cpp:292, 984-986`, `z80.cpp:97`, `emul.h:71, 300, 486`,
  `cfg/Unreal.ini:540-541`
- xpeccy-plus `local clone of xpeccy-plus/src/libxpeccy/hardware/scorpion.c`,
  `docs/machines-reference.md` (Turbo+ frame 316 lines, 70784 T)
- ZXMAK2 `src/ZXMAK2.Hardware/Scorpion/MemoryScorpionProfRom256.cs:28-80` (ProfROM strobe, any `#0100-#010F`)
- Scorpion256TPlus `ROM/LW/changes.md`, `ROM/LW/README.md`, `doc/files/Scorpion_ProfROM_Paging.md:333-335`
- OSZ `OSZ@2e1c9fd:OSZ/Drv/zsgmx.asm`, `os_main.asm`, `_Doc/ReadMe.txt` (cp866) - upstream
  https://github.com/izzx-git/OSZ, forum https://zx-pk.ru/threads/35996-os-dlya-zs-gmx.html
- EdgeGrinder GMX `EdgeGrinder@6639aa1:LoaderGMX.asm`, `EG_Interrupts2.asm` - upstream
  https://github.com/izzx-git/EdgeGrinder
- zx.by web Unreal `zxby.org/unreal/ (fetched 2026-10-30) page.html`, `js/app.js`, `x/gmx644.rom`, `x/unreal.wasm` (exports
  `web_gmx_state/out/cold/on/scroll`)
- pico-speccy (fetched with `gh api` at `5fb5470c`): `pico-speccy@5fb5470c:Ports.cpp`,
  `Scorpion.cpp`, `Video.cpp`, `scorpion_gmx_banks.h` - upstream https://github.com/DnCraptor/pico-speccy
- Firmware scans: per-page CRC32 / string / `LD BC,nnnn` scan and a z80dasm listing of F644 plane 0 page 0 (throwaway scripts)
- Repo: `docs/inprogress/2026-10-07-gmx/*`, `docs/inprogress/2026-09-29-machine-waits/research-scorpion-turbo.md:392`,
  `docs/inprogress/2026-10-03-zx-bus-slots/research-machines.md:478`, `docs/hardware/zx-ports-full-table.md` (the
  translated table lists "B - Scorpion GMX" as a configuration code but dropped the GMX rows; D07 used the original
  Black_Cat PDF)

Web: see [Web sources](#web-sources).

## Web sources

- [MAN]: https://web.archive.org/web/2006id_/http://www.scorpion.ru:80/spectrum/hard/gmx.zip ("Документация к GMX271198.doc")
- Official page: http://web.archive.org/web/2001/http://www.scorpion.ru/spectrum/hard/gmx.htm
- ZX-Pilot #31 (Apollonov): https://zxpress.ru/article.php?id=9138 ; City #18: https://zxpress.ru/article.php?id=10414
- My Speccy #01 (Trident): https://zxpress.ru/article.php?id=10696
- Spectrum Expert #01 (preliminary port table): https://zxpress.ru/ru/ezines/spectrum-expert/01/gmx-video-kartochka-rasshireniya-grafiki-dlya-zx-spectrum-scorpion-s-podderzhkoy-vysokih
- ZX-Format #07: https://zxpress.ru/ru/ezines/zx-format/07/plata-rasshireniya-gmx-dlya-scorpion-256-rasshirenie-grafiki-pamyati-i-vozmozhnostey-kompyutera-s
- zx-pk ports thread: https://zx-pk.ru/threads/7050-opisanie-portov-zx-spectrum.html ; GMX summary: https://zx-pk.ru/threads/31334.html (docs rev.2022 https://disk.yandex.ru/d/1kVVfxOMIREqBw , schematic https://disk.yandex.ru/i/fjcXk_U2pRvHjA - not fetched)
- OSZ: https://github.com/izzx-git/OSZ , https://zx-pk.ru/threads/35996-os-dlya-zs-gmx.html ; EdgeGrinder: https://github.com/izzx-git/EdgeGrinder
- MAME: https://github.com/mamedev/mame/blob/master/src/mame/sinclair/scorpion.cpp (PRs #11439, #11466)
- pico-speccy: https://github.com/DnCraptor/pico-speccy , https://github.com/billgilbert7000/pico-speccy
- Not reachable: speccy.info (403), t.me/ZS_ProfRom (no public preview)

## Open questions (after [MAN])

Resolved by [MAN]: `#7EFD` bit 2 (both effects), scroll semantics (byte address, low 4 bits zero), INT type (`#00`
bit 7), the magic code / scheme meaning, `#00` bits 0-6, the flash map.

1. **Screen 1**: [MAN] says `#3A/#7A`, every emulator `#3B/#7B`. Follow [MAN]; check whether `#3B` aliases on some
   SIMM fits, and whether EG (uses `#3B/#7B`) was tested on hardware.
2. **Turbo waits**: ZX-Format says 1.8-1.9x from RAM, 2x from ROM; the wait rule is unknown. Turbo+ IN strobes on GMX
   still unknown (UNR none, MAME yes).
3. **Extended-mode raster**: first line, border width (Apollonov: nearly full TV width), "640*200(208)"; Trident's
   "INT stretched to 132000".
4. **NMI into RAM page 0** when `#1FFD` bit 0 = 1 (OSZ stub implies yes, MAME forces ROM).
5. **Reset vs `#00`**: [MAN] keeps `#00` over RESET; what the loader does with a kept CONF/FIXROM.
6. **Flash chip and protocol**: 28F400 vs 28F040; commands only needed for the update path.
7. **ProfROM `#0100` strobe on GMX**: [MAN] does not mention it, PICO trace says off; MAME keeps it.
8. **SIMM holes**: which page numbers exist for 256K..2M fits (0-#F + #40-#4F with two 256K).
9. Pentagon scheme timings (320 lines, 80 lines INT-to-screen) and its port decode, if the Pentagon profile is
   emulated.
