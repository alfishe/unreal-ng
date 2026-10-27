# Automation effectiveness: findings and gap analysis

Scenario runs of real user tasks driven by an agent through MCP (WebAPI only where MCP has no tool, through
`invoke_api`), measured for the number of calls, wall time, dead ends and what the agent had to work around. Each run
gets a report here; the table below collects the findings so that they can be costed and ordered.

## How a scenario is measured

| Metric | Meaning |
|---|---|
| Calls, golden path | MCP calls of the shortest run that works, knowing everything learned in the run |
| Calls, actual | MCP calls the run took, including the dead ends and the diagnosis |
| Calls, ideal | MCP calls with the proposed improvements in place |
| Dead ends | Steps that failed or misled, with the cost in calls |
| Out-of-band work | Bash calls the agent needed outside MCP (moving files, reading the source code) |

A finding is one of: **bug** (a tool does something wrong), **gap** (no tool for a step the scenario needs),
**friction** (works, costs extra calls or guesswork), **docs** (a recipe or a note is wrong or missing).

## Scenarios

| Date | Scenario | Report | Golden / actual / ideal calls |
|---|---|---|---|
| 2026-10-10 | GENS4B (TR-DOS): disk from host files, start, load a source, assemble, run, save, read back | [gens4b-trdos.md](gens4b-trdos.md) | ~30 / ~150 / ~10 |

## Findings

Severity: **S1** blocks the scenario or gives a wrong result silently, **S2** costs a dead end, **S3** costs calls.
Size: **XS** under an hour, **S** half a day, **M** 1-3 days, **L** more.

| Id | Kind | Sev | Size | Finding | Scenario | Status |
|---|---|---|---|---|---|---|
| F1 | bug | S1 | S | The BASIC typer (`basic/run`, `type_input tokenized`) stores literal spaces (#20) in the line: before the first keyword and after `:`. `RANDOMIZE USR 15619: REM : <cmd>` then does nothing (TR-DOS reads #20 where the command token should be), "0 OK, 0:1". Without the spaces the typer refuses (`no key gives #F8 at position 10 in L mode`) | GENS4B | fixed 2026-10-10: the tokenizer drops blanks next to keywords and `:`; the typer waits up to 10 frames for the editor mode to settle (the "L mode" refusal was FLAGS read before the ROM redrew the line) |
| F2 | bug | S1 | XS | `type_input type` drops `\n` silently (`CharToKeys` has no ENTER); the reply says "queued", length counts it | GENS4B | fixed 2026-10-10: `\n`, `\r`, `\r\n` press ENTER once; a character no key types refuses the whole text (400 with the codes) |
| F3 | bug | S2 | XS | `capture_media screenshot` and `media targets` percent-encode the path (`scratch%2F...png`); a screenshot lands in the emulator's working directory under that name. The emulator's directory had 15 such files from other sessions | GENS4B | fixed 2026-10-10: the MCP loopback client no longer encodes the path a second time; the screenshot reply gives the absolute path |
| F4 | bug | S2 | S | A user breakpoint that stops the machine while `basic/run` types leaves the typer's ROM control-point breakpoints in the list (34 extra, one with 7604 hits); the call answers 409 "paused while typing" | GENS4B | fixed 2026-10-10: list and clear (WebAPI, MCP, CLI, Lua, Python) deal with the user's breakpoints; `?all=true` shows the owned ones |
| F5 | bug | S2 | M | Two unexplained machine resets to the 128 menu while typing into TR-DOS / GENS (one in the 128K-editor context, one right after a `LOAD ... CODE` that had not finished). Not reproduced under TTD | GENS4B | not reproduced on a private instance (3 runs under the black box); the first reset is F7 (ROM 0 under TR-DOS), the second came on a shared emulator process another session was restarting |
| F6 | friction | S2 | S | `basic/run` at `A>` answers 422 "TR-DOS error before the command was recognised, ERR_NR 255" for `RANDOMIZE USR`: TR-DOS does not run BASIC statements; the error should say so and name the way (`RETURN`, then the statement) | GENS4B | fixed 2026-10-10: "not a TR-DOS command ... RETURN to BASIC first" |
| F7 | friction | S2 | S | `basic/run` called at the 128 menu enters 128 BASIC; `RANDOMIZE USR 15616` from there enters TR-DOS with ROM 0 under it (#7FFD bit 4 = 0). Programs started from that TR-DOS reset or hang. No warning; `basic/mode` has no `trdos` | GENS4B | open |
| F8 | friction | S2 | S | `basic/run` returns before a TR-DOS disk command has finished (`LOAD` answered at 50 frames); the next call races it (422) | GENS4B | open |
| F9 | gap | S2 | M | No "wait until" primitive: `control_execution wait` only reports debugger events and kept reporting a stale breakpoint stop. Every step of an editor-driven program needs a guessed delay and a screenshot | GENS4B | open |
| F10 | gap | S2 | M | No typing handshake for programs outside the ROM editor: keys are queued with fixed delays; one run got a phantom `B` before `R` (`BR` → `Pardon?`) | GENS4B | open |
| F11 | gap | S3 | M | `screen_ocr` reads only the ROM font: GENS (and most assemblers / editors) come back as `????`; the agent has to save a PNG, move it, and read the image | GENS4B | open |
| F12 | gap | S3 | S | No tool puts a code file / BASIC program onto a disk image (only `asm_source encode` for sources); hobeta insert works but gives a one-file disk | GENS4B | open |
| F13 | gap | S3 | S | No "run code file from disk" macro: TR-DOS init in the 48K context, `LOAD "x" CODE`, wait, `RETURN`, `RANDOMIZE USR start` is 5-7 calls and three traps (F6, F7, F8) | GENS4B | open |
| F14 | friction | S3 | XS | `time_travel coverage_summary` defaults to frame 0..end, not the session: 97 of 100 buckets empty | GENS4B | fixed 2026-10-10: coverage_summary / coverage_scan default to the session |
| F15 | friction | S3 | XS | A TR-DOS name with `/` (`GENS4/B`) gives an ambiguous path `disk:A/GENS4/B.C` | GENS4B | open |
| F16 | gap | S3 | S | MCP does not say which emulator process it drives: here a `zx-next` build running from another session's worktree (`scratch/wt-next-p2`), shared with that session's NEXT instance; `server` gives branch and commit, not the pid, working directory or ports | GENS4B | open |
| F17 | docs | S2 | XS | `.recipe/assemblers/gens.md` says GENS4B starts with `RANDOMIZE USR 30000` at the `A>` prompt; that never runs (F6). `docs/inprogress/2026-10-05-unreal-asm/TODO.md` says no way to start it was found. The working way is in [gens4b-trdos.md](gens4b-trdos.md#golden-path) | GENS4B | fixed on local master by another session (96c8b2cda) |
