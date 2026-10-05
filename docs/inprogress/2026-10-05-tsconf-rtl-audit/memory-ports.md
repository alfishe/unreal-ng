# TS-Conf audit: memory mapping, port decoding, system registers, reset

Scope: the CPU memory windows, `MEM_CONFIG` / `#7FFD` paging, the FM window, the CPU cache, the
`#xxAF` register file as far as decode, read-back and reset go, and every mainboard port decode
(`#FE`, `#FB`, AY, Beta-128 / vdos, Gluk CMOS / `#EFF7`, Kempston, Z-Controller SD, Nemo IDE,
`#xxEF` AVR ports). DMA, video modes, TSU and interrupts belong to other audits; their registers
appear here only for decode, read-back and reset values.

Each row is checked against both references:

- **[V]** RTL, repository `zx-evo`, paths under `pentevo/fpga/current/` unless stated
  (`z80/zports.v`, `z80/zmem.v`, `z80/zmaps.v`, `video/video_ports.v`, `top.v`, `quartus/tune.v`).
  `tune.v` defines `IDE_HDD` and `KEMPSTON_8BIT` and leaves `COPPER`, `FDR`, `IDE_VDAC*`,
  `ESP32_SPI` and `SPI_MODE_EN` undefined. The rows below describe that standard build.
  `pentevo/docs/TSconf/tsconf_en.md` and `pentevo/avr/current/` are cited where they add facts.
- **[U]** TS-Labs Unreal Speccy, repository `zx-evo-unreal`, paths under `Unreal/`.
- **[N]** unreal-ng at master `cf3adb714`, paths from the repository root.
  `PD` = `core/src/emulator/ports/models/portdecoder_tsconf.cpp`,
  `TM` = `core/src/emulator/memory/tsconf/tsconfmemory.cpp`,
  `TS` = `core/src/emulator/platforms/tsconf/tsconfstate.h`.
- Tests: `PDT` = `core/tests/emulator/machines/tsconf/portdecoder_tsconf_test.cpp`,
  `MT` = `tsconfmemory_test.cpp`, `ST` = `tsconfstorage_test.cpp`, `SNT` = `tsconfsound_test.cpp`
  (same folder), `ZT` = `core/tests/emulator/io/network/zifi_test.cpp`.

Verdicts: `match`; `ng-matches-RTL, Unreal differs`; `ng-matches-Unreal, RTL differs` (an unreal-ng
bug); `ng differs from both` (an unreal-ng bug); `unclear`.

## Results

| # | Behavior | RTL | TS-Labs Unreal | unreal-ng | Test | Verdict |
|:--|:--|:--|:--|:--|:--|:--|
| 1 | `#xxAF` decode: low byte `== #AF`, register = A[15:8], all 8 high bits significant (no mirrors) | `zports.v:252,317-321,415-417,494` | `io.cpp:182-183` (out), `:1012-1015` (in) | `PD:329-330,512,593` | PDT `REG1` (`#80AF`, `#FFAF` read `#FF`) | match |
| 2 | Readable registers: only `#00` STATUS, `#12` PAGE2, `#13` PAGE3, `#27` DMA_STATUS. Everything else, including PAGE0/1, MEM_CONFIG, SYS_CONFIG, FMAPS, CACHE_CONFIG, reads `#FF` (`default: dout = 8'hFF`) | `zports.v:415-445`; doc `tsconf_en.md:107-112` (Page0/1 `W`, Page2/3 `R/W`) | `io.cpp:1017-1031`; other `#xxAF` fall through to `return 0xFF` (`:1433`) | `PD:650-671` | PDT `REG1` | match |
| 3 | STATUS = `{copper_ready, PWR_UP, FDR_VER, 2'b0, VDAC_VER}`. In this build bit 7 = 0, bit 5 = 0, VDAC_VER = 0 (IDE), 3 (`IDE_VDAC`), 7 (`IDE_VDAC2`), 6 (VDAC2 + `ESP32_SPI`) | `zports.v:230-248,418-423` | `pwr_up \| (vdac2 ? 7 : vdac)` with `vdac` 0..3 (`io.cpp:1017-1022`, `tsconf.h:143-150`). Its 3-bit and 4-bit settings report 1 and 2, which no RTL build in this tree produces | `PD:654-660`; `VdacVersion() = ts_vdac & 7` (`portdecoder_tsconf.h:71`) | PDT `REG2` (compares against `VdacVersion()` itself, so it does not pin the value) | match |
| 4 | PWR_UP: 1 from FPGA configuration, cleared after the first STATUS read, not set by a Z80 reset | `zports.v:371-376,393-399` (`reg pwr_up_reg = 1'b1`, cleared on `xstat_post_read`) | set in `applyconfig()` (`config.cpp:1045-1048`), cleared by the read (`io.cpp:1020`) | `PD:153` (PowerOn), `:657-658` | PDT `REG2` | match |
| 5 | DMA_STATUS (`#27AF` read) = `{dma_act, 7'b0}` | `zports.v:425-426` | `io.cpp:1030-1031` | `PD:664-667` | PDT `REG1` (idle only) | match |
| 6 | ZiFi "Please update TS Conf.": the ZiFi 0.733 client (`ZX-Spectrum-Projects/ZiFi/zifi.asm:131` `'0.733'`) writes `#F1` (SETAPI 1) and `#FF` (GETVER) to `#C7EF`, reads `#C7EF`, and jumps to `nozifi` on `#FF` (`:4507-4515`, message `:4566-4571`). It never reads `#00AF`. It accepts any ER other than `#FF`; the TS AVR answers `ZF_VER = 1` | FPGA only relays `#xxEF` to the AVR as a wait port (`zports.v:288,334,478-481,735-764`); AVR `avr/current/rs232.c:209-225`, `rs232.h:154` (`ZF_VER 0x01`) | answers only while a host COM port is open: `if ((p1 == 0xEF) && (zf232.open_port))` (`io.cpp:952-953,1424-1425`), GETVER = `ZF_LAYERS` = 1 (`zf232.cpp:581-613`, `zf232.h:18`). With no ZiFi port configured, `#C7EF` reads `#FF` and the client prints the message | the AVR's ZiFi block is always fitted on TS-Conf (`PD:1138-1163`). GETVER sets ER = `kVersion` = 1 (`core/src/emulator/io/network/zifi.cpp:150-164`, `zifi.h:40`) | ZT `TheAvrIsThereWithoutABoard`, `SetApiAndGetVer` | ng-matches-RTL, Unreal differs |
| 7 | PAGE0..3 written by `#10AF..#13AF` (`hoa[7:2] == RAMPAGE[7:2]`) | `zports.v:587-588` | `io.cpp:1490-1508` | `PD:698-703` | PDT `FM3`, `P7F5` | match |
| 8 | Windows 1..3: RAM only, 8-bit page (4 MB) | `zmem.v:74-76,128` | `memory.cpp:204-206` | `TM:76-78` | MT `MEM5` | match |
| 9 | W0 normal mode (`!W0_MAP` = 1): page = PAGE0, ROM uses page[4:0] | `zmem.v:73,81,298` | `memory.cpp:175-177,199-201` | `TM:42,71-74` | MT `MEM1` | match |
| 10 | W0 mapped mode (`!W0_MAP` = 0): page = `{PAGE0[7:2], ~DOS, ROM128}`, for RAM too. Groups: +0 service, +1 TR-DOS, +2 128, +3 48 | `zmem.v:73` | `memory.cpp:179-188` (uses `p7FFD & 0x10`, which a MEM_CONFIG write keeps in sync, `io.cpp:1483-1488`) | `TM:52-57` | MT `MEM3`, `MEM4`; PDT `DosTrapNeedsMappedModeAndRom128` | match |
| 11 | W0_RAM selects RAM; W0_WE gates RAM writes | `zmem.v:67-69,80-81,121` | `memory.cpp:190-195,390-391` | `TM:59-70` | MT `MEM2`, `WindowWritableFollowsTheMapper` | match |
| 12 | W0_WE = 1 with ROM in window 0 writes the flash chip (`romwe_n = !(memwr && w0_we)`) | `zmem.v:297`; doc `tsconf_en.md:218` | ROM bank writes go to TRASH (`memory.cpp:390-391`) | ROM bank, write dropped (`TM:71-74`) | none | ng-matches-Unreal, RTL differs |
| 13 | vdos: W0 = RAM page `#FF`, writable regardless of W0_WE | `zmem.v:73,80-81` | `memory.cpp:190-194,391` | `TM:46-51` | ST `VDOS1_VirtualDriveSwap` | match |
| 14 | Reset MEM_CONFIG = `#04` (normal mode, ROM page 0 = TS-BIOS, DOS trap off) | `zports.v:565`; doc `tsconf_en.md:209` | `tsinit()` sets `memconf = 0`, then `reset()` sets 4 only for `ResetRom=SYS`, else 0 (`tsconf.cpp:902`, `z80.cpp:113-119`) | `TS:68`, `PD:247` | PDT `RST1` | ng-matches-RTL, Unreal differs |
| 15 | `#7FFD` decode: `!a[15] && loa == #FD && !lock48` | `zports.v:624` | `!(port & 2)` and `!(port & 0x8000)`: low byte decoded on A1 only (`io.cpp:659,671`) | `PD:331-332` | PDT `P7F6` | ng-matches-RTL, Unreal differs |
| 16 | No `#1FFD` / `#DFFD`: `#1FFD` is `#7FFD` (A15 = 0), `#DFFD` selects the AY register (A15 = 1, A14 = 1) | `zports.v:624,633-635` | TSL is not in the `#1FFD` model lists (`io.cpp:674-688`); `#DFFD` matches `(port & 0xC0FF) == 0xC0FD` (`:790`) | `PD:331-332,598-600` | SNT `SND1` (`#3FFD`, `#C0FD`); `#1FFD` / `#DFFD` not asserted | match |
| 17 | LCK128 = 00 512K `{D7:D6, D2:D0}`; 01 128K `D2:D0`; 11 1024K `{D5, D7:D6, D2:D0}`; PAGE3[7:6] = 0 | `zports.v:551-553,582` | `io.cpp:727-753` | `PD:756-774` | PDT `P7F1`, `P7F2`, `P7F3` | match |
| 18 | LCK128 = 10 auto: `lock128 = !(D7 ^ D6)` of the last opcode fetch (`OUT (n),A` gives 128K, `OUT (C),r` gives 512K) | `zports.v:549-556` | `cpu.opcode` (`io.cpp:727,744-746`) | `PD:758-760,941-948` | PDT `P7F4` | match |
| 19 | lock48 = D5 of a `#7FFD` write outside 1024K mode. It blocks the whole `#7FFD` write (PAGE3, ROM128, V_PAGE) until reset; `#xxAF` paging still works | `zports.v:621-630` | `io.cpp:707-722,750-753` | `PD:746-747,776-777` | PDT `P7F5`, `P7F3` | match |
| 20 | `#7FFD` D4 -> MEM_CONFIG[0] (ROM128); D3 -> V_PAGE 5/7 at once (bypasses the line latch) | `zports.v:581`; `video_ports.v:122,150-151` | `io.cpp:757-761`; `#21AF` keeps `p7FFD.4` (`:1484-1486`) | `PD:750-754,779-780` | PDT `P7F1`, `P7F5`, `P7F7` | match |
| 21 | DOS on: opcode fetch in window 0 at `#3D00-#3DFF`, ROM128 = 1, mapped mode. W0_RAM is not checked | `zmem.v:87` | `CF_SETDOSROM` needs `p7FFD & 0x10` and a ROM bank in window 0, with no W0_MAP check (`memory.cpp:403-412`, `z80_main.inl:187-194`) | `PD:921-931` | PDT `DosTrapAndExit`, `DosTrapNeedsMappedModeAndRom128` (no W0_RAM = 1 case) | ng-matches-RTL, Unreal differs |
| 22 | DOS off: opcode fetch outside window 0 (`!win0`) and not in vdos | `zmem.v:88` | `CF_LEAVEDOSRAM`: an opcode fetch from any RAM bank, window 0 included, closes DOS (`z80_main.inl:205-209`) | `PD:932-937` | PDT `DosTrapAndExit`; ST `VDOS1_VirtualDriveSwap` (no exit in vdos) | ng-matches-RTL, Unreal differs |
| 23 | FM window: hit = `a[15:12] == FMADDR[3:0] && FMADDR[4] && memwr`. `a[11:9]` 000 = CRAM, 001 = SFILE, `a[11:8]` 0100 = registers; `#500-#FFF` has no effect; write-only | `zmaps.v:39-45,55-71` | `z80_main.inl:108-141`. An even write anywhere outside REGS, `#500-#FFF` included, overwrites the stash (RTL: only `cram_hit \|\| sfile_hit`) | `PD:995-1036`, `TS:181-182` | PDT `FM1`, `FM2`, `FM3`, `FM4` | ng-matches-RTL, Unreal differs |
| 24 | An FM-window write also writes memory (the DRAM request ignores FMAPS), the ROM window included | `zmem.v:121`; `zmaps.v:55` | write falls through to the bank (`z80_main.inl:159-166`) | overlay after the normal write (`core/src/emulator/memory/hostbusoverlay.h`) | PDT `FM1` | match |
| 25 | FM CRAM/SFILE word = `{odd byte, latched even byte}`, one shared latch | `zmaps.v:64-81` | `temp.fm_tmp` | `TS` `fmStash`, `PD:1023-1029` | PDT `FM2` | match |
| 26 | Reset clears FMAPS[4] only; the address nibble is kept | `zports.v:561` | `fmaddr = 0` (`tsconf.cpp:890`) | `PD:245` | PDT `FM4` | ng-matches-RTL, Unreal differs |
| 27 | CACHE_CONFIG[3:0] per window; a SYS_CONFIG write copies bit 2 into all four bits | `zports.v:593-603`; `zmem.v:214` | `io.cpp:1448-1456`; `z80_main.inl:37` | `PD:707-712,720-722`; `TM:114` | MT `CCH2`, `CacheOnlyForEnabledWindows` | match |
| 28 | Cache: 256 x 16-bit entries indexed by A[8:1], tag `{page, A[13:9]}`, a miss fills the word, ROM is never cached | `zmem.v:120,209-292` | `z80_main.inl:27-50` (byte arrays, both bytes filled) | `TM:96-128` | MT `CCH1` | match |
| 29 | Invalidation: a CPU write to a hit entry (RTL also requires `ramwr_en`); DMA and video writes do not invalidate | `zmem.v:215,262-265` | every write invalidates both bytes (`z80_main.inl:143-150`) | on a tag match (`TM:130-143`, `PD:1090-1095`), write-protected windows included | MT `CCH1` | match (the variants differ only in hit rate) |
| 30 | Cache contents are never cleared: not by reset, not when disabled; reads fill even with every window disabled | `zmem.v:222-292` (no `aclr`), `:214` | cleared on reset (`tsconf.cpp:921,876-880`); fills while disabled (`z80_main.inl:37`) | not filled while every window is off, cleared when the cache goes off and so at every reset (`TM:207-212`, `PD:1063-1072`); `PD:220-222` says "Not reset: ... the cache contents" | none | ng-matches-Unreal, RTL differs; **fixed 2026-10-05** (gap G1): fills on every CPU DRAM read, cleared only at power-on, MT `CCH3` |
| 31 | SYS_CONFIG[1:0] = 3.5 / 7 / 14 / 14 MHz (`turbo14 = turbo[1]`); [4:3] AY clock is not connected (`.ay_mod(2'b00)`) | `top.v:224-229,530-533`; `zmem.v:149` | `z80.cpp:190-198`; `ayclk` unused | `PD:1099-1124` | PDT `SysConfigClock` | match |
| 32 | Warm reset (AVR `genrst`): PAGE `{0,5,2,0}`, MEM_CONFIG `#04`, SYS_CONFIG 0, CACHE_CONFIG 0, INT_MASK 1, FDD_VIRT 0, FMAPS.MEN 0, V_CONFIG 0, V_PAGE 5, PAL_SEL `#0F`, T_CONFIG 0, G_X/G_Y 0, HS_INT **1**, VS_INT 0 (+ increment 0), lock48 0, EFF7 0, DOS 0, vdos 0, SPI CS all high | `zports.v:558-576,626-628,668-680,724-727`; `video_ports.v:77-82,95-105,137-148`; `zmem.v:91-93,107-112`; `top.v:536-541`, `common/slavespi.v:77,197` | `tsinit()` (`tsconf.cpp:883-924`): `hsint = 2`, MEM_CONFIG per ResetRom (row 14), FMAPS address cleared (row 26) | `PD:223-299` | PDT `RST1` | ng-matches-RTL, Unreal differs |
| 33 | Not reset by a warm reset: BORDER, T_MAP_PAGE, T0/T1_G_PAGE, SG_PAGE, T0/T1 offsets, DMA_WPD, the FDC drive latch, CRAM, SFILE | registers without a reset branch: `video_ports.v:45-72,107-134`; `zports.v:599-600,654-656` | CRAM `#F0-#FF` is reloaded at every reset (`z80.cpp:174`, `draw.cpp:820-827`) | `PD:220-299` (not touched) | PDT `RST2`, `FM4` | ng-matches-RTL, Unreal differs |
| 34 | Power-on (FPGA configuration): CRAM = `video/mem/video_cram.mif` (all 256 words compared: identical to `kTsConfCramPowerOn`), SFILE = 0 (no `lpm_file`), registers without a reset branch = 0, PWR_UP = 1 | `video/video_out.v:135-170`; `video/video_ts.v:355-391`; `video_ports.v:45-72` | CRAM is zero apart from `#F0-#FF` (`draw.cpp:820-827`) | `PD:148-159`; `core/src/emulator/platforms/tsconf/tsconfcraminit.h` | PDT `RST2` (3 CRAM words spot-checked) | ng-matches-RTL, Unreal differs |
| 35 | `#FE` read: full low byte, `{1, tape_in, 1, keys}`, half-rows by A15..A8 | `zports.v:330,405-406`; `zkbdmus.v:89-103` | `pFE = !(port & 1)` (A0 only, `io.cpp:1340-1353`) | `PD:333-334,514-516`; `core/src/emulator/ports/portdecoder.cpp:1258-1273` | none | ng-matches-RTL, Unreal differs |
| 36 | `#FE` write: BORDER = `{palsel[3:0], 0, D[2:0]}`, where `palsel` is the copy latched at the start of each line, not the register just written | `video_ports.v:109,153-160` | `border = 0xF0 \| (val & 7)`, PAL_SEL ignored (`io.cpp:623-632`) | unlatched `regs[PalSel]` (`PD:604-606`); the comment says "latched", and `TsConfState::latPalSel` exists | PDT `BorderWriteUsesPalSel` (asserts the unlatched result) | ng differs from both |
| 37 | `#FE` beeper (D4) and tape out (D3) | `zports.v:488-489`; `top.v:1218-1227` | `io.cpp:613-621` | `PD:1365-1377` | SNT `SND2_SharedDac` | match |
| 38 | Covox `#FB`: full low byte, any high byte | `zports.v:254,330,490` | `conf.sound.covoxFB && !(port & 4)` (A2 only, `io.cpp:867-873`) | `PD:335-336,608-610` | SNT `SND2` (`#12FB`) | ng-matches-RTL, Unreal differs |
| 39 | Kempston joystick `#1F` (8-bit) only when `!dos && !open_vg` | `zports.v:282,334,450-455`; `tune.v:30` | any port with A5 = 0 (`io.cpp:1283-1299`); not fitted reads `#FF` | `PD:337-338,524-527` | PDT `JOY5`, `JOY6`, `BetaPortsGatedByDosOrVgOpen` | ng-matches-RTL, Unreal differs |
| 40 | Kempston mouse `#xxDF`, also in DOS: A8 = 0 buttons/wheel, A8 = 1: A10 ? Y : X | `zports.v:283,334,457-458`; `zkbdmus.v:107` | only `#FADF/#FBDF/#FFDF` after `\| 0xFA00`; other `#xxDF` (`#FEDF`) and any A5 = 0 port read the joystick (`io.cpp:1283-1294`) | `PD:346-347,531-537` | none in the TS-Conf suites | ng-matches-RTL, Unreal differs |
| 41 | AY: `loa == #FD && A15`; BC1 = A14; BDIR = write. `#FFFD` reads the chip; `#BFFD` read floats | `zports.v:344,632-635` | write `#BFFD` = `(port & 0xC000) == 0x8000` inside the A1 = 0 branch (`io.cpp:827-851`); read `p1 == #FD` with A15 = A14 = 1 (`:1365-1394`) | `PD:331-332,517-520,598-600` | SNT `SND1` | ng-matches-RTL, Unreal differs |
| 42 | TurboSound chip select (`#FFFD` = `#FE`/`#FF`) | not in the FPGA: only `ay_bdir` / `ay_bc1` leave the chip (`top.v:63-66`, `zports.v:633-635`); board or card level | `conf.sound.ay_scheme` (`io.cpp:794-817`) | `PeripheralPortOut(PORT_FFFD)` to the sound manager (`PD:598-600`) | SNT `SND1` (comment) | unclear (board option, not RTL) |
| 43 | General Sound `#B3/#BB/#33`: not mainboard ports, the ZX-Bus card answers | `zports.v:330-334` (not in `porthit`) | `io.cpp:82-97,986-988` (`MOD_GS`) | `PD:552-562,631-638` | SNT `SND3` | match |
| 44 | Beta-128 `#1F/#3F/#5F/#7F/#FF` decoded only while `dos \|\| FDD_VIRT[7]` | `zports.v:327-334,344-345,642` | `io.cpp:215,425-453,458-460,1115,1224-1247` | `PD:325,337-343` | PDT `BetaPortsGatedByDosOrVgOpen` | match |
| 45 | VG93 selected only when `!vdos && !virt_vg`; `virt_vg = FDD_VIRT[drive_sel_raw]`, the drive latched before this cycle | `zports.v:640-647` | `(1 << comp.wd.drive) & fddvirt` (`io.cpp:443,1234`) | `PD:802-812` | ST `VDOS1_VirtualDriveHidesTheController` | match |
| 46 | vdos entry: any VG or `#FF` access in DOS (not vdos) to a virtual drive sets `pre_vdos`; vdos starts at the next opcode fetch | `zports.v:650`; `zmem.v:103-116` | OUT enters vdos at once (`io.cpp:443-447`); IN defers to the next M1 (`:1234-1237`, `z80_main.inl:177-181`) | `PD:816-820,914-919` | ST `VDOS1_VirtualDriveSwap` | ng-matches-RTL, Unreal differs |
| 47 | vdos exit: a VG register access (`#1F/#3F/#5F/#7F`, not `#FF`), immediately | `zports.v:651`; `zmem.v:108-112` | an IN from any of them, `#FF` included, exits (`io.cpp:1228-1233`); an OUT `#FF` inside vdos writes the real WD (`:429-435`) | `PD:821-826` | ST `VDOS1_VirtualDriveSwap` | ng-matches-RTL, Unreal differs |
| 48 | An OUT `#FF` latches the drive bits whenever `dos \|\| open_vg`, inside vdos too | `zports.v:648,654-656` | only through `wd.out` when the chip is accessed | `PD:813-814` | ST `VDOS1_VirtualDriveSwap` | ng-matches-RTL, Unreal differs |
| 49 | IN `#FF` (VGSYS) is driven by the FPGA as `{intrq, drq, 6'b111111}` even when the chip is not selected (virtual drive, vdos) | `zports.v:330,344-347,447-448` | returns `#FF` and leaves vdos (`io.cpp:1228-1238`) | `#FF` when `!chipSelected` (`PD:805-812`) | none | ng differs from both |
| 50 | `#EFF7`: low byte `#F7`, A8 = 1, A12 = 0, written only outside DOS, only bit 7 used | `zports.v:720,724-732` | full 16-bit `port == 0xEFF7`, writable in DOS (`io.cpp:884-912`) | `PD:344-345,849-856` | PDT `CmosGating` (`#EFF7` only) | ng-matches-RTL, Unreal differs |
| 51 | Gluk writes: `#DFF7` (A13 = 0) address, `#BFF7` (A14 = 0) data, when `(EFF7[7] \|\| dos) && (!dos \|\| vdos)` | `zports.v:720,732,742-749` | full 16-bit match, allowed in any DOS state (`io.cpp:913-951`) | `PD:836-839,857-861` | PDT `CmosGating`; ST `VDOS2_CmosInsideVdos` | ng-matches-RTL, Unreal differs |
| 52 | Gluk read outside DOS: A14 = 0, A8 = 1, EFF7[7] = 1 | `zports.v:469-476` | full 16-bit `port == 0xBFF7` (`io.cpp:1413-1422`) | `PD:841-847` | PDT `CmosGating` | ng-matches-RTL, Unreal differs |
| 53 | Gluk read in DOS from the TR-DOS ROM: `#FF`. `porthit` holds `(loa == PORTF7) && !dos`, so the FPGA does not drive the bus and `zbus` drives `#FF` | `zports.v:330,347`; `top.v:435`; `z80/zbus.v:31-32` | reads CMOS (`CF_DOSPORTS`, `io.cpp:1413-1415`) | `#FF` (`PD:838`) | PDT `CmosGating` | ng-matches-RTL, Unreal differs |
| 54 | Gluk read inside vdos: still `#FF` by the same `porthit` term (vdos implies `dos` = 1: `zmem.v:88-98`, `zports.v:650`). The `a[8] ^ dos` term at `zports.v:471` cannot be reached | `zports.v:330,347,471` | reads CMOS (`io.cpp:1413-1422`) | returns AVR data (`PD:838,844-845`); spec §9 claims the RTL allows it | ST `VDOS2_CmosInsideVdos` (asserts the non-RTL value) | ng-matches-Unreal, RTL differs |
| 55 | `#xxF7` bus ownership: outside DOS every `#xxF7` is a mainboard port (the A8 = 0 ones read `#FF` and never reach the ZX-Bus); in DOS none is | `zports.v:330,469-476` | n/a (full-decode compares, then `#FF`) | A8 = 0 goes to `ZxBus` outside DOS; A8 = 1 stays `Gluk` in DOS (`PD:344-345`) | none | ng differs from both (visible only with a ZX-Bus card on `#xxF7`) |
| 56 | `#xxEF`: any high byte is an AVR wait port (16550 `#F8EF-#FFEF`, ZiFi) | `zports.v:288,334,478-481,735-764` | only while `zf232.open_port` (`io.cpp:952-953,1424-1425`) | `PD:352-353,547-551` + the AVR block claims the port (`PD:1138-1163`) | ZT (19 tests) | ng-matches-RTL, Unreal differs |
| 57 | SD `#57` / `#77`: full low byte, also in DOS | `zports.v:285-286,334,460-467,663-665` | `io.cpp:122-126`; IN `conf.zc && (p1 == 0x57) \|\| (p1 == 0x77)` (precedence: `#77` reaches Zc with ZC off, `:1005`) | `PD:348-351,538-546,617-625` | ST `SPI1_PortSemantics`, `SD0_ReadsASectorThroughThePorts` | match |
| 58 | IN `#77` = `#00` (no ESP32 build) | `zports.v:460-465` | `Status` = 0 (`zc.cpp:81-82`) | `PD:538-542` | ST `SPI1` | match |
| 59 | OUT `#77`: D1 = SD /CS (`spi_cs_n <= {~din[4:2], din[1]}`); reset deselects | `zports.v:668-691` | `Cfg = 3` at reset, `Cfg & 2` (`zc.cpp:14,41-62`) | `core/src/emulator/io/spi/zcontrollerspi.h:46,96`; `PD:296` | ST `SPI1` | match |
| 60 | `#57`: a write sends; a read returns the previous exchange and sends `#FF` | `zports.v:714-717`; `common/spi.v:34-62` | `zc.cpp:55-62` | `core/src/emulator/io/spi/zcontrollerspi.cpp:45-55` | ST `SPI1` | match |
| 61 | Nemo IDE CS0 aliases `#08/#28/.../#E8` (`loa[2:0] == 0 && loa[3] != loa[4]`); `#C8` = CS1 | `zports.v:337-341,769-770` | not decoded: `(port & 0x1E) == 0x10` only (`io.cpp:128-174,1077-1106`) | `core/src/emulator/io/ide/ideadapter.cpp:189-200,234-244` | `core/tests/emulator/io/ide/idecontroller_test.cpp` (`SchemeFits` only) | ng-matches-RTL, Unreal differs |
| 62 | Nemo IDE odd ports: only `#11` (`ide_port11`); `#31/#51/.../#F1` are not IDE | `zports.v:337-339` | `(port & 0x1E) == 0x10` takes `rrr1000x` (`io.cpp:131`) | `cs0 = (low & 0x1E) == 0x10` takes `#31..#F1` as registers 1..7 (`ideadapter.cpp:198,226-229,243,276-279`) | none | ng-matches-Unreal, RTL differs |
| 63 | FDD_VIRT: write-only, reset 0, [3:0] virtual drives, [7] VG_OPEN | `zports.v:563,608-609,640-642` | `val & 0x8F` (`io.cpp:1458-1460`), reset 0 (`tsconf.cpp:897`) | `PD:251,717-719` | PDT `BetaPortsGatedByDosOrVgOpen` | match |

Counts: 31 `match`, 24 `ng-matches-RTL, Unreal differs`, 4 `ng-matches-Unreal, RTL differs`,
3 `ng differs from both`, 1 `unclear` (63 rows).

## Bugs and gaps

### B1 (row 54): the CMOS answers inside vdos; the RTL reads `#FF`

`porthit` includes `((loa==PORTF7) && !dos)` (`zports.v:330`), and `dataout = porthit && iord &&
~external_port` (`:347`) is the only way the FPGA drives a port read (`top.v:435`). vdos is entered
only from DOS (`vdos_on` needs `dos`, `zports.v:650`), and DOS cannot end during vdos (`dos_off`
needs `!vdos`, `zmem.v:88`), so `dos` = 1 for the whole vdos session. Every `#xxF7` read then comes
from the ZX-Bus default `#FF` (`zbus.v:31-32`). Writes inside vdos do reach the AVR (`portf7_wr`
allows `vdos`, `:720,742-749`).

The `(a[8] ^ dos)` term in the read mux (`:471`) suggests the authors meant `#BEF7` to work in DOS,
but that path cannot be reached in this RTL. unreal-ng (`PD:836-847`), the test
`VDOS2_CmosInsideVdos` and hardware-spec §9 all follow the intent instead of the gate. TS-Labs
Unreal also returns the CMOS (it reads it in every DOS state).

- Fix: `DecodeF7In` returns `#FF` whenever `_ts.dos`. Keep the write gating as it is.
- Test `VDOS2_CmosReadFloatsInsideVdos`: with EFF7 = `#80`, set CMOS `#0E` = `#5A`, then
  `dos = vdos = 1`. Assert `In(0xBFF7) == 0xFF`. Then `Out(0xDFF7, 0x0E); Out(0xBFF7, 0x33)`, leave
  vdos and DOS, and assert `In(0xBFF7) == 0x33` (the write inside vdos landed).
- Correct hardware-spec §9 to match.

### B2 (row 36): `#FE` builds BORDER from the unlatched PAL_SEL

The RTL uses `palsel`, the copy latched at `line_start_s` (`video_ports.v:109,153-160`), not
`palsel_r`. unreal-ng uses `_ts.regs[TsConfReg::PalSel]` (`PD:605-606`) although
`TsConfState::latPalSel` exists. They differ when PAL_SEL is written and `#FE` follows before the next
line start. The test `BorderWriteUsesPalSel` asserts the unlatched value.

- Fix: use `_ts.latPalSel`, after `FlushVideo()` (already called) has brought the engine up to the
  write.
- Test `BorderWriteUsesTheLatchedPalSel`: write PAL_SEL = `#0A` and `OUT (#FE),5` in the same line,
  then assert BORDER = `#F5`. After the engine passes a line start, `OUT (#FE),5` gives BORDER =
  `#A5`. Rewrite `BorderWriteUsesPalSel` to cross a line start.

### B3 (row 62): the Nemo-DIVIDE decode takes `#31/#51/.../#F1` as IDE registers

The RTL has `ide_even = (loa[2:0]==0) && (loa[3]!=loa[4])` plus exactly `#11` (`zports.v:337-339`).
`IdeAdapter::EvoIn` / `EvoOut` test `(low & 0x1E) == 0x10` (`ideadapter.cpp:198,243`). That accepts
`rrr10001` and maps it to register `low >> 5` (`:226-229`, `:276-279`). A program touching, for
example, `#F1` reads or writes the IDE command / status register. TS-Labs Unreal has the same
over-decode.

- Fix: accept odd low bytes only when `low == 0x11`.
- Test `IDE6_OddPortsOtherThan11AreNotIde` (`TsConfStorage_Test`, `[HDD] Scheme=NEMO-DIVIDE`): for
  `p` in `#31,#51,#71,#91,#B1,#D1,#F1`, `In(p)` returns `#FF`. `Out(p, 0xEC)` does not start
  IDENTIFY: the drive status is unchanged and no DRQ.

### B4 (row 49): VGSYS read for a virtual drive or inside vdos

The RTL always drives `{vg_intrq, vg_drq, 6'b111111}` on `#FF` while `dos || open_vg`, whatever the
chip select (`zports.v:330,344-347,447-448`). unreal-ng returns `#FF` (`PD:805-812`). The effect is
small: bits 7 and 6 read 1 instead of the physical controller's INTRQ / DRQ.

- Test `VDOS3_SystemPortReadsTheControllerLines`: `FDD_VIRT = #01`, `dos = 1`, controller idle after
  a completed command (INTRQ = 1, DRQ = 0). Assert `In(0x00FF) == 0xBF` and `preVdos == 1`.

### B5 (row 55): `#xxF7` ownership

Outside DOS the RTL claims every `#xxF7` (A8 = 0 reads `#FF`, `zports.v:474-475`), and in DOS it
claims none. unreal-ng sends A8 = 0 to the ZX-Bus and keeps A8 = 1 as `Gluk` inside DOS
(`PD:344-345`). This is visible only with a ZX-Bus card that decodes `#xxF7`.

- Test `GlukPortOwnershipFollowsDos` (`ClassifyPort`): outside DOS `#FEF7` (A8 = 0) is a mainboard
  arm that reads `#FF`, not `ZxBus`; with `dos = 1`, `#BFF7` is `ZxBus`.

### Gap G1 (row 30): the cache is cleared by reset and when disabled; the RTL never clears it

**Fixed 2026-10-05.** Checked on the running RTL (`tools/machines/tsconf/rtl-sim`, `tsconf-cpu-sim cache`, 10
scenarios, `results/cache-retention.txt`): the entries survive a reset and `CACHE_CONFIG` = 0, reads fill with the
cache off, and a CPU write invalidates the entry it hits with the cache on or off (`zmem.v:215` `cache_inv` has no
`cache_en`). unreal-ng now models the retention (`TsConfMemory::CacheRead` / `AfterWrite`; the cache is zeroed
only at power-on, the reset comment is true); test `TsConfMemory_Test.CCH3_CacheFillAndRetentionMatchTheRtl`. The
text below is the finding as audited.

This deviation is documented (technical-design §3.5 item 3). It matters only for reads that hit stale
entries after DMA. However, the reset comment `PD:220-222` lists "the cache contents" as not reset,
while `ApplyState()` -> `RefreshCache()` -> `CacheClear()` clears them. Fix the comment, or model the
retention: keep the tags across reset and fill while disabled. No test needed for the comment fix.

### Gap G2 (row 12): W0_WE with ROM in window 0 does not reach the flash

The RTL asserts `romwe_n` (`zmem.v:297`; doc `tsconf_en.md:218` "for ROM the write goes to the
flash"). Both emulators drop the write, so TS-BIOS flash updates cannot work.

- Test, once the flash is modeled: `MEM6_RomWriteEnableReachesTheFlash`. With MEM_CONFIG = `#06`
  (normal, W0_WE), the AMD/SST program sequence on page 0 changes the byte. With W0_WE = 0 it does
  not.

### Untested rows (behavior matches, coverage missing)

- Row 3 `STATUS_ReportsTheBuild`: with `ts_vdac` 0, 3 and 7, the first `In(0x00AF)` equals
  `0x40 | v`. Use literals, not `VdacVersion()`.
- Row 16 `P7F8_1FFDIsPagingAndDFFDIsTheAy`: `Out(0x1FFD, 0x03)` sets PAGE3 = 3. `Out(0xDFFD, 0x07)`
  leaves PAGE3 unchanged and `ClassifyPort(0xDFFD) == Ay`.
- Row 21 `DosTrapFromMappedRam`: MEM_CONFIG = `#09` (mapped, RAM, ROM128 = 1). An opcode fetch at
  `#3D00` sets `dos = 1` and window 0 shows RAM `{PAGE0[7:2], 0, 1}`.
- Row 22 `DosStaysOnWhileWindowZeroRamRuns`: in DOS with W0_RAM = 1, an opcode fetch at `#1000`
  keeps `dos = 1`.
- Row 34 `PowerOnCramIsTheFirmwareTable`: after `PowerOn()`, `ts.cram` equals
  `kTsConfCramPowerOn` word for word, and `sfile` is all zero.
- Row 35 `FeReadBits`: no key pressed and tape low: `In(0x7FFE) == 0xBF`. Tape high:
  `In(0x7FFE) == 0xFF`. `In(0x00FC)` is not `#FE` (`ZxBus`).
- Row 40 `MouseRegistersByA8A10`: `#FADF` buttons, `#FBDF` X, `#FFDF` Y, `#FEDF` buttons, `#7ADF`
  buttons; also in DOS.
- Row 61 `IDE7_CsZeroAliases`: `In(0x00E8)` reads the status register (`#F0` alias), `In(0x00C8)`
  the alternate status.

### TS-Labs Unreal divergences (for reference, no unreal-ng action)

- Partial low-byte decode of `#FE` (A0), `#7FFD` / `#BFFD` (A1), `#FB` (A2) and Kempston (A5).
- ZiFi answers only with a host COM port configured. Otherwise GETVER reads `#FF`, which is why the
  0.733 client stops with "Please update TS Conf.". The check never involves `#00AF`.
- MEM_CONFIG reset `#00` unless `ResetRom=SYS`; HS_INT reset 2; FMAPS address cleared at reset;
  CRAM `#F0-#FF` reloaded at every reset, and the rest of CRAM is not the firmware table.
- DOS trap without the W0_MAP condition, blocked by RAM in window 0; DOS exit on any RAM fetch.
- vdos: immediate entry on OUT, exit on IN `#FF`, OUT `#FF` written to the real WD inside vdos.
- Gluk / `#EFF7` decoded on all 16 bits and reachable from the TR-DOS ROM.
- BORDER from `#FE` ignores PAL_SEL.
- The FM stash is also overwritten by even writes at `#500-#FFF`.
- Precedence bug on IN `#77` (`io.cpp:1005`).
