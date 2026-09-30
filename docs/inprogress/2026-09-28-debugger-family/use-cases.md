# Debugger use cases: who needs what, and how well we serve them today

- **Date:** 2026-09-28
- **Status:** draft for review. Extended roles: [use-cases-extended.md](use-cases-extended.md). First of four documents:
  1. **use-cases.md** (this one): roles, operations, flows, today vs target.
  2. [comparative-analysis.md](comparative-analysis.md): what the best debuggers
     do in each widget category and flow, and why.
  3. [proposition.md](proposition.md): the unreal-ng debugger family, the
     options, MVP and north star.
  4. [roadmap.md](roadmap.md): priorities, phases, and what each phase delivers.
- **Builds on:** the debugger model
  ([2026-09-28-debugger-model](../2026-09-28-debugger-model/README.md): one
  model, one protocol, many skins; personas and jobs J1-J6 in
  [gui-main-debugger.md](../2026-09-28-debugger-model/gui-main-debugger.md) §2),
  and the survey of 17 debuggers
  ([2026-09-28-emulator-debugger-survey](../2026-09-28-emulator-debugger-survey/)).
- **Evidence for "today":** code on master at `35e9d050`. Each "today" cell names
  what exists; status words mean **works**, **partial**, **broken**
  (exists but gives wrong results), **designed** (document only), **none**.

> **In one line.** Nine kinds of people use a Spectrum debugger for very
> different jobs. Today unreal-ng serves automation and time travel very well,
> and the interactive, visual and "understand this program" jobs poorly.

## Contents

- [1. How to read this document](#1-how-to-read-this-document)
- [2. The roles](#2-the-roles)
- [3. Use cases by role](#3-use-cases-by-role)
  - [3.1 Game developer (GD)](#31-game-developer-gd)
  - [3.2 Demo coder (DM)](#32-demo-coder-dm)
  - [3.3 Reverse engineer, game hacker, cracker (RE)](#33-reverse-engineer-game-hacker-cracker-re)
  - [3.4 System programmer: NedoOS, TR-DOS, CP/M, firmware (OS)](#34-system-programmer-nedoos-tr-dos-cpm-firmware-os)
  - [3.5 Application programmer (AP)](#35-application-programmer-ap)
  - [3.6 Music and sound-card programmer (MU)](#36-music-and-sound-card-programmer-mu)
  - [3.7 Hardware, FPGA and emulator developer (HW)](#37-hardware-fpga-and-emulator-developer-hw)
  - [3.8 Learner, educator, spectator (ED)](#38-learner-educator-spectator-ed)
  - [3.9 Tester, CI, AI agent (QA / AI)](#39-tester-ci-ai-agent-qa--ai)
  - [3.10 Beyond debugging: atypical roles](#310-beyond-debugging-atypical-roles)
- [4. Cross-cutting capability themes](#4-cross-cutting-capability-themes)
- [5. The code-change loop in detail](#5-the-code-change-loop-in-detail)
- [6. Symbols and source: what is specified, what works](#6-symbols-and-source-what-is-specified-what-works)
- [7. Why today's coverage is inefficient](#7-why-todays-coverage-is-inefficient)
- [8. Summary matrix](#8-summary-matrix)
- [9. Defects found while writing this](#9-defects-found-while-writing-this)
- [10. Glossary](#10-glossary)

---

## 1. How to read this document

Each use case (UC) has:

- an **ID** (`GD-3`: role code and number), used later by requirements,
  designs and the roadmap;
- a **flow**: what the person actually does, step by step;
- **today**: how unreal-ng covers it now;
- **why it hurts**: what makes today's path slow, fragile or impossible;
- **target**: how it should work;
- a **class**:
  - **Need**: people do this weekly; missing it sends them to another emulator.
  - **Nice**: clear value, not a blocker.
  - **Future**: futuristic; a differentiator nobody has yet.

"Best in class" references point to the survey documents, so the claim can be
checked.

## 2. The roles

| Code | Role | What they build or do | Their measure of success |
|---|---|---|---|
| **GD** | Game developer | games in assembler or C (sjasmplus, z88dk, SDCC); sprites, levels, sound calls, loaders | "I changed a line and saw the effect in seconds" |
| **DM** | Demo coder | effects tied to the beam: multicolor, border, gigascreen, raster splits; tight T-state budgets | "the effect is stable to the T-state on every target machine" |
| **RE** | Reverse engineer, game hacker, cracker | understands other people's binaries: pokes, trainers, ports, remakes, preservation, protection analysis | "I found the routine and the data layout, and I can explain them" |
| **OS** | System programmer | NedoOS, TR-DOS/ROM patches, CP/M BIOS, IDE/SD drivers, ESX-style OSes, firmware for cards (GS/NeoGS) | "the scheduler, drivers and memory map behave, across processes and pages" |
| **AP** | Application programmer | utilities, editors, trackers, file managers under TR-DOS, NedoOS, CP/M | "my program runs correctly on the OS, with its files and APIs" |
| **MU** | Music and sound-card programmer | AY/TS/TSFM players, MOD players on GS/NeoGS, Covox/SounDrive, beeper engines | "the right register values at the right moment, on both CPUs" |
| **HW** | Hardware, FPGA and emulator developer | new clones and devices (ZX-Evo, TSConf, Next), emulator accuracy work | "the emulated bus, timing and devices match the real board" |
| **ED** | Learner, educator, spectator | learns Z80 and the machine; teaches it; or simply enjoys watching a machine work | "I can see and understand what happens inside" |
| **QA / AI** | Tester, CI pipeline, AI agent | automated runs, regression checks, scripted analysis | "one call returns a precise, structured answer" |

The debugger-model personas map onto these: *game hacker* is RE, *demo coder*
is DM, *reverse engineer* is RE, *emulator developer* is HW, *tool builder /
AI agent* is QA / AI. New here: GD, OS, AP, MU and ED as first-class roles.

---

## 3. Use cases by role

### 3.1 Game developer (GD)

| ID | Use case and flow | Today | Why it hurts | Target | Class |
|---|---|---|---|---|---|
| GD-1 | **Build and run with symbols.** Assemble with sjasmplus; the emulator loads the binary and the symbols; the disassembly shows my labels. | **broken** for sjasmplus `.sym` (lines `label: EQU 0x…` are silently skipped, the load reports success); **partial** via `.lst`; **works** via DeZog (it parses SLD itself) | the most common assembler's default symbol file fails quietly; people conclude "labels don't work" | every mainstream format loads, with a report of what was read; SLD is the primary format | **Need** |
| GD-2 | **Edit, rebuild, see it running in seconds.** Save the source; the build runs; the new code replaces the old in the running game, without a reboot or reload from tape. | primitives only (`memory load bank`, snapshot load, GDB `monitor load`); snapshot load wipes the TTD history | every change costs a reload plus manual navigation back to the game state | watch the build output; reload code, data, symbols and listing atomically into the current state; keep the history ([§5](#5-the-code-change-loop-in-detail)) | **Need** |
| GD-3 | **Step through my source, not the disassembly.** Break on a source line, step by line, see locals of a C function. | **partial**: `.lst` step_line / run_to_line on WebAPI, CLI, MCP, Lua, Python; one file, no pages; no Qt source view; C locals none | the GUI user never sees source; multi-file projects collide | SLD with pages and modules; a source view in every skin; C debug info (SDCC `.cdb`, z88dk) later | **Need** |
| GD-4 | **Find why a sprite is corrupted.** Break on a write to the sprite buffer, look back at who wrote it and when. | **works**: write breakpoints, TTD find-last-write, reverse step | strong; only the GUI lacks "who wrote this byte" on a right-click | one click from a pixel or a byte to its writer and the frame (debugger-model J4) | **Need** |
| GD-5 | **Stay inside the frame budget.** See how many T-states a routine takes and where the frame time goes; get warned when a routine exceeds its budget. | opcode profiler and call trace exist (automation); no per-routine budget view | numbers exist but not as "routine X took 12 400 T of the 69 888 available" | per-frame timeline of routines (a flame chart per frame), `@budget` annotations (devtools §13.5) | **Nice** |
| GD-6 | **Test on every target machine.** Run the same build on 48K, 128K, +3, Pentagon, Scorpion and compare. | several instances possible; comparison is manual | contention and timing bugs show only on some models | a matrix run with screen/state digests per model, differences highlighted | **Nice** |
| GD-7 | **Unit-test a routine.** Call a Z80 routine with given registers and memory, check the result, in CI. | MCP / Python can do it by hand | no framework, no assertions from source | a test runner: set state, call, assert (DeZog unit tests; devtools §13.4) | **Nice** |
| GD-8 | **Asset hot-swap.** Replace a sprite sheet or a level (`INCBIN`) in the running game. | none | art iteration needs a rebuild and reload | reload one binary blob to its label's address | **Future** |

### 3.2 Demo coder (DM)

| ID | Use case and flow | Today | Why it hurts | Target | Class |
|---|---|---|---|---|---|
| DM-1 | **See where the beam is when my code runs.** Step the effect and watch the beam position, the pixel being drawn and the remaining T-states of the line. | **partial**: Qt ULA beam widget and border timing widget; `run to pixel / scanline`; `Screen::DescribeBeam()` on automation | no "screen drawn up to the beam" view while stepping; no marker of the current pixel on the picture | TS-Labs style: each step re-renders the frame up to the exact T-state and marks the beam ([tslabs-unreal](../2026-09-28-emulator-debugger-survey/tslabs-unreal-debugger.md)); jnext style "run to end of scanline" ([jnext](../2026-09-28-emulator-debugger-survey/jnext-debugger.md)) | **Need** |
| DM-2 | **See every port write placed on the frame.** Border, AY, paging, `#FE` writes as dots on a frame map by (line, T-state). | port trace (automation list); border timing widget | a list of 3 000 writes per frame is unreadable; the eye needs the picture | an event viewer: events plotted on the frame grid, previous frame ghosted, click an event to jump to the code ([mesen2](../2026-09-28-emulator-debugger-survey/mesen2-debugger.md), [8bitanalysers](../2026-09-28-emulator-debugger-survey/8bitanalysers-debugger.md)) | **Need** |
| DM-3 | **See contention.** Which of my memory accesses were delayed, by how much, on which line. | none as a view | multicolor and border effects break on contention one T-state at a time | a per-line contention strip; delay per instruction in the trace | **Need** |
| DM-4 | **Make it stable on every clone.** Compare the same effect on 48K, 128K, Pentagon timing. | manual | the typical demo bug | raster diff between models: the first line where the pictures differ | **Nice** |
| DM-5 | **Gigascreen and flicker effects.** See both frames, the blend, the flicker per pixel. | ZX DLSS program (#56) designed; blend mode exists in the GUI | design only | two-frame view with a per-pixel flicker map | **Nice** |
| DM-6 | **Size-coding.** Count bytes and T-states of a routine while editing it. | assembler returns bytes | no live feedback | inline T-state and byte counters in the source or disassembly | **Future** |

The effect side in depth (music sync, live constant tweaking, table curves, flame charts, race-the-beam plots, double-buffer timing, animation review) is role X-18 in [use-cases-extended.md](use-cases-extended.md#318-animator-and-demo-effect-maker-x-18).

### 3.3 Reverse engineer, game hacker, cracker (RE)

| ID | Use case and flow | Today | Why it hurts | Target | Class |
|---|---|---|---|---|---|
| RE-1 | **Find the lives counter and make a trainer.** Search memory for a value, play, search for the changed value, set a write breakpoint, find the `DEC`, patch it. | memory find exists (automation); write breakpoints work; **the Qt GUI cannot edit memory or code** | the classic workflow ends in "now patch it" and the GUI cannot | RAM search with previous-value modes and undo ([bizhawk](../2026-09-28-emulator-debugger-survey/bizhawk-debugger.md)); patch from every view; export as `.pok` (debugger-model J1) | **Need** |
| RE-2 | **Who wrote this byte, who read it, from where.** On any byte: the list of PCs that read or wrote it, and the last writer. | TTD find-last; memory access counters (counts, not PCs) | counts without PCs do not answer "who" | per-byte provenance: up to N reader and writer PCs, the last writer ([8bitanalysers](../2026-09-28-emulator-debugger-survey/8bitanalysers-debugger.md), [zx-m8xxx](../2026-09-28-emulator-debugger-survey/zx-m8xxx-debugger.md)) | **Need** |
| RE-3 | **Separate code from data automatically.** Run the program; everything executed is code, everything read is data; the disassembly follows. | coverage and access counters exist; disassembly does not use them | the disassembly of a data table is garbage, and the RE marks it by hand | code/data logging drives the disassembly and the export ([mesen2](../2026-09-28-emulator-debugger-survey/mesen2-debugger.md) CDL, xpeccy "runtime mapping") | **Need** |
| RE-4 | **Find the sprites without guessing the width.** Show me the graphics in memory; I should not tune width, height and interleave by hand. | none | every sprite ripper today is a manual bitmap viewer with sliders | automatic layout from the access pattern: the routine that draws to the screen reads the source bytes in its stride, so the stride, width and height come from the recorded reads ([§4.5](#45-memory-intelligence)) | **Need** (basic), **Future** (full) |
| RE-5 | **Recognize the engine.** "This is a Ultimate Filmation engine", "this is the PT3 player", "this is a Hisoft BASIC runtime". | ROM signature catalog for ROMs only | every RE re-discovers the same well-known code | a signature base of routines, players, loaders, compressors, engines; matches shown as labels with a confidence ([§4.5](#45-memory-intelligence)) | **Future** |
| RE-6 | **Understand a memory layout.** Which areas are screen buffers, tables, stacks, code, free. | memory page visualizer (Qt); access counters | no classification | a memory map colored by role, inferred from accesses, with a legend and a timeline | **Nice** |
| RE-7 | **Compare two runs.** Where do a working and a failing run diverge? | TTD per session; no comparison | the first divergence is the answer to most "why does it break" questions | diff run: the first instruction where two runs differ ([zx-m8xxx](../2026-09-28-emulator-debugger-survey/zx-m8xxx-debugger.md)); what-if branches of the timeline ([§5](#5-the-code-change-loop-in-detail)) | **Nice** |
| RE-8 | **Export a documented disassembly.** Labels, comments, code/data marks to a re-assemblable source or SkoolKit. | disassembly export (document disassembly) exists | loses the RE's marks | export to sjasmplus, SkoolKit `.skool`/`.ctl`, Ghidra | **Nice** |
| RE-9 | **Analyze a protection or loader.** Custom tape loaders, disk protections, weak sectors. | strong: tape and disk analyzers, UDI weak bits, recipes | good for experts via automation; no GUI flow | the same data in a GUI device board with a timeline | **Nice** |
| RE-10 | **Keep notes that survive paging.** Comments and labels on code that lives in page 3 today and page 6 tomorrow. | labels store the bank, lookup ignores it | notes show on the wrong code after a page switch | page-aware labels and comments; MAME-style binding by address plus the CRC of the instruction bytes ([mame](../2026-09-28-emulator-debugger-survey/mame-debugger.md)) | **Need** |

### 3.4 System programmer: NedoOS, TR-DOS, CP/M, firmware (OS)

| ID | Use case and flow | Today | Why it hurts | Target | Class |
|---|---|---|---|---|---|
| OS-1 | **Debug a process, not an address.** Break in process 3's code, whichever pages it occupies now. | none | NedoOS moves processes between pages; address breakpoints hit the wrong process | OS awareness: process list, pages per process, breakpoints per process (devtools §14) | **Need** for NedoOS work |
| OS-2 | **Load a relocatable module and keep its symbols.** The OS loads my app at a different address each time; the symbols follow. | designed only (devtools §8.3, Phase 6) | symbols are useless for relocated code | module load events; `relocation_delta` applied to the module's symbols | **Need** for NedoOS work |
| OS-3 | **Watch the system calls.** See the BDOS / TR-DOS / NedoOS calls stream with arguments and results. | TR-DOS analyzer works; NedoOS none | the call stream is the main debugging view for an OS developer | an OS call log per OS: calls, arguments, results, time | **Nice** |
| OS-4 | **Drivers against real storage.** Debug an IDE/SD driver against a disk image or a host folder, see every command. | SD (Z-Controller), WD1793, uPD765 device reports; media manager #58 designed | the command stream is invisible in the GUI | a device board per controller with a command log | **Nice** |
| OS-5 | **Crash forensics.** After a reset or a hang: what ran last, which process, which call. | TTD reverse; call trace | needs expert use of automation | a crash dossier: last N calls, the process, the page map, the last writes to the crashed code (devtools §13.6) | **Nice** |
| OS-6 | **Structs in memory.** Show OS structures (process table, file descriptors) decoded, not as hex. | NedoOS struct DSL designed | hex only | a struct overlay from a description language | **Future** |

### 3.5 Application programmer (AP)

| ID | Use case and flow | Today | Why it hurts | Target | Class |
|---|---|---|---|---|---|
| AP-1 | **Edit and run on the OS.** Build a NedoOS or TR-DOS program, put it on the virtual disk or host folder, run it from the OS shell. | media manager #58 designed (host folders as FAT); TR-DOS disk images work | copying into disk images by hand for every build | the build output folder is a drive in the emulated OS | **Need** |
| AP-2 | **Break when my program starts.** Stop at the entry point of my program, whatever address the OS loaded it to. | none for relocated programs | as OS-2 | "break on program start" per OS | **Nice** |
| AP-3 | **Input automation.** Type commands, press keys, feed files for a test. | **works**: command typer, keyboard/mouse injection, recipes | automation only | the same from the GUI: a macro bar | **Nice** |

### 3.6 Music and sound-card programmer (MU)

| ID | Use case and flow | Today | Why it hurts | Target | Class |
|---|---|---|---|---|---|
| MU-1 | **See the sound chips work.** AY/TS/TSFM registers, envelopes, levels, per-channel scopes, live while the music plays. | DeviceState reports on automation; sound oscilloscope designed (#48) | no live view in the GUI | a live sound board per chip, with per-channel scopes and mute | **Need** (and a spectator wow) |
| MU-2 | **Debug both CPUs of a sound card.** The Spectrum sends a command to the GS; the GS firmware reacts; see both sides at the same moment. | GS debugger designed (#45, debugger-model card debugger) | nobody offers this today | one clock, both CPUs, the mailbox between them, stepping one while the other keeps relative time | **Nice** (rare today, a big differentiator) |
| MU-3 | **Player timing.** When exactly are AY registers written in the frame; does the player jitter? | AY log analyzer | numbers only | AY writes on the frame event viewer (DM-2) | **Nice** |
| MU-4 | **Find the music data format.** Identify the tracker format and player in an unknown program. | none | manual | signature base of players (RE-5) | **Future** |

### 3.7 Hardware, FPGA and emulator developer (HW)

| ID | Use case and flow | Today | Why it hurts | Target | Class |
|---|---|---|---|---|---|
| HW-1 | **See every device's state.** Registers of every chip on every machine: ULA, TSConf, ATM, FDC, AY, GS, SD, RTC. | DeviceState reports on automation for many devices | no single place to look; no GUI boards | device boards generated from each device's published description (debugger-model "device board") | **Need** (and the spectator wow) |
| HW-2 | **Bus-level view.** Per line: who used the bus (CPU, video, DMA, sprites). | none | clone bring-up needs it | TS-Labs `Border=5` style bus-usage bars per line; WinUAE/vAmiga DMA view ([winuae](../2026-09-28-emulator-debugger-survey/winuae-debugger.md), [vamiga](../2026-09-28-emulator-debugger-survey/vamiga-debugger.md)) | **Nice** |
| HW-3 | **Compare against the real board.** Replay a recorded input and compare frame digests against captures from hardware. | TTD, screen digests, test corpus | manual | a verification run: digests per frame vs reference | **Nice** |
| HW-4 | **Port decode checks.** Which device answered this port, and why. | port trace, port diag recorder | automation only | a port board: decode rule per device, conflicts | **Nice** |

### 3.8 Learner, educator, spectator (ED)

| ID | Use case and flow | Today | Why it hurts | Target | Class |
|---|---|---|---|---|---|
| ED-1 | **Watch the machine think.** A dashboard of everything blinking: the beam, the bus, the chips, memory heat, the sound. For streams, exhibitions, attract mode. | parts exist (beam widget, page visualizer) | no "wall" view | a peripheral monitoring wall: every device board live at once, with heat and activity ([§4.1](#41-peripheral-monitoring-wall)) | **Need** (for popularity) |
| ED-2 | **Learn Z80 step by step.** Explain what this instruction did: flags, memory, timing. | registers, flags string | no explanation | instruction explainer: before/after, flags changed and why, T-states | **Nice** |
| ED-3 | **Teach with exercises.** Load a task, run the student's code, check the result. | automation can do it | no packaging | lessons as scripts with checks (GD-7 runner) | **Future** |

### 3.9 Tester, CI, AI agent (QA / AI)

| ID | Use case and flow | Today | Why it hurts | Target | Class |
|---|---|---|---|---|---|
| QA-1 | **Headless regression run.** Boot, load, play input, compare digests, exit with a code. | **works** well: WebAPI, CLI, Python, TTD, digests, recipes | strong | keep; add a documented test-runner mode with exit codes ([mesen2](../2026-09-28-emulator-debugger-survey/mesen2-debugger.md) `--testrunner`) | **Nice** |
| QA-2 | **An agent investigates a bug.** MCP: set a condition breakpoint, wait for the pause event, read a snapshot, reason. | **partial**: 15 MCP tools; no condition engine (#6); no event push (#23) | agents poll; conditions are emulated by stepping | conditions in the core; pause events pushed (debugger-model protocol §5) | **Need** |
| QA-3 | **Self-describing API.** An agent asks "what can you do on this machine?" | `list_models`, search_api, OpenAPI | capabilities per machine are not listed (#9) | a capabilities endpoint ([zx-m8xxx](../2026-09-28-emulator-debugger-survey/zx-m8xxx-debugger.md) `brief/capabilities/require`) | **Nice** |

The essentials for multi-model lockstep runs with diffs, test wrappers, the Z80 unit / integration test harness and CI / CD are role X-19 in [use-cases-extended.md](use-cases-extended.md#319-tester-and-ci-engineer-x-19).

### 3.10 Beyond debugging: atypical roles

Streamers, composers, speedrunners, archivists, translators, modders, demo
party organizers, museums, hardware designers, other emulator authors,
accessibility users, CTF authors, collaborators and dataset builders: 15 roles
with their **essential** needs (A/V calibration, picture on real displays,
clean feeds, playlists, exports, unattended operation, provenance) are in
[use-cases-extended.md](use-cases-extended.md).

---

## 4. Cross-cutting capability themes

The use cases above collapse into a small number of capabilities. Each is
built once in the core and reused by every role and every skin.

### 4.1 Peripheral monitoring wall

**Serves:** ED-1, HW-1, MU-1, DM-2, OS-4.

Every device publishes a board description (fields, units, groups), and the
debugger shows all boards live at once: ULA and beam, memory pages and heat,
AY/TS/TSFM, GS/NeoGS, FDC and disk head, tape signal, SD/IDE commands, TSConf
registers, DMA, RTC. Nothing to configure; it lights up as the program runs.

Why it matters: it is the **first impression**. A coder or a spectator opens
the debugger and sees the whole machine working. No Spectrum emulator has
this; the closest are zxsp's per-device inspectors and Xpeccy's docks, which
refresh only while paused ([zxsp](../2026-09-28-emulator-debugger-survey/zxsp-debugger.md),
[xpeccy-plus](../2026-09-28-emulator-debugger-survey/xpeccy-plus-debugger.md)).

What we have: `DeviceState` reports for most devices, on every automation
surface. Missing: the live GUI boards and a push stream at frame rate.

### 4.2 Beam, raster and event views

**Serves:** DM-1, DM-2, DM-3, MU-3, HW-2, GD-5.

- **Screen up to the beam** while stepping (TS-Labs, xpeccy `Alt+F9`, jnext).
- **Event viewer:** port writes, interrupts, contention delays, breakpoint
  marks plotted on the frame grid by (line, T-state) (Mesen2, 8BitAnalysers).
- **Bus usage per line** (TS-Labs `Border=5`, WinUAE/vAmiga DMA views).
- **Raster stepping:** run to end of line, run to pixel.

- **Frame budget line** next to the registers (seen in a reference UI,
  [R-8](../2026-09-29-model-what-if/reference-branched-ttd-ui.md#3-what-unreal-ng-takes-from-it)): interrupt acceptance time, handler
  T, code T, HALT T and T until the next interrupt, for the current frame.

What we have: beam widget, border timing widget, `DescribeBeam`, run to
pixel/scanline, port trace with T-states. Missing: the event plot, the
partial-frame render, the contention strip, the frame budget line.

### 4.3 Symbols, source and the code-change loop

**Serves:** GD-1..3, GD-8, RE-8, RE-10, OS-2, AP-2.

See [§5](#5-the-code-change-loop-in-detail) and [§6](#6-symbols-and-source-what-is-specified-what-works).

### 4.4 Conditions, triggers and actions

**Serves:** QA-2, RE-1, DM-1, OS-1, every breakpoint use.

One expression language (#6) everywhere: breakpoints, watches, trace filters,
log points, triggers. Values it must reach (union of the best debuggers): all
registers incl. shadow and WZ; memory by CPU address and by page; the last
access (address, value, read or write, port); beam (line, T-state, pixel);
frame number; hit count; paging state; device fields. Actions: stop, log,
count, snapshot, run a script, mark on the event viewer without stopping.

What we have: breakpoint kinds with bank-exact match and groups; a
ZEsarUX-protocol condition evaluator. Missing: the core condition engine (#6),
hit counts, actions.

### 4.5 Memory intelligence

**Serves:** RE-2..RE-6, GD-4, MU-4, DM-2.

A per-byte record of what happened to memory, and analysis on top of it
(and, as a glance view, a **memory activity map** of the 64 KB CPU space with
map / read / write overlays for the current frame, as in the reference UI
[R-9](../2026-09-29-model-what-if/reference-branched-ttd-ui.md#3-what-unreal-ng-takes-from-it)):

1. **Provenance:** for each byte, who read and wrote it (PCs, a bounded list),
   and the last writer. Cost: memory per byte; made affordable by recording
   only while the analysis is on, and only for RAM that is used.
2. **Code/data logging:** executed, read as data, read as an operand, jump
   target, function entry. Drives the disassembly and the export.
3. **Automatic sprite and graphics layout.** The routine that draws a sprite
   reads its source bytes in a fixed pattern and writes them to the screen.
   From the recorded reads (source addresses in order) and writes (screen
   addresses), the analyzer derives the stride, width, height, mask
   interleave and frame count, without any slider. The viewer then shows the
   sprite set already aligned. Manual alignment remains a fallback.
4. **Pattern analysis:** tables (repeating strides), pointer tables (values
   that point into code), strings, compressed blocks (entropy), screen-like
   blocks (6144 + 768).
5. **Signature analysis and knowledge bases:** a base of known routines and
   layouts, matched by byte patterns with wildcards and by behavior (what the
   routine reads and writes):
   - ROMs and ROM routines (exists as the ROM signature catalog);
   - music players (PT2/PT3/STC/ASC/SQT, GS MOD players);
   - loaders and protections (Speedlock, Alkatraz, custom tape loaders);
   - compressors (hrust, laser compact, zx7, lzsa, exomizer);
   - game engines (Filmation, Freescape, 3D Construction Kit, Arcade Game
     Designer, the Quill/PAW), and demo-system kernels;
   - typical memory layouts per engine (where the level, the sprites, the
     screen buffers live).
   A match becomes a set of labels, comments and a memory-map overlay, with a
   confidence and the evidence.

What we have: memory access counters, coverage, call trace, the ROM
signature catalog, analyzers. Missing: provenance PCs, code/data log feeding
the disassembly, layout inference, the knowledge bases.

### 4.6 Two CPUs, one clock

**Serves:** MU-2, OS-4 (card firmware), HW-1.

The GS/NeoGS card debugger: both CPUs paused at the same moment, the mailbox
between them, step one CPU while the other keeps relative time. Designed
(#45, debugger-model card debugger). The same rules later serve ZX-Poly (#43,
four Z80s) and any card with its own CPU.

### 4.7 Time: history, branches and comparison

**Serves:** GD-4, RE-2, RE-7, OS-5, QA-1.

TTD is our strongest asset (record, seek, reverse step/continue, find-last,
coverage, bookmarks). Cutting recordings to the part that matters, with a
re-computed start state and re-packed deltas, is the TTD recording editor:
[use-cases-extended §5](use-cases-extended.md#5-ttd-recording-editor-and-visualizer). Missing: branches (what-if timelines kept side by side),
run comparison (first divergence), and keeping the history across a code
reload ([§5](#5-the-code-change-loop-in-detail)).

Designed (2026-09-29): [model what-if and branched history](../2026-09-29-model-what-if/design.md). Acting in
the past (a key, a poke, a media change) starts a branch at the frame boundary
before it instead of truncating the future; replay alone changes nothing;
branches are lanes on the timeline; snapshot, tape and disk loads become
timeline events instead of ending the session; reverse playback at speed. The
idea comes from a branched-history UI seen in another emulator
([reference](../2026-09-29-model-what-if/reference-branched-ttd-ui.md)).

### 4.8 One protocol for every surface

**Serves:** QA / AI, and every skin.

The debugger model's protocol: one snapshot, one command set, pushed events.
Every GUI feature above must exist on WebAPI, MCP, CLI, Lua and Python with
the same names (project rule), so an agent can do what a person does.

---

## 5. The code-change loop in detail

The person's loop is: **edit → build → load → reach the state → observe →
edit**. Today every lap pays "load" and "reach the state" in full. The target
removes both.

| Level | What it is | Today | Target | Class |
|---|---|---|---|---|
| L1 **Patch** | change bytes or one instruction in memory, from any view | works on automation (journaled in TTD); `/assemble write:true` skips the journal and bank targeting; **the Qt GUI cannot edit at all** | patch from every view; every patch journaled and undoable | **Need** |
| L2 **Reassemble in place** | edit one routine's source; assemble it; put it where the old one was, if it fits, and move the symbols | snippet assembler (no macros, INCLUDE, pages) | routine-level reassembly with symbol re-anchoring (label-manager task 0.2.4); fits-check and a report | **Nice** |
| L3 **Hot reload a build** | the external build finished; load the new code, data, symbols and listing into the running machine at a safe point; keep registers, stack, game state | primitives only; snapshot load wipes TTD; two conflicting endpoint names in the NedoOS docs (`hot_reload`, `reload_artifact`), superseded by edit-and-replay | file watch on the build output; one atomic reload (image + symbols + listing), page-aware; a reload event in the TTD journal instead of a wipe | **Need** |
| L4 **Edit and replay** | rebuild, go back to checkpoint N, inject the new binary, replay the recorded input, report the first divergence mapped to a source line | designed (devtools §13.1), engine pieces exist (checkpoints, input journal, `ResumeRecordingFrom`) | as described; the killer feature for "did my fix change anything else?" | **Future** (the pieces exist) |
| L5 **What-if branches** | keep the old future and the new one side by side; compare them | none: a resume truncates the future | branches in TTD created on divergence, lanes on the timeline, a diff view between them ([design](../2026-09-29-model-what-if/design.md) §4) | **Future** (designed) |
| L6 **Relocation** | code loaded at a different address (NedoOS apps, overlays, modules); symbols and breakpoints follow | designed only (devtools §8.3, Phase 6) | module load events, relocation deltas applied to symbols and breakpoints | **Need** for NedoOS, **Future** otherwise |
| L7 **Asset hot-swap** | replace one `INCBIN` blob in the running program | designed (devtools §13.3) | reload a blob to its label's address | **Future** |

The effect looper (loop a range through TTD, fork on every change, compare forks, export from any fork as binary, script output or a source diff) is the demo maker's form of L4-L5: [use-cases-extended X18-26 to X18-32](use-cases-extended.md#318-animator-and-demo-effect-maker-x-18).

**Safe points.** L2-L7 write into a running machine. They apply at an
instruction boundary on the emulation thread (the same path live input uses),
are journaled as TTD external events, and refuse while a replay owns the
machine. This is already how GS stimuli and debugger edits work.

## 6. Symbols and source: what is specified, what works

The question "do the requirements clearly describe assembler label formats,
hot code change, reassembly, hot reload, relocation?" has a short answer:
**partly, and the documents disagree with the code in several places.**

| Topic | Clearly specified | Vague or contradictory | Missing | Code today |
|---|---|---|---|---|
| **Assembler label formats** | generic MAP/SYM/VICE shapes ([label-manager.md](../../emulator/design/debugger/label-manager.md)); per-CPU GS `.map` with page prefixes ([gs-debugger requirements](../2026-09-27-gs-debugger/requirements.md) L1-L4) | "sjasmplus / SJASM / z88dk supported"; MCP and WebAPI texts claim `.sld` and `.lbl`; `command-interface.md` calls SLD the primary format | real z88dk `.map`, Pasmo, rasm, zmac, SkoolKit, MAME, NoICE, Fuse, ZXMAK, ZEsarUX, Unreal `user.l`, XAS/ALASM; a test corpus per format | MAP, simple SYM, VICE, old SJASM (`.s`/`.asm` only), z88dk `DEFC` (`.z88` only); **a real sjasmplus `.sym` is silently dropped**; labels store the bank but lookup ignores it |
| **SLD / source level** | the SLD v1 target with pages, modules and `K` triggers ([devtools toolchain design](../2026-09-21-devtools-roadmap/unreal-ng-developer-toolchain-design.md) §8-§9) | whether `ListingParser` is "legacy"; no source view requirement for any skin | SLD parser (postponed task 0.2.2), multi-file listings, banked addresses, DAP | `.lst`, one file, flat 64 KB; SLD only through DeZog |
| **Patch / reassembly** | snippet assembler and memory writes on every surface | symbol re-anchoring after reassembly (0.2.4); whether assembled symbols feed the label store | Qt patching; project reassembly | works on automation; `/assemble write:true` bypasses the TTD journal |
| **Hot reload** | none | two NedoOS endpoint names; superseded by edit-and-replay | file watch; raw binary load at an address; atomic image + symbols + listing reload; history kept | primitives only; snapshot load wipes TTD |
| **Relocation** | design level only (devtools §8.3, Phase 6) | | relocatable format, non-NedoOS cases | none |

## 7. Why today's coverage is inefficient

1. **Automation-first, GUI-last.** Most analysis exists only on WebAPI / MCP /
   CLI / Lua / Python. A person at the Qt debugger cannot edit memory, see
   source, see provenance, see device boards or see the event plot. The
   capability exists; the person never meets it.
2. **Numbers instead of pictures.** Port trace, AY log, access counters and
   call trace produce lists. Timing and layout problems are spatial; the eye
   needs the frame map and the memory map.
3. **Silent failures.** The sjasmplus `.sym` load reports success and loads
   nothing. People stop trusting the tool.
4. **Every lap pays in full.** No hot reload: each code change costs a reload
   and a manual walk back to the state under test.
5. **Paging is half-done.** Breakpoints can match banks; labels, comments,
   the listing and the disassembly cannot. On 128K and larger machines, notes
   show on the wrong code.
6. **Knowledge is not captured.** Every reverse engineer re-discovers the same
   players, loaders, compressors and engines. Nothing is recorded or shared.
7. **One CPU at a time.** The card CPU and the main CPU cannot be observed
   together (designed, not built).
8. **Promises in the docs.** Several documents describe features as present
   that are not (SLD, `.lbl`, Pasmo/TASM `.map`). The gap costs trust.

## 8. Summary matrix

Rows: capability themes. Columns: roles. **●** core need, **○** useful, blank:
not relevant.

| Theme | GD | DM | RE | OS | AP | MU | HW | ED | QA/AI |
|---|---|---|---|---|---|---|---|---|---|
| 4.1 Peripheral wall | ○ | ○ | ○ | ○ | | ● | ● | ● | |
| 4.2 Beam, raster, events | ○ | ● | ○ | | | ● | ● | ● | ○ |
| 4.3 Symbols, source, code-change loop | ● | ● | ● | ● | ● | ○ | | ○ | ○ |
| 4.4 Conditions, triggers, actions | ● | ● | ● | ● | ○ | ○ | ● | | ● |
| 4.5 Memory intelligence | ○ | | ● | ○ | | ○ | | ○ | ○ |
| 4.6 Two CPUs, one clock | | | ○ | ○ | | ● | ● | ○ | |
| 4.7 Time, branches, comparison | ● | ○ | ● | ● | ○ | ○ | ● | | ● |
| 4.8 One protocol | ○ | | ○ | ○ | | | ○ | | ● |

## 9. Defects found while writing this

These are recorded here and belong in PLAN as bugs; they are not fixed by this
document:

1. A real sjasmplus `.sym` (`label: EQU 0x…`) is routed to the simple SYM
   parser, every line is skipped, and the load reports success
   (`core/src/debugger/labels/labelmanager.cpp` `DetectFileFormat` /
   `ParseSymFile`).
2. `.sld` and `.lbl` are advertised by the WebAPI comments, the MCP
   `manage_symbols` description and `command-interface.md` §4.4, but are not
   recognized by the loader.
3. `command-interface.md` claims Pasmo, TASM and Z80ASM `.map` support and SLD
   saving; only the generic MAP parser exists, and saving writes SYM or MAP.
4. `POST /assemble` with `write:true` writes through `MemoryWriteFast` at the
   current mapping: no TTD `DebuggerEdit` marker, no bank targeting, unlike
   every other memory-write path.
5. Label lookup ignores the stored bank (`GetLabelByZ80Address` is a plain
   16-bit map); label-manager.md UC2 is unmet.
6. The NedoOS documents name the hot-reload endpoint twice, differently
   (`hot_reload` in the SDLC document, `reload_artifact` in the struct-DSL
   document).

## 10. Glossary

| Term | Meaning |
|---|---|
| **SLD** | sjasmplus Source Level Debugging file: maps every address (with its page) to a source file and line. |
| **Listing (`.lst`)** | the assembler's printed output: address, bytes and source line side by side. |
| **Code/data log (CDL)** | a per-byte record of how the byte was used: executed, read as data, jumped to. |
| **Provenance** | for a byte: which instructions read or wrote it. |
| **Event viewer** | a picture of one frame where each event (port write, interrupt, contention) is a dot at its line and T-state. |
| **Device board** | a live panel of one device's registers and state, drawn from a description the device publishes. |
| **Hot reload** | loading a rebuilt program into a running machine without rebooting it. |
| **Edit and replay** | going back in recorded history, applying a new build, and replaying the recorded input to see what changed. |
| **Relocation** | loading code at an address other than the one it was built for, and fixing its addresses and symbols accordingly. |
| **Safe point** | an instruction boundary on the emulation thread where external changes are applied and journaled. |
| **Signature** | a byte pattern (with wildcards) or a behavior pattern that identifies a known routine, player, loader, compressor or engine. |
