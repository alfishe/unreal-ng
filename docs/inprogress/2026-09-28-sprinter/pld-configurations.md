# PLD configurations: hardware, MAME and unreal-ng

**Status:** overview, 2026-10-03. It ties together the loader ([tdd-ports-memory.md](tdd-ports-memory.md) §6), the
module interface (§6.1 there), the Game module ([game-configuration.md](game-configuration.md)) and the MAME
comparison ([mame-gap-analysis.md](mame-gap-analysis.md) V10-V12, B2, B3, C7). Read this page first; the
others hold the details.

**In one sentence:** neither MAME nor unreal-ng runs the bitstream. Both recognize which bitstream was loaded by a
hash and switch to a hand-written model of that logic, a *module*. They differ in how the load is caught, how
faithfully it is timed, and how exactly each module's logic is reproduced.

## 1. On the hardware

Most of the Sprinter is one programmable logic chip, an Altera ACEX 1K30 (the PLD). Its configuration lives in SRAM
cells and is lost at power-off.

- **Power-on.** The ROM loader (ROM page `#C`, `#0000-#009B`; [loader disassembly](../../disasm/rom/sprinter/loader/README.md))
  streams the Standard bitstream into the PLD: 59 215 bytes, each written 8 times one bit at a time, so
  **473 720 writes**. Each write in the configuration state is one DCLK with D0.
- **Any program can load other logic.** It puts a bitstream into fast RAM (stream at `#1000`) with the signature
  `ACEX_30K_LOADING` at fast RAM `#FEF0`, patches the port table so that `OUT (C)` reaches code `#2E` (PLD reload)
  and resets. The loader finds the signature and streams the program's bitstream instead of the ROM's. Example:
  `C:\DEMOS\GAME_00\RELOAD.ASZ` (the program's own source); LDConf does the same through the BIOS function
  `RST_CONF.CUSTOM`.
- **The cells `#C0-#FF` come with the bitstream.** The port-table cells (CNF and its neighbors) live in the PLD's
  embedded RAM (`DCP.TDF`, `MEM`); their initial contents are part of the bitstream (`DCP.MIF` for Standard). So a
  load also sets them. Example: the Game bitstream brings cell `#EE` = `#41`, and the BIOS then returns into the
  program (page `#41`, address from `#FFF4`) instead of starting DSS.
- **The loader never stops on its own.** Its loop keeps streaming the `#FF` filler after the last byte until the
  configured PLD resets the CPU.

Open on the hardware side (needs the PLD sources or a board): whether CONF_DONE rises exactly at write 473 720,
how many extra DCLKs the ACEX takes after it (FLEX/ACEX need about 10: two more bytes would be 16 writes), and how
the CPU reset follows.

## 2. In MAME (`src/mame/sinclair/sprinter.cpp`)

| What | MAME |
|---|---|
| Catching the load | hashes only the **first 4 096 writes** ("head hash") and resets the CPU right there, in the middle of the stream (`:1156-1163`) |
| Known bitstreams | Standard and Game (head hash `#3861CFA4`) |
| Game | sets cell `#EE` = `#41` (`m_ram_pages[0x2e] = 0x41`, `:1162`), draws with `screen_update_game` (`:499-545`) |
| Game scroll | `lookback_scroll` (`:555-568`): every line starts from scratch, the offset does not carry from square 55 to the next line's square 0; the offset can change in the middle of a square; the source address runs on into the next row at column 1 024 |
| Loader timing | 113 T per bitstream byte (no Z84C15 wait generator) |
| Soft reset | keeps the configuration (`:1588-1600`) |
| Unknown bitstream | no module concept: Standard logic stays |
| DooM, Video configurations | none; they exist only as Sprinter 97 bitstreams and are not needed on the Sp2000 (§6) |

## 3. In unreal-ng

| What | unreal-ng | Where |
|---|---|---|
| Catching the load | the whole stream: every CPU memory write during loading goes to a sink, 473 720 writes; a watchdog ends a load that never reaches the count (Standard + warning). `[SPRINTER] FastStart=1` (test default) skips the stream and starts as the loader would leave the machine; an equivalence test keeps both paths identical | `PortDecoder_Sprinter::FinishLoad`, [tdd-ports-memory.md](tdd-ports-memory.md) §6 |
| Identification | two hashes: the **full stream** (FNV-1a of the 473 720 writes, exact firmware) and the **head** (first 4 096 writes, MAME's constants reusable); lookup by full hash first, head hash as the fallback | §6.1 there |
| Module interface | `SprinterPldConfiguration`: descriptor `{ name, fullStreamHash, headHash, streams }`, hooks 1 port decoding, 2 memory mapping, 3 video, 3b video with state (`BeamVideo`), 4 accelerator, 5 initial cells (`InitialCells`); every hook a module does not override falls through to Standard; Standard itself is a module | §6.1 there |
| Known modules | Standard (0); Game (1) since 2026-10-03 | [game-configuration.md](game-configuration.md) |
| Unknown bitstream | runs as Standard, and the PLD journal and reports say so: `unknown PLD bitstream, full hash ..., head hash ..., using Standard`, `pld.selected_by` (example: LDConf's `STREAM.300/303/305`) | §6.1 there |
| Loader timing | 142 T per byte: the Z84C15 wait generator (WCR / MWBR) is modeled | [mame-gap-analysis.md](mame-gap-analysis.md) C7 |
| RESET button | reloads the PLD, as on the board | §7 there, gap B3 |
| Game scroll | the grid offset is one register in beam order: Mode3 of a square whose Mode0 has bit 2 is loaded when the beam leaves that square and acts from the next square on, across squares, lines and frames (the author's description in `RELOAD.ASZ`); it is machine state, so it also runs in turbo-skipped frames, with ScreenHQ off and in TTD replay | [game-configuration.md](game-configuration.md) §3 |
| Cells at a load | set from the new module's `InitialCells` only when the load **changes** the configuration (power-on, Standard -> Game, Game -> Standard); a reload of the running configuration keeps them, because the launchers' `/ret-fn` reset intercept (cell `#EE`) must survive the RESET button (`SprinterZxResetFn_Test`) | [game-configuration.md](game-configuration.md) §2, §7 |
| TTD | the module id (saved by name) and the module's state are in the checkpoint and the snapshot; a replay across a reload restores the right module | §6.1 there |
| Visibility | the PLD journal (`pld_load`, `pld_configured ... module Game ... matched by the full hash`), the ZX-mode report and the BIOS report name the loaded module and why | [tdd-zx-mode.md](tdd-zx-mode.md) §12 |

Worked example (GAME_00): the program copies `GAME_00.ACX` to fast RAM, writes the signature and its return
address, patches the port table and resets. The loader streams 473 720 writes; their full hash `#C0FA3055` is the
Game module's, so Game is activated: cell `#EE` becomes `#41`, the BIOS jumps back into the program, and the
picture now comes from the Game renderer with its grid offset. `GC.BIN` (LDConf) and `titd-k30.acx` ("Thunder in
the Deep") are the same stream. LDConf later loads block 0 of the ROM again: the full hash is Standard's, the cells
return to `DCP.MIF`'s, and the machine is Standard.

## 4. Why the two emulators differ

- **Executing the bitstream is not an option for either.** A bitstream cannot be turned back into logic, and a
  gate-level model of the ACEX would be far too slow. So both use modules.
- **MAME takes shortcuts for simplicity:** the head hash and an early reset (no full stream), no wait generator,
  a soft reset that keeps the configuration, a simplified per-line scroll.
- **unreal-ng follows the PLD sources, the board and the author's descriptions,** because it needs the real load
  time (1.9 s at 3.5 MHz; programs and tests see it), deterministic TTD (the module and its state must be
  recorded), and a RESET button that behaves like the board's.
- **For Game nobody has the PLD sources** (none of `sprinter200x-altera-1k30`, `sprinter-computer-hard`, `sp97-flex`
  has it; only the bitstreams and the loader program are in `special/`). Both models are reconstructions. The
  difference is measurable: MAME's frame 1000 of GAME_00 redrawn by our renderer from MAME's own video RAM differs
  in 1.0 % of the graphics pixels, and our own run differs from MAME's screenshot in 0.8 % - exactly at the edges of
  the scrolled areas, where MAME switches the offset mid-square.

## 5. Open items

- The cells-at-load rule (only on a change of configuration) needs a board check: does the RESET button's reload
  restore `DCP.MIF`'s cell `#EE` on the hardware, and how does `/ret-fn` survive it there?
- Mode0 bits 5-4 of the Game configuration ("graphics / text, must be 0", "640 / 320 points") are not modeled; MAME
  draws every square 320 and GAME_00 uses only `#61`, `#62`, `#65`, `#66`, `#F8`, `#FC-#FE`.
- ~~DooM and Video configurations~~ - **closed 2026-10-03, not planned**: Sprinter 97 bitstreams only, their
  functions are in the Sp2000's Standard; the full account is §6.
- LDConf `STREAM.300/303/305`: unknown bitstreams, run as Standard.
- Hardware timing of the load end (CONF_DONE, extra DCLKs, the CPU reset), §1.

## 6. DooM and Video: Sprinter 97 legacy

**Decision (2026-10-03): no DooM or Video module is planned. No Sp2000 software needs them.** This section holds
the whole finding, so that the question does not have to be researched again.

### 6.1 What the two configurations were

Ivan Mak's manual (`sp2000_man.pdf`, 15.08.2003, §2.4-2.6) lists three special configurations besides Standard:
Game-1, DooM and Video. The descriptions there are the **Sprinter 97** ones, copied unchanged.

- **DooM** is Game-1 plus an accelerator that stretches or shrinks a vertical or horizontal line in hardware:
  the operation that draws the walls of a Doom-style 3D view. It was made for the DOOM demo port. In the Sp97 PLD
  design (`SPRINT08.TDF`, preset block "Sprinter-DOOM", `ACCELERAT = 3`) the accelerator adds a step register
  `AAGR` to a fixed-point index `XCNT:XAGR` on every access; a write to the port with DCP code `xxxxx111` sets
  the step. Worked example: a step of 0.5 reads every source byte twice, so a 64-pixel wall column fills 128 lines.
- **Video** is Game-1 plus a path that writes hard-disk data straight into video memory while a sector is
  being read. It was made for Ivan Mak's FLC2 player (`FLC2.ASZ`, 1998): the player starts a multi-sector IDE
  read, maps a screen page and runs chains of `POP DE` over it; each read fetches the next disk word into video
  RAM, 87 frames of 128 sectors. The last Video build added a mode that doubles pixels (160 x 128 on the whole
  screen); its sources are lost, so this is not verified.

On the Sp97 these configurations were not loaded from files: a BIOS build carried one in a configuration slot
(`SPRINPR2.BAT` puts the DooM stream into ROM slot `#14100`), and the 1999 DOOM demo switches the slot through
the BIOS.

### 6.2 The files that survive

All of them are Sprinter 97 bitstreams for an Altera **FLEX EPF10K10**, 14 751 bytes each (header
`FF FF 62 7B 25 00`). The Sp2000's PLD is an **ACEX 1K30**, whose stream is 59 215 bytes (header
`FF FF 62 7B 39 00`).

| File (materials name) | Original name | Date | Configuration | SHA-256 | Head hash |
|---|---|---|---|---|---|
| `doom/sp97-sprint08-dm.bin` | `SPRINT08.DM` | 1999-01-23 | DooM | `2e0b6d85389303bce95480d6c61c49879b332da904a6211ff8c1b15b8d3d1e91` | `#8C0B2C6D` |
| `video/sp97-sprint04-vid.bin` | `SPRINT04.VID` | 1998-11-16 | Video (`SPRINT04` design) | `25850a1e76a2c5c1d0c0c8e27a9dcfef72c289b82cbc44be68229a247bf704a5` | `#3664CFCD` |
| `video/sp97-sprint08-vid.bin` | `SPRINT08.VID` | 1999-02-09 | Video (`SPRINT08.TDF`) | `43b2cce9dd352b9fc7890acd3a076e8383c6cca9d1e038778e44304dd7b8614b` | `#C670F611` |
| `video/sp97-sprint11-vid.bin` | `SPRINT11.VID` | 2000-08-17 | Video, newest (sources lost) | `50f249182cf6bec0c119db7e9b97a5f2ea0e25ead34d1fc28d35f5efcddd8151` | `#4F7C1F52` |

The head hash is MAME's (the first 4 096 loader writes, §3); it is listed only to tell the files apart. The full
hash does not apply: the Sp2000 loader always streams 473 720 writes, these streams have 118 008.

Where they came from (both links checked, HTTP 200, 2026-10-03):

- [sp97-bios-master.zip](https://zxgit.org/Sprinter/97/raw/branch/master/master/sp97-bios-master.zip) from
  [zxgit.org/Sprinter/97](https://zxgit.org/Sprinter/97) (commit `56d4b1e`): all four bitstreams and the build
  script `SPRINPR2.BAT`.
- [2_DEMO.ZIP](https://winglion.sprinter.ru/soft/2_DEMO.ZIP) (winglion archive): the same `SPRINT08.DM`, the demo
  sources `DOOM.ASZ` (1999, Sp97), `DOOM2.ASZ` (2002, Sp2000) and the player `FLC2.ASZ` / `FLC2.COM`.

Copies, checksums of every file and the research notes are in the owner's materials, `firmware/pld/special/`
(README there); they are not in the repository.

No Sp2000 (ACEX 1K30) build of either configuration was found in any archive, repository, BIOS image, disk image
or CD that was checked, including the MAME pack's hard disk and the Peters Plus CD.

### 6.3 Why the Sp2000 cannot load them

The Sp2000's ROM loader (§1) streams a fixed 59 215 bytes into an ACEX 1K30. A FLEX 10K10 stream is a different
chip's configuration: different size, header and cell layout. Loaded through the Sp2000 loader it would not
configure the chip at all. Running them would mean emulating the Sp97 board (another chip, another memory and
port map), which is out of scope.

### 6.4 What replaced them on the Sp2000

The manual (§1.4) says that on the Sp2000 these configurations are merged into one PLD firmware. The standard
sources confirm it for DooM and suggest it for Video:

- **DooM -> the standard accelerator's scale register, code `#C7`.** `ACCELER.TDF` of the standard 1K30 design has
  `WR_C7` (DCP code `1100X111`), `ALT_ACC` and `XCNT_AGR = (XCNT,XAGR) + AAGR`: the Sp97 DooM accelerator, now
  always present. MAME models it (`m_alt_acc`), and so does unreal-ng (`SprinterAccelerator::OnScaleWrite`,
  [s5-accelerator-outcome.md](s5-accelerator-outcome.md)).
- **Video -> `HDD_FLIP` / `HDDR`.** The standard sources carry hard-disk-to-memory logic: `DCP.TDF`
  `HDD_FLIP` / `HDD_DATA` and `SP2_1K30.TDF` `HDDR[]` fed into the memory data path. That it reproduces the Sp97
  "read into the screen" path, and whether a 160 x 128 pixel-doubling mode exists in `VIDEO2.TDF`, is **not
  verified identical**. No Sp2000 program is known to use either.

### 6.5 What Sp2000 software does today

- The Sp2000 DOOM demo (`DOOM2.ASZ`, 2002) only writes `#C7` into the port table ("open the scale port") and uses
  the scale port. It loads no bitstream; the copy on the Peters Plus CD contains none (searched for the ACEX
  header). It runs on Standard in unreal-ng ([demo-status.md](demo-status.md), DOOM2: runs).
- Today's video player (kostya261 SVP, 2026) uses the ordinary 320 x 256 / 640 x 256 modes, the accelerator and
  the Covox-Blaster on Standard; no reload.
- The only Video software, FLC2 (1998), is Sp97 code.

### 6.6 Decision and what would reopen it

**Not planned: no Sp2000 software needs them.** Gap V12 is closed as not needed
([mame-gap-analysis.md](mame-gap-analysis.md)); MAME has neither.

Reopen when either turns up:

- a Sp2000 (ACEX 1K30, 59 215-byte) build of the DooM or the Video bitstream;
- a Sp2000 program that reloads the PLD with one of them. The emulator would show it without any change:
  it runs the program on Standard and logs `unknown PLD bitstream, full hash ..., head hash ..., using
  Standard` (§3), with the hashes to identify the stream.

Separately, and only if a Sp2000 program is found that streams the hard disk through it: whether the standard
`HDD_FLIP` / `HDDR` path needs emulating inside the Standard module.

## Glossary

| Term | Meaning |
|---|---|
| PLD | the Sprinter's programmable logic chip (Altera ACEX 1K30); holds most of the machine's logic |
| Bitstream | the configuration data streamed into the PLD; defines its logic |
| Configuration / module | one bitstream's logic; in the emulator, the hand-written model of it (`SprinterPldConfiguration`) |
| Head hash / full hash | hash of the first 4 096 / all 473 720 configuration writes |
| Cells `#C0-#FF` | the port-table cells in the PLD's embedded RAM; initial values come with the bitstream |
| DCLK, CONF_DONE | the ACEX configuration clock and its "configuration done" pin |
| Grid offset | the Game configuration's per-square scroll register (Mode3 of a square with Mode0 bit 2) |
