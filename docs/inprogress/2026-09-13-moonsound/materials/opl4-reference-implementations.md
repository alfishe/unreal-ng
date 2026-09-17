# OPL4 Reference Implementations

Research on available OPL4 (YMF278B) emulation implementations for potential use in MoonSound support.

**Date**: 2026-09-17  
**Status**: Research complete

## Summary

| Implementation | License | OPL4 Support | FM Accuracy | PCM/Wavetable | Testing Status |
|----------------|---------|--------------|-------------|---------------|----------------|
| **ymfm** | BSD-3 | Full | High | Basic | ⚠️ Limited OPL4 testing |
| **openMSX/VGMPlay** | GPL-2 | Full | High | **Best** | ✅ ValleyBell fixes |
| Nuked-OPL3 | LGPL-2.1 | FM only | Die-shot | No | ✅ Complete |
| MAME (old) | BSD-3 | Partial | Medium | Incomplete | Superseded |

### ⚠️ Important: OPL4 Testing Status

**ymfm OPL4 is less tested than its OPN/OPM cores**. The author states:

> "The goal of the ymfm cores is not 100% digital accuracy, as achieving that would require full emulation of the pipelines."

**openMSX/VGMPlay has the most battle-tested OPL4 wavetable code** thanks to ValleyBell's fixes addressing real-world bugs found in MSX game music.

## Recommended: ymfm

### Overview

**Repository**: [github.com/aaronsgiles/ymfm](https://github.com/aaronsgiles/ymfm)  
**Author**: Aaron Giles (MAME project lead)  
**License**: BSD-3-Clause  
**Language**: C++ (header-only library)

ymfm is a collection of BSD-licensed Yamaha FM sound cores written by Aaron Giles. Originally developed for MAME during summer/fall 2020, it was released as a standalone library under permissive BSD license.

### Supported Chips

| Family | Chips | Notes |
|--------|-------|-------|
| OPM | YM2151 | 8-channel FM |
| OPN | YM2203, YM2608, YM2610, YM2612, YM3438, YMF276 | FM + SSG/ADPCM |
| OPL | YM3526, Y8950, YM3812, YMF262, YMF278B, YMF289B, YMF988 | FM + Wavetable |
| OPQ | YMF271 | 12-channel FM |
| OPZ | YM2414 | TX81Z |

### YMF278B (OPL4) Specifics

The YMF278B implementation in ymfm provides:

- **FM synthesis**: 18 channels (OPL3-compatible)
  - 6 four-operator channels, or
  - 18 two-operator channels
  - Full OPL3 register compatibility
  
- **Wavetable/PCM synthesis**: 24 channels
  - 12-bit and 16-bit sample support
  - Sample ROM (YRW-801) and RAM support
  - Envelope generation
  - LFO (vibrato/tremolo)
  - Pseudo-reverb effect

### Integration

ymfm is header-only C++:

```cpp
#include "ymfm_opl.h"

// Create OPL4 instance
ymfm::ymf278b opl4;
ymfm::ymf278b::output_data output;

// Reset
opl4.reset();

// Write register
opl4.write_address(0x00);  // Select register
opl4.write_data(0x20);     // Write value

// Generate samples
opl4.generate(&output);
// output.data[0] = left, output.data[1] = right
```

### MAME Integration

MAME replaced its legacy YMF278B engine with ymfm in version 0.231:

> "MAME formally replaced the old YMF278B engine with the one from ymfm, re-evaluated envelope calculations, and changed the way FM resampling is computed to be more precise."

### Source Files

Key files in ymfm for OPL4:

| File | Description |
|------|-------------|
| `ymfm_opl.h` | OPL family header (includes YMF278B) |
| `ymfm_opl.cpp` | OPL implementation |
| `ymfm_fm.h` | FM engine core |
| `ymfm_fm.ipp` | FM engine implementation |
| `ymfm_pcm.h` | PCM/wavetable engine |

### Known OPL4 Wavetable Issues (Fixed by ValleyBell)

These bugs were found in real MSX games and fixed in openMSX/VGMPlay:

| Issue | Symptom | Root Cause |
|-------|---------|------------|
| Applause fade | Sound plays forever instead of fading | Envelope release handling |
| Cymbal swell | Different swell times vs hardware | Attack rate calculation |
| Lizard Star SFX | Missing sound effect | OPL4 hardware glitch not emulated |
| Bombaman Boss | Volume decreases too slowly | Decay rate computation |

**Recommendation**: For accurate OPL4 wavetable, cross-reference with VGMPlay's `ymf278b.c` which contains ValleyBell's fixes.

---

## Alternative: Nuked-OPL3

### Overview

**Repository**: [github.com/nukeykt/Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3)  
**Author**: Alexey Khokholov (nukeykt)  
**License**: LGPL-2.1  
**Language**: C/C++

Nuked-OPL3 is a cycle-accurate OPL3 (YMF262) emulator based on analysis of die shots. It is considered the most accurate OPL3 implementation available.

### Limitations for OPL4

**Nuked-OPL3 does NOT support OPL4**. It only emulates:
- YMF262 (OPL3)
- CT1747 (OPL3 clone)

The FM part of OPL4 is register-compatible with OPL3, so theoretically Nuked-OPL3 could handle the FM registers. However:
- No wavetable/PCM synthesis
- No YMF278B-specific registers
- No sample ROM/RAM handling

### Potential Use Case

Could be used as a **reference for FM accuracy verification** against ymfm, but not as a complete OPL4 solution.

### Die-Shot Analysis

Nuked-OPL3 is based on actual chip die analysis:

> "The emulator is written based on analysis of the chip die shots, and provides a cycle-accurate emulation of the YMF262 chip."

This methodology provides the highest possible accuracy for FM synthesis behavior.

---

## openMSX Implementation

### Overview

**Repository**: [github.com/openMSX/openMSX](https://github.com/openMSX/openMSX)  
**License**: GPL-2.0  
**Language**: C++

openMSX contains a complete YMF278B emulation for MoonSound support.

### Key Files

```
src/sound/
├── YMF278.cc       # Main YMF278B implementation
├── YMF278.hh       # Header
└── YMF262.cc       # OPL3 FM core
```

### ValleyBell Fixes

ValleyBell (VGMRips contributor) made accuracy improvements:

> "ValleyBell did some nice fixes on the OPL4 emulation in VGMPlay that should be ported to OpenMSX since they solve longstanding bugs in the emulation."

Reference: [openMSX Issue #1114](https://github.com/openMSX/openMSX/issues/1114)

### Limitations

- GPL-2.0 license (copyleft)
- Tightly integrated with openMSX architecture
- Not designed as standalone library

---

## VGMPlay YMF278B (Most Accurate PCM)

### Overview

**Repository**: [github.com/vgmrips/vgmplay-legacy](https://github.com/vgmrips/vgmplay-legacy)  
**File**: `VGMPlay/chips/ymf278b.c`  
**License**: GPL-2.0  
**Author**: ValleyBell (accuracy fixes)

This is the **most accurate OPL4 wavetable implementation** available, containing fixes derived from comparing VGM recordings against real hardware.

### Key Fixes by ValleyBell

From [openMSX Issue #1114](https://github.com/openMSX/openMSX/issues/1114):

- Envelope release handling (applause fade bug)
- Attack rate calculation (cymbal swell timing)
- Hardware glitch emulation (Lizard Star SFX)
- Decay rate computation (Bombaman Boss volume)

### Status

- **Reference implementation** for OPL4 wavetable accuracy
- GPL-2.0 licensed (copyleft concern)
- Code should be studied even if not directly used

### Critical Code Sections

Study these sections in `ymf278b.c`:

```c
// Envelope rate calculation
// Attack/Decay/Release timing
// Sample interpolation
// LFO (vibrato/tremolo) handling
// Pseudo-reverb effect
```

---

## Comparison: ymfm vs Nuked-OPL3

| Aspect | ymfm | Nuked-OPL3 |
|--------|------|------------|
| **License** | BSD-3 | LGPL-2.1 |
| **OPL4 FM** | Yes | OPL3 only |
| **OPL4 PCM** | Yes | No |
| **Accuracy** | High (MAME reference) | Die-shot cycle-accurate |
| **Integration** | Header-only, easy | Single file, easy |
| **Maintenance** | Active | Stable (complete) |
| **Usage** | Production | Reference/verification |

---

## Recommendations

### Option A: ymfm (BSD, Less Tested)

**Pros:**
- BSD-3 license (permissive, no copyleft)
- Header-only C++, easy integration
- Active maintenance
- FM part well-tested (OPL3 games in MAME)

**Cons:**
- OPL4 wavetable less tested than FM
- May have envelope/timing bugs for PCM
- Not validated against real MoonSound extensively

### Option B: openMSX/VGMPlay Core (GPL, Battle-Tested)

**Pros:**
- ValleyBell's fixes for known OPL4 bugs
- Tested against real MSX MoonSound games
- Most accurate PCM/wavetable behavior

**Cons:**
- GPL-2.0 license (copyleft)
- Not a standalone library
- Requires extraction/adaptation

### Recommended Strategy

```
┌─────────────────────────────────────────────────────────┐
│                 MoonSound Emulation                     │
├─────────────────────────────────────────────────────────┤
│  FM Synthesis (OPL3)    │  Wavetable/PCM (24 ch)       │
├─────────────────────────┼───────────────────────────────┤
│  ymfm or Nuked-OPL3     │  Port ValleyBell fixes       │
│  (well tested)          │  from VGMPlay ymf278b.c      │
├─────────────────────────┴───────────────────────────────┤
│              Sample ROM/RAM (YRW-801)                   │
└─────────────────────────────────────────────────────────┘
```

**Practical approach:**

1. **Start with ymfm** for BSD license and clean API
2. **Cross-reference VGMPlay ymf278b.c** for wavetable accuracy
3. **Port specific fixes** from ValleyBell's work (envelope, decay rates)
4. **Test against VGM recordings** of known problematic games
5. **Use Nuked-OPL3** as FM accuracy reference

### Key Files to Study

| Source | File | Focus |
|--------|------|-------|
| VGMPlay | `chips/ymf278b.c` | ValleyBell's wavetable fixes |
| openMSX | `src/sound/YMF278.cc` | Complete implementation |
| ymfm | `ymfm_opl.h` | BSD-licensed base |
| Nuked | `opl3.c` | FM accuracy reference |

---

## References

### Primary Sources

- [ymfm - GitHub](https://github.com/aaronsgiles/ymfm)
- [ymfm GeneralInfo.md](https://github.com/aaronsgiles/ymfm/blob/main/GeneralInfo.md)
- [ymfm README.md](https://github.com/aaronsgiles/ymfm/blob/main/README.md)
- [Nuked-OPL3 - GitHub](https://github.com/nukeykt/Nuked-OPL3)

### Emulator Implementations

- [openMSX - GitHub](https://github.com/openMSX/openMSX)
- [openMSX OPL4 fixes - Issue #1114](https://github.com/openMSX/openMSX/issues/1114)
- [VGMPlay legacy ymf278b.c](https://github.com/vgmrips/vgmplay-legacy/blob/master/VGMPlay/chips/ymf278b.c)
- [MAME ymf278b.c (old)](https://github.com/libretro/mame2015-libretro/blob/master/src/emu/sound/ymf278b.c)

### Documentation

- [Yamaha YMF278 - Wikipedia](https://en.wikipedia.org/wiki/Yamaha_YMF278)
- [OPL emulation - Doom Wiki](https://doomwiki.org/wiki/OPL_emulation)
- [Aaron Giles 2021 Year In Review](https://aarongiles.com/about/2021/)

### Community

- [openMSX Accuracy Discussion](https://www.msx.org/forum/msx-talk/openmsx/openmsx-accuracy)
- [MoonSound YRW-801 samples](https://www.msx.org/forum/msx-talk/general-discussion/moonsound-yrw-801-rom-samplesinstruments-ripped-files)

---

## Appendix: YMF278B Register Map

### FM Registers (OPL3 Compatible)

| Range | Bank | Description |
|-------|------|-------------|
| 0x01 | 1 | Test / Waveform select |
| 0x02-0x03 | 1 | Timer 1/2 |
| 0x04 | 1 | Timer control |
| 0x05 | 1 | OPL3 mode enable |
| 0x08 | 1 | CSW / Note-sel |
| 0x20-0x35 | 1,2 | AM/VIB/EG/KSR/MULT |
| 0x40-0x55 | 1,2 | KSL/TL |
| 0x60-0x75 | 1,2 | AR/DR |
| 0x80-0x95 | 1,2 | SL/RR |
| 0xA0-0xA8 | 1,2 | F-Number (low) |
| 0xB0-0xB8 | 1,2 | Key-on/Block/F-Num(high) |
| 0xBD | 1 | Rhythm/AM-depth/VIB-depth |
| 0xC0-0xC8 | 1,2 | FB/Connection/Panning |
| 0xE0-0xF5 | 1,2 | Waveform select |

### Wave Registers (PCM)

| Register | Description |
|----------|-------------|
| 0x00-0x01 | Wave table header |
| 0x02 | Memory access mode |
| 0x03-0x05 | Memory address |
| 0x06 | Memory data |
| 0x08-0x1F | Channel 0-23: Wave number |
| 0x20-0x37 | Channel 0-23: F-Number (low) |
| 0x38-0x4F | Channel 0-23: Octave/F-Num(high) |
| 0x50-0x67 | Channel 0-23: Level/LD |
| 0x68-0x7F | Channel 0-23: Key/Damp/LFO/VIB/AM/Pan |
| 0x80-0x97 | Channel 0-23: AR/D1R |
| 0x98-0xAF | Channel 0-23: DL/D2R |
| 0xB0-0xC7 | Channel 0-23: RC/RR |
| 0xF8 | FM Mix control |
| 0xF9 | PCM Mix control |
