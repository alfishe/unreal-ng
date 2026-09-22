# ZX Profi - review of existing emulators (2026-09-21)

Purpose: collect how existing emulators and one hardware-derived FPGA core implement the ZX Profi
(Profi 1024 / 512, "PROFI+") so unreal-ng can implement `PortDecoder_Profi` correctly.

Corpus root used below: `/Volumes/TB4-4Tb/Projects/emulators/github/` (abbreviated `$C/`).

Sources that actually contain Profi code (everything else in the corpus was grepped and has none):

| Tag | Source | Path | Nature |
|---|---|---|---|
| **UNREAL** | UnrealSpeccy (NedoPC/ZX-Evo fork, newer) | `$C/zx-evo/pentevo/unreal/Unreal/{io,config,draw,drawers,vars}.cpp`, `cfg/Unreal.ini` | full emulation (ports, video, IDE, RTC, palette) |
| **UNREAL-SP** | `unreal-speccy` (Qt-less 0.39-based snapshot) | `$C/unreal-speccy/{io,memory,config,draw,dxr_prof,profi}.cpp` | same lineage, older; has the memory mapper and the hi-res renderer |
| **ZXMAK2** | ZXMAK2 (C#) | `$C/ZXMAK2/src/ZXMAK2.Hardware/Profi/*.cs`, `$C/ZXMAK2/ROMS/~mapping.xml` | modular device model, very readable |
| **XPECCY** | Xpeccy | `$C/Xpeccy/src/libxpeccy/hardware/profi.c`, `video/video.c`, `hdd.c` | table-driven ports |
| **XPECCY+** | xpeccy-plus (fork) | `$C/xpeccy-plus/res/machines/profi.conf`, `docs/machines-reference.md` | config/doc only, core = Xpeccy |
| **KARABAS** | Karabas-Pro FPGA "profi" firmware (VHDL) | `$C/karabas-pro/firmware/src/fpga/profi/rtl/` | RTL of a real Profi-compatible board; closest thing to hardware ground truth in the corpus, but it is a modern clone with extras (DivMMC, ZiFi, flash regs, 6 MB option) |

Not Profi: `UnrealSpeccyP`, ZX-M8XXX (+alfishe), Zero-Emulator, Spectral, jnext (mentions "profi" only as the ZX Next VHDL
flag that it hard-wires to 0), zxsp, Scorpion256TPlus. ZX-M8XXX ships a `roms/profi.rom` but no Profi machine code.
`zxevo.pentevo/tools/unreal_fix/0.39.0/*` and `pentevo/tools/...` are older copies of UNREAL (same code).

---

## 1. Model definition, ROM set, RAM

| Source | Definition | RAM | ROM |
|---|---|---|---|
| UNREAL | `vars.cpp:270` `{ "Profi", "PROFI", MM_PROFI, 1024, RAM_1024 }` | 1024K (`ramsize` may be smaller; mask `temp.ram_mask = (ramsize-1)>>4`, `config.cpp:1010`) | one 64K file, `PROFI=rom\profi.rom` (`Unreal.ini:532`); 4 x 16K pages; `[ROM.profi]` (`Unreal.ini:591-596`) `sys=:0 dos=:1 128=:2 sos=:3` |
| UNREAL-SP | `vars.cpp:211`, `config.cpp:899-903` | same | same layout: `base_sys_rom=p0, base_dos_rom=p1, base_128_rom=p2, base_sos_rom=p3` |
| ZXMAK2 | `MemoryProfi1024.cs:29-35` `"PROFI+ 1024K"`, romset `"PROFI"`, 4 ROM pages, 64 RAM pages; `MemoryProfi512` (`:237-244`) = romset `PROFI-V03`, 32 RAM pages | 1024K / 512K | `GetRomIndex` (`:158-169`): `ROM_SYS=0, ROM_DOS=1, ROM_128=2, ROM_SOS=3`. Rom sets in `~mapping.xml:43-56`: `PROFI/PROF-M.ROM`, `PROFI_v03.ROM`, `profi_v10.rom`, `profi_v450.ROM`, `profi-hddboot.rom` (all single 64K "RAW") |
| XPECCY | `profi.c:183` `HW_PROFI ... MEM_512K \| MEM_1M` | 512K or 1M | comment `profi.c:6` "Profi ROM: EXT,DOS,128,48" = pages 0..3 |
| XPECCY+ | `profi.conf`: `memory = 1024`, `rom0 = profi.rom`, 1 YM 1.75 MHz ACB, covox, Beta Disk, `ide = profi`, mouse yes; doc says "512K, **1M**" | 1M default | `docs/machines-reference.md:127` "Profi: profi.rom, 64K combined" |
| KARABAS | `karabas_pro.vhd:1373` etc. | 1M default (`RAM_EXT(2:0)` = DFFD[2:0]), 6 MB option | `memory.vhd`: `rom_page <= (not TRDOS) & ROM_BANK` (see 4.) |

All four agree on the ROM page order **SYS(0), DOS(1), 128(2), 48(3)** and on the 64K single-file layout.

### 1.1 ROM-order check against the actual files (resolves a doubt in `unreal-ng-integration-audit.md` section 1.2)

The audit measured `data/rom/profi.rom` and concluded page 0 = "128K ROM with Profi menu", page 2 = "service/STS", suggesting
`128=0, sys=2`. Re-measured (first bytes / strings):

| File | page 0 | page 1 | page 2 | page 3 |
|---|---|---|---|---|
| `ZXMAK2/ROMS/PROFI/PROFI_v03.ROM` | `C3 09 00`, "Computer- Profi", "fIRMA profi" (boot menu / BIOS) | "TR-DOS Ver 5.04T" | `F3 01 2B 69`, contains 128K editor code | 48K BASIC |
| `karabas-pro/.../profi_v450.ROM` | `C3 CC 1C`, "Award Modular BIOS v 4.50PG ... Profi Code Club" | "TR-DOS Ver 6.08, 1998 Profi Code Computers Club" | "Monitor-Debugger 128/48" | 48K BASIC |
| `unreal-ng/data/rom/profi.rom` | `ED 56 C3 34 03`, "TR-DOS 48K/128K, Sinclair 48/128, ROM Bios" (boot menu) | TR-DOS 6.08 | "Monitor-Debugger 128/48" | 48K BASIC |

Page 0 is the **service (SYS) ROM = boot menu + BIOS** (menu entries are "TR-DOS 48K / TR-DOS 128K / Sinclair 48 / Sinclair 128 /
ROM Bios"); page 2 holds the 128K editor plus monitor. This matches the emulators' `sys=0, 128=2` and the FPGA: at reset
`dos_act='1'` and ROM14=0 gives `rom_page = 0` (`karabas_pro.vhd:1508`, `memory.vhd`). So `rom.cpp:172-177` (`sys=0,dos=1,128=2,sos=3`) is correct
and should NOT be swapped; what is wrong is booting into 48K BASIC instead of the service ROM (see section 8).

---

## 2. Ports

### 2.1 #7FFD and #DFFD (paging)

**UNREAL / UNREAL-SP** (`io.cpp` UNREAL-SP lines 540-575; UNREAL `io.cpp:690-770`):

```cpp
// 7FFD (bit15 = 0, bit1 = 0 for non-Scorpion/Quorum: general 128K decode, port & 0x8002 == 0)
if (comp.p7FFD & 0x20) {                    // 48K lock latch
    // DFFD.4 = "worom" lifts the lock on Profi
    else if (conf.mem_model == MM_PROFI && (comp.pDFFD & 0x10)) goto set_7ffd;
    else return; }
set_7ffd: comp.p7FFD = val; set_banks(); return;
// xx0xxxxxxxxxxx0x (3.2) [vv]
if ((port & 0x2002) == (0xDFFD & 0x2002) && conf.mem_model == MM_PROFI)
{ comp.pDFFD = val; set_banks(); init_raster(); return; }
```

`0x7FFD` block runs under `!(port & 0x8000)`, therefore `#DFFD` (A15=1, A13=0, A1=0) is only reached for A15=1; no double-fire.
Screen bit: `comp.p7FFD & 8` -> page 7 else 5 (`memory.cpp:24`). RAM bits 6-7 of 7FFD are ignored on Profi.

**Mapper** (`memory.cpp:121-131` UNREAL-SP, verbatim):

```cpp
case MM_PROFI:
   bank += ((comp.pDFFD & 0x07U) << 3U); bank3 = RAM_BASE_M + (bank & temp.ram_mask)*PAGE;
   if(comp.pDFFD & 0x08) { bankr[1] = bankw[1] = bank3; bank3 = RAM_BASE_M + 7 * PAGE; }
   if (comp.pDFFD & 0x10) bank0 = RAM_BASE_M+0*PAGE;
   if (comp.pDFFD & 0x20) comp.flags |= CF_DOSPORTS;
   if (comp.pDFFD & 0x40) bankr[2] = bankw[2] = RAM_BASE_M + 6*PAGE;
   break;
```
with `bank = p7FFD & 7`, default `bankr[1]=5`, `bankr[2]=2`, and `bank0` chosen before the switch (see 4).

**ZXMAK2** (`MemoryProfi1024.cs:46-47`, `103-156`, `175-193`):

```cs
bmgr.Events.SubscribeWrIo(0x8002, 0x7FFD & 0x8002, BusWritePort7FFD);   // A15=0, A1=0
bmgr.Events.SubscribeWrIo(0x2002, 0xDFFD & 0x2002, BusWritePortDFFD);   // A13=0, A1=0
...
int sega = CMR1 & m_cmr1mask;            // m_cmr1mask = ramPages/8 - 1  (7 for 1024K, 3 for 512K)
ramPage |= sega << 3;
MapRead4000 = sco ? RamPages[ramPage] : RamPages[5];   // SCO = CMR1.3
MapRead8000 = scr ? RamPages[6] : RamPages[2];         // SCR = CMR1.6
MapReadC000 = sco ? RamPages[7] : RamPages[ramPage];
MapRead0000 = norom ? RamPages[0] : RomPages[romPage]; // NOROM = CMR1.4
protected virtual void BusWritePort7FFD(...) { if (!m_lock) CMR0 = value; }   // m_lock = CMR0.5, cleared while NOROM
```
Names given by ZXMAK2 to DFFD bits: `DS80=7 (hi-res), SCR=6, CPM=5, NOROM=4, SCO=3`, bits 2:0 = RAM extension. Overlap 7FFD/DFFD is solved by
subscription order and the `handled` flag (7FFD first).

**XPECCY** (`profi.c:7-17, 69-84, 113-116`):

```c
{0x8002,0x7ffd,...,prfOut7FFD}, {0x2002,0xdffd,...,prfOutDFFD}, {0xc002,0xbffd..}, {0xc002,0xfffd..}
void prfOut7FFD(...) { if ((~comp->pDFFD & 0x10) && (comp->p7FFD & 0x20)) return; /* blocked */ ... }
memSetBank(comp->mem, 0x80, MEM_RAM, ((comp->pDFFD & 0x40) && (comp->p7FFD & 8)) ? 6 : 2, ...);   // NB extra 7FFD.3 condition
```
Other banks as UNREAL.

**KARABAS** (`karabas_pro.vhd:1374-1384, 1391-1394, 1539-1545`; `memory.vhd` process on `A(15:14)`):

```vhdl
cs_dffd <= '1' when ... cpu_a_bus = X"DFFD" and fd_port='1' and lock_dffd='0'   -- full 16-bit decode (board specific)
cs_xxfd <= '1' when ... cpu_a_bus(15)='0' and cpu_a_bus(1)='0'                   -- 7FFD: A15=0, A1=0
if cs_xxfd ... and (port_7ffd_reg(5)='0' or port_dffd_reg(4)='1') then port_7ffd_reg(5 downto 0) <= D(5 downto 0);
scr <= dffd(6); sco <= dffd(3); worom <= dffd(4); cpm <= dffd(5); ds80 <= dffd(7);
when "10" => if SCR='0' then page 2 else page 6           -- no dependency on 7FFD.3 in RTL
when "01" => if SCO='0' then 5 else (DFFD[2:0] & 7FFD[2:0])
when "11" => if SCO='0' then (DFFD[2:0] & 7FFD[2:0]) else 7
```
KARABAS also latches 7FFD[7:6] (Pentagon-512 style) into a `ram_ext` that only matters on the 6 MB option; ignored on the 1M board.

Result: consensus on bit meanings (table in section 6). Only disagreement: XPECCY's extra `7FFD.3` gate on SCR (section 5).

### 2.2 #FE, palette port (#7E family), border in hi-res

UNREAL-SP `io.cpp:471-477`, `profi.cpp`; UNREAL `io.cpp:640-651`:

```cpp
// in the #FE branch (port & 1 == 0)
if (conf.mem_model == MM_PROFI && !(port & 0x80) && (comp.pDFFD & 0x80))
    profi_writepal(u8(~(port >> 8)));            // colour = ~A15..A8, index = ~previous pFE & 0xF
static u8 profi_pal[16]; ProfiPalIdx = (~comp.pFE) & 0xF;  // "Gg0Rr0Bb"
comp.pFE = val;   // after: the value written by this OUT becomes the index for the NEXT palette write
```

ZXMAK2 `UlaProfi5XX.cs:27-31, 96-106`: `if ((addr & 0x0081) == 0 && CMR1.7) SetPalette((PortFE ^ 0x0F) & 0x0F, ~(addr>>8))`; map `Gg0Rr0Bb`, `r,g,b = 2 bits * 85`;
reset palette `m_pal_startup` (`:73-77`) = `00 02 10 12 80 82 90 92 | 00 03 18 1B C0 C3 D8 DB`.
Only `UlaProfi5XX` does colour hi-res; `UlaProfi3XX` (Profi 3.x) renders hi-res black/white (`ProfiRendererParams.c_ulaProfiColor=false`).

XPECCY `profi.c:26-51`: palette port is `{0x00ff,0x007e}` (mask 0xFF, port 0x7E) i.e. A7=0 with A0=0 must be exactly `xx7E`:

```c
void prfOut7E(...) { if (DFFD & 0x80) { port ^= 0xff00; col.b = tab[(port&0x0300)>>7]; col.r = tab[(port&0x1c00)>>10];
                     col.g = tab[(port&0xe000)>>13]; vid_set_col(vid, p7E & 15, col); p7E = ~val & 15; } }
void prfOutFE(...) { xOutFE(...); if (DFFD & 0x80) { vid->nextbrd ^= 7; vid->brdcol = vid->nextbrd; } }   // border inverted in hi-res
```
KARABAS `video.vhd:207,213`: `palette(BORDER[3:0] xor F) <= (not BUS_A[15:8]) & BORDER(7)` written when `CS7E & DS80`, `CS7E = cs_xxfe and A7=0` (`karabas_pro.vhd:1389`):
9-bit palette entry, `G = ~A15:13`, `R = ~A12:10`, `B = ~A9:8 & D7-of-previous-FE-write`.

Palette value format disagrees (2-bit vs 3-bit channels): see section 5.

FE read bit 7 (only 5.xx): ZXMAK2 `UlaProfi5XX.cs:34-53` ("UniCopy"; bit7 <- palette bit 6 of the colour selected by `PortFE`, inverted index in hi-res); KARABAS `video.vhd:219` `GX0 = pal(6) xor pal(0)` in DS80 mode, 1 otherwise. UNREAL/XPECCY do not emulate it.
XPECCY `prfInFE` sets bit 6 (EAR) from tape only. No emulator does the Profi keyboard's extra keys through #FE except XPECCY (`kbdScanProfi`, `input/keyboard.c:126`: `extMap` ANDed in, bit 5 forced from ext map).

`#FE` outside DOS ports: ZXMAK2 `UlaProfi3XX.cs:70-77` decodes `(addr & 0x67) == (0xFE & 0x67)` (A0=0, A1=1? see note) and ignores it while `DOSEN`. UNREAL decodes `!(port&1)` for Profi (`io.cpp:425-440`).
Note the ZXMAK2 mask `0x67` looks copied from Scorpion-like decoding; XPECCY `{0x00f7,0x00fe}` (A3 ignored, A0..A2,A4..A7 = FE) and KARABAS `A0=0` (`cs_xxfe`) show that only A0 is decoded on real boards with the standard ULA; treat A0-only as the reference.

### 2.3 AY / TurboSound

| Source | Decode |
|---|---|
| UNREAL | `(port & 0xC0FF) == 0xC0FD` (FFFD) and `0x80FD` (BFFD) - full low byte (stricter than hardware) |
| ZXMAK2 | no Profi-specific AY device in the Profi folder (uses the generic 128K AY device) |
| XPECCY | `{0xc002,0xbffd}`, `{0xc002,0xfffd}` = A15,A14,A1 only; single AY/YM (XPECCY+ `profi.conf`: `psg.type=ym, frq=1.75, stereo=acb`) |
| KARABAS | `cs_fffd = (A = FFFD and fd_port)`; second chip / TurboSound via `ssg_cn0/cn1` (clone extra) |

No emulator implements TurboSound for Profi. The 128K-style A15/A14/A1 decode is what everyone but UNREAL uses.

### 2.4 WD1793 (Beta Disk) ports and the "modified ports" (extended) mode

Three modes exist. Let `CPM = DFFD.5`, `ROM48 = 7FFD.4`, `DOS = TR-DOS/service latch`.

| Mode | Condition (UNREAL) | FDC data regs | System (FF) port |
|---|---|---|---|
| normal | DOSPORTS && !(ROM48 && CPM) and CPM=0 | `(p1&0x83)==0x03` -> 1F,3F,5F,7F (A6:5 = register) | `(p1&0xE3)==0xE3` -> `#FF` |
| CP/M "normal" | DOSPORTS && CPM=1 && ROM48=0 | same 1F..7F | `(p1&0xE3)==0xA3` -> `#BF` |
| extended ("modified ports") | `(p7FFD&0x10) && (pDFFD&0x20)` | `(p1&0x9F)==0x83` -> 83,A3,C3,E3 -> registers 1F,3F,5F,7F | `(p1&0xE3)==0x23` -> `#3F` |

UNREAL-SP `io.cpp:260-330` (write) and `867-905` (read), verbatim core:

```cpp
if((comp.p7FFD & 0x10) && (comp.pDFFD & 0x20)) { // modified ports
    if((p1 & 0x9F) == 0x83) wd.out((p1 & 0x60) | 0x1F, val);   // 83 A3 C3 E3
    if((p1 & 0xE3) == 0x23) wd.out(0xFF, val);                 // port 3F
    /* RTC, IDE below */ }
else { if((p1 & 0x83) == 0x03) wd.out((p1 & 0x60) | 0x1F,val);
       if((p1 & 0xE3) == ((comp.pDFFD & 0x20) ? 0xA3 : 0xE3)) wd.out(0xFF,val); }
```
XPECCY `profi.c:119-147` encodes the same thing as a `{mask, port, dos, rom, cpm}` table: `!DOS ROM CPM` -> 83/A3/C3/E3 and `3F`; `!DOS !ROM CPM` -> 1F/3F/5F/7F and `BF`; `DOS *` and `!CPM` -> 1F..7F and `FF` (mask `0x9F`, i.e. A7=0 and A4..A0=11111).
ZXMAK2 `FddControllerProfi.cs:19-61`: normal `IsActive = DOSEN || IsNormalMode`, `IsNormalMode = (cpm && !rom48) || (!cpm && SYSEN)`; ports `(0x9F, 0x1F&0x9F)` (1F/3F/5F/7F, A7=0, A4:A0=11111), system `(0x9F, 0xFF&0x9F)` (9F/BF/DF/FF alias); extended mode (`cpm && rom48`): `(0x9F, 0x83&0x9F)` = 83/A3/C3/E3 and `#3F` (full 8-bit).
KARABAS `karabas_pro.vhd:1463-1473` (hardware truth):

```vhdl
-- ext = (cpm='1' and rom14='1') or (dos_act='1' and rom14='0')
RT_F2_1 (sys port) : A7:5="001", A1:0="11"  and ext                                   -- 3F class
RT_F2_2 (sys port) : A7:5="101", A1:0="11"  and cpm='1' and dos_act='0' and rom14='0' -- BF class
RT_F2_3 (sys port) : A7:5="111", A1:0="11"  and cpm='0' and dos_act='1' and rom14='1' -- FF class
RT_F1_1 (regs)     : A7='0', A1:0="11" and cpm='1' and dos_act='0' and rom14='0'
RT_F1_2 (regs)     : A7='0', A1:0="11" and cpm='0' and dos_act='1' and rom14='1'
P0      (regs)     : A7='1', A4:0="00011" and ext                                     -- 83 A3 C3 E3
```
All four agree on the three port sets; **KARABAS additionally treats "DOS latch on with 7FFD.4=0" (the service/SYS ROM) as extended mode** - see section 5.

### 2.5 Kempston joystick / mouse

XPECCY `profi.c:138-143` (only when `!CPM`, i.e. never while CP/M ports are live): `1F` (Kempston joy, `xIn1F`, only when DOS is off), `#FADF/#FBDF/#FFDF` (Kempston mouse), `#FF` idle read (dummy).
KARABAS `karabas_pro.vhd:1822-1829`: `1F` when `dos_act=0 and cpm=0`; mouse `FADF/FBDF/FFDF` when `cpm=0`; port `#FF` (video attribute latch) when `dos_act=0, cpm=0, ds80=0`.
UNREAL: standard Kempston mouse decode (general code); `PortFF` preset flag `0` for Profi (`Unreal.ini:139`) = no floating "ULA port FF".
ZXMAK2: none in the Profi folder (generic Kempston devices).
Real Profi mouse is the standard Kempston mouse. Rule: joystick/mouse only while `CPM=0`.

### 2.6 Covox / SoundRive ("PROFI covox", 8255-style)

UNREAL `io.cpp:320-345` (extended mode) and `509-522` (normal mode):

```cpp
case 0x87:  // VV55 in CP/M-extended mode (p1&0x9F)==0x87  -> 87 A7 C7 E7
   if ((port & 0x60) == 0x40) covProfiL = ...; if ((port & 0x60) == 0x20) covProfiR = ...;
...
if (conf.mem_model == MM_PROFI && (port & 0x83) == 0x03) // normal mode, DOS ports off
   { L if (port&0x60)==0x40 ; R if (port&0x60)==0x20 }   // 5F = left, 3F = right
```
(`conf.sound.covoxProfi_vol`, `Unreal.ini:390` `CovoxProfiVol=6000`.) ZXMAK2 `CovoxProfi.cs.bak` (disabled) uses `PortR=0x3F`, `PortL=0x5F`, mask `0xFF`. XPECCY only stubs `1F..7F` as `dummyIn/Out` ("BB55", `profi.c:136`) and `8F/AF/CF/EF` "BB51/3"; XPECCY+ wires `soundrive = covox` at machine level.
So: stereo 8-bit DAC, left `#5F`, right `#3F` (8255 PA/PB at 1F..7F) when the FDC is NOT selected; when the FDC is selected the same 1F..7F ports go to the WD1793. In CP/M-extended mode the DAC moves to `87/A7/C7/E7`.
Not implemented anywhere: 8255 mode/control register, PC port.

### 2.6b Profi "system" ports 008B / 018B / 028B (KARABAS only)

KARABAS has clone-specific registers (`port_008b_reg`: extra ROM page bits, `onrom` forces DOS, `unlock_128` lets 128K ROM enter DOS via 3Dxx; `018B`, `028B` HDD type, FDD swap, DFFD lock, turbo).
Only reachable in ext mode. Not part of the original Profi; ignore for unreal-ng except as an explanation of some board quirks.

### 2.7 IDE (Profi IDE, "HDD")

Ports (UNREAL `io.cpp:344-375, 1189-1200`; UNREAL-SP `io.cpp:265-300`), active only in extended mode (`7FFD.4 && DFFD.5`):

```cpp
if ((p1 & 0x9F) == 0x8B && conf.ide_scheme == IDE_PROFI) {          // 8B AB CB EB
  if (p1 & 0x40) {                                                   // cs1  (CB, EB)
      if (!(p1 & 0x20)) { comp.ide_write = val; return; }            // write CB = high-byte latch
      port >>= 8; goto write_hdd; }                                  // write EB = register A10..A8 (data: low byte + latch)
  if (p1 & 0x20) { if (((port>>8)&7) == 6) hdd.write(8, val); return; }   // write AB (A6=0,A5=1): cs3 -> control/altstatus (reg 6)
}
// read: if (p1&0x40) { if (p1&0x20) return comp.ide_read; /* EB read = latched high byte */ port >>= 8; goto read_hdd; }
```
XPECCY `hdd.c:790-802` `ide_profi_decode`: write swaps A5 (`port ^= 0x20`); `iorq = (port&0xFF)==0xCB || (port&0x7FF)==0xEB`; `port == 0x06AB` -> ATA control register; `high = (port & 0x7FF)==0xEB`; reg = `(port>>8)&7`.
ZXMAK2 `IdeProfi.cs:55-56,133-190`: `SubscribeRd/WrIo(0x9F, 0x8B)`, only when `cpm && rom48` (mode check `IsExtendedMode`), same cs1/cs3 logic and `m_ide_write` latch.
KARABAS `karabas_pro.vhd:1419-1425`: `cs = A7=1 and A4:0=01011`, `wwc = write CB`, `wwe = write EB`, `rww = read CB`, `rwe = read EB`, `cs3fx = write AB with A10:8 = ?`.
Consensus: 16-bit data via a 8-bit high-byte latch; **write**: OUT `CB` stores the high byte, OUT `EB` (A10:8 = reg) completes the register write (for reg 0 = low data byte + latched high byte). **read**: IN `CB` (A10:8 = reg) reads the register (for reg 0 it reads the word and latches the high byte), IN `EB` returns the latched high byte. `AB` with A10:8=6 writes the device control register. UNREAL's write/read use different A5 polarity for the latch; XPECCY encodes this with the XOR.
Only `IDE_PROFI` scheme in `hdd`: master only in ZXMAK2/UNREAL; no LBA/CHS emulation details are Profi-specific.

### 2.8 RTC / CMOS

Chip: **DS12885 (MC146818-compatible)** (`ZXMAK2 CmosProfi.cs:12` `RtcChipType.DS12885`; KARABAS has `mc146818` bus device).
Ports (extended mode only): address latch `#BF` (write, A5=1) / `#FF`; data `#DF`, `#9F` (A5=0). Decode `(port & 0x9F) == 0x9F` with A5 choosing address vs. data:

```cpp
// UNREAL io.cpp write
if ((port & 0x9F) == 0x9F && conf.cmos) { if (port & 0x20) { comp.cmos_addr = val; return; } cmos_write(val); return; }
// read: only A5 = 0 -> cmos_read()
```
ZXMAK2 `CmosProfi.cs:34-35,72-98`: `SubscribeWrIo(0x9F, 0x9F)`, `(addr & 0x20) != 0 -> WriteAddr else WriteData`, read only when `A5=0`; active in `IsExtendedMode` (`CMR1.5 && CMR0.4`).
XPECCY `profi.c:127-128`: `{0xFF, 0xDF}` data (`prfInDS/prfOutDS`), `{0xFF, 0xBF}` address (`prfOutAS` -> `cmos_wr(CMOS_ADR)`), only in `!DOS ROM CPM` (extended).
KARABAS `karabas_pro.vhd:1408-1416`: `AS = FF or BF`, `DS = DF or 9F`, condition `ext`.
Consensus fully aligned: `BF/FF` = address, `DF/9F` = data. Aliases: UNREAL/ZXMAK2 alias to all 4 ports; XPECCY only `BF`, `DF`.

### 2.9 Other Profi ports

- `#F7` (XPECCY `profi.c:117` "f7 (off?)" dummy): printer-ish; ignore.
- `#EFF7`, `#1FFD`: not on real Profi (KARABAS adds them for Pentagon-compat).
- `#7FFD` read-back: none of UNREAL/XPECCY/ZXMAK2 implement it (KARABAS: `x"09"` returns 7FFD for debugging only).

---

## 3. Video

### 3.1 Hi-res 512x240, colour

Enabled by `DFFD.7` (DS80). Data:

| Item | Value | Where |
|---|---|---|
| Bitmap | page **4**, or **6** when `7FFD.3` (screen select) = 1 | UNREAL `drawers.cpp:560` `page_ram(p7FFD & 8 ? 6 : 4)`; ZXMAK2 `UlaProfi3XX.cs:185-189` `polek = videoPage==7 -> BW page 6/4`; XPECCY `video.c:882-887`; KARABAS `memory.vhd` `"00001" & VID_PAGE & '0'` |
| Colour attributes | page **0x38** (or **0x3A** when screen select = 1) | UNREAL `PAGE*0x34` from page 4/6 (i.e. 56 / 58); ZXMAK2 `m_pageClr = polek ? 0x3A : 0x38`; XPECCY `MADR(0x38\|0x3a)`; KARABAS `"01110" & VID_PAGE & '0'` |
| Layout of a bitmap byte | ZX-style third/row addressing: `pixCoff = 2048*(line>>6) + 256*(line&7) + ((line&0x38)<<2)` (ZXMAK2 `ProfiRenderer.cs:361-363`); the same offset is used in the attribute page (attr is per 8 pixels, per pixel line - "hi-colour", 240 x 64 attribute bytes) | ZXMAK2, UNREAL `drawers.cpp:558-573`, XPECCY `video.c:881` |
| Pixel order | for each 8 px cell: first byte at `+0x2000` (left 8 px), then byte at `+0x0000` (right 8 px); i.e. 32 bytes/line from each half. 4 hi-res pixels per CPU T-state, 512 px = 128 T | ZXMAK2 `:365-369` (`x4&2 == 0 -> +8192`), UNREAL `dxr_prof.cpp:1-50` (`s = src+0x2000` first), XPECCY `video.c:884-886` (`adr \|= 0x2000` for even half) |
| Attribute byte | `b7=paper bright, b6=ink bright, b5:3=paper colour, b2:0=ink colour` (no flash) -> 16-colour palette index `ink = (a&7)\|((a&0x40)>>3)`, `paper = ((a>>3)&7)\|((a&0x80)>>4)` | UNREAL `drawers.cpp:570-571`, ZXMAK2 `ProfiRenderer.cs:385-386`, XPECCY `video.c:889-890`; KARABAS `profi_video.vhd:188-198` (`i78 = attr(7)` for paper in DS80) |
| Monochrome fallback (Profi 3.xx, or `ProfiMonochrome=1`) | attribute page not used: ink/paper = `pFE&7` and `~pFE&7` (border colour) | UNREAL `conf.profi_monochrome` (`Unreal.ini:147`), ZXMAK2 `Profi32_*_BNW` (`ProfiRenderer.cs:109-144`, `UlaProfi3XX`); UNREAL `dxr_prof.cpp` |
| Border in hi-res | shown with the *inverted* index: ink=`pal[pFE&7]`, paper = `pal[~pFE&7]` (border uses the paper one) | ZXMAK2 `ProfiRenderer.cs:61-66,103-107`; XPECCY `profi.c:47-50` (`nextbrd ^= 7`) |
| Palette | 16 entries programmed through `xx7E` (2.2); startup palette `00 02 10 12 80 82 90 92 / 00 03 18 1B C0 C3 D8 DB` in `Gg0Rr0Bb` (ZXMAK2 5XX only) | ZXMAK2 |

In non-DS80 mode the machine behaves as a 128K Spectrum: screen page 5/7, standard 256x192, palette not used.
`DFFD.7` also gates CP/M-side hardware in KARABAS (ext ports need `DS80` for flash regs only).

### 3.2 Frame timing / geometry

Sources disagree; nothing here is confirmed against a real 3.5 MHz Profi.

| Source | Frame T | Line T | INT | First paper | Notes |
|---|---|---|---|---|---|
| UNREAL | 69888 | 224 | length 28 T (`PRESET.PROFI=69888,12580,224,50,28,0,0,0,0,0`, `Unreal.ini:139`, "thanks to DDp") | field 2 = 12580 (= 56*224+36) | hi-res raster `R_512_240 { 56, 296, 70, 198 }` (`draw.cpp:18`); no separate hi-res timing |
| ZXMAK2 3XX/5XX (normal mode) | 69888 | 224 | length **39** (marked "TODO needs approve") | first paper line 56, first paper tact **39** (`UlaProfi3XX.cs:133-147`); flash period 25 | uses 4T border stepping off |
| ZXMAK2 hi-res | array of 69888 T, but `c_ulaLineTime = 192`, first paper line 72, tact 24 | 192 | 39 | comment says "59904 for profi mode (312x192)" - inconsistent with the 69888 actually used | `ProfiRenderer.cs:238-254` |
| XPECCY | layout `ULA.Profi:448:312:64:56:64:16:64:72:64:256:192` (dots; 2 dots = 1 T) -> 224 T x 312 = 69888, INT 32 T, blank 32x16 | 224 | 32 T | `bord.x=64 dots` = 32 T, `bord.y=56` | XPECCY+ doc states 32 T is **open** and "UnrealSpeccy's preset says 12580" |
| KARABAS (hi-res, 12 MHz pixel clock) | 768 px x 312 lines = 64 us x 312 = 19.97 ms -> 224 T x 312 = 69888 T @ 3.5 MHz (50.08 Hz); optional 60 Hz mode (263 lines) | 224 | falls at `h > 656 (px), v = 257` (`profi_video.vhd:74-77,134`) = about 191 T into line 257 (counted from first paper pixel), INT rises again when `INTA` | first paper at (0,0) of the counters | not comparable 1:1 with the others; normal (non-DS80) mode uses a Pentagon-style generator (INT at `ver_cnt=29, row 7`, `pentagon_video.vhd:130-170`) |

Only two numbers are consistent: 224 T lines and 312 lines = 69888 T/frame at 3.5 MHz. First-paper offset is somewhere between 12544+32 and +39 T; INT length 28..39 T.

### 3.3 Contention

- UNREAL: none for Profi (no `ContentionModel` for `MM_PROFI`; preset `floatbus=0`).
- ZXMAK2: none in `UlaProfi3XX` (the base `UlaDeviceBase` contention hook is not enabled for hi-res; writes to the video pages in hi-res just call `UpdateState`, `:38-68`).
- XPECCY+: `docs/machines-reference.md:32`: Profi "0 (none)" contention (pattern 0).
- KARABAS `memory.vhd` (`contended` signal): the clone **does** contend when `DS80=0 and turbo=0` for accesses to the logical `#4000-#7FFF` window (`mux="01"`) and to even I/O ports; no contention in DS80 hi-res mode. This is the board's Pentagon-style stall, not documented original Profi behaviour.
- No emulator implements the ULA-wait for hi-res video reads on the CPU (video "steals" cycles) - the attribute/bitmap fetch is not modelled at all.

---

## 4. TR-DOS / DOS signal and ROM selection

ROM select at `#0000-#3FFF` (all sources):

| DOS latch | 7FFD.4 | DFFD.4 (NOROM/worom) | Mapped |
|---|---|---|---|
| on | 0 | 0 | page 0 SYS (service/boot menu/BIOS/STS) |
| on | 1 | 0 | page 1 DOS (TR-DOS) |
| off | 0 | 0 | page 2 128K editor |
| off | 1 | 0 | page 3 48K BASIC |
| any | any | 1 | RAM page 0 at `#0000` (writable, `bankw[0]` = RAM); 7FFD lock ignored |

UNREAL-SP `memory.cpp:29-40`: `if (CF_TRDOS) bank0 = (p7FFD&0x10) ? base_dos_rom : base_sys_rom; else bank0 = (p7FFD&0x10) ? base_sos_rom : base_128_rom;` then `DFFD.4` replaces `bank0` by RAM page 0.
XPECCY `profi.c:8-12`: `memSetBank(0x00, ROM, (flgDOS ? 0 : 2) | (flgROM ? 1 : 0))` (order EXT,DOS,128,48) or RAM 0 when `DFFD.4`.
KARABAS `memory.vhd`: `rom_page <= (not TRDOS) & ROM_BANK` (TRDOS = dos_act, ROM_BANK = 7FFD.4), `is_rom = A(15:14)="00" and WOROM='0'`.
ZXMAK2 `MemoryProfi1024.cs:103-156`: `SYSEN` (service latch, set by reset) forces SYS, `DOSEN` forces DOS **regardless of 7FFD.4** (differs; see 5), else `CMR0.4 ? SOS : 128`.

DOS latch (aka "DOS" signal, TR-DOS shadow):

| Event | UNREAL | ZXMAK2 | XPECCY | KARABAS |
|---|---|---|---|---|
| reset | `RM_SYS` (service) or as ini `RESET=`; DFFD.4 cleared (`memory.cpp:372`) | `SYSEN=true, DOSEN=false, CMR0=CMR1=0` (`:215-221`) | (reset routines in shared code) | `dos_act <= '1'` (`karabas_pro.vhd:1508`), i.e. boots into page 0 |
| set | in `step()`: `CF_SETDOSROM` armed when `7FFD.4=1 && trdos_present`, then `pch == 0x3D` -> `CF_TRDOS` (`z80_main.inl:115-121`) | M1 fetch `#3Dxx` when `IsRom48` (`BusReadMem3D00_M1`, `:195-201`) | `flgDOS` set by the shared 3Dxx logic | M1+MREQ, `A15:8 = 3D`, `(rom14=1 or unlock_128)`, `DFFD.4=0` (`:1596`) |
| clear | `pch & 0xC0` (PC >= #4000) at instruction fetch (`CF_LEAVEDOSADR`, Pentagon/Profi flavour) | any M1 read from #4000+ clears both `DOSEN` and `SYSEN` (`BusReadMemRamM1`) | shared | M1 with `A15:14 != 00`, or `DFFD.4=1` |
| NMI ("magic button") | not modelled for Profi | `NmiRq` rejected unless `IsRom48`; `NmiAck` sets `DOSEN` | not modelled | `cpu_nmi_n='0' and DS80='0' and DFFD.4='0'` sets `dos_act` |

`UNREAL doc/unreal_e.txt:565`: "Profi service ROM can work only when all TR-DOS delays are enabled" (i.e. the service ROM is sensitive to FDC timing).
TR-DOS traps for Profi: UNREAL runs `trdos_traps()` when in DOS mode; the traps are not Profi-specific.

Profi RAM banks at `#0000` in CP/M (DFFD.4) additionally clear the 128K lock: `7FFD` writes are allowed while `DFFD.4=1` (all four sources).

---

## 5. Disagreements and bugs

1. **SCR (DFFD.6) window `#8000`**: UNREAL, UNREAL-SP, ZXMAK2, KARABAS: `DFFD.6 = 1` -> page 6, unconditional. XPECCY: `(DFFD&0x40) && (7FFD&8) ? 6 : 2` (`profi.c:15`). The KARABAS header comment says "D3 of CMR0 must be 1", i.e. that is a *software* convention (page 6 is the shadow screen's bitmap partner), not a hardware gate. XPECCY is the outlier; follow the RTL.
2. **ROM select while DOS latch is on and 7FFD.4 = 0 vs 1**: UNREAL/XPECCY/KARABAS use `{dos, 7FFD.4}`; ZXMAK2 maps DOS ROM whenever `DOSEN` is on (ignores 7FFD.4) and has a separate `SYSEN`. Equivalent in normal use (DOSEN only sets while ROM48), differs if a program writes 7FFD.4=0 while in TR-DOS.
3. **Palette data width**: UNREAL/ZXMAK2 treat the palette byte as `Gg0Rr0Bb` (2 bits per channel, mid bits ignored); XPECCY reads `GGGRRRBB` (3-bit G and R, 2-bit B from `A15..A13, A12..A10, A9..A8`) ; KARABAS uses 3-bit G/R and a 3rd blue bit from D7 of the previously written FE value (`(not BUS_A) & BORDER(7)`). Hardware (Karabas) sides with 3+3+(2..3). UNREAL/ZXMAK2 lose one bit per channel.
4. **Palette index semantics**: UNREAL/ZXMAK2 (`pFE` before this write), XPECCY (`p7E` latched from previous write, updated on 7E only) and KARABAS (`BORDER` = last FE write) all use "colour from THIS write's A15:8, index from the PREVIOUS FE write value XOR 0xF". Equivalent, except that XPECCY only latches the index on `xx7E` writes whereas UNREAL/KARABAS latch it on every FE-family write (XPECCY misses a plain `OUT (#FE),n`).
5. **Palette port mask**: UNREAL/ZXMAK2 use A7=0 and A0=0 (`x?7E/x?6E/...`, any port with `!(port&0x80)` in the FE decode); XPECCY requires exactly `xx7E`; KARABAS `cs_xxfe and A7=0`. Prefer A7=0 & A0=0.
6. **Extended vs SYS mode ports**: KARABAS extended mode = `(CPM and ROM48) or (DOS and !ROM48)`; UNREAL/ZXMAK2/XPECCY only `CPM and ROM48`. In KARABAS the SYS ROM (boot menu / BIOS / HDD boot) therefore sees IDE, RTC, 83/A3/C3/E3 and `#3F`, whereas UNREAL/ZXMAK2 give the SYS ROM the normal `1F..7F/FF` ports. Two consequences worth a boot test: (a) UNREAL's "Profi service ROM ... HDD boot" can only work if the service ROM enters CP/M mode first; (b) ZXMAK2 has a `PROFI-HDDBOOT` ROM set but its IDE only answers with `CMR1.5 && CMR0.4`. Verify with `profi_v450.ROM` (Award BIOS page 0) whether it reaches the IDE from SYS mode.
7. **FDC "normal" decode width**: UNREAL `(p1&0x83)==0x03` aliases every port with A7=0, A1A0=11 (03,1B,23,...); ZXMAK2 `(0x9F, 0x1F)` demands A4..A0=11111; XPECCY same as ZXMAK2 (`0x9F`); KARABAS `A7=0, A1:0=11` (like UNREAL). System port: UNREAL `(p1&0xE3)==0xE3`; ZXMAK2 `(0x9F,0x9F)` (accepts 9F/BF/DF/FF); XPECCY exactly `FF`/`BF`/`3F`.
8. **NMI**: three different rules (section 4).
9. **DOS entry while DFFD.4 = 1**: KARABAS blocks entry (`dos_act` cleared) whenever RAM is at #0000; UNREAL/ZXMAK2 do not gate on DFFD.4 (UNREAL clears DFFD.4 only inside `SetROMMode`).
10. **ZXMAK2 hi-res timing** (`c_ulaLineTime = 192` with 69888 T/frame array, comment 59904) is internally inconsistent; do not copy. ZXMAK2 `#FE` mask `0x67` (`UlaProfi3XX.cs:72`) is also suspect (A7 and A3 ignored but A5,A6 must match?).
11. **IDE**: UNREAL applies the ide_read/ide_write latch and swaps A5 polarity between read and write exactly like XPECCY's `port ^= 0x20`; a naive `A5 = high byte` decode gives the wrong direction.
12. **AY mask**: UNREAL `0xC0FF/0xC0FD` is stricter than hardware (all others use A15/A14/A1); unreal-ng's current `0xC002` matches XPECCY/hardware.
13. **Reset value of 7FFD/DFFD**: ZXMAK2 `BusReset` zeros CMR0/CMR1 and turns `SYSEN` on; UNREAL keeps DFFD except bit 4 unless full reset (`memory.cpp:364-372` region, "no RAM/cache/SERVICE" comment); KARABAS resets `dffd/7ffd = 0` and `dos_act=1`.

Most trustworthy: **KARABAS RTL** for what the hardware does (page mapping, DOS latch, port qualification by CPM/ROM14/dos_act, IDE/RTC strobes, palette format, hi-res memory map), with the caveat that it is a clone (adds `fd_port`/`lock_dffd`, DivMMC, ZiFi, flash regs, ROM-page regs 008B/018B/028B and a Pentagon-style video generator for the normal mode). Second: **ZXMAK2** for a clean model of the original ("modified ports" naming, IDE, CMOS, palette start-up table, per-device separation) but with timing errors. **UNREAL/UNREAL-SP** for behaviour that has been validated by demos (mapping, lock logic, palette, hi-res drawing) and the only complete implementation, but with Gg0Rr0Bb palette loss and loose port aliasing. **XPECCY** is compact and matches on ports but has the SCR/7FFD.3 deviation and partial palette latching.

---

## 6. Consensus table

`+` agrees; `!` sources disagree (see number in section 5).

| Port / bit | Decode (reference choice) | Meaning | Agreement |
|---|---|---|---|
| #7FFD | A15=0, A1=0 (A2 not constrained) | b2:0 RAM low, b3 screen 5/7, b4 ROM48 (1 = 48K/DOS side), b5 lock, b7:6 ignored on 1M | `+` (all). unreal-ng stub adds A2=1 |
| lock rule | write ignored when 7FFD.5=1 unless DFFD.4=1 | all bits of 7FFD incl. screen | `+` |
| #DFFD | A15=1, A13=0, A1=0 (must not fire for A15=0) | see next rows | `+` mask; KARABAS full 16-bit |
| DFFD.2:0 | RAM high bits -> page = (DFFD&7)<<3 \| (7FFD&7); 512K variant uses only DFFD.1:0 | | `+` |
| DFFD.3 (SCO) | 0: `#4000`=page 5, `#C000`=RAM page N; 1: `#4000`=page N, `#C000`=page 7 | | `+` |
| DFFD.4 (NOROM/worom) | RAM page 0 at `#0000`, DOS latch off, 7FFD lock disabled | | `+` (KARABAS also blocks DOS entry, `!` 9) |
| DFFD.5 (CPM) | selects CP/M port set; with 7FFD.4=1 -> extended ports | | `+` |
| DFFD.6 (SCR) | `#8000` = page 6 (else 2) | | `!` 1 (XPECCY needs 7FFD.3) |
| DFFD.7 (DS80) | hi-res 512x240 + palette write enable | | `+` |
| ROM index | `{dos_latch ? 0 : 2} \| 7FFD.4`; 0=SYS 1=DOS 2=128 3=48 | | `+` (ZXMAK2 `!` 2) |
| Hi-res pages | bitmap 4 (6 if screen bit), attr 0x38 (0x3A) | | `+` |
| Hi-res attr | b7 papBR, b6 inkBR, b5:3 paper, b2:0 ink | | `+` |
| Palette write | `OUT (xx7E)` A7=0,A0=0, DFFD.7=1; colour = ~A15:A8; index = ~(previous FE write) & 15 | | `+` semantic, `!` 3,4,5 |
| #FE | A0=0; beeper b4, MIC b3, border b2:0; ignored while DOS ports active? | | `!` (ZXMAK2 masks; UNREAL/XPECCY no) |
| Normal FDC | `1F,3F,5F,7F` (A7=0, A1:0=11, A6:5=reg) when DOS latch on (and CPM=0, or CPM=1 with 7FFD.4=0) | | `+` (`!` 7 width) |
| Normal sys | `#FF` (CPM=0) or `#BF` (CPM=1, 7FFD.4=0) | WD FF port (drive select/side/density; not covered by any emulator beyond delegating to the FDC) | `+` |
| Ext FDC | `83,A3,C3,E3` -> regs `1F,3F,5F,7F`; sys `#3F` | 7FFD.4=1 and DFFD.5=1 | `+` (KARABAS also SYS ROM `!` 6) |
| RTC | AS `#BF/#FF`, DS `#DF/#9F` (ext only); DS12885 | | `+` |
| IDE | `8B,AB,CB,EB` (A7=1, A4:0=01011), reg = A10:8, high-byte latch, ext only | | `+` |
| Covox | DAC L=`#5F`, R=`#3F` (normal, DOS latch off); `87/A7/C7/E7` in ext mode (UNREAL) | | `+` (ext-mode location only UNREAL) |
| AY | `#FFFD` reg select, `#BFFD` data: A15=1, A14 select, A1=0 | | `!` 12 (UNREAL low byte) |
| Kempston joy | `#1F`, DOS off, CPM=0 | | `+` |
| Kempston mouse | `#FBDF/#FFDF/#FADF`, CPM=0 | | `+` |
| INT / frame | 224 T x 312 = 69888, no contention (KARABAS differs) | INT length 28/32/36?/39 T | `!` 3.2 |

---

## 7. Limits (features missing per emulator)

| Feature | UNREAL | ZXMAK2 | XPECCY | KARABAS |
|---|---|---|---|---|
| colour hi-res / palette | yes (2-bit) | yes only `UlaProfi5XX`; `3XX` mono | yes (3-bit) | yes |
| FE bit 7 read (5.xx "GX0") | no | yes (5XX) | no | yes |
| hi-res video read contention / ULA wait | no | no | no | no |
| memory contention (Profi) | no | no | no | partial (clone) |
| RTC | yes (`conf.cmos`, needs CMOS ini) | yes (DS12885 file persisted) | yes | yes |
| IDE | yes (`IDE_PROFI`) | yes (single master, `vmide` file) | yes | yes (+Nemo) |
| Covox | yes (vol setting) | disabled `.bak` | stubs | (DAC only) |
| TurboSound / 2nd AY | no | no | no | yes (clone) |
| NMI / magic button | partial | yes (NMI -> DOSEN) | no | yes |
| keyboard ext keys | no | `KeyboardProfi.cs` | yes (`extMap`) | via PS/2 |
| Profi 512K | ramsize | `MemoryProfi512` | `MEM_512K` | - |
| Profi 3.xx vs 5.xx distinction | mono flag only | yes (two ULA classes) | no | 5.xx |
| 60 Hz mode | no | no | no | yes (clone) |
| Frame/INT timing verified | preset "thanks to DDp" | TODOs | XPECCY+ says open | video-timing generator only |
| RTC persistence / time source | file | file | file | AVR RTC |

---

## 8. Cross-check of `core/src/emulator/ports/models/portdecoder_profi.cpp`

Findings (file:line refer to that file):

1. **ROM polarity inverted (`:239,246`)**: `isROM0 = value & 0x10; SetROMMode(isROM0 ? RM_128 : RM_SOS)`. All references: 7FFD.4 = 1 selects 48K (SOS/DOS side), 0 = 128K (SYS side). The `reset()` comment (`:26`) "Bit 4: 0 = SOS, 1 = 128K" and the class header repeat the same mistake; `modelsregression_test.cpp` goldens (`kGoldenRows_Profi`) freeze it and must be corrected together. `SetROMMode(RM_128)` also *clears* `state.p7FFD.4`, so the state latch and the bank contradict each other.
2. **`state.p7FFD` is never written** in `Port_7FFD` (`:222-255`); the memory mapper (`UpdateZ80Banks`) takes the ROM bit from it. Other models set it (Spectrum128 does). `_screen->SetActiveScreen` is applied even when the lock is set (`:251-252`): all references drop the entire write, including the screen bit, when locked.
3. **Lock semantics**: `_7FFD_Locked` is only cleared on reset; per hardware, `DFFD.4 = 1` ignores the lock (`io.cpp` UNREAL, KARABAS `:1539`). Also the lock must be tested against the *current* 7FFD.5 bit (a latch is equivalent) but it must respect DFFD.4.
4. **`IsPort_7FFD` (`:180-198`)**: mask `A15, A2, A1` (match A2=1). Every reference decodes only `A15=0, A1=0`. The `A2=1` extra term was added to avoid SoundRive F1/F9; it breaks legitimate writes such as `OUT (C),r` with `BC=#7FF9`-style ports (rare) and, more importantly, contradicts hardware/other emulators. Prefer `A15=0, A1=0`, and resolve the Covox conflict on the covox side (covox ports are `#3F/#5F`, A2=1 as well, so A2 never separates them anyway).
5. **`IsPort_DFFD` (`:200-217`)** mask `A13=0, A1=0`: without `A15=1` it overlaps 7FFD-class ports (`#5FFD`, `#1FFD`, `#0FFD` all satisfy both, `DecodePortOut :126-142` fires both handlers - there is no `else`). Required: A15=1, A13=0, A1=0 (UNREAL's order does this by structure; ZXMAK2 by `handled`). Hardware (KARABAS) even uses the full `#DFFD`.
6. **`Port_DFFD` is a stub (`:259-273`)** - pDFFD never written, so `Screen::DetectModeProfi` (screen.cpp:420-428) never selects hi-res, `SetROMMode` (memory.cpp:764) clears an always-0 bit, and no bank calculation exists: `UpdateZ80Banks` has no `MM_PROFI` branch (the audit's section 2 gives the formula; use the mapper snippets in 2.1). It must also: trigger `set_banks`, `update_screen()/init_raster()` on DS80 change, TTD serializer as per the comment.
7. **`reset()` (`:22-55`)** boots the 48K ROM (`RM_SOS`) and maps `bank3 = 0`; hardware/references boot into the SYS ROM (DOS latch on, 7FFD.4=0, page 0 = boot menu/BIOS): use `RM_SYS` (which sets `CF_TRDOS` and clears 7FFD.4) and zero `p7FFD/pDFFD` (except keep nothing). `pBFFD/pFFFD` names in reset are the AY data/address latches (not ports state); harmless.
8. **`DecodePortOut` has no #FE handler** (beeper/MIC/border, palette write `A7=0 & DFFD.7`); `DecodePortIn` does route FE via `IsFEPort`/`Default_Port_FE_In` (`:86`). Add `Default_Port_FE_Out` (see Spectrum128 `:171-173`) plus the palette write.
9. **`DecodePortIn` has no Profi-specific inputs**: no Beta Disk gating (`IsBeta128Port && !CF_TRDOS` pattern of Pentagon128, and the 1F/3F/5F/7F/FF, BF, 83/A3/C3/E3/3F port sets in 2.4), no RTC (`BF/FF/DF/9F`), no IDE (`8B/AB/CB/EB`), no Covox (`3F/5F`, `87/A7/C7/E7`), joystick/mouse must be masked when `DFFD.5=1`.
10. AY decode (`:75,81,146,155`) `(port & 0xC002) == 0xC000 / 0x8000` matches the hardware-style decode (XPECCY/KARABAS) - fine; note `#BFFD` on write is the data port and `#FFFD` select, consistent.
11. Model prerequisites outside the decoder (from the existing audit): `configs/profi/unreal.ini` missing (blocks creation), ROM roles per section 1.1 are right, `DrawProfi` is a no-op, no 512-wide geometry row.

Suggested minimal correct order of work: fix 7FFD (polarity, state, lock incl. DFFD.4) -> real `Port_DFFD` + Profi bank calculation in the mapper -> reset to SYS ROM -> DOS latch entry/exit (3Dxx / PC>=4000; block while DFFD.4=1; `CF_LEAVEDOSADR`) -> port sets for FDC/RTC/IDE/Covox by mode -> palette port -> hi-res renderer (bitmap page 4/6 + attr 0x38/0x3A) -> timing constants (69888/224 T, INT 32 T as XPECCY+ documented default, flagged open).
