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
| DooM, Video configurations | none; they exist only as Sprinter 97 bitstreams and are not needed on the Sp2000 (§5) |

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
- ~~DooM and Video configurations~~ - **closed 2026-10-03, not planned.** They exist only as **Sprinter 97**
  bitstreams (FLEX EPF10K10, 14 751 bytes each) in `sp97-bios-master.zip` from zxgit.org/Sprinter/97; no Sp2000
  (ACEX 1K30) build exists anywhere, and the Sp2000 loader cannot load a FLEX stream. The Sp2000 manual says those
  functions were merged into the standard firmware: DooM's line stretching is the standard accelerator's `#C7`
  scale register (`ACCELER.TDF`; MAME and unreal-ng model it), and Video's disk-to-memory logic matches the
  standard `HDD_FLIP` / `HDDR` (not verified identical). The Sp2000 DOOM demo loads no bitstream and runs on
  Standard; the 2026 video player uses Standard too. Verdict: deep legacy, no DooM / Video modules. Head hashes
  (first 4 096 writes), recorded in case a Sp2000 build ever turns up: DooM `sp97-sprint08-dm.bin` `#8C0B2C6D`;
  Video `sp97-sprint04-vid.bin` `#3664CFCD`, `sp97-sprint08-vid.bin` `#C670F611`, `sp97-sprint11-vid.bin`
  `#4F7C1F52`. A full hash does not apply. The files and the research note are in the materials,
  `firmware/pld/special/` (README there).
- LDConf `STREAM.300/303/305`: unknown bitstreams, run as Standard.
- Hardware timing of the load end (CONF_DONE, extra DCLKs, the CPU reset), §1.

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
