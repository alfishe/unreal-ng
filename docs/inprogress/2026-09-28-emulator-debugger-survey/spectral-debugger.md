# Spectral debugger — capability survey

**Source:** https://github.com/r-lyeh/Spectral · local checkout commit `2401f59` (2025-07-02, `v1.13-WIP`) · C (single translation unit `src/app.c` including header-only modules), TIGR/OpenGL window with a custom immediate-mode UI (`src/sys_ui.h`)
**Surveyed:** 2026-09-28 (source reading)
**Scope note:** Spectral has **hardly any debugger**. There is one read-only text overlay (status line, registers, 22-line disassembly at PC), a `DEV`-build-only "DevTools" toggle with a few timing-tuning keys, and stdout tracing hooks that are compiled out or disabled. No breakpoints, no stepping, no memory view, no monitor, no remote protocol. The "unusual tooling" is in player features (run-ahead via state snapshots, AY waveform scope, ZXDB cheats/maps), not in debugging.

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| CPU & registers | Register dump in the debug overlay | `af af' bc bc' pc de de' hl hl' sp`, flag bits `SZYHXPNC` as 0/1, `iff` (IFF1<<8 \| IFF2), `im`, `ir`, `ix`, `iy` | `Spectral/src/zx_dis.h:43` |
| CPU & registers | Device state in the same dump | Active AY chip + selected AY register + all 16 AY registers; memory map string; port values `FE` (border), `1FFD` (`2A`), `7FFD` (`128`), FDC motor LED, ROM-patch flags | `Spectral/src/zx_dis.h:54`, `:61` |
| Memory views | Per-bank RAM fingerprint | One byte per RAM bank derived from FNV-1a hash of the 16 KiB bank; shows at a glance which bank changed | `Spectral/src/zx_dis.h:57-59` |
| Disassembly | 22-line disassembly from PC | `<banks> ADDR: mnemonic  hexbytes`, bank prefix = ROM/bank-at-4000/8000/C000 digits | `Spectral/src/zx_dis.h:18`, `:31`; overlay `Spectral/src/app.c:2997-3008` |
| Disassembly | Disassembler engine | floooh-style `z80dasm_op()` callback disassembler | `Spectral/src/emu_z80dasm.h:27` |
| Execution control | Pause | Only "pause when window loses focus" (and not while a tape plays); no user pause/step | `Spectral/src/app.c:1896-1916` |
| Execution control | Speed | Fast-forward toggle `MAX`, alternate Z80 rates (`CPU` command) | `Spectral/src/app.c:2404`, `:628` |
| Tracing & logging | Port logger (disabled) | Would print `PC OUT port,value` / `PC IN port (value)` for unknown ports; function `return`s immediately | `Spectral/src/zx.h:1018-1065` |
| Tracing & logging | Per-instruction hook (DEV builds) | `transact()` checks `cpu.step == 0` (opcode fetch): ROM print-routine trap at `$09F4` writes printed chars to a file (used by test mode); disassembly trace for `$38..$44` guarded by `if(0)` | `Spectral/src/zx.h:793-826` |
| Tracing & logging | Status line | renders/s, FPS, run-ahead flag, model, ROM bank + paging, `7FFD`/`1FFD`, PC, per-voice counters, tape state, tape/disk Hz, play time | `Spectral/src/app.c:2278-2296` |
| History / rewind | Quick-save slots | 10 user slots + 1 reserved slot in RAM (`struct quicksave`), full machine state | `Spectral/src/zx.h:1637-1726`, `:1733`, `:1833` |
| History / rewind | Run-ahead | Latency reduction by snapshot/replay of 1-2 frames using reserved slot 10 | `Spectral/src/app.c:2022-2049` |
| History / rewind | Rewind | Not implemented (TODO list only) | `Spectral/src/app.c:268`, `:300` |
| Video, raster & beam | ULA timing tuning keys (DEV + DevTools) | Keys `A`/`X`/`B` cycle `ADD`/`XX`/`BR` timing constants (frame tail T-states, INT offset, border width) live and print them | `Spectral/src/zx_ula.h:678-683`, used at `:701`, `:724`, `:749` |
| Video, raster & beam | Paper fetch kill (DEV + DevTools) | Holding `TAB` forces pixel/attribute fetch addresses to -1 (paper renders as border) | `Spectral/src/zx_ula.h:443` |
| Video, raster & beam | VRAM change highlight | Frame-diff of VRAM into a bitmask; code is `#if 0` | `Spectral/src/app.c:257`, `:2052` |
| Sound & device views | AY/beeper waveform scope | Up to 7 channels (beeper + 3 AY + 3 second AY) drawn as oscilloscopes over the screen | `Spectral/src/app.c:554`, `:3012-3022` |
| Sound & device views | Light-gun debug (DEV + DevTools) | Prints hit coordinates/attribute and paints the hit cell | `Spectral/src/zx.h:1218-1223`, `:1271-1276` |
| Profiling, coverage | None | Only design notes for an "instrumented profiler" via `OUT` ports | `Spectral/src/app.c:283-293` |
| Scripting & automation | Test mode (`TESTS` build) | Loads a file from `src/tests`, runs at max speed, logs ROM-printed text to `<file>.txt`, exits after inactivity | `Spectral/src/app.c:316-323`, `:1747-1755`, `:1975-2000` |
| Scripting & automation | Command bus | Every UI action is a four-char command (`'DEV'`, `'SAVE'`, `'POKE'`…) bindable to F-keys | `Spectral/src/app.c:2395-2520`, `Spectral/src/zx.h:150` |
| Import / export | POK files and inline pokes | `.pok` parser (bank-aware `lbbb aaaaa vvv ooo`, value 256 prompts user); `POKE` command | `Spectral/src/zx_sna.h:400-440` |
| Import / export | ZXDB cheats | Downloads the title's `.pok` from ZXDB and applies it | `Spectral/src/app.c:1118-1123` |
| Import / export | RZX | Record/playback via embedded librzx (marked WIP) | `Spectral/src/zx_rzx.h:81-97` |
| UI conveniences | Debug toggle | Bottom-right UI button, command `DEV`, menu entry "Debugger"; `DEVT` toggles DevTools (DEV builds only) | `Spectral/src/app.c:1428`, `:2489`, `:1406-1408`, `:2516` |

## 2. CPU, registers and disassembly

- The whole "debugger" is `gui()` drawing into a half-transparent `dbg` bitmap when `ZX_DEBUG` is set: status line, `regs(0)`, then `dis(PC, 22)` (`Spectral/src/app.c:2997-3008`, `:3034-3035`). It is a live overlay: the emulator keeps running, so the listing scrolls with PC every frame.
- `z80dasm()` returns `"<len><banks> %04X: %-13s  <hex>"`; the first character is the opcode length used by `dis()` to advance (`Spectral/src/zx_dis.h:18-41`). The bank string is four digits: ROM select bit, bank mapped at `$4000` (5 or 7 from the shadow bit), `2`, and `page128&7` (`:26`). Memory reads go through `READ8` (as seen by the CPU).
- `regs()` also dumps AY registers and a one-byte FNV-1a fingerprint of each RAM bank (`:57-59`), a cheap "which bank changed" indicator.
- Nothing is editable. The file header lists what is missing: hexdump of `(HL)`/`(BC)`, ROM symbols/sysvars, memwatch, step-out, call stack, breakpoints, port breakpoints, data-condition breaks, poke finder (`Spectral/src/zx_dis.h:1-6`). The app TODO adds "breakpoint on a source-code expression with wildcards, e.g. `HALT\n*\nJ*\n`" (`Spectral/src/app.c:36`) and "live coding disasm (like bonzomatic)" (`:269`).

## 3. Tracing and logging

- `transact()` (the pin-level bus callback) runs extra code only in `DEV` builds and only on the opcode-fetch step (`cpu.step == 0`): a ROM trap on `$09F4` (PRINT-OUT) that writes printable characters to `printer` (stdout, or the test log file) when not printing to channel K (`Spectral/src/zx.h:793-822`). A per-instruction `puts(dis(pc,1))` trace for PC in `$38..$44` exists but sits behind `if( 0 )` (`:823-824`).
- `logport()` filters known ports and would print unknown `IN`/`OUT` with PC, but starts with `return;` (`Spectral/src/zx.h:1018-1065`).
- `DEV` builds print unknown command IDs as hexdump + alert (`Spectral/src/app.c:2399-2402`).

## 4. History, snapshots, run-ahead

- `struct quicksave` holds the complete machine state (CPU, paging, ULA, AY, FDC, tape position, full RAM when `FULL_QUICKSAVES`) in `quicksaves[10+1]`: user slots 0-9, slot 10 reserved for run-ahead (`Spectral/src/zx.h:1637-1726`). The UI exposes slot 0 only (`SAVE`/`LOAD`, `Spectral/src/app.c:2424-2425`).
- Run-ahead: emulate a frame without audio/video, `quicksave(10)`, emulate 1 (or 2) more frames, render the last one with audio, then `quickload(10)` (`Spectral/src/app.c:2022-2049`). This is the only time-travel-like mechanism; there is no rewind buffer, and "auto-saves, then F11 to rewind" is a TODO (`:268`).

## 5. Video, raster and timing

- No beam marker, no event overlay, no contention view. The ULA frame is built by `run(...)` calls per region with timing constants declared through `ENUM(...)`; in `DEV` builds these become `static int` and, with DevTools on, keys `A`, `X`, `B` bump `ADD`, `XX`, `BR` and print the new values (`Spectral/src/zx_ula.h:678-683`). This is a developer calibration aid for INT placement and border width, not a user feature. Test programs used for calibration are named in the comment (`debugbreak*.z80`, `megashock*.z80`, `:686-689`).
- With DevTools on, holding `TAB` disables paper fetches (`Spectral/src/zx_ula.h:443`), showing only border timing.

## 6. Sound and device views

- Waveform overlay (`WAVE` command): draws the last 320 samples of the beeper and up to two AY chips' channels as scopes (`Spectral/src/app.c:3012-3022`, renderer `:554`).
- Status line shows per-voice activity counters and tape level/polarity (`Spectral/src/app.c:2291-2294`).

## 7. Notable and unique ideas

1. **Run-ahead built on in-memory full-state snapshots** (`Spectral/src/app.c:2022-2049`): the same save/restore path a rewind or TTD feature needs, used every frame; proves the snapshot is cheap enough for per-frame use.
2. **Per-bank hash fingerprint in the register dump** (`Spectral/src/zx_dis.h:57-59`): a one-byte digest per 16 KiB bank is a very cheap way to notice which bank a routine touched.
3. **Bank digits in every disassembly line** (`Spectral/src/zx_dis.h:26`): the current paging is printed with each instruction.
4. **Live ULA timing-constant tweaking by hotkeys** (`Spectral/src/zx_ula.h:678-683`): a quick calibration loop against timing test programs.
5. **ROM print trap as a test oracle** (`Spectral/src/zx.h:797-819`, `Spectral/src/app.c:316-323`): test programs' printed output is captured to a text file and the run ends on inactivity.
6. **Idea only: wildcard instruction-pattern breakpoints** (`Spectral/src/app.c:36`), e.g. break when `HALT` is followed by any instruction and then a jump.

## 8. Gaps and caveats

- No breakpoints of any kind, no single step, no user pause, no memory viewer or editor, no symbols, no call stack, no watches.
- The disassembly overlay is read-only and follows PC every frame; it cannot be scrolled.
- Port logging and the instruction trace are dead code (`return;` / `if(0)`), and the DevTools keys exist only in `DEV` builds (`NDEBUG < 2`, `Spectral/src/app.c:241-247`).
- The VRAM-change highlight and the bottom "RZX slider" are disabled with `#if 0` / `if( 0 )` (`Spectral/src/app.c:2052`, `:1600-1602`).
- Only quick-save slot 0 is reachable from the UI; RZX support is marked WIP (`Spectral/src/app.c:13`).
