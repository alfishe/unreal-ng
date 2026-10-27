# LSY256: what the ROM image shows

**Date:** 2026-10-07 · part of [README.md](README.md) · image: [roms.md](roms.md)

A byte scan and a `z80dasm` listing of the SYS page (page 2) of `data/rom/lsy256.rom`. This is not a full
disassembly; every item names the address it was read at. Addresses are in the SYS page, which is an **8K image
mirrored twice** in its 16K page (the two halves are byte-identical).

## 1. Reset and `#7B`

- `#0000`: `DI`, `LD A,#10`, `OUT (#7B),A`, `JP #0074`. So the very first instruction writes `#7B` = `#10`
  (PA3 only, still window-0 mode 0: the SYS ROM stays).
- `#0074`: `LD A,#4D`, `OUT (#FD),A` (the port is `#4DFD`: a `#7FFD` write with value `#4D` = RAM 5, shadow screen bit, ROM 0),
  `IM 1`, then a checksum of 12 bytes at `#E4F0` (RAM, `#C000` window) decides between the RAM test (`#09BF`) and
  the menu (`#00B4`).
- Values written to `#7B` by immediate loads in the SYS page: `#00` (24 times, `XOR A`), `#10` (18 times), `#03` (twice).
  The 48 BASIC page also writes `#03` once (at `#0069`, the NMI entry). `#08`, `#0A`, `#18` and `#1A` (anything with the
  EMUL bit) are **not** written by an immediate load anywhere in pages 1 and 2. Computed writes exist
  (section 3, and the far-call stub in section 4).

## 2. NMI

At `#0066` (both the SYS page and the 48 page): `PUSH AF`, `LD A,#03`, `OUT (#7B),A`, `PUSH HL`, `LD HL,(#0000)`, `JP (HL)`.
Value `#03` is `DV0 | BLKROM`: RAM page 13 read/write at `#0000`. The vector at `#0000` then comes from RAM page 13, which
software fills. This matches Unreal's mode table ("RAM 12/13, DV0 selector, r/w") and shows what that mode is for: a
magic-button / monitor hook kept in RAM.

## 3. RAM test (`#09BF`..`#0A5F`)

For `e` = 15 down to 0: `A = (e & 8) << 1` (`AND 8; RLCA`) is written to `#7B` (so `#10` for `e >= 8`), `A = e & 7` is
written to `#7FFD`, then `e` is stored at `#C000`. A second pass compares. Sixteen distinct pages are therefore selected by
`#7B` bit 4 + `#7FFD` bits 0-2: **`#7B` bit 4 is page bit 3 of the `#C000` window** (256K = 16 pages). This contradicts
Unreal's `latch & 0x10` (index 16+n), see [research-reference-consensus.md](research-reference-consensus.md) 3.1.

## 4. The far-call stub (`#0219`)

`LD A,D`; `OUT (#7B),A`; `LD A,E`; `AND #3F`; `OR #40`; `OUT (#FD),A`; `JP (HL)`. A trampoline: it sets the `#7B` mode from
`D` and a `#7FFD` value from `E` (bit 6 forced, which the LSY paging ignores) and jumps to `HL`. So code can enter RAM-resident
routines under any `#7B` mode, including ones with the EMUL bit.

## 5. Other observations

- Around `#0781`-`#07B6` the SYS ROM sets `#7B` = 0, writes `#7FFD` = `#4F` (RAM page 7 at `#C000`, shadow screen bit), stores a
  byte, then restores `#7B` = `#10` and `#7FFD` = `#4D`. It looks like a character plot into the shadow screen page; not traced further.
- It copies with `LDIR` (`#00AF`, `#0216`, `#07ED`, `#088A`, `#0891`); the sources and destinations were not traced.
  Whether it fills RAM pages 8..11 with the ROM images is **unverified**.
- Page 1 (48 BASIC) has a modified key table next to its token table (strings such as `BHY65TGVNJU74RFCMKI83EDX`), i.e. the
  keyboard scan was changed for the BK-08 matrix. Page 3 is TR-DOS 5.04T unchanged (CRC32 `E212D1E0`). Page 0 differs from the
  Pentagon 128 editor ROM (`pentagon128k.rom`) in 147 bytes.
- `IN A,(#7B)` does not occur: no evidence for a readable `#7B`.

## 6. What this does not tell

- The ROM's boot menu and how it starts 128 / 48 / TR-DOS (the way the EMUL bit gets set).
- Timing: nothing in the ROM shows the frame length.
- The keyboard matrix beyond what the tables imply.

These are checked in phase 1 by running the ROM in the emulator under a TTD recording and a port trace, see
[design.md](design.md) section 9.
