# MoonBlaster 1.4 File Formats Specification

**Authors / Publishers:** Moonsoft, Sunrise  
**Target Systems:** MSX-AUDIO (Y8950 / OPL) & MSX-MUSIC (YM2413 / OPLL)

---

## 1. Music File Format (`*.MBM`)

- **File Extension:** `*.MBM` (User song file)
- **Length:** Variable (minimum `$0180` bytes)
- **File Type:** Binary Data
- **Contents:** Song configuration, instrument definitions, voice data, and pattern data

### 1.1 Header & Memory Offsets

| Offset | Length | Description |
|:-------|:-------|:------------|
| `$0000` | 3 | Song Length + ID |
| `$0003` | 144 (`16 * 9`) | Voice data MSX-AUDIO (including volume) |
| `$0093` | 16 | Instrument list MSX-AUDIO |
| `$00A3` | 32 | Instrument/Volume list MSX-MUSIC<br>*(Instruments `1`–`15`: Hardware presets, `16`–`22`: Original user instruments)* |
| `$00C3` | 10 | Channel chip set |
| `$00CD` | 1 | Start tempo |
| `$00CE` | 1 | Sustain MSX-AUDIO |
| `$00CF` | 41 | Track name |
| `$00F8` | 9 | Start instruments MSX-AUDIO |
| `$0101` | 9 | Start instruments MSX-MUSIC |
| `$010A` | 48 (`6 * 8`) | MSX-MUSIC original instrument OPL data (`6 * OPL`) |
| `$013A` | 6 | MSX-MUSIC original instrument program numbers |
| `$0140` | 8 | Sample-Kit name |
| `$0148` | 15 | Drum set-up MSX-MUSIC / PSG |
| `$0157` | 3 | Drum volumes MSX-MUSIC |
| `$015A` | 20 | Drum frequencies MSX-MUSIC |
| `$016E` | 9 | Start detune |
| `$0177` | 1 | Loop position |
| `$0178` | `snglen` | Position table |
| `$0xxx` | `hipat * 2` | Pattern address table |
| `$0xxx` | Variable | Pattern data |

---

### 1.2 Pattern Data: Music Channels (Channels `00`–`08`)

| Value / Range | Description | Notes |
|:--------------|:------------|:------|
| `000` | Empty | Rest / no event |
| `001`–`096` | Note event | `data / 12` = Octave, `data % 12` = Note index |
| `097` | Note OFF | Key-off |
| `098`–`113` | Instrument change | Select instrument |
| `114`–`176` | Volume change | Channel volume |
| `177`–`179` | Stereo set | Panning / stereo configuration |
| `180`–`198` | Note LINK | `180` = `L-9`<br>`189` = `L+0`<br>`198` = `L+9` |
| `199`–`217` | Pitch | `199` = `P-9`<br>`208` = `P+0`<br>`217` = `P+9` |
| `218`–`223` | Brightness negative | Timbre adjustment (negative) |
| `224`–`230` | (de-)Tune | `224` = `T-3`<br>`227` = `T+0`<br>`230` = `T+3` |
| `231`–`236` | Brightness positive | Timbre adjustment (positive) |
| `237` | Sustain | Sustain toggle / parameter |
| `238` | Modulation | Frequency modulation toggle |
| `237`–`242` | Free | Reserved |
| `243`–`255` | Crunched Line data | Compressed empty steps / rows |

---

### 1.3 Pattern Data: Command Channel

| Value / Range | Description |
|:--------------|:------------|
| `000` | Empty |
| `001`–`023` | Change Tempo |
| `024` | End Of Pattern |
| `025`–`027` | Change Drumset MSX-MUSIC |
| `028`–`039` | Set status-byte |
| `049`–... | Transpose |

---

### 1.4 Replayer Channel Buffer Layout (`IY` Register)

| Offset | Description | Details / Values |
|:-------|:------------|:-----------------|
| `IY + $00` | Note number | Current active note |
| `IY + $01` | MSX-AUDIO frequency | Frequency value (low/high) |
| `IY + $03` | MSX-MUSIC frequency | Frequency value (low/high) |
| `IY + $05` | Reserved | |
| `IY + $06` | Frequency mode | `0` = Normal<br>`1` = Pitch<br>`2` = Modulation |
| `IY + $07` | Reserved | |
| `IY + $09` | Tuning | Detune setting |
| `IY + $0A` | Instrument | Current instrument number |
| `IY + $0B` | Reserved | |
| `IY + $0E` | Pitch value | Magnitude of pitch bend |
| `IY + $0F` | Pitch sign | `0` = Positive<br>`-1` (`$FF`) = Negative |
| `IY + $10` | MSX-AUDIO original frequency | Base note frequency |
| `IY + $12` | MSX-MUSIC original frequency | Base note frequency |
| `IY + $14` | MSX-AUDIO brightness | Brightness parameter |
| `IY + $15` | Reserved | |
| `IY + $16` | MSX-AUDIO volume | Channel volume level |
| `IY + $17` | Reserved | |
| `IY + $18` | Last OPL register value | Cached last written register (MSX-AUDIO) |

---

## 2. Sample Kit File Format (`*.MBK`)

- **File Extension:** `*.MBK` (Drumkit / Sample Kit)
- **Length:** `56` bytes (header) + `32,768` bytes (ADPCM data) = `32,824` bytes total
- **File Type:** Binary Data
- **Contents:** Sample start addresses and ADPCM audio samples

These are the drum kits for MoonBlaster. Unlike Soundtracker sample files, these are fixed 32 KB files (excluding header).

### 2.1 File Structure

| Section | Byte Range | Size | Description |
|:--------|:-----------|:-----|:------------|
| **Header** | `00`–`55` (`$00`–`$37`) | 56 bytes | Sample start addresses (16-bit per sample, 14 samples) |
| **Data** | `56`–`32823` (`$38`–`$8037`) | 32,768 bytes | Raw ADPCM sample data |

> **Note:** In version 1.4, `*.MBK` files are strictly `56 + 32k` bytes long (a standard 32 KB ADPCM file).

---

## References

- [MSX Wiki: Moonblaster file format](https://www.msx.org/wiki/Moonblaster_file_format)