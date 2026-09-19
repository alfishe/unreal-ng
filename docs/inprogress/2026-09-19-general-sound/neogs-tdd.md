# NeoGS Sound Card Implementation

**Priority:** P2  
**Depends on:** GS implementation (P0)  
**Scope:** NeoGS-specific extensions over base General Sound

---

## 1. Overview

NeoGS is an enhanced General Sound card developed by the NedoPC group in 2008. It uses a **physical Z80 CPU** controlled by an FPGA that handles memory mapping, ZX-BUS interface, and peripheral control.

### 1.1 NeoGS Hardware Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│ NeoGS Board                                                     │
│  ┌───────────┐    ┌────────────────────────────────────────┐   │
│  │  Z80 CPU  │◄──►│  FPGA (Altera EP1K30/50)               │   │
│  │  12/24MHz │    │  - Memory mapping                      │   │
│  └───────────┘    │  - Port decoding                       │   │
│       │           │  - ZX-BUS interface                    │   │
│       ▼           │  - Audio DAC control                   │   │
│  ┌───────────┐    │  - SD card SPI                         │   │
│  │  2-4 MB   │    │  - VS1001 MP3 control                  │   │
│  │  SRAM     │    └────────────────────────────────────────┘   │
│  └───────────┘              │           │          │           │
│       │                     ▼           ▼          ▼           │
│  ┌───────────┐    ┌─────────────┐  ┌────────┐  ┌────────┐     │
│  │  512 KB   │    │  Audio DAC  │  │ SD Card│  │ VS1001 │     │
│  │  Flash    │    │  (I2S)      │  │  Slot  │  │ MP3    │     │
│  └───────────┘    └─────────────┘  └────────┘  └────────┘     │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 NeoGS vs Original GS

| Feature | General Sound | NeoGS |
|:--------|:--------------|:------|
| Architecture | Discrete Z80 + ~40 ICs | **Z80 CPU + FPGA** (not soft-core!) |
| CPU | Z80H @ 12 MHz | Z80H @ 12/24 MHz (switchable) |
| ROM | 32 KB EPROM | 512 KB Flash |
| RAM | 128–512 KB SRAM | 2–4 MB SRAM |
| Channels | 4 × 8-bit | 8 × 8-bit |
| SD Card | No | Yes (SPI interface) |
| MP3 | No | Yes (VS1001K hardware decoder) |
| DMA | No | Yes (21-bit addressing) |

### 1.3 Hardware Architecture Details

The FPGA/CPLD does **NOT** contain a Z80 soft-core. It acts purely as programmable glue logic:

```
Physical Z80 CPU ◄──► FPGA/CPLD ◄──┬──► SRAM (2-4 MB)
                     (glue logic)  ├──► Flash ROM (512 KB)
                                   ├──► ZX-BUS interface
                                   ├──► I2S Audio DAC
                                   ├──► SD Card (SPI)
                                   └──► VS1001K MP3 (SPI+data)
```

From `fpgaD/main.v`:
- Z80 address/data buses are **external inputs** (`input [15:0] a`, `inout [7:0] d`)
- FPGA generates memory chip-selects, address lines A14-A18
- FPGA handles port decoding and ZX-BUS arbitration

### 1.4 Bill of Materials (Key Components)

| Component | Part | Role |
|:----------|:-----|:-----|
| CPU | Z80H / Z84C00 | **Physical chip** (not soft-core!), 12/24 MHz |
| Logic | [Altera ACEX 1K](materials/neogs/acex.pdf) (EP1K30/50) | Glue: memory map, ports, bus control |
| RAM | 4 × 512KB SRAM | Up to 4 MB total |
| Flash | 512 KB | Firmware + user data |
| Audio DAC | [MA8201](materials/neogs/ma8201_dac.pdf) | I2S stereo DAC (`dac_bitck`, `dac_lrck`, `dac_dat`) |
| MP3 Decoder | [VS1001K](materials/neogs/vs1001_datasheet.pdf) | Hardware MPEG decoder, SPI control |
| SD Slot | Standard SD/SDHC | SPI mode (`sd_clk`, `sd_cs`, `sd_do`, `sd_di`) |

### 1.5 VS1001K MP3 Decoder Chip

The [VS1001K](https://www.vlsi.fi/fileadmin/datasheets/vs1001.pdf) is a hardware MPEG audio decoder from VLSI Solution:

| Spec | Value |
|:-----|:------|
| Formats | MPEG 1/2 Layer III (MP3), MPEG 1/2 Layer I/II |
| Sample rates | 8–48 kHz |
| Bitrates | 8–320 kbps CBR/VBR |
| Interface | SPI control (SCI) + serial data (SDI) |
| Output | I2S / analog |

**FPGA signals (from `main.v`):**
```verilog
// Control interface (SPI)
output ma_clk;      // SCI clock
output ma_cs;       // SCI chip select
output ma_do;       // SCI data out (to VS1001)
input  ma_di;       // SCI data in (from VS1001)

// Data interface (SDI - MP3 stream)
output mp3_xreset;  // VS1001 reset
input  mp3_req;     // Data request (DREQ)
output mp3_clk;     // SDI clock
output mp3_dat;     // SDI data
output mp3_sync;    // SDI sync
```

### 1.6 Design Goals

1. **Extend GS implementation** — NeoGS is a superset; GS code handles 90% of work
2. **Config-driven features** — Enable via `[NGS] RamSize`, etc.
3. **Optional MP3/SD** — Stubs acceptable if VS1001/SD not needed
4. **Full GS compatibility** — All GS software must work unchanged

---

## 2. Extended Hardware Features

### 2.1 GSCFG0 Register (Port 0x0F)

| Bit | Name | Function |
|:----|:-----|:---------|
| 0 | M_NOROM | 1 = ROM disabled, RAM at 0x0000 |
| 1 | M_RAMRO | 1 = RAM pages 0,1 read-only |
| 2 | M_8CHANS | 1 = 8-channel mode |
| 3 | M_EXPAG | 1 = Extended paging mode |
| 4-5 | CLKSEL | Clock select (see table below) |
| 6 | M_PAN4CH | 1 = 4ch panning (each channel on L+R with two volumes) |
| 7 | M_INV7B | 1 = invert bit 7 of samples before DAC (ports.inc:100) |

**CLKSEL frequency table (from ports.inc:92-95):**
| CLKSEL | Value | Frequency |
|:-------|:------|:----------|
| 00 | 0x00 | 24 MHz |
| 01 | 0x10 | 12 MHz |
| 10 | 0x20 | 20 MHz |
| 11 | 0x30 | **10 MHz** (reset default) |

**Readback:** mask 0x7F (bit 7 reads as 0 per ports.v readback).  
**Reset value:** 0x30 = clksel=3 → **10 MHz at power-up** (not 20 MHz).

> **Note:** Unreal ignores CLKSEL (`ngs_cfg0 = val & 0x3F`, gsz80.cpp:371). For P2 emulation,
> use current CLKSEL to derive GS CPU frequency for cycle accounting.

### 2.2 Extended Paging (MPAGEX)

Port 0x10 enables independent paging of window 3 (0xC000–0xFFFF):
```cpp
if (ngs_cfg0 & M_EXPAG)
{
    // Window 2 uses MPAG (port 0x00)
    // Window 3 uses MPAGEX (port 0x10) independently
}
```

### 2.3 Additional Volume Registers

Ports 0x16–0x19 for channels 5–8 are **always writable** (FPGA does not gate on M_8CHANS):
```cpp
// ports.v registers these unconditionally — 8ch interpretation is in sound path
case 0x16: case 0x17: case 0x18: case 0x19:
    gsvol[4 + (port & 3)] = val & 0x3F;
    break;
```

### 2.3.1 8-Channel DAC Memory Layout (ports.v:534-537)

Both modes use **0x100-byte windows**, repeating every 0x800 across 0x6000-0x7FFF:
```cpp
// 4ch mode: snd_addr = {1'b0, a[9:8]}
channel = (addr >> 8) & 3;   // 0x100-byte windows

// 8ch mode: snd_addr = a[10:8]  
channel = (addr >> 8) & 7;   // 0x100-byte windows
```

Stereo mapping: ch 1,2,5,6 → L; ch 3,4,7,8 → R (sound_main2.v:9-14).

### 2.4 DMA Controller

| Port | Function |
|:-----|:---------|
| 0x1B | DMA_MOD — DMA mode select |
| 0x1C | DMA_HAD — Address high (5 bits) |
| 0x1D | DMA_MAD — Address middle |
| 0x1E | DMA_LAD — Address low |
| 0x1F | DMA_CST — DMA control/status |

DMA enables 21-bit addressing (2 MB) for sample data.

> **Provenance:** fpgaD does NOT contain DMA (zxbus.v:15 has "TODO: add DMA"). These
> registers come from later CPLD revisions and Unreal's implementation (gsz80.cpp:481-502).
> For P2 implementation, follow Unreal's DMA logic.

### 2.5 SD Card / MP3 Interface (from ports.v:105-115)

| Port | R/W | Function |
|:-----|:----|:---------|
| 0x11 | R/W | SCTRL — SD/MP3 control |
| 0x12 | R | SSTAT — SD/MP3 status |
| 0x13 | W | SD_SEND — Write byte to SD |
| 0x13 | R | SD_READ — Read byte from SD |
| 0x14 | W | MD_SEND — MP3 data write (same port as SD_RSTR!) |
| 0x15 | W | MC_SEND — MP3 SCI command write |
| 0x15 | R | MC_READ — MP3 SCI command read |

> **Note:** Port 0x14 is shared between SD_RSTR and MD_SEND per ports.v:112 comment.

### 2.6 VS1001 MP3 Decoder Emulation

#### VS1001 SPI Protocol

The VS1001 uses two interfaces:
1. **SCI (Serial Control Interface)** — Register read/write, 16-bit transfers
2. **SDI (Serial Data Interface)** — MP3 data stream, byte transfers

Key registers:
| Reg | Name | Description |
|:----|:-----|:------------|
| 0x00 | MODE | Software reset, test modes |
| 0x03 | CLOCKF | Clock frequency config |
| 0x05 | STATUS | Decoder status |
| 0x08 | DECODE_TIME | Playback position (seconds) |
| 0x09 | AUDATA | Sample rate, stereo mode |
| 0x0B | VOL | Volume (0=max, 0xFEFE=min) |

**DREQ signal:** When high, VS1001 can accept at least 32 bytes of MP3 data.

#### Emulation Implementation: minimp3

We will use **[minimp3](https://github.com/lieff/minimp3)** — a single-header MP3 decoder:

| Property | Value |
|:---------|:------|
| Size | Single header (~90 KB) |
| API | `mp3dec_t`, `mp3dec_decode_frame()` |
| Quality | ISO conformant |
| Performance | SSE/NEON SIMD optimized |
| Compatibility | MPEG 1/2/2.5 Layer I/II/III |

**Integration:**
```cpp
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

class VS1001Emulator {
public:
    void reset();
    
    // SCI interface
    void sciWrite(uint8_t addr, uint16_t val);
    uint16_t sciRead(uint8_t addr);
    
    // SDI interface - MP3 data feed
    void sdiWrite(uint8_t byte);
    bool dreq() const { return _ringBuffer.space() >= 32; }
    
    // Audio output (called by mixer)
    void render(int16_t* buffer, size_t samples);

private:
    mp3dec_t _mp3d;
    RingBuffer<uint8_t, 4096> _ringBuffer;  // MP3 input buffer
    int16_t _pcmBuffer[MINIMP3_MAX_SAMPLES_PER_FRAME];
    uint16_t _regs[16];
    uint32_t _decodeTime = 0;
};
```

**Decoding flow:**
1. Firmware writes MP3 data via port 0x14 (MD_SEND) → `_ringBuffer`
2. When buffer has enough data, call `mp3dec_decode_frame()`
3. Mix decoded PCM into audio output alongside DAC channels

**Alternative (deferred):** libmpg123 for hardware accuracy testing.

---

## 3. Implementation Strategy

### 3.1 Extend SoundChip_GeneralSound

```cpp
class SoundChip_NeoGS : public SoundChip_GeneralSound
{
public:
    SoundChip_NeoGS(EmulatorContext* context);

protected:
    // Override internal port handling
    uint8_t gsIORead(uint8_t port) override;
    void gsIOWrite(uint8_t port, uint8_t val) override;

private:
    // NGS-specific state
    uint8_t _gscfg0 = 0x30;  // Reset: clksel=3 (10 MHz)
    uint8_t _mpagex = 0;
    
    // Additional channels
    uint8_t _channelVol8[8];
    uint8_t _channelData8[8];
    
    // DMA
    uint8_t _dmaMode = 0;
    uint32_t _dmaAddr = 0;
    
    // SD Card
    SDCardEmulator _sdCard;
    
    // VS1001 (stub or full)
    VS1001Emulator _mp3;
};
```

### 3.2 Config Integration

```ini
[NGS]
RamSize=2048       ; KB (512, 1024, 2048, 4096)
SDCardImage=       ; Path to SD card image
MP3Support=stub    ; stub | software | none
```

### 3.3 Feature Detection

NeoGS firmware probes for features:
```cpp
// Software checks GSCFG0 accessibility
OUT 0x0F, value
IN  0x0F -> should return value & 0x7F  // Bit 7 always reads 0
```

---

## 4. Threading Model

Same as GS — runs in emulator thread, synchronized via port access.
See GS TDD §5.5 for details.

---

## 5. TTD Integration

Extends GS state with:
- `_gscfg0`, `_mpagex`
- 8-channel volumes/samples
- DMA registers
- SD card state (if image loaded)

---

## 6. Test Plan

### 6.1 Compatibility Tests

```cpp
TEST(NeoGS, GSCompatibility_BasicPlayback)
TEST(NeoGS, GSCompatibility_AllGSCommands)
TEST(NeoGS, GSCompatibility_NoNGSPorts)
```

### 6.2 Extension Tests

```cpp
TEST(NeoGS, GSCFG0_ReadWrite)
TEST(NeoGS, GSCFG0_8ChannelMode)
TEST(NeoGS, MPAGEX_IndependentPaging)
TEST(NeoGS, DMA_AddressSetup)
TEST(NeoGS, SDCard_SPIProtocol)
```

---

## 7. Implementation Checklist

- [ ] Extend `SoundChip_GeneralSound` with NeoGS features
- [ ] GSCFG0 register handling
- [ ] MPAGEX paging
- [ ] 8-channel audio mixing
- [ ] DMA controller
- [ ] SD card SPI emulation (stub first)
- [ ] VS1001 stub
- [ ] Config integration (`[NGS]` section)
- [ ] TTD state extension
- [ ] Compatibility test suite
- [ ] Extension test suite

---

## 8. References

### Local Materials (`materials/neogs/`)

| File | Source | Description |
|:-----|:-------|:------------|
| [`NEOGS-DIFFERENCES.md`](materials/neogs/NEOGS-DIFFERENCES.md) | — | Key differences vs original GS |
| [`ngspgm_01a.pdf`](materials/neogs/ngspgm_01a.pdf) | [GitHub](https://raw.githubusercontent.com/alfishe/neogs/master/docs/NGS_prm/ngspgm_01a.pdf) | Programming Manual (16pp, Russian) |
| [`ports.inc`](materials/neogs/ports.inc) | [GitHub](https://raw.githubusercontent.com/alfishe/neogs/master/docs/ports.inc) | Port definitions (assembly) |
| [`GS_PORTS.TXT`](materials/neogs/GS_PORTS.TXT) | [GitHub](https://raw.githubusercontent.com/alfishe/neogs/master/docs/_old_gs/GS_PORTS.TXT) | Original GS port reference |
| [`ngs_c_cpld.pdf`](materials/neogs/ngs_c_cpld.pdf) | NedoPC | CPLD variant docs |
| [`ngs_b.pdf`](materials/neogs/ngs_b.pdf) | NedoPC | Rev B full documentation |
| [`NGS_b_scheme.pdf`](materials/neogs/NGS_b_scheme.pdf) | NedoPC | NeoGS schematic |
| [`fpgaD/`](materials/neogs/fpgaD/) | NedoPC | FPGA Verilog source |
| [`ngsrom109/`](materials/neogs/ngsrom109/) | NedoPC | ROM v1.09 with flasher |

### Web Sources

| Source | URL |
|:-------|:----|
| NeoGS GitHub Mirror | https://github.com/alfishe/neogs |
| NedoPC NeoGS Page | http://nedopc.com/gs/ngs.php |
| VS1001K Datasheet | [local](materials/neogs/vs1001_datasheet.pdf) · [VLSI](https://www.vlsi.fi/fileadmin/datasheets/vs1001.pdf) |
| minimp3 Library | https://github.com/lieff/minimp3 |

### Reference Implementation

| Emulator | Files | Notes |
|:---------|:------|:------|
| Unreal Speccy | [`gsz80.cpp`](https://github.com/alfishe/unrealspeccy/blob/master/gsz80.cpp) | Full NGS + SD + VS1001 |
| Unreal Speccy | [`gshlbass.cpp`](https://github.com/alfishe/unrealspeccy/blob/master/gshlbass.cpp) | VS1001 emulation (BASS) |

---

*Document created 2026-09-19 for Unreal-NG emulator implementation planning.*
