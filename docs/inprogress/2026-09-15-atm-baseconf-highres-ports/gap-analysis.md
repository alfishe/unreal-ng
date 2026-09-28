# ZX-Evo BaseConf (`ATM3`) — gap analysis against hardware and other emulators

| | |
|---|---|
| **Date** | 2026-09-27 |
| **Status** | Analysis complete. **E0 done 2026-09-28** ([e0-decoder-fixes.md](e0-decoder-fixes.md)): P-1…P-4, P-6, P-7, P-9, P-10 closed; P-5 moved to E8. **E1 done 2026-09-28** ([e1-fpga-variant-and-rom.md](e1-fpga-variant-and-rom.md)): C-1, C-9, R-1 closed, C-2 plumbing in place. Plan: [implementation-plan.md](implementation-plan.md) |
| **Machine** | `MM_ATM3`, short name `ATM3` (ZX Evolution / PentEvo running the NedoPC **Base Configuration**) |
| **Baseline** | `master` @ `95fce44d` |
| **Evidence** | [baseconf-hardware-reference.md](baseconf-hardware-reference.md) (FPGA, AVR firmware, ROM; what the board does) · [emulator-feature-matrix.md](emulator-feature-matrix.md) (what 9 other emulators do) · [unreal-ng-atm3-audit.md](unreal-ng-atm3-audit.md) (what we do, with file:line) |
| **Designs** | [tdd-evo-control-and-avr.md](tdd-evo-control-and-avr.md) · [tdd-virtual-trdos.md](tdd-virtual-trdos.md) · [tdd-storage-sd-ide-cd.md](tdd-storage-sd-ide-cd.md) |

## 0. Summary

1. **Our ATM3 targets the wrong BaseConf.** The FPGA source has two BaseConf trees. The one our
   2026-09-15 port was checked against (`fpga/baseconf/trunk`) has been frozen since 2020. The released
   BaseConf is `fpga/base_trdemu/trunk` (firmware "ZXEvo 4M", 2026-01-07). It moved the register
   readback from `#xxBE` to `#xxBD`, made `#xxBE` a write-only "exit" strobe, and added the hardware
   hook for virtual TR-DOS drives. The current Evo Reset Service (ERS) ROM and NedoOS read `#xxBD`.
   Without `#13BD` the ERS shows **"Incorrect FPGA zxevo_fw.bin"** and disables its RAM-disk and
   image-mount features.
2. **Our ROM image matches no official build.** `data/rom/zxevo.rom` differs from both prebuilt
   images (`zxevo_fe.rom` for the current FPGA, `zxevo.rom` for the legacy one) in 20 of 32 pages. It
   holds a custom 2018 TR-DOS in pages 0-3 and an older ERS.
3. **The four gaps already known are confirmed, and each needs more than one piece.** The table below
   lists the parts each one needs.
4. **Storage is missing entirely.** There is no SD card, no IDE and no virtual TR-DOS. Worse, our
   `#FE` port decodes on A0 alone, so every even port (all NemoIDE ports) currently drives the
   border and beeper.
5. **The AVR side (clock, NVRAM, keyboard, versions) is a plain 256-byte array.** It returns what was
   written, which is exactly the signal the ERS uses to decide "no Evo AVR". It also has no
   PS/2 scancode buffer, and the NedoOS Evo kernel reads its keyboard from that buffer.
6. **Several ports every Evo program uses are missing or dead:** Kempston mouse and joystick, Covox
   `#FB` (configured but never reached), NMI and the Magic button, breakpoints, flash writes,
   per-window write protect, and the `#F6`/`#FC` border ports.
7. **Nothing here is new invention.** Every missing piece either exists in a shared design already
   (IDE core, SD card model, virtual FAT, the M1 hook from TSConf phase 0) or is small and local to
   the ATM3 decoder. The plan reuses those designs and adds three BaseConf-specific pieces: the
   trdemu trap, an `EvoAvr` device and a Z-Controller SPI glue shared with TSConf.

### 0.1 The four known gaps, broken into parts

| Known gap | What the user sees on a real Evo | Parts needed (gap IDs, §3) |
|---|---|---|
| **RAM disk; mount TR-DOS images from file / SD / HDD** | ERS "Format ramdisk 640k", "TRD to: Ramdisk A..D", "Mount A:-D:" from a FAT file system on SD or HDD, automount via `IMAGE.MNT`. Then plain TR-DOS 5.03 uses the drive as if it were real. | **ST-5** trdemu trap (hardware half; the ERS does the rest in Z80 code) · **C-1** `#xxBD` (the ERS checks `#13BD`) · **R-1** current ROM image `zxevo_fe.rom` · **ST-1** SD card or **ST-2** IDE as the image source · **A-4** CMOS settings (virtual drive number, automount) · **ST-6** FDC gating |
| **IDE HDD emulation (disk image, host folder)** | ERS "HDD boot" (LBA 2 → `#6000`), NedoOS on HDD, mount TRD from a FAT partition | **ST-2** NemoIDE adapter on the shared IDE core · **P-1** full `#FE` decode (otherwise the IDE ports are the border) · **ST-3** image formats and host folder (shared IDE rollout 1) |
| **CD ATAPI emulation** | ERS "CD boot": ATAPI drive on the slave, ISO 9660, `AUTORUN.ZX` | **ST-4** shared ATAPI device on the NemoIDE slave |
| **BaseConf and AVR boot indication in the ERS** | ERS header "Baseconf: ZXEvo 4M 07.01.2026" and "AVR Boot: ZXEvoAVRBoot …"; "NONE" today | **A-1** AVR version window in Gluk cells `F0-FF` (must *not* echo writes) · **C-1** `#13BD` read-back (FPGA suitability check) |

## 1. Glossary

| Term | Meaning |
|---|---|
| **BaseConf** | The NedoPC FPGA configuration of the ZX Evolution board: an ATM Turbo 2+ / Pentagon-1024 compatible machine. Our `ATM3` model. (The other configuration, TS-Conf, is PLAN #41.) |
| **Legacy tree / current tree** | `pentevo/fpga/baseconf/trunk` (frozen 2020, readback on `#xxBE`) vs `pentevo/fpga/base_trdemu/trunk` (released, readback on `#xxBD`, trdemu). Abbreviated **BC** and **TD** below. |
| **ERS** | Evo Reset Service: the boot menu in ROM page 31. It programs the memory pager, shows versions, mounts disk images, boots from FDD/HDD/SD/CD and flashes the ROM. |
| **AVR** | The ATmega128 microcontroller on the board. It loads the FPGA, reads the PS/2 keyboard and mouse, and emulates the MC146818 clock ("Gluk" clock) with an I²C RTC chip and its battery RAM. |
| **Gluk ports** | `#DFF7` (address) and `#BFF7` (data) of the clock/NVRAM, enabled by `#EFF7` bit 7; `#DEF7`/`#BEF7` while shadow is on. Cells `F0-FF` are an AVR "extension window" (versions, PS/2 buffer, modes). |
| **Shadow** | "DOS ports on": TR-DOS is active or `#xxBF` bit 0 is set. The FDC, the ATM pager and some other ports answer only in shadow. |
| **trdemu** | The current tree's hook for virtual floppy drives. When TR-DOS touches the FDC for a drive marked in `#13BD`, the FPGA swaps RAM page `#FE` into `#0000-#3FFF` so that ERS code there emulates the controller, then `OUT (#BE)` swaps back. |
| **Z-Controller** | The SD card interface convention: `#xx77` chip select, `#xx57` SPI data. Shared by BaseConf, TS-Conf and several clones. |
| **NemoIDE** | The IDE interface on ports `#10…#F0`, `#11`, `#C8`. The Evo version adds "nemo-divide" (two reads of `#10` move one 16-bit word, so `INIR` works). |

## 2. How the verdicts were reached

- **Hardware wins.** The current FPGA tree, AVR firmware and ROM sources decide what the board does
  (hardware reference §A-§C). Where the 2020 manual and the RTL disagree, the RTL wins
  (hardware reference §D, last table).
- **Emulators show what software needs, not what is correct.** No reference emulator matches the
  current tree fully. The three that answer on `#xxBD` (nedopc Unreal patch, Unreal_NS,
  xpeccy-plus) are the parity bar for the new features. zx-evo-unreal and MAME still use `#xxBE`
  (feature matrix, headline table).
- **Severity:** **H** = a mainstream Evo program or the ERS fails or shows wrong results; **M** = a
  documented feature is missing but common software copes; **L** = rare software or cosmetic.
- **Size:** S < 1 day, M = days, L = a week or more (same scale as PLAN.md).

## 3. Gap list

"Refs" summarizes the reference emulators (details in the feature matrix): **U** zx-evo-unreal,
**N** nedopc / Unreal_NS, **X+** xpeccy-plus, **X** Xpeccy, **Z** ZXMAK2 / kozynax, **M** MAME.

### 3.1 Platform correctness (P) — wrong behavior in shipped code

| ID | Gap | Hardware (evidence) | unreal-ng today | Refs | Sev | Size |
|---|---|---|---|---|---|---|
| **P-1** | `#FE` decoded on A0 only | `#FE`, `#F6`, `#FC` are full low-byte decodes; `#F6` sets border 8-15 **without** beeper/tape; `#FC` writes border and, with A15=0, also `#7FFD` (hw ref §A.2) | every even port is border/beeper, so NemoIDE `#10…#F0`, `#C8` and `#08…#E8` are swallowed (audit §1.1) | all decode the IDE ports | H (blocks ST-2) | S |
| **P-2** | FDC answers outside shadow; no Kempston joystick | VG93 `#1F/#3F/#5F/#7F/#FF` only in shadow; outside shadow `#1F` is an 8-bit Kempston joystick (hw ref §A.2) | FDC always; `#1F` never reaches a joystick (audit §1.1) | U, N, X+, M gate the FDC | M | S |
| **P-3** | Kempston mouse not decoded | `#FADF` buttons + 4-bit wheel, `#FBDF` X, `#FFDF` Y, always (hw ref §A.2) | ATM decoders never call the mouse; `Mouse=KEMPSTON` in the ini creates an unreachable device | all (Z has no wheel) | H for NedoOS/WC | S |
| **P-4** | Covox `#FB` dead | `#xxFB` 8-bit DAC on the beeper output, always (hw ref §A.12) | device created from `CovoxFB=1`/`SD=1` but ATM decoders never dispatch self-decoding devices | U, N, X+ | M | S |
| **P-5** | `#xBF7` write protect missing | `#3BF7…#FBF7` D0 = read-only bit per window, also blocks flash writes (hw ref §A.4) | not decoded | N, X+ | L | S |
| **P-6** | `#EFF7` bits and write rule | bit 3 = RAM page 0 at `#0000` (beats the pager); bit 2 = 0 means Pentagon-1024 mode where `#7FFD` bits 7-5 extend the page; `#EFF7` is **not writable in shadow**; bit 7 opens the Gluk ports (hw ref §A.4) | bit 3 ignored, `#7FFD` 7-5 ignored (the header claims them), written in shadow, Gluk not gated (audit §1.1, §2) | N, X+ (M maps ROM 0 for bit 3: bug) | M | S |
| **P-7** | Reset clock | CPU leaves reset at **7 MHz** (`#EFF7`.4 = 0, `#xx77`.3 = 0) (hw ref §A.11) | `hw_turbo_shift = 0` (3.5 MHz) until the first port write | M right; U 3.5 | L | S |
| **P-8** | NMI → RAM page `#FF` is dead code | window 0 = RAM `#FF` while in NMI (hw ref §A.7) | reads `EmulatorState::nmi_in_progress`, which nothing sets at runtime (audit §1.1) | U, N, X+ | (see C-3) | — |
| **P-9** | TTD misses ATM state | — | `atmPalette`, `atmPaletteRegs`, `atmBorderBright` not captured; the CMOS address latch saved is the wrong field (`EmulatorState::cmos_addr` instead of `CMOS::_cmos_addr`); a header comment claims `#BF`.0 gates the FDC (audit §8) | none have TTD | M | S |
| **P-10** | Introspection | — | memory API and CLI report 4 ROM pages for a 32-page image; port trace names the model "Unknown"; no ATM rows in the port map (audit §1.2, §2; PLAN #8) | — | L | S |
| **P-11** | 2026-09-15 docs verified against the legacy tree | current tree: 4:4:4 palette via `#BF`.5 + A15..A8, readbacks on `#xxBD`, `#FF` read reconstructed (hw ref §A.14) | [verification-gaps-and-tests.md](verification-gaps-and-tests.md) cites `fpga/baseconf/trunk` as "ground truth" | — | L (docs) | S |
| **P-12** | Floating bus on ATM3 | the FPGA drives `#FF` for every unclaimed IN (`zbus.v:50`); the Evo has no floating bus | the Z80 applies the ULA floating-bus value to undecoded ports; harmless while the shipped config keeps `FloatBus=0` | — | L | S |

### 3.2 Evo control ports (C) — `#xxBD`, `#xxBE`, `#xxBF`

| ID | Gap | Hardware | unreal-ng today | Refs | Sev | Size |
|---|---|---|---|---|---|---|
| **C-1** | Readback port and index set | TD: `#00BD…#13BD` (A12..A8 index): pages (inverted), RAM/ROM bits, dos7ffd bits, `#7FFD`, `#EFF7`, `#xx77` state, palette, font byte, border, breakpoint lo/hi (R/W), write-protect bits, **FDD mask `#13BD` (R/W)**. BC: same table on `#xxBE` (read), breakpoint write on `#00BD/#01BD` (hw ref §A.5, §A.14) | legacy `#xxBE` indices `00-0F`; `0E` returns `#FF`; `10-13` and `#xxBD` missing; page readbacks **not inverted** (TD returns inverted pages; our `00-07` return plain page numbers) | N, X+ on `#BD`; U, M on `#BE` | **H** (ERS, NedoOS `IN (#04BD)`) | S |
| **C-2** | `#xxBE` write = exit strobe | ends NMI (window 0 back after 2 more M1) and ends trdemu immediately (hw ref §A.7, §A.8) | latched into `pBE`, never consumed | U, N, X+ | H (with C-3, ST-5) | S |
| **C-3** | NMI | sources: `#BF`.3 1→0 edge, Magic key (PrintScreen), breakpoint. `#BF`/key NMIs wait for the next INT start; the FPGA feeds `NOP` for the `#0066` fetch and then maps RAM `#FF` into window 0 (hw ref §A.7) | generic Z80 NMI only; no page switch, no INT alignment, no `NOP` feed | U, N, X+ | H (ERS Magic service, STS debugger, tape emulation) | M |
| **C-4** | Breakpoint | `#10BD/#11BD` address, `#BF`.4 enable → immediate NMI on M1 at that address; stays armed (hw ref §A.7) | none (`pBD` only cleared) | N, X+ (M stores, never checks) | M (ERS tape emulation, debuggers) | S |
| **C-5** | Flash ROM writes | `#BF`.1 + a window not write-protected → memory writes reach the 29F040 flash, programmed with JEDEC command sequences; ERS "Fast update ROM", "Update custom ROM" (hw ref §A.4, §C 1.4) | ROM read-only | **none** | M | M |
| **C-6** | Font RAM | `#BF`.2: every memory write also writes font RAM at `A & 2047`; `#0EBD` reads the displayed font byte (hw ref §A.6) | fixed built-in font (PLAN #53 item 2) | none load fonts (U fixed) | L-M (ERS "Reload font") | S |
| **C-7** | 4:4:4 palette | `#BF`.5: `#FF` palette write takes the low bits of each channel from A15..A8 (TD only) (hw ref §A.6) | 2-bit-per-channel only | N, X+, M | M (NedoOS sets `#BF`=32) | S |
| **C-8** | ULA+ | `#BF3B` register select, `#FF3B` data (hw ref §A.2) | none | N, X+ | L | S |
| **C-9** | FPGA variant switch | BC and TD differ in C-1, C-2, ST-5 and `#2F/#4F/#6F/#8F` (BC RAM-disk latches) | — | U is BC-only, X+ is TD-only | M | S |

### 3.3 AVR: clock, NVRAM, keyboard, versions (A)

| ID | Gap | Hardware | unreal-ng today | Refs | Sev | Size |
|---|---|---|---|---|---|---|
| **A-1** | Version window (ERS indication) | write 0/1 to any cell `F0-FF`, read 16 bytes: 12-byte name, LE date word (bit 15 = release), CRC. Type 0 = main firmware ("ZXEvo 4M"), type 1 = bootloader ("ZXEvoAVRBoot"). A read that equals the written type means "not an Evo AVR" to the ERS (hw ref §B 1.5, 1.7) | plain RAM cells: the echo makes the ERS print "NONE" | U "UnrealSpeccy", X+ "Xpeccy+", Z "BaseConf Emu", M "MAME" | **H** (user request) | S |
| **A-2** | PS/2 scancode buffer | type 2: each read pops one raw set-2 byte (16-byte FIFO, 0 = empty, `#FF` = overflow; reg C bit 0 write clears) (hw ref §B 1.6) | none; the host keyboard feeds only the `#FE` matrix | U, X+, M (Z always 0) | **H** (NedoOS Evo kernel keyboard, ERS PC-key test) | M |
| **A-3** | Registers A-D and modes | A = EEPROM page; B keeps only DM (bit 2); C: bit 7 EEPROM mode, bit 4 UF (clear on read), bit 3 SD present, bit 2 SD write-protect, bit 1 Caps LED, bit 0 tape-out mode; D = `#80` + live Ctrl/Alt/Shift/F12; type 3 = `modes_register` (VGA, tape-out, Caps, raster) (hw ref §B 1.3, 1.5) | host-time clock only; A = `#20`, D = `#80`; no SD bits, no modifiers, no modes | partial everywhere | M | S |
| **A-4** | NVRAM persistence and power-on content | cells `0E-EF` live in the battery RAM (persistent). The ERS keeps its settings there (`#E8-#EF`: reset target, boot device, virtual drive, automount, CRC) (hw ref §B 1.4, §C 2.2) | `_cmos[256]` never initialized and never saved; `CMOS=` ini key ignored | U, X+ save to a file | **H** (every ERS setting is lost; garbage at power-on) | S |
| **A-5** | EEPROM window | reg C bit 7 = 1: `F0-FF` = AVR EEPROM page A (4 KiB; holds the user PS/2 keymap) (hw ref §B 1.5) | none | none | L | S |
| **A-6** | RS-232 | `#F8EF…#FFEF` 16550 subset served by the AVR (hw ref §B 3.8) | none (`#FF`) | U (zf232) | L (NedoOS ESP build) | M |
| **A-7** | Raster selection | the AVR picks Pentagon 71680 / 60 Hz / 48K 69888 / 128K 70908; INT position per raster; contention only in 48K/128K rasters at 3.5 MHz (hw ref §A.7, §A.11) | fixed 69888 T, 48K-style INT, no contention | U, M: one raster | M (Pentagon-timed demos) | M |

### 3.4 Storage (ST)

| ID | Gap | Hardware | unreal-ng today | Refs | Sev | Size |
|---|---|---|---|---|---|---|
| **ST-1** | Z-Controller SD card | `#xx77` write D1 = /CS, read = `#00`; `#xx57` data with a one-byte read pipeline; in shadow `#xx57` with A15=1 is the CS port (NedoOS uses `#8057`) (hw ref §A.13) | `#57` returns `#FF`, writes swallowed; `#77` CS not decoded; `[ZC] SDCARD` not parsed | U, N, Z SDSC; X+, M SDHC; X+ host folder | **H** (ERS SD boot, mounting from SD, NedoOS, Wild Commander) | M (reuse) |
| **ST-2** | NemoIDE | `#10` low / `#11` high latch (Nemo order), nemo-divide pairing on `#10`, `#30…#F0` task file, `#C8` control (CS1), `#08…#E8` aliases to CS0; answers in and out of shadow; IDE reset = system reset (hw ref §A.9) | nothing (skeleton `io/hdd`), and P-1 swallows the ports | U, N, X+, M (Z master only) | **H** | M (reuse) |
| **ST-3** | HDD media | raw images, HDF/HDI/VHD, host folder as FAT (unified IDE design §7) | none | U raw, X+ HDI, M CHD | M | shared |
| **ST-4** | ATAPI CD-ROM | ERS CD boot: ATAPI on the slave, `#08` reset, signature `#EB14`, READ TOC, ISO 9660, `AUTORUN.ZX` → `#6000` (hw ref §C 4) | none | U, M | M (user request) | shared |
| **ST-5** | Virtual TR-DOS (trdemu) | `#13BD` drive mask; FDC access by TR-DOS ROM for a masked drive swaps RAM `#FE` into window 0 for the next fetch, suppresses the real VG93, blocks writes to `#FE` until the next M1; `OUT (#BE)` ends it immediately; `#FF` read reconstructed (hw ref §A.8, §C 3.1) | none | X+ full; N mask only; M broken compare | **H** (user request: RAM disk + mount) | M |
| **ST-6** | Legacy RAM-disk latches | BC only: shadow R/W bytes `#2F/#4F/#6F/#8F` used by the patched EVO-DOS of the legacy ROM (hw ref §A.8 end) | none | U | L (legacy ROM only) | S |

### 3.5 ROM, config, surfaces (R, T)

| ID | Gap | Detail | Sev | Size |
|---|---|---|---|---|
| **R-1** | ROM image | Ship/point at the official `zxevo_fe.rom` (TD) and keep the legacy `zxevo.rom` (BC) as an option; our current image matches neither (feature matrix row K1). TS-Conf needs TS-BIOS in pages 0-3, which `zxevo_fe.rom` leaves empty, so **ATM3 and TSL must stop sharing one ROM file** | H | S |
| **R-2** | Config parsing | `[ZC] SDCARD/SDDelay`, `[HDD]` (shared IDE design §8.1), `[MISC] CMOS`, the new `[EVO]` keys (FPGA variant, NVRAM file, raster, AVR version strings) | M | S |
| **T-1** | TTD for new state | trdemu/NMI flags, `#BD` registers, `#BF` bits, font RAM (2 KB), AVR state (PS/2 FIFO, ext type, EEPROM page), flash command state; storage follows the interim rule of the IDE design §10.0 (first SD/IDE command invalidates a recording) | M | M |
| **T-2** | Automation | SD/HDD/CD media verbs shared with the IDE design §8.2 and TSConf API-1; an `evo` inspection surface (FPGA variant, trdemu mask/state, NMI, AVR ext type, NVRAM dump/load); `.recipe/machines/atm.md` update; MCP resource `unreal://machine/zx-evo` (PLAN #14) | M | M |
| **T-3** | Acceptance software | Real-ROM tests: ERS header versions, ERS RAM-disk format + TR-DOS `CAT`, mount TRD from SD/HDD, HDD/SD/CD boot, NedoOS boot from an SD image and from a host folder | M | M |

### 3.6 Not gaps (checked and deliberately left alone)

| Item | Why |
|---|---|
| DMA, TurboSound, SAA1099, SounDrive, on-board GS | Not in BaseConf (hw ref §A.2 last paragraph). GS/NeoGS and MoonSound are ZX-Bus cards and already work. Our default AY pair (TurboSound) is a harmless superset; NedoOS writes the TS select anyway. |
| ZiFi `#xxEF` | Not in BaseConf; `#xxEF` is the RS-232 (A-6). |
| AVR/FPGA firmware update from SD (bootloader) | Happens in the AVR before the Z80 runs; nothing a Z80 program can observe except the version window (A-1). |
| `newirq` enhanced interrupts (`#14BD…#16BD`) | Unreleased branch that does not compile (hw ref §0). |
| 14 MHz wait states, the hidden 2-entry cache | Timing realism only; listed as optional phase E9 in the plan. |
| Emulating the WD1793 on the host for virtual drives | The real board does it in Z80 code (ERS page `#FE`); the emulator provides only the trap (design decision in [tdd-virtual-trdos.md](tdd-virtual-trdos.md) §2). Our existing host-side floppy images for real drives stay as they are. |

## 4. Open questions

Answered from sources where possible; the remainder have a default so nothing blocks.

| # | Question | Default until answered |
|---|---|---|
| Q1 | Power-on AVR raster (`modes_register` bits 5:4) on a board with an empty NVRAM | 48K raster (keeps today's 69888 T; the setting is saved per NVRAM file) |
| Q2 | Should we ship the official `zxevo_fe.rom` in `data/rom/` (it contains Sinclair ROMs, ProfROM, GLUK) the same way as today's image? | Yes, same treatment as the current `zxevo.rom` (the license question is settled by the repo's standing rule) |
| Q3 | CRC byte order in the version record (manual: little-endian; AVR source: big-endian) | big-endian (source wins); the ERS does not display it |
| Q4 | Which NedoOS build to use as the acceptance image (`sd_boot.$C`, `atm=1`) | build from the local NedoOS tree; the test skips if absent |
