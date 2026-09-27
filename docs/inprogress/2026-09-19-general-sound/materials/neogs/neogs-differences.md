# NeoGS vs original General Sound — key differences

*Reference notes for unreal-ng. Checked 2026-09-27 against the NeoGS FPGA
and firmware sources (git-svn mirror of NedoPC `ngs`, r191) and the original
GS documentation. The full emulation design is in
[`../../neogs-tdd.md`](../../neogs-tdd.md).*

---

## Overview

NeoGS is a General Sound clone built by the NedoPC group (first board 2008,
sources maintained to 2026). It keeps the GS programming model and adds more
memory, eight channels, an interrupt controller, DMA, an SD card slot and an
MP3 decoder.

---

## 1. Hardware comparison

| Feature | General Sound | NeoGS |
|:--------|:--------------|:------|
| **Architecture** | Z80 + discrete logic | **Physical Z80** + Altera ACEX 1K FPGA (EP1K30) + a small CPLD (EPM3064) that loads the FPGA configuration at power-on |
| **CPU clock** | 12 MHz | 10 / 12 / 20 / 24 MHz, set by GSCFG0 bits 5:4. Reset: 10 MHz; the loader switches to 20 MHz before starting the main ROM |
| **ROM** | 32 KB EPROM | 512 KB flash (Am29F040-class, 8 × 64 KB sectors, reprogrammable in place) |
| **RAM** | 128 KB, expandable to 2 MB with an add-on board | 4 MB (256 pages of 16 KB) with the current FPGA; 2 MB with the older fpgaD |
| **Sound** | 4 channels, 8-bit samples × 6-bit volumes, analogue mixing | 4 or 8 channels multiplied digitally, summed per side to 16-bit, one stereo serial DAC |
| **Output rate** | set by the firmware's 37.5 kHz interrupt | DAC frames at exactly 37.5 kHz (24 MHz / 640) |
| **Interrupt** | 37.5 kHz from the 12 MHz clock | 37.5 kHz timer from a separate 24 MHz clock (independent of the CPU clock), plus SD-DMA and MP3-DMA interrupts, maskable, with vectors |
| **Bus** | ZX-BUS | ZX-BUS |

> NeoGS uses a **physical Z80**; the FPGA is glue logic, not a soft core
> (`fpgaD/main.v`: the Z80 buses are external pins).

---

## 2. New NeoGS features

### 2.1 Eight channels and new mixing modes
- `GSCFG0` bit 2 (`8CHANS`): eight channels. The channel is chosen by A10:A8 of
  the sample read at `#6000`-`#7FFF` (A9:A8 in 4-channel mode).
- `GSCFG0` bit 6 (`PAN4CH`, only when 8CHANS = 0): four channels, each with a
  left and a right volume.
- `GSCFG0` bit 7 (`INV7B`): samples are two's complement instead of
  `#80`-centred.
- Volumes for channels 5-8: ports `#16`-`#19`, always writable in every mode.

### 2.2 Memory paging
- **Current FPGA:** each of the four windows has its own 8-bit page register,
  PG0-PG3 at ports `#20`-`#23` (reset PG0 = 0, PG1 = 3).
- **`MPAG` (`#00`)** in normal mode sets PG2 = 2v and PG3 = 2v+1.
- **EXPAG mode** (`GSCFG0` bit 3): `MPAG` sets PG2 alone and `MPAGEX` (`#10`)
  sets PG3 (the `#C000` window). Both take the byte rotated left.
- **ROM mode** (`NOROM` = 0, reset): windows 0, 2 and 3 read flash, and window
  1 (`#4000`) is always RAM. With `NOROM` = 1 all windows read RAM.
- **fpgaD:** windows 0 and 1 are fixed (page 0 and page 3), and pages are
  7-bit.

### 2.3 SPI: SD card and MP3 decoder
| Port | Access | Function |
|:-----|:-------|:---------|
| `#11` SCTRL | R/W | SD nCS, MP3 control nCS, MP3 XRESET, SPI speed bits (write: bits set in d5..d0 take the value of d7); reset `#0B` |
| `#12` SSTAT | R | MP3 DREQ, SD card detect, SD write protect, MP3 control ready |
| `#13` | W / R | SD_SEND starts a byte exchange / SD_READ returns the last byte received |
| `#14` | R / W | SD_RSTR returns the last byte **and starts a new exchange sending `#FF`** (it is not a reset) / MD_SEND: MP3 data |
| `#15` | W / R | MC_SEND / MC_READ: MP3 control (SCI) |

### 2.4 MP3 decoder
- The chip is an MA8201 / MA8201A, a clone of VLSI VS1001 / VS1011
  (MPEG Layer III).
- Control (SCI) goes through port `#15`, data (SDI) through port `#14` or MP3
  DMA.
- The main ROM does **not** play MP3. Players on the Spectrum (e.g. Neo Player
  Light in the NeoGS repo, `zx/`) upload their own code to the card.

### 2.5 DMA
| Port | Function |
|:-----|:---------|
| `#1B` DMA_MOD | Selects which module `#1C`-`#1F` address: 1 = ZX ↔ card RAM, 2 = SD → RAM, 3 = RAM → MP3 |
| `#1C` DMA_HAD | Address bits 21:16 (**6 bits**) |
| `#1D` DMA_MAD | Address bits 15:8 |
| `#1E` DMA_LAD | Address bits 7:0 |
| `#1F` DMA_CST | Bit 7 = run |

These are in the **current FPGA** (`fpga/current/dma/`), not in fpgaD. DMA
reaches RAM only. It stalls the card CPU for 2 clocks per byte during bursts.

### 2.6 Interrupt controller
| Port | Function |
|:-----|:---------|
| `#0C` INTENA | Enable mask: bit 0 timer, bit 1 SD DMA, bit 2 MP3 DMA (write: d7 = set/clear, d2:0 = bits); reset `001` |
| `#0D` INTREQ | Pending requests (read), set/clear (write) |
| `#0E` TIM_FREQ | Timer divider: 37.5 kHz divided by 1, 2, 4, 8, 16, 64, 256 or 1024 |

`/INT` is a level held while a request is pending. The vectors are `#FF`
(timer), `#F7` (SD) and `#EF` (MP3), so IM 1 still works.

---

## 3. Port differences

### 3.1 Original GS ports on NeoGS
| Port | GS | NeoGS |
|:-----|:---|:------|
| `#00` | MPAG: D0-D3 select a 32 KB page for `#8000`-`#FFFF` (0 = ROM) | MPAG: 7 bits; see §2.2 |
| `#01` | Read: command from host | Read: same. **Write: LED** (d0 = 0 on) |
| `#02` | Read: data from host; clears status bit 7 | Same |
| `#03` | Write: data to host; sets status bit 7 | Same |
| `#04` | Read: status (bit 7 data, bit 0 command) | Same |
| `#05` | Clears status bit 0 | Same (read or write) |
| `#06`-`#09` | Volumes, channels 1-4 | Same |
| `#0A` | Status bit 7 ← NOT bit 0 of port 0 | Status bit 7 ← NOT bit 0 of **PG2**. In normal paging PG2 bit 0 is always 0, so the bit is always set. |
| `#0B` | Status bit 0 ← bit 5 of port 6 (VOL1) | Status bit 0 ← bit 5 of **port 9** (VOL4) |

GS column: the original port document `GS_PORTS.TXT`. NeoGS column:
`fpga/current/ports/ports.v:476-514`.

### 3.2 NeoGS-only ports (current FPGA)
| Port | Function |
|:-----|:---------|
| `#0C`-`#0E` | Interrupt controller (§2.6) |
| `#0F` | GSCFG0 (§3.3) |
| `#10` | MPAGEX (§2.2) |
| `#11`-`#15` | SPI (§2.3) |
| `#16`-`#19` | Volumes, channels 5-8 |
| `#1B`-`#1F` | DMA (§2.5) |
| `#20`-`#23` | PG0-PG3 (§2.2) |

The FPGA decodes A5..A0 of ports `#00`-`#3F`; A15..A8 are ignored.

### 3.3 GSCFG0 (port `#0F`), reset `#30`
| Bit | Name | Function |
|:----|:-----|:---------|
| 0 | NOROM | 0 = flash in windows 0, 2, 3 (window 1 always RAM); 1 = RAM in all windows |
| 1 | RAMRO | With NOROM = 1: writes blocked to RAM pages 0, 1, 128 and 129, in any window |
| 2 | 8CHANS | Eight-channel mode |
| 3 | EXPAG | Extended paging (§2.2) |
| 5:4 | clock | `00` = 24 MHz, `01` = 12, `10` = 20, `11` = 10 MHz |
| 6 | PAN4CH | Four channels with left and right volumes |
| 7 | INV7B | Two's-complement samples |

The register reads back what was written. The exception is fpgaD, where
bit 7 reads 0.

### 3.4 Host side
| Port | GS | NeoGS |
|:-----|:---|:------|
| `#B3`, `#BB` | Data / command and status | Same |
| `#33` | Bit 7 resets the card, bit 6 sends an NMI | Decoded on d7..d5 **exactly**: `100` resets the card (one reset per write), `010` sends an NMI, `001` toggles the LED |

**Spectrum reset.** It reaches the card only through jumper J1. The path is
ZX-BUS `/RES` → inverter → transistor → J1 → the FPGA's warm-reset input
(`pcad/revC-VS/NeoGS.sch`). Power-on and the reset button reconfigure the
FPGA through the CPLD.

---

## 4. Memory map

### 4.1 Original GS
```
0x0000–0x3FFF: ROM page 0
0x4000–0x7FFF: RAM page 1 (fixed; the upper half of MPAG 1 - see note)
0x8000–0xBFFF: MPAG 0: ROM page 0; MPAG v: RAM page 2(v-1)
0xC000–0xFFFF: MPAG 0: ROM page 1; MPAG v: RAM page 2(v-1)+1
```

Note: on both cards the fixed window shows the same cells as `0xC000-0xFFFF`
under MPAG 1.
- **Original GS:** the schematic (GeneralSound v1.0 `GS_GENER.TXT`) drives
  chip select RAM1 from both the `0x4000` window decode and page 1. Each
  32 KB chip takes CPU A0-A14.
- **NeoGS:** `memmap.v` maps the window to page 3 and MPAG v to pages 2v and
  2v+1.

The GS page numbering here counts 16 KB RAM pages from MPAG 1.

### 4.2 NeoGS running the main ROM
The loader leaves `GSCFG0 = #23`: NOROM, RAMRO, 20 MHz.
```
0x0000–0x3FFF: RAM page 0 (PG0; write-protected ROM copy)
0x4000–0x7FFF: RAM page 3 (PG1, reset value; also the upper half of MPAG 1)
0x8000–0xBFFF: RAM page 2v (MPAG = v), or PG2 via MPAG in EXPAG mode / port #22
0xC000–0xFFFF: RAM page 2v+1, or PG3 via MPAGEX in EXPAG mode / port #23
```
NeoGS numbers RAM from 0 with the ROM copy in pages 0-1. MPAG 0 therefore
shows that copy at `#8000`, as ROM appears there on a GS.

---

## 5. Firmware

### 5.1 What runs on the card
| Stage | Where | Source |
|:------|:------|:-------|
| FPGA boot (configuration) | flash `#70000` | `z80/bootFPGA00/` |
| Loader | flash `#00000` | `z80/loader_ngs/`: copies the main ROM from flash `#10000` to RAM, or loads `NEOGS.ROM` from the SD card; own command set after a `#55`/`#AA` handshake |
| Main ROM | flash `#10000`, run from RAM | `z80/main_rom/main_ngs.a80`: GS-compatible, based on the improved GS 1.04 (psb & Muchkin). Versions seen: 1.08 (`data/rom/bootGS.rom`), 1.09 (`ngsrom109/ngs_rom.upd`), 1.11 (current) |

The whole flash image is built from these sources with the AS assembler. See
`neogs-tdd.md` §4.1.

### 5.2 What the NeoGS main ROM adds over GS
- RAM detection for 512 KB, 2 MB and 4 MB.
- Commands `#6A` and `#6B` (player mode, relooper), which also exist in GS
  1.05 with the psb patches.

It has **no** SD, MP3, DMA or 8-channel code. SD boot lives in the loader;
MP3 playback and 8-channel use come from programs the host uploads.

### 5.3 Stock GS firmware
Stock GS 1.04, 1.05a, 1.05b and 1.08 exist as sources that rebuild the
original ROMs byte for byte (`z80/gs_old_vers/`). The loader can run such an
image from the host or from the SD card. `z80/gs105a_fix/gs105a_2mbonly.rom`
is 1.05a with its RAM test replaced by a fixed page list, for the old boot
program.

---

## 6. Emulation notes

### 6.1 Classic GS emulation
- GSCFG0, the interrupt controller, SPI, DMA and PG0-PG3 do not exist on a GS.
- Four channels.
- Stock RAM is 128 KB. The add-on board takes it to 2 MB. unreal-ng currently
  models 128-512 KB.

### 6.2 NeoGS emulation
See `neogs-tdd.md`.

### 6.3 Telling the cards apart
- **From the card side:** GSCFG0 reads back what was written. A GS has no
  such register.
- **From the host:** the NeoGS loader answers command `#1D` with `#76` in its
  command mode.

---

## 7. References

### Local materials
- [`fpgaD/main.v`](fpgaD/main.v) — fpgaD top level (external Z80 bus)
- [`fpgaD/ports/ports.v`](fpgaD/ports/ports.v) — fpgaD ports
- [`NGS_b_scheme.pdf`](NGS_b_scheme.pdf) — NeoGS rev B schematic
- [`ngs_b.pdf`](ngs_b.pdf) — NeoGS rev B documentation
- [`ngs_c_cpld.pdf`](ngs_c_cpld.pdf) — CPLD variant documentation
- [`GS_PORTS.TXT`](GS_PORTS.TXT) — original GS port document (CP866)
- [`vs1001_datasheet.pdf`](vs1001_datasheet.pdf) — MP3 decoder

### NeoGS sources
- NedoPC SVN `ngs` (`http://nedopc.com/gs/ngs_eng.php`); local git-svn mirror
  `/Volumes/TB4-4Tb/Projects/emulators/github/neogs`:
  - `fpga/current/`: current FPGA;
  - `z80/`: firmware;
  - `cpld/`: CPLD;
  - `pcad/`: schematics;
  - `docs/`: `ports.inc`, `spi_doc.txt`, `dma_zx_doc.txt`.
- [NeoGS GitHub mirror](https://github.com/alfishe/neogs)

### Other
- [Unreal Speccy gsz80.cpp](https://github.com/alfishe/unrealspeccy/blob/master/gsz80.cpp) —
  another NeoGS emulation; its differences from the FPGA are listed in
  `neogs-tdd.md` §3.11
- [minimp3](https://github.com/lieff/minimp3) — the MP3 decoder chosen for
  unreal-ng (CC0)
