# General Sound / NeoGS Reference Materials

Reference documentation, schematics, and firmware sources for implementing GS/NeoGS emulation.

---

## Folder Structure

```
materials/
├── gs/                          # Original General Sound
│   ├── gs_prog.pdf              # Official programming manual (31 pages, Russian)
│   ├── gs-programming-guide.md  # Compiled English guide
│   └── gs-firmware/             # GitHub: psbhlw/gs-firmware
│       ├── firmware/            # ROM source (Z80 assembly)
│       ├── docs/                # MOD/UST format specs
│       └── sch/                 # Schematics (PDF, PNG, PCB)
│
├── neogs/                       # NeoGS FPGA-based successor
│   ├── NEOGS-DIFFERENCES.md     # Key differences vs GS
│   ├── ngs_c_cpld.pdf           # Rev C CPLD documentation
│   ├── ngs_b.pdf                # Rev B full documentation
│   ├── NGS_b_scheme.pdf         # Schematic
│   ├── NGS_b_mount.pdf          # Component placement
│   ├── fpgaD/                   # FPGA Verilog source
│   │   ├── main.v               # Top-level module
│   │   ├── sound/               # DAC and mixing
│   │   ├── memmap/              # Memory mapping
│   │   ├── ports/               # Port decoding
│   │   └── zxbus/               # ZX-BUS interface
│   └── ngsrom109/               # ROM v1.09 with flasher
│
└── README.md                    # This file
```

---

## Original General Sound (`gs/`)

### Programming Documentation

| File | Description |
|:-----|:------------|
| `gs_prog.pdf` | Official GS Programming Manual v1.03 (31 pages, Russian) |
| `gs-programming-guide.md` | Compiled English guide with command reference |

### Firmware Source (`gs-firmware/`)

- **Source:** [GitHub: psbhlw/gs-firmware](https://github.com/psbhlw/gs-firmware)
- **Contents:**
  - `firmware/` — ROM source code (Z80 assembly)
  - `docs/` — MOD format specification, UST format
  - `sch/` — Schematic (PDF, PNG, PCB files)

### Schematics (`gs-firmware/sch/`)

| File | Description |
|:-----|:------------|
| `GS_schematic.pdf` | Full circuit schematic |
| `GS_footprint.pdf` | PCB footprint |
| `gs_sch_fixed.png` | Corrected schematic image |
| `general sound.pcb` | PCB layout file |

---

## NeoGS (`neogs/`)

### Documentation

| File | Source | Description |
|:-----|:-------|:------------|
| `NEOGS-DIFFERENCES.md` | — | Key differences vs original GS |
| `vs1001_datasheet.pdf` | [VLSI](https://www.vlsi.fi/fileadmin/datasheets/vs1001.pdf) | VS1001K MP3 decoder datasheet |
| `acex.pdf` | [GitHub](https://raw.githubusercontent.com/alfishe/neogs/master/docs/acex.pdf) | Altera ACEX 1K (EP1K30/50) FPGA datasheet |
| `ma8201_dac.pdf` | [GitHub](https://raw.githubusercontent.com/alfishe/neogs/master/docs/_chips/ma8201.pdf) | MA8201 I2S audio DAC datasheet |
| `ngs_c_cpld.pdf` | NedoPC | NeoGS rev C CPLD documentation (4 pages) |
| `ngs_b.pdf` | NedoPC | NeoGS rev B full documentation |
| `NGS_b_scheme.pdf` | NedoPC | NeoGS schematic |
| `NGS_b_mount.pdf` | NedoPC | Component placement diagram |
| `ngsrom109/version.pdf` | NedoPC | ROM v1.09 changelog |

### Programming Reference (from [GitHub mirror](https://github.com/alfishe/neogs))

| File | Source | Description |
|:-----|:-------|:------------|
| `ngspgm_01a.pdf` | [GitHub](https://raw.githubusercontent.com/alfishe/neogs/master/docs/NGS_prm/ngspgm_01a.pdf) | NeoGS Programming Manual (16 pages, Russian) |
| `ports.inc` | [GitHub](https://raw.githubusercontent.com/alfishe/neogs/master/docs/ports.inc) | Port definitions (assembly equates) |
| `GS_PORTS.TXT` | [GitHub](https://raw.githubusercontent.com/alfishe/neogs/master/docs/_old_gs/GS_PORTS.TXT) | Original GS port reference |

### FPGA Source (`fpgaD/`)

Complete Verilog implementation:

| Folder | Contents |
|:-------|:---------|
| `main.v` | Top-level module |
| `sound/` | DAC (`sound_dac2.v`), mixing (`sound_main2.v`) |
| `memmap/` | Memory mapping logic |
| `ports/` | Port decoding |
| `zxbus/` | ZX-BUS interface |
| `quartus/` | Altera Quartus project files |

### Firmware (`ngsrom109/`)

| File | Description |
|:-----|:------------|
| `full_ngs.rom` | NeoGS ROM v1.09 |
| `flasher.scl` | Flash update utility |
| `readme.txt` | Version notes |

---

## Web Sources

### Official

| Source | URL |
|:-------|:----|
| NedoPC NeoGS Page | http://nedopc.com/gs/ngs.php |
| GS Firmware Repository | https://github.com/psbhlw/gs-firmware |
| Spectrum Computing DB | https://spectrumcomputing.co.uk/entry/1000171/Hardware/General_Sound |

### Community

| Source | URL |
|:-------|:----|
| Wikipedia (Russian) | https://ru.wikipedia.org/wiki/General_Sound |
| ZX-News #26 Article | https://zxpress.ru/ru/ezines/zx-news/26/general-sound-muzykalnaya-karta-dlya-zx-spectrum-s-podderzhkoy-8-bitnogo-zvuka-4-kanalov-i-mod |
| ZX-PK Schematic Thread | https://zx-pk.ru/threads/6007-general-sound-(skhema).html |

### Reference Emulators

| Emulator | GS Files | Notes |
|:---------|:---------|:------|
| Unreal Speccy | `gsz80.cpp`, `gshle.cpp`, `gs.h` | Full Z80 + NGS support |
| Xpeccy | `src/libxpeccy/sound/gs.c` | Simplified GS model |

---

## Key Specifications

### General Sound

| Spec | Value |
|:-----|:------|
| CPU | Z80H @ 12 MHz |
| ROM | 32 KB (2 × 16 KB pages) |
| RAM | 128–512 KB |
| Channels | 4 × 8-bit DAC |
| Volume | 6-bit per channel (64 levels) |
| Sample Rate | 37.5 kHz |
| Ports | #B3 (data), #BB (command), #33 (control) |

### NeoGS Enhancements

| Feature | Original GS | NeoGS |
|:--------|:------------|:------|
| Architecture | Discrete Z80 + ICs | **Physical Z80** + FPGA/CPLD glue |
| CPU Speed | 12 MHz | 12/24 MHz (switchable) |
| RAM | 128–512 KB | 2–4 MB |
| ROM | 32 KB | 512 KB Flash |
| Channels | 4 | 8 |
| SD Card | No | Yes (SPI) |
| MP3 | No | Yes (VS1001K hardware decoder) |
| DMA | No | Yes (21-bit addressing) |

> **Note:** The FPGA/CPLD does NOT contain a Z80 soft-core. It acts as glue logic only.

---

## VS1001K MP3 Decoder

The NeoGS includes a [VLSI VS1001K](https://www.vlsi.fi/fileadmin/datasheets/vs1001.pdf) hardware MP3 decoder:

| Spec | Value |
|:-----|:------|
| Formats | MPEG 1/2/2.5 Layer I/II/III |
| Bitrates | 8–320 kbps |
| Sample rates | 8–48 kHz |
| Interface | SPI (control) + SDI (data) |

**Emulation strategy:** Use [minimp3](https://github.com/lieff/minimp3) (CC0/public domain, single-header).

---

*Materials collected 2026-09-19 for Unreal-NG emulator implementation.*
