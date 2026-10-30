# wsay: PT3 music on the Wild Sound card's AY chips (Sprinter)

A DSS program for the Peters Plus Sprinter that plays a PT3 module (Pro Tracker 3.x / Vortex Tracker II)
through the two AY chips of the ISA Wild Sound card: one module on chip 0, or a TurboSound pair on chip 0 and
chip 1. One EXE per track, the module(s) inside
([design](../../../../../docs/inprogress/2026-10-09-wildsound/design.md) §9, requirement R43).

| File | What it is |
|:--|:--|
| `wsay.asm` | the DSS program: banner, card probe, 50 Hz loop, key check, silence and exit; the card access |
| `pt3prom.asm` | S.V. Bulba's PT3 player (ROM version r.7), adapted: its AY output goes to `CardOut` in `wsay.asm` |
| `wsaybuild.py` | builds an EXE from one or two PT3 files with sjasmplus and writes the DSS header |

## Use

```bash
export SJASMPLUS=/path/to/sjasmplus          # 1.20 or later; or --sjasmplus PATH, or sjasmplus on PATH
python3 -I wsaybuild.py song.pt3 -o SONG.EXE              # one chip
python3 -I wsaybuild.py ts-song.pt3 -o TSSONG.EXE         # a TurboSound PT3: both chips
python3 -I wsaybuild.py ts-song.pt3 --single -o HALF.EXE  # only its first module
python3 -I wsaybuild.py a.pt3 b.pt3 -o PAIR.EXE           # two files: a on chip 0, b on chip 1
```

On the Sprinter: `SONG.EXE` (card in ISA slot 1) or `SONG.EXE 2` (slot 2). It prints the banner, "Wild Sound
found in ISA slot N" and each chip's "title by author" from the PT3 header, then plays until a key is
pressed (any key, Esc included). It then silences both chips (mixer `#3F`, R8-R10 = 0), leaves the ISA bus
idle (`#9FBD` <- `#40`) and returns to DSS (`Exit`, code 0). Without a card it prints "... not found in ISA
slot N" and exits with code 1.

A TurboSound PT3 is two PT3 modules back to back; Vortex Tracker II adds a 16-byte footer `"PT3!"` size1
`"PT3!"` size2 `"02TS"`. `wsaybuild.py` splits by the footer, or else at the second `"ProTracker 3."` /
`"Vortex Tracker II"` signature. The first module goes to chip 0 (select `#FF`), the second to chip 1 (`#FE`),
the order TurboSound players use.

## How it reaches the card

The Sprinter has no I/O-port path to an ISA slot: the slot's I/O space is mapped into CPU window 3
(`#C000-#FFFF`) and every memory access there is one ISA I/O cycle (the method of `prosiak.exe`; see
`docs/disasm/software/sprinter/prosiak-wildsound/player.asm`, `SetIsaLatch`, `SelectIsaIo`, `IsaOut`,
`BusIdle`, and `docs/disasm/software/sprinter/proplay/README.md`, "How it reaches the card"):

| Step | Code |
|---|---|
| open | `DI`; save `IN A,(#E2)` (window 3 page); `#1FFD` <- `#11` (pages `#D0-#DF` mean ISA); `OUT (#E2),#D4` (slot 1) or `#D6` (slot 2); `#9FBD` <- `0` (A19-A14 = 0, AEN = 0, RESET = 0) |
| register select | `LD (#C000),A`: ISA I/O port 0 = the AY address cycle; `#FF` selects chip 0, `#FE` chip 1, then a register number |
| data | `LD (#C001),A`: port 1 = the AY data write; `LD A,(#C001)` reads the selected register |
| close | `#9FBD` <- `#40` (AEN on, no cycles reach the cards); `OUT (#E2),saved`; `#1FFD` <- `#01`; `EI` |

Per frame and chip `CardOut` writes the chip select, R0-R12, and R13 only when the player's value is not
negative (the player marks "no new envelope" with bit 7), as the player's own `ROUT` did with `OUT (#FFFD)` /
`OUT (#BFFD)`: 27 or 29 ISA writes. The probe at start selects register `#E0` (DEV_SIG) and expects `#55`,
`#AA` (reference-firmware.md §A.4).

## The program

- DSS EXE: header `"EXE"`, 0; code at file offset `#16`; load and start `#8100`; SP = `StackTop` (inside the
  program, below `#C000`). DSS loads at most `#7F00` bytes there: code (3.0 KB with its stack, 5.1 KB with TurboSound)
  plus modules must fit in 32 512 bytes; `wsaybuild.py` refuses bigger ones.
- The player's code and variables, the stack and every routine that touches the card sit below `#C000`; the
  modules may run into window 3, which is RAM again whenever the player runs.
- TurboSound: `pt3prom.asm` is included twice in sjasmplus `MODULE`s (`pa`, `pb`), each with its own variables
  and its own `CHIPSEL`; no state swapping per frame.
- Pacing: `HALT` with interrupts on; DSS's frame interrupt wakes it 50 times a second. `CardOut` runs with
  interrupts off (window 3 is the card then); DSS `ScanKey` (`#31`, ZF = 1: no key) after each frame.
- Looping: the player loops the module (`SETUP` bit 0 = 0), as Vortex Tracker does.

## The player

`pt3prom.asm` is S.V. Bulba's Vortex Tracker II v1.0 PT3 player, ROM version r.7 ((c)2004,2007 S.V.Bulba,
freeware; `PT3Play/ROM/PT3PROM.asm` of `PTxTools.7z` from bulba.untergrund.net), in the z88dk z80asm form of
Ben Baker's ZX Next sources (`~/Downloads/zx-spectrum/zx-next/source-code/benbaker76-zxnext/src/PT3PROM.asm`).
Changed: z80asm to sjasmplus syntax, `SECTION` / `PUBLIC` and the Next notes removed, the `OUT` loop in
`ROUT_A0` replaced by `JP CardOut`, `&$FF` on eight intentionally truncated expressions (same bytes). The
player keeps all PT3 versions (3.0-3.7, VT II), both tone-table generations and both portamento algorithms.

The repository's `zxasm` tool converts sources between dialects (`zxasm convert --from z80asm --to sjasmplus`
produces the same statements as the conversion here) but does not assemble; the assembler is sjasmplus.

## Limits

- PT3 only (no PT2, STC, SQT ...); two chips at most (the card has two AYs).
- Code plus modules up to 32 512 bytes per EXE (no file loading at run time).
- The card's AY clock is 1 774 400 Hz (design §6), so the pitch is 1.4 % (24 cents) above a 1.75 MHz
  Pentagon; the player's tone tables are not re-tuned.
- No pause, seek or position display; the loop never ends by itself.

## Checked (2026-10-09, unreal-ng, `SPRINTER` with `sprinter_isa_slot1=wildsound`, BIOS 3.06 HF2, DSS 1.71)

EXEs on a FAT16 disk in `ide0.slave` (drive D:), started from Flex Navigator. Reference: the same player
(`OUT` loop, IM 2) as a snapshot on `PENTAGON` (TurboSound FM; `#FF` is the emulator's `ay2` row, `#FE`
`ay1`). `compare.py` (20 ms RMS envelopes) and a per-200 ms spectral-peak ratio:

| Track | Card row | Envelope corr. | Tempo drift | Peak ratio (median) |
|---|---|---|---|---|
| `spring.pt3` (one chip), 7 s | `ws_ay1` | 0.897 | 0 ms | 1.009; 32 of 39 windows within 3 % of 1.014 |
| `summer.pt3` (TS) chip 0, 30 s | `ws_ay1` | 0.978 | 0 ms | 1.014 |
| `summer.pt3` (TS) chip 1, 9 s | `ws_ay2` | 0.914 | 0 ms | 1.000 (39 of 49 within 3 %) |
| `Melomagic.pt3` (TS, module 2 at `#9xxx-#D5DA`, through window 3) chip 1, 19 s | `ws_ay2` | 0.933 | 0 ms | 1.000 |

A key press returned to Flex Navigator with both chips at R7 = `#3F`, R8-R10 = 0 and the ISA latch at `#40`;
`SPRING.EXE 2` (slot 2 holds an NE2000) read `#FF`, `#FF` from the probe and exited.

Test tracks: `spring.pt3` ("-=+ SpRiNg +=- version for cc999", SirIl/4D, 1999, PT 3.4) from
github.com/mborik/ayplayers `songs/`; `summer.pt3` ("Dreaming of Summer", MmcM, VT II TurboSound) and
`Melomagic.pt3` ("Stellar One", Cj Splinter feat. MmcM) from the ZX Next distribution's `demos/ts-songs/`
(taken from zxart.ee, its `authors.txt` says).
