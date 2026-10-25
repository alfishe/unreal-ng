# Article: Reverse-Engineering a Program into Rebuildable, Annotated Sources

A method for turning a program (a game, a demo, a resident tool) into sources that a person can read and that build
back into the original bytes: capture, coverage, classification, annotation, a findings register, a relocation proof,
labels back into the debugger. Every step below is a call the emulator answers; nothing is worked out by reading code
when a run can show it.

The method is adapted from [zx-reaper](https://github.com/ruguevara/zx-reaper) (its `docs/method.md`, the skills
`zx-coverage`, `zx-classify`, `zx-annotate`, `zx-relocate`, `zx-document`, and the lessons of its blind run on The
Hobbit). zx-reaper drives SkoolKit's simulator on 48K tapes and owns the skool files, the build and the ports. This
article covers what it does not: every machine unreal-ng runs (128K paging, Pentagon, Scorpion, TS-Conf ...), disk
software, the real machine's timing, RZX and TTD. The two are complementary: capture and measure here, hand the image
and the execution map to SkoolKit / zx-reaper, bring the labels back ([section 8](#8-symbols-round-trip)).

Companions: [program-state-reverse.md](program-state-reverse.md) (one variable: find, watch, disassemble),
[bug-hunt-ttd.md](bug-hunt-ttd.md), [code-coverage-and-analyzers.md](../analysis/code-coverage-and-analyzers.md),
[memory-search-map-and-regions.md](../analysis/memory-search-map-and-regions.md),
[ttd-write-journal.md](../analysis/ttd-write-journal.md), [ttd-reverse-debugging.md](../analysis/ttd-reverse-debugging.md),
[symbols-import-export.md](../analysis/symbols-import-export.md), [demo-boot-verification.md](demo-boot-verification.md)
(digests).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred: `analyze_performance` for coverage, `time_travel`
> for writers and readers, `capture_media` for digests and screenshots, `manage_symbols` for labels; snapshots and
> page dumps go through `invoke_api`. Use [WebAPI](#webapi) for the host-side scripts (coverage union, the shift test
> loop) or when MCP is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## 1. Principles

| Rule | What it means here |
|:--|:--|
| Byte-identical rebuild first, understanding later | The first disassembly must assemble back into the captured bytes. From then on every rename, comment and code/data split is a refactoring under test: the next build says `IDENTICAL` or names the line. |
| Each stage is checked against the previous one | Rebuilt image vs captured pages; moved build vs original on the same input (digests frame by frame); a claim vs a run. A stage without its check is not done. |
| Ask the emulator instead of reasoning | Who writes `#5C00`: `find_last`. When is this routine reached: a breakpoint or `coverage_scan`. What is on screen at frame 2000: a digest or a screenshot. What a table holds: render it. An answer is a few lines or a PNG. |
| Scripts for mechanics, the model for meaning | Capture, coverage union, gap lists, digest loops and builds are scripts. Tokens go to names, descriptions and format decisions. Never read a whole listing or map: query it. |

## 2. Capture the image

Capture at **handover**: the moment the loader passes control to the program, before it decompresses, relocates or
overwrites itself. Find it with an execution breakpoint on the entry address (the `RANDOMIZE USR` of the BASIC, the
CODE block's start, the disk file's start address), or for custom loaders with
[nonstandard-loader.md](../analysis/nonstandard-loader.md). Create the machine with `ram_power_on: "zero"`, so pages
5 and 7 hold no power-on noise and two captures compare.

| What | Call |
|:--|:--|
| The state at handover | `snapshot/save` (`.szx` / `.z80` for 128K; `GET /snapshot/formats` says what the machine can be saved as) |
| Every RAM page, mapped or not | `GET /memory/page/ram/N?format=binary` per page; `inspect_state memory_map` (`view: "ram"`) for which pages hold anything |
| What is paged in now | `inspect_state paging` / `memory_banks` |
| Where a known byte run lives | `debug_code find_bytes` with `space: "ram"` (page + offset) |
| Device memory (Sprinter VRAM, TS-Conf CRAM, GS RAM ...) | `memory_access regions`, then `read` / `save` |
| What changed between two moments | `time_travel memory_diff` over a recording (per page: `space: "ram5"`) |

The disassembly target is the set of pages the program uses, not the 64 KB CPU view: on 128K the same address
`#C000` is a different routine in each bank. Keep a page map (`page | range | what`) from the first day.

## 3. Coverage

Coverage tells code from data without reading it. Grow it with inputs, not with reading.

1. Record one input script per session: `coverage_start` (`clear: true`), drive the program, `coverage_read`
   (`max_ranges: 0`), save the ranges to a file. Union the files on the host; the gain of a script is what it adds.
2. Inputs: `type_input` (`type`, `tap`, `combo`), `joystick_input` (`tap` with `frames`), each followed by
   `control_execution run_frames` so the input plays inside a fixed number of frames. Typical scripts: every menu
   item, start-move-fire-die, lose every life and enter a high score, pause, redefine keys. Check a script does
   what you think with a screenshot at its key moments.
3. Random play: no built-in fuzzer; a host loop with a seeded RNG taps the program's real keys between
   `run_frames` chunks. A program with typed commands needs lines generated from its decoded vocabulary (every verb
   with the objects of the place, refused lines, very long lines), over many seeds; press every key of its key
   tables once at a line start and once in the middle.
4. Late levels and endings: an RZX from the RZX Archive or a person's playthrough, `rzx_playback play` with coverage
   on ([play-rzx.md](../media/play-rzx.md)).
5. **Stop rule**: two new scripts in a row add under 1 % to the program's window (`coverage_gaps` with the window's
   `start` / `end`: `executed_in_window`). Note what stays unexecuted and why.

Coverage is keyed by the **Z80 address**, not by the page. For banked code use the TTD coverage index with
`phys_page` (`coverage_scan`, `kind: "executed"`) over a recording, or the memory profiler's physical counters
([memory-counters.md](../analysis/memory-counters.md)).

**Coverage bounds claims.** An address missing from the maps was "not executed with these inputs", never "dead
code", unless the code itself makes it unreachable (an unconditional jump over it, a condition that cannot hold):
say which. **A trigger claim** ("this runs when the player types X", "this handles key Y") needs a run with the
input and one without, from the same snapshot: diff the two coverage sets; the routine is in one and not the
other, or the claim is not shown. `time_travel port_events` (`event: "key"`) tells the frame the program saw the key.

## 4. Classification: code, data, text, graphics, variables

Classification never changes bytes, so the build stays identical; it changes what the reader sees.

| Evidence, cheapest first | Call | Reads as |
|:--|:--|:--|
| Never executed | `coverage_gaps` over the window | gaps after `RET` / `JP` / `HALT` with odd instructions are data |
| Written at run time | `time_travel memory_diff` between two frames, then `coverage_scan` (`kind: "written"`) per range | written bytes are **variables** (or buffers, or self-modified code if they also execute) |
| Who writes it | `find_last` (`access: "write"`) with the journal on, or a write watchpoint + `control_execution wait` | the writer's PC names the variable's owner |
| Who reads it | `find_last` (`access: "read"`), `coverage_scan` (`kind: "read"`) | the reader tells the format: byte or word, stride, terminator |
| Who calls a routine | `debug_code trace`, the calltrace profiler ([calltrace-and-opcode-profiler.md](../analysis/calltrace-and-opcode-profiler.md)) | entry points, handler tables |
| Text | `screen_ocr` while it shows; bit-7 terminated or length-prefixed runs in a dump | `T` lines in a control file |
| Graphics | see below | sprites, tiles, fonts, maps |

**Graphics.** unreal-ng has screenshots (`capture_media screenshot`, `inspect_state screen_attributes`) and the raw
framebuffer (`capture_media framebuffer`), but **no generic sprite-sheet renderer** and **no font finder yet** (the
font-finder analyzer is a design in `docs/inprogress/2026-09-29-font-finder-analyzer/`, no code). Two ways:

- a host script renders a page dump (`memory/page/...?format=binary`) as 1-bit images at widths of 1, 2, 3, 4 bytes
  and as 8x8 cells; shapes are obvious at a glance, noise means not graphics or the wrong width;
- the program's own drawing routine draws it: `debug_code assemble` a stub at a free address (`LD HL,sprite` /
  `CALL DrawSprite` / `JR $`), `set_register pc` to it, `run_until_pc` the `JR`, screenshot. Level maps and title
  screens come out exactly as the program draws them.

## 5. Annotation rules

| Rule | Why (the Hobbit run's slips) |
|:--|:--|
| Name a field from the message its failed test prints ("too heavy" makes it the weight) | two fields named from how they are summed came out the wrong way round; no check reads a name |
| Every target of a handler table is an entry of its own, with a title | ~60 handlers left inside 24 generic entries nobody described |
| A trigger in a title needs both runs ([section 3](#3-coverage)) | a routine's trigger guessed from a table's layout was wrong |
| Labels: routines `VerbNoun` (`DrawSprite`), data and variables `NounPhrase` (`GuardX`), unique, at most 24 characters; loop heads and alternative entries as `DrawSprite_Loop` | the names become the port's names |
| Magic numbers become named constants (`LD (HL),CYAN*PAPER+BLACK`); addresses outside the program (ROM, system variables, screen) are `EQU`s; addresses inside it are labels only, never `EQU` | a label moves with the code, an `EQU` nails it down ([section 7](#7-relocation-proof-shift-test)) |
| Self-modifying code: a label on the modified instruction (`SmcGuardSpeed`), the store written as `LD (SmcGuardSpeed+1),A`, a comment on who modifies it | the shift test fails on an operand written by address |
| `Unknown` beats a wrong name | a wrong name costs more than a missing one |
| Register lines: inputs are what the routine's own instructions read before writing (flags too), outputs what some caller uses after the call (check every `CALL`) | a port builds its signatures from them |
| A `Hardware:` sentence when a routine touches screen, attributes, ports, ROM, paging, interrupts or timing loops | the list of what a port must replace |

Order: dependencies first. A routine is described once its callees are named, data once its readers are.

## 6. Findings register

One table (`ID | Status | Claim | Evidence | Refs`) for every claim other work depends on: a variable's meaning, a
format, a rule, a bug.

| Status | Means | Evidence |
|:--|:--|:--|
| `observed` | seen in a run | a watchpoint hit, `find_last`, a coverage diff, a digest, a screenshot, a byte check |
| `inferred` | read from the code, not run | the addresses read |
| `unknown` | open | what was tried |
| `refuted` | shown wrong | the run that refuted it; **kept, never deleted** |

- Reading code is `inferred`; only a run makes a claim `observed`.
- "Never", "unused", "always", "any order" need every case: a scan of every message for "this code is unused", a run
  of each way the mechanism allows for "no escape". A summary keeps the finding's conditions.
- **Closing pass**: when a finding answers an `unknown`, change every place that still states it (notes, questions,
  comments in the sources, the article): grep for the address and the term.
- Bugs at the edges need a deliberate pass: for each counter and timer, force the minimum, the maximum and the
  overflow (`memory/write` or `memory_access write`, then the same input script, then a screenshot).

## 7. Relocation proof (shift test)

Byte identity says nothing about whether the source can be changed. The shift test does: assemble the code at
`+0x100`, run the same input on the original and on the moved build, compare the screen frame by frame.

1. Build the moved image as a snapshot (sjasmplus `DEVICE` + `SAVESNA`, the paging as the original had it) and load
   it on a machine of the same model with `ram_power_on: "zero"`.
2. Align both runs on a frame start: `control_execution run_to_interrupt` once after the load (a `run_frames` budget
   runs from the current T-state, not to a frame end).
3. Replay the same input script; after each `run_frames` (count 1) read `capture_media screen_digest` (or
   `GET /state/screen/digest` `.combined`). Turbo off.
4. On the first different frame: screenshot both, then dump both machines' pages at the frame before and compare
   them allowing for the words that moved by the shift; the bytes left name the variable or the code.
5. Then an **odd shift** (`+0x101`): failures now are real alignment needs (`ALIGN 256` before a page-aligned
   table); record them. When memory is full to the top, use a small shift that fits (`+0x10`) and an odd one.

| Divergence cause | Fix |
|:--|:--|
| Absolute address hidden in data (`DEFB lo,hi`, a pointer at +14 of a record) | `DEFW Label`; pointer tables as words |
| Split-byte table lookups (`LD H,#90` + `LD L,A`) | `LD H,Table/256` (and an `ALIGN` the odd shift will demand) |
| Self-modifying code stores into an operand by address | the `Smc...` label of [section 5](#5-annotation-rules) |
| A random routine that reads memory (code included) | moved bytes give other numbers from the first random draw on: feed the original's values to the moved run, or exclude that range |
| Code copied to a fixed address (or into another 128K bank) at run time | keep it there with `DISP` / `ENT`, or leave that range out of the shift and record why |
| A negative address in an addition (`LD DE,#A000` to subtract `#6000`) | `LD DE,-Vocabulary` |
| `JR ... out of range` at assembly | data disassembled as code |

Done when `+0x100` and an odd shift pass, or the fixed-address parts are listed.

## 8. Symbols round trip

Labels from the sources go into the debugger, so every later run (breakpoints by name, `disasm`, `find_last`
answers) speaks the program's names:

- sjasmplus `--sym` file: `manage_symbols import` (format `sjasmplus-sym` by detection); per bank with `space: "ram3"`;
  a `.lst` listing with `load_listing` for stepping by source line ([symbols-listings-and-source-stepping.md](../analysis/symbols-listings-and-source-stepping.md));
- names found while debugging: export them for the sources with `manage_symbols export`.

**SkoolKit interop.** Two symbol codecs and a coverage export connect the debugger to a SkoolKit / zx-reaper
project (details: [symbols-import-export.md](../analysis/symbols-import-export.md#skoolkit-control-and-skool-files),
[code-coverage-and-analyzers.md](../analysis/code-coverage-and-analyzers.md#skoolkit-code-execution-map)):

| Id | Where | Direction | Use |
|:--|:--|:--|:--|
| `skoolkit-ctl` | `manage_symbols` | import / export | a control file: blocks become labels (`c` code, the rest data), titles become comments, `@ $X label=` and `equ=` are kept; out: a `.ctl` for `sna2skool.py -c` |
| `skoolkit-skool` | `manage_symbols` | import / export | in: `@label=`, `@equ=`, entry addresses and titles of a skool file; out: a `head.skool` with `@equ=` lines only |
| `skoolkit-map` | `analyze_performance coverage_export` | export | the executed instruction starts (CPU addresses, one `$HHHH` per line) as a code execution map for `sna2ctl.py -m` / `sna2skool.py -m`, from a coverage session on any machine; `merge` adds to an existing map |

```text
manage_symbols       {"action":"import","path":"scratch/game/re/game.ctl","format":"skoolkit-ctl"}
manage_symbols       {"action":"export","path":"scratch/game/re/labels.ctl","format":"skoolkit-ctl"}
analyze_performance  {"action":"coverage_export","path":"scratch/game/re/maps/unreal.map","start":"0x5B00","end":"0xFFFF","merge":true}
```

The loop: capture and cover here, `sna2ctl` / `sna2skool` from the exported map, annotate in skool, import the
labels back, debug with names. Coverage addresses are what the CPU saw: on a 128K program export one map per
paging state, or cover one bank layout at a time.

## 9. Agent orchestration

- **Contract first, then parallel slices.** The lead fixes what the parts share (the page map, names of shared
  variables and constants, formats); subagents then take one slice each.
- **One unit per file**: an entry per file (or a module per file); a subagent edits only the files it was given and
  never the shared head / constants file or the build directory. It verifies with the identity check, not a shared
  build.
- **Its own emulator instance per agent** (`target: "<id>"`, not `auto`), deleted at the end.
- **Tasks under an hour**; each report ends with what was unclear, and the lead folds it into the rules.
- **Resume a quiet agent**: a lead that only waits for notifications loses the night; check and resume.

## MCP (preferred)

```text
# 2 - capture at handover
emulator_manage   {"action":"create","model":"PENTAGON","ram_power_on":"zero"}
load_software     {"path":"scratch/game/game.trd","autostart":true}
control_execution {"action":"bp_add","type":"execution","address":"0x6000"}      # the entry address
control_execution {"action":"wait","timeout_ms":30000}
invoke_api        {"method":"POST","path":"/api/v1/emulator/{id}/snapshot/save","body":{"path":"scratch/game/orig.szx","force":true}}
inspect_state     {"aspects":["paging","memory_map"],"view":"ram"}
invoke_api        {"method":"GET","path":"/api/v1/emulator/{id}/memory/page/ram/3","query_params":{"format":"binary"}}

# 3 - one coverage script
analyze_performance {"action":"coverage_start","clear":true}
type_input          {"action":"tap","key":"1","frames":3}
control_execution   {"action":"run_frames","frames":500}
joystick_input      {"action":"tap","buttons":"fire","frames":5}
control_execution   {"action":"run_frames","frames":1500}
analyze_performance {"action":"coverage_read","max_ranges":0,"response_format":"json"}
analyze_performance {"action":"coverage_gaps","start":"0x6000","end":"0xBFFF","max_gaps":1024}
rzx_playback        {"action":"play","path":"scratch/game/late-levels.rzx"}

# 4 - variables, writers, readers (record with the journal first)
time_travel {"action":"start","journal":true}
#   ...the same script...
time_travel {"action":"stop"}
time_travel {"action":"memory_diff","space":"ram","from_frame":100,"to_frame":2000}
time_travel {"action":"find_last","addr":"0x7F12","access":"write"}              # pc, frame, value
time_travel {"action":"find_last","addr":"0x7F12","access":"read"}
time_travel {"action":"coverage_scan","kind":"executed","addr_from":"0xC000","addr_to":"0xC3FF","phys_page":4}

# 4 - the program draws its own graphics
debug_code        {"action":"assemble","address":"0x5B00","code":"LD HL,0x9000\nCALL 0x8D3C\nJR $"}
control_execution {"action":"set_register","name":"pc","value":"0x5B00"}
control_execution {"action":"run_until_pc","pc":"0x5B06"}
capture_media     {"action":"screenshot","area":"full","filename":"scratch/game/img/sprite-9000.png"}

# 7 - shift test, one frame
control_execution {"action":"run_to_interrupt"}
control_execution {"action":"run_frames","frames":1}
capture_media     {"action":"screen_digest"}

# 8 - labels into the debugger
manage_symbols {"action":"import","path":"scratch/game/build/game.sym"}
manage_symbols {"action":"load_listing","path":"scratch/game/build/game.lst"}
```

## WebAPI

```bash
# capture: snapshot and raw pages
curl -s -X POST "$BASE/emulator/$EMU_ID/snapshot/save" -H 'Content-Type: application/json' \
     -d '{"path":"scratch/game/orig.szx","force":true}'
for p in 0 1 3 4 6; do
  curl -s "$BASE/emulator/$EMU_ID/memory/page/ram/$p?format=binary&offset=0&length=16384" -o scratch/game/page$p.bin
done

# coverage per script, union on the host
curl -s -X POST "$BASE/emulator/$EMU_ID/coverage/start" -H 'Content-Type: application/json' -d '{"clear":true}'
curl -s -X POST "$BASE/emulator/$EMU_ID/keyboard/tap" -H 'Content-Type: application/json' -d '{"key":"1","frames":3}'
curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames"   -H 'Content-Type: application/json' -d '{"count":500}'
curl -s -X POST "$BASE/emulator/$EMU_ID/joystick/tap" -H 'Content-Type: application/json' -d '{"buttons":"fire","frames":5}'
curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames"   -H 'Content-Type: application/json' -d '{"count":1500}'
curl -s "$BASE/emulator/$EMU_ID/coverage?max_ranges=0" | jq -c '.ranges' > scratch/game/maps/menu.json
curl -s "$BASE/emulator/$EMU_ID/coverage/gaps?start=0x6000&end=0xBFFF&max_gaps=1024" \
     | jq '{executed_in_window, coverage_percent, gap_count}'

# writers and readers over a recording
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/memory-diff" -H 'Content-Type: application/json' \
     -d '{"space":"ram","from_frame":100,"to_frame":2000,"limit":50}' | jq '.ranges'
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/find-last" -H 'Content-Type: application/json' \
     -d '{"addr":32530,"access":"read"}' | jq -c '{found, frame, pc, value, phys_page}'

# shift test: per-frame digests of one run (original or moved build), compared on the host
curl -s -X POST "$BASE/emulator/$EMU_ID/run_to_interrupt" >/dev/null
for f in $(seq 1 3000); do
  curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" -H 'Content-Type: application/json' -d '{"count":1}' >/dev/null
  echo "$f $(curl -s "$BASE/emulator/$EMU_ID/state/screen/digest" | jq -r .combined)"
done > scratch/game/shift/moved-0100.digests
# input of the script is queued before the run_frames call of its frame; diff against orig.digests: the first line that differs

# labels
curl -s -X POST "$BASE/emulator/$EMU_ID/symbols/import" -H 'Content-Type: application/json' \
     -d '{"path":"/abs/scratch/game/build/game.sym"}' | jq -c '{format, set, records}'
```

## Pitfalls

- **RAM pages 5 and 7 are random at power-on** on 128K models: create with `ram_power_on: "zero"`, or two captures
  and two runs of the shift test differ before the program runs a single instruction.
- **Turbo renders only some frames**: digests taken in turbo differ between runs. Compare pictures and sound with
  turbo off; in turbo compare only RAM.
- **`run_frames` is not frame-aligned**: it runs N frame budgets from the current T-state. Align with
  `run_to_interrupt` before a frame-by-frame comparison, and start both runs the same way.
- **TTD holds the machine at 1x** with turbo and fast loaders off while recording, and **loading a tape or disk wipes a
  stopped session's history** (a load is refused while recording). Load the program, capture, then start the
  recording; long coverage scripts run faster without TTD.
- **Coverage is per Z80 address, not per bank**: two banks at `#C000` merge into one map. Split by page with the TTD
  index (`phys_page`) or the memory profiler.
- **Coverage records only while the machine runs**: `coverage_start` on a paused machine that is then stepped by
  hand, or zero ranges with `active: false`, means "not recording", not "nothing ran".
- **The Kempston joystick is not decoded everywhere**: 48K, 128K, +2, +2A, +3, ATM450 and ATM710 accept
  `joystick_input` and warn; on a Pentagon `#1F` is the joystick only outside TR-DOS
  ([joystick.md](../input/joystick.md)). Use the program's keys there.
- **One plane per phase, or an explicit session**: a WebAPI call that changes the machine opens an implicit control
  session (a 15 s lease), and MCP run calls in that window answer 409 `held_by_other` ("Run control is held by
  webapi (...)"), and the other way round: one surface holds run control at a time, by design. The implicit
  session's id comes back in that reply (header `X-Unreal-Session`, JSON `automation_session.id`; `GET
  .../control/session` does not show it): send it on the host script's next calls, or end it with `DELETE
  .../control/session` and that header. Better: take control explicitly once (`take_control`) and send its id from
  the host scripts too, or drive a phase from one plane.
- **Pause, load, poke, run**: `load_software` on a running machine starts the snapshot at once. To poke before its
  first frame (lives, a cavern number, a cheat flag), `control_execution pause`, load, `memory_access write`
  (`address` + `hex`), then `resume` / `run_frames`.
- **Hold keys for the program's loop, not for a frame**: a game whose main loop takes several frames misses a 2-4
  frame press. Measure the loop (frames between two passes of its input routine) and hold a key at least that long;
  Manic Miner wanted 10 frames for movement and ~40 for ENTER at the title.
- **A misspelt argument is an error, not a default**: MCP tools refuse argument names their schema does not list
  (`unknown argument 'addr'`); before that fix a read with `addr` read address 0.
- **Screenshots from host scripts**: `GET $BASE/emulator/$EMU_ID/capture/screen?area=screen&path=/abs/x.png`.
- **The emulator's frames are not zxrun's frames**: a loop pass measured 3.2-3.6 frames in SkoolKit's simulator and
  ~4.4 in unreal-ng (contention, interrupt timing). Input scripts written for one need their frame numbers checked
  on the other; compare at loop passes, not frame numbers.
- **`run_frames` and `run_until_pc` skip breakpoints**: to stop at a watchpoint, `resume` and `wait`.
- **`disasm` applies every loaded symbol set**: ROM names show on RAM addresses when a set does not belong to the
  program; disable foreign sets (`manage_symbols set_enable`).
- **Instances you create stay** until destroyed: delete them at the end of every script.
