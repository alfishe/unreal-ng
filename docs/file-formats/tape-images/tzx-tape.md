# TZX Tape Image File Format Specification

**Version 1.20**

*Originally devised by Tomaz Kac, September 1997*  
*Maintained by Martijn v.d. Heide, with v1.20 revision by Ramsoft*

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [File Structure](#2-file-structure)
3. [Rules and Definitions](#3-rules-and-definitions)
4. [Block Types](#4-block-types)
   - [ID 10 - Standard Speed Data Block](#id-10---standard-speed-data-block)
   - [ID 11 - Turbo Speed Data Block](#id-11---turbo-speed-data-block)
   - [ID 12 - Pure Tone](#id-12---pure-tone)
   - [ID 13 - Pulse Sequence](#id-13---pulse-sequence)
   - [ID 14 - Pure Data Block](#id-14---pure-data-block)
   - [ID 15 - Direct Recording](#id-15---direct-recording)
   - [ID 18 - CSW Recording](#id-18---csw-recording)
   - [ID 19 - Generalized Data Block](#id-19---generalized-data-block)
   - [ID 20 - Pause / Stop the Tape](#id-20---pause--stop-the-tape)
   - [ID 21 - Group Start](#id-21---group-start)
   - [ID 22 - Group End](#id-22---group-end)
   - [ID 23 - Jump to Block](#id-23---jump-to-block)
   - [ID 24 - Loop Start](#id-24---loop-start)
   - [ID 25 - Loop End](#id-25---loop-end)
   - [ID 26 - Call Sequence](#id-26---call-sequence)
   - [ID 27 - Return from Sequence](#id-27---return-from-sequence)
   - [ID 28 - Select Block](#id-28---select-block)
   - [ID 2A - Stop Tape if in 48K Mode](#id-2a---stop-tape-if-in-48k-mode)
   - [ID 2B - Set Signal Level](#id-2b---set-signal-level)
   - [ID 30 - Text Description](#id-30---text-description)
   - [ID 31 - Message Block](#id-31---message-block)
   - [ID 32 - Archive Info](#id-32---archive-info)
   - [ID 33 - Hardware Type](#id-33---hardware-type)
   - [ID 35 - Custom Info Block](#id-35---custom-info-block)
   - [ID 5A - Glue Block](#id-5a---glue-block)
5. [Deprecated Blocks](#5-deprecated-blocks)
6. [Machine-Specific Information](#6-machine-specific-information)
7. [Hardware Information Reference](#7-hardware-information-reference)
8. [Revision History](#8-revision-history)

---

## 1. Introduction

TZX is a file format designed to preserve all tapes with turbo or custom loading routines for the ZX Spectrum and compatible computers. Even though some newer and "smarter" emulators can find most of the info about the loader from the code itself, this isn't possible if you want to save the file to tape, or to a real Spectrum. With all this information in the file, emulators don't have to bother with finding out the timings and other details.

This file format was originally written for ZX Spectrum compatible computers only, but as it turned out, the SAM Coupe, Amstrad CPC, Jupiter ACE, and Enterprise range of computers use the same tape encoding. It can also be used for ZX-81 type computers, even though the encoding is somewhat different.

### Supported Platforms

| Platform | File Extension | Notes |
|----------|---------------|-------|
| ZX Spectrum | `.tzx` | Primary target |
| Amstrad CPC | `.cdt` | Identical internal structure |
| SAM Coupe | `.tzx` | Compatible encoding |
| Jupiter ACE | `.tzx` | Compatible encoding |
| Enterprise | `.tzx` | Compatible encoding |
| ZX-81 | `.tzx` | Different encoding, uses Pure Tone blocks |

---

## 2. File Structure

### Header Format

The file is identified by the first 8 bytes being `ZXTape!` plus the "end of file" byte `0x1A`. This is followed by two bytes containing the major and minor version numbers.

| Offset | Value | Type | Description |
|--------|-------|------|-------------|
| 0x00 | `ZXTape!` | ASCII[7] | TZX signature |
| 0x07 | `0x1A` | BYTE | End of text file marker |
| 0x08 | 1 | BYTE | TZX major revision number |
| 0x09 | 20 | BYTE | TZX minor revision number |
| 0x0A | ... | BYTE | ID of first block |
| 0x0B | ... | ... | Body of first block |

### Version Compatibility

To use a TZX file, your program must be able to handle files of at least its major version number. If your program can handle version 1.05 and encounters a file with version 1.06, it must be able to handle it, even if it cannot handle all the data in the file. Unrecognized data should be relatively unimportant.

---

## 3. Rules and Definitions

### General Rules

1. **Byte Order**: Any value requiring more than one byte is stored in little-endian format (LSB first).

2. **Unused Bits**: All unused bits should be set to zero.

3. **Timing Units**: Timings are given in Z80 clock ticks (T-states) unless otherwise stated:
   ```
   1 T-state = 1/3,500,000 seconds ≈ 285.7 nanoseconds
   ```

4. **ASCII Text**: All ASCII texts must include only characters from 32 to 127 (decimal). Multi-line texts should be separated by ASCII 13 (CR).

5. **Default Values**: Values in curly brackets `{}` are default values used by the Spectrum ROM saving routines.

### Pulse and Level Definitions

- **Full-period**: `----____` or `____----`
- **Half-period (pulse)**: `----` or `____`
- **High level**: Represented as `1` in Direct Recording blocks
- **Low level**: Represented as `0` in Direct Recording blocks

### Block Length Notation

Block lengths use the following notation:
- Numbers in square brackets `[]` indicate offsets to read values from
- Other values are literal numbers
- All numbers are in hexadecimal

**Example**: `[02,03]+04` means: read a word from offset 02-03 and add 04.

### Current Pulse Level Rules

1. After Standard Speed Data, Turbo Loading, Pure Tone, and Pulse Sequence blocks, the current pulse level is the opposite of the last pulse played.

2. A Pause block consists of a low pulse level. To finish the last edge properly, there should be at least 1ms pause of the opposite level before going low.

3. An emulator should set the current pulse level to low when starting to play a TZX file.

4. A pause of zero duration is completely ignored; the current pulse level will not change.

---

## 4. Block Types

### ID 10 - Standard Speed Data Block

This block must be replayed with the standard Spectrum ROM timing values. It can be used for ROM loading routines and custom loaders that use the same timings.

| Offset | Length | Type | Description | Default |
|--------|--------|------|-------------|---------|
| 0x00 | 2 | WORD | Pause after this block (ms) | {1000} |
| 0x02 | 2 | WORD | Length of data | - |
| 0x04 | N | BYTE[] | Data (as in .TAP file) | - |

**Block length**: `[02,03]+04`

---

### ID 11 - Turbo Speed Data Block

Similar to the Standard Speed block but with custom timing values. Use this for loaders with non-standard timings.

| Offset | Length | Type | Description | Default |
|--------|--------|------|-------------|---------|
| 0x00 | 2 | WORD | Length of PILOT pulse | {2168} |
| 0x02 | 2 | WORD | Length of SYNC first pulse | {667} |
| 0x04 | 2 | WORD | Length of SYNC second pulse | {735} |
| 0x06 | 2 | WORD | Length of ZERO bit pulse | {855} |
| 0x08 | 2 | WORD | Length of ONE bit pulse | {1710} |
| 0x0A | 2 | WORD | Length of PILOT tone (pulses) | {8063 header / 3223 data} |
| 0x0C | 1 | BYTE | Used bits in last byte (1-8) | {8} |
| 0x0D | 2 | WORD | Pause after this block (ms) | {1000} |
| 0x0F | 3 | TRIBYTE | Length of data | - |
| 0x12 | N | BYTE[] | Data (MSb first) | - |

**Block length**: `[0F,10,11]+12`

**Note**: If `Used bits in last byte` is 6, the bits used in the last byte are: `xxxxxx00` (where `x` is a used bit).

---

### ID 12 - Pure Tone

Produces a tone consisting of pulses of identical length.

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 2 | WORD | Length of one pulse (T-states) |
| 0x02 | 2 | WORD | Number of pulses |

**Block length**: `04`

---

### ID 13 - Pulse Sequence

A sequence of pulses, each with its own timing. Useful for non-standard sync tones used by protection schemes.

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 1 | BYTE | Number of pulses (N) |
| 0x01 | 2×N | WORD[] | Pulse lengths (T-states each) |

**Block length**: `[00]*02+01`

---

### ID 14 - Pure Data Block

Same as Turbo Speed Data Block but without pilot or sync pulses.

| Offset | Length | Type | Description | Default |
|--------|--------|------|-------------|---------|
| 0x00 | 2 | WORD | Length of ZERO bit pulse | {855} |
| 0x02 | 2 | WORD | Length of ONE bit pulse | {1710} |
| 0x04 | 1 | BYTE | Used bits in last byte | {8} |
| 0x05 | 2 | WORD | Pause after this block (ms) | {1000} |
| 0x07 | 3 | TRIBYTE | Length of data | - |
| 0x0A | N | BYTE[] | Data (MSb first) | - |

**Block length**: `[07,08,09]+0A`

---

### ID 15 - Direct Recording

For tapes that cannot be described by other block types. Each sample is represented by one bit (0=low, 1=high).

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 2 | WORD | T-states per sample |
| 0x02 | 2 | WORD | Pause after this block (ms) |
| 0x04 | 1 | BYTE | Used bits in last byte (1-8) |
| 0x05 | 3 | TRIBYTE | Length of data |
| 0x08 | N | BYTE[] | Sample data (MSb played first) |

**Block length**: `[05,06,07]+08`

**Recommended sampling frequencies**:
- 22,050 Hz (158 T-states per sample)
- 44,100 Hz (79 T-states per sample)

---

### ID 18 - CSW Recording

*Added in TZX v1.20*

Contains raw pulses encoded in CSW format v2 (Compressed Square Wave).

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 4 | DWORD | Block length (excluding these 4 bytes) |
| 0x04 | 2 | WORD | Pause after this block (ms) |
| 0x06 | 3 | TRIBYTE | Sampling rate |
| 0x09 | 1 | BYTE | Compression type (1=RLE, 2=Z-RLE) |
| 0x0A | 4 | DWORD | Number of stored pulses |
| 0x0E | N | BYTE[] | CSW data |

**Block length**: `[00,01,02,03]+04`

---

### ID 19 - Generalized Data Block

*Added in TZX v1.20*

Represents an extremely wide range of data encoding techniques by defining custom pulse sequences for each loading component.

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 4 | DWORD | Block length (excluding these 4 bytes) |
| 0x04 | 2 | WORD | Pause after this block (ms) |
| 0x06 | 4 | DWORD | Total number of symbols in pilot/sync block (TOTP) |
| 0x0A | 1 | BYTE | Maximum pulses per pilot/sync symbol (NPP) |
| 0x0B | 1 | BYTE | Symbols in pilot/sync alphabet (ASP) |
| 0x0C | 4 | DWORD | Total number of symbols in data stream (TOTD) |
| 0x10 | 1 | BYTE | Maximum pulses per data symbol (NPD) |
| 0x11 | 1 | BYTE | Symbols in data alphabet (ASD) |
| 0x12 | ... | ... | Symbol definitions and data |

**Block length**: `[00,01,02,03]+04`

---

### ID 20 - Pause / Stop the Tape

Creates silence (low amplitude) for the specified duration. A value of 0 means "stop the tape" until user intervention.

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 2 | WORD | Pause duration (ms), 0 = stop tape |

**Block length**: `02`

---

### ID 21 - Group Start

Marks the start of a group of blocks to be treated as a single composite block. Useful for protection schemes like Speedlock (with ~64 pure tone blocks) or Bleeploads (with 160+ custom loading blocks).

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 1 | BYTE | Length of group name |
| 0x01 | N | ASCII | Group name |

**Block length**: `[00]+01`

**Note**: For each Group Start, there must be a Group End. Nesting of groups is not allowed.

---

### ID 22 - Group End

Indicates the end of a group. This block has no body.

**Block length**: `00`

---

### ID 23 - Jump to Block

Enables jumping between blocks within the file. The value is a signed short word (relative offset).

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 2 | SWORD | Relative jump value |

**Block length**: `02`

**Examples**:
- Jump 0 = Loop forever (should never happen)
- Jump 1 = Go to next block (NOP)
- Jump 2 = Skip one block
- Jump -1 = Go to previous block

---

### ID 24 - Loop Start

Indicates a sequence of blocks to be repeated. Equivalent to BASIC's FOR statement.

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 2 | WORD | Number of repetitions (>1) |

**Block length**: `02`

**Note**: Nested loops are not allowed.

---

### ID 25 - Loop End

Marks the end of a loop (equivalent to BASIC's NEXT). This block has no body.

**Block length**: `00`

---

### ID 26 - Call Sequence

Executes a sequence of blocks elsewhere in the file, then returns. Analogous to a subroutine call.

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 2 | WORD | Number of calls (N) |
| 0x02 | 2×N | SWORD[] | Relative offsets to call blocks |

**Block length**: `[00,01]*02+02`

**Note**: Nesting of call blocks is not allowed, but calls can be used within loops.

---

### ID 27 - Return from Sequence

Indicates the end of a called sequence. Execution continues after the last CALL block. This block has no body.

**Block length**: `00`

---

### ID 28 - Select Block

Provides a menu for selecting from multiple tape parts (e.g., separate trainer, multi-load game levels).

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 2 | WORD | Length of whole block (excluding these 2 bytes) |
| 0x02 | 1 | BYTE | Number of selections (N) |
| 0x03 | ... | ... | Selection entries |

**Selection entry format**:
| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 2 | SWORD | Relative offset |
| 0x02 | 1 | BYTE | Length of description |
| 0x03 | L | ASCII | Description text |

**Block length**: `[00,01]+02`

---

### ID 2A - Stop Tape if in 48K Mode

The tape will stop only if the machine is a 48K Spectrum. Used for multi-loading games that behave differently in 48K vs 128K mode.

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 4 | DWORD | Length of block (always 0) |

**Block length**: `04`

---

### ID 2B - Set Signal Level

*Added in TZX v1.20*

Sets the current signal level to a specified value (high or low). Use when level-sensitive custom loaders require explicit level setting.

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 4 | DWORD | Block length (always 1) |
| 0x04 | 1 | BYTE | Signal level (0=low, 1=high) |

**Block length**: `05`

---

### ID 30 - Text Description

Identifies parts of the tape (e.g., where level 1 starts, rewind points). Not guaranteed to be displayed during playback.

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 1 | BYTE | Length of text |
| 0x01 | N | ASCII | Description text |

**Block length**: `[00]+01`

**Note**: Keep descriptions to about 30 characters. Use Archive Info block for title, author, publisher, etc.

---

### ID 31 - Message Block

Displays a message for a given time without stopping the tape or creating silence. A time of 0 means wait for user keypress.

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 1 | BYTE | Display time (seconds), 0 = wait for key |
| 0x01 | 1 | BYTE | Length of message |
| 0x02 | N | ASCII | Message text |

**Block length**: `[01]+02`

**Format suggestions**:
- Maximum 30 characters per line
- Use ASCII 13 (CR) to separate lines
- Maximum 8 lines

---

### ID 32 - Archive Info

Contains metadata about the tape (title, author, publisher, etc.). Use at the beginning of the tape.

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 2 | WORD | Length of block (excluding these 2 bytes) |
| 0x02 | 1 | BYTE | Number of text strings (N) |
| 0x03 | ... | ... | Text string entries |

**Text string entry format**:
| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 1 | BYTE | Text identification byte |
| 0x01 | 1 | BYTE | Length of text |
| 0x02 | L | ASCII | Text |

**Text identification bytes**:
| ID | Description |
|----|-------------|
| 0x00 | Full title |
| 0x01 | Software house/Publisher |
| 0x02 | Author(s) |
| 0x03 | Year of publication |
| 0x04 | Language |
| 0x05 | Game/Utility type |
| 0x06 | Price |
| 0x07 | Protection scheme/Loader |
| 0x08 | Origin |
| 0xFF | Comment(s) |

**Block length**: `[00,01]+02`

---

### ID 33 - Hardware Type

Specifies what hardware the program uses or requires. Only include entries for hardware you're 100% certain about.

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 1 | BYTE | Number of machine/hardware entries (N) |
| 0x01 | 3×N | ... | Hardware entries |

**Hardware entry format**:
| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 1 | BYTE | Hardware type |
| 0x01 | 1 | BYTE | Hardware ID |
| 0x02 | 1 | BYTE | Hardware information |

**Hardware information values**:
| Value | Description |
|-------|-------------|
| 0x00 | Runs on this machine/hardware, may or may not use special features |
| 0x01 | Uses the hardware or special features |
| 0x02 | Runs but doesn't use special features |
| 0x03 | Doesn't run on this machine/hardware |

**Block length**: `[00]*03+01`

---

### ID 35 - Custom Info Block

Stores arbitrary information (utility settings, emulator requirements, POKEs, title screens, etc.).

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 16 | ASCII | Identification string |
| 0x10 | 4 | DWORD | Length of custom info |
| 0x14 | N | BYTE[] | Custom info data |

**Block length**: `[10,11,12,13]+14`

**Standardized identification strings**:
- `POKEs           ` - Trainer/POKE data
- `Instructions    ` - Game instructions
- `Spectrum Screen ` - Loading screen (6912 bytes)
- `ZX-Edit document` - ZX-Editor documents
- `Picture         ` - GIF/JPEG images

---

### ID 5A - Glue Block

Generated when two TZX files are concatenated. Skip the next 9 bytes when encountered.

| Offset | Length | Type | Description |
|--------|--------|------|-------------|
| 0x00 | 9 | BYTE[] | `XTape!` + 0x1A + MajorVer + MinorVer |

**Block length**: `09`

**Note**: Prefer using a utility to properly merge files rather than relying on this block.

---

## 5. Deprecated Blocks

The following blocks were used until TZX version 1.13 and should not appear in v1.20+ files. They are documented for backward compatibility:

| ID | Name | Purpose |
|----|------|---------|
| 0x16 | C64 ROM Type Data Block | Commodore 64 standard ROM data |
| 0x17 | C64 Turbo Tape Data Block | Commodore 64 turbo tape data |
| 0x34 | Emulation Info | Emulator settings |
| 0x35 (old) | Custom Info (deprecated types) | Obsolete custom info formats |
| 0x40 | Snapshot Block | Embedded Z80/SNA snapshots |

---

## 6. Machine-Specific Information

### Standard Tape Encoding (ZX Spectrum, Amstrad CPC, SAM Coupe, Jupiter ACE, Enterprise)

These computers use frequency-based tape encoding where each bit is represented by one wave of different duration. Typically, bit 1 is twice as long as bit 0.

#### Timing Table (T-states at 3.5 MHz clock)

| Machine | Pilot Pulse | Pilot Length | Sync 1 | Sync 2 | Bit 0 | Bit 1 |
|---------|-------------|--------------|--------|--------|-------|-------|
| ZX Spectrum | 2168 | 8063 (header) / 3223 (data) | 667 | 735 | 855 | 1710 |
| Amstrad CPC | = Bit 1 | 4096 | = Bit 0 | = Bit 0 | Variable | Variable |
| SAM Coupe | 58+19×W | 6000 | 58+9×H | 113+9×H | 10+8×W | 42+15×W |
| Jupiter ACE | 2011 | 8192 (header) / 1024 (data) | 600 | 790 | 801 | 1591 |
| Enterprise (fast) | 742 | ? | 1280 | 1280 | 882 | 602 |
| Enterprise (slow) | 1750 | ? | 2800 | 2800 | 1982 | 1400 |

**Notes**:
- **Amstrad CPC**: Speed varies from 1000 to 2000 baud; bit timings are read from the pilot tone.
- **SAM Coupe**: W is a system variable (default 112), H = W/2.
- **Enterprise**: Stores data LSb first, but TZX requires MSb first (mirror each byte).

### Clock Frequency Adjustment

All TZX timings are based on a 3.5 MHz clock. For other clock speeds:

| Machine | Clock | Multiplier |
|---------|-------|------------|
| ZX Spectrum | 3.5 MHz | 1.0 |
| Amstrad CPC | 4.0 MHz | 1.142857 |
| SAM Coupe | 6.0 MHz | 1.714286 |
| Jupiter ACE | 3.2448 MHz | 0.927 |
| Enterprise | 4.0 MHz | 1.142857 |

### ZX-81 Tape Encoding

ZX-81 uses a different encoding based on the number of waves rather than frequency:

- **Bit 0**: 3-5 waves (normally 4)
- **Bit 1**: 6-9 waves (normally 9)
- **Wave frequency**: ~12 kHz (~637 T-states per pulse)
- **Inter-bit pause**: Same duration as Bit 0

The pilot tone is structured as repeating `10101010` pattern, followed immediately by data (no sync pulses).

---

## 7. Hardware Information Reference

### Type 0x00 - Computers

| ID | Description |
|----|-------------|
| 0x00 | ZX Spectrum 16k |
| 0x01 | ZX Spectrum 48k, Plus |
| 0x02 | ZX Spectrum 48k ISSUE 1 |
| 0x03 | ZX Spectrum 128k (Sinclair) |
| 0x04 | ZX Spectrum 128k +2 (Grey case) |
| 0x05 | ZX Spectrum 128k +2A, +3 |
| 0x06 | Timex Sinclair TC-2048 |
| 0x07 | Timex Sinclair TS-2068 |
| 0x08 | Pentagon 128 |
| 0x09 | Sam Coupe |
| 0x0A | Didaktik M |
| 0x0B | Didaktik Gama |
| 0x0C | ZX-81 with 1k RAM |
| 0x0D | ZX-81 with 16k RAM or more |
| 0x0E | ZX Spectrum 128k, Spanish version |
| 0x0F | ZX Spectrum, Arabic version |
| 0x10 | TK 90-X |
| 0x11 | TK 95 |
| 0x12 | Byte |
| 0x13 | Elwro |
| 0x14 | ZS Scorpion |
| 0x15 | Amstrad CPC 464 |
| 0x16 | Amstrad CPC 664 |
| 0x17 | Amstrad CPC 6128 |
| 0x18 | Amstrad CPC 464+ |
| 0x19 | Amstrad CPC 6128+ |
| 0x1A | Jupiter ACE |
| 0x1B | Enterprise |
| 0x1C | Commodore 64 |
| 0x1D | Commodore 128 |
| 0x1E | Inves Spectrum+ |
| 0x1F | Profi |
| 0x20 | GrandRomMax |
| 0x21 | Kay 1024 |
| 0x22-0x2A | Various clones |
| 0x2B | Lambda |
| 0x2C | TK-65 |
| 0x2D | ZX-97 |

### Type 0x01 - External Storage

| ID | Description |
|----|-------------|
| 0x00 | Microdrive |
| 0x01 | Opus Discovery |
| 0x02 | Disciple |
| 0x03 | Plus-D |
| 0x04 | Rotronics Wafadrive |
| 0x05 | TR-DOS (BetaDisk) |
| 0x06 | Byte Drive |
| 0x07 | Watsford |
| 0x08 | FIZ |
| 0x09 | Radofin |
| 0x0A | Didaktik disk drives |
| 0x0B | BS-DOS (MB-02) |
| 0x0C | ZX Spectrum +3 disk drive |
| 0x0D | JLO (Oliger) disk interface |
| 0x0E | FDD3000 |
| 0x0F | Zebra disk drive |
| 0x10 | Ramex Millenia |
| 0x11 | Larken |

### Type 0x02 - ROM/RAM Type Add-ons

| ID | Description |
|----|-------------|
| 0x00 | Sam Ram |
| 0x01 | Multiface |
| 0x02 | Multiface 128k |
| 0x03 | Multiface +3 |
| 0x04 | MultiPrint |
| 0x05 | MB-02 ROM/RAM expansion |

### Type 0x03 - Sound Devices

| ID | Description |
|----|-------------|
| 0x00 | Classic AY hardware (128k compatible) |
| 0x01 | Fuller Box AY sound hardware |
| 0x02 | Currah microSpeech |
| 0x03 | SpecDrum |
| 0x04 | AY ACB stereo (A+C=left, B+C=right); Melodik |
| 0x05 | AY ABC stereo (A+B=left, B+C=right) |

### Type 0x04 - Joysticks

| ID | Description |
|----|-------------|
| 0x00 | Kempston |
| 0x01 | Cursor, Protek, AGF |
| 0x02 | Sinclair 2 Left (12345) |
| 0x03 | Sinclair 1 Right (67890) |
| 0x04 | Fuller |

### Type 0x05 - Mice

| ID | Description |
|----|-------------|
| 0x00 | AMX mouse |
| 0x01 | Kempston mouse |

### Type 0x06 - Other Controllers

| ID | Description |
|----|-------------|
| 0x00 | Trickstick |
| 0x01 | ZX Light Gun |
| 0x02 | Zebra Graphics Tablet |

### Type 0x07 - Serial Ports

| ID | Description |
|----|-------------|
| 0x00 | ZX Interface 1 |
| 0x01 | ZX Spectrum 128k |

### Type 0x08 - Parallel Ports

| ID | Description |
|----|-------------|
| 0x00 | Kempston S |
| 0x01 | Kempston E |
| 0x02 | ZX Spectrum +3 |
| 0x03 | Tasman |
| 0x04 | DK'Tronics |
| 0x05 | Hilderbay |
| 0x06 | INES Printerface |
| 0x07 | ZX LPrint Interface 3 |
| 0x08 | MultiPrint |
| 0x09 | Opus Discovery |
| 0x0A | Standard 8255 chip (ports 31, 63, 95) |

### Type 0x09 - Printers

| ID | Description |
|----|-------------|
| 0x00 | ZX Printer, Alphacom 32 & compatibles |
| 0x01 | Generic Printer |
| 0x02 | EPSON Compatible |

### Type 0x0A - Modems

| ID | Description |
|----|-------------|
| 0x00 | VTX 5000 |
| 0x01 | T/S 2050 or Westridge 2050 |

### Type 0x0B - Digitizers

| ID | Description |
|----|-------------|
| 0x00 | RD Digital Tracer |
| 0x01 | DK'Tronics Light Pen |
| 0x02 | British MicroGraph Pad |

### Type 0x0C - Network Adapters

| ID | Description |
|----|-------------|
| 0x00 | ZX Interface 1 |

### Type 0x0D - Keyboards & Keypads

| ID | Description |
|----|-------------|
| 0x00 | Keypad for ZX Spectrum 128k |

### Type 0x0E - AD/DA Converters

| ID | Description |
|----|-------------|
| 0x00 | Harley Systems ADC 8.2 |
| 0x01 | Blackboard Electronics |

### Type 0x0F - EPROM Programmers

| ID | Description |
|----|-------------|
| 0x00 | Orme Electronics |

---

## 8. Revision History

### Version 1.20

- Added new blocks: ID 18 (CSW Recording), ID 19 (Generalized Data Block), ID 2B (Set Signal Level)
- Deprecated blocks: ID 16, ID 17, ID 34, ID 35 (old types), ID 40
- All new blocks follow the general extension rule (4-byte length after ID)
- Added new hardware IDs

### Version 1.13

- Added Commodore 64 support with blocks ID 16 and ID 17
- Added new Archive Info fields (0x05-0x08)
- Added Custom Info block types
- Added Enterprise computer support

### Version 1.12

- Added new block types for grouping and control flow
- Improved documentation

### Version 1.00

- Initial release by Tomaz Kac

---

## References

- Original specification: [World of Spectrum TZX Format](https://worldofspectrum.org/TZXformat.html)
- Amstrad CPC adaptation: CDT format documentation
- Related formats: TAP, PZX, CSW

---

*This document is based on the TZX format specification originally published at World of Spectrum. The format was created by Tomaz Kac in September 1997 and later maintained by Martijn v.d. Heide and Ramsoft.*
