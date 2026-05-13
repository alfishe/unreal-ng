# Z80 Snapshot Format Specification

The `.z80` format is arguably the most widely supported by emulators across all platforms. `.z80` files are memory snapshots — they contain an image of the contents of the ZX Spectrum memory at a particular instance in time. As a result of this, they cannot be used to reproduce the original tape from a snapshot file, but do load almost instantaneously.

The `.z80` format was originally developed by Gerton Lunter for use in his Z80 emulator, and three versions of the format, as saved by Z80 versions 1.45 (and earlier), 2.x and 3.x (and later) are in use. For ease of notation, these will be referred to as **versions 1, 2 and 3 of the format** respectively. Various extensions to the `.z80` format have also been made by other emulators. :contentReference[oaicite:0]{index=0}

---

## Version 1 Header (48K Snapshots)

Version 1 of the `.z80` format can save only 48K snapshots and has the following header structure:

| Offset | Length | Description |
|--------|--------|-------------|
| 0      | 1      | A register |
| 1      | 1      | F register |
| 2      | 2      | BC register pair (LSB first) |
| 4      | 2      | HL register pair |
| 6      | 2      | Program counter |
| 8      | 2      | Stack pointer |
| 10     | 1      | Interrupt register |
| 11     | 1      | Refresh register (Bit 7 not significant) |
| 12     | 1      | Bit 0: Bit 7 of R-register<br>Bit 1–3: Border colour<br>Bit 4: 1 = Basic SamRom switched in<br>Bit 5: 1 = Block of data is compressed<br>Bit 6–7: No meaning |
| 13     | 2      | DE register pair |
| 15     | 2      | BC' register pair |
| 17     | 2      | DE' register pair |
| 19     | 2      | HL' register pair |
| 21     | 1      | A' register |
| 22     | 1      | F' register |
| 23     | 2      | IY register |
| 25     | 2      | IX register |
| 27     | 1      | Interrupt flipflop (0 = DI, otherwise EI) |
| 28     | 1      | IFF2 |
| 29     | 1      | Bit 0–1: Interrupt mode<br>Bit 2: Issue 2 emulation<br>Bit 3: Double interrupt frequency<br>Bit 4–5: Video sync mode<br>Bit 6–7: Joystick type |

Because of compatibility, if byte 12 is `255`, it must be regarded as being `1`. :contentReference[oaicite:1]{index=1}

After this header block of 30 bytes, the 48K bytes of Spectrum memory follow in a compressed format **if bit 5 of byte 12 is set**. The compression method replaces repetitions of at least five equal bytes by a four-byte code `ED ED xx yy`, which stands for “byte `yy` repeated `xx` times.” The block is terminated by an end marker `00 ED ED 00`. :contentReference[oaicite:2]{index=2}

---

## Version 2 and 3 Headers

Version 2 and 3 `.z80` files start with the same 30-byte header as version 1, except:

- Bit 4 and 5 of the flag byte have no meaning.
- The program counter bytes (6 and 7) are zero to signal version 2 or 3. :contentReference[oaicite:3]{index=3}

After the first 30 bytes, an **additional header** follows:

| Offset | Length | Description |
|--------|--------|-------------|
| 30     | 2      | Length of additional header block |
| 32     | 2      | Program counter |
| 34     | 1      | Hardware mode |
| 35     | 1      | SamRam/128 mode state |
| 36     | 1      | Interface I ROM status |
| 37     | 1      | Flags for R emulation, LDIR emulation, AY sound, hardware modify |
| 38     | 1      | Last OUT to port `0xfffd` |
| 39     | 16     | Contents of the sound chip registers |
| 55     | 2      | Low T state counter |
| 57     | 1      | Hi T state counter |
| 58     | 1      | Spectator flag byte |
| 59     | 1      | `0xff` if MGT ROM paged |
| 60     | 1      | `0xff` if Multiface ROM paged (should always be 0) |
| 61     | 1      | `0xff` if addresses `0–8191` are ROM |
| 62     | 1      | `0xff` if addresses `8192–16383` are ROM |
| 63–72  | 10     | Keyboard mappings |
| 73–82  | 10     | ASCII words for mappings |
| 83     | 1      | MGT type |
| 84     | 1      | Disciple inhibit button status |
| 85     | 1      | Disciple inhibit flag |
| 86     | 1      | Last OUT to port `0x1ffd` (version 3 only) |

The value of the word at position 30 is `23` for version 2 files, and `54` or `55` for version 3. Fields marked `*` are present in version 2 header; the final byte marked `**` is present only if length equals `55`. :contentReference[oaicite:4]{index=4}

---

## Hardware Mode Meaning

| Value | Meaning in v2 | Meaning in v3 |
|-------|---------------|---------------|
| 0     | 48K | 48K |
| 1     | 48K + If.1 | 48K + If.1 |
| 2     | SamRam | SamRam |
| 3     | 128K | 48K + M.G.T. |
| 4     | 128K + If.1 | 128K |
| 5     | — | 128K + If.1 |
| 6     | — | 128K + M.G.T. |

Emulators have also extended the `.z80` format to support machines such as Spectrum +3, Pentagon, Scorpion, TC2048, and others with custom hardware mode values. :contentReference[oaicite:5]{index=5}

---

## Memory Blocks

After the extended header, a number of memory blocks follow, each containing the compressed data of a 16K block. The structure is:

| Byte | Length | Description |
|------|--------|-------------|
| 0    | 2      | Length of compressed data |
| 2    | 1      | Page number of block |
| 3    | [0]    | Data |

If length = `0xffff`, the data is 16384 bytes long and not compressed. :contentReference[oaicite:6]{index=6}

### Page Numbering

Depending on hardware mode:

| Page | '48 Mode | '128 Mode | SamRam Mode |
|------|-----------|------------|--------------|
| 0 | 48K ROM | ROM (Basic) | 48K ROM |
| 1 | Interface/Disciple/Plus D ROM | ROM (reset) | SamRam ROM (Basic) |
| 2–11 | Varies | See full spec | See full spec |

In 48K mode, pages 4, 5, and 8 are saved; in SamRam mode, pages 4–8; in 128K mode, all pages from 3 to 10 are saved. :contentReference[oaicite:7]{index=7}

---

## Notes

- Byte 60 must be zero since Multiface RAM isn’t saved — if it was paged, programs may crash on load. :contentReference[oaicite:8]{index=8}
- Bytes 61 and 62 depend on other flags such as byte 34, 59, 60, and 83. :contentReference[oaicite:9]{index=9}

---

## Source Reference

Original specification from **World of Spectrum FAQ – Z80 File Format**:  
https://worldofspectrum.org/faq/reference/z80format.htm :contentReference[oaicite:10]{index=10}
