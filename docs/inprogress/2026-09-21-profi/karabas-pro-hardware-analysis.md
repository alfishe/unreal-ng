# Karabas-Pro FPGA Analysis: ZX Profi Hardware Behaviour

Source: `scratch/profi/kp` (MIT, commit c210d6c "Added support to AY/YM detection via regs access").
All VHDL paths below are relative to `firmware/src/fpga/profi/rtl/`; `TOP` = `karabas_pro.vhd`,
`MEM` = `memory/memory.vhd`, `VID` = `video/video.vhd`, `PV` = `video/profi_video.vhd`,
`PENT` = `video/pentagon_video.vhd`.

Legend: **[PROFI]** = original Profi 1024 behaviour (as implemented by this core),
**[KP]** = Karabas-Pro extension (not on a real Profi), **[?]** = my inference or unclear.

Important scoping fact: the WD1793 FDC, the HDD (Profi/Nemo IDE) bus buffers and the
`#FF`-port latch live in an **external CPLD** (`build_cpld.sh`, not in this repo). The FPGA
only decodes chip-select strobes and forwards them (`storage/bus_port.vhd`). There is no WD1793
model in the sources. Emulator FDC behaviour has to come from WD1793 datasheet plus the
Beta-disk `#FF` semantics, not from this repo.

---------------------------------------------------------------------------------------------

## 0. Global structure

* Clocks: 50 MHz -> PLL 112 MHz -> 84/72/28/24/8 MHz (`TOP:565-581`).
  `clk_bus` = 28 MHz normally, **24 MHz when `ds80`=1** (Profi hires mode) (`TOP:584-590`).
  CPU clock is a *gated pulse train* derived from `clk_bus` (`TOP:1206-1212`).
* CPU: T80a (Z80), `CEN='1'`, `WAIT_n='1'` always (`TOP:1200`); wait states are made by
  gating `clk_cpu` instead.
* All hardware registers are cleared by `reset` (`TOP:1495-1512`). `reset = areset or kb_reset or
  loader_reset or loader_act or board_reset` (`TOP:1191`).
* Sound path etc. are in section 7.

### Mode-qualifier signals (the key to Profi port decoding)

| Signal | Source | Meaning |
|---|---|---|
| `rom14` | `7FFD` bit 4 (`TOP:1373`) | Profi "ROM14" line: selects 48/128 half of ROM (0 = 128/SYS, 1 = 48/DOS) |
| `cpm` | `DFFD` bit 5 (`TOP:1374`) | CP/M mode: blocks TR-DOS-from-ROM, enables extended-periph ports from RAM (rom14=0) or "modified" ports (rom14=1) |
| `worom` | `DFFD` bit 4 (`TOP:1375`) | ROM off, RAM seg 0 at 0000; also unlocks 7FFD |
| `ds80` | `DFFD` bit 7 (`TOP:1376`) | Profi 512x240 hi-res mode ("DS80") |
| `scr` | `DFFD` bit 6 (`TOP:1377`) | 8000-BFFF shows page 6 instead of 2 |
| `sco` | `DFFD` bit 3 (`TOP:1378`) | Window swap: 4000-7FFF <-> C000-FFFF page roles |
| `dos_act` | internal flag `DOS` (`TOP:1508, 1595-1598`) | TR-DOS/"DOS" active (traps 3Dxx) |

The "extended peripherals" qualifier used by many ports:

```
EXT = (cpm='1' and rom14='1') or (dos_act='1' and rom14='0')
```
(`TOP:1333, 1345, 1408, 1413, 1420-1425, 1464, 1472`). I.e. extended ports appear either in
CP/M-with-DOS-ROM state, or while the SYS ROM (page 0) is paged in.

---------------------------------------------------------------------------------------------

## 1. I/O port decode table

Decode is done on the CPU bus in `TOP` (lines 1316-1490) and on the read mux `selector`
(`TOP:1815-1842`). `M1_n=1` is required on most (i.e. not an interrupt-ack cycle).

### 1.1 Full priority-ordered READ mux (`TOP:1815-1842`, data `TOP:1783-1813`)

If nothing matches the CPU sees `#FF`. Top-to-bottom priority:

| Prio | Cond (all need IORQ=0, RD=0 unless RAM) | Data returned |
|---|---|---|
| 1 | `ram_oe_n=0` (memory read) | RAM/ROM byte |
| 2 | `cs_rtc_ds` (A7:0 = #DF or #9F and EXT) | MC146818 RAM byte at last AS latch |
| 3 | `cs_xxfe` (see 1.2) | `GX0 & not TAPE_IN & KB[5:0]` = D7=palette-present flag, D6=EAR (inverted TAPE_IN), D5..0=keys |
| 4 | `nemo_ebl_n=0` | CPLD bus (Nemo IDE) |
| 5 | A7:0=`#57` (or `#EB` when `cpm=0` and DivMMC on) and `is_flash_not_sd=0` | SPI shift-in byte (Z-Controller / DivMMC data) |
| 6 | A7:0=`#77`, `is_flash_not_sd=0` | constant `#FC` (`"11111100"`) (Z-Controller status) |
| 7 | A7:0=`#1F`, `dos_act=0`, `cpm=0`, `joy_mode="000"` | Kempston joystick `joy_bus` |
| 8/9 | `cs_fffd` and `ssg_sel`=0/1 | AY chip 0 / chip 1 data |
| 10 | `cs_dffd` | `DFFD` register |
| 11 | `cs_7ffd` | `7FFD` register |
| 12-14 | A15:0 = `#FADF/#FBDF/#FFDF`, `ms_present`, `cpm=0` | Kempston mouse buttons / X / Y |
| 15-17 | ZX-UNO UART2 / UNO addr reg / UNO UART | see 1.9 |
| 18 | `serial_ms_oe_n=0` | Serial-mouse VV51 / hw-int vector |
| 19 | `cs_xxC7` | flash status |
| 20 | `cs_xxE7` | flash data |
| 21-23 | `cs_008b`, `cs_018b`, `cs_028b` | readback of those registers |
| 24 | `zifi_oe_n=0` | ZiFi |
| 25 | `vid_pff_cs` and A7:0=`#FF` and `dos_act=0`, `cpm=0`, `ds80=0` | video attribute latch (floating-bus like port `#FF`) |
| 26 | any other IORQ read with M1=1 | `cpld_do` = CPLD bus `SD[15:8]` (FDC/HDD data, else whatever the CPLD drives) |

### 1.2 `#FE` (ULA port) [PROFI base]

* `cs_xxfe` (`TOP:1387-1388`): IORQ=0 and `A0=0` (any even port). When Nemo IDE enabled
  (`nemoide_en`=1, [KP]) it is narrowed to `A6:0 = 1111110` (so `#7E`/`#FE` only), to free the
  even ports for IDE.
* Write: `port_xxfe_reg <= D` latched on the rising edge of `WR_n` (`TOP:1417`).
  Bits: D0-D2 border colour (standard mode), D3 MIC/TAPE_OUT (`TOP:1674`), D4 speaker/EAR
  out (`TOP:1606`), D7 = LSB of palette blue (see 1.3 palette), D3:0 in hires = palette index /
  border (see section 3).
* Read: bits 4:0 keyboard matrix (rows selected by A15..A8 low, `avr/avr.vhd:311-356`),
  bit 5 = 6th key column (`KB(5) = not kb_data(40)`, `avr.vhd:356`), bit 6 = `not TAPE_IN`,
  bit 7 = `GX0` = `palette(idx)(6) xor palette(idx)(0)` in DS80 else `1` (`VID:219`) - a
  "palette exists" detector used by Profi software.

### 1.3 Palette port `#7E` (DS80 only) [PROFI]

* `cs_xx7e = cs_xxfe and A7=0` (`TOP:1389`), i.e. any even port with A7=0 (e.g. `#7E`).
* `palette_wr` = `cs_xx7e and WR_n=0 and ds80=1` (`VID:213`).
* On write (`VID:205-208`): `palette[ (BORDER[3:0]) xor #F ] <= (not A15..A8) & BORDER[7]`.
  `BORDER` = the **previous** `#FE` write (the register is only updated at the end of the write
  cycle, `TOP:1417`). So software does:
  1. `OUT (#FE), idx_byte` with D3:0 = **inverted** colour index (YGRB inverted), D7 = blue LSB;
  2. `OUT (#xx7E), any` with **A15:A8 = inverted GGGRRRBB** (address high byte carries the
     colour).
  Palette entry = 9 bits `GGG RRR BBB` (`VID:62-65`, output mapping `VID:231-233`).
* Palette index when displaying = `Y G R B` = `{bright, attr[G], attr[R], attr[B]}`
  (`VID:212`, colour bits in `PV:198-203`).
* Default (also restored on reset, `VID:199-203`): index 0-7 `B=4`; `G=4`; `R=4` per bit
  (level 4/7), index 8-15 same but level 6/7, index 8 = black.
  ```
  0:000 1:B4 2:R4 3:R4B4 4:G4 5:G4B4 6:G4R4 7:G4R4B4
  8:000 9:B6 10:R6 11:R6B6 12:G6 13:G6B6 14:G6R6 15:G6R6B6   (3-bit channels, max 7)
  ```
* The palette is applied in both modes for display, but can only be *written* in DS80.

### 1.4 Memory paging ports

Common gate: `fd_port='1'` (see below) for `#7FFD`, `#DFFD`, `#FFFD`, `#1FFD` full-address decode.

| Port | Decode (`TOP`) | Access | Notes |
|---|---|---|---|
| `#7FFD` (full) | `A15:0 = 7FFD`, IORQ, M1=1, `fd_port` (1393) | W: bits 7:6 only (1543-1545) | R returns whole reg (1826) |
| short `#xxFD` [PROFI] | `A15=0 and A1=0` (non-Nemo) or `A15=0 and A7:0 = #FD` (Nemo on) (1396-1397); **no `fd_port` qualifier** | W: bits 5:0 of 7FFD (1539-1541) | e.g. also matches `#1FFD`, `#3FFD`, `#7FFD`, `#0001`-style ports with A1=0 |
| `#DFFD` | `A15:0 = DFFD`, `fd_port`, and `lock_dffd=0` (1392) | R/W 8 bits (1534-1536, 1825) | lock from `#028B` bit 7 |
| `#1FFD` | `A15:0 = 1FFD`, `fd_port` (1394) | W 8 bits, **no effect on memory** (only commented alt use `TOP:1383`) | stored in `port_1ffd_reg`, not readable |
| `#EFF7` | `A15:0 = EFF7` (1390) | W stored in `port_eff7_reg`, **unused, not readable** | Pentagon 1024 legacy |

**7FFD write lock:** writes to 7FFD (both the short-port and the full-port branches) succeed only
if `(port_7ffd_reg(5)='0' or port_dffd_reg(4)='1')` (`TOP:1539, 1543`). i.e. bit 5 latches lock; DFFD.4
(`worom`) overrides the lock. Consequence: bits 7:6 have the same lock as bits 5:0. Note the split:
`OUT` to short `#xxFD` never changes bits 7:6 (only exact `#7FFD` does).

**7FFD bits** (`TOP:1540, 1544, 1373, MEM:667`): D0-D2 RAM bank (low 3 bits), D3 screen page
select (`VID_PAGE`), D4 `rom14`, D5 lock, D6-D7 extended RAM bits (`ram_ext(4:3)`; used only in
6 MB mode, see section 2).

**DFFD bits** (`TOP:1374-1384`, `MEM:231-260`): D0-D2 RAM high bits (bits 5:3 of 1024K page),
D3 `sco`, D4 `worom`, D5 `cpm`, D6 `scr`, D7 `ds80`. (Monochrome / shadow-screen bits: none
found. Video is chosen only by `ds80` and by AVR `SCREEN_MODE`.)

**`fd_port` "#FD correction" (`TOP:1316-1330`)**: `fd_sel='0'` when the data bus (either
direction) contains `1101 x 011` (i.e. `#D3` = `OUT (n),A` opcode / `#DB` = `IN A,(n)`
opcode). `fd_port` is latched at the rising edge of `M1_n` from the byte on the data bus during
the opcode fetch (cpu_di_bus during M1, hence the opcode). Effect: **after an `OUT (n),A` or
`IN A,(n)` instruction, the full-address ports `7FFD/DFFD/FFFD/1FFD` are not decoded**
(because A15:A8 is then just the accumulator); programs must use `OUT (C),r` form (`BC=#7FFD` etc.)
or the short `#xxFD` port. `fd_port` set to 1 on reset. `cs_xxfd` is not gated (short port
still works).

### 1.5 Profi extension registers (`#008B`, `#018B`, `#028B`) [PROFI ports, KP semantics]

Full 16-bit address decode, IORQ=0, M1=1 (`TOP:1333, 1345, 1357`). `#008B` and `#018B` need `EXT`;
`#028B` does not.

| Port | Bit | Meaning | Used? |
|---|---|---|---|
| `#008B` | 0 | select ROM ext bank 1 (PQ-DOS BIOS): `ext_rom_bank_pq = "01" if bit0 else ext_rom_bank` (`TOP:1371`) | yes |
| | 1-5 | "ROM64K page bits" | **declared, unused** (`TOP:1336-1340`) |
| | 6 (`onrom`) | force `dos_act`=1 permanently (`TOP:1597`) | yes |
| | 7 (`unlock_128`) | allow TR-DOS trap at `3Dxx` when `rom14=0` too (`TOP:1596`) | yes |
| `#018B` | 7:0 | "RAM page bits" | **declared, unused** (`TOP:1347-1354`) |
| `#028B` | 0 | `hdd_off` (disables Profi HDD ports) | yes |
| | 1 | HDD type Profi/Nemo | declared, unused |
| | 2 | `turbo_fdc_off = not bit2 and soft_sw(5)`: gates `FDC_STEP` to CPLD ("turbo FDC") | [KP] |
| | 3 | FDD swap (`or soft_sw(10)`): swaps drive-select bits 0/1 in the `#FF` write (`bus_port.vhd:68-83`) | [KP] |
| | 4 | `sound_off` (mutes DAC mix, `TOP:1627`) | [KP] |
| | 6:5 | **turbo mode** 00=3.5 MHz,01=7,10=14 (11 disabled by `max_turbo`); AVR key changes overwrite these bits (`TOP:1590-1593`) | [KP] |
| | 7 | `lock_dffd`: makes `#DFFD` writes and reads disappear | [KP-ish] |

### 1.6 Beta-Disk / Profi FDC ports (CPLD) [PROFI + KP]

`fdd_cs_n` (WD1793 CS strobe) and `fdd_cs_pff_n` (`#FF`-system port strobe) (`TOP:1463-1473`).
Register selection inside the CPLD uses `A10:A8, A6:A5` (`TOP:976`, only A6:A5 matter for the WD1793
register). Table by mode; all need IORQ=0 (the RD/WR direction goes to the CPLD).

| Mode (dos_act, cpm, rom14) | WD1793 regs (`fdd_cs_n=0`) | System/`#FF` strobe (`fdd_cs_pff_n=0`) |
|---|---|---|
| **TR-DOS classic**: dos=1, cpm=0, rom14=1 (`RT_F1_2`, `RT_F2_3`) | `A7=0, A1:0=11` -> `#1F,#3F,#5F,#7F` (A6:5 select reg: 00 cmd/status, 01 track, 10 sector, 11 data) | `A7:5=111, A1:0=11` -> `#FF` (and aliases `#E3,#E7,#EB,#EF,#F3,#F7,#FB` since A4:2 are ignored) |
| **CP/M via RAM**: cpm=1, dos=0, rom14=0 (`RT_F1_1`, `RT_F2_2`) | `A7=0, A1:0=11` (`#1F..#7F`) | `A7:5=101, A1:0=11` (`#A3,#A7,#AB,#AF,#B3,#B7,#BB,#BF`) |
| **Profi native** (EXT): (cpm=1 & rom14=1) or (dos=1 & rom14=0) (`P0`, `RT_F2_1`) | `P0`: `A7=1, A4:0=00011` -> `#83,#A3,#C3,#E3` (A6:5 select reg) | `RT_F2_1`: `A7:5=001, A1:0=11` -> `#23..#3F` family (`#3F`) |

Notes:
* In classic TR-DOS mode the system port is `#FF` (bits: drive select 1:0, /side, /reset, HLT, density -
  standard Beta-disk semantic, done inside the CPLD; **not defined in this repo** [?]).
* The FDC "turbo/slow-down" wait: after any FDC access (`fdd_cs_n=0` and RD or WR) `fdd_cnt`
  is reset to 0 and counts on `ena_div4` while `<=#7F`; `fdd_wait=fdd_cnt(7)`; while `fdd_wait=0`
  the CPU turbo is forced back to 3.5 MHz (`TOP:1475-1490, 1207-1212`).
  (~128 ticks of the 7 MHz enable = ~18 us in normal-clock mode).
* Port `#FF` **read** (`vid_pff_cs`, `x"17"`): only when `dos_act=0, cpm=0, ds80=0` and the
  ULA is in paper area returns the current attribute (`vid_attr`) = classic floating-bus/Beta-`#FF`
  substitute (`TOP:1840`).
* The Beta-Disk hidden-in-TR-DOS "ports at 1F/3F/5F/7F/FF exist only when DOS active" is exactly
  `dos_act=1, rom14=1, cpm=0`; **with `dos_act=0`, `#1F` is Kempston joystick** (row 7 above).

### 1.7 HDD (Profi HDD via CPLD) [PROFI], ports need `EXT` and `hdd_off=0` (`TOP:1420-1426`)

| Port (A7:0) | Direction | Strobe |
|---|---|---|
| `A7=1, A4:0=01011` (`#8B,#AB,#CB,#EB`, `#8B` family) | any | `hdd_profi_ebl_n` (HDD enable) |
| `#CB` | W | `hdd_wwc_n` (write high byte from CPU to "write register") |
| `#EB` | W | `hdd_wwe_n` (high byte to HDD bus) |
| `#CB` | R | `hdd_rww_n` (low-byte buffer direction) |
| `#EB` | R | `hdd_rwe_n` (read high byte register) |
| `#AB` | W | `hdd_cs3fx_n` (CS3FX = control block select) |

The IDE itself is in the CPLD; data path is via `SD[15:0]`/`SA`. Nemo IDE ports (`#F0,D0,B0,90,70,50,30,10,C8,11`;
`TOP:1441-1459`) only when `cpm=0` and `nemoide_en` (from AVR, [KP]); Nemo I/O strobes use `A2:0` (000 data
low/`001` high). Note the collision `#EB` vs DivMMC SPI (`cpm=0`, DivMMC on) - both decode, i.e. `#EB`
with `cpm=0` is DivMMC data port (`TOP:1687`).

### 1.8 Z-Controller SD and DivMMC SD [KP]

| Port | Decode | Behaviour |
|---|---|---|
| `#57` (data) | A7:0=`57`, IORQ, M1=1, `is_flash_not_sd=0`, loader done | W: shift out; R: shift-in byte. SPI mode 0, 8 CLK per byte (`sd/zc_spi.vhd`), `SCK = CLC and not COUNTER(3)` |
| `#77` (control) | A7:0=`77` | W: `zc_cs_n <= D1`. R: `#FC` constant |
| `#EB` (DivMMC data) | A7:0=`EB`, `cpm=0`, `divmmc_en` | same SPI engine |
| `#E7` (DivMMC CS) | A7:0=`E7`, `divmmc_en` (blocked when `ext_rom_bank="10"` i.e. FlashTool bank) | W: `zc_cs_n <= D0` (`TOP:1704-1708`) |
| `#E3` (DivMMC control) | A7:0=`E3`, `cpm=0`, `divmmc_en`, WR | `port_e3_reg <= D7 & (E3[6] or D6) & D5:0` (bit6 sticky, `TOP:1519-1521`) |

`#E3` bits: 7 CONMEM, 6 MAPRAM (sticky), 5:0 bank (5:1 -> 16K page number within the 512K DivMMC RAM,
0 -> 8K half; see 2.6).
The SD SPI bus is shared with SPI flash (`is_flash_not_sd` = `#xxC7` bit 2, `TOP:1266-1274`).

### 1.9 Sound ports

* **AY-3-8912/YM2149 x2 "TurboSound"** [KP TurboSound / Profi has none; Pentagon-style decode] (`sound/turbosound/turbosound.vhd:68-84`):
  * `bdir = M1=1 and IORQ=0 and WR=0 and A15=1 and A1=0`
  * `bc1  = M1=1 and IORQ=0 and A15=1 and A14=1 and A1=0` (**also on reads**)
  * Hence `#FFFD` (register select / read) and `#BFFD` (data write) are matched by a loose mask:
    `A15=1, A1=0` (data), `A15=A14=1, A1=0` (select).
  * Chip select: writing `#FF` or `#FE` (`D7:1 = 1111111`) to the register-select port sets `ssg <= D0`
    (`turbosound.vhd:80-82`); reset `ssg=0`. `ssg=0` -> chip 0, `ssg=1` -> chip 1
    (`A8 => not ssg` / `A8 => ssg`; only the addressed chip accepts address/data writes,
    `ym2149.sv:87`). Reads (`cs_fffd`) return the selected chip's register (`TOP:1823-1824`).
    `cs_fffd` additionally requires exact `A15:0=#FFFD` and `fd_port='1'` (`TOP:1391`).
  * AY clock enable `ena_div16` = 28/16 = 1.75 MHz; YM/AY model choice `I_MODE = soft_sw(8)` (read
    of an unimplemented register returns `addr & 0F` for AY mode else `FF`, `ym2149.sv:101,120`).
  * Stereo: ACB (soft_sw(7)=0) or ABC; mono mix via soft_sw(9) (`TOP:1610-1671`).
* **Covox / SoundRive** [PROFI has Covox-like at #FB; SoundRive is KP-extra] (`sound/covox/covox.vhd:54-63`):
  enabled by `soft_sw(6)`; **writes only when `dos_act=0 and cpm=0`**, IORQ=0, WR=0,
  address is the **low byte only**: `#0F -> A`, `#1F -> B`, `#3F -> B`, `#4F -> C`, `#5F -> D`,
  `#FB -> "FB" channel (mono, added to both L and R)`. 8-bit unsigned, held until overwritten,
  cleared on reset or when disabled. (Port `#1F/#3F/#5F` therefore double as Covox outputs when DOS off; Kempston `#1F` read
  is unaffected.)
* **SAA1099** [KP, build option `enable_saa1099`]: write A7:0=`#FF`, `dos_act=0`, `a0 = A8` (0=data, 1=address)
  (`TOP:876, 1677`). Only on EP4CE10 builds.
* **Beeper**: `#FE` D4 -> `BUZZER` and mixed at weight `2^11`; MIC D3 to `TAPE_OUT` (`TOP:1606-1608,1674`).
  Tape IN mixing into audio is commented out.

### 1.10 Kempston joystick and mouse

* Joystick `#1F` read: A7:0 = `1F`, `dos_act=0`, `cpm=0`, `joy_mode=000` [KP: AVR-driven]. Bits
  (`avr.vhd:260-268`): D0 right, D1 left, D2 down, D3 up, D4 fire, D5 fire2, D6 A, D7 B; **active-high**.
  Other `joy_mode` (Sinclair 1/2, cursor, QAOP) are implemented by the AVR by injecting keyboard
  bits [KP].
* Kempston mouse [KP-ish]: reads only when the AVR reports a mouse (`ms_present`) and `cpm=0`
  (`TOP:1827-1829`, data `TOP:1795-1797`):
  * `#FADF` buttons/wheel: `D7:4 = wheel(ms_z)`, `D3 = 1`, `D2 = not mid`, `D1 = not left`(ms_b(0)), `D0 = not right`(ms_b(1))
    (comment says D0=right, D1=left; buttons active-low).
  * `#FBDF` X, `#FFDF` Y: raw 8-bit AVR counters.
  Full 16-bit address match.
* Serial mouse (Profi-style VV51/i8251 emulation), needs `EXT` and `A7=1,A4:0=10011` (`mouse/serial_mouse.vhd:126-129`):
  data `#D3` (A6=1, A5=0), command/status `#F3` (A6=1, A5=1); control reg write `vv51_cs_cmd`; status
  bits 0 TxRDY, 1 RxRDY, 2 TxE; packets are 3 bytes, each byte `A6:0` data with `01` prefix in the first
  (`serial_mouse.vhd:274-283`). Also `#93/#B3` = interrupt-enable port (`mouse/hw_int.vhd`: D0=1 enables
  IM0 RST20 (RX ready) / RST28 (TX ready) interrupts when `cpm=1`). `INT_n = cpu_int_n and serial_ms_int`
  (`TOP:608`); vector bytes `#E7` (RST 20h), `#EF` (RST 28h) on INTA (`hw_int.vhd:69-70`). [PROFI 2+ feature, per the
  comment at `hw_int.vhd:34-38`.]

### 1.11 RTC [PROFI, MC146818-like, KP-emulated] (`TOP:1406-1414, 1529-1531, 1682`)

* AS (address latch): write A7:0 = `#BF` or `#FF`, EXT, M1=1 -> `mc146818_a_bus <= D`.
* DS (data): A7:0 = `#9F` or `#DF`, EXT, M1=1: write stores to a 256-byte dual-port RAM (`avr/rtc.vhd`), read
  returns it. Contents are populated by the AVR (`CMD_RTC`, `avr.vhd:280-282`), not by a counting clock in
  the FPGA. Real behaviour (time ticking, register B, update-in-progress flag) is **not** modelled in the FPGA [?].
* Typical driver (`software/profi/rtc/rtc.sys`): `IN A,(#FD)` style with `BC=#DFFD`, `SET 5` (cpm=1), then
  `OUT (#BF)` / `IN (#9F)` - confirming the EXT rule (cpm=1 and rom14=1).

### 1.12 KP-only service ports (need `cpm=1, rom14=1, ds80=1`, `TOP:1400-1404`)

`#C7` flash control/status (R: bit0 busy, bit1 rdy, bit2 SD/flash, bit3 fw-update; W: b0 rd,b1 wr,b2 sel,b3 fw,b4 erase64K);
`#87/#A7` flash page address low/high, `#67` byte address, `#E7` data. Only if `fw_update_mode`.
ZX-UNO UART: `#FC3B` register select, `#FD3B` data (regs `#C6` data, `#C7` status; UART2 `#C8/#C9`) (`uart/zxunoregs.v:40-41`).
ZiFi: `#C7EF` command/error, `#BFEF` data, `#C0EF` in-FIFO count, `#C1EF` out-FIFO count (`uart/zifi.vhd:54-58`).
None of these matter for Profi emulation.

---------------------------------------------------------------------------------------------

## 2. Memory map and paging

### 2.1 Physical layout (2 MB SRAM, "1 chip" boards; `MEM:129-157, 163-181`)

SRAM address `MA[20:0]`, 16K pages, `MA[20:14]` = page:

* `0x000000-0x0FFFFF` (1 MB): RAM, page number = `{DFFD[2:0], 7FFD[2:0]}` (64 x 16K).
* `0x100000-0x13FFFF` (256K): **four 64K ROM images** ("ext banks" 0-3); `MA[20:14] = "100" & ext_bank(1:0) & rom_page(1:0)`.
  Loaded from SPI flash at boot: flash 0x100000 -> SRAM 0x100000, 4x64K + 8K (`loader.vhd:23-27`).
  (Flash offsets from the `.cof`: 0x100000 main ROM, 0x110000 PQ-DOS BIOS, 0x120000 Flash Tool,
  0x130000 FDImage, 0x140000 esxDOS (8K DivMMC ROM), 0x1F0000 config byte.)
* `0x140000-...` DivMMC ROM (esxDOS 8K): `MA[20:14]="1010000"` (`MEM:173`); DivMMC RAM 512K at `0x180000` (`"11" & E3[5:1]`, `MEM:174`).
* 6 MB boards (`RAM_6MB` = cfg bit 5) use 3 chips; `ram_page` becomes 8 bits `{7FFD[6],7FFD[7],DFFD[2:0],7FFD[2:0]}` and `ram_page(7)` chooses chip 1/2 (`MEM:132-157, 238-239`);
  not relevant for original Profi 1024 (in 1 MB mode 7FFD bits 7:6 are ignored, `MEM:240`).

### 2.2 CPU 16K windows (`MEM:231-260`) [PROFI]

`mux = A15:A14`. With `sco`=`DFFD.3`, `scr`=`DFFD.6`, `B` = `{DFFD[2:0],7FFD[2:0]}` (page 0-63):

| Window | sco=0 (default) | sco=1 |
|---|---|---|
| 0000-3FFF | ROM (see 2.3) or RAM page 0 when `worom`=1 | same |
| 4000-7FFF | page 5 (fixed) | page B |
| 8000-BFFF | page 2 if `scr`=0, page 6 if `scr`=1 | same |
| C000-FFFF | page B | page 7 (fixed) |

Writes to a ROM window while `worom`=0 are ignored (`N_MWR` needs `is_ram`, `MEM:126`).
Shadow screen pages (from video): 5 (normal) / 7 (`7FFD.3`) (standard mode), 4/6 and 56/58 (hires), see 3.

### 2.3 ROM selection (0000-3FFF), `worom`=0 (`MEM:217, 225`)

`is_rom = MREQ=0 and A15:14=00 and worom=0`. Within a 64K image the 16K page is
`rom_page = {not dos_act, rom14}` (when DivMMC disabled), giving:

| dos_act | rom14 | rom_page | Content (main ROM, per `MEM:220-223` comments and string checks of `profi_mainrom_standart.rom`) |
|---|---|---|---|
| 1 | 0 | 0 | **SYS/CP-M page** (Profi service menu: "TR-DOS 48K / TR-DOS 128K / Sinclair 48 / Sinclair128 / CP/M system") - this is what boots after reset |
| 1 | 1 | 1 | **TR-DOS** ("TR-DOS Ver 5.04T", Profi-patched) |
| 0 | 0 | 2 | 128K BASIC ("1986 Sinclair Research") |
| 0 | 1 | 3 | 48K BASIC ("1982 Sinclair Research") |

Which 64K image: `ext_rom_bank_pq` = `soft_sw(4):soft_sw(3)` (menu keys, [KP]), overridden to `"01"` by
`#008B` bit 0 (`TOP:1371`; `board.vhd:37`). Bank 0 = main Profi ROM, 1 = PQ-DOS BIOS, 2 = Flash Tool,
3 = FDImage (order of `.cof` offsets). Changing `soft_sw(3/4)` triggers a board reset (`board.vhd:40-53`).
**With DivMMC enabled, `rom_page` is forced to 3** (48K BASIC, no TR-DOS/128 switching, `MEM:225`).
Note that in the ROM images the "128 ROM" is a **single 16K** (`128_low.rom` md5 a249... matches page 2 of the
main ROMs) - the "128 high" ROM is the Profi-patched 128 page; only 4 pages exist per image, so a real 128K
machine's 2x16K 128 ROM is reduced to one page + TR-DOS + SYS + 48K.

### 2.4 DOS (`dos_act`) flag - TR-DOS entry/exit (`TOP:1595-1598`)

Evaluated on every `clk_bus` rising edge (synchronous):
* **Set** `dos_act=1` when (and `DFFD.4`=0):
  * M1 fetch (`M1=0, MREQ=0`) at address `3D00..3DFF` **and** (`rom14=1` or `unlock_128`(`#008B.7`)), or
  * `NMI_n=0` and `DS80=0` (Magic button, [KP]),
  or when `onrom`(`#008B.6`)=1.
* **Clear** `dos_act=0` when (M1 fetch with `A15:A14 /= 00`, i.e. opcode fetch from RAM in >= 4000) or `DFFD.4=1`.
  Reset value = **1**.
  Priority: set has priority (the set branch is an `if`, the clear an `elsif`).
* Exit therefore happens on the first M1 opcode fetch from >= 4000 - identical to Beta-Disk logic
  (which uses `A15:14 != 00`).
* Effect: `dos_act=1` **and** `rom14=1` -> ROM page 1 (TR-DOS) and TR-DOS ports (1.6); `dos_act=1, rom14=0` -> SYS page 0.

### 2.5 Reset state

All port regs 0 (`TOP:1495-1512`): `7FFD=0` (rom14=0, page 0, screen 0, unlocked), `DFFD=0` (cpm=0, ds80=0, worom=0,
sco=0), `dos_act=1` -> SYS ROM page 0 of bank `ext_rom_bank`, CPU 3.5 MHz, palette default, DivMMC map reset.
Power-up: CPU held in reset until `loader_act=0` (flash->SRAM copy of ~270 KB, then `LOADER_RESET` pulse, `loader.vhd`); AVR delivers config (cfg byte).

### 2.6 DivMMC [KP]

Enabled from AVR (`divmmc_en`). Automap (`TOP:1730-1775`): on M1 fetch at `0000, 0008, 0038, 0066, 04C6, 0562`
(`mapterm`) or in `3D00-3DFF` (`map3DXX`) -> maps at the *next* instruction; unmaps after fetch at `1FF8-1FFF`
(`map1F00`). NMI when Magic pressed: `NMI_n = mapcond` (`TOP:1197`). Mapping (`MEM:214-215`): `(AUTOMAP or E3[7])`:
0000-1FFF -> DivMMC ROM `0x140000` (read only), 2000-3FFF -> DivMMC RAM: `MA = "11" & E3[5:1] & E3[0] & A[12:0]` (`MEM:166, 174`).
Not a Profi feature.

---------------------------------------------------------------------------------------------

## 3. Video

Two independent generators; `ds80` selects which one drives everything (`VID:172-187`).

### 3.1 Standard mode (DFFD.7=0): `pentagon_video.vhd` [PROFI base, Pentagon timing]

Clock: 28 MHz `CLK2x`, 14 `CLK`, 7 MHz `ENA` = pixel clock (`TOP:697-701`, `PENT:78`). CPU 3.5 MHz => 2 pixels per T-state.

**Geometry (default `SCREEN_MODE="00"` Pentagon, 50 Hz)** (`PENT:80-103, 105-123, 217-232`):
* Horizontal: `hor_cnt` 0..55 (56 characters x 8 px = 448 px = **224 T** per line).
  Paper: chars 0..31 (256 px). Right border chars 32..38 (56 px), blanking 39..47 (72 px;
  HSYNC low at chars 40..43, `hor(5:2)=1010`), left border chars 48..55 (64 px).
* Vertical: `ver_cnt` 0..39 x 8 rows = **320 lines** (`PENT:90`). Paper lines 0..191 (`PENT:282`: paper when `hor(5)=0, ver(5)=0`
  and not (`ver(4)=1 and ver(3)=1`)). Bottom border lines 192..239, blank 240..255 (`ver(5:1)=15`), top border 256..319.
  Line 0 = first paper line, INT is in the *bottom* border.
* Frame: 448 x 320 px / 2 = **71680 T** (Pentagon), **48.83 Hz** at 3.5 MHz.
* `MODE60` (`soft_sw(2)`): `ver_cnt` max 32 -> 264 lines (59136 T, ~59.2 Hz) (`PENT:90-93`) [KP].
* **INT** (`PENT:154-162`): `int_sig` set low when `ver_cnt=29, chr_row=7` (line 239), `hor_cnt(5:3)=100` at
  `chr_col=6, hor_cnt(2:0)=7` (char 39), high again 8 chars later. => INT pulse **32 T** (8 chars) starting at
  pixel 39*8+7 of line 239. Interrupt precedes first paper pixel by ~ (320-239)*224 - 159.5 = **~17985 T**
  (matches classic Pentagon 17988, MiSTer note 17987; the +-2 T is a pipeline offset, [?] exact edge).
  INT is not cleared by INTA in the pentagon block; it is simply 32 T wide.
* Turbo INT positions (`PENT:126-152`): INT width = 32 CPU T in all turbo speeds (4/2/1 chars) so INT wall-clock
  position stays fixed.
* **Classic 128 mode** (`SCREEN_MODE="01"`, [KP], from AVR): `ver_cnt` max 38 -> **312 lines**, 224 T/line =
  **69888 T** (48K/128K timing); INT at `ver_cnt=31, row=0, hor(5:3)=000, chr_col=0` (line 248; 64 lines before line 0 = 14336 T),
  32 T wide. `hor_cnt` blank 40..47 chars (`PENT:228`).
  60 Hz: `ver_cnt` max 31 (256 lines) [?].

**Address generation** (`PENT:276-280`). Video address `A[13:0]` on a 16K-bank offset, bank from `MEM:178`:
```
bitmap   A = 0, ver_cnt[4:3], chr_row[2:0], ver_cnt[2:0], hor_cnt[4:0]      (0x0000-0x17FF; standard ZX layout)
attr     A = 0, 1,1,0, ver_cnt[4:0], hor_cnt[4:0]                              (0x1800-0x1AFF)
```
Page: `MA[20:14] = "00001" & 7FFD[3] & "1"` -> **page 5** (7FFD.3=0) or **page 7** (7FFD.3=1) (`MEM:178`); i.e. the
shadow screen is page 7, independent of `scr/sco` and DFFD.
Video reads take memory cycles on the second RAM access slot (`vbus_mode`, `MEM:196-210`); CPU never sees contention
from this (the SRAM is time-multiplexed at 14 MHz); the CPU is only stalled in classic mode below.

**Pixel colour** (`PENT:186-212`): fg/bg from attr: ink = `attr[2:0]` = G R B (bit2 G, bit1 R, bit0 B), paper = `attr[5:3]`,
bright `attr[6]` (both ink and paper), **flash `attr[7]`**: swap when `invert(4)` where `invert` increments each frame
(`PENT:96`) => toggle every 16 frames (32-frame period).
Then through the 16-entry palette (index = `{bright,G,R,B}`).
Border: `BORDER[2:0]`, bright=0 (`PENT:207-210`).

### 3.2 Profi hires mode (DFFD.7=1, "DS80"): `profi_video.vhd` [PROFI]

Clock 24 MHz / 12 MHz `CLK` (`clk_bus`=24 when ds80, `TOP:584-590`). **12 MHz pixel clock**, CPU 24/8 = **3 MHz**
(`TOP:1212`), i.e. 4 pixels per T-state.

**Geometry** (`PV:42-82, 111-157, 209-224`):
* Horizontal counter 0..767 (768 px at 12 MHz = **64 us = 192 T @ 3 MHz**). Paper `h<512`; right border 512..559 (48);
  front porch to 591; HSYNC 591..655 (64); back porch to 719; left border 720..767 (48) [PV:62-66 constants;
  border/blank at `PV:211`].
* Vertical 0..311 (312 lines) = **59904 T** per frame; 3 MHz/59904 = **50.08 Hz**. Paper `v<240`, bottom border 240..255,
  blank/sync to 279, ...; (`PV:49-71`). `v_cnt` increments at `h = 591` (`PV:121-127`), not at 0.
* `MODE60`: 264 lines, sync/blank shortened, no borders (`PV:56-77`).
* **INT** (`PV:142-146`): `int_sig=0` while `h>656 and v==257` (turbo 00) or `h>706 and v==257` (turbo!=00). Because `v_cnt`
  only increments at h=591, this remains low from h=657 through the end of the line **and** h=0..591 of the next line,
  i.e. ~703 pixel clocks = **~58.6 us = ~176 T at 3 MHz** (long pulse!). It is *not* cleared by INTA (INTA only
  drives `bl_int`, `PV:149-153`), so an immediate `EI` in the handler will re-interrupt for the rest of that window.
  INT -> first paper pixel = (312-257)*768 - 65 + 176 = 42351 px = **~10588 T** (@3 MHz). [derived, check against a real Profi]

**Memory addressing** (`PV:190`):
```
A[13:0] = { not h_cnt[3], v_cnt[7:6], v_cnt[2:0], v_cnt[5:3], h_cnt[8:4] }
```
i.e. per 16-pixel cell (2 bytes, `h_cnt[3]` picks byte 0/1) the address is a standard ZX layout with **A13 = not h_cnt[3]**:
first byte of a 16-px pair is read from offset `0x2000 + zx_addr`, second from `0x0000 + zx_addr`
(`zx_addr` = third/row/line/column layout above with `v_cnt[7:6]` as third index up to 3, so v up to 239 uses 240 lines
of "192+48").
Both bitmap and attribute are read for each byte using the *same* `A` on two different pages, alternating by `VID_RD`
(`MEM:176-180`, `PV:175-188`):
| Fetch | `MA[20:14]` | Page |
|---|---|---|
| pixel byte (`vid_rd=0`) | `"00001" & 7FFD[3] & "0"` | **page 4** (7FFD.3=0) or **6** (7FFD.3=1) |
| attribute byte (`vid_rd=1`) | `"01110" & 7FFD[3] & "0"` | **page 56 (0x38)** or **58 (0x3A)** = `{DFFD[2:0]=7, 7FFD[2:0]=0/2}` |
(matches comment `TOP:1376`: "seg06 & seg3A & seg04 & seg38").

**Pixel/colour** (`PV:192-209`): 512 px/line, 1 bit per pixel, 8 pixels per byte, MSB first (`pixel_reg(7 - h[2:0])`), 1 attribute byte
per pixel byte (8 px), i.e. **attribute per 8x1 pixels** (Profi "hi-colour"):
* attr bits: D0 ink B, D1 ink R, D2 ink G, D3 paper B, D4 paper R, D5 paper G, **D6 = ink bright**, **D7 = paper bright**
  (`i78 = attr[7]` when ds80 for paper, `attr[6]` for ink, `PV:198-201, 209`). No flash in hires.
* colours through the palette: index `{bright,G,R,B}` (see 1.3). Palette is writable only here.
* Border (`PV:203`): `rgbi = not BORDER[1], not BORDER[2], not BORDER[0], (not BORDER[3]) and bl_int` -> border colour is the
  **inverted** low 3 bits of the last `#FE` write; bright = inverted bit 3 gated by `bl_int` [?] (`bl_int` is an INT/INTA
  derived signal; effect on visible border is not obvious).
* Pixel/attr fetched 8 px early and latched at `h[2:0]=7` (`PV:160-172`).

### 3.3 Other display modes / effects

None found beyond the above and the AVR `SCREEN_MODE` (00 Pentagon, 01 classic, 10/11 reserved, `avr.vhd:64`). No
Timex hires/multicolour, no 6912/attr-mode toggles, no separate shadow-screen bit in DFFD. VGA output uses a scandoubler
(`video/vga_pal.vhd`) and OSD overlay; ignore for emulation.

### 3.4 Contention / wait states

* **Pentagon mode (default) and DS80: no contention.** Contention logic exists only for classic mode:
  `clk_cpu = '0'` when `kb_screen_mode="01" and memory_contention=1 and automap=0 and cs_nemo_ports=0 and DS80=0` (`TOP:1206`).
* `memory_contention` (`MEM:273-284`) = `(A0=0 and IORQ=0 or A15:14=01)` (contended addresses: page 4000-7FFF regardless
  of which RAM page, and *even I/O ports*) and `block_reg` and `count_block` and `DS80=0` and `turbo=00`.
  `count_block = paper and (chr_col(2)=0 or hor_cnt(0)=0)` (`PENT:297`): within the 256-px paper area, contention window covers
  the first 4 px of each char always and the last 4 px only on even chars, i.e. 6 of 8 T of each 16-px pair (approximate ULA pattern).
  `block_reg` (`MEM:262-271`) prevents re-contention inside one access.
  Behaviour is a **clock stop** (not a delay curve); emulator can use standard 48K/128K 6,5,4,3,2,1,0,0 model for classic mode [?].
  Not contended in high pages (C000 banked pages 1,3,5,7 as on the 128K) - only address-based, so page 5 mapped at C000 is **not**
  contended and page 4 at 4000 (`sco=1`) **is** [?].
* No contention when turbo != 3.5 MHz (`TURBO_MODE="00"` term).

---------------------------------------------------------------------------------------------

## 4. Boot/reset, CPU speed

* Reset sources (`TOP:1191`): power-on `areset`, keyboard reset (`kb_reset`), loader, `board_reset`. `cpu_reset_n = not reset`.
* NMI (Magic): with DivMMC -> `mapcond`; without DivMMC: `NMI_n=0` when M1 fetch with `A15:14 /= 00` or `DS80=1` (`TOP:1197-1199`).
  With `DS80=0`, NMI also sets `dos_act` (enters DOS ROM).
* CPU clock (`TOP:1206-1212`): base = `clk_bus and ena_div8` (28/8 = **3.5 MHz**; 24/8 = **3 MHz** in DS80).
  `#028B[6:5]` (turbo, also set by hot key via AVR):
  * `00` -> 3.5 MHz; `01` -> `ena_div4` = **7 MHz**; `10` -> `ena_div2` = **14 MHz**; `11` -> would be full 28 MHz but `max_turbo` is hard-coded
    `"10"` (`TOP:1203`), so **not reachable**.
  * All turbo modes fall back to 3.5 MHz while `fdd_wait=0` (after FDC access; ~128 x 7 MHz-enable ticks) and
    when the SYSTEM is in "TR-DOS classic" mode this is automatic per the comment at `TOP:1208` (in fact the condition is only `fdd_wait`).
  * 14 MHz additionally inserts wait states: `WAIT_C` counter (`TOP:1214-1233`), active only if `turbo(1)=1`; on each `MREQ` cycle
    stops the CPU for ~2 `ena_div2` (14 MHz) periods; comment: "400 ns per /IORQ fall" (`TOP:1214`) [?].
  * `kb_wait` (AVR "pause" key) freezes the CPU (`TOP:1206`).
* `dos_act=1, rom14=0` after reset => SYS ROM boots (the Profi menu); "TR-DOS 48K" in that menu sets `7FFD.4=1` and jumps.
* Interrupts: INT = ULA INT (`cpu_int_n`) AND serial-mouse INT (`TOP:608`). INTA = `IORQ or M1` (`TOP:1194`). Default IM chosen by software.

---------------------------------------------------------------------------------------------

## 5. ROMs, tests and tools present

### 5.1 `firmware/src/fpga/profi/rom/` (each `.rom`/`.bin` is a raw image; `.hex` is the same data in Intel HEX for Quartus)

| File | Size | What | Automated-test use |
|---|---|---|---|
| `profi_mainrom_standart.rom` (md5 321f2c6b...) | 64K | **Standard Profi main ROM**: pages 0 SYS/CPM menu, 1 TR-DOS 5.04T (Profi), 2 128 BASIC, 3 48 BASIC | Primary emulator ROM target (Profi machine) |
| `profi-2.2.rom`, `profi_mainrom2_2_fatall.BIN/.HEX`, `profi_mainrom_2_3_fatall*.rom`, `profi_mainrom_fatall.rom`, `profi_mainrom_realcom.rom`, `profi_rage_fix.rom` | 64K | Profi main ROM variants (2.2, 2.3, "FATALL" = FAT + file manager builds, "RAGE fix", "realcom") - page 2 shared (md5 a249...) with `128_low.rom` | Alternative main ROMs; same paging |
| `ROMain.rom`, `ROMain_ramdisk_{A,B,C,D}.rom`, `ROMain_fatall_ramdisk_D.rom` | 64K | Karabas builds of main ROM (CP/M system, SD "File Simplorer", ramdisk variants A-D) | [KP] |
| `profi_v450.ROM` | 64K | Profi ROM "Award Modular BIOS v 4.50PG ... Profi Code Club" (joke BIOS-style Z80 splash) | none |
| `128_low.rom`, `sos.rom`, `service_2_2_fatall.rom` | 16K | single 16K pages (128 BASIC, 48 BASIC "SOS", service page) | |
| `dos6_11p.rom`, `dos6_11q_a.hex`, `TR-DOS_6.11Q_{PENTAGON1024,SCORPION1024,ZX_PROFI1024,ZXM_PHOENIX}_RMD_A.ROM` | 16K | TR-DOS 6.11 variants (`_ZX_PROFI1024_` is for Profi) | TR-DOS behaviour tests if FDC exists |
| `bios_pqdos.rom` | 64K | PQ-DOS BIOS bank (ext bank 1) | |
| `karabas-pro-flashtool_v2_{3a,4a,6_crc,7,71}.rom` | 64K | Doctor Max Flash Tool (ext bank 2, uses ports #C7/#87/#A7/#E7/#67) | [KP] |
| `fdimage.rom` | 64K | FDImage tool (ext bank 3), writes real floppies | [KP] |
| `full_divmmc.rom`, `full_gluk_dffd.rom` | 64K | full ROM bank with esxDOS/Gluk RTC/DFFD support variants | [KP] |
| `esxdos086/087/089.bin`, `.zip` | 8K | esxDOS 0.8.6/0.8.7/0.8.9 DivMMC ROM (SRAM 0x140000) | [KP] |
| `TEST430.ROM` | 64K | "SYSTEM TEST V4.30R - Collection of Crazy Tests!, Compo Wellcome, Chirchik 1999" (menu: "1 - POWTOR TESTA" ...). 4 x 16K pages (pages 0 and 2 identical) | Interactive menu of Profi hw tests; **screen output only** |
| `TEST3_00.ROM` | 3840 B | "COMPOWELLCOME PRESENTS: MEMORY TEST V3.00" (Russian-transliterated: "prowerka nivnej pamqti", "test nivnej pamqti pro{el", "test porta #7FFD...", "test ras{irennoj pamqti...", "o{ibka po adresu:"). Tests lower RAM, port `#7FFD`, and **extended RAM** paging | Good paging test: pass = final "test projden. navmite klawi{u." ("test passed. press any key"), fail = "o{ibka po adresu: xxxx" on screen. Load at 0000 as ROM (3840 B - pad to 16K) |
| `TEST48K.rom` | 2048 B | 2K ROM test image (font bitmap visible); "TEST 48K" | Small standalone memory/screen test [?] (unclear structure) |
| `DiagROM.v50` | 16K | Retroleum "DIAGROM V1.50" style: tests RAM 4000-7FFF (contended), 8000-FFFF, checks ROM CRC, machine detection (Pentagon128, TC2048, TC2068...), "ERROR! CHECKSUM HAS CHANGED" | Reports on **screen** ("Lower 16KB RAM: OK", "Upper 32KB RAM: OK", "Bad Bits: 76543210 XP:$.. RD:$.."). Good 48K RAM/contention sanity test |
| `cfg_rev*.bin` | 1 B | Board config byte flashed at 0x1F0000 (see `docs/karabas_pro_cfg.txt`: b0 DAC type, b2 tape in/out, b5 6MB, b4 Ї mod) | |

### 5.2 `software/profi/`

| Path | Purpose | Test usage |
|---|---|---|
| `tests/ZEXALL.$C`, `tests/ZEXDOC.$C` (8721 B each) | Z80 instruction exerciser (all flags / documented flags) as a **TR-DOS `C` file** (`zexall  C`; header bytes suggest start `#8000` [?]). Code starts `LD SP,#8000 ... LD A,2; CALL #1601` (48K ROM channel open), prints per-test lines through ROM `RST 16`: `"<name>......OK"` or `"CRC: xxxx expected: xxxx"` and ends with `"Tests complete"` (strings verified in the binary) | **Fully usable as an automated CPU test**: load bytes at #8000 with 48K BASIC ROM paged in (rom14=1, dos_act=0), run from #8000; scan screen text (attr/pixel matching) or hook `RST 16` / address `#9CDA` print routine for `"OK"` / `"CRC:"`; ends at `"Tests complete"`. Runtime is long - run unthrottled. |
| `tools/karabas_boot.$C`, `WDC1_6.$C`, `WPV0_333.SPG`, `unFDIv5.WMF`, `unTRD-PR.WMF` | Boot loader, WD Commander 1.6 (disk manager), "WPV" (SPG = ZX-Evo program) and unFDI/unTRD unpackers | Interactive; TR-DOS/CP-M dependent; not useful for automation. `WDC` exercises FDC + Profi ports |
| `rtc/rtc.sys` (195 B), `rtc.z80` (5.4 KB ASM source, TBBlue-derived, rewritten for Profi 5.0x by OCH 2023-07-18) | RTC driver that shows the `DFFD.5` (cpm) + `#BF/#9F` sequence | Test for RTC ports if RTC emulated |
| `pq-dos/pqdos1.fdi`, `.img`, `README.md` | PQ-DOS boot floppy, README describes making a boot floppy via FDImage / HDD | FDC/IDE tests (FDI image usable) |
| `isdos_nemoide/*` | IS-DOS raw HDD image + `isdoshdd.$C`; README: enable NEMO IDE, run from SD | Nemo IDE test |
| `fatfs/`, `net-tools/` | FatFS sample source, ZiFi/UNO network apps | not Profi related |

There is **no** ULA/contention/screen tests (no `hires` demo, no border/floating-bus tests) in this repo. No test reports via a port
or beeper; **all output is on-screen text** (48K ROM channel or own font).

---------------------------------------------------------------------------------------------

## 6. Open questions / ambiguities

1. **`fd_port` exact semantics.** The latch is on the rising edge of `cpu_m1_n` using the data bus during the
   opcode fetch; I read it as "the previous opcode was `OUT (n),A`/`IN A,(n)` -> disable 16-bit-decoded ports for
   the next I/O". Since `IN/OUT (n)` are 2-byte opcodes the M1 fetch of the *opcode* precedes the port cycle (M1 rising edge
   happens after the opcode fetch), so it applies to that same instruction; confirm on a real Profi that `OUT (#FD),A` with A=#7F
   should not page memory except via the short-port branch.
2. **Short-port `#xxFD` aliasing** (`A15=0 and A1=0`) also matches e.g. `#1FFD`, `#0000`-`#3FFC`-with-A1=0 ports and any
   `A15=0` even/odd port with A1=0 (including `#7FFC`, `#3FFD`). Real Profi uses `#7FFD`-only decode plus the short `#FD`? Unclear which is authoritative.
3. **DS80 (Profi) 3 MHz vs 3.5 MHz.** In hires the FPGA drops CPU to 3 MHz (24 MHz clock). Verify against real Profi documentation; INT length
   (~176 T) is unusually long and INT is not cleared on INTA (`PV:149-153`).
4. **Border in DS80**: colour is `not BORDER[3:0]` looking suspicious (inverted `#FE` bits), and bright bit is gated by `bl_int`. Needs verification on real HW/other Profi docs.
5. **Palette write** takes index from the *previous* `#FE` value and colour from address high byte; only D7 of that previous write is the
   blue LSB. Confirm precise Profi protocol (e.g. whether `#FE` write and `#7E` write can be one instruction or must be two).
6. **FDC/`#FF`/HDD internals** are in the external CPLD; the FPGA decode tables above are exact, but `#FF` bit meanings in
   TR-DOS / CP/M modes and Profi's alternate `#83/#A3/#C3/#E3` register file are inferred, not shown by source.
7. **ROM page layout.** Only 4 x 16K pages per 64K image (SYS, TR-DOS, 128, 48), so the 128K machine's second ROM half is not separate; page identities inferred from strings/md5, not from the VHDL. Use `profi_mainrom_standart.rom` as reference.
8. **RTC** is only a 256-byte RAM fed by the AVR; the read-only registers (seconds counting, `UIP`) are not modelled - real Profi behaviour with MC146818 would need to be
   emulated from datasheet.
9. **Unused/undefined bits**: `#008B` bits 1-5, `#018B`, `#028B` bit 1, `#1FFD`, `#EFF7` are stored and unused. Real Profi may have
   used them (e.g. `#1FFD` bits for 1024K extension), not visible here.
10. **7FFD lock and `worom`:** lock condition uses `port_7ffd_reg(5)` and `port_dffd_reg(4)`; when `DFFD.4=1` (RAM at 0000) every 7FFD write is accepted *and* `dos_act` is forced 0, ROM disabled.
11. **Contention pattern** approximated via clock stop (`TOP:1206`, `MEM:273`), not identical to ULA timing; classic-mode only.
12. **Sound `A15=1, A1=0` AY decode** is looser than the real Profi (which had no AY on the base board); TurboSound behaviour is KP-specific.
13. **Unknown:** how `ZEXALL` expects `#9CDA` print vector - the code is self-contained but disassembly not done here [?].
