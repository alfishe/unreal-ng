# Quorum: reference consensus

**Date:** 2026-10-07 · part of [README.md](README.md)

Every fact below was read in the named source. **U** = UnrealSpeccy (`io.cpp`, `memory.cpp`, `config.cpp`, `vars.cpp`,
`wd93cmd.cpp`), **Z** = ZXMAK2 (`Quorum/*.cs`, `machines.config`), **K** = kozynax (a fork of ZXMAK2, Quorum commits 2019-2025, that
added the 128K and 1024K memory classes: not independent of Z, but it adds facts and a contradiction), **BC** = Black_Cat's port
table (a hardware-derived decode table, 2008, computer letter "C" = "Quorum 128/+"), **ROM** = what the code of
`qu7v42.rom` does (byte-pattern scans for `OUT (n),A` / `IN A,(n)` / `LD BC,nn`, not a disassembly, so a hit can be
a false positive; [roms.md](roms.md)). Links are in [README.md](README.md).

The rule: where U and Z (and BC) agree, that is the answer. Where they differ, the row says what we take and why, and the
open ones are in [TODO.md](TODO.md).

Port decodes are written `mask/value` over the 16-bit address (a port matches when `(address & mask) == value`).

## 1. Ports

| # | Fact | U | Z / K | BC | ROM | Verdict |
|:--|:--|:--|:--|:--|:--|:--|
| P1 | `#00` write (control port, "CMR1" in Z) | `(port & 0xFF) == 0x00` exact | `0x0099/0x0000` | `xxxxxxxx0xx0xxx0` = `0x0099/0x0000` | 31 `OUT (0),A` in the SYS page, 4 in the 48 page | **`0x0099/0x0000`** (Z and BC agree; U's exact decode is a subset) |
| P2 | `#7FFD` write (CMR0, 128-style paging) | `0x801A/0x0018` | `0x801A/0x0018` | `0xxxxxxxxxx11x0x` = `0x801A/0x0018` | `LD BC,7FFD` in all pages | **`0x801A/0x0018`** (all three) |
| P3 | `#80FD` write | `0xA01A/0x8018`, latches `p80FD`, nothing uses it (the screen-base use in `memory.cpp` is commented out) | not present | `1x0xxxxxxxx11x0x` = `0xA01A/0x8018`, named "CP/M paging" | no `LD BC,80FD` in any page | **latch only, no effect** (Q4) |
| P4 | `#FE` read and write | `(port & 0x99) == 0x98` | `0x99/0x98` (beeper, tape, border) | `1xx11xx0` = `0x99/0x98` | `OUT (FE)` 31x, `IN (FE)` 10x in SYS | **`0x99/0x98`** (all three) |
| P5 | `#7E` read (extra keyboard matrix) | `(port & 0xFF) == 0x7E` exact | `0x99/0x18` | `xxxxxxxx0xx11xx0` = `0x99/0x18` | `IN A,(7E)` twice in SYS (`#0130`, `#077B`) | **`0x99/0x18`** (Z and BC) |
| P6 | FDC registers | `(port & 0xFC) == 0x80`: `#80` command/status, `#81` track, `#82` sector, `#83` data (to WD93 `#1F/#3F/#5F/#7F`) | `0x9C/0x80`, register = `address & 3` | the FDC row for "C" is `0BAx11x1` (the `#1F/#3F/#5F/#7F` family, A7 = 0): **disagrees** | DOS page: about 60 `OUT/IN #80..#83` byte patterns; no `#1F/#3F/#5F/#7F` access to the FDC in the DOS page | **`#80..#83` canonical addresses, exact decode** (U + Z + ROM). Aliases (Z ignores A6, A5) and BC's row are Q5 |
| P7 | FDC system port | `(port & 0xFF) == 0x85`, write only | `0x9F/0x85`, read and write | `1xxx11x1` (the `#FF` family): disagrees | DOS and SYS pages `OUT #85` (5 + 13) | **`#85`, write; read returns the WD93 system latch as Z does** (Q5 for aliases) |
| P8 | `#85` value translation to the Beta128 `#FF` format | `new = ((v & ~3) ^ 0x10) \| drv`, `drv = {3,0,1,3}[v & 3]` | identical | - | - | **identical in U and Z**: `01` = drive A, `10` = drive B, `00` and `11` = drive 3 (D, none); bit 4 inverted; the other bits (reset, HLT, density) pass through |
| P9 | When the FDC ports answer | only inside `if (comp.flags & CF_DOSPORTS)`, which `set_banks` sets for Quorum when `#00` bit 7 = 0; the Quorum arm's own bit-7 test is commented out | always (`IsActive` returns true; the bit-7 test is a comment) | - | every ROM `OUT (0)` has bit 7 = 0 | **always on** (Z; with the ROM both are the same) (Q6) |
| P10 | `#85` bit 5 | `wd93cmd.cpp`: "in Quorum D5 controls the drive motor"; the core sets HLD and keeps the motor on for 2 s | passes it through | - | - | pass through; the existing WD1793 model handles the Beta128 `#FF` bit meaning, check D5 against it in phase 2 |
| P11 | AY | the common `#FFFD/#BFFD` decode | `AY8910` device, default decode | data write `101xxxxxxxx1xx0x` = `0xE012/0xA010`, register/read `111xxxxxxxx1xx0x` = `0xE012/0xE010` | `LD BC,FFFD/BFFD` in the 128 page | **standard `#FFFD / #BFFD` ports work under every reading**; BC's stricter A4 = 1, A13 = 1 decode is the faithful one (Q7: only one source) |
| P12 | Kempston joystick | the generic Kempston decode | `KempstonJoystick` device, default decode | `0xx11xx1` = `0x0099/0x0019` (joystick and printer status share it) | `IN A,(1F)` 3x in SYS, 1x in the 48 page | **`#1F` reads the joystick**; BC's decode is Q7 |
| P13 | Printer | none | none | `#1B` read, `#7B`/`#FB` write (`0xx11xx1`, `0xx110x1`, `1xx110x1`) | no `OUT (7B)` / `OUT (FB)` found | **not emulated**, out of scope (documented only) |
| P14 | Overlapping decodes | first match wins in U's if-chain | every subscriber fires | - | - | e.g. `OUT (#7FFC),A` matches `#7FFD`'s mask (A1 = 0) and `#FE`'s (A7 = 1, A4 = 1, A3 = 1, A0 = 0): Z fires both, U only the earlier. Q8 |

## 2. Control port `#00` bits

| Bit | Name (U `emul.h`) | U | Z | K | ROM use | Verdict |
|:--|:--|:--|:--|:--|:--|:--|
| 0 (`#01`) | `Q_F_RAM` | RAM page 0 (or 8) at `#0000-#3FFF` | same (`isNoRom`) | same | `LD A,1; OUT (0),A` before RAM tests and ROM patching (`#0707`, `#088A`, `#1417`, `#18A5`, 48 page `#39E6`) | **RAM over the ROM** (all) |
| 1 (`#02`) | - | unused | **Quorum 64 only**: `0 -> screen page 5, 1 -> page 7` | same | never set (values seen: `#00 #01 #20 #60`) | unused on 128/1024 (Z's 64K variant is out of scope, Q9) |
| 2 (`#04`) | - | unused | unused | **`Q_PENTAGON`**: Pentagon timing instead of Quorum timing (1024+ only) | - | **unverified, one source** (Q3) |
| 3 (`#08`) | `Q_RAM_8` | the `#0000` RAM page is 8, not 0 (masked by RAM size, so 0 on 128K) | same | same | - | **page 8** (all); on 128K it folds to page 0 |
| 4 (`#10`) | - | unused | unused | **`Q_BLK_128`**: ignore 7FFD bits 5-7, 128K mode (1024+ only) | - | **unverified, one source** (Q2) |
| 5 (`#20`) | `Q_B_ROM` | 1 = normal ROM (128 / 48 / DOS); 0 = SYS ROM | `SYSEN = (bit5 == 0)` | same | `#00` after reset; the SYS code writes `#20` to start the normal ROM (`#0336`, `#0821`, `#142F`...) | **0 = SYS (menu) ROM, 1 = normal ROM**; reset value 0, so the machine boots into the SYS page (all agree) |
| 6 (`#40`) | `Q_BLK_WR` | **not implemented** | `m_lock`: 7FFD writes ignored, and writes to `#0000-#3FFF` go to a trash page | same | the 48 page writes `#60` (= B_ROM + BLK_WR) and jumps to `#0000` (`#39F8`): the 48K-mode lock | **Z's meaning** (the ROM sets it exactly where a Spectrum 48 lock belongs) |
| 7 (`#80`) | `Q_TR_DOS` | only gates the FDC ports (`CF_DOSPORTS`) | selects what the TR-DOS trap shows at `#0000`: 1 = the DOS ROM, 0 = RAM (shadow); also "SYS only if bit 7 = 0" | same | never set by the ROM | Q1 |

## 3. Paging and memory

| # | Fact | U | Z / K | Verdict |
|:--|:--|:--|:--|:--|
| M1 | ROM page order in the 64K image | `sys = 0, dos = 1, 128 = 2, sos(48) = 3` | `GetRomIndex`: 128 = 2, SOS = 3, DOS = 1, SYS = 0 | **SYS, DOS, 128, 48** (all; same as `rom.cpp` today) |
| M2 | Normal ROM selection | `7FFD` bit 4: 1 = 48 page, 0 = 128 page | `IsRom48 = CMR0 & 0x10` | same |
| M3 | `#4000` and `#8000` | RAM 5, RAM 2 | same | same |
| M4 | `#C000` page, 128K | `7FFD & 7` | `CMR0 & 7` (K `Quorum128`) | `7FFD & 7` |
| M5 | `#C000` page, 256K | `(7FFD & 7) \| ((7FFD & 0xC0) >> 3)`, masked by RAM size: 7FFD bit 6 -> page bit 3, bit 7 -> bit 4 | `& 0x0F`: only bit 6 -> bit 3 | **bit 6 -> page bit 3** (U and Z agree on 256K) |
| M6 | `#C000` page, 1024K | `(7FFD & 7) \| ((7FFD & 0xC0) >> 3) \| (7FFD & 0x20)`: bits 6, 7, 5 -> page bits 3, 4, 5 | K: `(7FFD & 7) \| ((7FFD & 0xE0) >> 2)`, as the Pentagon 1024: bits 5, 6, 7 -> page bits 3, 4, 5 (**bit 6 -> bit 4**, contradicting M5) | **open** (Q2). U agrees with Z on the 256K bits, K breaks them; no ROM evidence |
| M7 | 7FFD bit 5 as the 48K lock | **yes**: a write is ignored while bit 5 of the stored value is set (U's `io.cpp`: the Pentagon-1024 / Profi exceptions do not apply to Quorum) | **no** for Quorum (no lock; only `BLK_WR`) | **open** (Q2). The ROM's own 48K lock is `#00` = `#60`, which suggests bit 5 is not the lock |
| M8 | Writes while the ROM is mapped at `#0000` | ignored (trash page) | go to the RAM page under the ROM, unless `BLK_WR` | **Z** (the flag makes sense only if writes pass through): Q10 |
| M9 | Screen | `7FFD` bit 3: page 5 or 7 (the `p80FD` base is commented out) | same (`videoPage = CMR0 & 8 ? 7 : 5`) | **page 5 / 7 by 7FFD bit 3** |
| M10 | TR-DOS trap | the generic `CF_TRDOS` trap: M1 fetch at `#3Dxx` with the 48 page mapped; leaves when PC >= `#4000` | `DOSEN` set on M1 `#3Dxx` when `IsRom48`, cleared on any M1 fetch at `>= #4000` | **the standard trap** (same). What it shows is Q1 |
| M11 | While the trap is on | the DOS ROM page replaces `#0000-#3FFF` (when `Q_B_ROM` = 1) | DOS ROM page if `#00` bit 7 = 1, **RAM page 0/8 if bit 7 = 0** | **open** (Q1) |
| M12 | NMI | none for Quorum | `NmiAck`: `#00` := 0 (so the SYS ROM, whose `#0066` is the NMI handler, runs) | **Z** (ROM: SYS page `#0066` = `PUSH AF/BC/DE/HL; JP #00A7`) |
| M13 | Reset | `p00 = p80FD = 0`, `7FFD = 0` | `DOSEN = false; CMR0 = CMR1 = 0` | **all latches 0** |

## 4. Video and timing

| # | Fact | U | Z / K | Verdict |
|:--|:--|:--|:--|:--|
| V1 | Frame | no Quorum value: the shipped `x32/quorum.ini` has the generic preset `Frame=71680, Paper=17989, Line=224, IntLen=32` (the same as every other U ini) | `UlaQuorum`: line 224 T, **frame 69888 T (312 lines)**, first paper line 80, first paper T 65 (comments "proof???"), INT length 32 T, INT at 0; borders 32 top / 32 bottom lines | **Z's figures are the only Quorum-specific ones**; they are unproven (Q3) |
| V2 | Pentagon timing bit | - | K: `#00` bit 2 selects `UlaPentagon` timings (1024+ only) | one source (Q3) |
| V3 | Contention | none | `UlaDeviceBase` has none, `UlaQuorum` adds none | **none** |
| V4 | Floating bus | - | `UlaDeviceBase.ReadPortFF` reads the screen byte (generic) | unverified for the board; **`#FF` (nothing) first** |
| V5 | Border / paper colours, 15-colour attribute | standard Spectrum | standard | standard |

## 5. Devices

| Device | U | Z | Verdict |
|:--|:--|:--|:--|
| Sound | AY / YM (one chip, `Chip=YM` in `quorum.ini`), beeper | one `AY8910`, beeper (`0x99/0xFE`) | **one AY-3-8912 and the beeper**; the YM vs AY choice is the user's, as on other models |
| Tape | `#FE` bit 6 EAR | `TapeDevice` on `0x99/0xFE` | standard, at the Quorum's `#FE` decode |
| Joystick / mouse | Kempston joystick; `quorum.ini` has `Mouse=KEMPSTON` | `KempstonJoystick` only | joystick yes; mouse is U's generic default, not a board fact (no source says the Quorum has one): off |
| Disk | WD1793 + 4 drives | `FddControllerQuorum` + `noDelay` | built-in WD1793, drives A and B only (the `#85` decode has two valid selects) |
| IDE, RTC, DMA | none | none | none |
| Keyboard | `#FE` rows + `#7E` rows ([research-quorum-keyboard.md](research-quorum-keyboard.md)); `read_quorum` | same two matrices | two matrices, identical bit assignment in U and Z |
