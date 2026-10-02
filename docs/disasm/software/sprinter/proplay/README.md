# ProPlay 0.5.91 (Sprinter): General Sound MOD player

A disassembly of `PROPLAY.EXE`, the only MOD player for the Peters Plus Sprinter Sp2000 found on the
MAME-pack system disk. It plays a MOD file on a **General Sound** card (or a NeoGS) that sits on a
**ZX-bus adapter** in one of the Sprinter's two ISA slots. It is the end-to-end test program for the
design in [docs/inprogress/2026-10-02-sprinter-isa/](../../../../inprogress/2026-10-02-sprinter-isa/tdd.md).

| | |
|---|---|
| Program | "ProPlay - General Sound MOD player v0.5.91 (07.02.23) by Miroshnichenko Aleksandr aka Sayman@SprinterTeam" |
| File | `BIN/PROPLAY.EXE` (also in the disk root) of the MAME pack's DSS 1.71 system disk; also on the DSS 1.71 boot floppy |
| Size / CRC32 | 1 126 bytes, `2d06da86` (code after the 22-byte DSS EXE header: 1 104 bytes, `6f232150`) |
| Load / start / SP | `#8100` / `#8100` / `#BFFF` |
| Listing | [proplay.asm](proplay.asm): z80dasm 1.2.0 output, hand comments above the routines below |
| Usage | `PROPLAY.EXE NAME.MOD` |

## How it reaches the card

The Sprinter has no I/O port path to its ISA slots. A program maps the slot into CPU window 3
(`#C000-#FFFF`) and reads or writes memory there. Every access becomes one ISA cycle:

| Step | Code | Effect |
|---|---|---|
| 1 | `IN A,(#E2)` | save the page currently in window 3 |
| 2 | `#1FFD` <- `#11` | Scorpion extended-page bit on: pages `#D0-#DF` now mean "ISA", not RAM |
| 3 | `OUT (#E2),#D4` or `#D6` | window 3 = ISA **I/O** space of slot 0 (`#D4`) or slot 1 (`#D6`) |
| 4 | `#9FBD` <- `0` | ISA address bits A19-A14 = 0, AEN = 0, RESET = 0 |
| 5 | `LD (#C0BB),A` / `LD A,(#C0BB)` | ISA I/O write / read of port `#00BB` = the GS command / status port |
| 6 | `LD (#C0B3),A` / `LD A,(#C0B3)` | ISA I/O port `#00B3` = the GS data port |
| 7 | `#1FFD` <- `#01`, `OUT (#E2),saved` | back to RAM |

Worked example: with `#9FBD = 0` and window 3 = `#D4`, the instruction `LD A,(#C0BB)` makes the CPU
read address `#C0BB`. The board drops A15-A14 (they select the window), puts CPU A13-A0 = `#00BB` on
ISA A13-A0 and `#9FBD` bits 5-0 = 0 on A19-A14: ISA I/O read of port `#000BB`, slot 0. The ZX-bus
adapter turns it into a Spectrum `IN` from port `#xxBB`, which the General Sound answers with its
status byte.

## Routines

| Address | Name (ours) | What it does |
|---|---|---|
| `#8108` | IsaOpen | steps 1-4; the slot (0 / 1) is the operand at `#8109`, set by GsDetect |
| `#8124` | IsaClose | step 7 (the saved page is the operand at `#812C`) |
| `#8186` | GsCommand | write `#C0BB`, wait for status bit 0 = 0 |
| `#8190` | GsSendData | write `#C0B3`, wait for status bit 7 = 0 |
| `#81AA` | GsRestart | command `#F3` (warm restart of the GS firmware) |
| `#81BF` | GsSendBlock | DE bytes from (HL) through GsSendData |
| `#81D0` | GsLoadModuleStart | commands `#30` (load module) and `#D1` (open stream) |
| `#81E7` | GsLoadModuleEnd | command `#D2` (close stream) |
| `#81F3` | GsPlayModule | data = module number, command `#31` (play module) |
| `#8204` | GsDetect | status read in slot 0, then slot 1; `#FF` = no card; prints "General Sound found at slot: N" or "not found." |
| `#83CC` | Main | detect, open the file (DSS `#11`), set PORT_Y (`#89`) to `#C0`, read 16 KB pieces to `#4000` (DSS `#13`) and stream each byte, close (DSS `#12`), close the stream, play module 0, exit to DSS while the card plays |

The command meanings are the standard General Sound protocol (`#F3` warm restart, `#30` load module,
`#D1` / `#D2` open / close stream, `#31` play module). The player does no sound work itself: once the
module is on the card, the card's own firmware plays it.

## Other ports it touches

- `OUT (#A2),A` at `#83F7`: port `#A2` is the window-1 page register (port-table code `#E9`, BIOS 3.04
  table); the player puts its file buffer page at `#4000` before reading the file there.
- `IN A,(#89)` / `OUT (#89),#C0` at `#8411`: PORT_Y (code `#C4`); `#C0` turns the Spectrum screen shadow
  off. It is saved but not restored in the exit path that was read.
