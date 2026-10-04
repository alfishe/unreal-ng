# SP.COM: why the logo shows colored dashes

**Date:** 2026-10-04 · machines `PROFI` (ROM BIOS 2.0, Micco / Kondor CP/M) and `PROFI-PLUS` (ROM BIOS Plus
0.41h1, PQ-DOS) · verdict: **a bug in the program, not in the emulator**

## The symptom

`SP.COM` is a music demo from the `DEMOS` folder of the PQ-DOS 2023 hard-disk image: a menu of four AY tunes
(keys 1-4, Space exits), credited "Coding By Кулагин Александр, Graphics By Белоусов Александр", 08.03.1993. At
the top of the screen it shows a logo: 22 x 4 character cells at column 21, row 3 of the 512 x 240 hi-res screen.
In the emulator the logo appears as scattered colored dashes. The picture is the same in all four music parts, and
the same under PQ-DOS on `PROFI-PLUS` and under Micco CP/M on a stock `PROFI`.

| File | Size | CRC32 | SHA-256 |
|:--|--:|:--|:--|
| `DEMOS/SP.COM` (PQ-DOS 2023 HDD image, FAT16 partition at LBA 2048) | 28160 | `C1F1F69B` | `d9315d9b2db958eeddf17b64de55c1707417f2b56b856933bb4184ab5619f6aa` |

## How the program draws the logo

The program does not draw the logo itself. It prints its whole screen with BDOS function 9 (print a string ending
in `$`) and ends the string with the console escape `ESC i`, "draw a picture". The console driver reads the
picture's parameters from an 8-byte block at `#0080`:

```
0103  di
0104  ld hl,#02F1      ; the parameter block in the program
0107  ld de,#0080
010A  ld bc,8          ; 8 bytes
010D  ldir
010F  ld de,#0169      ; the screen text: ESC Y positions, the menu, the credits ... ESC i $
0112  ld c,9
0114  call 5           ; BDOS 9
```

The 8 bytes at `#02F1`, as the console driver reads them:

| Address | Bytes | Meaning | Value |
|:--|:--|:--|:--|
| `#0080` | `15 03` | column, row (character cells) | 21, 3 |
| `#0082` | `16 04` | width, height (cells) | 22 x 4 |
| `#0084` | `ED 03` | pixel data | `#03ED` |
| `#0086` | `F3 21` | attribute data | `#21F3` |

Only the first six bytes are the program's data. The block is 6 bytes long, and the last two copied bytes are the
first instruction bytes of the next routine, the key-1 handler at `#02F7` (`F3` = `di`, `21` = `ld hl,nn`).
`#21F3` points into the music data (`80 01 00 80 02 00 80 01 00 80 00 00 A0 01 ...`).

The picture data at `#03ED` has the console driver's format: for each cell row, for each column, 8 pixel bytes
(top line first). The 704 bytes of pixels (22 x 4 x 8) are followed by 704 bytes of attributes, all `#07` (white
ink on black paper). Nothing points at these attributes (`#06AD`).

## What the console driver does with the block

The `ESC i` handler of the console driver (Micco CP/M, traced on `PROFI` at frame 1420 of a boot from a test
disk: the driver runs at `#6C58` in RAM page 5, mapped at `#4000`):

```
6C58  ld bc,(#0082)    ; width, height
6C5C  ld de,(#0080)    ; column, row
6C60  ld (#0088),de
...                    ; per cell: 8 pixel bytes from (#0084) to the bitmap page, line by line (inc h)
6C68  ld de,(#0084)
6C70  ld a,(de)
6C71  ld (hl),a
6C72  inc h
6C73  inc de
6C74  djnz #6C70
...
6CA7  ld hl,(#0086)    ; attribute data pointer
6CAD  call #F83F       ; -> #FCB0: copy 8 bytes to the buffer #FED5, page in the attribute page, store them
6CB1  ld de,8
6CB4  add hl,de
...
```

`#FCB0` (resident, common to all banks):

```
FCB0  push de
FCB1  ld de,#FED5
FCB4  ld bc,8
FCB7  ldir             ; 8 attribute bytes from the caller's memory to the buffer
FCB9  ld hl,(#F844)    ; the caller's paging (7FFD, DFFD)
FCBC  ex (sp),hl
FCBD  ld de,(#F846)    ; the screen paging: 7FFD = #0A, DFFD = #BF -> page #3A at #4000
FCC1  call #FC38       ; OUT (#7FFD),D; OUT (#DFFD),E
FCC4  ld de,#FED5
FCC7  ld b,8
FCC9  ld a,(de)
FCCA  ld (hl),a        ; attribute byte of the cell, line by line
FCCB  inc de
FCCC  inc h
FCCD  djnz #FCC9
FCCF  pop de
FCD0  jp #FC38         ; restore the caller's paging
```

A trace of the writes (the probe's `PROFI_WATCH`, see below) shows both halves working as coded:

- **pixels**: bytes from `#03ED` onward land in bitmap page 6 at the right offsets. A dump of page 6 after the
  draw shows the complete logo: the lettering, the frame and the lines.
- **attributes**: bytes from `#21F3` onward (`80 01 00 80 02 00 80 01`, then `00 80 00 00 A0 01 00 A0` ...)
  land in attribute page `#3A`. With these values the ink often equals the paper, so most of the logo's pixels
  are invisible and the rest show in random colors: the dashes.

The emulator runs the driver's code as written, and the driver does what the block asks. The same result on two
different systems (Micco CP/M with ROM BIOS 2.0, PQ-DOS with ROM BIOS Plus) confirms it.

## Probable cause

The program was most likely written for a console driver whose `ESC i` takes a 6-byte block and reads the
attributes right after the pixels. The author copied 8 bytes; with a 6-byte block the extra two do no harm. The
drivers we have (Micco / Kondor CP/M, PQ-DOS) take an 8-byte block with a separate attribute pointer, so on them
the logo shows as dashes. On a real Profi with the same system it would look the same. We have no driver with the
6-byte variant to confirm this.

With `#0086` = `#06AD` (bytes `AD 06`, set after the copy at `#010D`) the driver would take the attributes from
the program's own `#07` table: a white logo on black (not tried in the emulator).

## How this was traced

Development aids of the probe test `ProfiBoot_Test.DISABLED_RunProgram`
([`core/tests/emulator/profi_boot_test.cpp`](../../../../../core/tests/emulator/profi_boot_test.cpp)):

| Variable | Use |
|:--|:--|
| `PROFI_PROGRAM` | the disk to boot: a Kondor CP/M disk with `SP.COM` and `AUTOEXEC.BAT` = `SP` |
| `PROFI_FRAMES`, `PROFI_SHOTS` | where the logo appears: between frames 1420 and 1430 |
| `PROFI_TRACE=n` | PC and the paging latches per instruction; the attribute loop runs at `7FFD=#0A DFFD=#BF` |
| `PROFI_WATCH=n`, `PROFI_WATCH_RECT=col,row,w,h` | each change in the cell rectangle of bitmap page 6 and attribute page `#3A`, with PC, HL, DE and the latches |
| `PROFI_DUMP_PAGES=prefix` | pages 4, 6, `#38`, `#3A` after the run |
| `PROFI_DUMP_MEM=file` | the Z80's 64K view (the driver code at `#6C58`, `#FCB0`) |
