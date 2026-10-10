# GENS4B on TR-DOS through MCP (2026-10-10)

**Task.** From host files only (`gens4b.$c` from `MONSGENS.LZH`, a source text): a Pentagon with a disk that holds
GENS4B and the source, GENS4B started from the disk, the source loaded with `G`, assembled with `A`, run with `R`,
saved back with `P`, and the saved file read on the host. Then the same start through a BASIC `boot` loader, the way
a user's disk does it.

**Verdict.** Doable through MCP now, not fast. The editor part works; getting GENS4B started cost most of the run,
and the loader way failed silently until the line bytes were read. The golden path is about 30 calls (about 2-3 min);
the run took about 150 calls with the diagnosis. With the fixes below it is about 10 calls.

Build: `zx-next` c3d520ecb, Release (the MCP bridge's emulator, see F16). Machines: `PENTAGON`, `ram_power_on: zero`.

## Golden path

What works, in order, with the calls it takes now.

| # | Step | Calls | Notes |
|---|---|---|---|
| 1 | `emulator_manage create PENTAGON` | 1 | |
| 2 | `media insert` slot A, the hobeta `gens4b.$c` | 1 | a hobeta goes in as a one-file disk: `GENS4/B` CODE 30000, 12174 bytes |
| 3 | `asm_source encode` codec `gens` version 2, output `disk:A/TEST.C` | 1 | lines numbered 10, 20 ... |
| 4 | `invoke_api POST basic/mode {"mode":"48k"}` | 1 | must come before TR-DOS (F7) |
| 5 | `basic/run "RANDOMIZE USR 15616"` | 1 | `A>`, #7FFD = #30 (48K ROM under TR-DOS) |
| 6 | `basic/run 'LOAD "GENS4/B" CODE'` + a wait + a check (`O.K.`, bytes at 30000) | 3 | the reply comes before the load ends (F8) |
| 7 | `basic/run "RETURN"`, `basic/run "RANDOMIZE USR 30000"` | 2 | TR-DOS does not run `RANDOMIZE` at `A>` (F6) |
| 8 | `type_input type "G,,1:TEST"`, `tap enter`, wait | 3 | `\n` in the text is dropped (F2) |
| 9 | `type "L"`, `tap enter`, screenshot, move the file, read it | 3 + 2 Bash | OCR cannot read GENS (F11); path bug (F3) |
| 10 | `type "A"`, `tap enter`, wait, screenshot | 4 + 2 Bash | `Pass 1/2 errors: 00`, `Executes: 49152` |
| 11 | `inspect_state memory #C000` | 1 | `21 00 40 11 01 40 01 FF 17 36 AA ED B0 3E 02 D3 FE C9` |
| 12 | `type "R"`, `tap enter`, `inspect_state memory #4000` | 3 | `AA AA ...` |
| 13 | `type "P10,100,1:SAVED"`, `tap enter`, wait | 3 | |
| 14 | `asm_source files`, `asm_source decode disk:A/SAVED.C` | 2 | the same 10 lines back |

About 30 MCP calls and 4 Bash calls, all of them at fixed guessed delays.

### The loader way

The user's form: `10 RANDOMIZE USR 15619: REM : LOAD "GENS4/B" CODE`, `20 RANDOMIZE USR 30000`, saved as `boot`. The
typer stores the lines, `RANDOMIZE USR 15619: REM : SAVE "boot" LINE 10` answers "0 OK, 0:1" and writes nothing;
`... : REM : CAT` the same, before and after TR-DOS was entered once. Line 10 in memory:

```text
#5D3B: 00 0A 20 00 | 20 F9 C0 "15619" 0E 00 00 03 3D 00 3A 20 EA 3A 20 EF 22 "GENS4/B" 22 20 AF 0D
                     ^^ space                         :  ^^ REM :  ^^ LOAD
```

The `#20` after `REM :` is what TR-DOS takes for the command token (F1). The same line with the spaces taken out by
`memory_access write` (length kept, `0D` padding) and `GO TO 10` loaded GENS4B through #3D03, and line 20 (which
still has its leading `#20`) started it. Typed without spaces, the typer refuses at `REM:` (`no key gives #F8 at
position 10 in L mode`), so the loader cannot be written through `basic/run` at all now. This is the likely cause of
the earlier session's "RANDOMIZE USR 15619 never ran, RUN did not start the program".

## Dead ends, in the order hit

| Dead end | Cost | Finding |
|---|---|---|
| `media targets` with the hobeta path: "file not found" (percent-encoded path) | 1 | F3 |
| No tool to put a CODE file on a disk; found by trying `media insert` with the hobeta | 3 | F12 |
| `basic/mode {"mode":"trdos"}`: 400, only `48k` / `128k` | 1 | F7 |
| `basic/run RANDOMIZE USR 15616` at the 128 menu: enters TR-DOS over ROM 0 | — | F7 |
| `RANDOMIZE USR 30000` at `A>`: 422 three times, then typed by keys: back to `A>`; breakpoint at 30000 never hit; TTD + coverage: the ROM's RANDOMIZE handler never ran | ~20 | F6, F17 |
| `RETURN` + `RANDOMIZE USR 30000` in the 128K context: GENS starts; `G,,1:TEST\n` + `L\n` then the machine is at the 128 menu | ~10 | F2, F5, F7 |
| 48K context, `LOAD` then `RANDOMIZE USR 30000` at once: 422 and the machine at the 128 menu again; the same steps under TTD with a wait between: no reset | ~15 | F5, F8 |
| Screenshots under `scratch%2F...` in another worktree's `bin/` (the emulator's cwd); found with `lsof` | 4 Bash | F3, F16 |
| `G,,1:TEST\n`: the `\n` dropped, `L` appended to the line | 4 | F2 |
| `R` arrived as `BR` (`Pardon?`) | 5 | F10 |
| A breakpoint at 15619 stopped `basic/run`: 409, 34 typer breakpoints left in `bp_list` | 3 | F4 |
| `control_execution wait` kept answering the old breakpoint stop after `bp_clear` and `resume` | — | F9 |
| `coverage_summary` over frames 0..end: 97 empty buckets | 1 | F14 |

## Improvements, by the calls they save

| Change | Fixes | Golden path after it |
|---|---|---|
| Typer: store keywords without the spaces around them (as keys do); type `REM : <keyword>` the way the ROM takes it; or a `raw` mode that writes the tokenized line into E_LINE / the program and lets the ROM run it | F1 | the loader way works |
| `type` maps `\n` / `\r` to ENTER; unknown characters answer an error naming them | F2 | −3 |
| Paths: no percent-encoding; a relative path resolves against the caller's directory (or the reply gives the absolute path) | F3 | −4 Bash |
| `basic/mode trdos` (TR-DOS over the 48K ROM, as the Pentagon menu does), and a warning when TR-DOS is entered over ROM 0 | F7 | −1, no reset trap |
| `basic/run` waits for a TR-DOS command to end (prompt back, `O.K.` or an error) and reports its result | F8 | −2 |
| `basic/run` at `A>` with a non-TR-DOS statement: "TR-DOS does not run BASIC statements: RETURN first" (or does the `RETURN` itself with a flag) | F6 | −1, no dead end |
| `wait_until` on: screen text / digest stable for N frames / PC in a range / the program reading the keyboard with no key down ("waiting for input") / memory value; with a timeout and the reason in the answer | F9 | every guessed wait becomes exact |
| Typing with a handshake for any program: the next key goes when the program has read the previous one (keyboard port read with the half-row selected, key released, read again) | F10 | no lost / phantom keys |
| OCR with a learned font: find the 8x8 (or 4-6 px) glyph table the program prints with, or let the caller name it; or capture the characters a program prints (RST #10 and the common print routines) | F11 | −3 per look at the screen |
| `media` / disk tools: add a file (hobeta, raw with name/type/start) to a disk in a slot; a BASIC program from text (tokenized correctly, F1) | F12 | the loader without typing |
| `trdos run_code {file, start}` / `load_software` of a CODE file with `run: true` | F13 | steps 4-7 become 1 call |
| MCP `server`: pid, working directory, ports; a session's own emulator by default | F16 | no shared-process surprises |

With F1-F3, F7-F9 and F11 in place: create, insert, encode, `run_code`, then `type "G,,1:TEST\n"` + `wait_until
input`, `type "A\n"` + `wait_until input` + OCR, memory read, `type "R\n"`, `type "P...\n"` + wait, decode: about 10
calls, no file moves, no guessed delays.

## Open

- F5: not reproduced on a private instance (three runs of `LOAD` + an immediate `RANDOMIZE USR 30000` under the
  black box). The first reset is F7 (GENS4B over ROM 0); the second happened on the shared emulator process of another
  session, which was restarted shortly after.
- F10: the phantom `B` was seen once.

## Resolved (2026-10-10)

- F1: the encoder kept blanks before keywords and after `:`
  (`core/src/debugger/analyzers/basic-lang/basicencoder.cpp`, `tokenizeImmediate`); it drops them now. The
  "L mode" refusal at `REM:` was the typer reading FLAGS before the ROM had redrawn the line (after `USR`, FLAGS reads
  #0D at the idle point and #00 two frames later); the typer now waits up to 10 frames for the mode to allow the byte.
  Checked: the `boot` loader written with `basic/run` and saved through `USR 15619`, started GENS4B from the disk.
- F2, F3, F4, F6, F14: see the status column in [README.md](README.md#findings).

## Artifacts

`scratch/gens4b-mcp/`: `gens4b.$c`, screenshots `01-started.png` ... `08.png`. TTD recordings under
`~/.unreal-ng/ttd/2026-10-10-1145*-pentagon` and `2026-10-10-1149*-pentagon`.
