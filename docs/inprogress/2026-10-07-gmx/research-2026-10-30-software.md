# Scorpion ZS 256 + GMX: what real software uses (EdgeGrinder, OSZ)

Research date 2026-10-30. Sources (read only, nothing executed):

| Project | Repository | Revision read |
|---|---|---|
| EdgeGrinder for ZS GMX (izzx, 2020-2021) | https://github.com/izzx-git/EdgeGrinder | `6639aa1a8f5d` (2025-05-16) |
| OSZ, a multitasking OS for ZS GMX (izzx, 2024-2025) | https://github.com/izzx-git/OSZ | `2e1c9fdb76f9` (2026-10-30) |
| Web emulator SD set | `testdata/machines/gmx/osz/osz-sd.zip` (osz.vhd + OS.TRD + osz.json) | - |
| GMX flash | `data/rom/gmx644.rom` | - |

Paths below are relative to `EdgeGrinder@6639aa1:` (EG) and `OSZ@2e1c9fd:OSZ/` (OSZ).
EdgeGrinder sources are UTF-8, its `Docs/*.txt` are cp1251; OSZ `_Doc/ReadMe (Win).txt` is cp1251, `_Doc/ReadMe.txt`
and `Release/OSZ/ReadMe.txt` are the same text in cp866. The port lists were made with throwaway grep scripts
(not kept); every row cites its source line.

Cross-reference: the open questions Q1-Q17 are in
[TODO.md](TODO.md); findings that bear on
them are marked **(Qn)**.

## 0. Key findings for the emulator

1. **Screen 1 is `#3B` / `#7B`** (Q11). EdgeGrinder double-buffers by flipping `#7FFD` bit 3 every game frame and
   draws into page `#7B` while screen 0 (`#39` / `#79`) shows, and into `#79` while screen 1 shows
   (EG `Compiled_Main3.asm:586-608`, `:428-439`; `LoaderGMX.asm:45-54`). The author says it ran on real hardware
   (Docs/Hystory.txt 2021.07.06, 2021.08.25), which would flicker if screen 1 were not `#3B` / `#7B`. OSZ reserves
   `#3A` / `#7A` as "video pages" (`os_main.asm:1545-1556`, `_Doc/ReadMe (Win).txt`) but its own driver labels the
   `#7FFD` bit 3 screen "экран 3b" (`Drv/zsgmx.asm:448`) and hands page `#3B` to applications (`Drv/zsgmx.asm:471`).
   OSZ copied the article's numbering; the working code agrees with the three emulators.
2. **`#7EFD` is written as `#C8` (extended on) and `#C0` (extended off)** by both programs
   (EG `LoaderGMX.asm:978-985`; OSZ `Drv/zsgmx.asm:294-316`). So bit 7 turbo = 1, bits 6-4 = `100` (ROM
   plane 4, the Scorpion plane per `roms.md`), bit 3 = extended mode. Software rewrites the plane bits on every
   mode switch, so plane 4 must be the plane the Scorpion scheme runs in (or `#00` bit 4 must lock the plane)
   **(Q1, R12)**. Neither program writes turbo anywhere else; "works at 7 MHz" (EG doc) comes from this bit.
3. **Hardware scroll, OSZ only** (Q4): a byte offset written to `#7AFD` (low) and `#7CFD` (high), range 0..15999,
   always a multiple of 640 (one 8-line text row), wrapping at 16000 (`Drv/zsgmx.asm:324-367`); the software adds the
   same offset to its own address arithmetic and wraps at 16000 (`Drv/zsgmx.asm:232-249`). Values are `k*640`, so the
   low byte is always `#00` or `#80` and the high byte <= `#3C`: the byte-offset and the `offset/80` line models give
   the same picture, and `& #F0` on `#7AFD` is harmless. OSZ cannot decide Q4 between those models, but a fixture can
   pin "offset in bytes = 80 x lines, wrap at 16000 for pixels and attributes alike". EdgeGrinder uses no hardware
   scroll; its doc says the GMX has "only a vertical" hardware scroll.
4. **Read-backs are load-bearing** (R14). The OSZ interrupt handler reads the current pages back every frame:
   `IN #7AFD & #7F` = page in slot 3, `IN #78FD xor 2` = page in slot 2 (`os_main.asm:2299-2306`). A wrong read-back
   corrupts the scheduler's page state on the next task switch.
5. **RAM page 0 instead of ROM, IM 1 in RAM.** Both programs set `#1FFD` bit 0, put a `JP` at `#0038` in page 0 and
   run IM 1 (EG `Compiled_Main3.asm:178-208`, `LoaderGMX.asm:753-755`; OSZ `os_main.asm:846`, `:928-955`). Neither
   uses IM 2. Both assume a 50 Hz INT (EG frame counter divides 50 by interrupts per frame, `EG_Panel.asm:659-690`).
6. **The shadow monitor is the disk driver for OSZ.** OSZ never touches Z-Controller or SMUC ports: it calls the
   LW/PLM monitor through `RST #08` + `DB #81` (R8DOS), `#91` (R8FAT), `#8E` (R8CONF), `#8F` (resident)
   (`Drv/zsfat.asm:20-62`, `os_main.asm:962-969`). The RST #08 / NMI entry stubs in RAM page 0 copy the ROM's own
   trampoline: write `#02` to port `#0000`, read `#7EFD` and `#7AFD`, then `OUT #1FFD,#12` (service ROM) from an
   `OUT (C),A` placed at `#0013` / `#0033` so the next fetch comes from the ROM (`os_main.asm:817-900`). The emulator
   must switch the `#0000-#3FFF` mapping between the `OUT` and the next opcode fetch.
   **Note:** the design table says a write to `#00` with bit 4 = 0 resets the CPU; OSZ writes `#02` there on every
   monitor call, and the v6.44 flash itself contains the same `LD BC,#0000 / LD A,#02 / OUT (C),A` (plane 4 page 0
   `+#2380`, plane 7 the same). Either the reset rule is wrong as stated, or it applies only in another state
   (loader / scheme load). Check this before implementing the `#00` reset (design phase 4).
7. **Firmware: OSZ needs the new flash.** OSZ requires "fresh firmware by LW/PLM, version about 40 and above"
   (ReadMe). `gmx644.rom` is "TMgmx(r) Loader V2.00 ... (c) 2024 LW/PLM" with "Shadow service monitor v6.44su,
   #25AB, 21.08.2025" and a "ScorpionFAT32" module in plane 5. The repository's `data/rom/gmx.rom` (loader V1.20,
   monitor 5.00) differs in 30 of 32 pages and lacks the R8FAT API. This is the newer firmware Q16 asked for.
8. **EdgeGrinder frame-rate counter is a timing probe.** In game, key R shows frames per second = 50 / interrupts per
   game frame. History: "on the emulator 25 fps, on real hardware 16" (2021.08.01), later "25 fps on real hardware"
   (2021.08.25). The final build should show 25 on a correctly timed 7 MHz GMX (if the turbo wait states are right).

## 1. Ports used

### 1.1 EdgeGrinder

| Port | Access | Meaning per code / comments | Where |
|---|---|---|---|
| `#7FFD` | W | Slot 3 page bits 0-2, bit 4 = 1 (48 ROM select, kept), **bit 3 = screen 0/1**. `#11` = screen 0 + page `#79`, `#13` = screen 0 + page `#7B`, `#19` = screen 1 + `#79`, `#1B` = screen 1 + `#7B` (high page bits come from `#1FFD`/`#DFFD`) | `LoaderGMX.asm:982-997`, `:1070-1074`; `Compiled_Main3.asm:586-608` |
| `#1FFD` | W | bit 0 = RAM page 0 at `#0000` ("ROM OFF", `#11`), bit 4 = page bit 3 | `LoaderGMX.asm:753-755`, `:1020-1025`, `:1064-1068` |
| `#DFFD` | W | Page bits 4-6 (`(page >> 4) & 7`) | `LoaderGMX.asm:1033-1039`, `:1076-1082` |
| `#78FD` | W | Slot 2 (`#8000`) page, written as `page xor 2` | `LoaderGMX.asm:972-976`; `Compiled_Main3.asm:995-1001`; `EG_Interrupts2.asm:24-27`, `:460-463`, `:903-907` |
| `#7EFD` | W | `#C8`: "enable screen 640*200" (turbo + plane 4 + extended) | `LoaderGMX.asm:978-981` |
| `#FFFD` / `#BFFD` | W | AY register / data: `C=#FD`, `B=#FF` ("порт регистра"), `B=#BF` ("порт данных"), `OUTI` | `ArkosTrackerPlayer_CPC_MSX.asm:1121`, `:1138-1141` (x14) |
| `#xxFE` | R | Keyboard half-rows (`#BFFE` H, `#FBFE` R, `#FDFE` F, table `DKEY` for Q/A/O/P/N/M/Space/Enter, Sinclair 1 = `#EFFE`) | `EG_Move3.asm:313-336`, `:378-392` |
| `#1F` | R | Kempston ("KBDJOY ... (c) DragonsLord") | `EG_Move3.asm:353` |
| `#FE` | W | border writes all commented out (debug colours) | `EG_Interrupts2.asm:23`, `Compiled_Main3.asm:409` |

Also used: TR-DOS `#3D13` (functions `#18` set up disk, `#13`, `#0A`, `#08`, `#05` read sectors) for loading,
before ROM is switched off (`LoaderGMX.asm:19-20`, `:889-914`); `POKE #5B5C,0` "system variable for the shadow
monitor" (`LoaderGMX.asm:12-13`). Not used: `#7AFD`, `#7CFD`, `#00`, SMUC, Z-Controller, IM 2, the CPC CRTC code
(left commented in `EG_Interrupts2.asm`).

### 1.2 OSZ kernel and applications

| Port | Access | Meaning per code / comments | Where |
|---|---|---|---|
| `#7FFD` | W | Same slot-3 driver as EG; bit 3 screen select (`#10` screen 0/5, `#18` screen 1/7, self-modified `PageSlot3Scr`) | `Drv/zsgmx.asm:377-421`, `:424-454`; monitor exit `os_main.asm:895-897` |
| `#1FFD` | W | `#01` RAM page 0 at `#0000` ("ОЗУ вместо ПЗУ"); bit 4 page bit 3; `#12` on monitor entry (service ROM + bit 4) | `Drv/zsgmx.asm:396-399`; `os_main.asm:928-933`, `:866-868`, `:886-888`, `:895` |
| `#DFFD` | W | Page bits 4-6 | `Drv/zsgmx.asm:410-414` |
| `#78FD` | W/R | W: slot-2 page `xor 2`; R: current slot-2 page (`xor 2` again) | `Drv/zsgmx.asm:492-496`; `os_main.asm:2303-2306` |
| `#7AFD` | W/R | W: hardware scroll low byte; R: current slot-3 page (`and #7F`); also read on monitor entry | `Drv/zsgmx.asm:362-364`; `os_main.asm:2299-2302`, `:864-865`, `:884-885` |
| `#7CFD` | W | Hardware scroll high byte | `Drv/zsgmx.asm:365-367` |
| `#7EFD` | W/R | W: `#C8` extended on, `#C0` extended off (turbo stays on); R: saved on monitor entry | `Drv/zsgmx.asm:294-316`; `os_main.asm:862-863`, `:882-883` |
| `#0000` | W | `#02` written before entering the monitor; the value returned by the monitor written back on exit (GMX global port `#00`) | `os_main.asm:859-861`, `:898-899` |
| `#FE` | W/R | Border 0; keyboard (`DrvKey`, LW/PLM driver) | `os_main.asm:13`, `:265`, `:2479`; `Drv/DrvKey.asm.a80:385-389` |
| `#FFFD` / `#BFFD` | W | AY / TurboSound PT2/PT3 player (S.V.Bulba), called from the interrupt | `player.asm:1361-1379`, `:1446-1463` |
| `#F8EF`-`#FFEF` | W/R | ZX WiFi card: 16C550 UART, register in the high byte (`RBR_THR=#F8` ... `SR=#FF`), ESP-12 AT commands, 115200 | `Drv/zx-wifi.asm:1-117`; `os_main.asm:1795-1835`, `:2687` |
| `#BB` / `#B3` | W/R | General Sound command / data (MOD in NC GPlay, MoonR) | `NC/GPlay/gplay.asm:507-508`; `MoonR/drivers/general-sound.asm:22-23` |
| `#24`-`#27` (or `#C4`-`#C7`), `#C2`/`#C3` | W/R | OPL3/OPL4 (BomgeMoon card at `#24-#27`; "for the emulator change `MOON_BASE` to `#C4`"), wave ports `#C2/#C3` | `NC/GPlay/common/moonsound.asm:1-9` |
| `#F0C1`-`#F3C1` | W/R | YM2151 OPM x2 (VGM player) | `NC/GPlay/common/opm.asm:4-7` |
| `#DFBA` | via TR-DOS | SMUC RTC (DS12887-style), through the TR-DOS ROM `OUT (C),A` at `#2A53` / `IN A,(C)` at `#3FF3` trampoline `#3D2F`; only in MoonR's `smucrtc` build variant | `MoonR/drivers/smuc-rtc.asm:8-39`; `MoonR/_Build_zsfat_zsgmx_smucrtc.bat` |

MoonR also carries drivers for other machines (ZX-Uno UART `#FC3B/#FD3B`, MB03 UART `#xx3B`, AY bit-banged UART,
Timex `#FF`); the OSZ build uses the GMX/zsfat variants only.

Not touched by any OSZ/EG code: Z-Controller `#57/#77`, SMUC IDE `#xxBE`, `#xxDF`, Dallas/SMUC RTC
(except the MoonR variant above). The flash firmware does use them (byte-pattern count in `gmx644.rom`: `OUT (#57),A`
in planes 4/6/7, `IN A,(#57)` / `IN A,(#77)` in planes 5/6, `LD BC,#FFBE` in planes 3/5), which is how OSZ reaches
the SD card.

Small defect worth knowing: `Uart.check` reads `IN A,(#FE)` with `A=#F8` (a keyboard read, not the UART), and
`net.apg` has the "no card" branch commented out, so it always calls `Wifi.init` (`Drv/zx-wifi.asm:23-31`,
`os_main.asm:516-524`). Without a UART the floating bus returns `#FF`: LSR says "data ready", RBR returns `#FF`
forever, and `checkOkErr` (`Drv/wifi.asm:98-120`) never sees `O`/`E`/`F`; the `net` process spins (the OS still
preempts it). Tests should use an `autorun.txt` without `net.apg`.

## 2. Video, turbo, interrupts, memory

### 2.1 640x200 extended mode

- **Layout** (both programs): linear, 80 bytes per pixel line, 200 lines = 16000 bytes; text rows of 8 lines =
  640 bytes (`Drv/zsgmx.asm:3-8`, row table `:499-528`; EG `EG_Display3.asm` strides `80`, `8*80`, `21*640`).
  Attributes are the same shape in page `pixel + #40`: one attribute byte per 8x1 pixels, Spectrum bit layout
  (EG fade separates `and 7` ink and `and 56` paper, `LoaderGMX.asm:709-724`; OSZ writes 8 attribute bytes per
  character, `Drv/zsgmx.asm:166-180`).
- **Pages**: screen 0 = pixels `#39`, attributes `#79`; screen 1 = `#3B` / `#7B` (EG, see section 0). Selected
  by `#7FFD` bit 3 while `#7EFD` bit 3 is set.
- **EdgeGrinder's trick** (from the Evo port): the loader fills both pixel pages with `#0F` (left 4 pixels paper,
  right 4 pixels ink) and never touches pixels again; all drawing writes attribute bytes, so one byte = two "fat
  pixels" (paper = left, ink = right), an effective 160x200 in 8 colours, as the Amstrad 160x200 mode
  (`LoaderGMX.asm:45-54`; Docs/Этапы). The loading picture is the Amstrad image converted into page `#79`
  (`LoaderGMX.asm:56-76`). OSZ `tetris.apg` uses the same "pseudo 160x200" (ReadMe).
- **Palette**: none. 8 colours from the standard attribute (EG: "graphics converted to 8 colours"; the
  `ConvertGFX/*Palette.asm` files only map CPC colours to the 8 Spectrum colours at build time).
- **Scroll**: EG scrolls in software (pre-rendered map, `LDI` chains, `do_scroll_new`, `Compiled_Main3.asm:1003+`;
  title text with `LDD` after a beam-wait loop "calculated for 7 MHz", `EG_Panel.asm:405-425`). OSZ uses the
  hardware scroll for the console (section 0, item 3), written every interrupt by the focused process
  (`os_main.asm:2475`, `Drv/zsgmx.asm:348-367`).
- **Double buffering**: EG flips screens every game frame (`scroll_step` bit 0) and draws into the hidden screen's
  attribute page; the loader fade-out and the end sequence also flip per frame (`LoaderGMX.asm:689-747`,
  `Compiled_Main3.asm:765-790`). OSZ keeps per-process buffer pages (two pages per process for the screen
  buffer, only `#39` is saved on a task switch, `os_defs.asm:268-273`).
- **Border**: black (`OUT #FE,0`), no border effects.
- **Mode switches**: OSZ switches back to the normal screen with `#7EFD=#C0` for screen 5/7 apps (`set_scr5`,
  `set_scr7`); NC's SCR viewer and ArtView show 6912-byte screens.

### 2.2 Turbo

Only `#7EFD` bit 7, always written as 1 together with the mode (`#C8` / `#C0`). EG assumes 7 MHz in a beam-wait
delay loop (`EG_Panel.asm:407-418`); the doc: "Работает в режиме 7Мгц". No `IN` from `#1FFD`/`#7FFD` (Turbo+
strobe, Q5) anywhere.

### 2.3 Interrupts

- **EG**: `DI`, `SP=#38`, writes `JP int_start` at `#0038` in RAM page 0, `EI`, IM 1 (default after the ROM)
  (`Compiled_Main3.asm:178-208`). The handler saves all registers on its own stack, maps page `#06` into slot 2,
  counts interrupts, runs the title/game routine, reads the keyboard, plays the AY music from page `#3E`, restores
  slot 2 from `CurrentBank` (`EG_Interrupts2.asm:8-32`, `:451-480`, `:903-955`). The main loop waits with `HALT`.
  Frame rate = 50 / interrupts per game frame.
- **OSZ**: `IM 1`, `JP Interrupts` at `#0038` in page 0 (`os_main.asm:846`, `:955`). The handler is preemptive: it
  saves the context, increments a 32-bit timer, scans the keyboard, plays AY for a "real-time" process, calls
  every process's registered interrupt hook with its own pages, then switches to the next process's stack and
  re-writes the hardware scroll (`os_main.asm:2196-2500`). Up to 8 processes, 8 open files.

### 2.4 Memory (2 MB, 128 pages)

Page number `p` (7 bits) -> `#7FFD` bits 0-2 = `p & 7`, `#1FFD` bit 4 = `p` bit 3, `#DFFD` bits 0-2 = `p >> 4`;
slot 2 independent through `#78FD` (`p xor 2`). Both drivers are identical in this
(`LoaderGMX.asm:1045-1084`, `Drv/zsgmx.asm:377-421`). EG's variant `PageSlot3R` also keeps `#1FFD` bit 0.

| User | Pages |
|---|---|
| EG | slot 0 = page 0 (code at `#00D0`); slot 1 = page 5 (loader `#6200`, later code); slot 2 = `#06` panel, `#3E` music, `#3F` temp, `#02`; slot 3 = sprites `#01`, `#03`, `#07`, `#0A-#0F`, map `#04`, pre-rendered map from `#10` in 4 bands of 10 pages and a 1-pixel-shifted copy at `+#30` (about `#10-#37`, `#40-#67`), screens `#39/#3B/#79/#7B`. "84 pages of 128 used" (Hystory 2021.08.01). Map pre-render takes ~15 s on the real machine |
| OSZ | page 0 = kernel instead of ROM; 5 = system; 1-4, 6 reserved for "mono" mode; video 5, 7, `#39`, `#3A`, `#79`, `#7A`; `#77-#7F` used by the shadow monitor; every process gets 4 pages in `#8000`/`#C000` and can request more (page table `Drv/zsgmx.asm:459-488`) |

## 3. The Russian documents, translated and summarised

### 3.1 EdgeGrinder `Docs/Этапы длинного пути.txt` ("Stages of a long road", 2021, izzx)

Edge Grinder is a C64 game with ports to the Amstrad CPC 128, Evo BaseConf and Evo TS-Conf. izzx ported it to the
ZS Scorpion GMX because there was nothing to play, the game almost fits the machine, the Evo trick (fill the pixel
area with vertical stripes and then change only attributes) saves T-states, and he wanted to see how a Z80 copes
with a 16 KB screen and to learn sjasm. It is "a port of a port": the Amstrad CPC sources were edited, Russian
comments mark his changes.

Result: graphics converted to 8 colours; music on the MSX frequency table; **runs at 7 MHz; uses two screens;
16 frames/s; byte-wise scroll (two "pixels" at a time)**; memory: slot 0 main code and music, slot 1 loader and extra
code, slot 2 extra code pages and graphics, slot 3 screens; the whole map is pre-rendered into memory before the game
(about 30 pages, 15 s wait).

Platform comparison: Evo BaseConf has a similar screen but another structure and uses all four memory windows
("the GMX has only 2.5 windows"). The Amstrad has the same Z80, a similar mode (160x200 is like the GMX 640x200
"with stripes", but 16 of 27 colours and a hardware **horizontal** scroll, **while the GMX has only a vertical
one**), similar banking, the same AY-3-8912.

Porting problems: page 0 replaces the ROM, so sjasm cannot make an SNA for quick testing (a TRD that autoboots
instead); **on real hardware the shadow monitor does not work with page 0 instead of ROM and with the extended
screens, so the code cannot be debugged there**; no hardware horizontal scroll, the 160x78-byte background copy
costs most time (`LDIR` -> `LDI` helped), the game runs slower than the original; vertical-scroll games would suit
the ZS better. "This seems to be the first game ever for the ZS extended screen." Update: faster versions followed.

### 3.2 EdgeGrinder `Docs/Hystory.txt` (history)

- 2020.10.05 idea; 2021.07.04 first working version **in the emulator**; 2021.07.06 **works on real hardware**;
  2021.07.18 faster scroll; 2021.07.19 pixel scroll.
- 2021.08.01 faster sprites; **"25 frames on the emulator, 16 on real hardware"**; autofire; 84 of 128 pages used.
- 2021.08.25 faster background; **25 frames on real hardware**.
- 2021.09.03 faster sprites at the screen edge, animation speed fixed.
- 2021.09.09 linear sprite format; the final screen and the loader fade use two screens.

### 3.3 EdgeGrinder `Docs/ReadMe.txt`

Forum thread https://zx-pk.ru/threads/33491-edge-grinder-dlya-zs-gmx.html; thanks to reddie, Evgeny Muchkin, jerri.
Build with `_All.bat`, **which starts the Unreal emulator automatically** (`unreal.exe EGGMX.trd`). Graphics
conversion and sprite/background code generators are C# tools. The game ends when all enemies are gone (marker 254
in `EG_Wave_Data_Converted.asm`) with lives left, or by writing `#80` to `Shield`; the source marks where to enable
infinite lives. File map: `boot.B` start; `EG.B` BASIC loader; `DISC.C` loader + map generator (bank 5, `#6200`);
`LOADSCR.C` loading picture (bank `#79`, `#C000`); `EDGEGRND.C` main code (bank 0 and 5, `#00D0`); `MUSIC.C` AY
music + player (bank `#3E`, `#8000`); `SPRITES.C` (bank 3), `SPRITESS.C` laser-hit sprites (bank 1), `CS.C`
player/laser compiled sprites (bank 7), `CS*_*.C` enemy compiled sprites (banks 10-15), `BG.C` map and tiles
(bank 4), `PANELBLK.C` title-screen code and graphics (bank 6). Sprite and map formats are described (126-byte
sprites 6x21, zero bytes skipped so no mask; the map is pre-rendered in rows of 409 bytes over 8 pages per
quarter, the second set shifted by one pixel).

BASIC loader (`EG.B`, detokenised): `BORDER 0: PAPER 0: INK 7: CLS: CLEAR 25087`, controls screen (Kempston,
Sinclair 1, Q/S up, A/X down, O left, P right, N/M/Space/Enter fire, H pause/break, **R frame rate**, F autofire),
`PAUSE 0`, then `RANDOMIZE USR 15619: REM : LOAD "DISC" CODE 25088: RANDOMIZE USR 25088`. `boot.B` does
`RANDOMIZE USR 15619: REM : RUN "EG"`. No turbo poke in BASIC.

### 3.4 OSZ `_Doc/Идея.txt` (idea, 2024-09-04)

An own multitasking OS for the Scorpion GMX, compatible with nothing. Borrow NedoOS's page juggling: the GMX selects
the upper two windows, all applications live at `#8000-#FFFF` in turn; the OS sits in banks 0 and 5 at
`#0000-#7FFF`. One application may ask for exclusive mode with the screen and memory from `#4000` (or `#6000`).
CP/M compatibility judged not worth it. Running TRD programs with return to the OS is important. Disk access first
through the new Scorpion ROM.

### 3.5 OSZ `_Doc/History.txt`

2024.12.10 two applications talk to the ESP through system calls in turn. 2025.06.18 Download folder, MoonR saves
there; Radio/ArtView save files (names not checked, duplicates possible); NC viewer scrolls by line/page/left-right;
calls `OS_FILE_POSITION`, `OS_FIND_PATH`. 2025.06.19 NC navigation, sort button. 2025.07.24 GPlay in NC (VGM, OPL2-3,
BomgeMoon card at `#24-#27`), SCR viewer. 2025.08.07 faster sorting; VGZ (unpacks in memory); "mono" mode: one
application may use `#4000-#FFFF`. 2025.08.22 text editor in NC; faster navigation; part of GPlay in a separate file;
MOD and PTx.

### 3.6 OSZ `_Doc/ReadMe (Win).txt` = `Release/OSZ/ReadMe.txt`

Multitasking OS for the ZS GMX, izzx 2024-2025, in development. Forum: https://zx-pk.ru/threads/35996-os-dlya-zs-gmx.html.

- **Hardware**: supports SMUC and ZC, FAT32 ("what the monitor can do"); **needs fresh firmware by LW/PLM,
  version about 40 and above**; up to 8 processes and 8 open files; **uses 2 MB RAM and the 640x200 screen**;
  built-in support for the ZX WiFi card (ESP-12).
- **Start**: copy `Release/OSZ` to a FAT32 disk and run the hobeta `os.$c` from the navigator; or the HDD boot menu
  (monitor Disk utility: from drive = File, HDD file = `os.$c`); or the floppy image `OS.TRD`.
- **Keys**: SS+Enter switch tasks; CS+1 Rus/Lat; CS+2 Caps Lock; CS+3 PgUp; CS+4 PgDn; CS+9 graphics; Ext+0 shadow
  monitor (only on the system process screen; leave the monitor with its special key or you cannot return); Ext+1
  go to the system process; Ext+9 start NC; Break leaves an application (not all).
- **Files**: `Data` (gopher browser data), `Download\{ArtView,MoonR,Radio}`, `artview.apg` (zxart.ee SCR viewer),
  `autorun.txt` (not updated by `update`), `moonr.apg` (Moon Rabbit gopher browser: downloads, SCR, PT2/PT3/MOD),
  `nc.apg` (commander, max 512 files / first 16 KB of a directory, optional first-letter sort and long names, text
  viewer of the first 16 KB, player: VGM/VGZ on OPL1-3 with the BomgeMoon card at `#24-#27` - "for the emulator change
  `MOON_BASE = 0xC4` in moonsound.asm and rebuild NC" -, MOD with a GS card, PT2/PT3 on AY, SCR viewer), `net.apg`
  (network system app, must run once), `os.$c` (kernel, hobeta), `OS.TRD` (kernel floppy, not updated by
  `update`), `radio.apg` (zxart.ee music), `sys.osg` (system process, always running), `terminal.apg` (UART terminal,
  blocks the ESP network), `tetris.apg` (pseudo 160x200 graphics on the 640x200 screen), `update.apg` (online update
  from the host in the 2nd line of `update.txt`; files up to ~30 KB, no subfolders).
- **Plans** (done items marked + in the original): TR-DOS disks and images, return to the OS on reset, porting apps
  (+ Moon Rabbit, Radio), + mono mode, + real-time processes per interrupt (AY player), + multitask ESP networking,
  big data in memory, remote terminal, network folders, mouse, + autorun, + NC, viewer, editor, + saving, +
  Download, + long names and sorting, GMX Pic View, +- music player, + OPL3 player, partition choice, FAT library
  v2, video player, NeoGS SD, file operations in NC, SMUC clock.
- **For programmers**: build on Windows with `OSZ`, `Release\OSZ` and `Unreal` (optional, **"an emulator set up for
  the GMX"**) side by side; `_Build_OS.bat` copies fresh files into the emulator's disk image `wc.img`
  (`dmimg.exe ... put`) and starts `..\Unreal\unreal.exe OS.trd`. System calls in `os_defs.asm`. **The system uses
  the shadow monitor's calls for disks. It lives in page 0 instead of ROM.** Pages 1-4, 6 reserved for mono mode;
  5, 7, `#39`, `#3A`, `#79`, `#7A` video pages; `#77-#7F` used by the shadow monitor; processes use the `#8000` and
  `#C000` windows, 4 pages each plus requests; two pages for a screen buffer; **default screen is the extended
  640x200; the hardware screen scroll is used**. Processes may print any time; only the focused one gets keys.
  Processes should use system calls and give time back; they should not change interrupts, the stack address,
  switch pages directly, access disks directly or through the monitor, or load the system with back-to-back calls.
- Links: BomgeMoon card (github.com/Kulicheg/BomgeMoon), OPL archive, ZX WiFi card (github.com/izzx-git/ZX-WiFi),
  Telegram "Scorpion ProfROM" (t.me/ZS_ProfRom). Code by LW/PLM (new monitor functions, keyboard), S.V.Bulba (PT
  player), Kulich (Radio), Nihirash (Moon Rabbit), ZORBA and GALSTAFF (OPL player); inspired by NedoOS (Alone Coder).

### 3.7 Emulator mentions, summary

- Both projects were developed against **Unreal Speccy** (Windows `unreal.exe`), configured for the GMX; OSZ ships
  the author's SD images for it: `Unreal/wc 2025 10 08.zip` and `Unreal/wc 2026 10 30  for Emul.zip`
  (3.39 MB each, one `wc.img` of 1 048 574 976 bytes inside).
- Known emulator divergence: EG ran at 25 fps in the emulator but 16 on the real machine (2021.08.01), i.e. the
  emulator was too fast in turbo / extended mode at that time.
- Known hardware limitation: the shadow monitor cannot debug code with page 0 instead of ROM or with the extended
  screen (EG); in OSZ, enter the monitor with Ext+0 only, or you cannot return.
- OPL3 port: the real BomgeMoon card is at `#24-#27`; the emulator (Unreal's MoonSound) needs `#C4-#C7` and a rebuild.

## 4. Licenses

| Item | Statement |
|---|---|
| EdgeGrinder repo | No LICENSE file, no license statement. `ReadMe.MD`: "Run EGGMX.TRD on ZS GMX. Based on source http://formatwar.net/view_article.php?art=collabortition_1" (the CPC/C64 sources released for the Format War "Collabortition" contest). Original game Edge Grinder (C64) by its authors; CPC port sources; GMX port by izzx; uses Arkos Tracker player (CPC/MSX version) |
| OSZ repo | No top-level LICENSE; README only describes the project. Contains third-party code: Moon Rabbit (`OSZ/MoonR/LICENSE`: Nihirash's "Coffeeware" license - "You may use all data from this project as you want", keep the coffee note), gunzip by Laurens Holst ("Copyright 2015", `NC/GPlay/common/gunzip.asm:5`), S.V.Bulba's PT player, LW/PLM keyboard driver, OPL player by ZORBA/GALSTAFF; `Other/` holds hardware files (BomgeMoon GAL, Karabas-OPL3 CPLD) |
| `gmx644.rom` | Flash image: loader (c) 1997-1998 Andrew MOA, (c) 2024 LW/PLM; monitor (c) 1993-1998 MOA, 2022-2025 PLM. No license text |

The owner permits ROMs and software in the repository; no further judgement.

## 5. Proposed `testdata/machines/scorpion-gmx/`

Existing pattern: one folder per machine, a `README.md` table "file | origin | what test uses it" with revision,
size and SHA-256 (`atm/README.md`, `sprinter/README.md`); SD cards as host folders (`zxevo/nedoos/sdcard/`,
`tsconf/wc-improved/sdcard/`) presented by the media manager as a synthesized FAT volume
(`core/src/emulator/io/storage/fat/fatsynthvolume.h` builds FAT16 or FAT32). The design names the model `GMX`
(`data/configs/gmx/`); `testdata/machines/gmx/` would match that name, `scorpion-gmx/` is used below as asked.

```
testdata/machines/scorpion-gmx/
  README.md
  rom/gmx644.rom                 # or data/rom/gmx644.rom next to gmx.rom, if it becomes a selectable ROM
  edgegrinder/EGGMX.TRD
  edgegrinder/reference-gameplay.png   # optional: the author's screenshot (Docs/Скрин.png), by-eye reference only
  osz/OS.TRD
  osz/sdcard/OSZ/os.$c
  osz/sdcard/OSZ/sys.osg
  osz/sdcard/OSZ/nc.apg
  osz/sdcard/OSZ/tetris.apg
  osz/sdcard/OSZ/autorun.txt     # ours: "nc.apg" only (no net.apg, see 1.2)
  osz/osz-sd.zip                 # optional: the web emulator's exact FAT32 image, for the raw-image path
```

| File | Size | SHA-256 | Origin |
|---|---:|---|---|
| `rom/gmx644.rom` | 524 288 | `b6796bf88049aac851006211f9ca1a78187ee3701f8b53a8763d427de20e31a5` | `data/rom/gmx644.rom` (Loader V2.00, monitor v6.44su 21.08.2025) |
| `edgegrinder/EGGMX.TRD` | 655 360 | `5d0151d711e576fe507dd7f5d39262a93de96fbba789c33afa50317b29668bad` | EdgeGrinder `6639aa1`, `EGGMX.TRD` (17 files, release 2021.09.09) |
| `edgegrinder/reference-gameplay.png` | 13 350 | `a5326c23a91310eccfe441116b0ab656a71b5ea68c2f3f18c595d0362782b81b` | EdgeGrinder `Docs/Скрин.png` (720x576, emulator capture with border) |
| `osz/OS.TRD` | 655 360 | `9f4319d2f88ea46fb5ea57b6d1e26084c79adfe6364dd7cea712b260d987e945` | OSZ `2e1c9fd`, `Release/OSZ/OS.TRD` (= `OSZ/OS.TRD` = `testdata/machines/gmx/osz/OS.TRD`): `boot`, `OS` (B), `OS` C (15 369 bytes, loaded at `#C000`) |
| `osz/sdcard/OSZ/os.$c` | 15 633 | `3b69420040d4d0337d955df8ef11a95574a9eed3614fdd05a9873412fc43f80c` | `Release/OSZ/os.$c` ("OS ver 2025.10.08") |
| `osz/sdcard/OSZ/sys.osg` | 1 047 | `b972bce7391b30f99c23c7fbb6ffd6ebef41055a32ce9e2ca3632c46ec6536e2` | `Release/OSZ/sys.osg` |
| `osz/sdcard/OSZ/nc.apg` | 9 638 | `84372368cdccda78f4dd7aa7dbb316f24121ac6495c97b0da790496bf36c2686` | `Release/OSZ/nc.apg` |
| `osz/sdcard/OSZ/tetris.apg` | 3 051 | `321039041be74311a4129454d02d9a8182de75f291a01ad2938e3b84e9f0fd70` | `Release/OSZ/tetris.apg` |
| `osz/sdcard/OSZ/autorun.txt` | ~8 | ours | `nc.apg` |
| `osz/osz-sd.zip` (optional) | 123 863 | `1e593722eb8afe4177f69d4f52e491cc2e71069a522a025328c0984374d30253` | `testdata/machines/gmx/osz/osz-sd.zip`: `osz.vhd` 67 109 376 bytes (`952efa35...8375`, fixed VHD = 64 MB raw + 512-byte `conectix` footer, MBR partition type `#0C` FAT32 at LBA 63, 131 009 sectors, 1 sector/cluster, `\OSZ\` = the Release folder with the same binaries), `OS.TRD` (same hash as above), `osz.json` `{"model":"gmx","a":"OS.TRD"}` |

The whole Release folder without `OS.TRD` is about 75 KB if the full set is wanted (`moonr.apg`, `radio.apg`,
`artview.apg`, `update.apg`, `terminal.apg`, `net.apg`, `gp_gzip.bin`, `data/*`); the four files above are enough
to reach NC and Tetris. Do not commit the 64 MB VHD or the 1 GB `wc.img` unpacked.

Precondition to verify: the kernel finds the first FAT partition as drive `E:` through the monitor's R8FAT and opens
`\OSZ` (`Drv/zsfat.asm:197-212`, `:1063`); the README says FAT32. If the monitor rejects FAT16, the host folder must be
presented as FAT32 (the synth volume supports it) or the test uses `osz-sd.zip`. **Settled (Q20):** the monitor
supports FAT32 only (LW/PLM changelog "поддержана только FAT32"); the folder is presented as FAT32.

### 5.1 What each fixture can test

| # | Fixture | Scenario | What it pins |
|---|---|---|---|
| 1 | `gmx644.rom` | Cold start to the loader V2.00 screen, then the Scorpion scheme and the monitor menu | ROM plane selection and the loader's `#7EFD` writes (Q1); a firmware that has R8FAT (needed by 6-8) |
| 2 | + `EGGMX.TRD` | Autoboot `boot` -> `EG` controls screen -> any key -> `DISC` loader: loading picture shown in extended mode | TR-DOS `#3D13` loading on GMX; `#7EFD=#C8`; `#7FFD=#11`; pixel pages `#39`/`#3B` filled with `#0F`, the picture in `#79` (screen digest of 640x200) |
| 3 | same | Map pre-render (~15 s emulated; turbo / fast-forward), fade-out alternating screens | 7-bit page formula over `#10-#67` (hash of those RAM pages after the loader); `#1FFD` bit 0 RAM at `#0000`; `#78FD` xor 2 |
| 4 | same | Title screen: zoom-scroll texts, colour rotation, AY music | IM 1 vector in RAM page 0; 50 Hz INT; AY via `#FFFD/#BFFD` (register writes per frame); screen pair `#39/#79` and `#3B/#7B` by `#7FFD` bit 3 (Q11): golden frames on two consecutive frames must both be complete |
| 5 | same | Fire (Space / Kempston fire) -> gameplay; press R | Frame-rate counter reads 25 on a correctly timed 7 MHz GMX (16 means too slow, 50 too fast); Kempston `#1F`, keyboard rows |
| 6 | `OS.TRD` + `osz/sdcard/` | Boot `OS.TRD` -> kernel loaded at `#C000`, `JP` into page 0 at `#0000` -> "OS ver 2025.10.08", "Found FAT:" `E:SD...`, "Found OS on disk:" -> NC panel (autorun) | RAM page 0 seen at `#0000` and `#C000` at once; RST `#08` into the service ROM via `OUT #1FFD,#12` at `#0013`/`#0033` (mapping changes before the next fetch); port `#0000` writes (do not reset the CPU, see 0.6); Z-Controller SD through the monitor; FAT32 |
| 7 | same | Fill the console past row 25 (e.g. an `autorun.txt` with extra lines naming missing files, each prints an error) | Hardware scroll `#7AFD/#7CFD`: byte offset in multiples of 640, wrap at 16000, pixels and attributes scroll together (Q4) |
| 8 | same | Keep NC running for several hundred frames, switch tasks (SS+Enter), Ext+1 | Read-backs `IN #7AFD` (slot-3 page) and `IN #78FD` every interrupt; a wrong value shows as a crash or garbage after a switch (R14) |
| 9 | same + `tetris.apg` | Start Tetris from NC (or `autorun.txt` = `tetris.apg`) | "Pseudo 160x200" on 640x200 (`OS_SET_SCREEN #39`), mode switch `#C8`/`#C0` |
| 10 | `osz-sd.zip` (optional) | The same as 6 from the raw FAT32 image | Raw image / VHD footer handling; parity with the web emulator (`?model=gmx&sd=sd/osz.zip&autorun=1`) |

Tests 6-10 depend on the GMX model's SD path (Z-Controller on the GMX in our config) and on the v6.44 firmware;
tests 2-5 need only the TR-DOS path, so they can come first.
