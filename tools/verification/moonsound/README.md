# MoonSound / OPL4 Music Verification Tools

Tools for parsing and verifying MoonBlaster music files used with MSX MoonSound and compatible OPL4 sound cards.

## Overview

This module provides parsing and verification capabilities for:

| Format | Extension | Description |
|--------|-----------|-------------|
| MoonBlaster FM | `.MFM` | OPL4 FM synthesis music |
| MoonBlaster Wave | `.MWM` | OPL4 FM + PCM/Wave music |
| MoonBlaster Wave Kit | `.MWK` | Sample kit for MWM files |

All formats use the `MBMS` (MoonBlaster MoonSound) file signature.

## Documentation

- **[MFM/MWM File Format Specification](../../../docs/file-formats/music/mfm-moonblaster.md)** - Comprehensive format documentation including header structure, instrument definitions, pattern encoding, and metadata layout.

## Tools

### mfm_parser.py

Python parser for MFM/MWM files. Extracts and dumps all file contents to JSON.

```bash
# Parse single file (prints summary)
python3 mfm_parser.py song.mfm

# Parse with verbose output
python3 mfm_parser.py song.mfm -v

# Output to JSON file
python3 mfm_parser.py song.mfm -o song.json

# Batch parse directory
python3 mfm_parser.py *.mfm -o output_dir/
```

#### Output Structure

```json
{
  "file": { "name": "...", "magic": "MBMS", "version": "16.01" },
  "metadata": { "text": "Cryogenity LIVE! / R. v/d Moosdijk / Zodiac 1995", "sample_kit": "NONE" },
  "song": { "length": 80, "loop_position": 4, "tempo": 7, "base_frequency": 0 },
  "channels": { "fm_total": 18, "four_op_chains": 6, "two_op_voices": 6, "connection_mask_0x104": "0x3F" },
  "instruments": { "fm": [...], "used": [0, 1, 2] },
  "position_table": [0, 1, 2, ...],
  "pattern_count": 49,
  "patterns": [...]
}
```

#### Python API

```python
from tools.verification.moonsound import parse_mfm, MFMFile

mfm = parse_mfm(Path("song.mfm"))
print(f"Metadata: {mfm.metadata}")
print(f"Patterns: {mfm.pattern_count}")
print(f"Channels: {mfm.four_op_chains} four-op chains + "
      f"{mfm.two_op_voices} two-op FM voices")
```

The layout implemented by the parser is player-verified — see the
[format specification](../../../docs/file-formats/music/mfm-moonblaster.md)
for the derivation (the `.MFM` track-info block starts at file offset 6;
instruments are 24 x 23 bytes; `chvol_1` is the 4-op chain count).

## Hardware Target

### Yamaha OPL4 (YMF278B)

| Feature | Specification |
|---------|---------------|
| FM Channels | 18 (6 four-op or 18 two-op) |
| PCM/Wave Channels | 24 (12/16-bit samples) |
| Sample ROM | 512 KB YRW-801 (General MIDI) |
| Sample RAM | Up to 4 MB |

### I/O Ports (MSX)

| Port | Function |
|------|----------|
| 0xC4 | FM Register Select (Bank 1) |
| 0xC5 | FM Data |
| 0xC6 | FM Register Select (Bank 2) |
| 0xC7 | FM Data (Bank 2) |
| 0x7E | Wave Register Select |
| 0x7F | Wave Data |

## Compatible Hardware

| Device | Manufacturer | Year | Notes |
|--------|--------------|------|-------|
| [MoonSound](https://en.wikipedia.org/wiki/Moonsound) | Sunrise | 1995 | Original cartridge by Henrik Gilvad |
| [Wozblaster](https://www.msx.org/news/en/wozblaster-moonsound-compatible-cartridge) | Gustavo Iriarte | 2010s | DIY clone |
| [Wozblaster Reloaded](https://www.theretrohacker.com/product/msx-opl4-wozblaster-reloaded-cartridge-ymf278/) | The Retro Hacker | 2020s | Modern reproduction |
| [MSX-Blaster OPL4](https://www.msx.org/wiki/8bits4ever_MSX-Blaster_OPL4) | 8bits4ever | 2020s | Commercial clone |

## Software Ecosystem

### Trackers (Original)

| Software | Author | Description |
|----------|--------|-------------|
| MoonBlaster | Remco Schrijvers | Original MSX-MUSIC/AUDIO tracker |
| MoonBlaster FM | Remco Schrijvers, Marcel Delorme | OPL4 FM-only editor |
| MoonBlaster Wave | Remco Schrijvers, Marcel Delorme | Full OPL4 editor |

### Players (MSX)

| Player | Link | Formats |
|--------|------|---------|
| RoboPlay | [MSX Resource Center](https://www.msx.org/forum/msx-talk/graphics-and-music/roboplay-multi-format-music-player-in-fusion-c) | MFM, MWM, MBM, PT3, VGM, etc. |
| VGMPlay MSX | [Grauw](https://www.grauw.nl/projects/vgmplay-msx/) | VGM with OPL4 support |
| MoonDriver | - | MML compiler/player |

### Emulators with OPL4 Support

| Emulator | Platform | OPL4 Quality | Link |
|----------|----------|--------------|------|
| openMSX | Cross-platform | Excellent | [GitHub](https://github.com/openMSX/openMSX) |
| blueMSX | Windows | Good | [Emulation Wiki](https://emulation.gametechwiki.com/index.php/BlueMSX) |
| blueMSX+ | Windows 11 | Improved | [GitHub](https://github.com/Sebbeug/blueMSX-plus) |
| MAME | Cross-platform | Partial | YMF278B core present |

### PC Players/Plugins

| Player | Platform | Link |
|--------|----------|------|
| MSXPlug | Winamp | [MSX Resource Center](https://www.msx.org/news/music/en/msxplug-version-026-mbm-support) |
| VGMPlay | Desktop | [GitHub](https://github.com/vgmrips/vgmplay) |

## References

### Format Documentation

- [Moonblaster file format - MSX Wiki](https://www.msx.org/wiki/Moonblaster_file_format)
- [MBM file format - MSX Resource Center](https://www.msx.org/forum/development/msx-development/mbm-file-format)
- [Music replayer routines - MSX Wiki](https://www.msx.org/wiki/Music_replayer_routines)

### Hardware Documentation

- [The Ultimate MSX FAQ - MoonSound](http://faq.msxnet.org/opl4.html)
- [OPL4 Category - MSX Wiki](https://www.msx.org/wiki/Category:OPL4)
- [Programming OPL4 (FM Part)](https://moltsxalats.wixsite.com/fusionc/post/programming-opl4-fm-part)

### Source Code References

- [MBMPlay-SMS](https://github.com/HerrSchatten/MBMPlay-SMS) - MoonBlaster replayer port to Master System
- [openMSX OPL4](https://github.com/openMSX/openMSX/issues/1114) - OPL4 emulation fixes

### Community

- [MSX Resource Center](https://www.msx.org/) - Primary MSX community hub
- [Team Bomba - MoonBlaster Wave](https://www.teambomba.net/mbwave.html) - Original software page
- [MSX Music Wiki](http://mus.msx.click/) - Music software documentation

## Sample Files

Test files can be obtained from:
- MSX Resource Center software archive
- MoonBlaster distribution packages
- Game music rips (Ys-II, Xak, etc.)

## License

Part of the unreal-ng project. See project root for license information.

## History

- 2026-09-17: Initial implementation with MFM parser
- 2026-09-17: Parser rewritten against the disassembled reference player
  (`moonsound.bin`): correct track-block base (file offset 6), 24 x 23-byte
  FM instruments, 4-op chain semantics for `chvol_1`, derived pattern count,
  bit-packed row decoding, bank-aware pattern pointers
