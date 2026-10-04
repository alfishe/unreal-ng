# ZX Profi PROFI-XT keyboard controller firmware

The PROFI-XT board plugs a PC/XT keyboard into the Profi's keyboard connector
(X9 on v5 boards). An 8035 (1816VE35) at 8 MHz runs this 2 KB EPROM image and
acts as a virtual Spectrum matrix with a 6th data line (KD5, the extra key EXT):
F1-F10, Home, End, PgUp, PgDn, Ins and Del become a letter plus EXT, which
BIOS 2.0 and CP/M read. The emulator runs the image on its MCS-48 core
(`[PROFI] Keyboard=XT`, class `ProfiXtKbc`). Design and facts:
[docs/inprogress/2026-10-01-profi-v3-v5](../../../docs/inprogress/2026-10-01-profi-v3-v5/README.md)
(`research-profi-keyboard.md`, `design.md` section "Keyboard").

| File | Version | Bytes | CRC32 | SHA-1 |
|---|---|---|---|---|
| `profi-xt-v1.27.rom` | "JV KRAMIS (C) 28.10.1992 vers 1.27, no autoselect, fool-proof, no wait!" (banner at `7A0h`), **reconstructed** | 2048 | `59C7A98C` | `4bb9801a9bea5829c2099b3416e7ee00cff7229e` |

## This image is a reconstruction

The only dump known is speccy4ever `rom/PROFI_XT-9A8E2686.ROM` (2048 bytes,
CRC32 `9A8E2686`, SHA-1 `63df86b3d1c6fd8e024b2da77d07972e9046e1e7`). It cannot
work as dumped:

- there is no `EN I` (opcode `05`) anywhere in it, so the keyboard's receive
  interrupt (vector `003h` -> `069h`, one interrupt per XT clock edge) never runs
  and no key is ever received;
- the main loop at `02Fh`, the target of every `JMP 02Fh`, has to fetch the next
  scan code, and the only routine that does is at `05Fh`; the dumped bytes there
  never call it.

The dump's bytes `02Eh`-`032h` are `C8 11 20 17 37` (DEC R0; INC @R1; XCH A,@R0;
INC A; CPL A). This image replaces them with:

| Address | Dump | Here | Instruction |
|---|---|---|---|
| `02Eh` | `C8` | `05` | `EN I` |
| `02Fh` | `11` | `14` | `CALL 05Fh` |
| `030h` | `20` | `5F` | |
| `031h` | `17` | `00` | `NOP` |
| `032h` | `37` | `00` | `NOP` |

Nothing else differs (`cmp -l` lists exactly these 5 bytes). Most likely the
EPROM lost bits there (or the dump was taken from a damaged chip); the replaced
bytes are what the surrounding code needs, not a copy of a verified original.

How it was checked (2026-10-03): an 8035 simulator with a model of the board
from the schematic `PROFI-XT.PDF` (materials/keyboard/sim: P1 = A8-A15, MOVX
address bit 7 = 0 -> output latch, bit 5 = 0 -> end of the Z80 wait, T1 = the
WAIT flip-flop, T0 = the inverted data bit, /INT = the keyboard clock) fed every
XT make and break code, the E0 keys, Shift, Num Lock, Scroll Lock, the `#AAFE`
mode switch and Ctrl + Alt + Del. Every key gives the matrix positions the
emulators ZXMAK2, Xpeccy-plus, Karabas-Pro and pico-spec use, and BIOS 2.0's EXT
test (KD5 of half-row A14) - in that simulator and in this emulator's MCS-48 core
(`core/tests/emulator/io/keyboard/profixtkbc_test.cpp`).

A clean re-dump of a real v1.27 EPROM would settle the 5 bytes. Until then
`[PROFI] Keyboard=XTTable` runs the controller from its key table without this
image, and `[ROM] PROFIXT=` takes another image (a re-dump) in place of this one.
The emulator logs a warning when it is given the unpatched dump.

Source: the dump from the speccy4ever collection
([speccy4ever.speccy.org](https://speccy4ever.speccy.org/_PR.htm)); the board's
schematic `PROFI-XT.PDF` from the same collection. Clone firmware distributed
freely in the ZX Spectrum community, see [`../README-ROMS.md`](../README-ROMS.md).
