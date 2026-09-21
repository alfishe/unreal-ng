# NeoGS vs Original General Sound — Key Differences

*For Unreal-NG emulator implementation planning (Priority: P2)*

---

## Overview

NeoGS is an FPGA-based reimplementation of General Sound developed by the NedoPC group in 2008. It maintains backward compatibility with original GS while adding significant enhancements.

---

## 1. Hardware Comparison

| Feature | General Sound | NeoGS |
|:--------|:--------------|:------|
| **Architecture** | Discrete Z80 + logic ICs (~40 chips) | **Physical Z80** + FPGA/CPLD glue logic |
| **CPU** | Z80H @ 12 MHz | Z80H @ 12/24 MHz (switchable) |
| **Glue Logic** | ~40 discrete ICs | Altera EP1K30/50 (FPGA) or CPLD |
| **ROM** | 32 KB EPROM | 512 KB Flash (reprogrammable) |
| **RAM** | 128–512 KB SRAM | 2–4 MB |
| **DAC** | 4 × 8-bit, discrete | 4–8 × 8-bit, I2S integrated |
| **Sample Rate** | 37.5 kHz | 37.5 kHz (compatible) |
| **Power** | +5V only | +5V / +3.3V |
| **Bus** | ZX-BUS edge connector | ZX-BUS compatible |

> **Note:** NeoGS uses a **physical Z80 CPU chip**, not an FPGA soft-core. The FPGA/CPLD
> handles memory mapping, port decoding, ZX-BUS interface, and peripheral control only.
> See `fpgaD/main.v` where Z80 buses are external inputs.

---

## 2. New NeoGS Features

### 2.1 Extended Audio Channels
- **8-channel mode** (vs 4 in original)
- Controlled via `GSCFG0` bit 2 (`M_8CHANS`)
- Additional volume registers: ports `0x16–0x19` (channels 5–8)

### 2.2 Extended Memory Paging
- **MPAGEX** (port `0x10`): Extended page select for 3rd memory window
- **EXPAG** mode: Enables independent paging of windows 2 and 3
- Controlled via `GSCFG0` bit 3 (`M_EXPAG`)

### 2.3 SD Card / MP3 Interface (ports.v:105-115)
- Combined SD card and VS1001 MP3 control
- Ports:
  - `SCTRL` (`0x11`): SD/MP3 control
  - `SSTAT` (`0x12`): SD/MP3 status (read)
  - `SD_SEND/SD_READ` (`0x13`): SD card write/read
  - `MD_SEND/SD_RSTR` (`0x14`): MP3 data write (shared with SD reset!)
  - `MC_SEND/MC_READ` (`0x15`): VS1001 command write/read

### 2.4 VS1001 MP3 Decoder
- Integrated VLSI VS1001K hardware MP3 decoder
- SCI (control) via port 0x15, SDI (data) via port 0x14
- Hardware MPEG 1/2/2.5 Layer I/II/III decoding

### 2.5 DMA Controller
- Direct memory access for high-speed transfers
- Ports:
  - `DMA_MOD` (`0x1B`): DMA mode select
  - `DMA_HAD` (`0x1C`): Address high (5 bits)
  - `DMA_MAD` (`0x1D`): Address middle
  - `DMA_LAD` (`0x1E`): Address low
  - `DMA_CST` (`0x1F`): DMA control/status

---

## 3. Port Differences

### 3.1 Original GS Ports (Preserved)
| Port | Function | Compatibility |
|:-----|:---------|:--------------|
| `0x00` | MPAG (page select) | ✓ Same |
| `0x01` | Read command | ✓ Same |
| `0x02` | Clear status bit 7 | ✓ Same |
| `0x03` | Write data / set status | ✓ Same |
| `0x04` | Read status | ✓ Same |
| `0x05` | Clear status bit 0 | ✓ Same |
| `0x06–0x09` | Volume ch 1–4 | ✓ Same |
| `0x0A` | Copy page→status | ✓ Same |
| `0x0B` | Copy vol→status | ✓ Same |

### 3.2 NeoGS Extended Ports (from ports.v:105-115)
| Port | Function | Notes |
|:-----|:---------|:------|
| `0x0F` | GSCFG0 (config) | Mode bits: NOROM, RAMRO, 8CHANS, EXPAG, CLKSEL, PAN4CH |
| `0x10` | MPAGEX (extended page) | Page select for 3rd window |
| `0x11` | SCTRL | SD/MP3 control |
| `0x12` | SSTAT (read) | SD/MP3 status |
| `0x13` | SD_SEND/SD_READ | SD card write/read |
| `0x14` | MD_SEND / SD_RSTR | MP3 data write (shared port!) |
| `0x15` | MC_SEND/MC_READ | VS1001 command write/read |
| `0x16–0x19` | Volume ch 5–8 | Always accessible (not gated by M_8CHANS) |
| `0x1B–0x1F` | DMA registers | From CPLD/Unreal, not in fpgaD |

### 3.3 GSCFG0 Register (Port `0x0F`)
| Bit | Name | Function |
|:----|:-----|:---------|
| 0 | M_NOROM | 1 = ROM disabled, RAM at `0x0000` |
| 1 | M_RAMRO | 1 = RAM pages 0,1 read-only |
| 2 | M_8CHANS | 1 = 8-channel mode |
| 3 | M_EXPAG | 1 = Extended paging mode |
| 4-5 | CLKSEL | Clock: 00=24MHz, 01=12MHz, 10=20MHz, 11=10MHz (reset) |
| 6 | M_PAN4CH | 1 = 4-channel pan mode |
| 7 | M_INV7B | 1 = invert bit 7 of samples before DAC |

---

## 4. Memory Map Differences

### 4.1 Original GS
```
0x0000–0x3FFF: ROM page 0
0x4000–0x7FFF: RAM page 3 (fixed)
0x8000–0xBFFF: ROM/RAM page N
0xC000–0xFFFF: ROM page 1 / RAM page N|1
```

### 4.2 NeoGS with EXPAG
```
0x0000–0x3FFF: RAM page 0 (when NOROM=1)
0x4000–0x7FFF: RAM page 3 (fixed)
0x8000–0xBFFF: RAM page N (via MPAG)
0xC000–0xFFFF: RAM page M (via MPAGEX)
```

---

## 5. Firmware Differences

### 5.1 ROM Versions
| Version | GS | NeoGS | Notes |
|:--------|:---|:------|:------|
| 1.04 | ✓ | ✓ | Original |
| 1.05a | ✓ | ✓ | psb bugfixes 2007 |
| 1.05b | ✓ | ✓ | Additional fixes 2015 |
| 1.09 | — | ✓ | NeoGS-specific features |

### 5.2 New ROM 1.09 Features
- SD card boot support
- MP3 playback commands
- Extended sample addressing (21-bit DMA)
- 8-channel mixing

---

## 6. Emulation Implications

### 6.1 For GS-Only Emulation (P0)
- Ignore all ports ≥ `0x0F` (GSCFG0 and DMA are NeoGS-only)
- 4-channel mode only (no 8ch, EXPAG, CLKSEL)
- 128–512 KB RAM limit

### 6.2 For NeoGS Emulation (P2)
- Full GSCFG0 register support
- 8-channel audio mixing
- SD card emulation (SPI state machine)
- VS1001 stub (or full MP3 decode)
- DMA controller
- Extended memory (up to 4 MB)

### 6.3 Compatibility Testing
- All original GS software should work on NeoGS emulation
- NeoGS-specific software requires extended features
- Detection: Check for GSCFG0 access or SD card probing

---

## 7. References

### Local Materials

- [`fpgaD/main.v`](fpgaD/main.v) — FPGA top-level module (confirms external Z80 bus)
- [`NGS_b_scheme.pdf`](NGS_b_scheme.pdf) — NeoGS schematic
- [`ngs_b.pdf`](ngs_b.pdf) — NeoGS rev B documentation
- [`ngs_c_cpld.pdf`](ngs_c_cpld.pdf) — CPLD variant documentation

### Web Sources

- [NedoPC NeoGS](http://nedopc.com/gs/ngs.php) — Official NeoGS page
- [NeoGS GitHub Mirror](https://github.com/alfishe/neogs) — Complete source repository
- [NeoGS Programming Manual](https://raw.githubusercontent.com/alfishe/neogs/master/docs/NGS_prm/ngspgm_01a.pdf) — `ngspgm_01a.pdf`
- [ports.inc](https://raw.githubusercontent.com/alfishe/neogs/master/docs/ports.inc) — Port definitions (assembly)
- [VS1001K Datasheet](vs1001_datasheet.pdf) — MP3 decoder specs ([VLSI source](https://www.vlsi.fi/fileadmin/datasheets/vs1001.pdf))
- [Unreal Speccy gsz80.cpp](https://github.com/alfishe/unrealspeccy/blob/master/gsz80.cpp) — Reference implementation
- [minimp3](https://github.com/lieff/minimp3) — Recommended MP3 decoder library (CC0)

> **Note:** NedoPC SVN may require authentication. Port definitions are available locally in
> [`fpgaD/ports/ports.v`](fpgaD/ports/ports.v).

---

*Document created 2026-09-19 for Unreal-NG emulator implementation planning.*
