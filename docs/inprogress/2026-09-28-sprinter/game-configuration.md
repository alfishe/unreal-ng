# The "Game" PLD configuration (V10)

How PLD configurations work in general, on the hardware, in MAME and here: [pld-configurations.md](pld-configurations.md).

**Status:** implemented 2026-10-03 (branch `sprinter-pld-game`). Closes gap V10 of
[mame-gap-analysis.md](mame-gap-analysis.md) and the GAME_00 / LDConf rows of [demo-status.md](demo-status.md).

Most of the Sprinter is one programmable logic chip (the PLD). The BIOS loads its standard logic at power-on, but a
program may load other logic (a *bitstream*) at any time: the machine then becomes a slightly different machine. The
emulator models each known bitstream as a *configuration module* ([tdd-ports-memory.md](tdd-ports-memory.md) §6.1,
`SprinterPldConfiguration`). Until now there was one, Standard. This page adds the second, **Game**.

## 1. Which bitstream, and who loads it

| File | Where | Bytes | Full hash (FNV-1a of the 473 720 writes) | Head hash (MAME's, first 4 096 writes) |
|---|---|---|---|---|
| `GAME_00.ACX` | `C:\DEMOS\GAME_00` of the MAME pack's disk; `firmware/pld/special/game-00.acx` of the materials | 65 536 (the stream is bytes `#0100-#E84E`) | `#C0FA3055` | `#3861CFA4` (MAME's Game constant) |
| `GC.BIN` | `C:\DEMOS\LDCONF` | 59 215 | `#C0FA3055` (the same stream) | `#3861CFA4` |
| `titd-k30.acx` | materials, "Thunder in the Deep" | 59 232 | `#C0FA3055` | `#3861CFA4` |

So GAME_00, LDConf's `START.BAT` and the game the configuration was made for all load **one** bitstream.
LDConf's `STREAM.304` is the shipped Standard (`#FC0928F2`); `STREAM.300/303/305` are unknown to the emulator and
run as Standard (as before).

**How it gets into the PLD** (`GAME_00\RELOAD.ASZ`, the program's own source next to it):

1. The program reads the 64 KB file into RAM pages `#1C-#1F`, copies it into the 64 KB fast RAM so that fast RAM
   `#1000` holds the stream (file `#0100`), and writes `"ACEX_30K_LOADING"` to fast RAM `#FEF0`.
2. It writes its return address (`RELOAD_RET`) to page `#41` `#FFF4`, its window pages to `#FFF0-#FFF3`
   and `"ZX"` to `#FFFE`.
3. It patches the port table so that `OUT (C)` with `B = 1` reaches code `#2E` (PLD reload) and resets.
4. The ROM loader (page `#C`) finds the signature, sets the Z84C15's chip-select boundary to `#F0` and streams the
   473 720 writes from fast RAM instead of ROM ([loader README](../../disasm/rom/sprinter/loader/README.md)).
5. The emulator's sink hashes the stream: full hash `#C0FA3055` → the **Game** module
   (`PortDecoder_Sprinter::FinishLoad`, journal event `pld_configured ... module Game ... matched by the full hash`).
6. The BIOS starts and reads cell `#EE` (below): `#41`, so it maps page `#41` into window 3, sets the windows from
   `#FFF0-#FFF3` and jumps to `RELOAD_RET`. The program draws on the new logic.

LDConf (`ldconf c gc.bin e scroll.exe`) does the same through the BIOS function `RST_CONF.CUSTOM`, runs `SCROLL.EXE`
on the new logic and then loads the ROM's bitstream again (`Reload_proc` with block 0): the machine is Standard again.

## 2. What it changes against Standard

There are **no sources** of this configuration in any of the PLD source trees (`sprinter200x-altera-1k30`,
`sprinter-computer-hard`, `sp97-flex`; only the bitstreams and the loader program are in `special/`), and a
bitstream cannot be read back into logic. The table is derived from three independent sources and says which:

| Area | Game | Source |
|---|---|---|
| Cells `#C0-#FF` after the load | Standard's, except **cell `#EE` = `#41`** | MAME (`m_ram_pages[0x2e] = 0x41`, `sprinter.cpp:1162`); the BIOS (3.04 page 8 `#02ED`, 3.07 `EXP.asm` `Reset_Handler`: `SET_PORTS #EE <- 0` and the old value back - non-zero = "return to the program in page `#41`"); the program, which relies on that return |
| Picture | every square graphics 320 x 256 colors from any byte corner of a 1024 x 256 virtual screen, with the per-square **grid offset** (section 3); no text squares | `RELOAD.ASZ` (the author's description of the mode bytes); MAME `screen_update_game` |
| Port codes, port table | Standard's (the table is RAM page `#40`, written by the BIOS) | the program uses `PAGE3`, `PORT_Y`, `#FE`, code `#2E` exactly as on Standard; MAME has no other decoding |
| Memory windows, fast RAM | Standard's | the program maps video RAM with `PAGE3 = #50`; MAME uses `update_memory` unchanged |
| Accelerator | Standard's | `RELOAD.ASZ` fills the mode table and draws the grid with `LD D,D` / `LD C,C` / `LD E,E` / `LD L,L` exactly as on Standard |
| Sound, keyboard, mouse, IDE, floppy, CMOS | Standard's | the program and DSS run on (DSS and the BIOS work, "except their screen functions", `RELOAD.ASZ`) |
| Frame INT | Standard's (blank + INT squares of the mode table) | the program waits with `EI : HALT`; MAME keeps its INT |

Why cell `#EE` comes "with the bitstream": cells `#C0-#FF` live in the PLD's embedded RAM (`DCP.TDF`, `MEM`), whose
contents are part of the bitstream (`DCP.MIF`), so a load sets them. Standard's `DCP.MIF` gives the table the emulator
already used at power-on (`#EE` = 0). The emulator sets the cells from the chosen module
(`SprinterPldConfiguration::InitialCells`, hook 5) when a load **changes** the configuration (power-on, Standard ->
Game, Game -> Standard; `SprinterPldState::moduleBeforeLoad`). A reload of the running configuration keeps them: the
emulator's RESET button reloads the PLD, and the launchers' reset intercept (cell `#EE` = `#41`, `/ret-fn`) survives
that button (`SprinterZxResetFn_Test`, the owner's checked behavior) - see §7.

## 3. The picture: squares and the grid offset

The mode table is the Standard one (square `(a, b)`: row `1 + 2a + #80 x RGMOD.0`, column `#300 + 4b`, four bytes).

```text
Mode0  bits 7-6  palette (pens palette x 256 + byte)
       bits 7-5 = %111: border (pen #400 + 9 x border), with bits 3-2 = %11: blank (#400)
       bit 2    grid offset: Mode3 becomes the offset when the beam leaves this square
       bits 1-0 source column bits 9-8
Mode1  source column bits 7-0       (any byte: the corner need not be a multiple of 8)
Mode2  source row
Mode3  grid offset: bits 3-0 X (2 beam pixels = one 320 pixel each), bits 7-4 Y (lines)
```

A square shows 8 x 8 pixels of the virtual screen from its corner, one byte per two beam pixels; columns wrap at
1 024 and rows at 256.

The **grid offset** is one register in beam order. With offset `(ox, oy)` the beam pixel `(x, y)` is drawn from
square coordinates `a16 = (x + 2 ox - 48 - holdX) mod 896`, `b8 = (y + oy - 16 - holdY) mod lines`. When the beam
finishes a square whose Mode0 has bit 2, Mode3 is loaded; the new offset acts from the next square on and stays until
another such square - across squares, lines and frames ("if bit 2 was not set, the offset REMAINS what was set
before"). Blank squares (`%1111 11xx`) have bit 2 too: the blanking squares load their Mode3, which is why
`RELOAD.ASZ` clears every Mode3 "including border and blank!". The beam reaches square 55 just before square 0
(`a16` wraps), so square 55 sets the next line's square 0 ("the offset of square 0 ... is set in the 55th").

**Worked example.** `RELOAD.ASZ` sets square 37 of rows 4-29 to Mode0 = `#65` (palette 1, bit 2, column bit 8) with
Mode3 = `#88`. When the beam leaves square 37 the offset becomes X 8, Y 8 - 16 beam pixels and 8 lines - so square
38 is fetched one column right and one row down: the right part of the picture shows the grid shifted by one square
in both directions. Squares 40-55 of the row are blank with Mode3 = 0: they set the offset back to 0 before the next
line starts.

**The register is machine state.** It depends on every square the beam passed, the blanking included, so the
emulator runs it on every catch-up of the screen, drawn or not (`SprinterBeamVideo`, `ScreenSprinter::DrawTo`), and
every frame start closes the previous frame (`ScreenSprinter::InitFrame`). A turbo-skipped frame, ScreenHQ
off or a TTD replay therefore end with the same register; it travels in the TTD blob (below).

**Where MAME differs** (deliberately; [mame-gap-analysis.md](mame-gap-analysis.md) V10 / V11):

- MAME restarts every line with `lookback_scroll`, which never looks at another square (gap analysis §3 item 8):
  the offset does not carry from square 55 to the next line's square 0.
- MAME switches the offset in the middle of an offset square when the physical square boundary falls inside it;
  the author says it acts "on the next square".
- MAME's source address runs on into the next row at column 1 024; here it wraps inside the virtual screen.
- Mode0 bits 5-4 (the author: "graphics / text, must be 0" and "640 / 320 points") are not modeled; MAME draws every
  square 320, and GAME_00 uses only `#61`, `#62`, `#65`, `#66` (bit 5 set, bit 4 clear), `#F8` and `#FC-#FE`.

Measured: MAME's frame 1000 of GAME_00 drawn again from MAME's own video RAM of that moment differs in 1.0 % of the
graphics pixels (the mid-square switch at the edges of the offset areas); the closest frame of our own run of
GAME_00 differs from MAME's screenshot in 0.8 % of the pixels.

## 4. Implementation

| Piece | File |
|---|---|
| The module (descriptor, cells, picture, state) | `core/src/emulator/ports/models/sprinter/sprinterpldgame.{h,cpp}` |
| The picture and the grid-offset register | `core/src/emulator/video/sprinter/sprintergamevideo.{h,cpp}` |
| Hook 3b `BeamVideo()` (a picture with state), hook 5 `InitialCells()` | `sprinterpldconfiguration.h`, `sprintervideorenderer.h` (`SprinterBeamVideo`) |
| Registry: Standard (0), Game (1) | `sprinterpldconfiguration.cpp` |
| Cells set when a load changes the configuration, the picture started at the load's frame and T | `portdecoder_sprinter.cpp` (`NoteModuleBeforeLoad`, `ApplyInitialCells`, `PerformPendingReset`) |
| Drawing through the module's picture (catch-up, batch, indexed frame, screen state), the frame closed at its start | `screensprinter.cpp` (`DrawTo`, `DrawRange`, `IndexedFrame`, `InitFrame`) |
| Why a module runs | `PortDecoder_Sprinter::ModuleSelection` |

**Back to Standard.** Any load chooses the module again: the RESET button (the ROM's bitstream), code `#2E` with
the ROM stream (LDConf's way back), or another program's bitstream. Standard's cells come back with it (`#EE` = 0),
and Standard draws again.

**Standard stays bit-exact.** A Standard-only session runs exactly as before: the cells are set at power-on as
before, a reload of Standard keeps them as before, the Standard picture and every port and memory path are untouched
(the full test suite and the TTD corpus pass unchanged but for the blob's module room). No hot path changed: the
per-access code is untouched, and `ScreenSprinter::DrawTo` asks one cached pointer per catch-up.

**TTD.** The module travels by name in the `SprinterPld` blob (id 25) with its 16-byte state
(`SprinterGameVideoState`; the blob holds the frame-start state: the offset as the frame began and the frame number,
the beam position 0); the blob's module room grew from 0 to 16 bytes, so `testdata/machines/sprinter/ttd/boot.ttd`
was re-recorded. The frame start (`ScreenSprinter::InitFrame`, before the checkpoint) closes the previous frame, so a
checkpoint holds the register after every square of the frame before, whatever was drawn: the blob is the same with
ScreenHQ on or off, turbo-skipped frames or a replay (`TTDSprinterMachine_Test.GameModule_ReplaysBitExactWithAnyRendering`).
How far the screen has caught up inside a frame is not in the blob: it depends on when the screen last drew (a
seek's repaint runs a few T), and running from the frame start gives the same register again. Found with the TTD of the GAME_00 run: the first version closed the frame in the decoder's frame-end hook,
which runs after the checkpoint, so a seek restored `beam_t` 71 680 and the first frame after a restore was not drawn.

## 5. Reports and automation

All five automation surfaces render the same core reports, so they all carry it:

| Report | Field |
|---|---|
| `GET /state/sprinter` (CLI `state sprinter`, Lua / Python `sprinter_state()`, MCP aspect `sprinter`) | `pld.module`, `pld.selected_by` (`full_hash`, `head_hash`, `unknown_bitstream`, `watchdog`, `loading`), `pld.why`, `pld.cell_EE`, `pld.game` (the grid-offset register), `video.renderer` |
| `GET /state/sprinter/bios` (CLI `state sprinter bios`, `sprinter_bios()`, MCP `sprinter_bios`) | `pld` (module, why, hashes, cell `#EE`) |
| `GET /state/sprinter/zx-mode` (CLI `state sprinter zx`, `sprinter_zx_mode()`, MCP `sprinter_zx_mode`) | `pld` |
| PLD journal (`/state/sprinter/pld-journal`, `sprinter_pld_journal`) | `pld_load` and `pld_configured` (module, hashes, "matched by the full hash" / "by MAME's head hash only" / "unknown bitstream, Standard runs") |
| GUI status bar, `/state/screen` (`DescribeScreenState`) | "PLD Game: 320x256 256c" |

Recipe: [.recipe/machines/sprinter.md](../../../.recipe/machines/sprinter.md) "PLD configurations: Standard and Game".

## 6. Tests

| Test | What |
|---|---|
| `SprinterGameVideo_Test.*` | the square rules, the grid offset in beam order (next square, across lines and frames, blank squares, square 55), any catch-up cadence gives the same register and pixels, MAME's frame from MAME's video RAM |
| `SprinterPldGame_Test.*` | selection by full / head hash, unknown stream → Standard, cells `#EE`, back to Standard by reload and by RESET, the reports, the screen, the TTD blob |
| `SprinterGameConfig_Test.RealHdd_Game00ReloadsThePld` (`UNREAL_SPRINTER_HDD`) | GAME_00.EXE from the MAME pack's disk: Game by the full hash, the BIOS returns into the program, the grid scrolls, the closest frame against MAME's capture |
| `SprinterGameConfig_Test.RealHdd_Test005AndTest010` (`UNREAL_SPRINTER_HDD`) | the two other GAME_00 programs reach the Game module and run on it |
| `SprinterGameConfig_Test.RealHdd_LdconfStartBat` (`UNREAL_SPRINTER_HDD`) | LDConf's `START.BAT`: Game for `SCROLL.EXE`, then Standard again |

MAME references: `testdata/machines/sprinter/reference/game/` (README there). TEST_005 and TEST_010 were compared by
eye with MAME's frame 1000 of the same programs (disk copies whose `SYSTEM.BAT` starts them): the same scenes, the
same palettes (TEST_005's is dark on MAME too), the scroll at another position. Their pictures use Mode0 `#60`-`#65`
(bit 5 set, bit 4 clear) like GAME_00.

## 7. Open

- Mode0 bits 5-4 (640 points, text): no known program sets them; a capture from a real board or the configuration's
  sources would settle them.
- Whether the PLD's `/RESET` clears the grid-offset register (the emulator keeps it over a soft reset and clears it at
  a load); no program depends on it.
- The RESET button and the embedded RAM: on the chip any configuration load sets the cells from the bitstream, yet
  the launchers' reset intercept in cell `#EE` survives the RESET button (owner-checked, `SprinterZxResetFn_Test`),
  while the emulator models that button as a PLD reload (tdd-ports-memory §7). Either the board's button does not
  reload the PLD, or the intercept works another way; a board would settle it. Until then cells are set only when the
  configuration changes.
- ~~The DooM and Video configurations (gap analysis V12) are still missing.~~ Not planned (2026-10-03): they exist only as Sprinter 97 (FLEX EPF10K10) bitstreams, no Sp2000 build exists, and the Sp2000 merged their functions into Standard (DooM's line stretching = the accelerator's `#C7` scale register; Video's disk-to-memory logic = `HDD_FLIP` / `HDDR`); the Sp2000 DOOM demo and the 2026 video player run on Standard. Details and head hashes: [pld-configurations.md](pld-configurations.md) §6.
